package org.openckey.ckey

import android.media.Image
import java.nio.ByteBuffer
import org.openckey.ckey.telemetry.Telemetry

/**
 * A continuous scan session over camera frames, with aggregate telemetry
 * (docs/adoption-and-telemetry.md): [start] begins a session, each processed
 * frame is counted with its processing time, the first decode succeeds the
 * session, and [stop] without a decode fails it (as does 30 s without one).
 * Recording is a few counter updates; sending happens on a background
 * thread, never on the scan thread.
 *
 * Use one instance per camera stream and feed it from one thread. After a
 * decode, frames are ignored until [scanAgain]. Use [CKeyDecoder] directly
 * for one-off decodes with no telemetry.
 */
class CKeyScanner @JvmOverloads constructor(
    private val maxCandidates: Int = CKeyDecoder.DEFAULT_MAX_CANDIDATES,
) {
    @Volatile private var active = false
    @Volatile private var decoded = false

    /** True after a decode until [scanAgain]. */
    val hasResult: Boolean get() = decoded

    /** The host started scanning (e.g. the camera view appeared). */
    fun start() {
        active = true
        decoded = false
        Telemetry.sessionBegin()
    }

    /** The host stopped scanning (camera unbound, view gone). */
    fun stop() {
        if (!active) return
        active = false
        Telemetry.sessionEnd()
    }

    /** Starts a new session after a decode. */
    fun scanAgain() = start()

    fun process(gray: ByteArray, width: Int, height: Int, stride: Int = width, offset: Int = 0): CKeyResult? =
        track { CKeyDecoder.decode(gray, width, height, stride, offset, maxCandidates) }

    fun process(gray: ByteBuffer, width: Int, height: Int, stride: Int, offset: Int = 0): CKeyResult? =
        track { CKeyDecoder.decode(gray, width, height, stride, offset, maxCandidates) }

    fun process(image: Image): CKeyResult? = track { CKeyDecoder.decode(image, maxCandidates) }

    /** Luma plane with any pixel stride (CameraX ImageProxy). */
    fun processLuma(buffer: ByteBuffer, rowStride: Int, pixelStride: Int, width: Int, height: Int): CKeyResult? =
        track { CKeyDecoder.decodeLuma(buffer, rowStride, pixelStride, width, height, maxCandidates) }

    private inline fun track(decode: () -> CKeyResult?): CKeyResult? {
        if (decoded) return null
        if (!active) start()
        val started = System.nanoTime()
        val result = decode()
        val elapsed = ((System.nanoTime() - started) / 1_000_000L).coerceIn(0L, Int.MAX_VALUE.toLong()).toInt()
        Telemetry.frame(elapsed)
        if (result != null) {
            decoded = true
            Telemetry.sessionSuccess(result.profile.code)
        }
        return result
    }
}
