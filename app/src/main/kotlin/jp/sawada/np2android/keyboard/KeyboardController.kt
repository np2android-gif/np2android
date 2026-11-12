package jp.sawada.np2android.keyboard

import android.os.Handler
import android.os.Looper
import jp.sawada.np2android.Np2Menu
import jp.sawada.np2android.Np2Native

/**
 * ソフトキー (複数の [SoftKeyboardView]) の押下状態をまとめて管理する。
 *
 * - 同じコードが複数の指・複数のパネルで押されても、エミュレータへは 1 回の押下/解放だけ送る
 * - 0x70 未満のキーは押し続けるとリピートする (PC-98 のキーボードと同様)
 * - CAPS / カナ はロック式キーなので、表示用にロック状態を保持する
 *
 * UI スレッドからのみ呼ぶこと。
 */
class KeyboardController(private val actions: Actions) {

    /** エミュレータ以外の操作 (表示切替など) は Activity に任せる */
    interface Actions {
        fun openMenu()
        fun toggleKeyboard()
        fun toggleSide()
        fun onLockStateChanged()
    }

    private val handler = Handler(Looper.getMainLooper())
    private val pressCount = HashMap<Int, Int>()
    private val locked = HashSet<Int>()
    private var repeatPc98 = -1

    private val repeatTask = object : Runnable {
        override fun run() {
            if (repeatPc98 >= 0) {
                Np2Native.keyDown(repeatPc98)
                handler.postDelayed(this, REPEAT_INTERVAL_MS)
            }
        }
    }

    fun isPressed(code: Int) = (pressCount[code] ?: 0) > 0

    fun isLocked(code: Int) = SoftKeyCodes.toPc98(code) in locked

    fun press(code: Int) {
        val n = (pressCount[code] ?: 0) + 1
        pressCount[code] = n
        if (n > 1) return

        val pc98 = SoftKeyCodes.toPc98(code)
        when {
            pc98 >= 0 -> {
                Np2Native.keyDown(pc98)
                if (pc98 == PC98_CAPS || pc98 == PC98_KANA) {
                    if (!locked.remove(pc98)) locked.add(pc98)
                    actions.onLockStateChanged()
                }
                if (pc98 < 0x70) startRepeat(pc98)
            }
            code == SoftKeyCodes.MOUSE_LEFT -> Np2Native.mouseButton(Np2Native.MOUSE_LEFT, true)
            code == SoftKeyCodes.MOUSE_RIGHT -> Np2Native.mouseButton(Np2Native.MOUSE_RIGHT, true)
        }
    }

    fun release(code: Int) {
        val n = (pressCount[code] ?: 0) - 1
        if (n > 0) {
            pressCount[code] = n
            return
        }
        pressCount.remove(code)

        val pc98 = SoftKeyCodes.toPc98(code)
        when {
            pc98 >= 0 -> {
                if (pc98 == repeatPc98) stopRepeat()
                Np2Native.keyUp(pc98)
            }
            code == SoftKeyCodes.MOUSE_LEFT -> Np2Native.mouseButton(Np2Native.MOUSE_LEFT, false)
            code == SoftKeyCodes.MOUSE_RIGHT -> Np2Native.mouseButton(Np2Native.MOUSE_RIGHT, false)
            // 以下は離したときに実行 (押した指をずらしてキャンセルする余地を残す)
            code == SoftKeyCodes.MENU -> actions.openMenu()
            code == SoftKeyCodes.STATE_SAVE -> Np2Native.saveState(Np2Menu.QUICK_SLOT)
            // ディスクがセーブ時と違うときは Np2Menu のリスナーが確認ダイアログを出す
            code == SoftKeyCodes.STATE_LOAD -> Np2Native.loadState(Np2Menu.QUICK_SLOT, false)
            code == SoftKeyCodes.TOGGLE_KEYBOARD -> actions.toggleKeyboard()
            code == SoftKeyCodes.TOGGLE_SIDE -> actions.toggleSide()
        }
    }

    /** 画面から指が離れた扱いにする (onPause・パネル非表示時など) */
    fun releaseAll() {
        stopRepeat()
        for (code in pressCount.keys.toList()) {
            val pc98 = SoftKeyCodes.toPc98(code)
            when {
                pc98 >= 0 -> Np2Native.keyUp(pc98)
                code == SoftKeyCodes.MOUSE_LEFT -> Np2Native.mouseButton(Np2Native.MOUSE_LEFT, false)
                code == SoftKeyCodes.MOUSE_RIGHT -> Np2Native.mouseButton(Np2Native.MOUSE_RIGHT, false)
            }
        }
        pressCount.clear()
    }

    private fun startRepeat(pc98: Int) {
        stopRepeat()
        repeatPc98 = pc98
        handler.postDelayed(repeatTask, REPEAT_DELAY_MS)
    }

    private fun stopRepeat() {
        repeatPc98 = -1
        handler.removeCallbacks(repeatTask)
    }

    companion object {
        const val PC98_CAPS = 0x71
        const val PC98_KANA = 0x72
        private const val REPEAT_DELAY_MS = 500L
        private const val REPEAT_INTERVAL_MS = 60L
    }
}
