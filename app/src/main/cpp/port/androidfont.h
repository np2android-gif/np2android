/**
 * @file	androidfont.h
 * @brief	ANDROID-FONT: font.tmp を作るときに使う字の形 (端末の日本語フォントを Kotlin で描いたもの)
 */

#pragma once

#include "fontmng.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Kotlin に全部の字を描いてもらう。端末に日本語フォントが無いなど、描けなかったときは NULL */
void *androidfont_create(void);
void androidfont_destroy(void *hdl);

/**
 * 字の形を返す (fontmng_get と同じ形。画素は 0 か 0xff)。
 * sjis は Shift JIS の 1 文字 (1 バイトの半角か、2 バイトの全角)。UTF-8 に変えずに渡す。
 * 表に無い文字は NULL
 */
FNTDAT androidfont_get(void *hdl, const char *sjis);

#ifdef __cplusplus
}
#endif
