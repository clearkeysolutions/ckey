package org.openckey.ckey.telemetry

import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import org.openckey.ckey.CKeyNative

/**
 * Process-wide telemetry facade used by [org.openckey.ckey.CKey] and
 * [org.openckey.ckey.CKeyScanner]. Recording calls go straight to the core
 * (a mutex and a few integer updates); everything that serializes or does
 * I/O runs on one low-priority daemon thread.
 */
internal object Telemetry {
    const val SDK = "android"
    const val PLATFORM = "android"
    /** How often a pending batch is re-checked while the process lives. The core enforces the hourly cadence. */
    private const val CHECK_INTERVAL_MINUTES = 10L

    /** Test hook: replaces the JNI core (pure-JVM unit tests). */
    @Volatile internal var coreOverride: TelemetryCore? = null
    @Volatile internal var transportOverride: TelemetryTransport? = null

    private val core: TelemetryCore get() = coreOverride ?: JniTelemetryCore

    private val origin = System.nanoTime()
    /** Monotonic milliseconds (never negative), the same clock for every core call. */
    fun nowMs(): Long = (System.nanoTime() - origin) / 1_000_000L + 1L

    private val clientSet = AtomicBoolean(false)
    private val periodicStarted = AtomicBoolean(false)

    private val executor: ScheduledExecutorService by lazy {
        ScheduledThreadPoolExecutor(1) { r ->
            Thread(r, "ckey-telemetry").apply { isDaemon = true; priority = Thread.MIN_PRIORITY }
        }.apply { removeOnCancelPolicy = true }
    }

    private val sender: TelemetrySender by lazy {
        TelemetrySender(core, transportOverride ?: HttpTelemetryTransport, executor, ::nowMs)
    }

    /** The canonical modes are 0, 1 and 2; anything else is a configuration error (never silently 1). */
    fun validate(mode: Int) {
        if (mode != 0 && mode != 1 && mode != 2) {
            throw IllegalArgumentException("CKey.telemetry must be 0 (off), 1 (on) or 2 (local only); got $mode")
        }
    }

    fun setMode(mode: Int) {
        validate(mode)
        ensureClient()
        val status = core.setMode(mode)
        if (status != 0) throw IllegalArgumentException("telemetry mode $mode refused by the core (status $status)")
    }

    fun getMode(): Int = core.getMode()

    val available: Boolean get() = core.available

    fun sessionBegin() {
        ensureClient()
        core.sessionBegin(nowMs())
    }

    fun frame(processingMs: Int) = core.frame(nowMs(), processingMs)

    fun sessionSuccess(profile: Int) {
        core.sessionSuccess(nowMs(), profile)
        trigger()
    }

    fun sessionEnd() {
        core.sessionEnd(nowMs())
        trigger()
    }

    fun snapshot(): LongArray? = core.snapshot()
    fun exportState(): String? = core.exportState()
    fun importState(json: String): Int = core.importState(json)

    /** Asks the background thread to send if a batch is due (e.g. when the app goes to the background). */
    fun trigger() {
        if (!core.available || core.getMode() != 1) return
        if (periodicStarted.compareAndSet(false, true)) {
            try {
                executor.scheduleWithFixedDelay({
                    try { sender.sendIfDue() } catch (_: Throwable) { }
                }, CHECK_INTERVAL_MINUTES, CHECK_INTERVAL_MINUTES, TimeUnit.MINUTES)
            } catch (_: RuntimeException) {
                periodicStarted.set(false)
            }
        }
        sender.requestSend()
    }

    private fun ensureClient() {
        if (!core.available || !clientSet.compareAndSet(false, true)) return
        val version = if (coreOverride == null) CKeyNative.nativeSoftwareVersion() else "0.0.0"
        core.setClient(SDK, version, PLATFORM)
    }
}
