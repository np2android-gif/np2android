/**
 * @file	compiler.h
 * @brief	Android (SDL2) 用のコンパイラ/プラットフォーム定義
 *
 * xnp2-0.86 の android-project/jni/src/compiler.h をベースに、
 * x11/compiler.h を参考にして IA-32 コアと 64bit ABI に対応させたもの。
 */

#pragma once

#include <sys/param.h>
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include "SDL.h"

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define	BYTESEX_BIG
#else
#define	BYTESEX_LITTLE
#endif
#define	OSLANG_UTF8
#define	OSLINEBREAK_CRLF
#define	RESOURCE_US

typedef	signed int			SINT;
typedef	unsigned int		UINT;
typedef	signed char			SINT8;
typedef	unsigned char		UINT8;
typedef	signed short		SINT16;
typedef	unsigned short		UINT16;
typedef	signed int			SINT32;
typedef	unsigned int		UINT32;
typedef	int64_t				SINT64;
typedef	uint64_t			UINT64;

#define	INTPTR				intptr_t
#define	PTR_TO_UINT32(p)	((UINT32)(uintptr_t)(p))
#define	UINT32_TO_PTR(v)	((void *)(uintptr_t)(UINT32)(v))

#define	BRESULT				UINT
#define	OEMCHAR				char
#define	OEMTEXT(string)		string
#define	OEMSPRINTF			sprintf
#define	OEMSTRLEN			strlen

#define	SIZE_VGA
#if !defined(SIZE_VGA)
#define	RGB16		UINT32
#define	SIZE_QVGA
#endif

typedef	signed char			BOOL;

#ifndef	TRUE
#define	TRUE	1
#endif

#ifndef	FALSE
#define	FALSE	0
#endif

#ifndef	MAX_PATH
#define	MAX_PATH	256
#endif

#ifndef	max
#define	max(a,b)	(((a) > (b)) ? (a) : (b))
#endif
#ifndef	min
#define	min(a,b)	(((a) < (b)) ? (a) : (b))
#endif

#ifndef	ZeroMemory
#define	ZeroMemory(d,n)		memset((d), 0, (n))
#endif
#ifndef	CopyMemory
#define	CopyMemory(d,s,n)	memcpy((d), (s), (n))
#endif
#ifndef	FillMemory
#define	FillMemory(a, b, c)	memset((a), (c), (b))
#endif

#ifndef	roundup
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))
#endif

#ifndef	NELEMENTS
#define	NELEMENTS(a)	((int)(sizeof(a) / sizeof(a[0])))
#endif

#define	GETTICK()			SDL_GetTicks()
#define	GETRAND()			rand()
#define	SPRINTF				sprintf
#define	STRLEN				strlen

/* ---- calling convention (no special ABI on ARM/x86_64) ---- */
#ifndef	FASTCALL
#define	FASTCALL
#endif
#define	CPUCALL		FASTCALL
#define	MEMCALL		FASTCALL
#define	DMACCALL	FASTCALL
#define	IOOUTCALL	FASTCALL
#define	IOINPCALL	FASTCALL
#define	SOUNDCALL	FASTCALL
#define	VRAMCALL	FASTCALL
#define	SCRNCALL	FASTCALL
#define	VERMOUTHCL	FASTCALL

#ifndef	__ASSERT
#define	__ASSERT(s)
#endif
#ifndef	INLINE
#define	INLINE		inline
#endif

/* ---- features ---- */
#define	VERMOUTH_LIB
#define	SOUND_CRITICAL
#define	SOUNDRESERVE	100

#define	SUPPORT_UTF8
#define	SUPPORT_16BPP
/* 32bit で描くと SDL (GLES2) へ変換なしで転送できる (sdl2/scrnmng.c 参照) */
#define	SUPPORT_32BPP
/* 32bit 画面のとき np2 内蔵メニューは 24bit の作業領域を使う (scrnmng_entermenu)。無いとメニューが開かない */
#define	SUPPORT_24BPP
/* 24bit 作業領域のバイト順 (上流では win9x/scrnmng.h だけが定義している) */
#define	RGB24_B		0
#define	RGB24_G		1
#define	RGB24_R		2
#define	SCREEN_BPP		32
#define	MEMOPTIMIZE		2

#define	SUPPORT_HOSTDRV
#define	SUPPORT_SWSEEKSND
#define	SUPPORT_ARC
#define	SUPPORT_RESUME
#define	SUPPORT_STATSAVE

#if defined(CPUCORE_IA32)
/* PC-9821 (IA-32) 構成: x11 版に合わせて IDE を使う */
#define	SUPPORT_PC9821
#define	SUPPORT_CRT31KHZ
#define	SUPPORT_IDEIO
#define	msgbox(title, msg)	SDL_Log("%s: %s", (title), (msg))
#else
/* PC-9801 (i286) 構成 */
#define	SUPPORT_CRT15KHZ
#define	SUPPORT_SASI
#define	SUPPORT_SCSI
#endif

#include "common.h"
#include "milstr.h"
#include "_memory.h"
#include "rect.h"
#include "lstarray.h"
#include "trace.h"

#if !defined(TRACE)
/* atapicmd.c が TRACE 無効時にも直接呼ぶため宣言だけ出しておく (実体は port/trace_stub.c) */
#ifdef __cplusplus
extern "C"
#endif
void trace_fmt(const char *str, ...);
#endif
