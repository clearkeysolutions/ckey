package org.openckey.ckey

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The receiving app's policy, with a fake core (no native library). The core's
 * own verification is checked against tests/vectors/safe-actions-v1.json by
 * sdk/android/jni-host-test (HostHarness).
 */
class CKeySafeActionsTest {
    private val key = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
    private val now = 1_794_000_000L

    /** Payload text after the prefix names the envelope; the fake core returns its fields. */
    private class FakeCore(val envelopes: Map<String, String>) : CKeySafeActions.Core {
        var verifyStatus = 0
        var verifyMessage = ""
        override fun run(payload: ByteArray, publicKey: ByteArray?, nowSeconds: Long, maxLifetime: Int): Pair<Int, String> {
            val id = String(payload, Charsets.UTF_8).removePrefix(CKeySafeActions.PREFIX)
            val fields = envelopes[id] ?: return 1 to "malformed envelope"
            if (publicKey == null) return 0 to fields
            return if (verifyStatus == 0) 0 to fields else verifyStatus to verifyMessage
        }
    }

    private fun envelope(scope: Int, app: String = "com.viadere", action: String = "open.library", issuer: String = "citeory.com", request: String = "r_8f3K2pQ7xZ") =
        listOf(scope.toString(), issuer, app, action, "2026-11", request, (now - 30).toString(), (now + 90).toString(), "7b7d").joinToString("\n")

    private fun actions(core: FakeCore, fetch: (String) -> String? = { null }): CKeySafeActions {
        val a = CKeySafeActions("com.viadere")
        a.core = core
        a.fetcher = CKeySafeActions.Fetcher { fetch(it) }
        a.clock = { now * 1000 }
        return a
    }

    @Test fun notASafeAction() {
        val a = actions(FakeCore(emptyMap()))
        assertEquals(CKeySafeActionOutcome.NotASafeAction, a.handle("https://citeory.com"))
        assertEquals(CKeySafeActionOutcome.NotASafeAction, a.handle(CKeySafeActions.PREFIX))
        assertTrue(a.handle(CKeySafeActions.PREFIX + "nonsense") is CKeySafeActionOutcome.Rejected)
    }

    @Test fun sameDeviceRunsOnceWithoutPrompt() {
        val ran = mutableListOf<CKeyVerifiedSafeAction>()
        val a = actions(FakeCore(mapOf("a" to envelope(0))))
            .trust("citeory.com", "2026-11", key)
            .register("open.library", listOf("citeory.com")) { ran += it }
        val outcome = a.handle(CKeySafeActions.PREFIX + "a")
        assertTrue(outcome is CKeySafeActionOutcome.Performed)
        assertEquals(1, ran.size)
        assertEquals("{}", ran[0].paramsText)
        assertEquals(CKeySafeActionScope.SAME_DEVICE, ran[0].scope)
        val replay = a.handle(CKeySafeActions.PREFIX + "a")
        assertEquals(CKeySafeActionOutcome.Rejected("this code has already been used"), replay)
        assertEquals(1, ran.size)
    }

    @Test fun everyPolicyCheckRunsNothing() {
        var ran = 0
        val core = FakeCore(mapOf(
            "other-app" to envelope(0, app = "com.other"),
            "unregistered" to envelope(0, action = "delete.everything"),
            "wrong-issuer" to envelope(0, issuer = "evil.example"),
            "ok" to envelope(0, request = "r_unique0001"),
        ))
        val a = actions(core)
            .register("open.library", listOf("citeory.com")) { ran++ }
        fun reason(id: String) = (a.handle(CKeySafeActions.PREFIX + id) as CKeySafeActionOutcome.Rejected).reason
        assertTrue(reason("other-app").contains("another app"))
        assertTrue(reason("unregistered").contains("does not accept"))
        assertTrue(reason("wrong-issuer").contains("may not request"))
        assertTrue(reason("ok").contains("no trusted key"))
        a.trust("citeory.com", "2026-11", key)
        core.verifyStatus = 6
        core.verifyMessage = "signature does not verify"
        assertEquals("signature does not verify", reason("ok"))
        core.verifyStatus = 7
        core.verifyMessage = "expired"
        assertEquals("expired", reason("ok"))
        assertEquals(0, ran)
    }

    @Test fun crossDeviceNeedsPendingRequestInfoAndTheSdkPrompt() {
        var ran = 0
        val urls = mutableListOf<String>()
        var status = "pending"
        val core = FakeCore(mapOf("x" to envelope(1, action = "signin.request", request = "sess-41fd0a9be2c74"),
            "y" to envelope(1, action = "signin.request", request = "sess-other00001")))
        val a = actions(core) { url ->
            urls += url
            """{"request_id":"${url.substringAfterLast('/')}","device":"Chrome on Windows","location":"near Lagos, Nigeria","requested_at":"2026-11-07T20:14:03Z","status":"$status"}"""
        }
            .trust("citeory.com", "2026-11", key, requestInfo = "https://citeory.com/api/ckey/requests/{request_id}")
            .register("signin.request", listOf("citeory.com")) { ran++ }   // registered same-device: the envelope's cross-device wins
        val outcome = a.handle(CKeySafeActions.PREFIX + "x") as CKeySafeActionOutcome.NeedsConfirmation
        assertEquals(listOf("https://citeory.com/api/ckey/requests/sess-41fd0a9be2c74"), urls)
        assertEquals(0, ran)
        val request = outcome.request
        assertEquals("Chrome on Windows", request.requestInfo.device)
        assertTrue(CKeySafeActionPrompt.message(request).contains("citeory.com (verified)"))
        assertTrue(CKeySafeActionPrompt.message(request).contains("near Lagos, Nigeria"))
        assertTrue(request.decide(true))
        assertEquals(1, ran)
        assertFalse("a request is decided once", request.decide(true))
        assertEquals(1, ran)
        status = "approved"
        assertTrue(a.handle(CKeySafeActions.PREFIX + "y") is CKeySafeActionOutcome.Rejected)
        assertEquals(1, ran)
    }

    @Test fun registeredCrossDeviceIsStricterThanTheEnvelope() {
        var ran = 0
        val a = actions(FakeCore(mapOf("s" to envelope(0)))) { null }
            .trust("citeory.com", "2026-11", key, requestInfo = "https://citeory.com/r/{request_id}")
            .register("open.library", listOf("citeory.com"), CKeySafeActionScope.CROSS_DEVICE) { ran++ }
        assertTrue("no request info, no prompt, nothing runs", a.handle(CKeySafeActions.PREFIX + "s") is CKeySafeActionOutcome.Rejected)
        assertEquals(0, ran)
    }

    @Test fun declineRunsNothing() {
        var ran = 0
        val a = actions(FakeCore(mapOf("x" to envelope(1)))) { """{"request_id":"r_8f3K2pQ7xZ","status":"pending"}""" }
            .trust("citeory.com", "2026-11", key, requestInfo = "https://login.citeory.com/r/{request_id}")
            .register("open.library", listOf("citeory.com")) { ran++ }
        val request = (a.handle(CKeySafeActions.PREFIX + "x") as CKeySafeActionOutcome.NeedsConfirmation).request
        assertFalse(request.decide(false))
        assertFalse(request.decide(true))
        assertEquals(0, ran)
    }

    @Test fun keyFiles() {
        val fetched = mutableListOf<String>()
        val a = CKeySafeActions("com.viadere", useKeyFiles = true)
        a.core = FakeCore(mapOf("a" to envelope(0), "b" to envelope(0, request = "r_second0001")))
        a.clock = { now * 1000 }
        a.fetcher = CKeySafeActions.Fetcher { url ->
            fetched += url
            """{"issuer":"citeory.com","keys":[{"id":"2026-11","alg":"Ed25519","public_key":"$key","not_after":"2027-11-01T00:00:00Z"}]}"""
        }
        var ran = 0
        a.register("open.library", listOf("citeory.com")) { ran++ }
        assertTrue(a.handle(CKeySafeActions.PREFIX + "a") is CKeySafeActionOutcome.Performed)
        assertTrue(a.handle(CKeySafeActions.PREFIX + "b") is CKeySafeActionOutcome.Performed)
        assertEquals("cached for an hour", listOf("https://citeory.com/.well-known/ckey-safe-actions.json"), fetched)
        assertEquals(2, ran)
    }

    @Test fun expiredKeyFileEntryIsNotTrusted() {
        val a = CKeySafeActions("com.viadere", useKeyFiles = true)
        a.core = FakeCore(mapOf("a" to envelope(0)))
        a.clock = { now * 1000 }
        a.fetcher = CKeySafeActions.Fetcher {
            """{"issuer":"citeory.com","keys":[{"id":"2026-11","alg":"Ed25519","public_key":"$key","not_after":"2026-01-01T00:00:00Z"}]}"""
        }
        a.register("open.library", listOf("citeory.com")) { }
        assertTrue(a.handle(CKeySafeActions.PREFIX + "a") is CKeySafeActionOutcome.Rejected)
    }

    @Test fun requestInfoUrlStaysOnTheIssuersDomainOverHttps() {
        val ok = CKeySafeActions.requestInfoUrl("https://citeory.com/r/{request_id}", "citeory.com", "sess-1234abcd")
        assertEquals("https://citeory.com/r/sess-1234abcd", ok)
        assertEquals("https://api.citeory.com/r/x", CKeySafeActions.requestInfoUrl("https://api.citeory.com/r/{request_id}", "citeory.com", "x"))
        assertNull(CKeySafeActions.requestInfoUrl("http://citeory.com/r/{request_id}", "citeory.com", "x"))
        assertNull(CKeySafeActions.requestInfoUrl("https://evilciteory.com/r/{request_id}", "citeory.com", "x"))
        assertNull(CKeySafeActions.requestInfoUrl("https://citeory.com.evil.example/r/{request_id}", "citeory.com", "x"))
        assertNull(CKeySafeActions.requestInfoUrl("https://citeory.com@evil.example/r/{request_id}", "citeory.com", "x"))
        assertNull(CKeySafeActions.requestInfoUrl(null, "citeory.com", "x"))
    }

    @Test fun argumentChecks() {
        fun throws(body: () -> Unit) = try { body(); false } catch (_: IllegalArgumentException) { true }
        assertTrue(throws { CKeySafeActions("not an app id") })
        assertTrue(throws { CKeySafeActions("com.viadere", maxLifetime = 3601) })
        assertTrue(throws { CKeySafeActions("com.viadere").trust("Citeory.com", "k", key) })
        assertTrue(throws { CKeySafeActions("com.viadere").trust("citeory.com", "k", "abc") })
        assertTrue(throws { CKeySafeActions("com.viadere").register("Open", listOf("citeory.com")) {} })
        assertTrue(throws { CKeySafeActions("com.viadere").register("open", emptyList()) {} })
    }

    @Test fun helpers() {
        val m = MiniJson.parse("""{"a":[1,true,null,"xA\n"],"b":{}}""") as Map<*, *>
        assertEquals(listOf(1.0, true, null, "xA\n"), m["a"])
        assertNull(MiniJson.parse("{\"a\":1} trailing"))
        assertNull(MiniJson.parse("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"))
        assertEquals(1_825_027_200_000L, CKeySafeActions.utcMillis("2027-11-01T00:00:00Z"))
        assertNull(CKeySafeActions.utcMillis("2027-13-01T00:00:00Z"))
        assertNull(CKeySafeActions.fields("1\na"))
        assertNull(CKeySafeActions.fields(envelope(2)))
    }
}
