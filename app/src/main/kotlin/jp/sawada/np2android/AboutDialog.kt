package jp.sawada.np2android

import android.app.AlertDialog
import android.content.Context
import android.graphics.Typeface
import android.widget.ScrollView
import android.widget.TextView

/**
 * 「このアプリについて」: バージョンと、アプリに含まれるソフトウェアのライセンス。
 * ライセンスの本文は assets/licenses/ にある (元のソースのライセンス文をそのまま抜き出したもの)。
 */
object AboutDialog {

    private class Component(val name: String, val license: String, val asset: String)

    private val components = listOf(
        Component("Neko Project II / Xnp2", "修正 BSD ライセンス", "np2.txt"),
        Component("IA-32 CPU コア (i386c)", "BSD ライセンス (NONAKA Kimihiro)", "i386c.txt"),
        Component("NP2kai (FDD・HDD・CD-ROM イメージの処理)", "MIT ライセンス / BSD ライセンス", "np2kai.txt"),
        Component("SDL2", "zlib ライセンス", "sdl2.txt"),
        Component("zlib", "zlib ライセンス", "zlib.txt"),
    )

    private fun builder(ctx: Context) = AlertDialog.Builder(ctx, android.R.style.Theme_DeviceDefault_Dialog_Alert)

    fun show(ctx: Context) {
        val labels = components.map { "${it.name}\n  ${it.license}" }.toTypedArray()
        builder(ctx)
            .setTitle(ctx.getString(R.string.about_title, BuildConfig.VERSION_NAME))
            .setItems(labels) { _, i -> showLicense(ctx, components[i]) }
            .setPositiveButton(R.string.ok, null)
            .show()
    }

    private fun showLicense(ctx: Context, c: Component) {
        val text = runCatching {
            ctx.assets.open("licenses/${c.asset}").use { it.readBytes().toString(Charsets.UTF_8) }
        }.getOrElse { "(${c.asset} を読み込めませんでした)" }
        val pad = (16 * ctx.resources.displayMetrics.density).toInt()
        val view = TextView(ctx).apply {
            setText(text)
            typeface = Typeface.MONOSPACE
            textSize = 12f
            setTextIsSelectable(true)
            setPadding(pad, pad, pad, pad)
        }
        builder(ctx)
            .setTitle(c.name)
            .setView(ScrollView(ctx).apply { addView(view) })
            .setPositiveButton(R.string.ok) { _, _ -> show(ctx) }
            .setOnCancelListener { show(ctx) }
            .show()
    }
}
