package jp.sawada.np2android

import android.app.AlertDialog
import android.content.Context
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView

/** アプリ側 (Kotlin) の設定。エミュレータ本体の設定は np2.cfg (内蔵メニュー) 側にある */
class Settings(context: Context) {

    private val prefs = context.getSharedPreferences("ui", Context.MODE_PRIVATE)

    /** タッチパッド操作の移動倍率 */
    var mouseSpeed: Float
        get() = prefs.getFloat("mouse_speed", 1.0f)
        set(v) = prefs.edit().putFloat("mouse_speed", v).apply()

    /** フルキーボードの高さ (画面の高さに対する割合) */
    var keyboardHeight: Float
        get() = prefs.getFloat("keyboard_height", 0.5f)
        set(v) = prefs.edit().putFloat("keyboard_height", v).apply()

    /** ソフトキーの不透明度 */
    var keyOpacity: Float
        get() = prefs.getFloat("key_opacity", 1.0f)
        set(v) = prefs.edit().putFloat("key_opacity", v).apply()

    var haptics: Boolean
        get() = prefs.getBoolean("haptics", true)
        set(v) = prefs.edit().putBoolean("haptics", v).apply()

    var showKeyboard: Boolean
        get() = prefs.getBoolean("show_keyboard", false)
        set(v) = prefs.edit().putBoolean("show_keyboard", v).apply()

    var showSide: Boolean
        get() = prefs.getBoolean("show_side", true)
        set(v) = prefs.edit().putBoolean("show_side", v).apply()

    /** 画面の向き: [ORIENTATION_AUTO] / [ORIENTATION_LANDSCAPE] / [ORIENTATION_PORTRAIT] */
    var orientation: Int
        get() = prefs.getInt("orientation", ORIENTATION_AUTO)
        set(v) = prefs.edit().putInt("orientation", v).apply()

    /**
     * ホストドライブ (作業フォルダの HOSTDRV/ を DOS から使う)。設定の正はここで、np2.cfg には保存しない。
     * 変えたら [applyHostDrv] でエミュレータに渡す
     */
    var hostDrv: Boolean
        get() = prefs.getBoolean("hostdrv", false)
        set(v) = prefs.edit().putBoolean("hostdrv", v).apply()

    var hostDrvWrite: Boolean
        get() = prefs.getBoolean("hostdrv_write", false)
        set(v) = prefs.edit().putBoolean("hostdrv_write", v).apply()

    /**
     * ホストドライブの設定をエミュレータに渡す。起動時はエミュレータスレッドが動き出す前 (onCreate) に呼ぶ。
     * 削除の許可は渡さない: xnp2 の hostdrv は削除・名前の変更・フォルダの作成に対応していない
     * (NP21/W 版の hostdrv に置き換えたら、削除の許可を設定に戻す)
     */
    fun applyHostDrv() = Np2Native.setHostDrv(hostDrv, hostDrvWrite, false)

    /** 「ファイル管理」の注意書きを表示済みか */
    var fileManagerNoticeShown: Boolean
        get() = prefs.getBoolean("file_manager_notice_shown", false)
        set(v) = prefs.edit().putBoolean("file_manager_notice_shown", v).apply()

    companion object {
        const val ORIENTATION_AUTO = 0
        const val ORIENTATION_LANDSCAPE = 1
        const val ORIENTATION_PORTRAIT = 2
    }
}

/** 設定ダイアログ。値は変更した瞬間に [onChanged] で反映される */
object SettingsDialog {

    /** keymap ファイル (.key) の読み込み・解除 (SAF を使うので Activity が担当) */
    interface KeymapHost {
        fun keymapStatus(): String
        fun importKeymap()
        fun clearKeymap()
    }

    fun show(context: Context, settings: Settings, keymap: KeymapHost, onChanged: () -> Unit) {
        val dp = context.resources.displayMetrics.density
        val pad = (20 * dp).toInt()
        val root = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad / 2, pad, 0)
        }

        fun slider(title: String, min: Float, max: Float, step: Float, value: Float,
                   format: (Float) -> String, set: (Float) -> Unit) {
            val label = TextView(context).apply {
                setTextAppearance(android.R.style.TextAppearance_DeviceDefault_Medium)
            }
            val steps = ((max - min) / step).toInt()
            fun update(v: Float) { label.text = "$title: ${format(v)}" }
            update(value)
            root.addView(label)
            root.addView(SeekBar(context).apply {
                this.max = steps
                progress = ((value - min) / step).toInt().coerceIn(0, steps)
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(s: SeekBar, p: Int, fromUser: Boolean) {
                        val v = min + p * step
                        update(v)
                        if (fromUser) { set(v); onChanged() }
                    }
                    override fun onStartTrackingTouch(s: SeekBar) {}
                    override fun onStopTrackingTouch(s: SeekBar) {}
                })
            }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
                .apply { bottomMargin = (12 * dp).toInt() })
        }

        fun switch(title: String, value: Boolean, set: (Boolean) -> Unit) {
            root.addView(Switch(context).apply {
                text = title
                isChecked = value
                gravity = Gravity.CENTER_VERTICAL
                setOnCheckedChangeListener { _, b -> set(b); onChanged() }
            })
        }

        root.addView(TextView(context).apply {
            setTextAppearance(android.R.style.TextAppearance_DeviceDefault_Medium)
            text = "画面の向き"
        })
        root.addView(RadioGroup(context).apply {
            orientation = RadioGroup.HORIZONTAL
            val labels = listOf(
                Settings.ORIENTATION_AUTO to "自動",
                Settings.ORIENTATION_LANDSCAPE to "横",
                Settings.ORIENTATION_PORTRAIT to "縦",
            )
            for ((value, label) in labels) {
                addView(RadioButton(context).apply {
                    id = View.generateViewId()
                    text = label
                    isChecked = settings.orientation == value
                    setOnClickListener { settings.orientation = value; onChanged() }
                })
            }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            .apply { bottomMargin = (12 * dp).toInt() })

        slider("マウスの速さ", 0.25f, 3.0f, 0.25f, settings.mouseSpeed,
            { "×%.2f".format(it) }) { settings.mouseSpeed = it }
        slider("フルキーボードの高さ (横画面)", 0.3f, 0.7f, 0.05f, settings.keyboardHeight,
            { "${(it * 100).toInt()}%" }) { settings.keyboardHeight = it }
        slider("ソフトキーの不透明度", 0.2f, 1.0f, 0.1f, settings.keyOpacity,
            { "${(it * 100).toInt()}%" }) { settings.keyOpacity = it }
        switch("右の余白にキーを表示 (横画面)", settings.showSide) { settings.showSide = it }
        switch("キーを押したときに振動", settings.haptics) { settings.haptics = it }

        // ---- ホストドライブ ----
        root.addView(TextView(context).apply {
            setTextAppearance(android.R.style.TextAppearance_DeviceDefault_Medium)
            text = context.getString(R.string.hostdrv_title)
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            .apply { topMargin = (16 * dp).toInt() })
        root.addView(TextView(context).apply {
            setTextAppearance(android.R.style.TextAppearance_DeviceDefault_Small)
            text = context.getString(R.string.hostdrv_note)
        })
        val hdWrite = Switch(context).apply {
            text = context.getString(R.string.hostdrv_write)
            isChecked = settings.hostDrvWrite
            isEnabled = settings.hostDrv
            gravity = Gravity.CENTER_VERTICAL
            setOnCheckedChangeListener { _, b -> settings.hostDrvWrite = b; settings.applyHostDrv() }
        }
        root.addView(Switch(context).apply {
            text = context.getString(R.string.hostdrv_enable)
            isChecked = settings.hostDrv
            gravity = Gravity.CENTER_VERTICAL
            setOnCheckedChangeListener { _, b ->
                settings.hostDrv = b
                settings.applyHostDrv()
                hdWrite.isEnabled = b
            }
        })
        root.addView(hdWrite)

        // ---- 物理キーの割り当て (旧版の keymap ファイル) ----
        val status = TextView(context)
        fun updateStatus() { status.text = "物理キーの割り当て: ${keymap.keymapStatus()}" }
        updateStatus()
        root.addView(status, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            .apply { topMargin = (16 * dp).toInt() })

        lateinit var dialog: AlertDialog
        root.addView(LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(android.widget.Button(context).apply {
                text = "keymap ファイルを読み込む…"
                isAllCaps = false
                setOnClickListener { dialog.dismiss(); keymap.importKeymap() }
            })
            addView(android.widget.Button(context).apply {
                text = "標準に戻す"
                isAllCaps = false
                setOnClickListener { keymap.clearKeymap(); updateStatus() }
            })
        })

        dialog = AlertDialog.Builder(context, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle("設定")
            .setView(android.widget.ScrollView(context).apply { addView(root) })
            .setPositiveButton("閉じる", null)
            .show()
    }
}
