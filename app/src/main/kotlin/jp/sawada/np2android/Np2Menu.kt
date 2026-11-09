package jp.sawada.np2android

import android.app.AlertDialog
import android.content.Context
import android.widget.Toast
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Kotlin 版メニュー (旧版のオプションメニューの置き換え)。
 * エミュレータ本体の細かい設定 (CPU・サウンドボード等) は np2 内蔵メニューに任せる。
 */
class Np2Menu(private val ctx: Context, private val host: Host) {

    interface Host {
        /** SAF で端末内のファイルを選び、取り込み後に [target] へ挿入する */
        fun importDisk(target: DiskTarget)
        /** SAF で端末内のファイルを選び、コピーせずに [target] へ挿入する */
        fun openDirect(target: DiskTarget)
        /** 直接オープンの履歴から [target] へ挿入する */
        fun insertRecent(target: DiskTarget, entry: DiskImageSource.Entry)
        fun openSettings()
        /** 作業フォルダをシステムのファイル画面で開く */
        fun openFileManager()
    }

    /**
     * ディスクの挿入先。[extensions] はそのドライブで使える拡張子
     * (np2 は SASI/IDE で thd・nhd・hdi、SCSI で hdd だけを受け付ける)。
     */
    data class DiskTarget(
        val kind: DiskImages.Kind,
        val drive: Int,
        val label: String,
        val extensions: Set<String> = kind.extensions,
    ) {
        fun accepts(name: String) = name.substringAfterLast('.', "").lowercase() in extensions
    }

    private fun builder() = AlertDialog.Builder(ctx, android.R.style.Theme_DeviceDefault_Dialog_Alert)

    private val fdd1 = DiskTarget(DiskImages.Kind.FDD, 0, "FDD1")
    private val fdd2 = DiskTarget(DiskImages.Kind.FDD, 1, "FDD2")
    private val hdd1 = DiskTarget(DiskImages.Kind.HDD, 0x00, "HDD1", SASI_EXTENSIONS)
    private val hdd2 = DiskTarget(DiskImages.Kind.HDD, 0x01, "HDD2", SASI_EXTENSIONS)
    private val scsi = (0..3).map { DiskTarget(DiskImages.Kind.HDD, 0x20 + it, "SCSI #$it", SCSI_EXTENSIONS) }

    fun show() {
        val items = listOf<Pair<String, () -> Unit>>(
            "FDD1: ${current(fdd1)}" to { diskMenu(fdd1) },
            "FDD2: ${current(fdd2)}" to { diskMenu(fdd2) },
            "HDD1: ${current(hdd1)}" to { diskMenu(hdd1) },
            "HDD2: ${current(hdd2)}" to { diskMenu(hdd2) },
            "SCSI HDD…" to { scsiMenu() },
            "ステートセーブ" to { slotMenu(save = true) },
            "ステートロード" to { slotMenu(save = false) },
            "リセット" to { confirm("リセットしますか？") { Np2Native.reset() } },
            "詳細設定 (np2 内蔵メニュー)" to { Np2Native.toggleMenu() },
            "ファイル管理" to { host.openFileManager() },
            "設定" to { host.openSettings() },
            "終了" to { confirm("Neko Project II を終了しますか？") { Np2Native.exit() } },
        )
        builder()
            .setTitle("Neko Project II")
            .setItems(items.map { it.first }.toTypedArray()) { _, i -> items[i].second() }
            .show()
    }

    private fun current(t: DiskTarget): String {
        val path = if (t.kind == DiskImages.Kind.FDD) Np2Native.getFdd(t.drive) else Np2Native.getHdd(t.drive)
        return path?.let { File(it).name } ?: "(なし)"
    }

    // ---- ディスク ----

    /** SCSI #0〜#3 (np2 内蔵メニューからディスク操作を外したので、こちらで設定する) */
    private fun scsiMenu() {
        builder()
            .setTitle("SCSI HDD")
            .setItems(scsi.map { "${it.label}: ${current(it)}" }.toTypedArray()) { _, i -> diskMenu(scsi[i]) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun diskMenu(t: DiskTarget) {
        val items = mutableListOf<Pair<String, () -> Unit>>()
        items += ctx.getString(R.string.disk_open_direct) to { host.openDirect(t) }
        items += ctx.getString(R.string.disk_import_copy) to { host.importDisk(t) }
        for (e in DiskImageSource.recent(ctx, t.kind).filter { t.accepts(it.name) }) {
            items += ctx.getString(R.string.disk_direct_suffix, e.name) to { host.insertRecent(t, e) }
        }
        for (f in DiskImages.list(ctx, t.kind).filter { t.accepts(it.name) }) {
            items += f.name to { insert(t, f) }
        }
        if (current(t) != "(なし)") items += "取り出す" to { eject(t) }

        builder()
            .setTitle("${t.label}: ${current(t)}")
            .setItems(items.map { it.first }.toTypedArray()) { _, i -> items[i].second() }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** SAF で登録した仮想パス [path] を挿入する */
    fun insertDirect(t: DiskTarget, entry: DiskImageSource.Entry, path: String) {
        if (t.kind == DiskImages.Kind.FDD) {
            Np2Native.setFdd(t.drive, path)
            toast(ctx.getString(if (entry.readOnly) R.string.disk_inserted_readonly else R.string.disk_inserted,
                t.label, entry.name))
        } else {
            Np2Native.setHdd(t.drive, path)
            askReset("${t.label} を ${entry.name} に変更しました。" +
                    if (entry.readOnly) "\n" + ctx.getString(R.string.disk_hdd_readonly) else "")
        }
    }

    fun insert(t: DiskTarget, file: File) {
        if (t.kind == DiskImages.Kind.FDD) {
            Np2Native.setFdd(t.drive, file.absolutePath)
            toast("${t.label} に ${file.name} を入れました")
        } else {
            Np2Native.setHdd(t.drive, file.absolutePath)
            askReset("${t.label} を ${file.name} に変更しました。")
        }
    }

    private fun eject(t: DiskTarget) {
        if (t.kind == DiskImages.Kind.FDD) {
            Np2Native.setFdd(t.drive, null)
            toast("${t.label} を取り出しました")
        } else {
            Np2Native.setHdd(t.drive, null)
            askReset("${t.label} を外しました。")
        }
    }

    /** HDD の変更はリセット後に有効になる */
    private fun askReset(message: String) {
        builder()
            .setMessage("$message\nHDD の変更はリセット後に有効になります。いまリセットしますか？")
            .setPositiveButton("リセット") { _, _ -> Np2Native.reset() }
            .setNegativeButton("あとで", null)
            .show()
    }

    // ---- ステート ----

    private fun slotFile(slot: Int) = File(DiskImages.dataDir(ctx), "np2sdl2.s%02d".format(slot))

    private fun slotMenu(save: Boolean) {
        val fmt = SimpleDateFormat("yyyy/MM/dd HH:mm", Locale.JAPAN)
        val labels = (0 until SLOTS).map { s ->
            val f = slotFile(s)
            "スロット ${s + 1}  " + if (f.exists()) fmt.format(Date(f.lastModified())) else "(空)"
        }
        builder()
            .setTitle(if (save) "ステートセーブ" else "ステートロード")
            .setItems(labels.toTypedArray()) { _, s ->
                when {
                    save && slotFile(s).exists() ->
                        confirm("スロット ${s + 1} に上書きしますか？") { Np2Native.saveState(s) }
                    save -> Np2Native.saveState(s)
                    !slotFile(s).exists() -> toast("スロット ${s + 1} は空です")
                    else -> confirm("スロット ${s + 1} をロードしますか？\n現在の状態は失われます。") {
                        Np2Native.loadState(s, false)
                    }
                }
            }
            .setNegativeButton("キャンセル", null)
            .show()
    }

    /** ネイティブからの結果通知 (UI スレッド) */
    val listener = object : Np2Native.Listener {
        override fun onStateSaved(slot: Int, success: Boolean) {
            toast(if (success) "スロット ${slot + 1} にセーブしました" else "セーブに失敗しました")
        }

        override fun onStateLoaded(slot: Int, result: Int) {
            toast(when (result) {
                Np2Native.LOAD_OK -> "スロット ${slot + 1} をロードしました"
                Np2Native.LOAD_NOT_FOUND -> "スロット ${slot + 1} は空です"
                else -> "このステートは現在の構成では読み込めません"
            })
        }

        override fun onDiskError(path: String) {
            Toast.makeText(ctx, ctx.getString(R.string.disk_write_error, File(path).name), Toast.LENGTH_LONG).show()
        }

        override fun onStateConflict(slot: Int, info: String) {
            builder()
                .setTitle("ディスクがセーブ時と異なります")
                .setMessage("$info\n\nこのままロードしますか？")
                .setPositiveButton("ロード") { _, _ -> Np2Native.loadState(slot, true) }
                .setNegativeButton("キャンセル", null)
                .show()
        }
    }

    // ---- 共通 ----

    private fun confirm(message: String, action: () -> Unit) {
        builder()
            .setMessage(message)
            .setPositiveButton("OK") { _, _ -> action() }
            .setNegativeButton("キャンセル", null)
            .show()
    }

    fun toast(message: String) = Toast.makeText(ctx, message, Toast.LENGTH_SHORT).show()

    companion object {
        const val SLOTS = 4
        val SASI_EXTENSIONS = setOf("thd", "nhd", "hdi")
        val SCSI_EXTENSIONS = setOf("hdd")
    }
}
