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
        fun openSettings()
    }

    /** ディスクの挿入先 */
    data class DiskTarget(val kind: DiskImages.Kind, val drive: Int, val label: String)

    private fun builder() = AlertDialog.Builder(ctx, android.R.style.Theme_DeviceDefault_Dialog_Alert)

    private val fdd1 = DiskTarget(DiskImages.Kind.FDD, 0, "FDD1")
    private val fdd2 = DiskTarget(DiskImages.Kind.FDD, 1, "FDD2")
    private val hdd1 = DiskTarget(DiskImages.Kind.HDD, 0x00, "HDD1")
    private val hdd2 = DiskTarget(DiskImages.Kind.HDD, 0x01, "HDD2")

    fun show() {
        val items = listOf<Pair<String, () -> Unit>>(
            "FDD1: ${current(fdd1)}" to { diskMenu(fdd1) },
            "FDD2: ${current(fdd2)}" to { diskMenu(fdd2) },
            "HDD1: ${current(hdd1)}" to { diskMenu(hdd1) },
            "HDD2: ${current(hdd2)}" to { diskMenu(hdd2) },
            "ステートセーブ" to { slotMenu(save = true) },
            "ステートロード" to { slotMenu(save = false) },
            "リセット" to { confirm("リセットしますか？") { Np2Native.reset() } },
            "詳細設定 (np2 内蔵メニュー)" to { Np2Native.toggleMenu() },
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

    private fun diskMenu(t: DiskTarget) {
        val files = DiskImages.list(ctx, t.kind)
        val labels = mutableListOf("＋ 端末から取り込む…")
        labels += files.map { it.name }
        val hasDisk = current(t) != "(なし)"
        if (hasDisk) labels += "取り出す"

        builder()
            .setTitle("${t.label}: ${current(t)}")
            .setItems(labels.toTypedArray()) { _, i ->
                when {
                    i == 0 -> host.importDisk(t)
                    i <= files.size -> insert(t, files[i - 1])
                    else -> eject(t)
                }
            }
            .setNegativeButton("キャンセル", null)
            .show()
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
    }
}
