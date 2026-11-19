/*
 * ANDROID-HOSTDRV: Windows NT 用ホストドライブ (hostdrvnt.c) が使う、短い名前 (8.3 名) の関数。
 * NP21/W rev107 では generic/hostdrvs.c にあるが、xnp2 の hostdrvs.c (DOS 版) とは関数名・型名が
 * ぶつかるので、ここに分けて置く。
 * 今は仮の実装 (hostdrvsfn.c) で、どの名前にも短い名前が無いと返す。Windows では長い名前だけが見え、
 * dir /x の短い名前は空になる。
 */
#pragma once

#if defined(SUPPORT_HOSTDRVNT)

/* 短い名前の対応表の 1 件。中身は hostdrvsfn.c だけが知っていればよい */
typedef struct tagHostDrvShortNameEntry HDRVSFNENTRY;

#ifdef __cplusplus
extern "C" {
#endif

BRESULT hostdrvs_getshortnamemap(const OEMCHAR *lpPath, HDRVSFNENTRY **ppEntries, UINT *pnEntries);
void hostdrvs_freeshortnamemap(HDRVSFNENTRY *pEntries);
void hostdrvs_invalidateshortnamecache(void);
BOOL hostdrvs_lookupshortnamecached(const OEMCHAR *lpPath, const OEMCHAR *lpFilename,
								 OEMCHAR *lpShortName, UINT cchShortName);
BOOL hostdrvs_lookuplongnamecached(const OEMCHAR *lpPath, const OEMCHAR *lpShortName,
								 OEMCHAR *lpFilename, UINT cchFilename, UINT32 *lpAttr);
BOOL hostdrvs_lookupshortname(const HDRVSFNENTRY *pEntries, UINT nEntries,
							  const OEMCHAR *lpFilename, OEMCHAR *lpShortName, UINT cchShortName);
BOOL hostdrvs_lookuplongname(const HDRVSFNENTRY *pEntries, UINT nEntries,
							 const OEMCHAR *lpShortName, OEMCHAR *lpFilename, UINT cchFilename,
							 UINT32 *lpAttr);

#ifdef __cplusplus
}
#endif

#endif	/* defined(SUPPORT_HOSTDRVNT) */
