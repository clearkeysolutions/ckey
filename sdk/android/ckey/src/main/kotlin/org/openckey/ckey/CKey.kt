package org.openckey.ckey

import android.app.Application
import android.content.Context
import org.openckey.ckey.telemetry.BackgroundWatcher
import org.openckey.ckey.telemetry.Telemetry

/**
 * Process-wide CKey settings.
 *
 * Telemetry (docs/adoption-and-telemetry.md): aggregate, anonymous scan
 * counters only — never payloads, URLs, identifiers or location. Opt out in
 * one line: `CKey.telemetry = 0`.
 */
object CKey {
    /** No counting, nothing sent. */
    const val TELEMETRY_OFF = 0
    /** Local counting and hourly aggregate batches (the default). */
    const val TELEMETRY_ON = 1
    /** Local counting, never sent. */
    const val TELEMETRY_LOCAL = 2

    /**
     * Telemetry mode: 0 off, 1 on (default), 2 local only. Any other value
     * throws [IllegalArgumentException]; the mode is never silently reset.
     * Switching to 0 purges all counters; 1 -> 2 drops the unsent delta and
     * any batch in flight. Reads 0 if the native library was built without
     * the telemetry module.
     */
    @JvmStatic
    var telemetry: Int
        get() = Telemetry.getMode()
        set(value) = Telemetry.setMode(value)

    /** Whether the native library contains the telemetry module. */
    @JvmStatic val telemetryAvailable: Boolean get() = Telemetry.available

    /** Local totals (modes 1 and 2; all zero in mode 0), or null without the telemetry module. */
    @JvmStatic
    fun telemetryCounters(): CKeyTelemetryCounters? = Telemetry.snapshot()?.let(CKeyTelemetryCounters::fromArray)

    /** Serialized counters for optional persistence across launches (host option; off unless you store it). */
    @JvmStatic fun exportTelemetryState(): String? = Telemetry.exportState()

    /** Adds stored counters back once at startup; false if refused. Never changes the mode. */
    @JvmStatic fun importTelemetryState(json: String): Boolean = Telemetry.importState(json) == 0

    /**
     * Optional: lets the SDK try a telemetry send when the app moves to the
     * background (an Application.ActivityLifecycleCallbacks, no androidx
     * dependency). CKeyView and the Compose and CameraX adapters call it.
     */
    @JvmStatic
    fun install(context: Context) {
        val app = context.applicationContext as? Application ?: return
        BackgroundWatcher.install(app)
    }

    /** The core's version (the repository VERSION, e.g. 0.1.0-build.63). */
    @JvmStatic val version: String get() = CKeyNative.nativeSoftwareVersion()

    /** Whether the native library contains the scanner / renderer / certification. */
    @JvmStatic val hasScanner: Boolean get() = CKeyNative.has(CKeyNative.FEATURE_SCAN)
    @JvmStatic val hasRenderer: Boolean get() = CKeyNative.has(CKeyNative.FEATURE_RENDER)
    @JvmStatic val hasCertification: Boolean get() = CKeyNative.has(CKeyNative.FEATURE_CERTIFY)
}

/** A histogram with inclusive upper bucket edges (see include/ckey/telemetry.h). */
data class CKeyHistogram(val count: Long, val sum: Long, val buckets: List<Long>)

/** Local aggregate telemetry totals (`ckey_telemetry_counters`). */
data class CKeyTelemetryCounters(
    val sessionsStarted: Long,
    val sessionsSucceeded: Long,
    val sessionsFailed: Long,
    val frames: Long,
    /** Decodes per profile, indexed by [CKeyProfile.code]. */
    val decoded: List<Long>,
    val timeToDecodeMs: CKeyHistogram,
    val frameMs: CKeyHistogram,
) {
    companion object {
        internal fun fromArray(v: LongArray): CKeyTelemetryCounters {
            fun histogram(at: Int) = CKeyHistogram(v[at], v[at + 1], v.copyOfRange(at + 2, at + 12).toList())
            return CKeyTelemetryCounters(v[0], v[1], v[2], v[3], v.copyOfRange(4, 9).toList(), histogram(9), histogram(21))
        }
    }
}
