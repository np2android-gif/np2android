/**
 * @file	safesave.h
 * @brief	ファイルを壊さずに書き換えるための補助 (Android 用)
 *
 * 保存先に直接書くと、書き込み中にプロセスが止められた (Android では普通に起こる) とき
 * 前の正常なファイルまで失われる。path.tmp に書き、すべて成功したときだけ rename で置き換える。
 * 同じディレクトリ内の rename は置き換えが一瞬で行われるので、どの時点で止まっても
 * 古いファイルか新しいファイルのどちらかが完全な形で残る。
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** path に対応する一時ファイル名 (path + ".tmp") を tmp に作る */
void safesave_tmpname(char *tmp, int size, const char *path);

/**
 * 一時ファイルへの書き込みを確定する。
 * ok なら tmp を path に置き換え、失敗なら tmp を消して path はそのまま残す。
 * @return 置き換えたら SUCCESS
 */
BRESULT safesave_commit(const char *tmp, const char *path, BOOL ok);

/** statsave_save() の安全版。戻り値は statsave_save() と同じ (0 = 成功) */
int safesave_statsave(const char *path);

#ifdef __cplusplus
}
#endif
