package jp.sawada.np2android

import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.CancellationSignal
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import android.provider.DocumentsContract.Root
import android.provider.DocumentsProvider
import android.util.Log
import android.webkit.MimeTypeMap
import java.io.File
import java.io.FileNotFoundException

/**
 * エミュレータの作業フォルダ ([DiskImages.dataDir]) を「ファイル」アプリや他のアプリの
 * ファイル選択画面 (SAF) に公開する。
 *
 * - ドキュメント ID は "work" (作業フォルダ自体) または "work/<作業フォルダからの相対パス>"。
 * - エミュレータが使用中のディスクイメージは、書き込み・削除・名前変更・移動を拒否する
 *   (開いたままのファイルを上書きするとイメージが壊れるため)。読み出しはできる。
 * - np2.cfg はエミュレータが起動時に読み、設定変更のたびに書き戻す。外から編集した内容は
 *   次回起動時に反映され、それまでにメニューで設定を変えると上書きされる (一覧の説明欄に表示する)。
 */
class Np2DocumentsProvider : DocumentsProvider() {

    private val root: File get() = DiskImages.dataDir(context!!)

    override fun onCreate(): Boolean = true

    // ---- 一覧 ---------------------------------------------------------------

    override fun queryRoots(projection: Array<out String>?): Cursor {
        val c = MatrixCursor(projection ?: ROOT_PROJECTION)
        val dir = root
        c.newRow().apply {
            add(Root.COLUMN_ROOT_ID, ROOT_ID)
            add(Root.COLUMN_DOCUMENT_ID, ROOT_ID)
            add(Root.COLUMN_TITLE, context!!.getString(R.string.app_name))
            add(Root.COLUMN_SUMMARY, "作業フォルダ")
            add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE or Root.FLAG_LOCAL_ONLY or Root.FLAG_SUPPORTS_IS_CHILD)
            add(Root.COLUMN_ICON, R.mipmap.ic_launcher)
            add(Root.COLUMN_AVAILABLE_BYTES, dir.usableSpace)
            add(Root.COLUMN_MIME_TYPES, "*/*")
        }
        return c
    }

    override fun queryDocument(documentId: String, projection: Array<out String>?): Cursor {
        val c = MatrixCursor(projection ?: DOCUMENT_PROJECTION)
        addRow(c, documentId, fileFor(documentId), disksInUse())
        return c
    }

    override fun queryChildDocuments(parentDocumentId: String, projection: Array<out String>?, sortOrder: String?): Cursor {
        val c = MatrixCursor(projection ?: DOCUMENT_PROJECTION)
        val dir = fileFor(parentDocumentId)
        val inUse = disksInUse()
        dir.listFiles()?.filter { !isHidden(it) }?.forEach { f ->
            addRow(c, childId(parentDocumentId, f.name), f, inUse)
        }
        c.setNotificationUri(context!!.contentResolver, childrenUri(parentDocumentId))
        return c
    }

    override fun isChildDocument(parentDocumentId: String, documentId: String): Boolean =
        documentId.startsWith("$parentDocumentId/")

    override fun getDocumentType(documentId: String): String = mimeOf(fileFor(documentId))

    // ---- 読み書き -----------------------------------------------------------

    override fun openDocument(documentId: String, mode: String, signal: CancellationSignal?): ParcelFileDescriptor {
        val file = fileFor(documentId)
        val writing = mode.any { it == 'w' || it == 'a' || it == 't' }
        if (writing) checkNotInUse(file)
        if (file.isDirectory) throw FileNotFoundException("is a directory: $documentId")
        // "w" は Android のバージョンによって切り詰めないことがある。上書き保存で末尾にゴミが残らないよう "wt" にする
        val m = if (mode == "w") "wt" else mode
        val flags = ParcelFileDescriptor.parseMode(m)
        if (!writing) return ParcelFileDescriptor.open(file, flags)
        return ParcelFileDescriptor.open(file, flags, Handler(Looper.getMainLooper())) {
            notifyChanged(parentId(documentId))
        }
    }

    override fun createDocument(parentDocumentId: String, mimeType: String, displayName: String): String {
        val dir = fileFor(parentDocumentId)
        if (!dir.isDirectory) throw FileNotFoundException("not a directory: $parentDocumentId")
        val file = uniqueFile(dir, DiskImages.sanitize(displayName))
        val ok = if (mimeType == Document.MIME_TYPE_DIR) file.mkdir() else file.createNewFile()
        if (!ok) throw FileNotFoundException("cannot create: ${file.name}")
        notifyChanged(parentDocumentId)
        return childId(parentDocumentId, file.name)
    }

    override fun deleteDocument(documentId: String) {
        if (documentId == ROOT_ID) throw UnsupportedOperationException("cannot delete root")
        val file = fileFor(documentId)
        val inUse = disksInUse()
        if (file.walkTopDown().any { isInUse(it, inUse) }) throw inUseError(file)
        if (!file.deleteRecursively()) throw IllegalStateException("cannot delete: ${file.name}")
        notifyChanged(parentId(documentId))
    }

    override fun renameDocument(documentId: String, displayName: String): String {
        if (documentId == ROOT_ID) throw UnsupportedOperationException("cannot rename root")
        val file = fileFor(documentId)
        checkNotInUse(file)
        val name = DiskImages.sanitize(displayName)
        if (name == file.name) return documentId
        val dest = File(file.parentFile, name)
        // 共有ストレージは大文字小文字を区別しないので、大文字小文字だけの変更は「同名あり」にしない
        if (dest.exists() && !name.equals(file.name, ignoreCase = true)) throw IllegalStateException("already exists: $name")
        if (!file.renameTo(dest)) throw IllegalStateException("cannot rename: ${file.name}")
        val parent = parentId(documentId)
        notifyChanged(parent)
        return childId(parent, name)
    }

    override fun moveDocument(sourceDocumentId: String, sourceParentDocumentId: String, targetParentDocumentId: String): String {
        val file = fileFor(sourceDocumentId)
        checkNotInUse(file)
        val dir = fileFor(targetParentDocumentId)
        if (!dir.isDirectory) throw FileNotFoundException("not a directory: $targetParentDocumentId")
        if (isChildDocument(sourceDocumentId, targetParentDocumentId)) {
            throw IllegalArgumentException("cannot move into itself")
        }
        val dest = File(dir, file.name)
        if (dest.exists()) throw IllegalStateException("already exists: ${file.name}")
        if (!file.renameTo(dest)) throw IllegalStateException("cannot move: ${file.name}")
        notifyChanged(sourceParentDocumentId)
        notifyChanged(targetParentDocumentId)
        return childId(targetParentDocumentId, file.name)
    }

    // ---- 下請け -------------------------------------------------------------

    /** ドキュメント ID → ファイル。作業フォルダの外を指す ID (../ など) は拒否する */
    private fun fileFor(documentId: String): File {
        val base = root.canonicalFile
        if (documentId == ROOT_ID) return base
        if (!documentId.startsWith("$ROOT_ID/")) throw FileNotFoundException("unknown document: $documentId")
        val file = File(base, documentId.substring(ROOT_ID.length + 1)).canonicalFile
        if (!file.path.startsWith(base.path + File.separator)) throw FileNotFoundException("outside of root: $documentId")
        if (!file.exists()) throw FileNotFoundException("not found: $documentId")
        return file
    }

    private fun childId(parentId: String, name: String) = "$parentId/$name"

    private fun parentId(documentId: String) = documentId.substringBeforeLast('/', ROOT_ID)

    private fun addRow(c: MatrixCursor, id: String, f: File, inUse: Set<String>) {
        val used = isInUse(f, inUse)
        var flags = 0
        if (f.isDirectory) {
            flags = flags or Document.FLAG_DIR_SUPPORTS_CREATE
            if (id != ROOT_ID) flags = flags or Document.FLAG_SUPPORTS_DELETE or
                    Document.FLAG_SUPPORTS_RENAME or Document.FLAG_SUPPORTS_MOVE
        } else if (!used) {
            flags = flags or Document.FLAG_SUPPORTS_WRITE or Document.FLAG_SUPPORTS_DELETE or
                    Document.FLAG_SUPPORTS_RENAME or Document.FLAG_SUPPORTS_MOVE
        }
        c.newRow().apply {
            add(Document.COLUMN_DOCUMENT_ID, id)
            add(Document.COLUMN_DISPLAY_NAME, if (id == ROOT_ID) context!!.getString(R.string.app_name) else f.name)
            add(Document.COLUMN_MIME_TYPE, mimeOf(f))
            add(Document.COLUMN_SIZE, if (f.isDirectory) null else f.length())
            add(Document.COLUMN_LAST_MODIFIED, f.lastModified())
            add(Document.COLUMN_FLAGS, flags)
            add(Document.COLUMN_SUMMARY, summaryOf(f, used))
        }
    }

    private fun summaryOf(f: File, used: Boolean): String? = when {
        used -> "使用中のため書き込み・削除はできません"
        f.name == "np2.cfg" && f.parentFile?.canonicalPath == root.canonicalPath ->
            "編集は次回起動時に反映 (それまでに設定を変えると上書きされます)"
        else -> null
    }

    private fun mimeOf(f: File): String {
        if (f.isDirectory) return Document.MIME_TYPE_DIR
        val ext = f.name.substringAfterLast('.', "").lowercase()
        if (ext in TEXT_EXTENSIONS) return "text/plain"
        return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
    }

    /**
     * 書き込み途中の一時ファイル (セーブ時の "np2sdl2.sav.tmp"・取り込み中の "xxx.hdi.part") は見せない。
     * フォントの "font.tmp" のような通常のファイルを隠さないよう、拡張子が二重のものだけを対象にする。
     */
    private fun isHidden(f: File): Boolean {
        val n = f.name
        val base = when {
            n.endsWith(".tmp") -> n.removeSuffix(".tmp")
            n.endsWith(".part") -> n.removeSuffix(".part")
            else -> return false
        }
        return base.contains('.')
    }

    /** 同名があれば "name (1).ext" のようにずらす */
    private fun uniqueFile(dir: File, name: String): File {
        var f = File(dir, name)
        val stem = name.substringBeforeLast('.')
        val ext = name.substringAfterLast('.', "").let { if (it.isEmpty() || it == name) "" else ".$it" }
        var n = 1
        while (f.exists()) f = File(dir, "$stem ($n)$ext").also { n++ }
        return f
    }

    /** エミュレータが使用中 (またはリセット後に使う) ディスクイメージの正規化パス */
    private fun disksInUse(): Set<String> {
        if (!Np2Native.loaded) return emptySet()
        return try {
            Np2Native.getDisksInUse().orEmpty().filterNotNull()
                .mapNotNull { runCatching { File(it).canonicalPath }.getOrNull() }.toSet()
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "native not available", e)
            emptySet()
        }
    }

    private fun isInUse(f: File, inUse: Set<String>) = f.isFile && f.canonicalPath in inUse

    private fun checkNotInUse(file: File) {
        val inUse = disksInUse()
        if (file.walkTopDown().any { isInUse(it, inUse) }) throw inUseError(file)
    }

    private fun inUseError(file: File) =
        SecurityException("${file.name} はエミュレータが使用中です。取り出すか、アプリを終了してから操作してください。")

    private fun childrenUri(parentId: String): Uri = DocumentsContract.buildChildDocumentsUri(AUTHORITY, parentId)

    private fun notifyChanged(parentId: String) =
        context!!.contentResolver.notifyChange(childrenUri(parentId), null, false)

    companion object {
        private const val TAG = "Np2DocumentsProvider"
        const val AUTHORITY = BuildConfig.APPLICATION_ID + ".documents"
        const val ROOT_ID = "work"

        private val TEXT_EXTENSIONS = setOf("cfg", "ini", "txt", "log")

        private val ROOT_PROJECTION = arrayOf(
            Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID, Root.COLUMN_TITLE, Root.COLUMN_SUMMARY,
            Root.COLUMN_FLAGS, Root.COLUMN_ICON, Root.COLUMN_AVAILABLE_BYTES, Root.COLUMN_MIME_TYPES,
        )
        private val DOCUMENT_PROJECTION = arrayOf(
            Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
            Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS, Document.COLUMN_SUMMARY,
        )
    }
}
