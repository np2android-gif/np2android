/**
 * @file	np2bridge.c
 * @brief	Kotlin (jp.sawada.np2android.Np2Native) との JNI 境界
 */

#include "compiler.h"
#include <jni.h>
#include <android/log.h>
#include "np2bridge.h"
#include "dosio.h"
#include "mousemng.h"
#include "keystat.h"
#include "pccore.h"
#include "statsave.h"
#include "safesave.h"
#include "fdd/diskdrv.h"
#include "vramhdl.h"
#include "menubase.h"
#include "sysmenu.h"
#include "taskmng.h"
#include "android_safio.h"
#include "sysmng.h"

#include "fdd/fddfile.h"
#include "fdd/sxsi.h"
#include "fdd/newdisk.h"
#include "hostdrv.h"
#include <errno.h>
#include <sys/stat.h>

#define LOG_TAG "np2"

float np2bridge_mousespeed = 1.0f;

/* ---- Kotlin へのコールバック (エミュレータスレッドから呼ぶ) ---------------- */

/* Np2Native.onNativeEvent(type, a, b, text) の type */
enum {
	EV_STATE_SAVED		= 1,	/* a = slot, b = 0:成功 / 1:失敗 */
	EV_STATE_LOADED		= 2,	/* a = slot, b = 0:成功 / 1:失敗(非互換) / 2:ファイル無し */
	EV_STATE_CONFLICT	= 3,	/* a = slot, text = 差分情報 (force 付きで再ロードするか確認) */
	EV_USER_EXIT		= 4,	/* メニューから終了した (Activity 終了後にプロセスも終わらせてよい) */
	EV_DISK_ERROR		= 5,	/* text = SAF の仮想パス。書き込みに失敗した */
	EV_FDD_SET			= 6,	/* a = drive, b = 0:成功 / 1:成功 (書き込み禁止) / 2:失敗, text = パス */
	EV_HDD_FAILED		= 7,	/* a = sxsi drive, text = パス。リセット時に開けなかった */
	EV_CD_SET			= 8,	/* b = 0:挿入 / 1:失敗 / 2:ドライブ無し (IDE 無効) / 3:取り出し, text = パス */
	EV_RESUME			= 9		/* b = 0:ディスクが違う (text = 差分。answerResume で答える) / 1:読めない */
};

static jclass		s_nativeClass;
static jmethodID	s_onNativeEvent;

/*
 * エミュレータスレッド (SDL_main) は SDL が作ったネイティブスレッドで、そこでの FindClass は
 * システムのクラスローダーを使うためアプリのクラスが見つからない。ロード時に解決しておく。
 */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
	JNIEnv *env;
	jclass cls;

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
		return JNI_ERR;
	}
	cls = (*env)->FindClass(env, "jp/sawada/np2android/Np2Native");
	if (cls != NULL) {
		s_nativeClass = (jclass)(*env)->NewGlobalRef(env, cls);
		s_onNativeEvent = (*env)->GetStaticMethodID(env, cls, "onNativeEvent",
													"(IIILjava/lang/String;)V");
		(*env)->DeleteLocalRef(env, cls);
	}
	if ((s_nativeClass == NULL) || (s_onNativeEvent == NULL)) {
		(*env)->ExceptionClear(env);
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Np2Native.onNativeEvent not found");
	}
	return JNI_VERSION_1_6;
}

/*
 * NewStringUTF は (modified) UTF-8 として不正なバイト列を渡すと CheckJNI で abort する。
 * ファイル名やステート情報に SJIS 等が混ざっても落ちないよう、1〜3 バイトの正しい並びだけ通し
 * それ以外は '?' に置き換える。
 */
static jstring newstring(JNIEnv *env, const char *src)
{
	char buf[1024];
	const unsigned char *p = (const unsigned char *)src;
	size_t n = 0;
	int len;
	int i;

	while ((*p != '\0') && (n + 4 < sizeof(buf))) {
		if (*p < 0x80) {
			len = 1;
		}
		else if ((*p & 0xe0) == 0xc0) {
			len = 2;
		}
		else if ((*p & 0xf0) == 0xe0) {
			len = 3;
		}
		else {
			len = 0;
		}
		for (i = 1; i < len; i++) {
			if ((p[i] & 0xc0) != 0x80) {
				len = 0;
				break;
			}
		}
		if (len == 0) {
			buf[n++] = '?';
			p++;
			continue;
		}
		SDL_memcpy(buf + n, p, len);
		n += len;
		p += len;
	}
	buf[n] = '\0';
	return (*env)->NewStringUTF(env, buf);
}

static void notify(int type, int a, int b, const char *text)
{
	JNIEnv *env;
	jstring jtext = NULL;

	if ((s_nativeClass == NULL) || (s_onNativeEvent == NULL)) {
		return;
	}
	env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	if (env == NULL) {
		return;
	}
	if (text != NULL) {
		jtext = newstring(env, text);
	}
	(*env)->CallStaticVoidMethod(env, s_nativeClass, s_onNativeEvent, type, a, b, jtext);
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
	}
	if (jtext != NULL) {
		(*env)->DeleteLocalRef(env, jtext);
	}
}

/* ---- UI スレッド側: イベントを積むだけ ---------------------------------- */

static void push(Sint32 code, intptr_t a, intptr_t b)
{
	SDL_Event e;

	SDL_zero(e);
	e.type = SDL_USEREVENT;
	e.user.code = code;
	e.user.data1 = (void *)a;
	e.user.data2 = (void *)b;
	if (SDL_PushEvent(&e) <= 0) {
		/* キュー未初期化 (起動直後) などで積めなかったら所有権のある文字列を解放 */
		if ((code == NP2CMD_SETFDD) || (code == NP2CMD_SETHDD) || (code == NP2CMD_SETCD)) {
			SDL_free((void *)b);
		}
	}
}

static char *dupstring(JNIEnv *env, jstring s)
{
	const char *utf;
	char *ret;

	if (s == NULL) {
		return NULL;
	}
	utf = (*env)->GetStringUTFChars(env, s, NULL);
	if (utf == NULL) {
		return NULL;
	}
	ret = SDL_strdup(utf);
	(*env)->ReleaseStringUTFChars(env, s, utf);
	return ret;
}

#define NP2JNI(name)	JNIEXPORT void JNICALL Java_jp_sawada_np2android_Np2Native_##name

NP2JNI(keyDown)(JNIEnv *env, jclass cls, jint code)
{
	push(NP2CMD_KEYDOWN, code & 0x7f, 0);
}

NP2JNI(keyUp)(JNIEnv *env, jclass cls, jint code)
{
	push(NP2CMD_KEYUP, code & 0x7f, 0);
}

NP2JNI(releaseAllKeys)(JNIEnv *env, jclass cls)
{
	push(NP2CMD_KEYALLRELEASE, 0, 0);
}

NP2JNI(mouseButton)(JNIEnv *env, jclass cls, jint button, jboolean down)
{
	push(NP2CMD_MOUSEBUTTON, button, down ? 1 : 0);
}

NP2JNI(mouseMove)(JNIEnv *env, jclass cls, jint dx, jint dy)
{
	push(NP2CMD_MOUSEMOVE, dx, dy);
}

NP2JNI(setMouseSpeed)(JNIEnv *env, jclass cls, jfloat speed)
{
	/* float 1 個の書き込みなので atomic 扱いで十分 */
	np2bridge_mousespeed = speed;
}

NP2JNI(setFdd)(JNIEnv *env, jclass cls, jint drive, jstring path)
{
	push(NP2CMD_SETFDD, drive, (intptr_t)dupstring(env, path));
}

NP2JNI(setHdd)(JNIEnv *env, jclass cls, jint drive, jstring path)
{
	push(NP2CMD_SETHDD, drive, (intptr_t)dupstring(env, path));
}

/* CD-ROM (IDE セカンダリ・マスター) の挿入・取り出し。HDD と違い、すぐに反映される */
NP2JNI(setCd)(JNIEnv *env, jclass cls, jstring path)
{
	push(NP2CMD_SETCD, 0, (intptr_t)dupstring(env, path));
}

/*
 * 起動時に挿入しておく CD-ROM のパス (null なら無し)。エミュレータスレッドが動き出す前
 * (onCreate) に呼ぶこと。np2bridge_startupcd() が使う。
 */
static char s_startupcd[MAX_PATH];

NP2JNI(setStartupCd)(JNIEnv *env, jclass cls, jstring path)
{
	const char *utf;

	s_startupcd[0] = '\0';
	if (path != NULL) {
		utf = (*env)->GetStringUTFChars(env, path, NULL);
		if (utf != NULL) {
			SDL_strlcpy(s_startupcd, utf, sizeof(s_startupcd));
			(*env)->ReleaseStringUTFChars(env, path, utf);
		}
	}
}

/*
 * ANDROID-HOSTDRV: ホストドライブの設定。設定の正は Kotlin の Settings で、np2.cfg には保存しない。
 * 起動時の値はエミュレータスレッドが動き出す前 (onCreate) に渡しておき、np2bridge_hostdrv() が
 * initload の後に読む。動作中の変更はイベントを積み、エミュレータスレッドで反映する。
 */
enum {
	HOSTDRV_ENABLE	= 0x01,
	HOSTDRV_WRITE	= 0x02,
	HOSTDRV_DELETE	= 0x04
};
static SDL_atomic_t s_hostdrv;

NP2JNI(setHostDrv)(JNIEnv *env, jclass cls, jboolean enabled, jboolean write, jboolean del)
{
	int v;

	v = 0;
	if (enabled) {
		v |= HOSTDRV_ENABLE;
		if (write) {
			v |= HOSTDRV_WRITE;
		}
		if (del) {
			v |= HOSTDRV_DELETE;
		}
	}
	SDL_AtomicSet(&s_hostdrv, v);
	push(NP2CMD_SETHOSTDRV, 0, 0);		/* 起動前で積めなくても、起動時に np2bridge_hostdrv() が読む */
}

void np2bridge_hostdrv(void)
{
	int		v;
	char	root[MAX_PATH];

	v = SDL_AtomicGet(&s_hostdrv);
	root[0] = '\0';
	if (v & HOSTDRV_ENABLE) {
		file_cpyname(root, file_getcd("HOSTDRV/"), NELEMENTS(root));
		if ((mkdir(root, 0777) != 0) && (errno != EEXIST)) {
			__android_log_print(ANDROID_LOG_ERROR, "NP2HDRV", "cannot create %s: %s", root, strerror(errno));
			root[0] = '\0';
		}
	}
	if ((root[0] == '\0') && (hostdrv.stat.is_mount)) {
		/* 開いているファイルを閉じて外す。HOSTDRV.COM は常駐したままなので、
		 * ゲストからの要求は次の処理に回されてエラーになる。使えるのはゲストを再起動して常駐し直してから */
		__android_log_print(ANDROID_LOG_INFO, "NP2HDRV", "unmount");
		hostdrv_reset();
	}
	file_cpyname(np2cfg.hdrvroot, root, NELEMENTS(np2cfg.hdrvroot));
	np2cfg.hdrvacc = HDFMODE_READ;
	if (root[0] != '\0') {
		if (v & HOSTDRV_WRITE) {
			np2cfg.hdrvacc |= HDFMODE_WRITE;
		}
		if (v & HOSTDRV_DELETE) {
			np2cfg.hdrvacc |= HDFMODE_DELETE;
		}
	}
	__android_log_print(ANDROID_LOG_INFO, "NP2HDRV", "root=\"%s\" acc=%d", np2cfg.hdrvroot, np2cfg.hdrvacc);
}

/*
 * SAF の fd を仮想パスとして登録する (android_safio.h)。fd は C 側で dup するので、
 * 呼び出し後に Kotlin 側で ParcelFileDescriptor を閉じてよい。
 * 戻り値の仮想パスを setFdd / setHdd に渡す。失敗したら null。
 */
JNIEXPORT jstring JNICALL Java_jp_sawada_np2android_Np2Native_safRegister(JNIEnv *env, jclass cls,
								jstring id, jstring name, jint fd, jboolean readOnly)
{
	char *cid;
	char *cname;
	char path[MAX_PATH];
	BRESULT r;

	cid = dupstring(env, id);
	cname = dupstring(env, name);
	r = FAILURE;
	if ((cid != NULL) && (cname != NULL)) {
		r = safio_register(cid, cname, fd, readOnly ? TRUE : FALSE, path, sizeof(path));
	}
	SDL_free(cid);
	SDL_free(cname);
	return (r == SUCCESS) ? newstring(env, path) : NULL;
}

/*
 * 空のディスクイメージを作る (np2 の newdisk.c)。ファイルを書くだけでエミュレータの状態には
 * 触れないので、呼び出し元のスレッド (Kotlin のワーカースレッド) でそのまま実行する。
 * newdisk_* は結果を返さないので、作ったファイルを開いてサイズで確かめる。
 */
JNIEXPORT jboolean JNICALL Java_jp_sawada_np2android_Np2Native_createDisk(JNIEnv *env, jclass cls,
								jstring path, jint type, jint param)
{
	char *cpath;
	FILEH fh;
	UINT size;

	cpath = dupstring(env, path);
	if (cpath == NULL) {
		return JNI_FALSE;
	}
	switch (type) {
		case 0:		/* FDD (D88)。param = fd_type */
			newdisk_fdd(cpath, (REG8)param, "");
			break;

		case 1:		/* SASI (hdi)。param = SASI の型番 */
			newdisk_hdi(cpath, (UINT)param);
			break;

		case 2:		/* IDE (nhd)。param = MB */
			newdisk_nhd(cpath, (UINT)param);
			break;

		case 3:		/* SCSI (Virtual98 hdd)。param = MB */
			newdisk_vhd(cpath, (UINT)param);
			break;

		default:
			break;
	}
	size = 0;
	fh = file_open_rb(cpath);
	if (fh != FILEH_INVALID) {
		size = file_getsize(fh);
		file_close(fh);
	}
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "newdisk type=%d param=%d %s: %u bytes",
						(int)type, (int)param, cpath, size);
	SDL_free(cpath);
	return (size > 0) ? JNI_TRUE : JNI_FALSE;
}

/* 登録を解除する。使用中のドライブがあれば、その後の読み書きは失敗する */
/* .cue が使う .bin を、登録済みの ID に付ける (android_safio.h)。fd は C 側で dup する */
JNIEXPORT jboolean JNICALL Java_jp_sawada_np2android_Np2Native_safRegisterSub(JNIEnv *env, jclass cls,
								jstring id, jint fd)
{
	char *cid;
	BRESULT r;

	cid = dupstring(env, id);
	r = (cid != NULL) ? safio_register_sub(cid, fd) : FAILURE;
	SDL_free(cid);
	return (r == SUCCESS) ? JNI_TRUE : JNI_FALSE;
}

NP2JNI(safUnregister)(JNIEnv *env, jclass cls, jstring id)
{
	char *cid;

	cid = dupstring(env, id);
	safio_unregister(cid);
	SDL_free(cid);
}

NP2JNI(toggleMenu)(JNIEnv *env, jclass cls)
{
	push(NP2CMD_MENU, 0, 0);
}

NP2JNI(reset)(JNIEnv *env, jclass cls)
{
	push(NP2CMD_RESET, 0, 0);
}

NP2JNI(requestRedraw)(JNIEnv *env, jclass cls)
{
	push(NP2CMD_REDRAW, 0, 0);
}

NP2JNI(saveState)(JNIEnv *env, jclass cls, jint slot)
{
	push(NP2CMD_STATESAVE, slot, 0);
}

NP2JNI(loadState)(JNIEnv *env, jclass cls, jint slot, jboolean force)
{
	push(NP2CMD_STATELOAD, slot, force ? 1 : 0);
}

NP2JNI(exit)(JNIEnv *env, jclass cls)
{
	push(NP2CMD_EXIT, 0, 0);
}

/*
 * 表示用の参照系。エミュレータスレッドと同期しないが、ポインタ/固定長文字列の読み取りだけで
 * 書き換え中に読んでも表示が一瞬古くなる程度なので許容する。
 */
JNIEXPORT jboolean JNICALL Java_jp_sawada_np2android_Np2Native_isMenuOpen(JNIEnv *env, jclass cls)
{
	return (menuvram != NULL) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_jp_sawada_np2android_Np2Native_getFdd(JNIEnv *env, jclass cls, jint drive)
{
	char buf[MAX_PATH];
	const char *name;

	if ((drive < 0) || (drive >= 4) || (!fdd_diskready((REG8)drive))) {
		return NULL;
	}
	name = fdd_diskname((REG8)drive);
	if ((name == NULL) || (name[0] == '\0')) {
		return NULL;
	}
	SDL_strlcpy(buf, name, sizeof(buf));
	return newstring(env, buf);
}

JNIEXPORT jstring JNICALL Java_jp_sawada_np2android_Np2Native_getHdd(JNIEnv *env, jclass cls, jint drive)
{
	char buf[MAX_PATH];
	const char *name;

	name = diskdrv_getsxsi((REG8)drive);
	if ((name == NULL) || (name[0] == '\0')) {
		return NULL;
	}
	SDL_strlcpy(buf, name, sizeof(buf));
	return newstring(env, buf);
}

/*
 * ドキュメントプロバイダー用: エミュレータが使っている (またはリセット後に使う) ディスクイメージのパス。
 * 上書きや削除で壊さないよう、プロバイダーはこれらへの書き込みを拒否する。
 * HDD は「実際に開いているファイル」と「np2.cfg 上の設定 (リセット後に開く)」の両方を返す。
 */
JNIEXPORT jobjectArray JNICALL Java_jp_sawada_np2android_Np2Native_getDisksInUse(JNIEnv *env, jclass cls)
{
	/* 0x02 は CD-ROM (np2.cfg には保存されないので、開いているファイルだけが返る) */
	static const REG8 hdds[] = {0x00, 0x01, 0x02, 0x20, 0x21, 0x22, 0x23};
	const char *names[4 + 2 * (sizeof(hdds) / sizeof(hdds[0]))];
	char buf[MAX_PATH];
	jclass strcls;
	jobjectArray ret;
	jstring s;
	int n = 0;
	int i;

	for (i = 0; i < 4; i++) {
		if (fdd_diskready((REG8)i)) {
			names[n++] = fdd_diskname((REG8)i);
		}
	}
	for (i = 0; i < (int)(sizeof(hdds) / sizeof(hdds[0])); i++) {
		names[n++] = sxsi_getfilename(hdds[i]);
		names[n++] = diskdrv_getsxsi(hdds[i]);
	}

	strcls = (*env)->FindClass(env, "java/lang/String");
	if (strcls == NULL) {
		return NULL;
	}
	ret = (*env)->NewObjectArray(env, n, strcls, NULL);
	if (ret == NULL) {
		return NULL;
	}
	for (i = 0; i < n; i++) {
		if ((names[i] == NULL) || (names[i][0] == '\0')) {
			continue;
		}
		SDL_strlcpy(buf, names[i], sizeof(buf));
		s = newstring(env, buf);
		if (s != NULL) {
			(*env)->SetObjectArrayElement(env, ret, i, s);
			(*env)->DeleteLocalRef(env, s);
		}
	}
	return ret;
}

/* ---- エミュレータスレッド側 ------------------------------------------- */

/* ---- ステートのディスクの確認 (16 章) ---- */

/* checkdisks() の戻り値に足すビット: Kotlin に確認するべき違いがある */
#define CHECK_CONFLICT	0x10000

#define DISKCHECK_MAX	16

typedef struct {
	char	dv[8];				/* "FDD1" / "SASI1" / "SCSI0" など (statsave.c の名前) */
	int		state;				/* 0: 変わっていない / 1: 書き換えられた / 2: 見つからない */
	char	path[MAX_PATH];		/* セーブ時のパス ("" = 空) */
} DISKCHECK;

static DISKCHECK s_diskcheck[DISKCHECK_MAX];
static int s_diskchecks;

static void diskcheck_cb(const char *dvname, const char *path, int state)
{
	DISKCHECK *d;

	if (s_diskchecks >= DISKCHECK_MAX) {
		return;
	}
	d = &s_diskcheck[s_diskchecks++];
	SDL_strlcpy(d->dv, dvname, sizeof(d->dv));
	SDL_strlcpy(d->path, path, sizeof(d->path));
	d->state = state;
}

/* 今そのドライブで開いているファイル ("" = 無し) */
static const char *sxsi_now(REG8 drv)
{
	SXSIDEV sxsi;

	sxsi = sxsi_getptr(drv);
	if ((sxsi != NULL) && (sxsi->flag & SXSIFLAG_READY)) {
		return sxsi->fname;
	}
	return "";
}

static const char *fdd_now(REG8 drv)
{
	const char *name;

	if (!fdd_diskready(drv)) {
		return "";
	}
	name = fdd_diskname(drv);
	return (name != NULL) ? name : "";
}

/* statsave.c の名前 → ドライブ番号。FDD なら *isfdd = TRUE */
static BOOL dvdrive(const char *dv, REG8 *drv, BOOL *isfdd)
{
	int n;

	if (SDL_strncmp(dv, "FDD", 3) == 0) {
		n = SDL_atoi(dv + 3) - 1;
		*isfdd = TRUE;
	}
	else if (SDL_strncmp(dv, "SASI", 4) == 0) {
		n = SDL_atoi(dv + 4) - 1;
		*isfdd = FALSE;
	}
	else if (SDL_strncmp(dv, "SCSI", 4) == 0) {
		n = 0x20 + SDL_atoi(dv + 4);
		*isfdd = FALSE;
	}
	else {
		return FALSE;
	}
	if (n < 0) {
		return FALSE;
	}
	*drv = (REG8)n;
	return TRUE;
}

/* HDD (CD-ROM 以外の sxsi のドライブ) か。違いがあれば確認を出す */
static BOOL ishdd(REG8 drv)
{
	return (drv == 0x00) || (drv == 0x01) || (drv >= 0x20);
}

static void addline(char *info, int size, const char *dv, const char *kind, const char *saved, const char *now)
{
	char line[300];

	SDL_snprintf(line, sizeof(line), "%s\t%s\t%s\t%s\n", dv, kind,
				 (saved[0]) ? file_getname(saved) : "", (now[0]) ? file_getname(now) : "");
	SDL_strlcat(info, line, size);
}

/*
 * ステートのディスクを今の状態と比べる。statsave_check() の結果を返し、Kotlin に確認するべき違いが
 * あれば CHECK_CONFLICT を足す (セーブ後に書き換えられた・見つからない HDD などがある、または
 * HDD が今つながっているものと違う)。info には違いを 1 行ずつ "名前\t種類\tセーブ時\t今\n" で入れる。
 * 種類: updated (書き換えられた) / notfound (見つからない) / change (ロードで切り替わる) /
 *       gone (ロードで外れる)。ファイル名だけを入れる (表示用)
 */
static int checkdisks(const char *path, char *info, int size)
{
	char err[1024];
	BOOL seen[0x24];
	BOOL conflict;
	BOOL isfdd;
	REG8 drv;
	const char *now;
	int ret;
	int i;

	info[0] = '\0';
	err[0] = '\0';
	s_diskchecks = 0;
	statsave_diskcb = diskcheck_cb;
	ret = statsave_check(path, err, sizeof(err));
	statsave_diskcb = NULL;
	if (ret & (~STATFLAG_DISKCHG)) {
		return ret;
	}
	conflict = (ret & STATFLAG_DISKCHG) ? TRUE : FALSE;
	SDL_memset(seen, 0, sizeof(seen));
	for (i = 0; i < s_diskchecks; i++) {
		DISKCHECK *d = &s_diskcheck[i];
		if (!dvdrive(d->dv, &drv, &isfdd)) {
			continue;
		}
		now = (isfdd) ? fdd_now(drv) : sxsi_now(drv);
		if (!isfdd && drv < NELEMENTS(seen)) {
			seen[drv] = TRUE;
		}
		if (d->state == 1) {
			addline(info, size, d->dv, "updated", d->path, now);
		}
		else if (d->state == 2) {
			addline(info, size, d->dv, "notfound", d->path, now);
		}
		else if (d->path[0] == '\0') {
			/* セーブ時は空。FDD はそのまま (ロードで取り出されない)、sxsi は外れる */
			if ((!isfdd) && (now[0])) {
				addline(info, size, d->dv, "gone", d->path, now);
				conflict |= ishdd(drv);
			}
		}
		else if (SDL_strcmp(d->path, now) != 0) {
			addline(info, size, d->dv, "change", d->path, now);
			if (!isfdd) {
				conflict |= ishdd(drv);
			}
		}
	}
	/* セーブ時につながっていなかった sxsi のドライブは、ロードで外れる */
	for (drv = 0; drv < NELEMENTS(seen); drv++) {
		char dv[8];
		if ((drv >= 0x04) && (drv < 0x20)) {
			continue;
		}
		if (seen[drv]) {
			continue;
		}
		now = sxsi_now(drv);
		if (now[0] == '\0') {
			continue;
		}
		if (drv < 0x20) {
			SDL_snprintf(dv, sizeof(dv), "SASI%d", drv + 1);
		}
		else {
			SDL_snprintf(dv, sizeof(dv), "SCSI%d", drv - 0x20);
		}
		addline(info, size, dv, "gone", "", now);
		conflict |= ishdd(drv);
	}
	if (conflict) {
		ret |= CHECK_CONFLICT;
		__android_log_print(ANDROID_LOG_INFO, "NP2SAF", "state check: %s", info);
	}
	return ret;
}

/*
 * ロードのあと、np2.cfg の HDD を実際に開いているファイルに合わせる (メニューの表示と次のリセットを
 * ロードした状態と一致させる)。CD-ROM は np2.cfg に無いので Kotlin 側 (onStateLoaded) で合わせる
 */
static void syncdisks(void)
{
	BOOL changed = FALSE;
	const char *now;
	REG8 drv;

	for (drv = 0; drv < 2; drv++) {
		now = (sxsi_getdevtype(drv) == SXSIDEV_HDD) ? sxsi_now(drv) : "";
		if (SDL_strcmp(np2cfg.sasihdd[drv], now) != 0) {
			__android_log_print(ANDROID_LOG_INFO, "NP2SAF", "sync hdd%d: %s -> %s", drv, np2cfg.sasihdd[drv], now);
			file_cpyname(np2cfg.sasihdd[drv], now, NELEMENTS(np2cfg.sasihdd[0]));
			changed = TRUE;
		}
	}
#if defined(SUPPORT_SCSI)
	for (drv = 0; drv < 4; drv++) {
		now = (sxsi_getdevtype((REG8)(0x20 + drv)) == SXSIDEV_HDD) ? sxsi_now((REG8)(0x20 + drv)) : "";
		if (SDL_strcmp(np2cfg.scsihdd[drv], now) != 0) {
			__android_log_print(ANDROID_LOG_INFO, "NP2SAF", "sync scsi%d: %s -> %s", drv, np2cfg.scsihdd[drv], now);
			file_cpyname(np2cfg.scsihdd[drv], now, NELEMENTS(np2cfg.scsihdd[0]));
			changed = TRUE;
		}
	}
#endif
	if (changed) {
		sysmng_update(SYS_UPDATECFG | SYS_UPDATEHDD);	/* np2.cfg を書く */
	}
}

static void statefilename(char *path, int size, int slot)
{
	char name[32];

	SDL_snprintf(name, sizeof(name), "np2sdl2.s%02d", slot);
	file_cpyname(path, file_getcd(name), size);
}

static void statesave(int slot)
{
	char path[MAX_PATH];
	int ret;

	statefilename(path, sizeof(path), slot);
	/* 失敗しても (途中で止められても) 前のセーブは残す */
	ret = safesave_statsave(path);
	__android_log_print(ret ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, LOG_TAG,
						"state save slot %d: %s", slot, ret ? "failed" : "ok");
	notify(EV_STATE_SAVED, slot, ret ? 1 : 0, NULL);
}

static void stateload(int slot, BOOL force)
{
	char path[MAX_PATH];
	char info[1024];
	int ret;

	statefilename(path, sizeof(path), slot);
	if (file_attr(path) == -1) {
		notify(EV_STATE_LOADED, slot, 2, NULL);
		return;
	}
	ret = checkdisks(path, info, sizeof(info));
	if (ret & (~(STATFLAG_DISKCHG | CHECK_CONFLICT))) {
		__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "state load slot %d: incompatible (%d)", slot, ret);
		notify(EV_STATE_LOADED, slot, 1, NULL);
		return;
	}
	if ((ret & CHECK_CONFLICT) && (!force)) {
		/* ディスクがセーブ時と違う: Kotlin 側で確認して force=true で再要求してもらう */
		notify(EV_STATE_CONFLICT, slot, 0, info);
		return;
	}
	statsave_load(path);
	syncdisks();
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "state load slot %d: ok", slot);
	notify(EV_STATE_LOADED, slot, 0, NULL);
}

static SDL_atomic_t s_resumeanswer;		/* -1 = 未回答 / 0 = ロード / 1 = ロードせず起動 / 2 = 終了 */

/* Kotlin: レジュームの確認の答え (Np2Native.answerResume → nativeAnswerResume) */
JNIEXPORT void JNICALL Java_jp_sawada_np2android_Np2Native_nativeAnswerResume(JNIEnv *env, jclass cls, jint answer)
{
	SDL_AtomicSet(&s_resumeanswer, answer);
}

BOOL np2bridge_resume(const char *path)
{
	char info[1024];
	int ret;
	int answer;
	SDL_Event e;

	if (file_attr(path) == -1) {
		return FALSE;		/* 初回起動など */
	}
	ret = checkdisks(path, info, sizeof(info));
	if (ret & (~(STATFLAG_DISKCHG | CHECK_CONFLICT))) {
		__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "resume: incompatible (%d)", ret);
		notify(EV_RESUME, 0, 1, NULL);
		return FALSE;
	}
	if (ret & CHECK_CONFLICT) {
		/* Kotlin のダイアログの答えを待つ。Activity が終わるとき (SDL_QUIT) は終了扱い */
		SDL_AtomicSet(&s_resumeanswer, -1);
		notify(EV_RESUME, 0, 0, info);
		while ((answer = SDL_AtomicGet(&s_resumeanswer)) < 0) {
			SDL_Delay(50);
			SDL_PumpEvents();
			if (SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) > 0) {
				answer = 2;
				break;
			}
		}
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "resume: answer %d", answer);
		if (answer == 2) {
			taskmng_userexit = TRUE;	/* メニューの「終了」と同じく、プロセスも終わらせる */
			return TRUE;
		}
		if (answer != 0) {
			return FALSE;
		}
	}
	statsave_load(path);
	syncdisks();
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "resume: ok");
	return FALSE;
}

void np2bridge_diskerror(const char *path)
{
	notify(EV_DISK_ERROR, 0, 0, path);
}

void np2bridge_fddresult(REG8 drv, const char *path, BOOL success)
{
	int result;

	if (!success) {
		result = 2;
	}
	else {
		result = fdd_diskprotect(drv) ? 1 : 0;
	}
	__android_log_print(success ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, LOG_TAG,
						"fdd%d: %s %s", (int)drv, path, success ? "mounted" : "rejected");
	notify(EV_FDD_SET, drv, result, path);
}

void np2bridge_hddfailed(REG8 drv, const char *path)
{
	if ((path == NULL) || (path[0] == '\0')) {
		return;		/* 設定されていないだけ */
	}
	__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "hdd%02x: %s rejected", (int)drv, path);
	notify(EV_HDD_FAILED, drv, 0, path);
}

/* ---- CD-ROM ---- */

#define CD_DRV	0x02	/* sxsi のドライブ番号 (IDE セカンダリ・マスター。pccore.c) */

/*
 * CD-ROM を入れ替える。path が NULL か空なら取り出すだけ。
 * sxsi_devopen() は前のイメージを閉じないので (fd とメモリが漏れる)、必ず先に sxsi_devclose() する。
 */
static BOOL cdset(const char *path, BOOL quiet)
{
	if (sxsi_getdevtype(CD_DRV) != SXSIDEV_CDROM) {
		__android_log_print(ANDROID_LOG_WARN, "NP2SAF", "cd: no drive (IDE disabled)");
		notify(EV_CD_SET, 0, 2, path);
		return FALSE;
	}
	sxsi_devclose(CD_DRV);
	if ((path == NULL) || (path[0] == '\0')) {
		__android_log_print(ANDROID_LOG_INFO, "NP2SAF", "cd: ejected");
		notify(EV_CD_SET, 0, 3, NULL);
		return TRUE;
	}
	if (sxsi_devopen(CD_DRV, path) != SUCCESS) {
		__android_log_print(ANDROID_LOG_WARN, "NP2SAF", "cd: %s rejected", path);
		notify(EV_CD_SET, 0, 1, path);
		return FALSE;
	}
	__android_log_print(ANDROID_LOG_INFO, "NP2SAF", "cd: %s inserted", path);
	if (!quiet) {
		notify(EV_CD_SET, 0, 0, path);
	}
	return TRUE;
}

void np2bridge_startupcd(void)
{
	if (s_startupcd[0] != '\0') {
		cdset(s_startupcd, TRUE);	/* 失敗だけ知らせる */
		s_startupcd[0] = '\0';
	}
}

void np2bridge_onexit(void)
{
	if (taskmng_userexit) {
		notify(EV_USER_EXIT, 0, 0, NULL);
	}
}

BOOL np2bridge_handle(const SDL_Event *e)
{
	const SDL_UserEvent *u;
	char *path;

	if (e->type != SDL_USEREVENT) {
		return FALSE;
	}
	u = &e->user;
	switch (u->code) {
		case NP2CMD_KEYDOWN:
#if !defined(NDEBUG)
			__android_log_print(ANDROID_LOG_DEBUG, "np2key", "down %02x", (int)(intptr_t)u->data1);
#endif
			keystat_keydown((REG8)(intptr_t)u->data1);
			break;

		case NP2CMD_KEYUP:
#if !defined(NDEBUG)
			__android_log_print(ANDROID_LOG_DEBUG, "np2key", "up   %02x", (int)(intptr_t)u->data1);
#endif
			keystat_keyup((REG8)(intptr_t)u->data1);
			break;

		case NP2CMD_KEYALLRELEASE:
			keystat_allrelease();
			mousemng_releaseall();
			break;

		case NP2CMD_MOUSEMOVE:
			mousemng_move((float)(intptr_t)u->data1, (float)(intptr_t)u->data2);
			break;

		case NP2CMD_MOUSEBUTTON:
			mousemng_button((int)(intptr_t)u->data1, (intptr_t)u->data2 != 0);
			break;

		case NP2CMD_SETFDD:
			path = (char *)u->data2;
			diskdrv_setfdd((REG8)(intptr_t)u->data1, path, 0);
			SDL_free(path);
			break;

		case NP2CMD_SETHDD:
			path = (char *)u->data2;
			diskdrv_setsxsi((REG8)(intptr_t)u->data1, path);
			SDL_free(path);
			break;

		case NP2CMD_SETCD:
			path = (char *)u->data2;
			cdset(path, FALSE);
			SDL_free(path);
			break;

		case NP2CMD_MENU:
			if (menuvram == NULL) {
				mousemng_releaseall();
				sysmenu_menuopen(0, 0, 0);
			}
			else {
				menubase_close();
			}
			break;

		case NP2CMD_RESET:
			pccore_cfgupdate();
			pccore_reset();
			break;

		case NP2CMD_STATESAVE:
			statesave((int)(intptr_t)u->data1);
			break;

		case NP2CMD_STATELOAD:
			stateload((int)(intptr_t)u->data1, (intptr_t)u->data2 != 0);
			break;

		case NP2CMD_EXIT:
			taskmng_exit();
			break;

		case NP2CMD_REDRAW:
			taskmng_requestredraw();
			break;

		case NP2CMD_SETHOSTDRV:
			np2bridge_hostdrv();
			break;

		default:
			return FALSE;
	}
	return TRUE;
}
