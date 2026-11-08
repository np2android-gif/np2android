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

UINT8 mousemng_getstat(SINT16 *x, SINT16 *y, int clear);

void mousemng_initialize(void);
void mousemng_move(float dx, float dy);
void mousemng_button(int button, BOOL down);
void mousemng_click(int button, UINT32 duration);
void mousemng_releaseall(void);
void mousemng_update(void);

#ifdef __cplusplus
}
#endif
