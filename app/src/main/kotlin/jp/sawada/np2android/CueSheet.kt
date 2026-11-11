package jp.sawada.np2android

import android.content.Context
import android.net.Uri
import android.util.Log
import java.nio.charset.Charset

/**
 * CUE シートから、使うファイル (FILE の行) の名前を取り出す。
 * 中身の解釈 (トラックや位置) は np2 コア (fdd/sxsicd.c) が行うので、ここでは .bin を
 * 選んでもらうための名前と、ファイルの数 (複数なら使えない) だけを見る。
 */
object CueSheet {

    private const val TAG = "NP2SAF"
    /** CUE シートはふつう数 KB。これより大きいものは CUE シートとして扱わない */
    private const val MAX_SIZE = 64 * 1024

    /** FILE の行に書かれた名前の一覧。読めなければ null */
    fun files(context: Context, uri: Uri): List<String>? {
        val bytes = runCatching {
            context.contentResolver.openInputStream(uri)?.use { input ->
                val buf = ByteArray(MAX_SIZE)
                var n = 0
                while (n < buf.size) {
                    val r = input.read(buf, n, buf.size - n)
                    if (r < 0) break
                    n += r
                }
                buf.copyOf(n)
            }
        }.onFailure { Log.w(TAG, "cue read failed: $uri", it) }.getOrNull() ?: return null
        return parse(decode(bytes))
    }

    /** UTF-8 として正しくなければ Shift_JIS (日本のツールで作られた CUE シート) として読む */
    private fun decode(b: ByteArray): String {
        val utf8 = Charsets.UTF_8.newDecoder()
        return runCatching { utf8.decode(java.nio.ByteBuffer.wrap(b)).toString() }
            .getOrElse { String(b, Charset.forName("MS932")) }
            .removePrefix("﻿")
    }

    fun parse(text: String): List<String> =
        text.lineSequence().mapNotNull { line ->
            val s = line.trim()
            if (!s.startsWith("FILE", ignoreCase = true) || s.length < 5 || !s[4].isWhitespace()) {
                return@mapNotNull null
            }
            val rest = s.substring(5).trim()
            if (rest.startsWith("\"")) {
                rest.substring(1).substringBefore('"')
            } else {
                // FILE <名前> <種類>。名前に空白があれば引用符で囲まれているはずなので、最後の語を種類とみなす
                rest.substringBeforeLast(' ', rest).trim()
            }
        }.filter { it.isNotEmpty() }.toList()
}
