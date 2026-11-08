package jp.sawada.np2android.keyboard

import android.util.SparseIntArray
import android.view.KeyEvent

/**
 * 物理キー → PC-98 キーコード。
 *
 * Android の KeyCode は各端末のキー配置定義 (.kl) で正規化された「キーの位置」を表すため、
 * 端末ごとに値が違う scancode より適している。JIS キーボードの刻印とは名前が異なるが
 * (例: JIS の「@」キーは KEYCODE_LEFT_BRACKET)、位置で対応させるので PC-98 の配列どおりになる。
 * KeyCode が UNKNOWN のときだけ、旧版と同じ scancode (Linux KEY_*) の表で補う。
 */
object KeyMapper {

    // ---- PC-98 キー以外の動作 (map() の戻り値。PC-98 キーコードは 0 以上) ----
    const val NONE = -1
    const val ACTION_MENU = -2
    const val ACTION_MOUSE_LEFT = -3
    const val ACTION_MOUSE_RIGHT = -4
    const val ACTION_STATE_SAVE = -5
    const val ACTION_STATE_LOAD = -6
    const val ACTION_VOLUME_DOWN = -7
    const val ACTION_VOLUME_UP = -8
    /** keymap ファイルで 0 (無効) を指定されたキー。押しても何もしない */
    const val ACTION_IGNORE = -9

    /** keymap ファイル (.key) による割り当て。scancode → np2 コード ([CustomKeymap] 参照) */
    @Volatile
    var custom = SparseIntArray()

    /** keymap ファイルの np2 コード → map() の戻り値 */
    private fun fromNp2Code(code: Int): Int = when (code) {
        0 -> ACTION_IGNORE
        in 1..0x74 -> code
        SoftKeyCodes.ESC -> 0x00
        SoftKeyCodes.MOUSE_LEFT -> ACTION_MOUSE_LEFT
        SoftKeyCodes.MOUSE_RIGHT -> ACTION_MOUSE_RIGHT
        SoftKeyCodes.MENU -> ACTION_MENU
        SoftKeyCodes.STATE_SAVE -> ACTION_STATE_SAVE
        SoftKeyCodes.STATE_LOAD -> ACTION_STATE_LOAD
        123 -> ACTION_VOLUME_DOWN
        124 -> ACTION_VOLUME_UP
        else -> ACTION_IGNORE
    }

    /** keymap ファイルで割り当てられたキーなら、その動作。なければ [NONE] */
    fun mapCustom(event: KeyEvent): Int {
        val sc = event.scanCode
        if (sc <= 0) return NONE           // ナビゲーションバー等の仮想キーは scancode 0
        val code = custom.get(sc, -1)
        return if (code < 0) NONE else fromNp2Code(code)
    }

    private val byKeyCode = SparseIntArray().apply {
        // 英字
        val letters = intArrayOf(
            0x1D, 0x2D, 0x2B, 0x1F, 0x12, 0x20, 0x21, 0x22, 0x17, 0x23, 0x24, 0x25, 0x2F, // A-M
            0x2E, 0x18, 0x19, 0x10, 0x13, 0x1E, 0x14, 0x16, 0x2C, 0x11, 0x2A, 0x15, 0x29, // N-Z
        )
        for (i in letters.indices) put(KeyEvent.KEYCODE_A + i, letters[i])
        // 数字 (1-9, 0)
        for (i in 1..9) put(KeyEvent.KEYCODE_0 + i, i)
        put(KeyEvent.KEYCODE_0, 0x0A)

        // 記号 (位置で対応)
        put(KeyEvent.KEYCODE_MINUS, 0x0B)          // -
        put(KeyEvent.KEYCODE_EQUALS, 0x0C)         // ^ (JIS)
        put(KeyEvent.KEYCODE_YEN, 0x0D)            // ¥
        put(KeyEvent.KEYCODE_LEFT_BRACKET, 0x1A)   // @ (JIS)
        put(KeyEvent.KEYCODE_RIGHT_BRACKET, 0x1B)  // [ (JIS)
        put(KeyEvent.KEYCODE_SEMICOLON, 0x26)      // ;
        put(KeyEvent.KEYCODE_APOSTROPHE, 0x27)     // : (JIS)
        put(KeyEvent.KEYCODE_BACKSLASH, 0x28)      // ] (JIS)
        put(KeyEvent.KEYCODE_COMMA, 0x30)
        put(KeyEvent.KEYCODE_PERIOD, 0x31)
        put(KeyEvent.KEYCODE_SLASH, 0x32)
        put(KeyEvent.KEYCODE_RO, 0x33)             // _ (ろ)
        put(KeyEvent.KEYCODE_SPACE, 0x34)

        // 編集・制御
        put(KeyEvent.KEYCODE_ESCAPE, 0x00)
        put(KeyEvent.KEYCODE_DEL, 0x0E)            // Backspace
        put(KeyEvent.KEYCODE_TAB, 0x0F)
        put(KeyEvent.KEYCODE_ENTER, 0x1C)
        put(KeyEvent.KEYCODE_NUMPAD_ENTER, 0x1C)
        put(KeyEvent.KEYCODE_HENKAN, 0x35)         // XFER
        put(KeyEvent.KEYCODE_MUHENKAN, 0x51)       // NFER
        put(KeyEvent.KEYCODE_PAGE_UP, 0x36)        // ROLL UP
        put(KeyEvent.KEYCODE_PAGE_DOWN, 0x37)      // ROLL DOWN
        put(KeyEvent.KEYCODE_INSERT, 0x38)
        put(KeyEvent.KEYCODE_FORWARD_DEL, 0x39)
        put(KeyEvent.KEYCODE_DPAD_UP, 0x3A)
        put(KeyEvent.KEYCODE_DPAD_LEFT, 0x3B)
        put(KeyEvent.KEYCODE_DPAD_RIGHT, 0x3C)
        put(KeyEvent.KEYCODE_DPAD_DOWN, 0x3D)
        put(KeyEvent.KEYCODE_MOVE_HOME, 0x3E)      // HOME/CLR
        put(KeyEvent.KEYCODE_MOVE_END, 0x3F)       // HELP
        put(KeyEvent.KEYCODE_NUM_LOCK, 0x3E)       // 旧版: Num Lock → HOME/CLR
        put(KeyEvent.KEYCODE_SCROLL_LOCK, 0x3F)    // 旧版: Scroll Lock → HELP
        put(KeyEvent.KEYCODE_BREAK, 0x60)          // Pause → STOP
        put(KeyEvent.KEYCODE_SYSRQ, 0x61)          // PrintScreen → COPY

        // ファンクション
        for (i in 0..9) put(KeyEvent.KEYCODE_F1 + i, 0x62 + i)
        put(KeyEvent.KEYCODE_F11, 0x4D)            // 旧版: F11 → テンキー =
        put(KeyEvent.KEYCODE_F12, 0x4F)            // F12 → テンキー ,

        // テンキー
        put(KeyEvent.KEYCODE_NUMPAD_SUBTRACT, 0x40)
        put(KeyEvent.KEYCODE_NUMPAD_DIVIDE, 0x41)
        put(KeyEvent.KEYCODE_NUMPAD_7, 0x42)
        put(KeyEvent.KEYCODE_NUMPAD_8, 0x43)
        put(KeyEvent.KEYCODE_NUMPAD_9, 0x44)
        put(KeyEvent.KEYCODE_NUMPAD_MULTIPLY, 0x45)
        put(KeyEvent.KEYCODE_NUMPAD_4, 0x46)
        put(KeyEvent.KEYCODE_NUMPAD_5, 0x47)
        put(KeyEvent.KEYCODE_NUMPAD_6, 0x48)
        put(KeyEvent.KEYCODE_NUMPAD_ADD, 0x49)
        put(KeyEvent.KEYCODE_NUMPAD_1, 0x4A)
        put(KeyEvent.KEYCODE_NUMPAD_2, 0x4B)
        put(KeyEvent.KEYCODE_NUMPAD_3, 0x4C)
        put(KeyEvent.KEYCODE_NUMPAD_EQUALS, 0x4D)
        put(KeyEvent.KEYCODE_NUMPAD_0, 0x4E)
        put(KeyEvent.KEYCODE_NUMPAD_COMMA, 0x4F)
        put(KeyEvent.KEYCODE_NUMPAD_DOT, 0x50)

        // シフト系
        put(KeyEvent.KEYCODE_SHIFT_LEFT, 0x70)
        put(KeyEvent.KEYCODE_SHIFT_RIGHT, 0x70)
        put(KeyEvent.KEYCODE_CAPS_LOCK, 0x71)
        put(KeyEvent.KEYCODE_KATAKANA_HIRAGANA, 0x72)  // カナ
        put(KeyEvent.KEYCODE_KANA, 0x72)
        put(KeyEvent.KEYCODE_ALT_LEFT, 0x73)       // GRPH
        put(KeyEvent.KEYCODE_ALT_RIGHT, 0x73)
        put(KeyEvent.KEYCODE_CTRL_LEFT, 0x74)
        put(KeyEvent.KEYCODE_CTRL_RIGHT, 0x74)

        // np2 メニュー
        put(KeyEvent.KEYCODE_MENU, ACTION_MENU)
    }

    /** 旧版 ScanCode.java の既定表 (Linux KEY_* → PC-98)。KeyCode が UNKNOWN のときのみ使う */
    private val byScanCode = SparseIntArray().apply {
        val t = intArrayOf(
            1, 0x00, 2, 0x01, 3, 0x02, 4, 0x03, 5, 0x04, 6, 0x05, 7, 0x06, 8, 0x07, 9, 0x08, 10, 0x09,
            11, 0x0A, 12, 0x0B, 13, 0x0C, 14, 0x0E, 15, 0x0F, 16, 0x10, 17, 0x11, 18, 0x12, 19, 0x13,
            20, 0x14, 21, 0x15, 22, 0x16, 23, 0x17, 24, 0x18, 25, 0x19, 26, 0x1A, 27, 0x1B, 28, 0x1C,
            29, 0x74, 30, 0x1D, 31, 0x1E, 32, 0x1F, 33, 0x20, 34, 0x21, 35, 0x22, 36, 0x23, 37, 0x24,
            38, 0x25, 39, 0x26, 40, 0x27, 42, 0x70, 43, 0x28, 44, 0x29, 45, 0x2A, 46, 0x2B, 47, 0x2C,
            48, 0x2D, 49, 0x2E, 50, 0x2F, 51, 0x30, 52, 0x31, 53, 0x32, 54, 0x70, 55, 0x45, 56, 0x73,
            57, 0x34, 58, 0x71, 59, 0x62, 60, 0x63, 61, 0x64, 62, 0x65, 63, 0x66, 64, 0x67, 65, 0x68,
            66, 0x69, 67, 0x6A, 68, 0x6B, 69, 0x3E, 70, 0x3F, 71, 0x42, 72, 0x43, 73, 0x44, 74, 0x40,
            75, 0x46, 76, 0x47, 77, 0x48, 78, 0x49, 79, 0x4A, 80, 0x4B, 81, 0x4C, 82, 0x4E, 83, 0x50,
            87, 0x4D, 88, 0x4F, 89, 0x33, 92, 0x35, 93, 0x72, 94, 0x51, 96, 0x1C, 98, 0x41, 99, 0x61,
            102, 0x3E, 103, 0x3A, 104, 0x36, 105, 0x3B, 106, 0x3C, 107, 0x3F, 108, 0x3D, 109, 0x37,
            110, 0x38, 111, 0x39, 119, 0x60, 124, 0x0D,
        )
        for (i in t.indices step 2) put(t[i], t[i + 1])
    }

    /**
     * PC-98 キーコード (0x00-0x74)、ACTION_* のいずれか、対応なしなら [NONE]。
     * keymap ファイルの割り当て → KeyCode の標準表 → (KeyCode 不明時のみ) scancode の標準表 の順に調べる。
     */
    fun map(event: KeyEvent): Int {
        val custom = mapCustom(event)
        if (custom != NONE) return custom
        val code = byKeyCode.get(event.keyCode, NONE)
        if (code != NONE) return code
        if (event.keyCode == KeyEvent.KEYCODE_UNKNOWN) {
            return byScanCode.get(event.scanCode, NONE)
        }
        return NONE
    }
}
