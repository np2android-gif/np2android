package jp.sawada.np2android

/**
 * ステートのディスクの確認結果 (port/np2bridge.c の checkdisks) を、確認ダイアログの文にする。
 * 1 行に 1 台、"名前\t種類\tセーブ時のファイル名\t今のファイル名\tセーブ時の ID\t今の ID"。
 * 名前は np2 の statsave.c の名前 (FDD1〜 / SASI1〜 / SCSI0〜)。種類は
 * updated (セーブ後に書き換えられた) / notfound (見つからない) / change (ロードで切り替わる) / gone (ロードで外れる)。
 * ID は仮想パス (saf:/<id>/…) の <id> で、仮想パスでなければ空。ID の列が無い行 (4 列) も読む。
 */
class StateDiskCheck(
    info: String,
    /** (ID か null, 表示名) のイメージが今は開けない (SD カードなど。[DiskImageSource.isDiskUnavailable]) */
    private val isUnavailable: (String?, String) -> Boolean = { _, _ -> false },
    /** (ID か null, 表示名) のイメージが直接オープンの履歴にある (外れても履歴から入れ直せる) */
    private val inHistory: (String?, String) -> Boolean = { _, _ -> false },
) {

    private class Line(val label: String, val kind: String, val saved: String, val now: String,
                       val savedId: String?, val nowId: String?)

    private val lines = info.lineSequence().mapNotNull { s ->
        val f = s.split('\t')
        if (f.size < 4) null
        else Line(label(f[0]), f[1], f[2], f[3], f.getOrNull(4)?.ifEmpty { null }, f.getOrNull(5)?.ifEmpty { null })
    }.toList()

    private fun Line.savedUnavailable() = isUnavailable(savedId, saved)

    /** セーブ後に書き換えられたディスクがある (ロードするとゲストのファイルシステムが壊れるおそれ) */
    val hasUpdated = lines.any { it.kind == "updated" }

    /** 確認の本文。[loadWord] は「ロード」か「レジューム」 */
    fun message(loadWord: String): String {
        val sb = StringBuilder()
        for (l in lines) {
            sb.append("・").append(l.label).append(": ").append(
                when (l.kind) {
                    "updated" -> "${l.saved} はセーブのあとで書き換えられています"
                    "notfound" -> if (l.savedUnavailable()) {
                        "${l.saved} は SD カードなどにあり、今は開けません。${loadWord}すると、このドライブは空になり、" +
                                "次の前回の続きにも空で記録されます"
                    } else {
                        "${l.saved} が見つかりません。${loadWord}すると、このドライブは空になります"
                    }
                    "gone" -> "${loadWord}すると、今の ${l.now} は外れます (セーブ時はつながっていません)" +
                            if (inHistory(l.nowId, l.now)) "。後でメニューの履歴から入れ直せます" else ""
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
        val notFound = lines.filter { it.kind == "notfound" }
        if (notFound.any { it.savedUnavailable() }) {
            sb.append(if (loadWord == "レジューム") {
                "\n\n今は開けないディスクは、SD カードなどをつないでから、アプリを開き直してください (「終了」を選ぶと、" +
                        "何も保存せずに閉じます)。"
            } else {
                "\n\n今は開けないディスクは、SD カードなどをつないでから${loadWord}してください。"
            })
        }
        if (notFound.any { !it.savedUnavailable() }) {
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
