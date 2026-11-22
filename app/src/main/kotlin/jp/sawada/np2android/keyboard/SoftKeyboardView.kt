package jp.sawada.np2android.keyboard

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.SparseArray
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * マルチタッチ対応のソフトキーボード。
 *
 * android.inputmethodservice.KeyboardView (API 29 で非推奨、2 本目の指で 1 本目が離される) の代わり。
 * 指ごと (pointerId) に押したキーを記録し、その指が離れたときにだけそのキーを離す。
 * 押した後に指がずれても、押したキーは変わらない (ゲーム操作で誤って別キーに移らないように)。
 *
 * ANDROID-KBD: クイックセーブ・ロード (121・122) は、[LONG_PRESS_MS] 押し続けたときだけ、その場で 1 回実行する
 * (誤って触れて今の状態を上書き・巻き戻ししないように)。それより早く離すか、指がキーの外へずれたら取り消す。
 * 実行したら振動で知らせ、離すまで茶色で表示する。物理キーボードの keymap の割り当ては Np2Activity が扱い、長押しにしない。
 */
@SuppressLint("ViewConstructor")
class SoftKeyboardView(
    context: Context,
    private val pages: List<KeyLayout>,
    private val controller: KeyboardController,
) : View(context) {

    private class KeyRect(val key: KeyLayout.Key, val rect: RectF)

    private var page = 0
    private var keyRects: List<KeyRect> = emptyList()
    private val pointers = SparseArray<KeyRect>()
    /** 長押しのキー: 指ごとの待ちの処理と、実行し終えたキー */
    private val longTasks = SparseArray<Runnable>()
    private val longFired = HashSet<KeyRect>()

    private val density = resources.displayMetrics.density
    private val inset = 2f * density
    private val corner = 6f * density

    private val bgPaint = Paint().apply { color = Color.argb(0x40, 0, 0, 0) }
    private val keyPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(0x70, 0x30, 0x38, 0x48) }
    private val pressedPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(0xC0, 0x4A, 0x90, 0xE2) }
    private val lockedPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(0xA0, 0xE2, 0xA0, 0x4A) }
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1f * density
        color = Color.argb(0x90, 0xFF, 0xFF, 0xFF)
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
    }

    /** キーを押したときに振動するか */
    var haptics = true

    init {
        isHapticFeedbackEnabled = true
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        layoutKeys()
    }

    private fun layoutKeys() {
        val layout = pages[page]
        val total = layout.rows.sumOf { it.weight.toDouble() }.toFloat().takeIf { it > 0 } ?: return
        val unit = height / total
        var y = 0f
        val list = ArrayList<KeyRect>()
        for (row in layout.rows) {
            val h = row.weight * unit
            for (k in row.keys) {
                val l = k.x * width / 100f
                val r = (k.x + k.width) * width / 100f
                list.add(KeyRect(k, RectF(l + inset, y + inset, r - inset, y + h - inset)))
            }
            y += h
        }
        keyRects = list
        invalidate()
    }

    private fun hit(x: Float, y: Float): KeyRect? {
        // 隙間 (inset) をタッチしても隣のキーとして扱えるよう、少し広げて判定する
        return keyRects.firstOrNull {
            x >= it.rect.left - inset && x < it.rect.right + inset &&
                y >= it.rect.top - inset && y < it.rect.bottom + inset
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val kr = hit(e.getX(i), e.getY(i)) ?: return true
                val id = e.getPointerId(i)
                pointers.put(id, kr)
                when {
                    isLongPressKey(kr.key.code) -> startLongPress(id, kr)
                    kr.key.code != SoftKeyCodes.PAGE -> controller.press(kr.key.code)
                }
                if (haptics) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                invalidate()
            }
            MotionEvent.ACTION_MOVE -> {
                // 長押しを待っているキーから指が外れたら取り消す
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val kr = pointers.get(id) ?: continue
                    if (longTasks.get(id) == null) continue
                    if (!contains(kr, e.getX(i), e.getY(i))) {
                        cancelLongPress(id)
                        pointers.remove(id)
                        invalidate()
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                val kr = pointers.get(id) ?: return true
                pointers.remove(id)
                cancelLongPress(id)
                longFired.remove(kr)
                releaseKey(kr)
            }
            MotionEvent.ACTION_CANCEL -> releaseAll()
        }
        // キーの無い場所も含めて消費し、下の画面 (マウス操作) へ漏らさない
        return true
    }

    private fun contains(kr: KeyRect, x: Float, y: Float) =
        x >= kr.rect.left - inset && x < kr.rect.right + inset &&
            y >= kr.rect.top - inset && y < kr.rect.bottom + inset

    private fun isLongPressKey(code: Int) =
        code == SoftKeyCodes.STATE_SAVE || code == SoftKeyCodes.STATE_LOAD

    private fun startLongPress(id: Int, kr: KeyRect) {
        cancelLongPress(id)
        val task = Runnable {
            longTasks.remove(id)
            if (pointers.get(id) !== kr) return@Runnable
            longFired.add(kr)
            if (haptics) performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            invalidate()
            controller.longPress(kr.key.code)
        }
        longTasks.put(id, task)
        postDelayed(task, LONG_PRESS_MS)
    }

    private fun cancelLongPress(id: Int) {
        longTasks.get(id)?.let { removeCallbacks(it) }
        longTasks.remove(id)
    }

    private fun releaseKey(kr: KeyRect) {
        if (isLongPressKey(kr.key.code)) {
            // 長押しのキーは押したときも離したときもエミュレータへは何も送らない
        } else if (kr.key.code == SoftKeyCodes.PAGE) {
            if (pages.size > 1) {
                releaseAll()
                page = (page + 1) % pages.size
                layoutKeys()
            }
        } else {
            controller.release(kr.key.code)
        }
        invalidate()
    }

    /** このビューで押されているキーをすべて離す */
    fun releaseAll() {
        for (i in 0 until pointers.size()) {
            val kr = pointers.valueAt(i)
            if (kr.key.code != SoftKeyCodes.PAGE && !isLongPressKey(kr.key.code)) controller.release(kr.key.code)
        }
        for (i in 0 until longTasks.size()) removeCallbacks(longTasks.valueAt(i))
        longTasks.clear()
        longFired.clear()
        pointers.clear()
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)
        for (kr in keyRects) {
            val code = kr.key.code
            // ANDROID-KBD: 青 (押している) はこのパネルで指が触れているキーだけ。CAPS・カナを押すと全パネルが
            // 描き直されるので、パネルをまたいだ押下を見ると、反対側のパネルの同じキーが青のまま残っていた
            val paint = when {
                kr in longFired -> lockedPaint
                isHeldHere(kr) -> pressedPaint
                controller.isLocked(code) -> lockedPaint
                else -> keyPaint
            }
            canvas.drawRoundRect(kr.rect, corner, corner, paint)
            canvas.drawRoundRect(kr.rect, corner, corner, strokePaint)
            drawLabel(canvas, kr)
        }
    }

    private fun isHeldHere(kr: KeyRect): Boolean {
        for (i in 0 until pointers.size()) if (pointers.valueAt(i) === kr) return true
        return false
    }

    private fun drawLabel(canvas: Canvas, kr: KeyRect) {
        val label = kr.key.label
        if (label.isEmpty()) return
        val r = kr.rect
        var size = minOf(r.height() * 0.45f, 22f * density)
        textPaint.textSize = size
        val w = textPaint.measureText(label)
        if (w > r.width() * 0.9f) {
            size *= r.width() * 0.9f / w
            textPaint.textSize = size
        }
        val baseline = r.centerY() - (textPaint.descent() + textPaint.ascent()) / 2f
        canvas.drawText(label, r.centerX(), baseline, textPaint)
    }

    companion object {
        /** クイックセーブ・ロードを実行するまで押し続ける時間 */
        const val LONG_PRESS_MS = 500L
    }
}
