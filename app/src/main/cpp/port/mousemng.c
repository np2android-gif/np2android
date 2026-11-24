/**
 * @file	mousemng.c
 * @brief	Android 用マウス管理 (np2/sdl2/mousemng.c を置き換える)
 *
 * 移動量は float で蓄積し、整数部だけをエミュレータへ渡す
 * (タッチパッド操作でゆっくり動かしたときに端数が失われないように)。
 *
 * ANDROID-MOUSE: ボタン (Q14)
 * - 押している理由 (ソフトキー・物理キーの L・R、画面のタッチ、外部マウス) を別々に持ち、
 *   どれか 1 つでも押していればゲストには押したと見せる。1 つが離れても、他が押していれば離さない。
 * - クリック (画面のタップ) は数を溜め、「押す・離す」を 1 回ずつゲストに見せる。
 * - ゲストがボタンを読むのは 1 フレームに 1 回 (pccore.c の mouseif_sync → mousemng_getstat) で、
 *   重いときは実時間で数十 ms あく。その間に押して離すと、ゲストからは押したことが見えない。
 *   ゲストに見せる状態は、変えてから MIN_HOLD ms たち、かつゲストが 1 回以上読むまで次へ変えない。
 *   これで短い押下も抜けず、「離す」と次の「押す」も続けて読み落とされない。
 */

#include "compiler.h"
#include "mousemng.h"
#if !defined(NDEBUG)
#include <android/log.h>
#endif

/** ゲストに見せるボタンの状態を変えてから、次に変えるまでの最小の時間 (ms) */
#define	MIN_HOLD	60

/** 溜めておくクリックの数の上限 (ダブルクリックの分) */
#define	MAX_CLICKS	2

typedef struct {
	UINT	holders;				/* 押している理由 (MOUSEMNG_BY*) */
	int		clicks;					/* まだ見せていないクリックの数 */
	BOOL	inclick;				/* クリックの「押す」を見せている */
	BOOL	down;					/* ゲストに見せている状態 */
	UINT32	changed;				/* down を変えた時刻 */
	UINT	samples;				/* down を変えてからゲストが読んだ回数 */
} MOUSEBTN;

typedef struct {
	float		x;
	float		y;
	MOUSEBTN	b[2];
} MOUSEMNG;

static MOUSEMNG s_mouse;

static const UINT8 s_bits[2] = { uPD8255A_LEFTBIT, uPD8255A_RIGHTBIT };

static void btn_set(MOUSEBTN *b, BOOL down, UINT32 now)
{
	b->down = down;
	b->changed = now;
	b->samples = 0;
}

/** ゲストに見せる状態を、押している理由と溜まったクリックに 1 段だけ近づける */
static void btn_step(MOUSEBTN *b, UINT32 now)
{
	if ((b->samples == 0) || ((now - b->changed) < MIN_HOLD)) {
		return;
	}
	if (b->down) {
		if (b->inclick) {
			/* クリックの「押す」を見せ終えた。他の理由で押していれば、押したままにする */
			b->inclick = FALSE;
			b->clicks--;
			if (b->holders == 0) {
				btn_set(b, FALSE, now);
			}
		}
		else if (b->holders == 0) {
			btn_set(b, FALSE, now);
		}
	}
	else {
		if (b->clicks > 0) {
			b->inclick = TRUE;
			btn_set(b, TRUE, now);
		}
		else if (b->holders != 0) {
			btn_set(b, TRUE, now);
		}
	}
}

static void step_all(void)
{
	UINT32 now = SDL_GetTicks();

	btn_step(&s_mouse.b[0], now);
	btn_step(&s_mouse.b[1], now);
}

void mousemng_initialize(void)
{
	ZeroMemory(&s_mouse, sizeof(s_mouse));
	s_mouse.b[0].samples = 1;
	s_mouse.b[1].samples = 1;
}

UINT8 mousemng_getstat(SINT16 *x, SINT16 *y, int clear)
{
	int ix = (int)s_mouse.x;
	int iy = (int)s_mouse.y;
	UINT8 btn;
	int i;

	if (ix < -32768) ix = -32768;
	if (ix > 32767) ix = 32767;
	if (iy < -32768) iy = -32768;
	if (iy > 32767) iy = 32767;
	*x = (SINT16)ix;
	*y = (SINT16)iy;
	if (clear) {
		/* 渡した整数部だけ減らし、端数は次回へ持ち越す */
		s_mouse.x -= (float)ix;
		s_mouse.y -= (float)iy;
	}
	btn = uPD8255A_LEFTBIT | uPD8255A_RIGHTBIT;	/* 押下中のビットが 0 */
	for (i = 0; i < 2; i++) {
		if (s_mouse.b[i].down) {
			btn &= ~s_bits[i];
		}
		if (s_mouse.b[i].samples < 0x10000) {
			s_mouse.b[i].samples++;
		}
	}
#if !defined(NDEBUG)
	{
		/* コアが読み取った値の確認用 (ボタン変化時と移動時のみ) */
		static UINT8 lastbtn = uPD8255A_LEFTBIT | uPD8255A_RIGHTBIT;
		if ((btn != lastbtn) || ((ix | iy) && clear)) {
			__android_log_print(ANDROID_LOG_DEBUG, "np2mouse", "getstat dx=%d dy=%d btn=%02x", ix, iy, btn);
			lastbtn = btn;
		}
	}
#endif
	return btn;		/* 未押下時 0xa0 (上流 sdl2 版・旧 Android 版と同じ) */
}

void mousemng_move(float dx, float dy)
{
	s_mouse.x += dx;
	s_mouse.y += dy;
}

/** 理由 by でボタンを押す・離す */
void mousemng_hold(int button, UINT by, BOOL down)
{
	if ((button < 0) || (button > 1)) {
		return;
	}
	if (down) {
		s_mouse.b[button].holders |= by;
	}
	else {
		s_mouse.b[button].holders &= ~by;
	}
	step_all();
}

/** 理由 by でボタンを押しているか */
BOOL mousemng_isheld(int button, UINT by)
{
	if ((button < 0) || (button > 1)) {
		return FALSE;
	}
	return (s_mouse.b[button].holders & by) ? TRUE : FALSE;
}

/**
 * クリック (押す・離す) を 1 回送る。ゲストがマウスを読まない間 (重い処理の途中など) に何度もタップしても、
 * 後からクリックが続けて出ないよう、溜めるのはダブルクリックの分 (MAX_CLICKS) までにする
 */
void mousemng_click(int button)
{
	if ((button < 0) || (button > 1)) {
		return;
	}
	if (s_mouse.b[button].clicks < MAX_CLICKS) {
		s_mouse.b[button].clicks++;
	}
	step_all();
}

void mousemng_releaseall(void)
{
	UINT32 now = SDL_GetTicks();
	int i;

	for (i = 0; i < 2; i++) {
		s_mouse.b[i].holders = 0;
		s_mouse.b[i].clicks = 0;
		s_mouse.b[i].inclick = FALSE;
		if (s_mouse.b[i].down) {
			btn_set(&s_mouse.b[i], FALSE, now);
		}
	}
}

/** taskmng_rol から毎回呼び、ゲストに見せる状態を進める */
void mousemng_update(void)
{
	step_all();
}
