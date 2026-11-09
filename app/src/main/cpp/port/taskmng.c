/**
 * @file	taskmng.c
 * @brief	Android 用イベント処理 (np2/sdl2/taskmng.c を置き換える)
 *
 * タッチ操作 (np2 メニューが閉じているとき) はタッチパッド方式:
 *   - 1 本指ドラッグ        : マウス移動 (相対)
 *   - 1 本指タップ          : 左クリック
 *   - タップ直後に押して保持 : 左ボタンを押したままドラッグ (ダブルタップ＝ダブルクリック)
 *   - 2 本指タップ          : 右クリック
 * np2 メニューが開いているときは、SDL がタッチから合成するマウスイベント
 * (論理座標 640x400) でメニューを操作する。
 */

#include "compiler.h"
#include "inputmng.h"
#include "taskmng.h"
#include "sdlkbd.h"
#include "vramhdl.h"
#include "menubase.h"
#include "sysmenu.h"
#include "keystat.h"
#include "mousemng.h"
#include "np2bridge.h"
#include "scrndraw.h"
#include "np2.h"
#include "fdd/fddfile.h"
#include "fdd/fdd_d88.h"
#include "fdd/sxsi.h"
#include "android_safio.h"

	BOOL	task_avail;

#if !defined(NDEBUG)
#include <android/log.h>
#define	TOUCHLOG(...)	__android_log_print(ANDROID_LOG_DEBUG, "np2touch", __VA_ARGS__)
#else
#define	TOUCHLOG(...)
#endif

/* ---- タッチパッド設定 ---- */
#define	TAP_MAXTIME			250		/* ms: これより長い押下はタップ扱いしない */
#define	TAP_MAXMOVE			6.0f	/* PC-98 画素: これより動いたらタップではない */
#define	DOUBLETAP_GAP		300		/* ms: 前回タップからこの時間内に押すとドラッグ */
#define	CLICK_DURATION		60		/* ms: タップで送るクリックの長さ */
#define	MAX_FINGERS			5

typedef struct {
	int				count;					/* 現在触れている指の数 */
	SDL_FingerID	ids[MAX_FINGERS];
	SDL_FingerID	primary;				/* カーソルを動かす指 */
	BOOL			hasprimary;
	UINT32			downtime;				/* 最初の指が触れた時刻 */
	float			moved;					/* 最初の指の累積移動量 (PC-98 画素) */
	int				maxcount;				/* このジェスチャ中の最大指数 */
	BOOL			dragging;				/* 左ボタン保持中 */
	UINT32			lasttap;				/* 直近のタップ時刻 */
} TOUCHPAD;

static	TOUCHPAD	s_touch;

/*
 * 画面の回転・復帰で描画面が作り直された後の再描画。
 * サイズ変更イベントは新しい描画面の準備より先に届くことがあり (Pixel 9a で確認)、
 * その時点で 1 回描いても表示に反映されない。しばらくの間、定期的に全体を描き直す。
 */
#define	REDRAW_PERIOD		1500	/* ms */
#define	REDRAW_INTERVAL		250		/* ms */
static	UINT32		s_redrawuntil;
static	UINT32		s_lastredraw;

static void redraw_all(void) {

	if (menuvram != NULL) {
		menubase_setrect(menuvram, NULL);
		menubase_draw(NULL, NULL);
	}
	else {
		scrndraw_redraw();
	}
}

static void redraw_request(void) {

	s_redrawuntil = SDL_GetTicks() + REDRAW_PERIOD;
	s_lastredraw = 0;
}

void taskmng_requestredraw(void) {

	redraw_request();
}

static void redraw_update(void) {

	UINT32 now;

	if (s_redrawuntil == 0) {
		return;
	}
	now = SDL_GetTicks();
	if ((SINT32)(now - s_redrawuntil) >= 0) {
		s_redrawuntil = 0;
		return;
	}
	if ((s_lastredraw == 0) || ((now - s_lastredraw) >= REDRAW_INTERVAL)) {
		s_lastredraw = now;
		redraw_all();
	}
}


void sighandler(int signo) {

	(void)signo;
	task_avail = FALSE;
}

void taskmng_initialize(void) {

	task_avail = TRUE;
	taskmng_userexit = FALSE;
	ZeroMemory(&s_touch, sizeof(s_touch));
	mousemng_initialize();
}

/** ユーザー操作 (メニューの終了) で止めたか。SDL_QUIT (Activity 破棄) と区別する */
	BOOL	taskmng_userexit;

void taskmng_exit(void) {

	task_avail = FALSE;
	taskmng_userexit = TRUE;
}

/* ---- touch ---- */

/** ウィンドウ上の正規化移動量 → PC-98 画素 (表示倍率に合わせ、指の移動と画面上のカーソル移動が一致する) */
static void touch_topc98(const SDL_TouchFingerEvent *f, float *dx, float *dy) {

	SDL_Window	*win;
	int			w = 0;
	int			h = 0;
	float		scale;

	win = SDL_GetWindowFromID(f->windowID);
	if (win == NULL) {
		win = SDL_GetMouseFocus();
	}
	if (win != NULL) {
		SDL_GetWindowSize(win, &w, &h);
	}
	if ((w <= 0) || (h <= 0)) {
		*dx = f->dx * 640.0f;
		*dy = f->dy * 400.0f;
		return;
	}
	/* 640x400 をアスペクト維持で表示したときの 1 PC-98 画素あたりの画面画素 */
	scale = min((float)w / 640.0f, (float)h / 400.0f);
	*dx = f->dx * (float)w / scale;
	*dy = f->dy * (float)h / scale;
}

static int finger_index(SDL_FingerID id) {

	int i;

	for (i = 0; i < s_touch.count; i++) {
		if (s_touch.ids[i] == id) {
			return i;
		}
	}
	return -1;
}

static void touch_reset(void) {

	if (s_touch.dragging) {
		mousemng_button(MOUSEMNG_LEFT, FALSE);
	}
	ZeroMemory(&s_touch, sizeof(s_touch));
}

static void touch_down(const SDL_TouchFingerEvent *f) {

	UINT32 now = SDL_GetTicks();

	if ((finger_index(f->fingerId) >= 0) || (s_touch.count >= MAX_FINGERS)) {
		return;
	}
	s_touch.ids[s_touch.count++] = f->fingerId;
	if (s_touch.count > s_touch.maxcount) {
		s_touch.maxcount = s_touch.count;
	}

	if (s_touch.count == 1) {
		s_touch.primary = f->fingerId;
		s_touch.hasprimary = TRUE;
		s_touch.downtime = now;
		s_touch.moved = 0.0f;
		s_touch.maxcount = 1;
		if ((s_touch.lasttap != 0) && ((now - s_touch.lasttap) < DOUBLETAP_GAP)) {
			/* タップ直後にもう一度押した → ボタンを押したままドラッグ */
			s_touch.dragging = TRUE;
			mousemng_button(MOUSEMNG_LEFT, TRUE);
			TOUCHLOG("drag start (left down)");
		}
		s_touch.lasttap = 0;
	}
}

static void touch_motion(const SDL_TouchFingerEvent *f) {

	float dx;
	float dy;

	if ((!s_touch.hasprimary) || (f->fingerId != s_touch.primary)) {
		return;
	}
	/* 2 本指で触れている間はカーソルを動かさない (右クリック操作中) */
	if ((s_touch.count > 1) && (!s_touch.dragging)) {
		return;
	}
	touch_topc98(f, &dx, &dy);
	s_touch.moved += SDL_fabsf(dx) + SDL_fabsf(dy);
	mousemng_move(dx * np2bridge_mousespeed, dy * np2bridge_mousespeed);
}

static void touch_up(const SDL_TouchFingerEvent *f) {

	UINT32	now = SDL_GetTicks();
	int		idx;
	BOOL	quick;

	idx = finger_index(f->fingerId);
	if (idx < 0) {
		return;
	}
	s_touch.ids[idx] = s_touch.ids[--s_touch.count];

	if (f->fingerId == s_touch.primary) {
		s_touch.hasprimary = FALSE;
		if (s_touch.dragging) {
			s_touch.dragging = FALSE;
			mousemng_button(MOUSEMNG_LEFT, FALSE);
			s_touch.maxcount = 0;		/* このジェスチャではクリックを出さない */
			TOUCHLOG("drag end (left up), moved=%.1f", s_touch.moved);
		}
	}
	if (s_touch.count > 0) {
		return;
	}

	/* 全部の指が離れた: ジェスチャ判定 */
	quick = ((now - s_touch.downtime) < TAP_MAXTIME) && (s_touch.moved < TAP_MAXMOVE);
	if (quick && (s_touch.maxcount == 1)) {
		mousemng_click(MOUSEMNG_LEFT, CLICK_DURATION);
		s_touch.lasttap = now;
		TOUCHLOG("tap -> left click");
	}
	else if (quick && (s_touch.maxcount == 2)) {
		mousemng_click(MOUSEMNG_RIGHT, CLICK_DURATION);
		TOUCHLOG("2-finger tap -> right click");
	}
	else if (s_touch.maxcount > 0) {
		TOUCHLOG("move: fingers=%d moved=%.1f time=%u", s_touch.maxcount, s_touch.moved, now - s_touch.downtime);
	}
	s_touch.maxcount = 0;
}

/* ---- main ---- */

/*
 * 1 回の呼び出しで溜まっているイベントをまとめて処理する。
 * 上流は 1 回に 1 個だけで、エミュレーションが重くて実時間に追いつかないとき
 * (taskmng_rol が 1 フレームに 1 回しか呼ばれないとき) 指の移動イベント (120Hz 前後) が
 * 溜まり、操作が遅れて見えていた。イベントが途切れず届き続けても抜けられるよう上限を設ける。
 */
#define	MAX_EVENTS_PER_ROL	256

void taskmng_rol(void) {

	SDL_Event	e;
	int			n;

	mousemng_update();
	redraw_update();

	for (n = 0; n < MAX_EVENTS_PER_ROL; n++) {
		if ((!task_avail) || (!SDL_PollEvent(&e))) {
			return;
		}
		switch(e.type) {
			case SDL_USEREVENT:
				np2bridge_handle(&e);
				break;

			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
			case SDL_FINGERUP:
				/* 外部マウスから合成されたタッチは無視 (マウスイベント側で処理) */
				if ((e.tfinger.touchId == SDL_MOUSE_TOUCHID) || (menuvram != NULL)) {
					break;
				}
				if (e.type == SDL_FINGERDOWN) {
					touch_down(&e.tfinger);
				}
				else if (e.type == SDL_FINGERMOTION) {
					touch_motion(&e.tfinger);
				}
				else {
					touch_up(&e.tfinger);
				}
				break;

			case SDL_MOUSEMOTION:
				if (menuvram != NULL) {
					menubase_moving(e.motion.x, e.motion.y, 0);
				}
				else if (e.motion.which != SDL_TOUCH_MOUSEID) {
					/* 外部マウス */
					mousemng_move((float)e.motion.xrel, (float)e.motion.yrel);
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if (menuvram != NULL) {
					if (e.button.button == SDL_BUTTON_LEFT) {
						menubase_moving(e.button.x, e.button.y,
										(e.type == SDL_MOUSEBUTTONDOWN) ? 1 : 2);
					}
				}
				else if (e.button.which != SDL_TOUCH_MOUSEID) {
					/* 外部マウス */
					if (e.button.button == SDL_BUTTON_LEFT) {
						mousemng_button(MOUSEMNG_LEFT, e.type == SDL_MOUSEBUTTONDOWN);
					}
					else if (e.button.button == SDL_BUTTON_RIGHT) {
						mousemng_button(MOUSEMNG_RIGHT, e.type == SDL_MOUSEBUTTONDOWN);
					}
				}
				break;

			case SDL_KEYDOWN:
				if (e.key.keysym.sym == SDLK_F11) {
					if (menuvram == NULL) {
						sysmenu_menuopen(0, 0, 0);
					}
					else {
						menubase_close();
					}
				}
				else {
					sdlkbd_keydown(e.key.keysym.sym);
				}
				break;

			case SDL_KEYUP:
				sdlkbd_keyup(e.key.keysym.sym);
				break;

			case SDL_WINDOWEVENT:
				switch (e.window.event) {
					case SDL_WINDOWEVENT_FOCUS_LOST:
						/* フォーカスを失ったら押しっぱなしを防ぐ */
						keystat_allrelease();
						mousemng_releaseall();
						touch_reset();
						break;

					case SDL_WINDOWEVENT_SIZE_CHANGED:
					case SDL_WINDOWEVENT_RESIZED:
					case SDL_WINDOWEVENT_EXPOSED:
					case SDL_WINDOWEVENT_SHOWN:
					case SDL_WINDOWEVENT_RESTORED:
					case SDL_WINDOWEVENT_FOCUS_GAINED:
						/*
						 * 画面の回転・復帰で描画面が作り直されると表示が消える。np2 は変化した部分しか
						 * 描かないため、静止画面 (DOS のプロンプト等) では黒いままになるので全体を描き直す。
						 */
						redraw_request();
						break;
				}
				break;

			case SDL_APP_DIDENTERBACKGROUND:
				/*
				 * バックグラウンドへ移った。Android はこの後プロセスを予告なく止めることがあり
				 * np2_main 終了時のレジューム保存は当てにできないので、ここで保存しておく。
				 * SDL はこのイベントをアプリが受け取るまでスレッドを止めない (SDL_androidevents.c) ので、
				 * ここでの保存は止められる前に必ず終わる。
				 *
				 * ディスクの書き込みはレジューム保存より先にファイルへ出す。レジュームにはイメージの
				 * 更新日時が記録されるので、後から書くと次回起動時に「ディスクが更新された」扱いになる。
				 * D88 のトラックバッファは次のアクセスか取り出しまで書き戻されず、HDD も FILE* の
				 * バッファに残っているので、ここで出してから SAF の fd を fsync する。
				 */
				fddd88_flush();
				sxsi_allflash();
				np2_saveresume();
				safio_syncall();
				break;

			case SDL_QUIT:
				task_avail = FALSE;
				break;
		}
	}
}

BOOL taskmng_sleep(UINT32 tick) {

	UINT32	base;

	base = GETTICK();
	while((task_avail) && ((GETTICK() - base) < tick)) {
		taskmng_rol();
		SDL_Delay(1);
	}
	return(task_avail);
}
