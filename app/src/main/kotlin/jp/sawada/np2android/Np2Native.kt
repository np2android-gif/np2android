package jp.sawada.np2android

import android.os.Handler
import android.os.Looper

/**
 * エミュレータ (libmain.so) への入口。
 *
 * 操作系はどのスレッドから呼んでもよい。ネイティブ側はコマンドを SDL のイベントキューに
 * 積むだけで、実際の処理はエミュレータスレッドで行われる (旧版のスレッド競合対策)。
 * libmain.so は [Np2Activity] (SDLActivity) がロードするので、それ以降に呼ぶこと。
 */
object Np2Native {

    // ---- キーボード (code = PC-98 キーコード 0x00-0x7F) ----
    @JvmStatic external fun keyDown(code: Int)
    @JvmStatic external fun keyUp(code: Int)
    @JvmStatic external fun releaseAllKeys()

    // ---- マウス ----
    const val MOUSE_LEFT = 0
    const val MOUSE_RIGHT = 1

    @JvmStatic external fun mouseButton(button: Int, down: Boolean)
    /** 相対移動 (PC-98 画素単位) */
    @JvmStatic external fun mouseMove(dx: Int, dy: Int)
    /** タッチパッド操作の移動倍率 (1.0 = 指の移動量どおり) */
    @JvmStatic external fun setMouseSpeed(speed: Float)

    // ---- ディスク (path = null で取り出し) ----
    @JvmStatic external fun setFdd(drive: Int, path: String?)
    /** drive: 0x00/0x01 = SASI・IDE, 0x20-0x23 = SCSI。変更はリセット後に有効 (設定は np2.cfg に保存される) */
    @JvmStatic external fun setHdd(drive: Int, path: String?)
    /** CD-ROM (IDE セカンダリ・マスター) の入れ替え。すぐに反映され、結果は [Listener.onCdResult] で通知される */
    @JvmStatic external fun setCd(path: String?)
    /**
     * 起動時に入れておく CD-ROM のパス (CD のパスは np2.cfg に保存されないため)。
     * エミュレータスレッドが動き出す前 (onCreate) に呼ぶこと。失敗したときだけ [Listener.onCdResult] が来る
     */
    @JvmStatic external fun setStartupCd(path: String?)
    /** sxsi のドライブ番号。[getHdd] に渡すと、入っている CD-ROM のパスが返る */
    const val CD_DRIVE = 0x02
    /** 現在のディスクのパス (表示用。未挿入なら null) */
    @JvmStatic external fun getFdd(drive: Int): String?
    @JvmStatic external fun getHdd(drive: Int): String?
    /**
     * SAF で開いた fd を仮想パス "saf:/<id>/<name>" として登録する。fd はネイティブ側で dup するので、
     * 呼び出し後に ParcelFileDescriptor を閉じてよい。戻り値を [setFdd] / [setHdd] に渡す。
     * id は履歴が持つ固定の ID (英数字・'-'・'_')。シークできない fd などで失敗したら null。
     */
    @JvmStatic external fun safRegister(id: String, name: String, fd: Int, readOnly: Boolean): String?
    /**
     * [safRegister] で登録した [id] に、.cue が名前で開く .bin を読み取り専用で付ける。
     * その ID の仮想パスのうち本体 (.cue) 以外の名前を開くと、この fd が開く。
     * [safRegister] で登録し直すと外れるので、そのたびに付け直す。失敗したら false
     */
    @JvmStatic external fun safRegisterSub(id: String, fd: Int): Boolean
    /** 登録を解除する。使用中のドライブがあると、その後の読み書きは失敗する */
    @JvmStatic external fun safUnregister(id: String)
    /**
     * 空のディスクイメージを作る (np2 の newdisk.c)。時間がかかるのでワーカースレッドから呼ぶ。
     * [type] は NEWDISK_*、[param] は FDD なら D88 の fd_type、hdi なら SASI の型番、nhd・hdd なら MB。
     * 作れたら true。
     */
    @JvmStatic external fun createDisk(path: String, type: Int, param: Int): Boolean
    const val NEWDISK_FDD = 0
    const val NEWDISK_HDI = 1
    const val NEWDISK_NHD = 2
    const val NEWDISK_VHD = 3

    /** 使用中 (またはリセット後に使う) ディスクイメージのパス。要素は null のことがある */
    @JvmStatic external fun getDisksInUse(): Array<String?>?

    /**
     * libmain.so がロード済みか。ドキュメントプロバイダーは Activity なしでプロセスが起動することがあり、
     * そのときに external 関数を呼ぶと落ちるため、これを見てから呼ぶ。
     */
    @Volatile var loaded = false

    // ---- 本体操作 ----
    @JvmStatic external fun toggleMenu()
    /** 画面全体を描き直させる (描画面が作り直されたとき用) */
    @JvmStatic external fun requestRedraw()
    @JvmStatic external fun isMenuOpen(): Boolean
    @JvmStatic external fun reset()
    /** 結果は [Listener.onStateSaved] で通知される */
    @JvmStatic external fun saveState(slot: Int)
    /**
     * 結果は [Listener.onStateLoaded] で通知される。
     * セーブ時とディスクが違う場合は読み込まずに [Listener.onStateConflict] が来るので、
     * ユーザーが了承したら force = true で呼び直す。
     */
    @JvmStatic external fun loadState(slot: Int, force: Boolean)
    /** エミュレータを終了する (終了後 SDLActivity が Activity を閉じる) */
    @JvmStatic external fun exit()

    // ---- ネイティブからの通知 ----

    const val LOAD_OK = 0
    const val LOAD_INCOMPATIBLE = 1
    const val LOAD_NOT_FOUND = 2

    interface Listener {
        fun onStateSaved(slot: Int, success: Boolean)
        fun onStateLoaded(slot: Int, result: Int)
        fun onStateConflict(slot: Int, info: String)
        /** SAF で開いたイメージ ([path] は仮想パス) への書き込みが失敗した */
        fun onDiskError(path: String)
        /** FDD の挿入結果。[result] は FDD_MOUNTED / FDD_MOUNTED_PROTECTED / FDD_REJECTED */
        fun onFddResult(drive: Int, result: Int, path: String)
        /** リセット時に、設定されている HDD ([drive] は sxsi のドライブ番号) を開けなかった */
        fun onHddFailed(drive: Int, path: String)
        /** CD-ROM の入れ替え結果。[result] は CD_INSERTED / CD_REJECTED / CD_NO_DRIVE / CD_EJECTED */
        fun onCdResult(result: Int, path: String)
    }

    const val CD_INSERTED = 0
    const val CD_REJECTED = 1
    const val CD_NO_DRIVE = 2
    const val CD_EJECTED = 3

    const val FDD_MOUNTED = 0
    const val FDD_MOUNTED_PROTECTED = 1
    const val FDD_REJECTED = 2

    /** UI スレッドで呼ばれる */
    @Volatile
    var listener: Listener? = null

    /** メニューの「終了」でエミュレータが止まった (Activity 破棄による停止ではない) */
    @Volatile
    var userExited = false
        private set

    private val mainHandler = Handler(Looper.getMainLooper())

    /** エミュレータスレッドから JNI で呼ばれる。UI スレッドへ転送する */
    @JvmStatic
    fun onNativeEvent(type: Int, a: Int, b: Int, text: String?) {
        if (type == 4) {
            // SDLActivity の終了処理より前に確実に反映されるよう、post せずにここで立てる
            userExited = true
            return
        }
        mainHandler.post {
            val l = listener ?: return@post
            when (type) {
                1 -> l.onStateSaved(a, b == 0)
                2 -> l.onStateLoaded(a, b)
                3 -> l.onStateConflict(a, text ?: "")
                5 -> l.onDiskError(text ?: "")
                6 -> l.onFddResult(a, b, text ?: "")
                7 -> l.onHddFailed(a, text ?: "")
                8 -> l.onCdResult(b, text ?: "")
            }
        }
    }
}
