package jp.sawada.np2android

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.Typeface
import android.graphics.fonts.FontFamily
import android.graphics.fonts.FontStyle
import android.graphics.fonts.SystemFonts
import android.os.Build
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.charset.Charset
import java.nio.charset.CodingErrorAction
import java.util.Locale

/**
 * ANDROID-FONT (Q15): font.tmp (漢字 ROM の代わり) に使う字を、端末の日本語フォントで描く。
 *
 * font.tmp を作るのはネイティブの makepc98bmp (np2/font/fontmake.c) で、字の形だけを
 * [Np2Native.renderFontGlyphs] → [render] でここからもらう (port/androidfont.c)。PC-98 に無い文字の除外、
 * 78/83 JIS の入れ替え、内蔵の字の上書きは makepc98bmp が行う。
 *
 * a22 までの font.tmp は、漢字・かな・半角カナが「?」になっている。起動のたびに [prepare] で調べ、
 * そうなっていれば消して、エミュレータが起動するときに作り直させる。
 */
object FontGlyphs {

    /** font.tmp を作る仕組みの版。描き方を変えて作り直させたいときに上げる ([Settings.fontTmpVersion]) */
    const val VERSION = 1

    private const val TAG = "np2font"
    private const val ANK_SIZE = 256 * 16
    private const val KNJ_SIZE = 94 * 94 * 32

    /**
     * 白黒の描き方。アンチエイリアスで描いてしきい値で切ると、16 ドットでは細い線 (矢印の縦線、①の円など) が
     * 消えるか、太りすぎる。アンチエイリアスなしで描くと、どちらも起きなかった (Q15 指示書 5 章の比べた画像)
     */
    private const val ANTI_ALIAS = false
    /** アンチエイリアスで描くときのしきい値 (濃さ 0〜255 がこれ以上なら字) */
    private const val THRESHOLD = 128

    @Volatile
    private var settings: Settings? = null

    /**
     * 描いている間の表示 (true で出し、false で消す)。エミュレータのスレッドから呼ぶので、
     * 受け取る側で UI スレッドに回すこと。描くのは数秒かかる (XZ3 で約 3 秒。多くはフォントの一覧を取る時間)
     */
    @Volatile
    var progress: ((Boolean) -> Unit)? = null

    /**
     * 起動時 (エミュレータのスレッドが動き出す前) に呼ぶ。版の記録が古く、font.tmp が a22 までに作った
     * 「?」のものなら消す。外から写した font.tmp や、利用者が置いた FONT.BMP・FONT.ROM は消さない。
     * 日本語フォントが無い端末では、作り直しても同じなので消さない
     */
    fun prepare(workDir: File, settings: Settings) {
        this.settings = settings
        if (settings.fontTmpVersion >= VERSION) return
        val file = File(workDir, "font.tmp")
        if (!file.isFile) return
        val old = runCatching { isAnkOnly(file) }.getOrElse {
            Log.w(TAG, "font.tmp: cannot read", it)
            false
        }
        if (!old || !hasJapaneseFont()) return
        if (file.delete()) {
            Log.i(TAG, "font.tmp: deleted the old one (kanji were '?'); it will be made again from the device font")
        } else {
            Log.w(TAG, "font.tmp: could not delete the old one")
        }
    }

    /**
     * a22 までの font.tmp (英数字の fontmng だけで作ったもの) か。
     * 「亜」(JIS 3021) が全角の「？」(JIS 2129) と同じ形で、空白でなければそうとみなす
     */
    private fun isAnkOnly(file: File): Boolean {
        RandomAccessFile(file, "r").use { f ->
            if (f.length() < 62) return false
            val head = ByteArray(30)
            f.readFully(head)
            val bb = ByteBuffer.wrap(head).order(java.nio.ByteOrder.LITTLE_ENDIAN)
            val offset = bb.getInt(10)
            if (bb.getInt(18) != 2048 || bb.getInt(22) != 2048 || bb.getShort(28).toInt() != 1) return false
            if (f.length() < offset + 2048L * 256) return false
            fun cell(jis: Int): ByteArray {
                val xbyte = ((jis shr 8) - 0x20) * 2
                val top = 2048 - (0x80 - (jis and 0xff)) * 16
                val out = ByteArray(32)
                for (y in 0 until 16) {
                    // BMP は下の行から並ぶ
                    f.seek(offset + (2047L - (top + y)) * 256 + xbyte)
                    f.readFully(out, y * 2, 2)
                }
                return out
            }
            val a = cell(0x3021)
            val q = cell(0x2129)
            val blank = q.all { it == 0xff.toByte() }     // 1 の画素が白
            return !blank && a.contentEquals(q)
        }
    }

    private fun japanesePaint() = Paint().apply { textLocale = Locale.JAPAN }

    private fun hasJapaneseFont(): Boolean {
        val p = japanesePaint()
        return p.hasGlyph("亜") && p.hasGlyph("あ")
    }

    /**
     * 日本語フォント。Android 10 以上は、システムが日本語に使うフォントを先頭に置き、
     * 無い字はシステムのフォントで補う。それより前は、既定のフォントを日本語の字体で使う。
     * 返す名前はログと報告のため
     */
    private fun japaneseTypeface(): Pair<Typeface, String> {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            runCatching {
                val ja = SystemFonts.getAvailableFonts().filter { f ->
                    val locales = f.localeList
                    (0 until locales.size()).any { locales.get(it).language == "ja" } &&
                        f.style.weight == FontStyle.FONT_WEIGHT_NORMAL &&
                        f.style.slant == FontStyle.FONT_SLANT_UPRIGHT
                }
                // システムが日本語に使っているもの (既定のフォントで描いた「亜あ」と同じ形になるもの) を選ぶ
                val ref = sample(Typeface.DEFAULT)
                for (f in ja) {
                    val tf = Typeface.CustomFallbackBuilder(FontFamily.Builder(f).build())
                        .setSystemFallback("sans-serif").build()
                    if (sample(tf).contentEquals(ref)) {
                        return tf to "${f.file?.name ?: "?"}#${f.ttcIndex}"
                    }
                }
            }.onFailure { Log.w(TAG, "font lookup failed", it) }
        }
        return Typeface.DEFAULT to "default (ja)"
    }

    private fun sample(tf: Typeface): IntArray {
        val bmp = Bitmap.createBitmap(64, 32, Bitmap.Config.ARGB_8888)
        val p = japanesePaint().apply { typeface = tf; textSize = 24f; color = Color.BLACK }
        Canvas(bmp).drawText("亜あ", 0f, 26f, p)
        val px = IntArray(64 * 32)
        bmp.getPixels(px, 0, 64, 0, 0, 64, 32)
        bmp.recycle()
        return px
    }

    /** Shift JIS (MS932。NEC 特殊文字を含む) で読む */
    private fun sjisCharset(): Charset {
        for (name in arrayOf("windows-31j", "MS932", "Shift_JIS")) {
            runCatching { return Charset.forName(name) }
        }
        return Charsets.ISO_8859_1
    }

    /** JIS の区・点 (0 から) の字。読めない (Unicode に無い) ときは null */
    private fun kutenToString(cs: Charset, ku: Int, ten: Int): String? {
        val j1 = ku + 0x21
        val j2 = ten + 0x21
        val s1 = ((j1 + 1) shr 1) + (if (j1 <= 0x5e) 0x70 else 0xb0)
        val s2 = if (j1 and 1 != 0) (j2 + (if (j2 >= 0x60) 0x20 else 0x1f)) else (j2 + 0x7e)
        val dec = cs.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
        val s = runCatching { dec.decode(ByteBuffer.wrap(byteArrayOf(s1.toByte(), s2.toByte()))).toString() }
            .getOrNull() ?: return null
        return s.takeIf { it.isNotEmpty() && it != "�" && it[0].code >= 0x80 }
    }

    /** 半角 (Shift JIS の 1 バイト) の字。PC-98 の 5C は ¥、7C は ¦、7E は ‾ (FONT.ROM と同じ) */
    private fun ankToString(code: Int): String? = when (code) {
        0x5c -> "¥"
        0x7c -> "¦"
        0x7e -> "‾"
        in 0x20..0x7e -> code.toChar().toString()
        in 0xa1..0xdf -> (0xff61 + code - 0xa1).toChar().toString()
        else -> null
    }

    /**
     * エミュレータのスレッドから JNI で呼ばれる。全部の字を描いたバイト列 (形は port/androidfont.c) を返す。
     * 日本語フォントが無いときや、描けなかったときは null (今までどおり英数字だけの font.tmp になる)
     */
    fun render(): ByteArray? {
        return try {
            if (!hasJapaneseFont()) {
                Log.i(TAG, "font.tmp: no Japanese font on this device")
                return null
            }
            val start = SystemClock.uptimeMillis()
            progress?.invoke(true)
            val (tf, name) = japaneseTypeface()
            val out = renderTable(tf, ANTI_ALIAS, THRESHOLD)
            settings?.fontTmpVersion = VERSION
            Log.i(TAG, "font.tmp: glyphs from $name in ${SystemClock.uptimeMillis() - start} ms")
            out
        } catch (t: Throwable) {
            Log.e(TAG, "font.tmp: render failed", t)
            null
        } finally {
            progress?.invoke(false)
        }
    }

    /** ink の画素 (濃さが threshold 以上) を 1 にして、左から bits ビット分を返す */
    private fun row(px: IntArray, base: Int, bits: Int, threshold: Int): Int {
        var v = 0
        for (x in 0 until bits) {
            if ((px[base + x] ushr 24) >= threshold) v = v or (0x8000 ushr x)
        }
        return v
    }

    private fun renderTable(tf: Typeface, antiAlias: Boolean, threshold: Int, ankTf: Typeface = tf): ByteArray {
        val out = ByteArray(ANK_SIZE + KNJ_SIZE)
        val paint = japanesePaint().apply {
            typeface = tf
            isAntiAlias = antiAlias
            color = Color.BLACK
            textSize = 16f
        }
        // 全角は「国」の上下の中心を 16 ドットの中心に合わせる (どの字も同じ高さに置く)
        val bounds = Rect()
        paint.getTextBounds("国", 0, 1, bounds)
        val kBase = 8f - (bounds.top + bounds.bottom) / 2f

        val cols = 94
        val bmp = Bitmap.createBitmap(cols * 16, 16, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        val px = IntArray(cols * 16 * 16)

        // ---- 半角 (英数字・半角カナ) は 8x16。字幅が 8 を超えるものは横を縮める
        bmp.eraseColor(Color.TRANSPARENT)
        val ank = (0 until 256).map { ankToString(it) }
        for (half in 0 until 2) {
            bmp.eraseColor(Color.TRANSPARENT)
            for (i in 0 until 128) {
                val s = ank[half * 128 + i] ?: continue
                // 半角カナは日本語フォントで描く
                paint.typeface = if (half == 0) ankTf else tf
                if (!paint.hasGlyph(s)) continue
                val w = paint.measureText(s)
                val scale = if (w > 8f) 8f / w else 1f
                paint.textScaleX = scale
                canvas.save()
                canvas.clipRect(i * 8, 0, i * 8 + 8, 16)
                canvas.drawText(s, i * 8 + (8f - w * scale) / 2f, kBase, paint)
                canvas.restore()
                paint.textScaleX = 1f
            }
            bmp.getPixels(px, 0, cols * 16, 0, 0, cols * 16, 16)
            for (i in 0 until 128) {
                val code = half * 128 + i
                for (y in 0 until 16) {
                    out[code * 16 + y] = (row(px, y * cols * 16 + i * 8, 8, threshold) shr 8).toByte()
                }
            }
        }

        paint.typeface = tf

        // ---- 全角は 16x16。1 区ずつ (94 字を横に並べて) 描く
        val cs = sjisCharset()
        for (ku in 0 until 94) {
            bmp.eraseColor(Color.TRANSPARENT)
            var any = false
            for (ten in 0 until 94) {
                val s = kutenToString(cs, ku, ten) ?: continue
                if (!paint.hasGlyph(s)) continue          // フォントに無い字 (豆腐になるもの) は空白
                val w = paint.measureText(s)
                canvas.save()
                canvas.clipRect(ten * 16, 0, ten * 16 + 16, 16)
                canvas.drawText(s, ten * 16 + (16f - w) / 2f, kBase, paint)
                canvas.restore()
                any = true
            }
            if (!any) continue
            bmp.getPixels(px, 0, cols * 16, 0, 0, cols * 16, 16)
            for (ten in 0 until 94) {
                val o = ANK_SIZE + (ku * 94 + ten) * 32
                for (y in 0 until 16) {
                    val v = row(px, y * cols * 16 + ten * 16, 16, threshold)
                    out[o + y * 2] = (v shr 8).toByte()
                    out[o + y * 2 + 1] = v.toByte()
                }
            }
        }
        bmp.recycle()
        return out
    }
}
