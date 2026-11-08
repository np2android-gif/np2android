/**
 * @file	mousemng.c
 * @brief	Android 用マウス管理 (np2/sdl2/mousemng.c を置き換える)
 *
 * 移動量は float で蓄積し、整数部だけをエミュレータへ渡す
 * (タッチパッド操作でゆっくり動かしたときに端数が失われないように)。
 */

#include "compiler.h"
#include "mousemng.h"
#if !defined(NDEBUG)
#include <android/log.h>
#endif

typedef struct {
	float	x;
	float	y;
	UINT8	btn;					/* 押下中のビットが 0 */
	UINT32	release_at[2];			/* クリック自動解除時刻 (0 = なし) */
} MOUSEMNG;

static MOUSEMNG s_mouse;

static const UINT8 s_bits[2] = { uPD8255A_LEFTBIT, uPD8255A_RIGHTBIT };

void mousemng_initialize(void)
{
	ZeroMemory(&s_mouse, sizeof(s_mouse));
	s_mouse.btn = uPD8255A_LEFTBIT | uPD8255A_RIGHTBIT;
}

UINT8 mousemng_getstat(SINT16 *x, SINT16 *y, int clear)
{
	int ix = (int)s_mouse.x;
	int iy = (int)s_mouse.y;

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
#if !defined(NDEBUG)
	{
		/* コアが読み取った値の確認用 (ボタン変化時と移動時のみ) */
		static UINT8 lastbtn = uPD8255A_LEFTBIT | uPD8255A_RIGHTBIT;
		if ((s_mouse.btn != lastbtn) || ((ix | iy) && clear)) {
			__android_log_print(ANDROID_LOG_DEBUG, "np2mouse", "getstat dx=%d dy=%d btn=%02x", ix, iy, s_mouse.btn);
			lastbtn = s_mouse.btn;
		}
	}
#endif
	return s_mouse.btn;		/* 未押下時 0xa0 (上流 sdl2 版・旧 Android 版と同じ) */
}

void mousemng_move(float dx, float dy)
{
	s_mouse.x += dx;
	s_mouse.y += dy;
}

void mousemng_button(int button, BOOL down)
{
	if ((button < 0) || (button > 1)) {
		return;
	}
	s_mouse.release_at[button] = 0;
	if (down) {
		s_mouse.btn &= ~s_bits[button];
	}
	else {
		s_mouse.btn |= s_bits[button];
	}
}

void mousemng_click(int button, UINT32 duration)
{
	if ((button < 0) || (button > 1)) {
		return;
	}
	s_mouse.btn &= ~s_bits[button];
	s_mouse.release_at[button] = SDL_GetTicks() + duration;
	if (s_mouse.release_at[button] == 0) {
		s_mouse.release_at[button] = 1;
	}
}

void mousemng_releaseall(void)
{
	s_mouse.btn = uPD8255A_LEFTBIT | uPD8255A_RIGHTBIT;
	s_mouse.release_at[0] = 0;
	s_mouse.release_at[1] = 0;
}

/** taskmng_rol から毎回呼び、クリックの自動解除を行う */
void mousemng_update(void)
{
	UINT32 now = SDL_GetTicks();
	int i;

	for (i = 0; i < 2; i++) {
		if ((s_mouse.release_at[i] != 0) &&
			((SINT32)(now - s_mouse.release_at[i]) >= 0)) {
			s_mouse.release_at[i] = 0;
			s_mouse.btn |= s_bits[i];
		}
	}
}
