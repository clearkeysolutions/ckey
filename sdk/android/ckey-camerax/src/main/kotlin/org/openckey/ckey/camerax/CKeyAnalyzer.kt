package org.openckey.ckey.camerax

import android.graphics.ImageFormat
import android.os.Handler
import android.os.Looper
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import java.util.concurrent.Executor
import org.openckey.ckey.CKeyResult
import org.openckey.ckey.CKeyScanner

/**
 * A CameraX analyzer that decodes CKeys from the luma (Y) plane of each
 * frame and drives a telemetry scan session (docs/adoption-and-telemetry.md):
 * the session begins with the first frame (or [start]), every analyzed
 * frame is counted with its processing time, the first decode succeeds it,
 * and [stop] without a decode fails it.
 *
 * ```
 * val analyzer = CKeyAnalyzer { result -> showResult(result) }
 * val analysis = ImageAnalysis.Builder()
 *     .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
 *     .setResolutionSelector(...)        // CKeys need ~6-11 camera px per symbol unit
 *     .build().apply { setAnalyzer(cameraExecutor, analyzer) }
 * // when the camera is unbound / the screen goes away:
 * analyzer.stop()
 * ```
 *
 * After a decode, frames are skipped until [scanAgain]. [onResult] runs on
 * [callbackExecutor] (the main thread by default), never as an automatic
 * action: show the payload and let the user choose (CKeyResult.action).
 * Use the default YUV_420_888 output format. Call `CKey.install(context)`
 * once (e.g. in Application.onCreate) so telemetry can also be flushed when
 * the app goes to the background.
 */
class CKeyAnalyzer @JvmOverloads constructor(
    private val callbackExecutor: Executor = MainThreadExecutor,
    /** Minimum milliseconds between analyzed frames (50 = at most 20 fps, as the iOS reader). */
    private val minFrameIntervalMs: Long = 50,
    private val onResult: (CKeyResult) -> Unit,
) : ImageAnalysis.Analyzer {

    private val scanner = CKeyScanner()
    private var lastFrameNs = 0L

    /** Begins a scan session (optional: the first frame begins one). */
    fun start() = scanner.start()

    /** Ends the scan session; call when the camera is unbound or the scanner UI goes away. */
    fun stop() = scanner.stop()

    /** Starts a new session after a decode. */
    fun scanAgain() = scanner.scanAgain()

    override fun analyze(image: ImageProxy) {
        try {
            if (scanner.hasResult) return
            val now = System.nanoTime()
            if (lastFrameNs != 0L && now - lastFrameNs < minFrameIntervalMs * 1_000_000L) return
            lastFrameNs = now
            if (image.format != ImageFormat.YUV_420_888 && image.format != ImageFormat.YUV_422_888 &&
                image.format != ImageFormat.YUV_444_888) return
            val y = image.planes[0]
            val result = scanner.processLuma(y.buffer, y.rowStride, y.pixelStride, image.width, image.height)
            if (result != null) callbackExecutor.execute { onResult(result) }
        } finally {
            image.close()
        }
    }

    private object MainThreadExecutor : Executor {
        private val handler = Handler(Looper.getMainLooper())
        override fun execute(command: Runnable) {
            handler.post(command)
        }
    }
}
