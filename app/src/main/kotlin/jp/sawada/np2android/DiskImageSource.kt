package jp.sawada.np2android

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.FileNotFoundException
import java.util.UUID

/**
 * SAF で選んだディスクイメージを、作業フォルダへコピーせずに元の場所から直接使う。
 *
 * content URI から ParcelFileDescriptor を開き、fd をネイティブ側に登録して仮想パス
 * "saf:/<id>/<表示名>" を得る (fd はネイティブ側が dup して持つので、ここではすぐ閉じる)。
 * 仮想パスは np2 コアから普通のファイル名に見えるので、既存の setFdd / setHdd にそのまま渡せる。
 *
 * 履歴 (URI・ID・表示名) は SharedPreferences に保存する。ID は URI ごとに固定なので、
 * レジュームファイルや np2.cfg に残った仮想パスも、起動時に [registerAll] で登録し直せば開ける。
 */
object DiskImageSource {

    private const val TAG = "NP2SAF"
    private const val PREFS = "saf_disks"
    private const val KEY = "entries"
    /** 永続 URI 権限には上限 (アプリごとに 128〜512) があるので、履歴はこの件数までにする */
    private const val MAX_HISTORY = 16

    data class Entry(
        val id: String,
        val uri: Uri,
        val name: String,
        val kind: DiskImages.Kind,
        val readOnly: Boolean,
        val lastUsed: Long,
        /** 実体が端末のストレージに無いので、ユーザーの選択で読み取り専用として開いている */
        val nonLocal: Boolean = false,
        /** .cue・.ccd・.cdm・.mds が使う本体 (.bin・.img・.mdf。CD-ROM のみ)。同じ ID に読み取り専用で付ける */
        val subUri: Uri? = null,
        val subName: String? = null,
    ) {
        /** 登録済みならこの接頭辞の仮想パスになっている */
        val pathPrefix get() = "saf:/$id/"
    }

    sealed class Result {
        /** [path] を setFdd / setHdd に渡す */
        data class Ok(val entry: Entry, val path: String) : Result()
        /** 拡張子が [kind] に合わない */
        data class Unsupported(val name: String) : Result()
        /** パイプなどシークできない。コピー方式なら使える */
        data class NotSeekable(val name: String) : Result()
        /**
         * シークできるが、実体が端末のストレージに無い (クラウド系プロバイダーが自分のキャッシュの
         * fd を返す場合など、書き込みが元の場所に届かないおそれがある)。読み取り専用かコピー方式なら使える
         */
        data class NotLocal(val name: String) : Result()
        /** 権限が無い・ファイルが無い */
        data class NotFound(val name: String) : Result()
    }

    @Volatile private var cache: MutableList<Entry>? = null

    // ---- 履歴 ---------------------------------------------------------------

    @Synchronized
    private fun entries(context: Context): MutableList<Entry> {
        cache?.let { return it }
        val list = mutableListOf<Entry>()
        val s = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null)
        if (s != null) {
            runCatching {
                val a = JSONArray(s)
                for (i in 0 until a.length()) {
                    val o = a.getJSONObject(i)
                    list += Entry(
                        id = o.getString("id"),
                        uri = Uri.parse(o.getString("uri")),
                        name = o.getString("name"),
                        kind = DiskImages.Kind.valueOf(o.getString("kind")),
                        readOnly = o.optBoolean("ro"),
                        lastUsed = o.optLong("used"),
                        nonLocal = o.optBoolean("nl"),
                        subUri = o.optString("sub").takeIf { it.isNotEmpty() }?.let { Uri.parse(it) },
                        subName = o.optString("subname").takeIf { it.isNotEmpty() },
                    )
                }
            }.onFailure { Log.e(TAG, "history broken", it) }
        }
        cache = list
        return list
    }

    @Synchronized
    private fun save(context: Context) {
        val a = JSONArray()
        for (e in entries(context)) {
            a.put(JSONObject()
                .put("id", e.id)
                .put("uri", e.uri.toString())
                .put("name", e.name)
                .put("kind", e.kind.name)
                .put("ro", e.readOnly)
                .put("used", e.lastUsed)
                .put("nl", e.nonLocal)
                .put("sub", e.subUri?.toString() ?: "")
                .put("subname", e.subName ?: ""))
        }
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY, a.toString()).apply()
    }

    /** [kind] の履歴 (新しい順) */
    @Synchronized
    fun recent(context: Context, kind: DiskImages.Kind): List<Entry> =
        entries(context).filter { it.kind == kind }.sortedByDescending { it.lastUsed }

    // ---- 開く ---------------------------------------------------------------

    /**
     * ファイル選択 (ACTION_OPEN_DOCUMENT) の結果を登録する。
     * [flags] は結果の Intent の flags (永続権限として取れる範囲を判断する)。
     * ブロックすることがあるので、ワーカースレッドから呼ぶこと。
     * [readOnlyIfNotLocal] = true なら、実体が端末のストレージに無いファイルも読み取り専用で開く
     * (false なら [Result.NotLocal] を返すので、ユーザーに選ばせてから true で呼び直す)。
     */
    fun openPicked(context: Context, uri: Uri, flags: Int, target: Np2Menu.DiskTarget,
                   readOnlyIfNotLocal: Boolean = false, subUri: Uri? = null): Result {
        val kind = target.kind
        val name = queryName(context, uri)
        if (!target.accepts(name)) return Result.Unsupported(name)

        val cr = context.contentResolver
        // CD-ROM は読み取りの権限だけを持つ
        val canWrite = kind != DiskImages.Kind.CD && (flags and Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0
        runCatching {
            cr.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or
                    if (canWrite) Intent.FLAG_GRANT_WRITE_URI_PERMISSION else 0)
        }.onFailure {
            // 書き込みつきで取れなければ読み取りだけ取る
            runCatching { cr.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
                .onFailure { e -> Log.w(TAG, "takePersistableUriPermission failed: $uri", e) }
        }
        if (subUri != null) {
            runCatching { cr.takePersistableUriPermission(subUri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
                .onFailure { e -> Log.w(TAG, "takePersistableUriPermission failed: $subUri", e) }
        }
        val subName = subUri?.let { queryName(context, it) }

        val id = synchronized(this) { entries(context).firstOrNull { it.uri == uri }?.id }
            ?: UUID.randomUUID().toString().replace("-", "").take(16)
        val result = register(context, id, uri, name, kind, readOnlyIfNotLocal, subUri, subName)
        if (result is Result.Ok) {
            remember(context, result.entry)
        } else if (result !is Result.NotLocal) {
            // 使えなかったファイルの権限は持ち続けない。NotLocal はユーザーが「読み取り専用で開く」を
            // 選ぶかもしれないので残す (一度解放すると永続権限を取り直せない)。選ばなかったら [discard]
            discard(context, uri)
            subUri?.let { discard(context, it) }
        }
        return result
    }

    /** 履歴に無い [uri] の永続権限を解放する (選んだが使わなかったファイル) */
    fun discard(context: Context, uri: Uri) {
        if (synchronized(this) { entries(context).none { it.uri == uri || it.subUri == uri } }) {
            releasePermission(context, uri)
        }
    }

    /** 履歴のイメージを使う直前に登録し直す (fd を取り直す) */
    fun reopen(context: Context, entry: Entry): Result {
        val result = register(context, entry.id, entry.uri, entry.name, entry.kind, entry.nonLocal,
            entry.subUri, entry.subName)
        if (result is Result.Ok) {
            remember(context, result.entry)
        } else if (result is Result.NotFound) {
            forget(context, entry)
        }
        return result
    }

    /**
     * 起動時に、np2.cfg・レジューム・ステートスロットが参照している履歴だけを登録する。
     * これらに残った仮想パスはコアが起動直後やロード時に直接開くので、エミュレータスレッドが
     * 動き出す前 (onCreate) に呼ぶこと。それ以外の履歴は、メニューから挿入するときに登録する
     * (開きっぱなしの fd を履歴の件数だけ持たないように)。
     * @return 開けなくなって履歴から外したイメージの表示名
     */
    fun registerReferenced(context: Context): List<String> {
        val referenced = referencedIds(context)
        val persisted = context.contentResolver.persistedUriPermissions
            .filter { it.isReadPermission }.map { it.uri }.toSet()
        val lost = mutableListOf<String>()
        for (e in synchronized(this) { entries(context).toList() }) {
            if (e.uri !in persisted || (e.subUri != null && e.subUri !in persisted)) {
                // 権限が無くなった (アプリのデータ消去など)。参照の有無にかかわらず使えない
                forget(context, e)
                lost += e.name
                continue
            }
            if (e.id !in referenced) continue
            val r = register(context, e.id, e.uri, e.name, e.kind, e.nonLocal, e.subUri, e.subName)
            if (r is Result.NotFound) {
                forget(context, e)
                lost += e.name
            } else if (r !is Result.Ok) {
                Log.w(TAG, "startup register failed: ${e.name} $r")
            }
        }
        return lost
    }

    /**
     * np2.cfg・レジューム (np2sdl2.sav)・ステートスロット (np2sdl2.s00〜) に書かれた仮想パスの ID。
     * どれも仮想パスをそのままの文字列で持っている (ステートは STATPATH 構造体の path)。
     */
    private fun referencedIds(context: Context): Set<String> {
        val dir = DiskImages.dataDir(context)
        val files = listOf("np2.cfg", "np2sdl2.sav") + (0 until Np2Menu.SLOTS).map { "np2sdl2.s%02d".format(it) }
        val ids = mutableSetOf<String>()
        // 前回の CD-ROM (np2.cfg には保存されないので、アプリの設定に持っている)
        savedCdPath(context)?.takeIf { it.startsWith("saf:/") }?.let { ids += it.removePrefix("saf:/").substringBefore('/') }
        val prefix = "saf:/".toByteArray()
        for (f in files.map { java.io.File(dir, it) }.filter { it.isFile }) {
            val b = runCatching { f.readBytes() }.getOrNull() ?: continue
            var i = 0
            while (i <= b.size - prefix.size) {
                if (b[i] == prefix[0] && (prefix.indices).all { b[i + it] == prefix[it] }) {
                    val start = i + prefix.size
                    var end = start
                    while (end < b.size && end - start < 40 && isIdByte(b[end])) end++
                    if (end < b.size && b[end] == '/'.code.toByte() && end > start) {
                        ids += String(b, start, end - start, Charsets.US_ASCII)
                    }
                    i = end
                } else {
                    i++
                }
            }
        }
        Log.i(TAG, "referenced at startup: $ids")
        return ids
    }

    private fun isIdByte(c: Byte): Boolean {
        val ch = c.toInt().toChar()
        return ch in '0'..'9' || ch in 'a'..'z' || ch in 'A'..'Z' || ch == '-' || ch == '_'
    }

    /**
     * URI を開いてネイティブに登録する。"rw" で開けなければ "r" のまま読み取り専用にする。
     * "w" は使わない (プロバイダーによっては中身を切り詰める)。
     *
     * 先に "r" で開いて、ファイルがあることを確かめる。DownloadsProvider の raw: URI などは
     * ファイルが無いと "rw" で空のファイルを作ってしまうため (削除済みのイメージが 0 バイトで復活する)。
     *
     * 実体が端末のストレージに無い場合は、[allowNonLocal] なら読み取り専用で登録し、
     * そうでなければ [Result.NotLocal] を返す。
     */
    private fun register(context: Context, id: String, uri: Uri, name: String, kind: DiskImages.Kind,
                         allowNonLocal: Boolean = false, subUri: Uri? = null, subName: String? = null): Result {
        val cr = context.contentResolver
        val probe = try {
            cr.openFileDescriptor(uri, "r")
        } catch (e: Exception) {
            Log.w(TAG, "open failed: $name (${e.javaClass.simpleName}: ${e.message})")
            null
        } ?: return Result.NotFound(name)

        // CD-ROM は読むだけなので "r" でしか開かない
        val rw = if (kind == DiskImages.Kind.CD) null else try {
            cr.openFileDescriptor(uri, "rw")
        } catch (e: Exception) {
            if (e is FileNotFoundException || e is SecurityException || e is UnsupportedOperationException ||
                e is IllegalArgumentException) {
                Log.i(TAG, "rw open failed, use read-only: $name (${e.message})")
            } else {
                Log.w(TAG, "rw open failed, use read-only: $name", e)
            }
            null
        }

        // パイプなどは端末のストレージかどうか以前に使えない (先に調べないと NotLocal 扱いになる)
        if (!isSeekableFile(rw ?: probe, name)) {
            rw?.close()
            probe.close()
            return Result.NotSeekable(name)
        }
        val local = isLocalStorage((rw ?: probe).fd, name)
        // CD-ROM は書き込まないので、端末のストレージに無くても確認なしで開く
        if (!local && !allowNonLocal && kind != DiskImages.Kind.CD) {
            rw?.close()
            probe.close()
            return Result.NotLocal(name)
        }
        // 端末のストレージに無いものは、書き込めても読み取り専用にする (書き込みが元に届かないおそれ)
        val pfd = if (local && rw != null) rw.also { probe.close() } else probe.also { rw?.close() }
        val readOnly = (pfd === probe)

        val path = pfd.use { Np2Native.safRegister(id, name, it.fd, readOnly) }
            ?: return Result.NotSeekable(name)
        Log.i(TAG, "opened $uri as $path ro=$readOnly local=$local")
        if (subUri != null) {
            val r = registerSub(context, id, subUri, subName ?: "bin")
            if (r != null) {
                Np2Native.safUnregister(id)
                return r
            }
        }
        return Result.Ok(Entry(id, uri, name, kind, readOnly, System.currentTimeMillis(), nonLocal = !local,
            subUri = subUri, subName = subName), path)
    }

    /** 目次ファイルが使う本体を読み取り専用で開いて [id] に付ける。失敗したらその結果を返す */
    private fun registerSub(context: Context, id: String, uri: Uri, name: String): Result? {
        val pfd = try {
            context.contentResolver.openFileDescriptor(uri, "r")
        } catch (e: Exception) {
            Log.w(TAG, "open failed: $name (${e.javaClass.simpleName}: ${e.message})")
            null
        } ?: return Result.NotFound(name)
        pfd.use {
            if (!isSeekableFile(it, name)) return Result.NotSeekable(name)
            if (!Np2Native.safRegisterSub(id, it.fd)) return Result.NotSeekable(name)
        }
        Log.i(TAG, "opened $uri as sub of $id ($name)")
        return null
    }

    /**
     * fd の実体が端末のストレージ (内部共有ストレージ・SD カード・USB ストレージ) にあるか。
     * Google ドライブなどのクラウド系プロバイダーは、ダウンロードした自分のキャッシュファイルの fd を
     * 返すことがある。シークできるので仮想パスとしては開けてしまうが、書き込みが元の場所に戻らない。
     * 実体のパスはプロバイダーごとに決まっていないため、端末のストレージのパスかどうかで判断する。
     */
    private fun isLocalStorage(fd: Int, name: String): Boolean {
        val real = runCatching { Os.readlink("/proc/self/fd/$fd") }.getOrNull()
        val local = real != null && LOCAL_PREFIXES.any { real.startsWith(it) }
        Log.i(TAG, "fd of $name -> $real (${if (local) "local" else "not local"})")
        return local
    }

    /** 通常ファイルでランダムアクセスできるか (クラウド系などはパイプを返すことがある) */
    private fun isSeekableFile(pfd: ParcelFileDescriptor, name: String): Boolean {
        val ok = runCatching {
            val fd = pfd.fileDescriptor
            OsConstants.S_ISREG(Os.fstat(fd).st_mode) && Os.lseek(fd, 0, OsConstants.SEEK_CUR) >= 0
        }.getOrElse {
            Log.w(TAG, "seek check failed: $name", it)
            false
        }
        if (!ok) Log.i(TAG, "$name is not a seekable regular file")
        return ok
    }

    private val LOCAL_PREFIXES = listOf("/storage/", "/mnt/", "/data/media/", "/sdcard/")

    private fun queryName(context: Context, uri: Uri): String {
        val n = runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0) else null
            }
        }.getOrNull()
        return DiskImages.sanitize(n ?: uri.lastPathSegment?.substringAfterLast('/') ?: "disk")
    }

    // ---- CD-ROM のパスの保存 ------------------------------------------------

    private const val KEY_CD = "cd_path"

    /** 最後に入っていた CD-ROM のパス (仮想パスか作業フォルダのパス)。無ければ null */
    fun savedCdPath(context: Context): String? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_CD, null)

    fun saveCdPath(context: Context, path: String?) {
        if (path == savedCdPath(context)) return
        Log.i(TAG, "cd path saved: $path")
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().apply {
            if (path.isNullOrEmpty()) remove(KEY_CD) else putString(KEY_CD, path)
        }.apply()
    }

    /**
     * 起動時に入れ直す CD-ROM のパス。[registerReferenced] の後に呼ぶこと。
     * 履歴から外れた (見つからなくなった) イメージや、消された作業フォルダのファイルなら null を返して忘れる
     */
    fun startupCdPath(context: Context): String? {
        val path = savedCdPath(context) ?: return null
        val ok = if (path.startsWith("saf:/")) {
            synchronized(this) { entries(context).any { path.startsWith(it.pathPrefix) } }
        } else {
            java.io.File(path).isFile
        }
        if (!ok) saveCdPath(context, null)
        return path.takeIf { ok }
    }

    // ---- 履歴の更新 ---------------------------------------------------------

    private fun remember(context: Context, entry: Entry) {
        val dropped: List<Entry>
        synchronized(this) {
            val list = entries(context)
            list.removeAll { it.id == entry.id || it.uri == entry.uri }
            list += entry
            dropped = trim(list)
            save(context)
        }
        for (e in dropped) release(context, e)
    }

    /** 上限を超えた分を古い順に外す。使用中のものは外さない。外したものを返す */
    private fun trim(list: MutableList<Entry>): List<Entry> {
        if (list.size <= MAX_HISTORY) return emptyList()
        val inUse = runCatching { Np2Native.getDisksInUse()?.filterNotNull() }.getOrNull().orEmpty()
        val dropped = list.sortedBy { it.lastUsed }
            .filter { e -> inUse.none { it.startsWith(e.pathPrefix) } }
            .take(list.size - MAX_HISTORY)
        list.removeAll(dropped)
        return dropped
    }

    private fun forget(context: Context, entry: Entry) {
        synchronized(this) {
            entries(context).removeAll { it.id == entry.id }
            save(context)
        }
        release(context, entry)
    }

    private fun release(context: Context, entry: Entry) {
        Log.i(TAG, "release ${entry.name} (${entry.id})")
        Np2Native.safUnregister(entry.id)
        releasePermission(context, entry.uri)
        entry.subUri?.let { releasePermission(context, it) }
    }

    private fun releasePermission(context: Context, uri: Uri) {
        runCatching {
            context.contentResolver.releasePersistableUriPermission(uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        }.recoverCatching {
            context.contentResolver.releasePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
    }
}
