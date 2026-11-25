package jp.sawada.np2android

import android.content.Context
import android.content.res.AssetManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.Typeface
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.charset.Charset
import java.nio.charset.CodingErrorAction
import java.util.Locale

/**
 * ANDROID-FONT (Q15): font.tmp (漢字 ROM の代わり) に使う字の形。
 *
 * font.tmp を作るのはネイティブの makepc98bmp (np2/font/fontmake.c) で、字の形だけを
 * [Np2Native.renderFontGlyphs] → [render] でここからもらう (port/androidfont.c)。PC-98 に無い文字の除外、
 * 78/83 JIS の入れ替え、内蔵の字の上書きは makepc98bmp が行う。
 *
 * 字の形は、assets の font16.bin (東雲フォントと JFドットJISKan16s の 16 ドットの字。作り方は
 * tools/font16/README.md) から取る。そこに無い字 (89〜92 区のまれな漢字など) だけ、端末のフォントで描く。
 *
 * 古い font.tmp (a22 までの「?」のものと、このアプリが前の版で作ったもの) は、起動のたびに [prepare] で
 * 調べて消し、エミュレータが起動するときに作り直させる。
 */
object FontGlyphs {

    /**
     * font.tmp を作る仕組みの版。字の形を変えて作り直させたいときに上げる ([Settings.fontTmpVersion])。
     * 1: 端末のフォント (a23 の前のテスト版だけ)、2: 東雲フォント + JFドットJISKan16s
     */
    const val VERSION = 2

    private const val TAG = "np2font"
    private const val ASSET = "font16.bin"
    private val MAGIC = byteArrayOf(0x4e, 0x50, 0x32, 0x46, 0x31, 0x36, 0x00, 0x01)   // "NP2F16" 0 1
    private const val ANK_SIZE = 256 * 16
    private const val KNJ_SIZE = 94 * 94 * 32
    private const val ANK_MARK = 256 / 8
    private const val KNJ_MARK = (94 * 94 + 7) / 8

    /** font.tmp の更新時刻が、記録した描いた時刻からこの範囲なら、このアプリが作ったものとみなす */
    private const val MADE_BEFORE_MS = 5_000L
    private const val MADE_AFTER_MS = 60_000L

    @Volatile
    private var settings: Settings? = null

    @Volatile
    private var assets: AssetManager? = null

    /**
     * 描いている間の表示 (true で出し、false で消す)。エミュレータのスレッドから呼ぶので、
     * 受け取る側で UI スレッドに回すこと。ふつうはすぐ終わり、表示は出ない
     */
    @Volatile
    var progress: ((Boolean) -> Unit)? = null

    /**
     * 起動時 (エミュレータのスレッドが動き出す前) に呼ぶ。版の記録が古く、font.tmp が古い作り方のものなら消す。
     *  - a22 までに作った「?」のもの
     *  - 版 1 で作ったもの (版 1 は描いた時刻を記録していないので、版 1 の記録があれば消す)
     *  - 版 2 から: 更新時刻が、描いた時刻の記録のすぐ後のもの
     * 外から写した font.tmp や、利用者が置いた FONT.BMP・FONT.ROM は消さない
     */
    fun prepare(context: Context, workDir: File, settings: Settings) {
        this.settings = settings
        this.assets = context.applicationContext.assets
        val made = settings.fontTmpVersion
        if (made >= VERSION) return
        val file = File(workDir, "font.tmp")
        if (!file.isFile) return
        val reason = when {
            made == 1 -> "made by version 1"
            made >= 2 && madeByApp(file, settings.fontTmpMadeAt) -> "made by version $made"
            runCatching { isAnkOnly(file) }.getOrElse {
                Log.w(TAG, "font.tmp: cannot read", it)
                false
            } -> "kanji were '?'"
            else -> return
        }
        if (file.delete()) {
            Log.i(TAG, "font.tmp: deleted the old one ($reason); it will be made again")
        } else {
            Log.w(TAG, "font.tmp: could not delete the old one")
        }
    }

    private fun madeByApp(file: File, madeAt: Long): Boolean {
        if (madeAt <= 0) return false
        val t = file.lastModified()
        return t >= madeAt - MADE_BEFORE_MS && t <= madeAt + MADE_AFTER_MS
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

    /** Shift JIS (MS932。NEC 特殊文字・NEC 選定 IBM 拡張文字を含む) で読む */
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

    private fun marked(data: ByteArray, base: Int, i: Int) =
        (data[base + (i shr 3)].toInt() and (0x80 ushr (i and 7))) != 0

    /**
     * エミュレータのスレッドから JNI で呼ばれる。全部の字のバイト列 (形は port/androidfont.c) を返す。
     * font16.bin が読めなかったときは null (今までどおり英数字だけの font.tmp になる)
     */
    fun render(): ByteArray? {
        return try {
            val am = assets ?: run {
                Log.w(TAG, "font.tmp: assets not ready")
                return null
            }
            val start = SystemClock.uptimeMillis()
            progress?.invoke(true)
            val data = am.open(ASSET).use { it.readBytes() }
            require(data.size == MAGIC.size + ANK_SIZE + KNJ_SIZE + ANK_MARK + KNJ_MARK &&
                data.copyOfRange(0, MAGIC.size).contentEquals(MAGIC)) { "$ASSET: bad data" }
            val out = data.copyOfRange(MAGIC.size, MAGIC.size + ANK_SIZE + KNJ_SIZE)
            val drawn = drawMissing(data, out)
            settings?.let {
                it.fontTmpVersion = VERSION
                it.fontTmpMadeAt = System.currentTimeMillis()
            }
            Log.i(TAG, "font.tmp: glyphs from $ASSET, $drawn from the device font, " +
                "in ${SystemClock.uptimeMillis() - start} ms")
            out
        } catch (t: Throwable) {
            Log.e(TAG, "font.tmp: render failed", t)
            null
        } finally {
            progress?.invoke(false)
        }
    }

    /**
     * font16.bin に無い全角の字を、端末のフォントで描いて out に入れる。描いた字の数を返す。
     * アンチエイリアスなしで描く (しきい値で切るやり方は、16 ドットでは細い線が消えるか、太りすぎた)。
     * 上下は「国」の中心を 8 ドット目に合わせる (東雲の漢字も 1〜14 段で、中心が同じ)
     */
    private fun drawMissing(data: ByteArray, out: ByteArray): Int {
        val markBase = MAGIC.size + ANK_SIZE + KNJ_SIZE + ANK_MARK
        val cs = sjisCharset()
        val paint = Paint().apply {
            textLocale = Locale.JAPAN
            typeface = Typeface.DEFAULT
            isAntiAlias = false
            color = Color.BLACK
            textSize = 16f
        }
        val bounds = Rect()
        paint.getTextBounds("国", 0, 1, bounds)
        val base = 8f - (bounds.top + bounds.bottom) / 2f
        val bmp = Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        val px = IntArray(16 * 16)
        val perKu = StringBuilder()
        var total = 0
        try {
            for (ku in 0 until 94) {
                var n = 0
                for (ten in 0 until 94) {
                    if (marked(data, markBase, ku * 94 + ten)) continue
                    val s = kutenToString(cs, ku, ten) ?: continue
                    if (!paint.hasGlyph(s)) continue          // フォントにも無い字 (豆腐になるもの) は空白
                    bmp.eraseColor(Color.TRANSPARENT)
                    canvas.drawText(s, (16f - paint.measureText(s)) / 2f, base, paint)
                    bmp.getPixels(px, 0, 16, 0, 0, 16, 16)
                    val o = ANK_SIZE + (ku * 94 + ten) * 32
                    for (y in 0 until 16) {
                        var v = 0
                        for (x in 0 until 16) {
                            if ((px[y * 16 + x] ushr 24) >= 128) v = v or (0x8000 ushr x)
                        }
                        out[o + y * 2] = (v shr 8).toByte()
                        out[o + y * 2 + 1] = v.toByte()
                    }
                    n++
                }
                if (n > 0) perKu.append(" ku${ku + 1}=$n")
                total += n
            }
        } finally {
            bmp.recycle()
        }
        if (total > 0) Log.i(TAG, "font.tmp: device font:$perKu")
        return total
    }
}
