package jp.sawada.np2android

/**
 * ステートのディスクの確認結果 (port/np2bridge.c の checkdisks) を、確認ダイアログの文にする。
 * 1 行に 1 台、"名前\t種類\tセーブ時のファイル名\t今のファイル名"。
 * 名前は np2 の statsave.c の名前 (FDD1〜 / SASI1〜 / SCSI0〜)。種類は
 * updated (セーブ後に書き換えられた) / notfound (見つからない) / change (ロードで切り替わる) / gone (ロードで外れる)。
 */
class StateDiskCheck(info: String) {

    private class Line(val label: String, val kind: String, val saved: String, val now: String)

    private val lines = info.lineSequence().mapNotNull { s ->
        val f = s.split('\t')
        if (f.size < 4) null else Line(label(f[0]), f[1], f[2], f[3])
    }.toList()

    /** セーブ後に書き換えられたディスクがある (ロードするとゲストのファイルシステムが壊れるおそれ) */
    val hasUpdated = lines.any { it.kind == "updated" }

    /** 確認の本文。[loadWord] は「ロード」か「レジューム」 */
    fun message(loadWord: String): String {
        val sb = StringBuilder()
        for (l in lines) {
            sb.append("・").append(l.label).append(": ").append(
                when (l.kind) {
                    "updated" -> "${l.saved} はセーブのあとで書き換えられています"
                    "notfound" -> "${l.saved} が見つかりません。${loadWord}すると、このドライブは空になります"
                    "gone" -> "${loadWord}すると、今の ${l.now} は外れます (セーブ時はつながっていません)"
                    else -> "${loadWord}すると、${l.now.ifEmpty { "(なし)" }} から ${l.saved} に切り替わります"
                }
            ).append('\n')
        }
        if (hasUpdated) {
            sb.append("\n書き換えられたディスクのまま${loadWord}すると、ゲストのファイルシステムが壊れるおそれがあります。" +
                    "ふつうは${loadWord}しないでください。")
        } else {
            sb.append("\n${loadWord}すると、HDD の設定もセーブ時のディスクに合わせて変わります。")
        }
        if (lines.any { it.kind == "notfound" }) {
            sb.append("\n\n見つからないディスクは、${loadWord}後にメニューの「端末のファイルを直接開く」で選び直してください。")
        }
        return sb.toString()
    }

    private companion object {
        fun label(dv: String): String = when {
            dv == "SASI1" -> "HDD1"
            dv == "SASI2" -> "HDD2"
            dv == "SASI3" -> "CD-ROM"
            dv.startsWith("SASI") -> "IDE${dv.removePrefix("SASI")}"
            dv.startsWith("SCSI") -> "SCSI #${dv.removePrefix("SCSI")}"
            else -> dv   // FDD1〜
        }
    }
}
