#include	"compiler.h"

#if defined(SUPPORT_KAI_IMAGES)
/*
 * ANDROID-SAF: CD-ROM イメージを np2kai の diskimage/ (cddfile.c・cd/cdd_*.c) で開く中継ぎ。
 * 形式は拡張子で決める (np2kai と同じ): .cue・.ccd・.cdm・.mds、それ以外は ISO/生イメージ
 * (2048・2352・2448 バイト/セクタ)。.nrg は未対応 (先送り)。
 * 開いたあと、np2kai のトラック表を xnp2 の形 (_CDTRK_NP2) に変換して ATAPI・CD-DA に渡す。
 * cbus/atapicmd.c・cbus/ideio.c は xnp2 のまま (ステートセーブの互換性のため)。
 */
#include	"dosio.h"
#include	"sxsi.h"
#include	"diskimage/img_strres.h"
#include	"diskimage/cddfile.h"
#include	"diskimage/cd/cdd_iso.h"
#include	"diskimage/cd/cdd_cue.h"
#include	"diskimage/cd/cdd_ccd.h"
#include	"diskimage/cd/cdd_mds.h"
#if defined(__ANDROID__)
#include	<android/log.h>
#define	CDLOG(...)	__android_log_print(ANDROID_LOG_INFO, "NP2SAF", __VA_ARGS__)
#else
#define	CDLOG(...)
#endif

BRESULT sxsicd_open(SXSIDEV sxsi, const OEMCHAR *fname) {

const OEMCHAR	*ext;
	REG8		(*read)(SXSIDEV sxsi, long pos, UINT8 *buf, UINT size);
	long		totals;
	BRESULT		r;
	CDINFO		cdinfo;
	UINT		i;

	/* np2kai の関数は失敗しても read・totals を書き換えていることがあるので戻す */
	read = sxsi->read;
	totals = sxsi->totals;
	ext = file_getext(fname);
	if (!file_cmpname(ext, str_cue)) {
		r = opencue(sxsi, fname);
	}
	else if ((!file_cmpname(ext, str_ccd)) || (!file_cmpname(ext, str_cdm))) {
		r = openccd(sxsi, fname);		/* .cdm も読み方は .ccd と同じ (np2kai と同じ) */
	}
	else if (!file_cmpname(ext, str_mds)) {
		r = openmds(sxsi, fname);
	}
	else if (!file_cmpname(ext, str_nrg)) {
		r = FAILURE;					/* 未対応 */
	}
	else {
		r = openiso(sxsi, fname);
	}
	if (r != SUCCESS) {
		sxsi->read = read;
		sxsi->totals = totals;
		CDLOG("cd: cannot open %s", fname);
		return(FAILURE);
	}

	/* トラック表を xnp2 の形にする。[trks] はリードアウト (np2kai が作る) */
	cdinfo = (CDINFO)sxsi->hdl;
	for (i=0; i<=cdinfo->trks; i++) {
		cdinfo->np2trk[i].type = cdinfo->trk[i].adr_ctl;
		cdinfo->np2trk[i].track = cdinfo->trk[i].point;
		cdinfo->np2trk[i].pos = cdinfo->trk[i].pos;
		CDLOG("cd: trk %02x type=%02x pos=%u size=%u img=%u-%u", cdinfo->trk[i].point,
					cdinfo->trk[i].adr_ctl, (unsigned)cdinfo->trk[i].pos,
					(unsigned)cdinfo->trk[i].sector_size,
					(unsigned)cdinfo->trk[i].str_sec, (unsigned)cdinfo->trk[i].end_sec);
	}
	/* 形の値は xnp2 の CD と同じにする (np2kai は 17x8 の形にしている) */
	sxsi->cylinders = 0;
	sxsi->sectors = 1;
	sxsi->surfaces = 1;
	CDLOG("cd: opened %s tracks=%u totals=%ld media=%02x", fname, cdinfo->trks,
												sxsi->totals, sxsi->mediatype);
	return(SUCCESS);
}

CDTRK sxsicd_gettrk(SXSIDEV sxsi, UINT *tracks) {

	CDINFO	cdinfo;

	cdinfo = (CDINFO)sxsi->hdl;
	if (tracks) {
		*tracks = cdinfo->trks;
	}
	return(cdinfo->np2trk);
}

/*
 * CD-DA 用に 1 セクタ (2352 バイト) を読む。pos は CD 上の位置。
 * np2kai の sxsicd_readraw() と同じ計算。CUE の PREGAP (イメージに実体の無いセクタ) を除く処理は、
 * np2kai では ideio.c にあるが、xnp2 の ideio.c は変えないのでここで行う。
 */
BRESULT sxsicd_readraw(SXSIDEV sxsi, long pos, void *buf) {

	CDINFO	cdinfo;
	FILEH	fh;
	FILEPOS	fpos;
	UINT16	secsize;
	SINT32	i;
	UINT32	secs;

	cdinfo = (CDINFO)sxsi->hdl;
	if ((cdinfo == NULL) || (cdinfo->trks == 0) || (pos < 0)) {
		return(FAILURE);
	}
	for (i=cdinfo->trks - 1; i>0; i--) {
		if (cdinfo->trk[i].pos <= (UINT32)pos) {
			break;
		}
	}
	if (pos < (long)cdinfo->trk[i].pregap_offset_ex) {
		ZeroMemory(buf, 2352);			/* 実体の無い PREGAP は無音 */
		return(SUCCESS);
	}
	pos -= (long)cdinfo->trk[i].pregap_offset_ex;
	if (pos >= sxsi->totals) {
		return(FAILURE);
	}

	secsize = 0;
	for (i=cdinfo->trks - 1; i>=0; i--) {
		if (cdinfo->trk[i].pos <= (UINT32)pos) {
			secsize = cdinfo->trk[i].sector_size;
			break;
		}
	}
	if ((secsize == 0) || (secsize == 2048)) {
		return(FAILURE);
	}
	if (sxsi_prepare(sxsi) != SUCCESS) {
		return(FAILURE);
	}
	fh = cdinfo->fh;
	fpos = 0;
	secs = 0;
	for (i=0; i<(SINT32)cdinfo->trks; i++) {
		if ((cdinfo->trk[i].str_sec <= (UINT32)pos) && ((UINT32)pos <= cdinfo->trk[i].end_sec)) {
			fpos += (pos - secs) * cdinfo->trk[i].sector_size;
			break;
		}
		fpos += (FILEPOS)cdinfo->trk[i].sectors * cdinfo->trk[i].sector_size;
		secs += cdinfo->trk[i].sectors;
	}
	fpos += (FILEPOS)cdinfo->trk[0].start_offset;
	if ((file_seek(fh, fpos, FSEEK_SET) != fpos) ||
		(file_read(fh, buf, 2352) != 2352)) {
		return(FAILURE);
	}
	return(SUCCESS);
}

#else	/* SUPPORT_KAI_IMAGES: 以下は xnp2 のまま */

#include	"compiler.h"
#include	"strres.h"
#include	"textfile.h"
#include	"dosio.h"
#include	"sysmng.h"
#include	"cpucore.h"
#include	"pccore.h"
#include	"sxsi.h"


static const UINT8 cd001[7] = {0x01,'C','D','0','0','1',0x01};

typedef struct {
	FILEH	fh;
	UINT	type;
	UINT	trks;
	_CDTRK	trk[100];
	OEMCHAR	path[MAX_PATH];
} _CDINFO, *CDINFO;


// ---- セクタ2048

static int issec2048(FILEH fh) {

	long	fpos;
	UINT8	buf[2048];
	UINT	secsize;
	UINT	fsize;

	fpos = 16 * 2048;
	if (file_seek(fh, fpos, FSEEK_SET) != fpos) {
		goto sec2048_err;
	}
	if (file_read(fh, buf, sizeof(buf)) != sizeof(buf)) {
		goto sec2048_err;
	}
	if (memcmp(buf, cd001, 7) != 0) {
		goto sec2048_err;
	}
	secsize = LOADINTELWORD(buf + 128);
	if (secsize != 2048) {
		goto sec2048_err;
	}
	fsize = file_getsize(fh);
	if ((fsize % 2048) != 0) {
		goto sec2048_err;
	}
	return(fsize / 2048);

sec2048_err:
	return(-1);
}

static REG8 sec2048_read(SXSIDEV sxsi, long pos, UINT8 *buf, UINT size) {

	FILEH	fh;
	UINT	rsize;

	if (sxsi_prepare(sxsi) != SUCCESS) {
		return(0x60);
	}
	if ((pos < 0) || (pos >= sxsi->totals)) {
		return(0x40);
	}
	pos = pos * 2048;
	fh = ((CDINFO)sxsi->hdl)->fh;
	if (file_seek(fh, pos, FSEEK_SET) != pos) {
		return(0xd0);
	}
	while(size) {
		rsize = min(size, 2048);
		CPU_REMCLOCK -= rsize;
		if (file_read(fh, buf, rsize) != rsize) {
			return(0xd0);
		}
		buf += rsize;
		size -= rsize;
	}
	return(0x00);
}


// ---- セクタ2352

static int issec2352(FILEH fh) {

	long	fpos;
	UINT8	buf[2048];
	UINT	secsize;
	UINT	fsize;

	fpos = (16 * 2352) + 16;
	if (file_seek(fh, fpos, FSEEK_SET) != fpos) {
		goto sec2352_err;
	}
	if (file_read(fh, buf, sizeof(buf)) != sizeof(buf)) {
		goto sec2352_err;
	}
	if (memcmp(buf, cd001, 7) != 0) {
		goto sec2352_err;
	}
	secsize = LOADINTELWORD(buf + 128);
	if (secsize != 2048) {
		goto sec2352_err;
	}
	fsize = file_getsize(fh);
	if ((fsize % 2352) != 0) {
		goto sec2352_err;
	}
	return(fsize / 2352);

sec2352_err:
	return(-1);
}

static REG8 sec2352_read(SXSIDEV sxsi, long pos, UINT8 *buf, UINT size) {

	FILEH	fh;
	long	fpos;
	UINT	rsize;

	if (sxsi_prepare(sxsi) != SUCCESS) {
		return(0x60);
	}
	if ((pos < 0) || (pos >= sxsi->totals)) {
		return(0x40);
	}
	fh = ((CDINFO)sxsi->hdl)->fh;
	while(size) {
		fpos = (pos * 2352) + 16;
		if (file_seek(fh, fpos, FSEEK_SET) != fpos) {
			return(0xd0);
		}
		rsize = min(size, 2048);
		CPU_REMCLOCK -= rsize;
		if (file_read(fh, buf, rsize) != rsize) {
			return(0xd0);
		}
		buf += rsize;
		size -= rsize;
		pos++;
	}
	return(0x00);
}


// ----

static BRESULT cd_reopen(SXSIDEV sxsi) {

	CDINFO	cdinfo;
	FILEH	fh;

	cdinfo = (CDINFO)sxsi->hdl;
	fh = file_open_rb(cdinfo->path);
	if (fh != FILEH_INVALID) {
		cdinfo->fh = fh;
		return(SUCCESS);
	}
	else {
		return(FAILURE);
	}
}

static void cd_close(SXSIDEV sxsi) {

	CDINFO	cdinfo;

	cdinfo = (CDINFO)sxsi->hdl;
	file_close(cdinfo->fh);
}

static void cd_destroy(SXSIDEV sxsi) {

	_MFREE((CDINFO)sxsi->hdl);
}


// ----

static const OEMCHAR str_cue[] = OEMTEXT("cue");
static const OEMCHAR str_file[] = OEMTEXT("FILE");
static const OEMCHAR str_track[] = OEMTEXT("TRACK");
static const OEMCHAR str_mode1[] = OEMTEXT("MODE1/2352");
static const OEMCHAR str_index[] = OEMTEXT("INDEX");
static const OEMCHAR str_audio[] = OEMTEXT("AUDIO");


static BRESULT openimg(SXSIDEV sxsi, const OEMCHAR *path,
												const _CDTRK *trk, UINT trks) {

	FILEH	fh;
	UINT	type;
	int		totals;
	CDINFO	cdinfo;
	UINT	mediatype;
	UINT	i;

	fh = file_open_rb(path);
	if (fh == FILEH_INVALID) {
		goto sxsiope_err1;
	}
	type = 2048;
	totals = issec2048(fh);
	if (totals < 0) {
		type = 2352;
		totals = issec2352(fh);
	}
	if (totals < 0) {
		goto sxsiope_err2;
	}
	cdinfo = (CDINFO)_MALLOC(sizeof(_CDINFO), path);
	if (cdinfo == NULL) {
		goto sxsiope_err2;
	}
	ZeroMemory(cdinfo, sizeof(_CDINFO));
	cdinfo->fh = fh;
	cdinfo->type = type;
	if ((trk != NULL) && (trks != 0)) {
		trks = min(trks, NELEMENTS(cdinfo->trk) - 1);
		CopyMemory(cdinfo->trk, trk, trks * sizeof(_CDTRK));
	}
	else {
		cdinfo->trk[0].type = 0x14;
		cdinfo->trk[0].track = 1;
//		cdinfo->trk[0].pos = 0;
		trks = 1;
	}
	mediatype = 0;
	for (i=0; i<trks; i++) {
		if (cdinfo->trk[i].type == 0x14) {
			mediatype |= SXSIMEDIA_DATA;
		}
		else if (cdinfo->trk[i].type == 0x10) {
			mediatype |= SXSIMEDIA_AUDIO;
		}
	}
	cdinfo->trk[trks].type = 0x10;
	cdinfo->trk[trks].track = 0xaa;
	cdinfo->trk[trks].pos = totals;
	cdinfo->trks = trks;
	file_cpyname(cdinfo->path, path, NELEMENTS(cdinfo->path));

	sxsi->reopen = cd_reopen;
	if (type == 2048) {
		sxsi->read = sec2048_read;
	}
	else {
		sxsi->read = sec2352_read;
	}
	sxsi->close = cd_close;
	sxsi->destroy = cd_destroy;
	sxsi->hdl = (INTPTR)cdinfo;
	sxsi->totals = totals;
	sxsi->cylinders = 0;
	sxsi->size = 2048;
	sxsi->sectors = 1;
	sxsi->surfaces = 1;
	sxsi->headersize = 0;
	sxsi->mediatype = mediatype;
	return(SUCCESS);

sxsiope_err2:
	file_close(fh);

sxsiope_err1:
	return(FAILURE);
}

static BRESULT getint2(const OEMCHAR *str, UINT *val) {

	if ((str[0] < '0') || (str[0] > '9') ||
		(str[1] < '0') || (str[1] > '9')) {
		return(FAILURE);
	}
	if (val) {
		*val = ((str[0] - '0') * 10) + (str[1] - '0');
	}
	return(SUCCESS);
}

static UINT32 getpos(const OEMCHAR *str) {

	UINT	m;
	UINT	s;
	UINT	f;

	if ((getint2(str + 0, &m) != SUCCESS) || (str[2] != ':') ||
		(getint2(str + 3, &s) != SUCCESS) || (str[5] != ':') ||
		(getint2(str + 6, &f) != SUCCESS)) {
		return(0);
	}
	return((((m * 60) + s) * 75) + f);
}

static BRESULT opencue(SXSIDEV sxsi, const OEMCHAR *fname) {

	_CDTRK		trk[99];
	OEMCHAR		path[MAX_PATH];
	UINT		idx;
	UINT8		curtrk;
	UINT		curtype;
	TEXTFILEH	tfh;
	OEMCHAR		buf[512];
	OEMCHAR		*argv[8];
	int			argc;

	ZeroMemory(trk, sizeof(trk));
	path[0] = '\0';
	idx = 0;
	curtrk = 1;
	curtype = 0x14;
	tfh = textfile_open(fname, 0x800);
	if (tfh == NULL) {
		return(FAILURE);
	}
	while(textfile_read(tfh, buf, NELEMENTS(buf)) == SUCCESS) {
		argc = milstr_getarg(buf, argv, NELEMENTS(argv));
		if ((argc >= 3) && (!milstr_cmp(argv[0], str_file))) {
			file_cpyname(path, fname, NELEMENTS(path));
			file_cutname(path);
			file_catname(path, argv[1], NELEMENTS(path));
		}
		else if ((argc >= 3) && (!milstr_cmp(argv[0], str_track))) {
			curtrk = (UINT8)milstr_solveINT(argv[1]);
			if (!milstr_cmp(argv[2], str_mode1)) {
				curtype = 0x14;
			}
			else if (!milstr_cmp(argv[2], str_audio)) {
				curtype = 0x10;
			}
		}
		else if ((argc >= 3) && (!milstr_cmp(argv[0], str_index))) {
#if defined(__ANDROID__)	/* ANDROID-SAF: INDEX 00 (プリギャップ) をトラックとして数えない */
			if (milstr_solveINT(argv[1]) != 1) {
				continue;
			}
#endif
			if (idx < NELEMENTS(trk)) {
				trk[idx].type = curtype;
				trk[idx].track = curtrk;
				trk[idx].pos = getpos(argv[2]);
				idx++;
			}
		}
	}
	textfile_close(tfh);
	return(openimg(sxsi, path, trk, idx));
}

BRESULT sxsicd_open(SXSIDEV sxsi, const OEMCHAR *fname) {

const OEMCHAR	*ext;

	ext = file_getext(fname);
	if (!file_cmpname(ext, str_cue)) {
		return(opencue(sxsi, fname));
	}
	return(openimg(sxsi, fname, NULL, 0));
}

CDTRK sxsicd_gettrk(SXSIDEV sxsi, UINT *tracks) {

	CDINFO	cdinfo;

	cdinfo = (CDINFO)sxsi->hdl;
	if (tracks) {
		*tracks = cdinfo->trks;
	}
	return(cdinfo->trk);
}

BRESULT sxsicd_readraw(SXSIDEV sxsi, long pos, void *buf) {

	CDINFO	cdinfo;
	FILEH	fh;
	long	fpos;

	cdinfo = (CDINFO)sxsi->hdl;
	if (cdinfo->type != 2352) {
		return(FAILURE);
	}
	if (sxsi_prepare(sxsi) != SUCCESS) {
		return(FAILURE);
	}
	if ((pos < 0) || (pos >= sxsi->totals)) {
		return(FAILURE);
	}
	fh = ((CDINFO)sxsi->hdl)->fh;
	fpos = pos * 2352;
	if ((file_seek(fh, fpos, FSEEK_SET) != fpos) ||
		(file_read(fh, buf, 2352) != 2352)) {
		return(FAILURE);
	}
	return(SUCCESS);
}


#endif	/* SUPPORT_KAI_IMAGES */
