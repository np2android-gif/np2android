package jp.sawada.np2android

import android.app.AlertDialog
import android.content.Intent
import android.content.pm.ActivityInfo
import android.media.AudioManager
import android.util.SparseIntArray
import jp.sawada.np2android.keyboard.CustomKeymap
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.util.Log
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.RoundedCorner
import android.view.SurfaceHolder
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RelativeLayout
import android.widget.TextView
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import jp.sawada.np2android.keyboard.KeyLayout
import jp.sawada.np2android.keyboard.KeyMapper
import jp.sawada.np2android.keyboard.KeyboardController
import jp.sawada.np2android.keyboard.SoftKeyboardView
import org.libsdl.app.SDLActivity
import java.io.File
import kotlin.concurrent.thread

/**
 * Neko Project II のメイン Activity。
 *
 * SDL2 の [SDLActivity]（ベンダーコード、Java のまま）を継承し、
 * アプリ固有の処理 (メニュー・ソフトキー・物理キー・ディスク取り込み) をここで行う。
 */
class Np2Activity : SDLActivity(), KeyboardController.Actions, Np2Menu.Host, SettingsDialog.KeymapHost {

    private var backCallback: OnBackInvokedCallback? = null
    private var lastBackTime = 0L

    private val settings by lazy { Settings(this) }
    private val menu by lazy { Np2Menu(this, this) }

    private lateinit var controller: KeyboardController
    private var leftPanel: SoftKeyboardView? = null
    private var sidePanel: SoftKeyboardView? = null
    private var mainKeyboard: SoftKeyboardView? = null
    private var portraitKeyboard: SoftKeyboardView? = null
    private var overlaySize = 0 to 0
    private val allPanels get() = listOfNotNull(leftPanel, sidePanel, mainKeyboard, portraitKeyboard)

    /** SAF の結果を受け取るまでの挿入先 */
    private var pendingImport: Np2Menu.DiskTarget? = null

    /** libSDL2.so → libmain.so の順にロードする。 */
    override fun getLibraries(): Array<String> = arrayOf("SDL2", "main")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (mBrokenLibraries) return

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            // 予測型「戻る」(targetSdk 36+ で既定) では onBackPressed が呼ばれないため、こちらで受ける
            val cb = OnBackInvokedCallback { onBackAction() }
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, cb
            )
            backCallback = cb
        }
        Np2Native.listener = menu.listener
        KeyMapper.custom = CustomKeymap.load(this)
        mLayout?.let { setupOverlay(it) }
        if (BuildConfig.DEBUG) registerDebugKeyReceiver()
        applySettings()
    }

    override fun onDestroy() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            backCallback?.let { onBackInvokedDispatcher.unregisterOnBackInvokedCallback(it) }
        }
        Np2Native.listener = null
        val finishing = isFinishing
        super.onDestroy()
        // np2 のコアは C の静的変数で状態を持ち、同じプロセスでの再起動を想定していない。
        // ユーザーがメニューから終了したときだけプロセスごと終わらせ、次回はまっさらな状態から起動する。
        // (FLAG_ACTIVITY_CLEAR_TASK による作り直しなど、別インスタンスが続く場合は終わらせない)
        if (finishing && Np2Native.userExited) System.exit(0)
    }

    override fun onPause() {
        // 押しっぱなしのまま裏に回らないよう、ソフトキーをすべて離す
        if (::controller.isInitialized) {
            allPanels.forEach { it.releaseAll() }
            controller.releaseAll()
        }
        super.onPause()
    }

    // ---- メニュー・設定 -----------------------------------------------------

    override fun openMenu() {
        // np2 内蔵メニューが開いていたら、まずそれを閉じる
        if (Np2Native.isMenuOpen()) Np2Native.toggleMenu() else menu.show()
    }

    override fun openSettings() = SettingsDialog.show(this, settings, this) { applySettings() }

    private fun applySettings() {
        Np2Native.setMouseSpeed(settings.mouseSpeed)
        for (v in allPanels) {
            v.alpha = settings.keyOpacity
            v.haptics = settings.haptics
        }
        applyOrientation()
        placePanels(overlaySize.first, overlaySize.second)
    }

    // ---- 画面の向き ---------------------------------------------------------

    /**
     * SDL はウィンドウ (640x400) が横長なので横向きに固定しようとする。設定に従った向きに置き換える。
     * (SDL の native スレッドから呼ばれることがある)
     */
    override fun setOrientationBis(w: Int, h: Int, resizable: Boolean, hint: String?) {
        runOnUiThread { applyOrientation() }
    }

    private fun applyOrientation() {
        // 縦画面でも描画面は横長 (画面上部) になる。SDLSurface は要求が PORTRAIT / SENSOR_PORTRAIT のとき
        // 横長の描画面を「準備できていない」として描画を止めるため、USER_PORTRAIT / FULL_USER を使う。
        val req = when (settings.orientation) {
            Settings.ORIENTATION_LANDSCAPE -> ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            Settings.ORIENTATION_PORTRAIT -> ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT
            else -> ActivityInfo.SCREEN_ORIENTATION_FULL_USER
        }
        if (requestedOrientation != req) requestedOrientation = req
    }

    // ---- ディスクの取り込み (SAF) -------------------------------------------

    override fun importDisk(target: Np2Menu.DiskTarget) {
        pendingImport = target
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*")
        @Suppress("DEPRECATION")
        startActivityForResult(intent, REQUEST_IMPORT)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_KEYMAP) {
            data?.data?.takeIf { resultCode == RESULT_OK }?.let { onKeymapPicked(it) }
            return
        }
        if (requestCode != REQUEST_IMPORT) return
        val target = pendingImport ?: return
        pendingImport = null
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return

        val name = DiskImages.sanitize(DiskImages.displayName(this, uri) ?: uri.lastPathSegment ?: "disk")
        if (!DiskImages.matches(target.kind, name)) {
            dialog()
                .setTitle("対応していない形式です")
                .setMessage("$name\n\n${target.label} に使える拡張子:\n" +
                        target.kind.extensions.sorted().joinToString(" "))
                .setPositiveButton("OK", null)
                .show()
            return
        }
        val dest = File(DiskImages.diskDir(this), name)
        if (dest.exists()) {
            dialog()
                .setTitle("同じ名前のファイルがあります")
                .setMessage("$name はすでに取り込まれています。\n" +
                        "上書きすると、取り込み済みのイメージに保存された変更は失われます。")
                .setPositiveButton("上書きする") { _, _ -> copyAndInsert(uri, dest, target) }
                .setNeutralButton("既存のものを使う") { _, _ -> menu.insert(target, dest) }
                .setNegativeButton("キャンセル", null)
                .show()
        } else {
            copyAndInsert(uri, dest, target)
        }
    }

    private fun copyAndInsert(uri: Uri, dest: File, target: Np2Menu.DiskTarget) {
        val dp = resources.displayMetrics.density
        val text = TextView(this).apply { text = "${dest.name}\nコピーしています…" }
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            val pad = (20 * dp).toInt()
            setPadding(pad, pad, pad, pad)
            addView(ProgressBar(this@Np2Activity))
            addView(text, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                .apply { marginStart = (16 * dp).toInt() })
        }
        val progress = dialog().setView(box).setCancelable(false).show()

        thread(name = "disk-import") {
            var lastUpdate = 0L
            val error = runCatching {
                DiskImages.copy(this, uri, dest) { bytes ->
                    val now = SystemClock.uptimeMillis()
                    if (now - lastUpdate > 200) {
                        lastUpdate = now
                        runOnUiThread { text.text = "${dest.name}\nコピーしています… %.1f MB".format(bytes / 1048576.0) }
                    }
                }
            }.exceptionOrNull()
            runOnUiThread {
                progress.dismiss()
                if (error == null) {
                    menu.insert(target, dest)
                } else {
                    menu.toast("取り込みに失敗しました: ${error.message}")
                }
            }
        }
    }

    private fun dialog() = AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)

    // ---- ソフトキーボード ---------------------------------------------------

    private fun setupOverlay(root: ViewGroup) {
        controller = KeyboardController(this)
        val ov = FrameLayout(this)
        root.addView(ov, RelativeLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        leftPanel = SoftKeyboardView(this, listOf(KeyLayout.load(resources, R.xml.kbd_left)), controller)
        sidePanel = SoftKeyboardView(this, listOf(KeyLayout.load(resources, R.xml.kbd_side)), controller)
        mainKeyboard = SoftKeyboardView(this, listOf(
            KeyLayout.load(resources, R.xml.kbd_main),
            KeyLayout.load(resources, R.xml.kbd_ext),
        ), controller)
        portraitKeyboard = SoftKeyboardView(this, listOf(KeyLayout.load(resources, R.xml.kbd_portrait)), controller)
        ov.addView(leftPanel)
        ov.addView(sidePanel)
        ov.addView(mainKeyboard)
        ov.addView(portraitKeyboard)

        ov.addOnLayoutChangeListener { _, l, t, r, b, ol, ot, or_, ob ->
            if (r - l != or_ - ol || b - t != ob - ot) {
                overlaySize = (r - l) to (b - t)
                ov.post { placePanels(r - l, b - t) }
            }
        }
        // 切り欠き・角の情報が届いた/変わったら置き直す
        ov.setOnApplyWindowInsetsListener { v, insets ->
            v.post { placePanels(overlaySize.first, overlaySize.second) }
            insets
        }
        // SDL の surfaceChanged の後に呼ばれる (コールバックは登録順)。描画面が作り直されたら再描画させる
        mSurface?.holder?.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {}
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                Np2Native.requestRedraw()
            }
            override fun surfaceDestroyed(holder: SurfaceHolder) {}
        })
    }

    /** 画面の縦横に合わせて PC-98 画面とソフトキーを配置する */
    private fun placePanels(w: Int, h: Int) {
        if (w <= 0 || h <= 0) return
        if (BuildConfig.DEBUG) Log.d(TAG, "placePanels ${w}x$h safe=${safeArea()}")
        if (h > w) placePortrait(w, h) else placeLandscape(w, h)
    }

    /** 画面の切り欠き (カメラ穴) と角の丸みを避けるための余白 */
    private data class SafeArea(val left: Int, val top: Int, val right: Int, val bottom: Int)

    private fun safeArea(): SafeArea {
        val insets = window.decorView.rootWindowInsets ?: return SafeArea(0, 0, 0, 0)
        val cutout = insets.displayCutout
        fun radius(pos: Int): Int =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) insets.getRoundedCorner(pos)?.radius ?: 0 else 0
        val tl = radius(RoundedCorner.POSITION_TOP_LEFT)
        val tr = radius(RoundedCorner.POSITION_TOP_RIGHT)
        val bl = radius(RoundedCorner.POSITION_BOTTOM_LEFT)
        val br = radius(RoundedCorner.POSITION_BOTTOM_RIGHT)
        return SafeArea(
            left = maxOf(cutout?.safeInsetLeft ?: 0, (maxOf(tl, bl) * CORNER_FACTOR).toInt()),
            top = maxOf(cutout?.safeInsetTop ?: 0, maxOf(tl, tr)),
            right = maxOf(cutout?.safeInsetRight ?: 0, (maxOf(tr, br) * CORNER_FACTOR).toInt()),
            bottom = maxOf(cutout?.safeInsetBottom ?: 0, (maxOf(bl, br) * CORNER_FACTOR).toInt()),
        )
    }

    /**
     * 横画面: PC-98 画面 (640x400 をアスペクト維持で中央表示) の左右余白にパネルを置き、
     * フルキーボードは画面下部に重ねる。
     */
    private fun placeLandscape(w: Int, h: Int) {
        setSurfaceArea(0, ViewGroup.LayoutParams.MATCH_PARENT)
        val dp = resources.displayMetrics.density
        val screenW = minOf(w, h * 16 / 10)
        var margin = (w - screenW) / 2
        // 余白が狭い (正方形に近い画面) ときは画面に重ねる
        if (margin < (96 * dp).toInt()) margin = (120 * dp).toInt()

        // 角の丸み・切り欠きの分だけ内側に寄せる (縦方向は角の丸みの分)
        val safe = safeArea()
        val vInset = minOf(safe.bottom, (16 * dp).toInt())
        leftPanel?.layoutParams = FrameLayout.LayoutParams(margin - safe.left, h - vInset * 2,
            Gravity.START or Gravity.TOP).apply { leftMargin = safe.left; topMargin = vInset }
        sidePanel?.layoutParams = FrameLayout.LayoutParams(margin - safe.right, h - vInset * 2,
            Gravity.END or Gravity.TOP).apply { rightMargin = safe.right; topMargin = vInset }
        mainKeyboard?.layoutParams = FrameLayout.LayoutParams(
            screenW, (h * settings.keyboardHeight).toInt(), Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL)

        show(portraitKeyboard, false)
        show(leftPanel, true)
        show(sidePanel, settings.showSide)
        show(mainKeyboard, settings.showKeyboard)
    }

    /**
     * 縦画面: SDL の描画面を画面上部 (幅いっぱい × 16:10) に縮め、
     * 残りの領域に縦画面用キーボード (旧版のフルキーボード + 操作行) を置く。
     */
    private fun placePortrait(w: Int, h: Int) {
        // 上端の角の丸み・カメラ穴で PC-98 画面が欠けないよう、その分だけ下げる
        val safe = safeArea()
        val screenTop = safe.top
        val screenH = w * 10 / 16
        setSurfaceArea(screenTop, screenH)
        val kbTop = screenTop + screenH
        portraitKeyboard?.layoutParams = FrameLayout.LayoutParams(w, h - kbTop - safe.bottom, Gravity.TOP).apply {
            topMargin = kbTop
        }

        show(leftPanel, false)
        show(sidePanel, false)
        show(mainKeyboard, false)
        show(portraitKeyboard, true)
    }

    /** SDL の描画面の位置と高さを変える (SDL は surfaceChanged でウィンドウサイズを更新し、640x400 を合わせ直す) */
    private fun setSurfaceArea(top: Int, height: Int) {
        val surface = mSurface ?: return
        val current = surface.layoutParams as? RelativeLayout.LayoutParams
        if (current != null && current.width == ViewGroup.LayoutParams.MATCH_PARENT &&
            current.height == height && current.topMargin == top) return
        surface.layoutParams = RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, height).apply {
            addRule(RelativeLayout.ALIGN_PARENT_TOP)
            topMargin = top
        }
    }

    private fun show(view: SoftKeyboardView?, visible: Boolean) {
        view ?: return
        val v = if (visible) View.VISIBLE else View.GONE
        if (view.visibility == v) return
        if (!visible) view.releaseAll()
        view.visibility = v
    }

    override fun toggleKeyboard() {
        settings.showKeyboard = toggle(mainKeyboard)
    }

    override fun toggleSide() {
        settings.showSide = toggle(sidePanel)
    }

    /** 表示を切り替え、切り替え後に表示中なら true */
    private fun toggle(view: SoftKeyboardView?): Boolean {
        view ?: return false
        val show = view.visibility != View.VISIBLE
        if (!show) view.releaseAll()
        view.visibility = if (show) View.VISIBLE else View.GONE
        return show
    }

    override fun onLockStateChanged() {
        allPanels.forEach { it.invalidate() }
    }

    // ---- 物理キー -----------------------------------------------------------

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (mBrokenLibraries) return super.dispatchKeyEvent(event)
        val down = event.action == KeyEvent.ACTION_DOWN
        val up = event.action == KeyEvent.ACTION_UP
        val first = down && event.repeatCount == 0

        // keymap ファイルの割り当ては、戻るキー (ハードウェアキーの場合) を含めて最優先
        val custom = KeyMapper.mapCustom(event)

        // 戻るキーは SDL に渡さずメニューに使う (外部マウスの右ボタン由来のものは SDL 側の処理に任せる)
        if (custom == KeyMapper.NONE && event.keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.isFromSource(InputDevice.SOURCE_MOUSE)) return super.dispatchKeyEvent(event)
            if (up && !event.isCanceled) onBackAction()
            return true
        }

        when (val action = if (custom != KeyMapper.NONE) custom else KeyMapper.map(event)) {
            KeyMapper.NONE ->
                // 音量などシステムのキーは Android に任せる。それ以外 (未対応のキー) は
                // SDL の既定処理に渡すと別のキーとして解釈されうるので捨てる
                return if (event.isSystem) super.dispatchKeyEvent(event) else true
            KeyMapper.ACTION_IGNORE -> {}
            KeyMapper.ACTION_MENU -> if (up) openMenu()
            KeyMapper.ACTION_MOUSE_LEFT, KeyMapper.ACTION_MOUSE_RIGHT -> {
                val button = if (action == KeyMapper.ACTION_MOUSE_LEFT) Np2Native.MOUSE_LEFT else Np2Native.MOUSE_RIGHT
                if (first) Np2Native.mouseButton(button, true) else if (up) Np2Native.mouseButton(button, false)
            }
            KeyMapper.ACTION_STATE_SAVE -> if (up) Np2Native.saveState(0)
            KeyMapper.ACTION_STATE_LOAD -> if (up) Np2Native.loadState(0, false)
            KeyMapper.ACTION_VOLUME_DOWN, KeyMapper.ACTION_VOLUME_UP -> if (down) {
                val dir = if (action == KeyMapper.ACTION_VOLUME_UP) AudioManager.ADJUST_RAISE else AudioManager.ADJUST_LOWER
                (getSystemService(AUDIO_SERVICE) as AudioManager)
                    .adjustStreamVolume(AudioManager.STREAM_MUSIC, dir, AudioManager.FLAG_SHOW_UI)
            }
            else -> {
                // PC-98 キー。0x70 未満は Android のキーリピートをそのまま PC-98 のリピートとして送る
                if (down && (event.repeatCount == 0 || action < 0x70)) Np2Native.keyDown(action)
                else if (up) Np2Native.keyUp(action)
            }
        }
        return true
    }

    /**
     * デバッグ版のみ: scancode 付きの物理キー入力を adb から再現する (adb の input keyevent は scancode 0 になるため)。
     *   adb shell am broadcast -a jp.sawada.np2android.DEBUG_KEY -p jp.sawada.np2android --ei scancode 30 --ei keycode 29
     */
    private fun registerDebugKeyReceiver() {
        val receiver = object : android.content.BroadcastReceiver() {
            override fun onReceive(c: android.content.Context, i: Intent) {
                val scan = i.getIntExtra("scancode", 0)
                val code = i.getIntExtra("keycode", KeyEvent.KEYCODE_UNKNOWN)
                val t = SystemClock.uptimeMillis()
                for (action in intArrayOf(KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP)) {
                    dispatchKeyEvent(KeyEvent(t, t, action, code, 0, 0, -1, scan, 0, InputDevice.SOURCE_KEYBOARD))
                }
            }
        }
        val filter = android.content.IntentFilter("jp.sawada.np2android.DEBUG_KEY")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, filter, RECEIVER_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(receiver, filter)
        }
    }

    // ---- keymap ファイル (.key) ---------------------------------------------

    override fun keymapStatus(): String {
        val name = CustomKeymap.name(this) ?: return "未設定 (標準の割り当て)"
        return "$name (${KeyMapper.custom.size()} キー)"
    }

    override fun importKeymap() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*")
        @Suppress("DEPRECATION")
        startActivityForResult(intent, REQUEST_KEYMAP)
    }

    override fun clearKeymap() {
        CustomKeymap.clear(this)
        KeyMapper.custom = SparseIntArray()
        menu.toast("キー割り当てを標準に戻しました")
    }

    private fun onKeymapPicked(uri: Uri) {
        val name = DiskImages.displayName(this, uri) ?: "keymap"
        val text = runCatching {
            contentResolver.openInputStream(uri)?.use { it.readBytes().take(MAX_KEYMAP_BYTES).toByteArray() }
                ?.toString(Charsets.ISO_8859_1)   // 数字とカンマだけなので文字コードは問わない
        }.getOrNull()
        if (text == null) {
            menu.toast("読み込めませんでした: $name")
            return
        }
        val parsed = CustomKeymap.parse(text)
        if (parsed.entries.size() == 0) {
            menu.toast("$name に有効な割り当てがありません (書式: scancode,np2 コード)")
            return
        }
        CustomKeymap.save(this, name, parsed.entries)
        KeyMapper.custom = parsed.entries
        menu.toast("$name: ${parsed.entries.size()} キーを割り当てました" +
                if (parsed.skipped > 0) " (読み飛ばした行 ${parsed.skipped})" else "")
    }

    // ---- 戻る操作 -----------------------------------------------------------

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        onBackAction()
    }

    /** 戻る操作: メニューを開く (np2 内蔵メニューが開いていれば閉じる) */
    private fun onBackAction() {
        // 戻るキーと OnBackInvokedCallback の両方が届く環境で二重に処理しないようにする
        val now = SystemClock.uptimeMillis()
        if (now - lastBackTime < 300) return
        lastBackTime = now
        openMenu()
    }

    // ---- 全画面 -------------------------------------------------------------

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    /** ステータスバー/ナビゲーションバーを隠す（スワイプで一時表示）。 */
    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    or View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN)
        }
    }

    companion object {
        private const val TAG = "np2ui"
        private const val REQUEST_IMPORT = 1001
        private const val REQUEST_KEYMAP = 1002
        /** keymap ファイルの読み込み上限 (旧版の表は数 KB) */
        private const val MAX_KEYMAP_BYTES = 64 * 1024
        /**
         * 角の丸みのうち、横方向・下方向で避ける割合。半径 r の角で 45° の位置は端から r×(1-1/√2) ≒ 0.3r。
         * キーは角が丸いので、これだけ離せば欠けは目立たない。上端 (PC-98 画面) は半径いっぱい避ける。
         */
        private const val CORNER_FACTOR = 0.3f
    }
}
