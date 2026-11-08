package jp.sawada.np2android.keyboard

import android.content.Context
import android.util.SparseIntArray

/**
 * 旧版の keymap ファイル (.key) によるキー割り当て。
 *
 * 書式 (旧版 ScanCode.java と同じ): 1 行に「scancode,np2 コード」。
 *   scancode  : 物理キーの scancode (Linux の KEY_* 番号)
 *   np2 コード : 0 = 無効 (押しても何もしない)、1〜116 = PC-98 キーコード、117 = ESC、
 *               118/119 = 左/右クリック、120 = メニュー、121/122 = ステートセーブ/ロード、
 *               123/124 = 音量 下げる/上げる
 * 空行・数値でない行・'#' で始まる行は無視する (旧版で落ちる原因だった行も読み飛ばす)。
 *
 * ここで指定した scancode は、KeyCode による標準の割り当てより優先される。
 */
object CustomKeymap {

    private const val PREFS = "ui"
    private const val KEY = "custom_keymap"
    private const val KEY_NAME = "custom_keymap_name"

    /** 解析結果。[entries] は scancode → np2 コード */
    data class Parsed(val entries: SparseIntArray, val skipped: Int)

    fun parse(text: String): Parsed {
        val map = SparseIntArray()
        var skipped = 0
        for (raw in text.lineSequence()) {
            val line = raw.substringBefore('#').trim()
            if (line.isEmpty()) continue
            val parts = line.split(',', limit = 3)
            val scan = parts.getOrNull(0)?.trim()?.toIntOrNull()
            val code = parts.getOrNull(1)?.trim()?.toIntOrNull()
            if (scan == null || code == null || scan < 0 || code !in 0..124) {
                skipped++
                continue
            }
            map.put(scan, code)
        }
        return Parsed(map, skipped)
    }

    fun save(context: Context, name: String, entries: SparseIntArray) {
        val s = (0 until entries.size()).joinToString(";") { "${entries.keyAt(it)}:${entries.valueAt(it)}" }
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString(KEY, s).putString(KEY_NAME, name).apply()
    }

    fun clear(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .remove(KEY).remove(KEY_NAME).apply()
    }

    fun load(context: Context): SparseIntArray {
        val map = SparseIntArray()
        val s = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null) ?: return map
        for (item in s.split(';')) {
            val k = item.substringBefore(':').toIntOrNull() ?: continue
            val v = item.substringAfter(':').toIntOrNull() ?: continue
            map.put(k, v)
        }
        return map
    }

    /** 読み込み済みファイルの名前 (未設定なら null) */
    fun name(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_NAME, null)
}
