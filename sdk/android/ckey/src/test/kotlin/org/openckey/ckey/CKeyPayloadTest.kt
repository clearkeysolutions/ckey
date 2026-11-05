package org.openckey.ckey

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** spec/CKey-Payload-Conventions.md. Pure JVM: no native library needed. */
class CKeyPayloadTest {
    private fun bytes(vararg b: Int) = ByteArray(b.size) { b[it].toByte() }

    @Test fun textAcceptsUtf8WithTabsAndLineFeeds() {
        assertEquals("a\tb\nc", CKeyPayload.text("a\tb\nc".toByteArray()))
        assertEquals("BEGIN:VCARD\r\nEND:VCARD", CKeyPayload.text("BEGIN:VCARD\r\nEND:VCARD".toByteArray()))
        assertEquals("Grüße 👋", CKeyPayload.text("Grüße 👋".toByteArray()))
    }

    @Test fun textRejectsBinaryAndControlCharacters() {
        assertNull(CKeyPayload.text(ByteArray(0)))
        assertNull(CKeyPayload.text(bytes(0xff, 0xfe)))            // not UTF-8
        assertNull(CKeyPayload.text(bytes(0xc3)))                  // truncated sequence
        assertNull(CKeyPayload.text(bytes(0xed, 0xa0, 0x80)))      // encoded surrogate
        assertNull(CKeyPayload.text("a\u0000b".toByteArray()))
        assertNull(CKeyPayload.text("a\u001bb".toByteArray()))
        assertNull(CKeyPayload.text("a\rb".toByteArray()))         // lone CR
        assertNull(CKeyPayload.text("a\u0085b".toByteArray()))     // C1 control
    }

    @Test fun hex() = assertEquals("00ff10", CKeyPayload.hex(bytes(0, 255, 16)))

    @Test fun web() {
        val a = CKeyPayload.classify("  HTTPS://Citeory.com/tour ") as CKeyPayloadAction.Web
        assertEquals("citeory.com", a.host)
        assertEquals("Open citeory.com", a.label)
        assertEquals("HTTPS://Citeory.com/tour", a.uri)
        assertEquals("evil.example", (CKeyPayload.classify("https://bank.example@evil.example/") as CKeyPayloadAction.Web).host)
        assertEquals("[::1]", (CKeyPayload.classify("http://[::1]:8080/x") as CKeyPayloadAction.Web).host)
        assertNull(CKeyPayload.classify("https://"))
        assertNull(CKeyPayload.classify("https:/no-host"))
        assertNull(CKeyPayload.classify("http://:80/"))
    }

    @Test fun email() {
        val a = CKeyPayload.classify("mailto:hello%40citeory.com?subject=Hi") as CKeyPayloadAction.Email
        assertEquals("hello@citeory.com", a.address)
        assertNull(CKeyPayload.classify("mailto:nobody"))
    }

    @Test fun phoneAndSms() {
        val call = CKeyPayload.classify("tel:+1 (555) 123-4567") as CKeyPayloadAction.Call
        assertEquals("+15551234567", call.number)
        assertEquals("tel:+15551234567", call.uri)
        val sms = CKeyPayload.classify("SMS:+15551234567?body=Hi%20there") as CKeyPayloadAction.Message
        assertEquals("+15551234567", sms.number)
        assertEquals("Hi there", sms.body)
        assertNull(CKeyPayload.classify("tel:12"))
        assertNull(CKeyPayload.classify("tel:abc"))
    }

    @Test fun geo() {
        val g = CKeyPayload.classify("geo:37.33490,-122.00900?q=Apple%20Park") as CKeyPayloadAction.Location
        assertEquals(37.3349, g.latitude, 1e-9)
        assertEquals(-122.009, g.longitude, 1e-9)
        assertEquals("Apple Park", g.name)
        assertEquals("geo:37.33490,-122.00900?q=37.33490,-122.00900(Apple%20Park)", g.uri)
        assertTrue(CKeyPayload.classify("geo:1,2,3;crs=wgs84") is CKeyPayloadAction.Location)
        assertNull(CKeyPayload.classify("geo:91,0"))
        assertNull(CKeyPayload.classify("geo:0,181"))
        assertNull(CKeyPayload.classify("geo:NaN,0"))
        assertNull(CKeyPayload.classify("geo:0x1p3,0"))
        assertNull(CKeyPayload.classify("geo:12"))
    }

    @Test fun address() {
        val a = CKeyPayload.classify("address:1 Infinite Loop\n Cupertino, CA 95014\n\n") as CKeyPayloadAction.Address
        assertEquals("1 Infinite Loop, Cupertino, CA 95014", a.address)
        assertEquals("geo:0,0?q=1%20Infinite%20Loop%2C%20Cupertino%2C%20CA%2095014", a.uri)
        assertNull(CKeyPayload.classify("address:   "))
    }

    @Test fun vcard() {
        val card = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Ada Lovelace\r\nEND:VCARD"
        val c = CKeyPayload.classify(card) as CKeyPayloadAction.Contact
        assertEquals("Ada Lovelace", c.name)
        assertEquals(card, c.vcard)
        assertNull(c.uri)
        assertNull(CKeyPayload.classify("BEGIN:VCARD\nFN:Incomplete"))
    }

    @Test fun everythingElseIsPlainText() {
        for (t in listOf("javascript:alert(1)", "data:text/html,<b>x</b>", "file:///etc/passwd",
                         "intent://scan/#Intent;scheme=zxing;end", "market://details?id=x", "hello world", "", ":", "ftp://x.org")) {
            assertNull(t, CKeyPayload.classify(t))
        }
    }
}
