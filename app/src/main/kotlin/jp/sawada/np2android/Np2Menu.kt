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
     * (SASI/IDE は thd・nhd・hdi・slh・vhd、SCSI は hdd・nhd・hdi・hdn・hds・vhd を受け付ける)。
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
            ctx.getString(R.string.newdisk_menu) to { newDiskMenu() },
            "ステートセーブ" to { slotMenu(save = true) },
            "ステートロード" to { slotMenu(save = false) },
            "リセット" to { confirm("リセットしますか？") { Np2Native.reset() } },
            "詳細設定 (np2 内蔵メニュー)" to { Np2Native.toggleMenu() },
            "ファイル管理" to { host.openFileManager() },
            "設定" to { host.openSettings() },
            ctx.getString(R.string.about_menu) to { AboutDialog.show(ctx) },
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

    // ---- 空のディスクイメージ作成 ----

    /**
     * 作れるイメージの種類。[type] と [params] は Np2Native.createDisk にそのまま渡す。
     * FDD は D88 ヘッダーの fd_type (上位 4 ビットが種類)、hdi は np2 の SASI の型番、nhd・hdd は MB。
     */
    private class NewDiskType(val label: String, val ext: String, val type: Int,
                              val params: List<Pair<String, Int>>)

    private val newDiskTypes = listOf(
        NewDiskType("FDD 2HD (1.25MB, d88)", "d88", Np2Native.NEWDISK_FDD, listOf("" to 0x20)),
        NewDiskType("FDD 2DD (640KB, d88)", "d88", Np2Native.NEWDISK_FDD, listOf("" to 0x10)),
        NewDiskType("HDD SASI (hdi)", "hdi", Np2Native.NEWDISK_HDI,
            listOf("5MB" to 0, "10MB" to 1, "15MB" to 2, "20MB" to 3, "30MB" to 5, "40MB" to 6)),
        NewDiskType("HDD IDE (nhd)", "nhd", Np2Native.NEWDISK_NHD,
            listOf("20MB" to 20, "40MB" to 40, "80MB" to 80, "128MB" to 128, "256MB" to 256, "512MB" to 512)),
        NewDiskType("HDD SCSI (hdd)", "hdd", Np2Native.NEWDISK_VHD,
            listOf("20MB" to 20, "40MB" to 40, "80MB" to 80, "128MB" to 128, "256MB" to 256, "512MB" to 512)),
    )

    private fun newDiskMenu() {
        builder()
            .setTitle(R.string.newdisk_title)
            .setItems(newDiskTypes.map { it.label }.toTypedArray()) { _, i ->
                val t = newDiskTypes[i]
                if (t.params.size == 1) {
                    askNewDiskName(t, t.params[0])
                } else {
                    builder()
                        .setTitle(t.label)
                        .setItems(t.params.map { it.first }.toTypedArray()) { _, j -> askNewDiskName(t, t.params[j]) }
                        .setNegativeButton(R.string.cancel, null)
                        .show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun askNewDiskName(t: NewDiskType, param: Pair<String, Int>) {
        val edit = android.widget.EditText(ctx).apply {
            setText("newdisk")
            setSingleLine()
            selectAll()
        }
        val pad = (20 * ctx.resources.displayMetrics.density).toInt()
        val box = android.widget.FrameLayout(ctx).apply {
            setPadding(pad, pad / 2, pad, 0)
            addView(edit)
        }
        builder()
            .setTitle(ctx.getString(R.string.newdisk_name_title, t.ext))
            .setView(box)
            .setPositiveButton(R.string.newdisk_create) { _, _ ->
                val base = DiskImages.sanitize(edit.text.toString()).removeSuffix(".${t.ext}")
                createNewDisk(t, param, File(DiskImages.diskDir(ctx), "$base.${t.ext}"))
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun createNewDisk(t: NewDiskType, param: Pair<String, Int>, dest: File) {
        if (dest.exists()) {
            builder()
                .setMessage(ctx.getString(R.string.newdisk_exists, dest.name))
                .setPositiveButton(R.string.ok, null)
                .show()
            return
        }
        val progress = builder()
            .setMessage(ctx.getString(R.string.newdisk_creating, dest.name))
            .setCancelable(false)
            .show()
        val main = android.os.Handler(android.os.Looper.getMainLooper())
        kotlin.concurrent.thread(name = "newdisk") {
            val ok = runCatching { Np2Native.createDisk(dest.absolutePath, t.type, param.second) }
                .getOrDefault(false)
            main.post {
                progress.dismiss()
                if (ok) {
                    builder()
                        .setMessage(ctx.getString(R.string.newdisk_done, dest.name))
                        .setPositiveButton(R.string.ok, null)
                        .show()
                } else {
                    dest.delete()
                    toast(ctx.getString(R.string.newdisk_failed, dest.name))
                }
            }
        }
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

    /**
     * [path] を [t] 以外のドライブが使っていれば、そのドライブ名を返す。
     * 同じイメージを2つのドライブに入れると、ドライブごとにメモリに持つ D88 のヘッダーや
     * HDD の FILE* のバッファが食い違ってイメージが壊れるため、二重マウントは禁止する。
     * HDD は np2.cfg の設定 (リセット後に開くもの) で調べる。
     */
    private fun usedByOtherDrive(t: DiskTarget, path: String): String? {
        val drives = (0..3).map { DiskTarget(DiskImages.Kind.FDD, it, "FDD${it + 1}") } +
                listOf(hdd1, hdd2) + scsi
        for (d in drives) {
            if (d.kind == t.kind && d.drive == t.drive) continue
            val p = if (d.kind == DiskImages.Kind.FDD) Np2Native.getFdd(d.drive) else Np2Native.getHdd(d.drive)
            if (p == path) return d.label
        }
        return null
    }

    /** 二重マウントなら知らせて true を返す */
    private fun refuseDoubleMount(t: DiskTarget, path: String, name: String): Boolean {
        val other = usedByOtherDrive(t, path) ?: return false
        builder()
            .setTitle(R.string.disk_in_use_title)
            .setMessage(ctx.getString(R.string.disk_in_use_message, name, other, t.label))
            .setPositiveButton(R.string.ok, null)
            .show()
        return true
    }

    /** SAF で登録した仮想パス [path] を挿入する */
    fun insertDirect(t: DiskTarget, entry: DiskImageSource.Entry, path: String) {
        if (refuseDoubleMount(t, path, entry.name)) return
        if (t.kind == DiskImages.Kind.FDD) {
            // 結果 (マウントできたか・書き込み禁止か) は np2 から onFddResult で届くので、そこで表示する
            Np2Native.setFdd(t.drive, path)
        } else {
            Np2Native.setHdd(t.drive, path)
            askReset("${t.label} を ${entry.name} に変更しました。" +
                    if (entry.readOnly) "\n" + ctx.getString(R.string.disk_hdd_readonly) else "")
        }
    }

    fun insert(t: DiskTarget, file: File) {
        if (refuseDoubleMount(t, file.absolutePath, file.name)) return
        if (t.kind == DiskImages.Kind.FDD) {
            // 結果は np2 から onFddResult で届く
            Np2Native.setFdd(t.drive, file.absolutePath)
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

        override fun onFddResult(drive: Int, result: Int, path: String) {
            val label = "FDD${drive + 1}"
            val name = File(path).name
            when (result) {
                Np2Native.FDD_MOUNTED -> toast(ctx.getString(R.string.disk_inserted, label, name))
                Np2Native.FDD_MOUNTED_PROTECTED -> toast(ctx.getString(R.string.disk_inserted_readonly, label, name))
                else -> builder()
                    .setTitle(R.string.disk_mount_failed_title)
                    .setMessage(ctx.getString(R.string.disk_mount_failed, name, label))
                    .setPositiveButton(R.string.ok, null)
                    .show()
            }
        }

        override fun onHddFailed(drive: Int, path: String) {
            val label = when {
                drive < 0x20 -> "HDD${drive + 1}"
                else -> "SCSI #${drive - 0x20}"
            }
            builder()
                .setTitle(R.string.disk_mount_failed_title)
                .setMessage(ctx.getString(R.string.disk_hdd_failed, File(path).name, label))
                .setPositiveButton(R.string.ok, null)
                .show()
        }

        override fun onDiskError(path: String) {
            Toast.makeText(ctx, ctx.getString(R.string.disk_write_error, File(path).name), Toast.LENGTH_LONG).show()
        }

        override fun onStateConflict(slot: Int, info: String) {
            builder()
                .setTitle("ディスクがセーブ時と異なります")
                .setMessage("$info\n\nこのままロードしますか？" +
                        if (info.contains("not found")) "\n\n" + ctx.getString(R.string.state_disk_not_found_hint) else "")
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
        val SASI_EXTENSIONS = setOf("thd", "nhd", "hdi", "slh", "vhd")
        val SCSI_EXTENSIONS = setOf("hdd", "nhd", "hdi", "hdn", "hds", "vhd")
    }
}
