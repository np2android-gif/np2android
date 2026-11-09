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

#include "fdd/fddfile.h"
#include "fdd/sxsi.h"

#define LOG_TAG "np2"

float np2bridge_mousespeed = 1.0f;

/* ---- Kotlin へのコールバック (エミュレータスレッドから呼ぶ) ---------------- */

/* Np2Native.onNativeEvent(type, a, b, text) の type */
enum {
	EV_STATE_SAVED		= 1,	/* a = slot, b = 0:成功 / 1:失敗 */
	EV_STATE_LOADED		= 2,	/* a = slot, b = 0:成功 / 1:失敗(非互換) / 2:ファイル無し */
	EV_STATE_CONFLICT	= 3,	/* a = slot, text = 差分情報 (force 付きで再ロードするか確認) */
	EV_USER_EXIT		= 4		/* メニューから終了した (Activity 終了後にプロセスも終わらせてよい) */
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
		if ((code == NP2CMD_SETFDD) || (code == NP2CMD_SETHDD)) {
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

/* 登録を解除する。使用中のドライブがあれば、その後の読み書きは失敗する */
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
	static const REG8 hdds[] = {0x00, 0x01, 0x20, 0x21, 0x22, 0x23};
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
	info[0] = '\0';
	ret = statsave_check(path, info, sizeof(info));
	if (ret & (~STATFLAG_DISKCHG)) {
		__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "state load slot %d: incompatible (%d)", slot, ret);
		notify(EV_STATE_LOADED, slot, 1, NULL);
		return;
	}
	if ((ret & STATFLAG_DISKCHG) && (!force)) {
		/* ディスクがセーブ時と違う: Kotlin 側で確認して force=true で再要求してもらう */
		notify(EV_STATE_CONFLICT, slot, 0, info);
		return;
	}
	statsave_load(path);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "state load slot %d: ok", slot);
	notify(EV_STATE_LOADED, slot, 0, NULL);
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

		default:
			return FALSE;
	}
	return TRUE;
}
