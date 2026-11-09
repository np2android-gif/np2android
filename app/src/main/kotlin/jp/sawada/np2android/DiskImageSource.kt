package jp.sawada.np2android

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import android.system.Os
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
    ) {
        /** 登録済みならこの接頭辞の仮想パスになっている */
        val pathPrefix get() = "saf:/$id/"
    }

    sealed class Result {
        /** [path] を setFdd / setHdd に渡す */
        data class Ok(val entry: Entry, val path: String) : Result()
        /** 拡張子が [kind] に合わない */
        data class Unsupported(val name: String) : Result()
        /**
         * パイプなどシークできない、または実体が端末のストレージに無い (クラウド系プロバイダーが
         * 自分のキャッシュの fd を返す場合など、書き込みが元の場所に届かない)。コピー方式なら使える
         */
        data class NotSeekable(val name: String) : Result()
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
                .put("used", e.lastUsed))
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
     */
    fun openPicked(context: Context, uri: Uri, flags: Int, target: Np2Menu.DiskTarget): Result {
        val kind = target.kind
        val name = queryName(context, uri)
        if (!target.accepts(name)) return Result.Unsupported(name)

        val cr = context.contentResolver
        val canWrite = (flags and Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0
        runCatching {
            cr.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or
                    if (canWrite) Intent.FLAG_GRANT_WRITE_URI_PERMISSION else 0)
        }.onFailure {
            // 書き込みつきで取れなければ読み取りだけ取る
            runCatching { cr.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
                .onFailure { e -> Log.w(TAG, "takePersistableUriPermission failed: $uri", e) }
        }

        val id = synchronized(this) { entries(context).firstOrNull { it.uri == uri }?.id }
            ?: UUID.randomUUID().toString().replace("-", "").take(16)
        val result = register(context, id, uri, name, kind)
        if (result is Result.Ok) {
            remember(context, result.entry)
        } else if (synchronized(this) { entries(context).none { it.uri == uri } }) {
            // 使えなかったファイルの権限は持ち続けない
            releasePermission(context, uri)
        }
        return result
    }

    /** 履歴のイメージを使う直前に登録し直す (fd を取り直す) */
    fun reopen(context: Context, entry: Entry): Result {
        val result = register(context, entry.id, entry.uri, entry.name, entry.kind)
        if (result is Result.Ok) {
            remember(context, result.entry)
        } else if (result is Result.NotFound) {
            forget(context, entry)
        }
        return result
    }

    /**
     * 起動時に履歴を全部登録する。レジュームファイルや np2.cfg に残った仮想パスを開けるよう、
     * エミュレータスレッドが動き出す前 (onCreate) に呼ぶ。
     * @return 開けなくなって履歴から外したイメージの表示名
     */
    fun registerAll(context: Context): List<String> {
        val persisted = context.contentResolver.persistedUriPermissions
            .filter { it.isReadPermission }.map { it.uri }.toSet()
        val lost = mutableListOf<String>()
        for (e in synchronized(this) { entries(context).toList() }) {
            val r = if (e.uri in persisted) register(context, e.id, e.uri, e.name, e.kind) else Result.NotFound(e.name)
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
     * URI を開いてネイティブに登録する。"rw" で開けなければ "r" のまま読み取り専用にする。
     * "w" は使わない (プロバイダーによっては中身を切り詰める)。
     *
     * 先に "r" で開いて、ファイルがあることを確かめる。DownloadsProvider の raw: URI などは
     * ファイルが無いと "rw" で空のファイルを作ってしまうため (削除済みのイメージが 0 バイトで復活する)。
     */
    private fun register(context: Context, id: String, uri: Uri, name: String, kind: DiskImages.Kind): Result {
        val cr = context.contentResolver
        val probe = try {
            cr.openFileDescriptor(uri, "r")
        } catch (e: Exception) {
            Log.w(TAG, "open failed: $name (${e.javaClass.simpleName}: ${e.message})")
            null
        } ?: return Result.NotFound(name)

        var readOnly = false
        val pfd = try {
            cr.openFileDescriptor(uri, "rw")?.also { probe.close() }
        } catch (e: Exception) {
            if (e is FileNotFoundException || e is SecurityException || e is UnsupportedOperationException ||
                e is IllegalArgumentException) {
                Log.i(TAG, "rw open failed, use read-only: $name (${e.message})")
            } else {
                Log.w(TAG, "rw open failed, use read-only: $name", e)
            }
            null
        } ?: probe.also { readOnly = true }

        val path = pfd.use {
            if (!isLocalStorage(it.fd, name)) return Result.NotSeekable(name)
            Np2Native.safRegister(id, name, it.fd, readOnly)
        } ?: return Result.NotSeekable(name)
        Log.i(TAG, "opened $uri as $path ro=$readOnly")
        return Result.Ok(Entry(id, uri, name, kind, readOnly, System.currentTimeMillis()), path)
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

    private val LOCAL_PREFIXES = listOf("/storage/", "/mnt/", "/data/media/", "/sdcard/")

    private fun queryName(context: Context, uri: Uri): String {
        val n = runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0) else null
            }
        }.getOrNull()
        return DiskImages.sanitize(n ?: uri.lastPathSegment?.substringAfterLast('/') ?: "disk")
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
