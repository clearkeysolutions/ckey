package org.openckey.ckey.telemetry

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URI
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import java.util.concurrent.Executor

/** POSTs one batch; returns the HTTP status, or 0 for a network error / timeout / no connectivity. */
internal fun interface TelemetryTransport {
    fun post(endpoint: String, json: ByteArray): Int
}

/**
 * HttpURLConnection transport (no extra dependencies). HTTPS only, no
 * cookies, no caching, no redirects followed (a 3xx is reported to the core,
 * which keeps the batch), short timeouts. Sends nothing but the batch body.
 */
internal object HttpTelemetryTransport : TelemetryTransport {
    private const val TIMEOUT_MS = 15_000

    override fun post(endpoint: String, json: ByteArray): Int {
        val url = URI(endpoint).toURL()
        if (url.protocol != "https") return 0
        var connection: HttpURLConnection? = null
        return try {
            connection = (url.openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                doOutput = true
                useCaches = false
                instanceFollowRedirects = false
                connectTimeout = TIMEOUT_MS
                readTimeout = TIMEOUT_MS
                setRequestProperty("Content-Type", "application/json")
                setFixedLengthStreamingMode(json.size)
            }
            connection.outputStream.use { it.write(json) }
            val status = connection.responseCode
            // Drain so the connection can be reused; the body is ignored.
            try {
                (if (status >= 400) connection.errorStream else connection.inputStream)?.use { s ->
                    val buf = ByteArray(512); while (s.read(buf) >= 0) Unit
                }
            } catch (_: IOException) { }
            if (status in 0..999) status else 0
        } catch (_: IOException) {
            0
        } catch (_: SecurityException) {
            0 // the app lacks android.permission.INTERNET
        } catch (_: RuntimeException) {
            0
        } finally {
            connection?.disconnect()
        }
    }
}

/** "YYYY-MM-DD" for a UTC instant (java.time needs API 26; minSdk is 24). */
internal fun utcDay(epochMs: Long): String =
    SimpleDateFormat("yyyy-MM-dd", Locale.US).apply { timeZone = TimeZone.getTimeZone("UTC") }.format(Date(epochMs))

/**
 * The wrapper loop of include/ckey/telemetry.h: when the core says a batch
 * is due, take it, POST it, and report the result exactly once. Runs only on
 * [executor] (a background thread), never on the caller's thread.
 */
internal class TelemetrySender(
    private val core: TelemetryCore,
    private val transport: TelemetryTransport,
    private val executor: Executor,
    private val monotonicMs: () -> Long,
    private val wallClockMs: () -> Long = System::currentTimeMillis,
) {
    /** Queues a check; returns immediately. */
    fun requestSend() {
        try {
            executor.execute { sendIfDue() }
        } catch (_: RuntimeException) {
            // executor shut down or saturated: the next trigger retries
        }
    }

    /** Runs on the executor. Returns the HTTP status reported, or null when nothing was sent. */
    fun sendIfDue(): Int? {
        if (!core.shouldSend(monotonicMs())) return null
        val batch = core.takeBatch(utcDay(wallClockMs())) ?: return null
        val status = try {
            transport.post(core.endpoint, batch.json.toByteArray(Charsets.UTF_8))
        } catch (_: Throwable) {
            0
        }.let { if (it in 0..999) it else 0 }
        // A stale id (mode switched while in flight) is refused by the core and
        // the batch is never resent; nothing else to do here.
        core.batchResult(batch.id, status, monotonicMs())
        return status
    }
}
