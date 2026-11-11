/**
 * @file	android_safio.h
 * @brief	SAF (content URI) で開いたファイルディスクリプタを「仮想パス」として np2 コアに見せる
 *
 * Kotlin が ParcelFileDescriptor から得た fd を登録すると、C 側で dup して保持し、
 * 仮想パス "saf:/<id>/<表示名>" を返す。np2 コアはこのパスを普通のファイル名として扱い、
 * dosio.c の file_open 系が仮想パスを見つけたら、保持している fd をさらに dup して fdopen する。
 *
 * - 登録時に dup するので、Kotlin 側は登録直後に pfd.close() してよい。
 * - file_open で返す FILE* はそれぞれ別の dup を持つので、登録を解除しても開いている FILE* は使える。
 * - <id> は Kotlin 側の履歴が持つ固定の ID。レジュームファイルや np2.cfg にパスが残っても、
 *   次回起動時に同じ ID で登録し直せば同じパスで開ける。
 * - 表とその中の fd はミューテックスで保護する (JNI は UI スレッド、file_open はエミュレータスレッド)。
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 仮想パスの接頭辞 */
#define SAFIO_PREFIX		"saf:/"

/** path が仮想パスか (登録されているかどうかは見ない) */
BOOL safio_isvirtual(const char *path);

/**
 * fd を登録する。fd は dup して保持するので、呼び出し側は自分の fd を閉じてよい。
 * 同じ ID がすでにあれば置き換える (古い fd は閉じる)。
 * @param[in] id		固定の ID (英数字、'-'、'_' のみ)
 * @param[in] name		表示名 (拡張子の判定に使う。長すぎる場合は拡張子を残して切り詰める)
 * @param[in] fd		シーク可能な通常ファイルの fd
 * @param[in] readonly	TRUE なら書き込みモードで開かれても読み取り専用で開く
 * @param[out] path		仮想パス
 * @param[in] size		path のバイト数
 * @return SUCCESS / FAILURE (シークできない fd、表が満杯、ID が不正など)
 */
BRESULT safio_register(const char *id, const char *name, int fd, BOOL readonly,
														char *path, int size);

/**
 * 登録済みの ID に、読み取り専用の付属ファイル (.cue が名前で開く .bin) を 1 つ付ける。
 * その ID の仮想パスのうち、本体以外の名前 ("saf:/<id>/<何か>") を開くと付属ファイルが開く。
 * 本体を登録し直すと付属ファイルは外れるので、そのたびに付け直すこと。
 * @return SUCCESS / FAILURE (ID が未登録、シークできない fd など)
 */
BRESULT safio_register_sub(const char *id, int fd);

/** ID の登録を解除する (付属ファイルも)。開いている FILE* はそのまま使える */
void safio_unregister(const char *id);

/**
 * 仮想パスを開く。mode は fopen と同じ。
 * 読み取り専用エントリーを書き込みモードで開こうとした場合は "rb" で開く
 * (書き込みは失敗し、FDD は file_attr の読み取り専用属性で書き込み禁止になる)。
 * @return FILE*。未登録なら NULL
 */
FILE *safio_fopen(const char *path, const char *mode);

/**
 * file_attr 相当。未登録なら -1、それ以外は 0 か FILEATTR_READONLY
 */
short safio_attr(const char *path);

/**
 * dosio.c の書き込み・シーク・クローズが失敗したときに呼ぶ。fp が仮想パスのエントリーなら
 * ログに出し、Kotlin に通知する (エミュレータは止めない)。それ以外のファイルなら何もしない。
 */
void safio_ioerror(FILE *fp);

/** 登録中の全 fd を fsync する (バックグラウンドへ移るとき用) */
void safio_syncall(void);

#ifdef __cplusplus
}
#endif
