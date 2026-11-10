/* ANDROID-SAF: FDD イメージの形式を中身 (ヘッダー) から判定する。
 *
 * np2kai の fdd_set() は拡張子だけで形式を決めるため、中身が D88 や NFD の .hdb などは
 * ベタとして扱われてマウントに失敗する。拡張子でベタと決まったものだけについて、
 * 先にここでヘッダーを調べる。どれにも当たらなければ FTYPE_NONE を返し、従来どおり
 * サイズでベタか BKDSK かを決める。
 *
 * DCP は先頭のメディア種別とファイルサイズでしか判定できず、ベタと見分けにくいので
 * 中身からは判定しない。
 */

#include	<compiler.h>
#include	<dosio.h>
#include	<diskimage/fddfile.h>
#include	"diskimage/fddsniff_android.h"

#ifdef SUPPORT_KAI_IMAGES

#include	"diskimage/img_common.h"

#if defined(__ANDROID__)
#include	<android/log.h>
#define	SNIFFLOG(...)	__android_log_print(ANDROID_LOG_INFO, "NP2SAF", __VA_ARGS__)
#else
#define	SNIFFLOG(...)
#endif

enum {
	SNIFF_HEADSIZE	= 0x2b0		/* D88 のヘッダー (トラック 164 本分) */
};

static UINT32 le32(const UINT8 *p) {

	return((UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24));
}

/* D88: ヘッダーのディスクサイズ (0x1c) がファイルサイズと等しく、最初のトラックの位置が
 * ヘッダーの範囲 (0x20〜0x2b0) にあり、ディスク種別 (0x1b) が 2D/2DD/2HD のときだけ D88 とする。
 * ヘッダーが 0x2b0 より短いイメージ (トラック 160 本分の 0x2a0 など) もあるので、
 * 最初のトラックの位置より前にあるトラック表だけを調べる。 */
static BOOL isd88(const UINT8 *head, UINT rsize, UINT32 fsize) {

	UINT32	first;
	UINT32	ptr;
	UINT	tracks;
	UINT	i;

	if ((rsize < SNIFF_HEADSIZE) || (le32(head + 0x1c) != fsize)) {
		return(FALSE);
	}
	if ((head[0x1b] != 0x00) && (head[0x1b] != 0x10) && (head[0x1b] != 0x20)) {
		return(FALSE);
	}
	first = 0;
	for (i = 0; i < 164; i++) {
		ptr = le32(head + 0x20 + (i * 4));
		if ((ptr != 0) && ((first == 0) || (ptr < first))) {
			first = ptr;
		}
	}
	if ((first <= 0x20) || (first > SNIFF_HEADSIZE) || ((first - 0x20) & 3)) {
		return(FALSE);
	}
	tracks = (first - 0x20) / 4;
	for (i = 0; i < tracks; i++) {
		ptr = le32(head + 0x20 + (i * 4));
		if ((ptr != 0) && (ptr >= fsize)) {
			return(FALSE);
		}
	}
	return(TRUE);
}

UINT fdd_sniff_android(const OEMCHAR *fname) {

	FILEH	fh;
	UINT8	head[SNIFF_HEADSIZE];
	UINT	rsize;
	UINT32	fsize;
	UINT	ftype;

	fh = file_open_rb(fname);
	if (fh == FILEH_INVALID) {
		return(FTYPE_NONE);
	}
	fsize = (UINT32)file_getsize(fh);
	rsize = file_read(fh, head, sizeof(head));
	file_close(fh);

	ftype = FTYPE_NONE;
	if ((rsize >= 15) &&
		((!memcmp(head, "T98FDDIMAGE.R0", 15)) || (!memcmp(head, "T98FDDIMAGE.R1", 15)))) {
		ftype = FTYPE_NFD;
	}
	else if ((rsize >= 8) &&
		((!memcmp(head, "VFD1.00", 8)) || (!memcmp(head, "VFD1.01", 8)))) {
		ftype = FTYPE_VFDD;
	}
	else if (isd88(head, rsize, fsize)) {
		ftype = FTYPE_D88;
	}
	if (ftype != FTYPE_NONE) {
		SNIFFLOG("fdd: %s -> %s (by header)", file_getname(fname),
			(ftype == FTYPE_NFD) ? "NFD" : (ftype == FTYPE_VFDD) ? "VFDD" : "D88");
	}
	return(ftype);
}

#endif	/* SUPPORT_KAI_IMAGES */
