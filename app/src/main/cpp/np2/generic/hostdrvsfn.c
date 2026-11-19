/*
 * ANDROID-HOSTDRV: Windows NT 用ホストドライブの短い名前 (8.3 名) の関数の仮の実装 (hostdrvsfn.h)。
 * どの名前にも短い名前が無いと返す。hostdrvnt.c は、そのときは長い名前だけで動く
 * (一覧の短い名前は空、短い名前での指定は見つからない)。
 * NP21/W rev107 の generic/hostdrvs.c の該当部分は、後の段階でここに取り込む。
 */
#include	"compiler.h"

#if defined(SUPPORT_HOSTDRVNT)

#include	"hostdrvsfn.h"

BRESULT hostdrvs_getshortnamemap(const OEMCHAR *lpPath, HDRVSFNENTRY **ppEntries, UINT *pnEntries)
{
	(void)lpPath;
	if (ppEntries) *ppEntries = NULL;
	if (pnEntries) *pnEntries = 0;
	return FAILURE;
}

void hostdrvs_freeshortnamemap(HDRVSFNENTRY *pEntries)
{
	(void)pEntries;
}

void hostdrvs_invalidateshortnamecache(void)
{
}

BOOL hostdrvs_lookupshortnamecached(const OEMCHAR *lpPath, const OEMCHAR *lpFilename,
								 OEMCHAR *lpShortName, UINT cchShortName)
{
	(void)lpPath; (void)lpFilename; (void)lpShortName; (void)cchShortName;
	return FALSE;
}

BOOL hostdrvs_lookuplongnamecached(const OEMCHAR *lpPath, const OEMCHAR *lpShortName,
								 OEMCHAR *lpFilename, UINT cchFilename, UINT32 *lpAttr)
{
	(void)lpPath; (void)lpShortName; (void)lpFilename; (void)cchFilename; (void)lpAttr;
	return FALSE;
}

BOOL hostdrvs_lookupshortname(const HDRVSFNENTRY *pEntries, UINT nEntries,
							  const OEMCHAR *lpFilename, OEMCHAR *lpShortName, UINT cchShortName)
{
	(void)pEntries; (void)nEntries; (void)lpFilename; (void)lpShortName; (void)cchShortName;
	return FALSE;
}

BOOL hostdrvs_lookuplongname(const HDRVSFNENTRY *pEntries, UINT nEntries,
							 const OEMCHAR *lpShortName, OEMCHAR *lpFilename, UINT cchFilename,
							 UINT32 *lpAttr)
{
	(void)pEntries; (void)nEntries; (void)lpShortName; (void)lpFilename; (void)cchFilename; (void)lpAttr;
	return FALSE;
}

#endif	/* defined(SUPPORT_HOSTDRVNT) */
