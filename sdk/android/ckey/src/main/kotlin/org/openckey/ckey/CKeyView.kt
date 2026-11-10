package org.openckey.ckey

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.util.AttributeSet
import android.view.View
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

/**
 * A classic View that shows a CKey for [data]. It is square (the smaller
 * measured side) and renders native pixels at its exact pixel size on a
 * background thread; no SVG, file or WebView is involved.
 *
 * Set [data], and optionally [style], [level], [layout], [policy] and
 * [transparent]. A payload that does not fit or a refused style leaves the
 * view empty and reports through [onError].
 */
class CKeyView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {
    var data: String? = null
        set(value) { field = value; rerender() }
    var style: CKeyStyle? = null
        set(value) { field = value; rerender() }
    var level: EccLevel = EccLevel.M
        set(value) { field = value; rerender() }
    var layout: Layout = Layout.STANDARD
        set(value) { field = value; rerender() }
    var policy: RasterPolicy = RasterPolicy.COVERAGE
        set(value) { field = value; rerender() }
    var transparent: Boolean = false
        set(value) { field = value; rerender() }
    /** Called on the main thread when encoding or rendering fails. */
    var onError: ((Throwable) -> Unit)? = null

    private var bitmap: Bitmap? = null
    private val generation = AtomicInteger()
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val dst = Rect()

    init {
        CKey.install(context)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val fallback = (192 * resources.displayMetrics.density).toInt()
        val w = resolveSize(fallback, widthMeasureSpec)
        val h = resolveSize(fallback, heightMeasureSpec)
        val side = minOf(w, h)
        setMeasuredDimension(side, side)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        rerender()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val b = bitmap ?: return
        val side = minOf(width - paddingLeft - paddingRight, height - paddingTop - paddingBottom)
        if (side <= 0) return
        dst.set(paddingLeft, paddingTop, paddingLeft + side, paddingTop + side)
        canvas.drawBitmap(b, null, dst, paint)
    }

    private fun rerender() {
        val ticket = generation.incrementAndGet()
        val text = data
        val side = minOf(width - paddingLeft - paddingRight, height - paddingTop - paddingBottom)
        if (text == null || side < 16) {
            bitmap = null
            invalidate()
            return
        }
        val style = style; val level = level; val layout = layout; val policy = policy; val transparent = transparent
        renderer.execute {
            val outcome = runCatching {
                CKeySymbol.encode(text, 0, level, layout).toBitmap(side.coerceAtMost(4096), policy, transparent, style)
            }
            post {
                if (generation.get() != ticket) return@post
                outcome.onSuccess { bitmap = it }.onFailure { bitmap = null; onError?.invoke(it) }
                invalidate()
            }
        }
    }

    private companion object {
        val renderer = Executors.newSingleThreadExecutor { r -> Thread(r, "ckey-render").apply { isDaemon = true } }
    }
}
