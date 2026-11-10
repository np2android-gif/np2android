package jp.sawada.np2android

import android.database.Cursor
import android.database.MatrixCursor
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract.Document
import android.provider.DocumentsContract.Root
import android.provider.DocumentsProvider
import android.util.Log
import java.io.File
import java.io.FileNotFoundException
import kotlin.concurrent.thread

/**
 * デバッグビルド専用: SAF の直接オープンの異常系を再現するためのテスト用プロバイダー。
 * リリース・profile ビルドには含まれない (src/debug にだけある)。
 *
 * 端末の Android/data/<pkg>/files/saftest/ に置いたファイルを、名前の頭で返し方を変えて見せる。
 * - ro_*   : 書き込みを許可しない (FLAG_SUPPORTS_WRITE なし、"w" を含むモードは拒否)
 * - pipe_* : パイプで返す (シークできない。クラウド系プロバイダーのストリーム配信と同じ)
 * - nl_*   : アプリ専用の内部領域 (/data/user/...) にコピーしたものを返す
 *            (Google ドライブなどが自分のキャッシュの fd を返すのと同じ状態)
 * - それ以外: 普通に読み書きできるファイルとして返す
 */
class SafTestProvider : DocumentsProvider() {

    private val srcDir: File get() = File(context!!.getExternalFilesDir(null), "saftest").apply { mkdirs() }
    private val nlDir: File get() = File(context!!.filesDir, "saftest_nl").apply { mkdirs() }

    override fun onCreate() = true

    override fun queryRoots(projection: Array<out String>?): Cursor =
        MatrixCursor(projection ?: ROOT_COLUMNS).apply {
            newRow().apply {
                add(Root.COLUMN_ROOT_ID, ROOT)
                add(Root.COLUMN_DOCUMENT_ID, ROOT)
                add(Root.COLUMN_TITLE, "NP2 SAF テスト")
                add(Root.COLUMN_FLAGS, 0)
                add(Root.COLUMN_ICON, android.R.drawable.ic_menu_manage)
            }
        }

    override fun queryDocument(documentId: String, projection: Array<out String>?): Cursor =
        MatrixCursor(projection ?: DOC_COLUMNS).apply { addDoc(this, documentId) }

    override fun queryChildDocuments(parentDocumentId: String, projection: Array<out String>?,
                                     sortOrder: String?): Cursor =
        MatrixCursor(projection ?: DOC_COLUMNS).apply {
            srcDir.listFiles()?.filter { it.isFile }?.sortedBy { it.name }?.forEach { addDoc(this, "f:${it.name}") }
        }

    private fun addDoc(c: MatrixCursor, id: String) {
        val row = c.newRow()
        if (id == ROOT) {
            row.add(Document.COLUMN_DOCUMENT_ID, ROOT)
            row.add(Document.COLUMN_DISPLAY_NAME, "NP2 SAF テスト")
            row.add(Document.COLUMN_MIME_TYPE, Document.MIME_TYPE_DIR)
            row.add(Document.COLUMN_FLAGS, 0)
            return
        }
        val f = source(id)
        row.add(Document.COLUMN_DOCUMENT_ID, id)
        row.add(Document.COLUMN_DISPLAY_NAME, f.name)
        row.add(Document.COLUMN_MIME_TYPE, "application/octet-stream")
        row.add(Document.COLUMN_SIZE, f.length())
        row.add(Document.COLUMN_LAST_MODIFIED, f.lastModified())
        row.add(Document.COLUMN_FLAGS, if (f.name.startsWith("ro_") || f.name.startsWith("pipe_")) 0
                                       else Document.FLAG_SUPPORTS_WRITE)
    }

    private fun source(id: String): File {
        if (!id.startsWith("f:")) throw FileNotFoundException(id)
        val name = id.removePrefix("f:")
        if (name.contains('/')) throw FileNotFoundException(id)
        return File(srcDir, name)
    }

    override fun openDocument(documentId: String, mode: String, signal: CancellationSignal?): ParcelFileDescriptor {
        val f = source(documentId)
        if (!f.isFile) throw FileNotFoundException(documentId)
        val write = mode.contains('w')
        Log.i(TAG, "openDocument ${f.name} mode=$mode")
        return when {
            f.name.startsWith("ro_") -> {
                if (write) throw SecurityException("read-only test document: ${f.name}")
                ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY)
            }
            f.name.startsWith("pipe_") -> {
                if (write) throw SecurityException("pipe test document is read-only: ${f.name}")
                val pipe = ParcelFileDescriptor.createReliablePipe()
                thread(name = "saftest-pipe") {
                    runCatching {
                        ParcelFileDescriptor.AutoCloseOutputStream(pipe[1]).use { out ->
                            f.inputStream().use { it.copyTo(out) }
                        }
                    }.onFailure { Log.i(TAG, "pipe closed: ${it.message}") }
                }
                pipe[0]
            }
            f.name.startsWith("nl_") -> {
                val copy = File(nlDir, f.name)
                if (!copy.exists() || copy.lastModified() < f.lastModified()) f.copyTo(copy, overwrite = true)
                ParcelFileDescriptor.open(copy, ParcelFileDescriptor.parseMode(mode))
            }
            else -> ParcelFileDescriptor.open(f, ParcelFileDescriptor.parseMode(mode))
        }
    }

    companion object {
        private const val TAG = "NP2SAFTEST"
        private const val ROOT = "root"
        private val ROOT_COLUMNS = arrayOf(Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID, Root.COLUMN_TITLE,
            Root.COLUMN_FLAGS, Root.COLUMN_ICON)
        private val DOC_COLUMNS = arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS)
    }
}
