/**
 * @file	mousemng.h
 * @brief	Android 用マウス管理 (np2/sdl2/mousemng.h を置き換える)
 *
 * すべての関数はエミュレータスレッド (SDL_main) からのみ呼ぶこと。
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
	uPD8255A_LEFTBIT	= 0x80,
	uPD8255A_RIGHTBIT	= 0x20
};

enum {
	MOUSEMNG_LEFT	= 0,
	MOUSEMNG_RIGHT	= 1
};

/* ANDROID-MOUSE: ボタンを押している理由 (どれか 1 つでも押していれば、ゲストには押したと見せる) */
enum {
	MOUSEMNG_BYKEY		= 0x01,		/* ソフトキー・物理キーボードの L・R (Np2Native.mouseButton) */
	MOUSEMNG_BYTOUCH	= 0x02,		/* 画面のタッチ (タップして押し直すドラッグ) */
	MOUSEMNG_BYMOUSE	= 0x04		/* 外部マウス */
};

UINT8 mousemng_getstat(SINT16 *x, SINT16 *y, int clear);

void mousemng_initialize(void);
void mousemng_move(float dx, float dy);
void mousemng_hold(int button, UINT by, BOOL down);
BOOL mousemng_isheld(int button, UINT by);
void mousemng_click(int button);
void mousemng_releaseall(void);
void mousemng_update(void);

#ifdef __cplusplus
}
#endif
