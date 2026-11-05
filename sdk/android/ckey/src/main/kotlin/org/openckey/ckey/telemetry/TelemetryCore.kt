package org.openckey.ckey.telemetry

import org.openckey.ckey.CKeyNative

/** A batch taken from the core: its id and JSON body. */
internal class TelemetryBatch(val id: Long, val json: String)

/** The telemetry core's C ABI (include/ckey/telemetry.h), behind an interface so pure-JVM tests can fake it. */
internal interface TelemetryCore {
    val available: Boolean
    fun setMode(mode: Int): Int
    fun getMode(): Int
    fun setClient(sdk: String, version: String, platform: String): Int
    fun sessionBegin(nowMs: Long)
    fun frame(nowMs: Long, processingMs: Int)
    fun sessionSuccess(nowMs: Long, profile: Int): Int
    fun sessionEnd(nowMs: Long)
    fun shouldSend(nowMs: Long): Boolean
    fun takeBatch(utcDay: String): TelemetryBatch?
    fun batchResult(id: Long, httpStatus: Int, nowMs: Long): Int
    fun snapshot(): LongArray?
    fun exportState(): String?
    fun importState(json: String): Int
    val endpoint: String
}

/**
 * The JNI-backed core. When the native library was built without the
 * telemetry module every recording call is a no-op and the mode reads 0.
 */
internal object JniTelemetryCore : TelemetryCore {
    override val available: Boolean by lazy { CKeyNative.has(CKeyNative.FEATURE_TELEMETRY) }

    override fun setMode(mode: Int): Int = if (available) CKeyNative.nativeTelemetrySetMode(mode) else 0
    override fun getMode(): Int = if (available) CKeyNative.nativeTelemetryGetMode() else 0
    override fun setClient(sdk: String, version: String, platform: String): Int =
        if (available) CKeyNative.nativeTelemetrySetClient(sdk, version, platform) else 0
    override fun sessionBegin(nowMs: Long) { if (available) CKeyNative.nativeTelemetrySessionBegin(nowMs) }
    override fun frame(nowMs: Long, processingMs: Int) { if (available) CKeyNative.nativeTelemetryFrame(nowMs, processingMs) }
    override fun sessionSuccess(nowMs: Long, profile: Int): Int =
        if (available) CKeyNative.nativeTelemetrySessionSuccess(nowMs, profile) else 0
    override fun sessionEnd(nowMs: Long) { if (available) CKeyNative.nativeTelemetrySessionEnd(nowMs) }
    override fun shouldSend(nowMs: Long): Boolean = available && CKeyNative.nativeTelemetryShouldSend(nowMs)
    override fun takeBatch(utcDay: String): TelemetryBatch? {
        if (!available) return null
        val id = LongArray(1)
        val json = CKeyNative.nativeTelemetryTakeBatch(utcDay, id) ?: return null
        return TelemetryBatch(id[0], json)
    }
    override fun batchResult(id: Long, httpStatus: Int, nowMs: Long): Int =
        if (available) CKeyNative.nativeTelemetryBatchResult(id, httpStatus, nowMs) else 0
    override fun snapshot(): LongArray? {
        if (!available) return null
        val out = LongArray(CKeyNative.SNAPSHOT_LONGS)
        return if (CKeyNative.nativeTelemetrySnapshot(out) == 0) out else null
    }
    override fun exportState(): String? = if (available) CKeyNative.nativeTelemetryExportState() else null
    override fun importState(json: String): Int = if (available) CKeyNative.nativeTelemetryImportState(json) else 1
    override val endpoint: String by lazy {
        if (available) CKeyNative.nativeTelemetryEndpoint() else "https://openckey.org/api/telemetry/v1/batch"
    }
}
