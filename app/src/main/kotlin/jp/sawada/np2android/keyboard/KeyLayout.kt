package jp.sawada.np2android.keyboard

import android.content.res.Resources
import android.content.res.XmlResourceParser
import org.xmlpull.v1.XmlPullParser

/**
 * ソフトキーボードのレイアウト (res/xml/kbd_*.xml)。
 *
 * 書式:
 * ```
 * <keyboard keyWidth="7.5">            既定のキー幅 (キーボード幅に対する %)
 *   <row height="1">                   行の高さの比率 (省略時 1)
 *     <key code="16" label="Q" width="7.5" gap="0"/>
 *   </row>
 * </keyboard>
 * ```
 * code の意味は [SoftKeyCodes] を参照。
 */
data class KeyLayout(val rows: List<Row>) {

    data class Row(val weight: Float, val keys: List<Key>)

    /** x / width は % (0〜100) */
    data class Key(val code: Int, val label: String, val x: Float, val width: Float)

    companion object {
        fun load(res: Resources, xmlId: Int): KeyLayout {
            val rows = mutableListOf<Row>()
            res.getXml(xmlId).use { p -> parse(p, rows) }
            return KeyLayout(rows)
        }

        private fun parse(p: XmlResourceParser, rows: MutableList<Row>) {
            var defWidth = 10f
            var keys: MutableList<Key>? = null
            var weight = 1f
            var x = 0f
            while (true) {
                when (p.next()) {
                    XmlPullParser.END_DOCUMENT -> return
                    XmlPullParser.START_TAG -> when (p.name) {
                        "keyboard" -> defWidth = p.float("keyWidth") ?: defWidth
                        "row" -> {
                            keys = mutableListOf()
                            weight = p.float("height") ?: 1f
                            x = 0f
                        }
                        "key" -> {
                            val code = p.getAttributeValue(null, "code")?.toIntOrNull() ?: continue
                            val label = p.getAttributeValue(null, "label") ?: ""
                            val w = p.float("width") ?: defWidth
                            x += p.float("gap") ?: 0f
                            keys?.add(Key(code, label, x, w))
                            x += w
                        }
                    }
                    XmlPullParser.END_TAG -> if (p.name == "row") {
                        rows.add(Row(weight, keys ?: emptyList()))
                        keys = null
                    }
                }
            }
        }

        private fun XmlResourceParser.float(name: String): Float? =
            getAttributeValue(null, name)?.toFloatOrNull()
    }
}

/** レイアウト XML の code の値 (旧版のソフトキー定義と互換) */
object SoftKeyCodes {
    /** 0〜116 は PC-98 キーコードそのもの。ただし 0 (ESC) は「未定義」と区別するため 117 で表す */
    const val ESC = 117
    const val MOUSE_LEFT = 118
    const val MOUSE_RIGHT = 119
    const val MENU = 120
    const val STATE_SAVE = 121
    const val STATE_LOAD = 122
    const val PAGE = 123
    const val TOGGLE_KEYBOARD = 124
    const val TOGGLE_SIDE = 125

    /** PC-98 キーコードに変換する。PC-98 のキーでなければ -1 */
    fun toPc98(code: Int): Int = when (code) {
        ESC -> 0x00
        in 1..0x74 -> code
        else -> -1
    }
}
