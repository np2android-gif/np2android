/**
 * @file	np2bridge.h
 * @brief	Kotlin (UI スレッド) → エミュレータスレッドのコマンド橋渡し
 *
 * JNI 関数は SDL_PushEvent で SDL_USEREVENT を積むだけで、エミュレータの状態には
 * 一切触れない。実際の処理は taskmng_rol() から np2bridge_handle() で行う。
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
	NP2CMD_KEYDOWN = 1,		/* data1 = PC-98 キーコード (0x00-0x7f) */
	NP2CMD_KEYUP,
	NP2CMD_KEYALLRELEASE,
	NP2CMD_MOUSEMOVE,		/* data1/data2 = (intptr_t) dx, dy  PC-98 画素単位 */
	NP2CMD_MOUSEBUTTON,		/* data1 = button, data2 = down(0/1) */
	NP2CMD_SETFDD,			/* data1 = drive, data2 = char* path (NULL で取り出し) */
	NP2CMD_SETHDD,			/* data1 = sxsi drive, data2 = char* path (NULL で取り出し) */
	NP2CMD_MENU,			/* np2 内蔵メニューの開閉 */
	NP2CMD_RESET,
	NP2CMD_STATESAVE,		/* data1 = slot */
	NP2CMD_STATELOAD,		/* data1 = slot */
	NP2CMD_EXIT,
	NP2CMD_REDRAW,			/* 描画面が作り直されたので全体を描き直す */
	NP2CMD_SETCD			/* data2 = char* path (NULL で取り出し)。CD-ROM (IDE セカンダリ・マスター) */
};

/** 一定時間、画面全体を描き直し続ける (port/taskmng.c) */
void taskmng_requestredraw(void);

/** taskmng_rol() から呼ぶ。処理したら TRUE */
BOOL np2bridge_handle(const SDL_Event *e);

/** np2_main 終了後に呼ぶ。ユーザー操作による終了なら Kotlin に通知する */
void np2bridge_onexit(void);

/** SAF で開いたイメージへの書き込みが失敗したことを Kotlin に通知する (エミュレータスレッドから呼ぶ) */
void np2bridge_diskerror(const char *path);

/**
 * FDD の挿入 (diskdrv_readyfddex) の結果を Kotlin に通知する (エミュレータスレッドから呼ぶ)。
 * 形式やサイズが合わないイメージは np2 がマウントを拒否するので、失敗を表示できるようにする。
 */
void np2bridge_fddresult(REG8 drv, const char *path, BOOL success);

/** リセット時に、設定されているのに開けなかった HDD を Kotlin に通知する (エミュレータスレッドから呼ぶ) */
void np2bridge_hddfailed(REG8 drv, const char *path);

/**
 * 起動時 (pccore_reset の後、レジュームの前) に呼ぶ。Kotlin が前回の CD-ROM のパスを
 * 渡していれば挿入する (CD のパスは np2.cfg に保存されないため)。エミュレータスレッドから呼ぶ
 */
void np2bridge_startupcd(void);

/** taskmng_exit() (メニューの終了) で止めたとき TRUE */
extern BOOL taskmng_userexit;

/** タッチ操作の移動倍率 (Kotlin から設定) */
extern float np2bridge_mousespeed;

#ifdef __cplusplus
}
#endif
