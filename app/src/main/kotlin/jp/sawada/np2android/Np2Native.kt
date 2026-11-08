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
    /** 現在のディスクのパス (表示用。未挿入なら null) */
    @JvmStatic external fun getFdd(drive: Int): String?
    @JvmStatic external fun getHdd(drive: Int): String?

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
    }

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
            }
        }
    }
}
