/**
 * @file	androidfont.c
 * @brief	ANDROID-FONT: font.tmp を作るときに使う字の形 (Q15)
 *
 * xnp2 は font.tmp (漢字 ROM の代わり) を fontmake.c の makepc98bmp で作り、字の形を fontmng から
 * もらう。Android 版の fontmng (sdl2/fontmng.c) は RESOURCE_US で英数字 (ank12) しか持たないので、
 * 漢字・かな・半角カナが「?」になっていた。
 * font.tmp を作るときだけ、字の形を Kotlin (Np2Native.renderFontGlyphs → FontGlyphs.kt) で端末の
 * 日本語フォントから描いてもらい、ここから makepc98bmp に渡す。PC-98 に無い文字の除外・78/83 JIS の
 * 入れ替え・内蔵の字の上書きは makepc98bmp のまま。内蔵メニューの文字 (fontmng) は変えない。
 *
 * Kotlin から受け取るバイト列の形 (1 の画素が字):
 *   [0, 4096)          半角 256 文字 x 16 行。1 行 1 バイト (bit7 が左端)。番号は Shift JIS の 1 バイト
 *   [4096, +282752)    全角 94 区 x 94 点 x 16 行。1 行 2 バイト (左 8 画素、右 8 画素)
 * フォントに無い文字は 0 (空白) で来る。
 */

#include "compiler.h"
#include <android/log.h>
#include "androidfont.h"
#include "np2bridge.h"

#define	ANK_SIZE	(256 * 16)
#define	KNJ_SIZE	(94 * 94 * 32)

typedef struct {
	UINT8	*glyphs;
	_FNTDAT	head;
	UINT8	pixels[16 * 16];
} ANDROIDFONT;

void *androidfont_create(void)
{
	ANDROIDFONT *ret;
	UINT8 *glyphs;
	UINT size;
	UINT32 start = SDL_GetTicks();

	glyphs = np2bridge_renderfont(&size);
	if (glyphs == NULL) {
		__android_log_print(ANDROID_LOG_INFO, "np2font", "font.tmp: no Japanese font, using the built-in ANK font");
		return NULL;
	}
	if (size != ANK_SIZE + KNJ_SIZE) {
		__android_log_print(ANDROID_LOG_ERROR, "np2font", "font.tmp: bad glyph table size %u", size);
		_MFREE(glyphs);
		return NULL;
	}
	ret = (ANDROIDFONT *)_MALLOC(sizeof(ANDROIDFONT), "android font");
	if (ret == NULL) {
		_MFREE(glyphs);
		return NULL;
	}
	ZeroMemory(ret, sizeof(*ret));
	ret->glyphs = glyphs;
	__android_log_print(ANDROID_LOG_INFO, "np2font", "font.tmp: glyphs rendered in %u ms", (unsigned)(SDL_GetTicks() - start));
	return ret;
}

void androidfont_destroy(void *hdl)
{
	ANDROIDFONT *af = (ANDROIDFONT *)hdl;

	if (af != NULL) {
		_MFREE(af->glyphs);
		_MFREE(af);
	}
}

/** Shift JIS の 2 バイトを、区・点 (0 から) にする。全角でなければ FALSE */
static BOOL sjis2kuten(UINT s1, UINT s2, UINT *ku, UINT *ten)
{
	if ((s1 >= 0x81) && (s1 <= 0x9f)) {
		s1 -= 0x81;
	}
	else if ((s1 >= 0xe0) && (s1 <= 0xef)) {
		s1 -= 0xc1;
	}
	else {
		return FALSE;
	}
	if ((s2 < 0x40) || (s2 > 0xfc) || (s2 == 0x7f)) {
		return FALSE;
	}
	if (s2 >= 0x9f) {
		*ku = (s1 * 2) + 1;
		*ten = s2 - 0x9f;
	}
	else {
		*ku = s1 * 2;
		*ten = s2 - 0x40 - ((s2 >= 0x80) ? 1 : 0);
	}
	return (*ku < 94) && (*ten < 94);
}

FNTDAT androidfont_get(void *hdl, const char *sjis)
{
	ANDROIDFONT *af = (ANDROIDFONT *)hdl;
	const UINT8 *s = (const UINT8 *)sjis;
	const UINT8 *src;
	UINT8 *dst;
	UINT ku;
	UINT ten;
	UINT row;
	int width;
	int x;
	int y;

	if ((af == NULL) || (s == NULL) || (s[0] == '\0')) {
		return NULL;
	}
	if (sjis2kuten(s[0], s[1], &ku, &ten)) {
		width = 16;
		src = af->glyphs + ANK_SIZE + (((ku * 94) + ten) * 32);
	}
	else {
		width = 8;
		src = af->glyphs + (s[0] * 16);
	}
	af->head.width = width;
	af->head.height = 16;
	af->head.pitch = width;
	dst = af->pixels;
	for (y = 0; y < 16; y++) {
		if (width == 16) {
			row = (src[0] << 8) | src[1];
			src += 2;
		}
		else {
			row = src[0] << 8;
			src += 1;
		}
		for (x = 0; x < width; x++) {
			*dst++ = (row & (0x8000 >> x)) ? 0xff : 0x00;
		}
	}
	/* fontmng_get と同じく、ヘッダーの直後に画素を置く */
	return &af->head;
}
