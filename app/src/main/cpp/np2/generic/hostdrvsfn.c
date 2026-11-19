/**
 * @file	hostdrvsfn.c
 * @brief	Windows NT 用ホストドライブ (hostdrvnt.c) の短い名前 (8.3 名)
 *
 * ANDROID-HOSTDRV: Neko Project 21/W rev107 の generic/hostdrvs.c の短い名前の部分 (先頭から
 * hostdrvs_lookuplongnamecached まで) を写したもの。ライセンスは NP21/W の LICENSES/LICENSE.TXT
 * (3 条項 BSD)。xnp2 の hostdrvs.c (DOS 版) と関数名・型名がぶつかるので別のファイルにした。
 * 元から変えた点:
 *  - hostdrvs.h の代わりに hostdrvsfn.h を読み、元の hostdrvs.h・hostdrv.h にあった型 (HDRVFILE・
 *    HDRVSFNENTRY・短い名前の方式) をここに置く。HDRVFILE は xnp2 の hostdrv.h の同名の型 (ポインタ) と
 *    違うが、このファイルは xnp2 の hostdrv.h を読まないのでぶつからない。
 *  - DOS 版だけが使うもの (ルートの情報 s_hddroot、GetHostRootPath の宣言、IsMatchFcb、AddFileToList) を除く。
 *  - SUPPORT_HOSTDRV ではなく SUPPORT_HOSTDRVNT でビルドする。
 * それ以外は元のまま (文字コードも元と同じ Shift_JIS)。
 */

#include "compiler.h"

#if defined(SUPPORT_HOSTDRVNT)

#include "dosio.h"
#include "hostdrvsfn.h"

/* 元の hostdrv.h から: 短い名前の作り方 */
enum {
	HOSTDRV_SHORTNAME_LEGACY	= 0,	/*!< Original truncation; colliding entries are omitted */
	HOSTDRV_SHORTNAME_TILDE	= 1	/*!< Host short name, then long-name ordered NAME~n */
};
#ifndef HOSTDRV_SHORTNAME_DEFAULT
#define HOSTDRV_SHORTNAME_DEFAULT	HOSTDRV_SHORTNAME_TILDE
#endif

/* 元の hostdrvs.h から: DOS ファイル情報と短い名前の対応表の 1 件 */
struct tagHostDrvFile
{
	char	fcbname[11];	/*!< FCB 名 */
	UINT	caps;			/*!< 情報フラグ */
	UINT32	size;			/*!< サイズ */
	UINT32	attr;			/*!< 属性 */
	DOSDATE	date;			/*!< 日付 */
	DOSTIME	time;			/*!< 時間 */
};
typedef struct tagHostDrvFile HDRVFILE;

struct tagHostDrvShortNameEntry
{
	HDRVFILE file;
	OEMCHAR szFilename[MAX_PATH];
	OEMCHAR szShortFilename[64];
	UINT nOrder;
	BOOL bAssigned;
};

void hostdrvs_setshortnamemode(UINT nMode);
UINT hostdrvs_getshortnamemode(void);

/* ---- ここから NP21/W rev107 generic/hostdrvs.c ---- */

#if defined(OSLANG_EUC) || defined(OSLANG_UTF8) || defined(OSLANG_UCS2)
#include "oemtext.h"
#endif
#include "pccore.h"

/* 性能上最適化で優先しない方がいいコードなのでわざと別セグメントに置く */
/* #pragma code_seg(".MISCCODE") */

/*! 自分 */
static const char s_self[11] = {'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};

/*! 親 */
static const char s_parent[11] = {'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '};

/*! DOSで許可されるキャラクタ */
static const UINT8 s_cDosCharacters[] =
{
	0xfa, 0x23,		/* '&%$#"!  /.-,+*)( */
	0xff, 0x03,		/* 76543210 ?>=<;:98 */
	0xff, 0xff,		/* GFEDCBA@ ONMLKJIH */
	0xff, 0xef,		/* WVUTSRQP _^]\[ZYX */
	0x01, 0x00,		/* gfedcba` onmlkjih */
	0x00, 0x40		/* wvutsrqp ~}|{zyx  */
};

static UINT s_nShortNameMode = HOSTDRV_SHORTNAME_DEFAULT;

/* OEMCHAR helpers kept local so generic/hostdrvs.c does not require the
 * Windows _tcs* family.  Case-insensitive filename comparisons are delegated
 * to the active dosio backend; exact cache-key comparisons stay local. */
static UINT32 HostOemCharValue(OEMCHAR c)
{
	if (sizeof(OEMCHAR) == 1)
	{
		return (UINT32)(UINT8)c;
	}
	return (UINT32)c;
}

static int HostOemCmp(const OEMCHAR *a, const OEMCHAR *b)
{
	UINT32 ca;
	UINT32 cb;

	for (;;)
	{
		ca = HostOemCharValue(*a++);
		cb = HostOemCharValue(*b++);
		if (ca != cb) return (ca > cb) ? 1 : -1;
		if (ca == 0) return 0;
	}
}

static int HostOemIcmp(const OEMCHAR *a, const OEMCHAR *b)
{
	/* Use the backend's filename comparison semantics.  This preserves the
	 * Windows/SJIS behavior while allowing X11/SDL2 to apply their native
	 * case-folding rules.  Cache keys themselves remain byte/code-unit exact
	 * (HostOemCmp) so case-sensitive host filesystems cannot alias /Foo and
	 * /foo to the same SFN cache entry. */
	return file_cmpname(a, b);
}

static int HostOemNcmp(const OEMCHAR *a, const OEMCHAR *b, UINT n)
{
	UINT32 ca;
	UINT32 cb;

	while (n-- != 0)
	{
		ca = HostOemCharValue(*a++);
		cb = HostOemCharValue(*b++);
		if (ca != cb) return (ca > cb) ? 1 : -1;
		if (ca == 0) return 0;
	}
	return 0;
}

static UINT HostOemLen(const OEMCHAR *s)
{
	const OEMCHAR *p;

	p = s;
	while (*p != 0) p++;
	return (UINT)(p - s);
}

#define HOSTDRV_SFNCACHE_COUNT 8

/*
 * Directory name-change monitoring is an optional safety aid for the SFN
 * cache.  hostdrvs.c does not depend on any platform API: a dosio backend may
 * define DOSIO_HAS_DIRMONITOR and provide the three file_dirmonitor_* helpers.
 * Backends without that capability simply use an unmonitored cache and rely
 * on HOSTDRV-originated invalidation.
 */

typedef struct {
	BOOL valid;
	UINT32 age;
	OEMCHAR szPath[MAX_PATH];
	HDRVSFNENTRY *pEntries;
	UINT nEntries;
	/*
	 * Optional platform monitor.  NULL means that namespace changes cannot be
	 * observed.  Such an entry is still cacheable; this intentionally accepts a
	 * short stale-cache window on platforms/filesystems without monitoring.
	 */
	void *pNameChange;
} HDRVSFNCACHE;

static HDRVSFNCACHE s_sfnCache[HOSTDRV_SFNCACHE_COUNT];
static UINT32 s_sfnCacheAge;

static BOOL SfnNameMonitorValid(void *pMonitor)
{
	return (pMonitor != NULL) ? TRUE : FALSE;
}

static void SfnNameMonitorClose(void *pMonitor)
{
#if defined(DOSIO_HAS_DIRMONITOR)
	if (pMonitor != NULL)
	{
		file_dirmonitor_close((FDIRMONH)pMonitor);
	}
#else
	(void)pMonitor;
#endif
}

static void ClearSfnCacheSlot(UINT nSlot)
{
	if (nSlot >= HOSTDRV_SFNCACHE_COUNT)
	{
		return;
	}
	if (s_sfnCache[nSlot].pEntries != NULL)
	{
		free(s_sfnCache[nSlot].pEntries);
	}
	SfnNameMonitorClose(s_sfnCache[nSlot].pNameChange);
	ZeroMemory(&s_sfnCache[nSlot], sizeof(s_sfnCache[nSlot]));
}

void hostdrvs_invalidateshortnamecache(void)
{
	UINT i;

	for (i = 0; i < HOSTDRV_SFNCACHE_COUNT; i++)
	{
		ClearSfnCacheSlot(i);
	}
	s_sfnCacheAge = 0;
}

/**
 * Short File Nameのつけ方ルール設定
 */
void hostdrvs_setshortnamemode(UINT nMode)
{
	UINT nOldMode;

	nOldMode = s_nShortNameMode;
	switch (nMode)
	{
		case HOSTDRV_SHORTNAME_LEGACY:
		case HOSTDRV_SHORTNAME_TILDE:
			s_nShortNameMode = nMode;
			break;

		default:
			s_nShortNameMode = HOSTDRV_SHORTNAME_DEFAULT;
			break;
	}
	if (nOldMode != s_nShortNameMode)
	{
		hostdrvs_invalidateshortnamecache();
	}
}

/**
 * Short File Nameのつけ方ルール取得
 */
UINT hostdrvs_getshortnamemode(void)
{
	return s_nShortNameMode;
}

/**
 * パスを FCB に変換
 * @param[out] lpFcbname FCB
 * @param[in] cchFcbname FCB バッファ サイズ
 * @param[in] lpPath パス
 */
static void RealPath2FcbSub(char *lpFcbname, UINT cchFcbname, const char *lpPath)
{
	REG8 c;

	while (cchFcbname)
	{
		c = (UINT8)*lpPath++;
		if (c == 0)
		{
			break;
		}
#if defined(OSLANG_SJIS) || defined(OSLANG_EUC) || defined(OSLANG_UTF8) || defined(OSLANG_UCS2)
		if ((((c ^ 0x20) - 0xa1) & 0xff) < 0x3c)
		{
			if (lpPath[0] == '\0')
			{
				break;
			}
			if (cchFcbname < 2)
			{
				break;
			}
			lpFcbname[0] = c;
			lpFcbname[1] = *lpPath++;
			lpFcbname += 2;
			cchFcbname -= 2;
		}
		else if (((c - 0x20) & 0xff) < 0x60)
		{
			if (((c - 'a') & 0xff) < 26)
			{
				c -= 0x20;
			}
			if (s_cDosCharacters[(c >> 3) - (0x20 >> 3)] & (1 << (c & 7)))
			{
				*lpFcbname++ = c;
				cchFcbname--;
			}
		}
		else if (((c - 0xa0) & 0xff) < 0x40)
		{
			*lpFcbname++ = c;
			cchFcbname--;
		}
#else
		if (((c - 0x20) & 0xff) < 0x60)
		{
			if (((c - 'a') & 0xff) < 26)
			{
				c -= 0x20;
			}
			if (s_cDosCharacters[(c >> 3) - (0x20 >> 3)] & (1 << (c & 7)))
			{
				*lpFcbname++ = c;
				cchFcbname--;
			}
		}
		else if (c >= 0x80)
		{
			*lpFcbname++ = c;
			cchFcbname--;
		}
#endif
	}
}

/**
 * パスを FCB に変換
 * @param[out] lpFcbname FCB
 * @param[in] lpPath パス
 */
static void RealName2Fcb(char *lpFcbname, const OEMCHAR *lpPath)
{
	OEMCHAR	*ext;
#if defined(OSLANG_EUC) || defined(OSLANG_UTF8) || defined(OSLANG_UCS2)
	char sjis[MAX_PATH];
#endif
	OEMCHAR szFilename[MAX_PATH];

	FillMemory(lpFcbname, 11, ' ');

	ext = file_getext(lpPath);
#if defined(OSLANG_EUC) || defined(OSLANG_UTF8) || defined(OSLANG_UCS2)
	oemtext_oemtosjis(sjis, NELEMENTS(sjis), ext, (UINT)-1);
	RealPath2FcbSub(lpFcbname + 8, 3, sjis);
#else
	RealPath2FcbSub(lpFcbname + 8, 3, ext);
#endif

	file_cpyname(szFilename, lpPath, NELEMENTS(szFilename));
	file_cutext(szFilename);
#if defined(OSLANG_EUC) || defined(OSLANG_UTF8) || defined(OSLANG_UCS2)
	oemtext_oemtosjis(sjis, NELEMENTS(sjis), szFilename, (UINT)-1);
	RealPath2FcbSub(lpFcbname + 0, 8, sjis);
#else
	RealPath2FcbSub(lpFcbname + 0, 8, szFilename);
#endif
}

/**
 * FCB名をDOS表示名へ変換
 */
static void Fcb2DosName(char *lpDosName, const char *lpFcbname)
{
	char *p;
	UINT i;

	p = lpDosName;
	for (i = 0; (i < 8) && (lpFcbname[i] != ' '); i++)
	{
		*p++ = lpFcbname[i];
	}
	if (lpFcbname[8] != ' ')
	{
		*p++ = '.';
		for (i = 8; (i < 11) && (lpFcbname[i] != ' '); i++)
		{
			*p++ = lpFcbname[i];
		}
	}
	*p = '\0';
}

/**
 * FCB名をOEMCHAR変換.
 */
static void Fcb2OemName(OEMCHAR *lpOemName, UINT cchOemName, const char *lpFcbname)
{
	char szDosName[16];

	Fcb2DosName(szDosName, lpFcbname);
#if defined(OSLANG_EUC) || defined(OSLANG_UTF8) || defined(OSLANG_UCS2)
	oemtext_sjistooem(lpOemName, cchOemName, szDosName, (UINT)-1);
#else
	file_cpyname(lpOemName, szDosName, cchOemName);
#endif
}

/**
 * ホストのファイル名がShort File Name互換ならTrue
 */
static BOOL IsExact83Name(const OEMCHAR *lpFilename, const char *lpFcbname)
{
	OEMCHAR szRoundTrip[64];

	Fcb2OemName(szRoundTrip, NELEMENTS(szRoundTrip), lpFcbname);
	return (HostOemIcmp(szRoundTrip, lpFilename) == 0);
}

typedef struct {
	char fcbname[11];
	BOOL used;
} SFNUSEDSLOT;

typedef struct {
	SFNUSEDSLOT *pSlots;
	UINT nMask;
} SFNUSEDSET;

static UINT32 HashFcbName(const char *lpFcbname)
{
	UINT32 h;
	UINT i;

	h = 2166136261UL;
	for (i = 0; i < 11; i++)
	{
		h ^= (UINT8)lpFcbname[i];
		h *= 16777619UL;
	}
	return h;
}

static BRESULT SfnUsedInit(SFNUSEDSET *pSet, UINT nNames)
{
	UINT nCapacity;

	if (pSet == NULL)
	{
		return FAILURE;
	}
	pSet->pSlots = NULL;
	pSet->nMask = 0;
	nCapacity = 64;
	while (nCapacity < ((nNames + 1) * 2))
	{
		if (nCapacity > 0x40000000U)
		{
			return FAILURE;
		}
		nCapacity <<= 1;
	}
	pSet->pSlots = (SFNUSEDSLOT *)calloc(nCapacity, sizeof(SFNUSEDSLOT));
	if (pSet->pSlots == NULL)
	{
		return FAILURE;
	}
	pSet->nMask = nCapacity - 1;
	return SUCCESS;
}

static void SfnUsedDestroy(SFNUSEDSET *pSet)
{
	if (pSet != NULL)
	{
		free(pSet->pSlots);
		pSet->pSlots = NULL;
		pSet->nMask = 0;
	}
}

static BOOL IsFcbUsed(const SFNUSEDSET *pSet, const char *lpFcbname)
{
	UINT nPos;

	if ((pSet == NULL) || (pSet->pSlots == NULL))
	{
		return FALSE;
	}
	nPos = (UINT)(HashFcbName(lpFcbname) & pSet->nMask);
	while (pSet->pSlots[nPos].used)
	{
		if (memcmp(pSet->pSlots[nPos].fcbname, lpFcbname, 11) == 0)
		{
			return TRUE;
		}
		nPos = (nPos + 1) & pSet->nMask;
	}
	return FALSE;
}

static BRESULT AddUsedFcb(SFNUSEDSET *pSet, const char *lpFcbname)
{
	UINT nPos;

	if ((pSet == NULL) || (pSet->pSlots == NULL))
	{
		return FAILURE;
	}
	nPos = (UINT)(HashFcbName(lpFcbname) & pSet->nMask);
	while (pSet->pSlots[nPos].used)
	{
		if (memcmp(pSet->pSlots[nPos].fcbname, lpFcbname, 11) == 0)
		{
			return SUCCESS;
		}
		nPos = (nPos + 1) & pSet->nMask;
	}
	memcpy(pSet->pSlots[nPos].fcbname, lpFcbname, 11);
	pSet->pSlots[nPos].used = TRUE;
	return SUCCESS;
}

static BOOL IsSjisLeadByte(UINT8 c)
{
#if defined(OSLANG_SJIS) || defined(OSLANG_EUC) || defined(OSLANG_UTF8) || defined(OSLANG_UCS2)
	return ((((c ^ 0x20) - 0xa1) & 0xff) < 0x3c);
#else
	(void)c;
	return FALSE;
#endif
}

static UINT DecimalDigits(UINT32 nValue)
{
	UINT nDigits;

	nDigits = 1;
	while (nValue >= 10)
	{
		nValue /= 10;
		nDigits++;
	}
	return nDigits;
}

static void UIntToDecimal(char *lpBuffer, UINT32 nValue, UINT nDigits)
{
	UINT i;

	for (i = 0; i < nDigits; i++)
	{
		lpBuffer[nDigits - i - 1] = (char)('0' + (nValue % 10));
		nValue /= 10;
	}
	lpBuffer[nDigits] = '\0';
}

/**
 * DOS互換名のベースになる部分を取得　SJISも考慮
 */
static UINT CopyStemPrefix(char *lpDst, UINT nLimit, const char *lpLegacy)
{
	UINT src;
	UINT dst;

	src = 0;
	dst = 0;
	while ((src < 8) && (lpLegacy[src] != ' ') && (dst < nLimit))
	{
		UINT nCharSize;

		nCharSize = 1;
		if (IsSjisLeadByte((UINT8)lpLegacy[src]) &&
			(src + 1 < 8) && (lpLegacy[src + 1] != ' '))
		{
			nCharSize = 2;
		}
		if (dst + nCharSize > nLimit)
		{
			break;
		}
		memcpy(lpDst + dst, lpLegacy + src, nCharSize);
		src += nCharSize;
		dst += nCharSize;
	}
	return dst;
}

/**
 * チルダ番号で保護したSFNを生成する
 */
static BOOL MakeTildeFcb(char *lpFcbname, const OEMCHAR *lpFilename,
						 const SFNUSEDSET *used, UINT32 nStart, UINT32 *pnUsed)
{
	char legacy[11];
	char digits[16];
	UINT32 nNumber;

	RealName2Fcb(legacy, lpFilename);
	if (nStart == 0)
	{
		nStart = 1;
	}
	for (nNumber = nStart; nNumber <= 9999999UL; nNumber++)
	{
		UINT nDigits;
		UINT nPrefixLimit;
		UINT nPos;

		nDigits = DecimalDigits(nNumber);
		if (nDigits >= 8)
		{
			break;
		}
		nPrefixLimit = 8 - nDigits - 1;

		FillMemory(lpFcbname, 11, ' ');
		nPos = CopyStemPrefix(lpFcbname, nPrefixLimit, legacy);
		if (nPos == 0)
		{
			// 1文字も有効でないときは適当に"FILE"とする
			static const char s_fallback[] = "FILE";
			UINT nFallback;

			nFallback = min(nPrefixLimit, (UINT)(sizeof(s_fallback) - 1));
			memcpy(lpFcbname, s_fallback, nFallback);
			nPos = nFallback;
		}
		lpFcbname[nPos++] = '~';
		UIntToDecimal(digits, nNumber, nDigits);
		memcpy(lpFcbname + nPos, digits, nDigits);
		memcpy(lpFcbname + 8, legacy + 8, 3);

		if (!IsFcbUsed(used, lpFcbname))
		{
			if (pnUsed != NULL)
			{
				*pnUsed = nNumber;
			}
			return TRUE;
		}
	}
	return FALSE;
}


static int CompareRawByName(const void *vp1, const void *vp2)
{
	const HDRVSFNENTRY *p1;
	const HDRVSFNENTRY *p2;
	int r;

	p1 = (const HDRVSFNENTRY *)vp1;
	p2 = (const HDRVSFNENTRY *)vp2;
	r = HostOemIcmp(p1->szFilename, p2->szFilename);
	if (r == 0)
	{
		r = HostOemCmp(p1->szFilename, p2->szFilename);
	}
	if (r == 0)
	{
		if (p1->nOrder < p2->nOrder)
		{
			r = -1;
		}
		else if (p1->nOrder > p2->nOrder)
		{
			r = 1;
		}
	}
	return r;
}

static int CompareRawByOrder(const void *vp1, const void *vp2)
{
	const HDRVSFNENTRY *p1;
	const HDRVSFNENTRY *p2;

	p1 = (const HDRVSFNENTRY *)vp1;
	p2 = (const HDRVSFNENTRY *)vp2;
	if (p1->nOrder < p2->nOrder)
	{
		return -1;
	}
	if (p1->nOrder > p2->nOrder)
	{
		return 1;
	}
	return 0;
}

static BOOL IsUsableMappedFcb(const char *lpFcbname, const SFNUSEDSET *used)
{
	OEMCHAR szName[32];
	char roundTrip[11];

	if ((lpFcbname == NULL) || (lpFcbname[0] == ' '))
	{
		return FALSE;
	}
	Fcb2OemName(szName, NELEMENTS(szName), lpFcbname);
	RealName2Fcb(roundTrip, szName);
	if (memcmp(roundTrip, lpFcbname, 11) != 0)
	{
		return FALSE;
	}
	return !IsFcbUsed(used, lpFcbname);
}

static BRESULT AppendUsedName(SFNUSEDSET *used, HDRVSFNENTRY *pEntry,
							  const char *lpFcbname)
{
	if (AddUsedFcb(used, lpFcbname) != SUCCESS)
	{
		return FAILURE;
	}
	memcpy(pEntry->file.fcbname, lpFcbname, 11);
	pEntry->bAssigned = TRUE;
	return SUCCESS;
}

static BRESULT GatherRawEntries(const OEMCHAR *lpPath, HDRVSFNENTRY **ppEntries,
								UINT *pnEntries, UINT *pnCapacity)
{
	FLISTH flh;
	FLINFO fli;

	flh = file_list1st(lpPath, &fli);
	if (flh == FLISTH_INVALID)
	{
		return SUCCESS;
	}
	do
	{
		HDRVSFNENTRY *p;
		HDRVSFNENTRY *pNew;
		UINT nCapacity;
		OEMCHAR szEntryPath[MAX_PATH];
		FLINFO freshInfo;
		const FLINFO *pInfo;

		// .と..は無視
		if ((HostOemCmp(fli.path, OEMTEXT(".")) == 0) || (HostOemCmp(fli.path, OEMTEXT("..")) == 0))
		{
			continue;
		}

		// Reject symbolic links/reparse points through the dosio abstraction.
		// This keeps hostdrvs.c independent of Windows FILE_ATTRIBUTE_* values.
		file_cpyname(szEntryPath, lpPath, NELEMENTS(szEntryPath));
		file_setseparator(szEntryPath, NELEMENTS(szEntryPath));
		file_catname(szEntryPath, fli.path, NELEMENTS(szEntryPath));
		if (file_infoislink(&fli, szEntryPath))
		{
			continue;
		}

		/* Some portable directory iterators cannot cheaply return full metadata.
		 * Refresh only when fields used by HOSTDRV are missing; Windows/X11
		 * normally take the zero-extra-call path. */
		pInfo = &fli;
		if ((fli.caps & (FLICAPS_SIZE | FLICAPS_ATTR | FLICAPS_DATE | FLICAPS_TIME)) !=
			(FLICAPS_SIZE | FLICAPS_ATTR | FLICAPS_DATE | FLICAPS_TIME))
		{
			if (file_getinfo(szEntryPath, &freshInfo) == SUCCESS)
			{
				pInfo = &freshInfo;
			}
		}
		// SFNマップ格納領域が足りなければ拡張
		if (*pnEntries >= *pnCapacity)
		{
			nCapacity = (*pnCapacity == 0) ? 64 : *pnCapacity * 2;
			pNew = (HDRVSFNENTRY *)realloc(*ppEntries,
										 nCapacity * sizeof(HDRVSFNENTRY));
			if (pNew == NULL)
			{
				file_listclose(flh);
				return FAILURE;
			}
			*ppEntries = pNew;
			*pnCapacity = nCapacity;
		}

		// SFN登録
		p = &(*ppEntries)[*pnEntries];
		ZeroMemory(p, sizeof(*p));
		p->file.caps = pInfo->caps;
		p->file.size = pInfo->size;
		p->file.attr = pInfo->attr;
		p->file.date = pInfo->date;
		p->file.time = pInfo->time;
		file_cpyname(p->szFilename, fli.path, NELEMENTS(p->szFilename));
		file_cpyname(p->szShortFilename, fli.shortpath,
					 NELEMENTS(p->szShortFilename));
		/* Preserve the pre-optimization host-SFN behavior.  Some filesystems may
		 * leave cAlternateFileName empty while GetShortPathName still supplies a
		 * usable host alias.  This fallback costs one query for those entries but
		 * avoids changing which real file an existing host SFN denotes. */
		if (p->szShortFilename[0] == '\0')
		{
			OEMCHAR szFullPath[MAX_PATH];

			file_cpyname(szFullPath, lpPath, NELEMENTS(szFullPath));
			file_setseparator(szFullPath, NELEMENTS(szFullPath));
			file_catname(szFullPath, fli.path, NELEMENTS(szFullPath));
			file_getshortname(szFullPath, p->szShortFilename,
							  NELEMENTS(p->szShortFilename));
		}

		p->nOrder = *pnEntries;
		(*pnEntries)++;
	} while (file_listnext(flh, &fli) == SUCCESS);
	file_listclose(flh);
	return SUCCESS;
}

static BRESULT AssignHostAndExactNames(HDRVSFNENTRY *pEntries, UINT nEntries,
									   SFNUSEDSET *used)
{
	UINT i;
	char candidate[11];

	/* Host OS aliases are authoritative when the file system supplies one. */
	for (i = 0; i < nEntries; i++)
	{
		HDRVSFNENTRY *p;

		p = &pEntries[i];
		if (p->szShortFilename[0] != '\0')
		{
			RealName2Fcb(candidate, p->szShortFilename);
			if (IsExact83Name(p->szShortFilename, candidate) &&
				IsUsableMappedFcb(candidate, used))
			{
				if (AppendUsedName(used, p, candidate) != SUCCESS)
				{
					return FAILURE;
				}
			}
		}
	}

	/* Preserve real names that already are valid, unique 8.3 names. */
	for (i = 0; i < nEntries; i++)
	{
		HDRVSFNENTRY *p;

		p = &pEntries[i];
		if (!p->bAssigned)
		{
			RealName2Fcb(candidate, p->szFilename);
			if (IsExact83Name(p->szFilename, candidate) &&
				IsUsableMappedFcb(candidate, used))
			{
				if (AppendUsedName(used, p, candidate) != SUCCESS)
				{
					return FAILURE;
				}
			}
		}
	}
	return SUCCESS;
}

static BRESULT AssignGeneratedNames(HDRVSFNENTRY *pEntries, UINT nEntries,
									SFNUSEDSET *used)
{
	UINT i;
	char candidate[11];
	char legacy[11];
	char lastLegacy[11];
	BOOL bHaveLast;
	UINT32 nNext;

	bHaveLast = FALSE;
	nNext = 1;
	for (i = 0; i < nEntries; i++)
	{
		HDRVSFNENTRY *p;
		UINT32 nUsed;

		p = &pEntries[i];
		if (p->bAssigned)
		{
			continue;
		}
		RealName2Fcb(legacy, p->szFilename);
		if (!bHaveLast || (memcmp(lastLegacy, legacy, 11) != 0))
		{
			nNext = 1;
		}
		nUsed = 0;
		if (!MakeTildeFcb(candidate, p->szFilename, used, nNext, &nUsed))
		{
			return FAILURE;
		}
		if (AppendUsedName(used, p, candidate) != SUCCESS)
		{
			return FAILURE;
		}
		memcpy(lastLegacy, legacy, 11);
		bHaveLast = TRUE;
		nNext = nUsed + 1;
	}
	return SUCCESS;
}

static BRESULT AssignLegacyNames(HDRVSFNENTRY *pEntries, UINT nEntries, SFNUSEDSET *used)
{
	UINT i;
	char candidate[11];

	for (i = 0; i < nEntries; i++)
	{
		HDRVSFNENTRY *p;

		p = &pEntries[i];
		RealName2Fcb(candidate, p->szFilename);
		if ((candidate[0] == ' ') || IsFcbUsed(used, candidate))
		{
			continue;
		}
		if (AppendUsedName(used, p, candidate) != SUCCESS)
		{
			return FAILURE;
		}
	}
	return SUCCESS;
}

/// <summary>
/// 短いファイル名のマップを作る
/// s_nShortNameModeがHOSTDRV_SHORTNAME_LEGACYの場合、旧np2互換の単純切り捨て重複無視での生成
/// s_nShortNameModeがHOSTDRV_SHORTNAME_TILDEの場合、Windows標準のチルダ番号方式での生成
/// </summary>
/// <param name="lpPath">SFNマップを作るパス</param>
/// <param name="ppEntries">SFNマップ</param>
/// <param name="pnEntries">SFNマップエントリ数</param>
/// <returns></returns>
static BRESULT BuildShortNameMap(const OEMCHAR *lpPath, HDRVSFNENTRY **ppEntries, UINT *pnEntries)
{
	HDRVSFNENTRY *entries;
	UINT nEntries;
	UINT nCapacity;
	SFNUSEDSET used;
	BRESULT r;

	// パスがNULLは不可
	if ((lpPath == NULL) || (ppEntries == NULL) || (pnEntries == NULL))
	{
		return FAILURE;
	}
	*ppEntries = NULL;
	*pnEntries = 0;
	entries = NULL;
	nEntries = 0;
	nCapacity = 0;

	// 
	if (GatherRawEntries(lpPath, &entries, &nEntries, &nCapacity) != SUCCESS)
	{
		free(entries);
		return FAILURE;
	}
	if (nEntries == 0)
	{
#if defined(HOSTDRV_SFN_TRACE)
		TRACEOUT(("hostdrv:sfn build %s (0)", lpPath));
#endif
		free(entries);
		return SUCCESS;
	}
#if defined(HOSTDRV_SFN_TRACE)
	TRACEOUT(("hostdrv:sfn build %s (%u)", lpPath, nEntries));
#endif

	ZeroMemory(&used, sizeof(used));
	if (SfnUsedInit(&used, nEntries + 2) != SUCCESS)
	{
		free(entries);
		return FAILURE;
	}
	if ((AddUsedFcb(&used, s_self) != SUCCESS) ||
		(AddUsedFcb(&used, s_parent) != SUCCESS))
	{
		SfnUsedDestroy(&used);
		free(entries);
		return FAILURE;
	}

	if (s_nShortNameMode == HOSTDRV_SHORTNAME_LEGACY)
	{
		r = AssignLegacyNames(entries, nEntries, &used);
	}
	else
	{
		qsort(entries, nEntries, sizeof(HDRVSFNENTRY), CompareRawByName);
		r = AssignHostAndExactNames(entries, nEntries, &used);
		if (r == SUCCESS)
		{
			r = AssignGeneratedNames(entries, nEntries, &used);
		}
	}

	SfnUsedDestroy(&used);
	if (r != SUCCESS)
	{
		free(entries);
		return FAILURE;
	}

	qsort(entries, nEntries, sizeof(HDRVSFNENTRY), CompareRawByOrder);
	*ppEntries = entries;
	*pnEntries = nEntries;
	return SUCCESS;
}

static UINT FindSfnCacheSlot(const OEMCHAR *lpPath)
{
	UINT i;

	for (i = 0; i < HOSTDRV_SFNCACHE_COUNT; i++)
	{
		if (s_sfnCache[i].valid && (HostOemCmp(s_sfnCache[i].szPath, lpPath) == 0))
		{
			return i;
		}
	}
	return (UINT)-1;
}

static UINT SelectSfnCacheVictim(void)
{
	UINT i;
	UINT nVictim;
	UINT32 nOldest;

	for (i = 0; i < HOSTDRV_SFNCACHE_COUNT; i++)
	{
		if (!s_sfnCache[i].valid)
		{
			return i;
		}
	}
	nVictim = 0;
	nOldest = s_sfnCache[0].age;
	for (i = 1; i < HOSTDRV_SFNCACHE_COUNT; i++)
	{
		if (s_sfnCache[i].age < nOldest)
		{
			nOldest = s_sfnCache[i].age;
			nVictim = i;
		}
	}
	return nVictim;
}

static void *StartSfnNameChangeMonitor(const OEMCHAR *lpPath)
{
#if defined(DOSIO_HAS_DIRMONITOR)
	FDIRMONH hChange;

	hChange = file_dirmonitor_open(lpPath);
	if (hChange == FDIRMONH_INVALID)
	{
		return NULL;
	}
	return (void *)hChange;
#else
	(void)lpPath;
	return NULL;
#endif
}

static BOOL SfnCacheSlotNamespaceStable(UINT nSlot)
{
	if (nSlot >= HOSTDRV_SFNCACHE_COUNT || !s_sfnCache[nSlot].valid)
	{
		return FALSE;
	}
	/* No monitor means "unknown", not "changed".  Cross-platform builds and
	 * filesystems without notification support are allowed to keep using the
	 * cache; HOSTDRV-originated mutations still invalidate it explicitly. */
	if (!SfnNameMonitorValid(s_sfnCache[nSlot].pNameChange))
	{
		return TRUE;
	}
#if defined(DOSIO_HAS_DIRMONITOR)
	return file_dirmonitor_changed((FDIRMONH)s_sfnCache[nSlot].pNameChange)
		? FALSE : TRUE;
#else
	return TRUE;
#endif
}

static void StoreSfnCacheOwned(const OEMCHAR *lpPath, HDRVSFNENTRY *pEntries,
	UINT nEntries, void *pNameChange)
{
	UINT nSlot;

	nSlot = FindSfnCacheSlot(lpPath);
	if (nSlot == (UINT)-1)
	{
		nSlot = SelectSfnCacheVictim();
	}
	ClearSfnCacheSlot(nSlot);
	s_sfnCache[nSlot].valid = TRUE;
	s_sfnCache[nSlot].age = ++s_sfnCacheAge;
	file_cpyname(s_sfnCache[nSlot].szPath, lpPath, NELEMENTS(s_sfnCache[nSlot].szPath));
	s_sfnCache[nSlot].pEntries = pEntries;
	s_sfnCache[nSlot].nEntries = nEntries;
	s_sfnCache[nSlot].pNameChange = pNameChange;
}

static void StoreSfnCacheCopy(const OEMCHAR *lpPath, const HDRVSFNENTRY *pEntries,
	UINT nEntries, void *pNameChange)
{
	HDRVSFNENTRY *pCopy;

	pCopy = NULL;
	if (nEntries != 0)
	{
		pCopy = (HDRVSFNENTRY *)malloc(nEntries * sizeof(HDRVSFNENTRY));
		if (pCopy == NULL)
		{
			SfnNameMonitorClose(pNameChange);
			return;
		}
		memcpy(pCopy, pEntries, nEntries * sizeof(HDRVSFNENTRY));
	}
	StoreSfnCacheOwned(lpPath, pCopy, nEntries, pNameChange);
}

/*
 * On Windows, arm a directory-name notification before building the map so a
 * rename/create/delete during the build can be detected.  On other platforms,
 * or when the Windows filesystem cannot provide a monitor, build normally and
 * keep an unmonitored cache entry.  The latter deliberately trades some stale
 * namespace safety for cross-platform performance, as requested.
 */
static BRESULT BuildShortNameMapForCache(const OEMCHAR *lpPath,
	HDRVSFNENTRY **ppEntries, UINT *pnEntries, void **ppNameChange)
{
	if (ppNameChange != NULL)
	{
		*ppNameChange = NULL;
	}
#if defined(DOSIO_HAS_DIRMONITOR)
	{
		UINT nAttempt;

		for (nAttempt = 0; nAttempt < 2; nAttempt++)
		{
			void *pNameChange;
			BRESULT r;

			pNameChange = StartSfnNameChangeMonitor(lpPath);
			*ppEntries = NULL;
			*pnEntries = 0;
			r = BuildShortNameMap(lpPath, ppEntries, pnEntries);
			if (r != SUCCESS)
			{
				SfnNameMonitorClose(pNameChange);
				return FAILURE;
			}

			/* Monitoring may be unavailable (network/legacy filesystem etc.).
			 * Keep the freshly built map and cache it unmonitored. */
			if (!SfnNameMonitorValid(pNameChange))
			{
				return SUCCESS;
			}
			if (!file_dirmonitor_changed((FDIRMONH)pNameChange))
			{
				if (ppNameChange != NULL)
				{
					*ppNameChange = pNameChange;
				}
				else
				{
					SfnNameMonitorClose(pNameChange);
				}
				return SUCCESS;
			}

			SfnNameMonitorClose(pNameChange);
			free(*ppEntries);
			*ppEntries = NULL;
			*pnEntries = 0;
		}

		/* A monitor was available but observed repeated namespace changes.
		 * Do not silently downgrade to an unmonitored mapping on this request. */
		return FAILURE;
	}
#else
	/* Backends without monitoring intentionally use a normal cached map. */
	return BuildShortNameMap(lpPath, ppEntries, pnEntries);
#endif
}

static BRESULT GetCachedShortNameMap(const OEMCHAR *lpPath,
	const HDRVSFNENTRY **ppEntries, UINT *pnEntries, BOOL bRequireStableNamespace)
{
	UINT nSlot;
	HDRVSFNENTRY *pEntries;
	UINT nEntries;
	void *pNameChange;

	(void)bRequireStableNamespace;
	nSlot = FindSfnCacheSlot(lpPath);
	if (nSlot != (UINT)-1)
	{
		/* If a monitor exists, a signalled handle makes the whole SFN assignment
		 * stale.  Without a monitor, accept the cached namespace until explicit
		 * HOSTDRV invalidation or an access failure discards it. */
		if (SfnNameMonitorValid(s_sfnCache[nSlot].pNameChange) &&
			!SfnCacheSlotNamespaceStable(nSlot))
		{
			ClearSfnCacheSlot(nSlot);
			nSlot = (UINT)-1;
		}
		else
		{
			s_sfnCache[nSlot].age = ++s_sfnCacheAge;
			*ppEntries = s_sfnCache[nSlot].pEntries;
			*pnEntries = s_sfnCache[nSlot].nEntries;
#if defined(HOSTDRV_SFN_TRACE)
			TRACEOUT(("hostdrv:sfn cache hit %s (%u)%s", lpPath, *pnEntries,
				SfnNameMonitorValid(s_sfnCache[nSlot].pNameChange) ? " monitored" : " unmonitored"));
#endif
			return SUCCESS;
		}
	}

	pEntries = NULL;
	nEntries = 0;
	pNameChange = NULL;
	if (BuildShortNameMapForCache(lpPath, &pEntries, &nEntries, &pNameChange) != SUCCESS)
	{
		return FAILURE;
	}
	StoreSfnCacheOwned(lpPath, pEntries, nEntries, pNameChange);
	nSlot = FindSfnCacheSlot(lpPath);
	if (nSlot == (UINT)-1)
	{
		return FAILURE;
	}
	*ppEntries = s_sfnCache[nSlot].pEntries;
	*pnEntries = s_sfnCache[nSlot].nEntries;
#if defined(HOSTDRV_SFN_TRACE)
	TRACEOUT(("hostdrv:sfn cache miss %s (%u)%s", lpPath, *pnEntries,
		SfnNameMonitorValid(s_sfnCache[nSlot].pNameChange) ? " monitored" : " unmonitored"));
#endif
	return SUCCESS;
}

BRESULT hostdrvs_getshortnamemap(const OEMCHAR *lpPath, HDRVSFNENTRY **ppEntries, UINT *pnEntries)
{
	BRESULT r;
	void *pNameChange;

	pNameChange = NULL;
	r = BuildShortNameMapForCache(lpPath, ppEntries, pnEntries, &pNameChange);
	if (r == SUCCESS)
	{
		StoreSfnCacheCopy(lpPath, *ppEntries, *pnEntries, pNameChange);
	}
	else
	{
		SfnNameMonitorClose(pNameChange);
	}
	return r;
}

void hostdrvs_freeshortnamemap(HDRVSFNENTRY *pEntries)
{
	free(pEntries);
}

BOOL hostdrvs_lookupshortname(const HDRVSFNENTRY *pEntries, UINT nEntries,
							  const OEMCHAR *lpFilename, OEMCHAR *lpShortName, UINT cchShortName)
{
	UINT i;

	if ((lpFilename == NULL) || (lpShortName == NULL) || (cchShortName == 0))
	{
		return FALSE;
	}
	for (i = 0; i < nEntries; i++)
	{
		if (pEntries[i].bAssigned && (HostOemIcmp(pEntries[i].szFilename, lpFilename) == 0))
		{
			Fcb2OemName(lpShortName, cchShortName, pEntries[i].file.fcbname);
			return TRUE;
		}
	}
	lpShortName[0] = '\0';
	return FALSE;
}

BOOL hostdrvs_lookuplongname(const HDRVSFNENTRY *pEntries, UINT nEntries,
							 const OEMCHAR *lpShortName, OEMCHAR *lpFilename, UINT cchFilename,
							 UINT32 *lpAttr)
{
	UINT i;
	OEMCHAR szCandidate[16];

	if ((lpShortName == NULL) || (lpFilename == NULL) || (cchFilename == 0))
	{
		return FALSE;
	}
	for (i = 0; i < nEntries; i++)
	{
		if (!pEntries[i].bAssigned)
		{
			continue;
		}
		Fcb2OemName(szCandidate, NELEMENTS(szCandidate), pEntries[i].file.fcbname);
		if (HostOemIcmp(szCandidate, lpShortName) == 0)
		{
			file_cpyname(lpFilename, pEntries[i].szFilename, cchFilename);
			if (lpAttr != NULL)
			{
				*lpAttr = pEntries[i].file.attr;
			}
			return TRUE;
		}
	}
	lpFilename[0] = '\0';
	return FALSE;
}

/*
 * Cached lookup helpers.  The cache owns the map; callers receive only the
 * translated name, so eviction/invalidation cannot leave dangling pointers.
 */
BOOL hostdrvs_lookupshortnamecached(const OEMCHAR *lpPath, const OEMCHAR *lpFilename,
								 OEMCHAR *lpShortName, UINT cchShortName)
{
	const HDRVSFNENTRY *pEntries;
	UINT nEntries;

	if ((lpPath == NULL) || (lpFilename == NULL) || (lpShortName == NULL) ||
		(cchShortName == 0))
	{
		return FALSE;
	}
	pEntries = NULL;
	nEntries = 0;
	if (GetCachedShortNameMap(lpPath, &pEntries, &nEntries, FALSE) != SUCCESS)
	{
		lpShortName[0] = '\0';
		return FALSE;
	}
	return hostdrvs_lookupshortname(pEntries, nEntries, lpFilename, lpShortName, cchShortName);
}

BOOL hostdrvs_lookuplongnamecached(const OEMCHAR *lpPath, const OEMCHAR *lpShortName,
								 OEMCHAR *lpFilename, UINT cchFilename, UINT32 *lpAttr)
{
	const HDRVSFNENTRY *pEntries;
	UINT nEntries;
	UINT nSlot;
	BOOL bFound;

	if ((lpPath == NULL) || (lpShortName == NULL) || (lpFilename == NULL) ||
		(cchFilename == 0))
	{
		return FALSE;
	}
	pEntries = NULL;
	nEntries = 0;
	if (GetCachedShortNameMap(lpPath, &pEntries, &nEntries, TRUE) != SUCCESS)
	{
		lpFilename[0] = '\0';
		return FALSE;
	}
	bFound = hostdrvs_lookuplongname(pEntries, nEntries, lpShortName,
		lpFilename, cchFilename, lpAttr);

	/* A monitored namespace change must never cause one request to be retried
	 * against a different SFN assignment.  Discard this request and let the
	 * next request rebuild from the new directory state. */
	nSlot = FindSfnCacheSlot(lpPath);
	if (nSlot != (UINT)-1 &&
		SfnNameMonitorValid(s_sfnCache[nSlot].pNameChange) &&
		!SfnCacheSlotNamespaceStable(nSlot))
	{
		ClearSfnCacheSlot(nSlot);
		lpFilename[0] = '\0';
		return FALSE;
	}
	return bFound;
}

#endif	/* defined(SUPPORT_HOSTDRVNT) */
