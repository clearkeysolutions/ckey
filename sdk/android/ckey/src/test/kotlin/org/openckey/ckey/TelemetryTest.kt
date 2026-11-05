package org.openckey.ckey

import java.util.concurrent.Executor
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.openckey.ckey.telemetry.Telemetry
import org.openckey.ckey.telemetry.TelemetryBatch
import org.openckey.ckey.telemetry.TelemetryCore
import org.openckey.ckey.telemetry.TelemetrySender
import org.openckey.ckey.telemetry.utcDay

/** Telemetry validation and the sender loop, with a fake core (no native library). */
class TelemetryTest {
    private class FakeCore : TelemetryCore {
        var current = 1
        var due = true
        var batch: TelemetryBatch? = TelemetryBatch(7, "{\"schema\":1}")
        val results = mutableListOf<Pair<Long, Int>>()
        val days = mutableListOf<String>()
        override val available = true
        override fun setMode(mode: Int): Int = if (mode in 0..2) { current = mode; 0 } else 1
        override fun getMode() = current
        override fun setClient(sdk: String, version: String, platform: String) = 0
        override fun sessionBegin(nowMs: Long) = Unit
        override fun frame(nowMs: Long, processingMs: Int) = Unit
        override fun sessionSuccess(nowMs: Long, profile: Int) = 0
        override fun sessionEnd(nowMs: Long) = Unit
        override fun shouldSend(nowMs: Long) = due
        override fun takeBatch(utcDay: String): TelemetryBatch? { days += utcDay; return batch.also { batch = null } }
        override fun batchResult(id: Long, httpStatus: Int, nowMs: Long): Int { results += id to httpStatus; return 0 }
        override fun snapshot(): LongArray? = null
        override fun exportState(): String? = null
        override fun importState(json: String) = 1
        override val endpoint = "https://openckey.org/api/telemetry/v1/batch"
    }

    private val direct = Executor { it.run() }

    @After fun tearDown() { Telemetry.coreOverride = null }

    @Test fun invalidModesThrowAndNeverReset() {
        val core = FakeCore().also { Telemetry.coreOverride = it }
        for (bad in listOf(-1, 3, 4, 100, Int.MIN_VALUE, Int.MAX_VALUE)) {
            try {
                CKey.telemetry = bad
                fail("accepted $bad")
            } catch (e: IllegalArgumentException) {
                assertTrue(e.message!!.contains("$bad"))
            }
        }
        assertEquals(1, core.current)
        CKey.telemetry = 0
        assertEquals(0, CKey.telemetry)
        CKey.telemetry = 2
        assertEquals(2, CKey.telemetry)
        try { CKey.telemetry = 7; fail() } catch (_: IllegalArgumentException) { }
        assertEquals(2, CKey.telemetry)
    }

    @Test fun validationNeedsNoNativeLibrary() {
        // No override: validation must fail before the JNI core is touched.
        try { CKey.telemetry = 5; fail() } catch (_: IllegalArgumentException) { }
    }

    @Test fun sendsADueBatchAndReportsTheStatusOnce() {
        val core = FakeCore()
        var posted: String? = null
        val sender = TelemetrySender(core, { url, body -> posted = "$url ${String(body)}"; 204 }, direct, { 5_000L }, { 0L })
        assertEquals(204, sender.sendIfDue())
        assertEquals("https://openckey.org/api/telemetry/v1/batch {\"schema\":1}", posted)
        assertEquals(listOf(7L to 204), core.results)
        assertEquals(listOf("1970-01-01"), core.days)
        assertNull(sender.sendIfDue())  // nothing left
        assertEquals(1, core.results.size)
    }

    @Test fun networkErrorsAreReportedAsZero() {
        val core = FakeCore()
        TelemetrySender(core, { _, _ -> throw java.io.IOException("offline") }, direct, { 1L }).sendIfDue()
        assertEquals(listOf(7L to 0), core.results)
        val core2 = FakeCore()
        TelemetrySender(core2, { _, _ -> 5000 }, direct, { 1L }).sendIfDue()
        assertEquals(listOf(7L to 0), core2.results)
    }

    @Test fun nothingHappensWhenNotDue() {
        val core = FakeCore().apply { due = false }
        var calls = 0
        assertNull(TelemetrySender(core, { _, _ -> calls++; 204 }, direct, { 1L }).sendIfDue())
        assertEquals(0, calls)
        assertTrue(core.days.isEmpty())
    }

    @Test fun requestSendRunsOnTheExecutorOnly() {
        val core = FakeCore()
        val queued = mutableListOf<Runnable>()
        val sender = TelemetrySender(core, { _, _ -> 204 }, { queued += it }, { 1L })
        sender.requestSend()
        assertTrue(core.results.isEmpty())
        queued.single().run()
        assertEquals(1, core.results.size)
    }

    @Test fun utcDayFormat() {
        assertEquals("2026-11-05", utcDay(1_793_836_800_000L))
        assertEquals("2026-11-04", utcDay(1_793_836_799_999L))
    }
}
