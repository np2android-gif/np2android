/* ANDROID-SAF: FDD イメージの形式を中身 (ヘッダー) から判定する */

#ifndef _FDDSNIFF_ANDROID_H_
#define _FDDSNIFF_ANDROID_H_

#ifdef __cplusplus
extern "C" {
#endif

/* NFD・VFDD・D88 のどれかなら FTYPE_*、どれでもなければ FTYPE_NONE を返す */
UINT fdd_sniff_android(const OEMCHAR *fname);

#ifdef __cplusplus
}
#endif

#endif	/* _FDDSNIFF_ANDROID_H_ */
