package jp.sawada.np2android

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File

/**
 * ディスクイメージの置き場所と取り込み。
 *
 * scoped storage ではエミュレータ (C の fopen) から任意の場所のファイルを開けないため、
 * SAF で選ばれたファイルはアプリ専用領域 (Android/data/<pkg>/files/disks) にコピーして使う。
 * HDD イメージへの書き込みもコピー側に行われる。
 */
object DiskImages {

    enum class Kind(val extensions: Set<String>) {
        /**
         * d88 系・fdi・dcp/dcu・nfd・fdd (Virtual98) は形式判定あり (np2kai の diskimage/)。
         * それ以外はサイズで判定されるベタイメージ (合わなければ BKDSK (hdb など) として判定)
         */
        FDD(setOf("d88", "88d", "d98", "98d", "fdi", "xdf", "hdm", "dup", "2hd", "tfd", "nfd", "fdd", "flp", "img",
            "dcp", "dcu", "hdb")),
        /** SASI/IDE: thd・nhd・hdi、SCSI: hdd */
        HDD(setOf("thd", "nhd", "hdi", "hdd")),
    }

    /** エミュレータの作業フォルダ (np2.cfg・ROM・ステートセーブの置き場所) */
    fun dataDir(context: Context): File =
        context.getExternalFilesDir(null) ?: context.filesDir

    /** 取り込んだディスクイメージの置き場所 */
    fun diskDir(context: Context): File = File(dataDir(context), "disks").apply { mkdirs() }

    fun matches(kind: Kind, name: String): Boolean =
        name.substringAfterLast('.', "").lowercase() in kind.extensions

    /** 取り込み済み (disks/ と作業フォルダ直下) のイメージ一覧 */
    fun list(context: Context, kind: Kind): List<File> {
        val dirs = listOf(diskDir(context), dataDir(context))
        return dirs.flatMap { dir -> dir.listFiles()?.filter { it.isFile && matches(kind, it.name) } ?: emptyList() }
            .sortedBy { it.name.lowercase() }
    }

    fun displayName(context: Context, uri: Uri): String? =
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }

    /** ファイル名として安全な形にする (パス区切りなどを除く) */
    fun sanitize(name: String): String =
        name.replace(Regex("""[/\\:*?"<>|\u0000-\u001f]"""), "_").trim().ifEmpty { "disk" }

    /**
     * [uri] の内容を [dest] にコピーする (呼び出し側でワーカースレッドから呼ぶこと)。
     * 途中で失敗したら中途半端なファイルを残さない。
     */
    fun copy(context: Context, uri: Uri, dest: File, onProgress: (Long) -> Unit) {
        val tmp = File(dest.parentFile, dest.name + ".part")
        try {
            context.contentResolver.openInputStream(uri).use { input ->
                requireNotNull(input) { "cannot open $uri" }
                tmp.outputStream().use { out ->
                    val buf = ByteArray(256 * 1024)
                    var total = 0L
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                        total += n
                        onProgress(total)
                    }
                }
            }
            if (dest.exists()) dest.delete()
            if (!tmp.renameTo(dest)) error("rename failed: $dest")
        } finally {
            tmp.delete()
        }
    }
}
