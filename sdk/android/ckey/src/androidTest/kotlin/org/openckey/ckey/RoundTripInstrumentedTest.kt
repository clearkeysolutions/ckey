package org.openckey.ckey

import android.graphics.Bitmap
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith

/**
 * On-device round trips through the real libckey_jni.so:
 * encode -> render (Bitmap / gray) -> decode. Run with
 * `gradle -p sdk/android :ckey:connectedDebugAndroidTest`.
 */
@RunWith(AndroidJUnit4::class)
class RoundTripInstrumentedTest {
    private val url = "https://openckey.org/android"

    @Test fun nativeLibraryHasEveryModule() {
        assertTrue(CKey.hasScanner && CKey.hasRenderer && CKey.telemetryAvailable)
        assertTrue(CKey.version.startsWith("0."))
    }

    @Test fun bitmapRoundTrip() {
        val symbol = CKeySymbol.encode(url)
        val bitmap = symbol.toBitmap(480)
        assertEquals(Bitmap.Config.ARGB_8888, bitmap.config)
        val result = CKeyDecoder.decode(bitmap)
        assertNotNull(result)
        assertEquals(url, result!!.text)
        assertEquals(CKeyProfile.P0, result.profile)
        assertTrue(result.action is CKeyPayloadAction.Web)
    }

    @Test fun transparentBitmapIsPremultipliedCorrectly() {
        val bitmap = CKeySymbol.encode(url).toBitmap(256, RasterPolicy.COVERAGE, transparent = true)
        assertTrue(bitmap.hasAlpha() && bitmap.isPremultiplied)
        // getPixel un-premultiplies: an ink pixel must come back black, not darkened garbage.
        val straight = CKeySymbol.encode(url).toRgba(256, RasterPolicy.COVERAGE, transparent = true)
        var compared = 0
        for (i in 0 until 256 * 256 step 97) {
            val a = straight[i * 4 + 3].toInt() and 0xff
            if (a < 128) continue
            val c = bitmap.getPixel(i % 256, i / 256)
            assertEquals(a, c ushr 24)
            assertTrue(((c ushr 16) and 0xff) <= 2)
            compared++
        }
        assertTrue(compared > 0)
    }

    @Test fun grayRoundTripEveryLayoutAndLevel() {
        for (layout in Layout.entries) for (level in EccLevel.entries) {
            val payload = "CKey ${layout.name} ${level.name} Grüße 👋".toByteArray()
            val symbol = CKeySymbol.encode(payload, 0, level, layout)
            // U and X need a much larger version for the same payload; keep the modules resolvable.
            val side = if (level == EccLevel.U || level == EccLevel.X) 2000 else 400
            val gray = symbol.toGray(side)
            val result = CKeyDecoder.decode(gray, side, side)
            assertNotNull("$layout $level", result)
            assertArrayEquals(payload, result!!.bytes)
            assertEquals(layout, result.layout)
        }
    }

    @Test fun exports() {
        val symbol = CKeySymbol.encode(url)
        assertEquals(0x89.toByte(), symbol.toPng(256)[0])
        assertTrue(symbol.toSvg().contains("<svg"))
        assertEquals("%PDF-", String(symbol.toPdf().copyOf(5)))
    }

    @Test fun telemetryProperty() {
        val before = CKey.telemetry
        try {
            CKey.telemetry = 3
            fail("3 accepted")
        } catch (_: IllegalArgumentException) {
        }
        assertEquals(before, CKey.telemetry)
        CKey.telemetry = 2
        val scanner = CKeyScanner()
        scanner.start()
        assertNotNull(scanner.process(CKeySymbol.encode(url).toGray(300), 300, 300))
        scanner.stop()
        assertTrue(CKey.telemetryCounters()!!.sessionsSucceeded >= 1)
        CKey.telemetry = before
    }
}
