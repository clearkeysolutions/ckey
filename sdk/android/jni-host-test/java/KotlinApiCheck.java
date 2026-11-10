// Exercises the Kotlin public API (compiled from ckey/src/main/kotlin) from
// plain Java on a desktop JVM, with android.* replaced by tiny stubs. Run by
// run.sh when a Kotlin compiler is available.

import android.graphics.Bitmap;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import org.openckey.ckey.CKey;
import org.openckey.ckey.CKeyDecoder;
import org.openckey.ckey.CKeyException;
import org.openckey.ckey.CKeyPayload;
import org.openckey.ckey.CKeyPayloadAction;
import org.openckey.ckey.CKeyResult;
import org.openckey.ckey.CKeyScanner;
import org.openckey.ckey.CKeyStyle;
import org.openckey.ckey.CKeySymbol;
import org.openckey.ckey.CKeyTelemetryCounters;
import org.openckey.ckey.EccLevel;
import org.openckey.ckey.Layout;
import org.openckey.ckey.RasterPolicy;
import org.openckey.ckey.StylePolicy;

public final class KotlinApiCheck {
    static int checks = 0;

    static void check(boolean ok, String what) {
        checks++;
        if (!ok) throw new AssertionError("FAILED: " + what);
        System.out.println("ok   " + what);
    }

    public static void main(String[] args) {
        String url = "https://openckey.org/kotlin";
        CKeySymbol symbol = CKeySymbol.encode(url);
        check(symbol.getLevel() == EccLevel.M && symbol.getLayout() == Layout.STANDARD && symbol.getPayloadLength() == url.length(), "encode(text) defaults: " + symbol);
        CKeySymbol print = CKeySymbol.encode(url.getBytes(StandardCharsets.UTF_8), 0, EccLevel.Q, Layout.PRINT);
        check(print.getLayout() == Layout.PRINT && print.getLevel() == EccLevel.Q, "encode(bytes, 0, Q, PRINT)");
        CKeySymbol ultra = CKeySymbol.encode(url.getBytes(StandardCharsets.UTF_8), 0, EccLevel.U, Layout.STANDARD_GROUPED);
        check(ultra.getLayout() == Layout.STANDARD_GROUPED && ultra.getLevel() == EccLevel.U && ultra.getLayout().isGrouped() && !ultra.getLayout().isPrint(), "encode(bytes, 0, U, STANDARD_GROUPED)");
        CKeySymbol extreme = CKeySymbol.encode(url.getBytes(StandardCharsets.UTF_8), 0, EccLevel.X, Layout.PRINT_GROUPED);
        check(extreme.getLayout() == Layout.PRINT_GROUPED && extreme.getLevel() == EccLevel.X && extreme.getLayout().isPrint(), "encode(bytes, 0, X, PRINT_GROUPED)");
        check(EccLevel.fromCode(4) == EccLevel.U && EccLevel.fromCode(5) == EccLevel.X && Layout.fromCode(2) == Layout.STANDARD_GROUPED && Layout.fromCode(3) == Layout.PRINT_GROUPED, "enum codes for U, X and the grouped layouts");
        check(CKeySymbol.capacity(1, EccLevel.X, Layout.STANDARD) == 0, "capacity(1, X) = 0");
        check(CKeySymbol.capacity(31) > 4000, "capacity(31) = " + CKeySymbol.capacity(31));

        // toBitmap: premultiplied bytes handed to copyPixelsFromBuffer.
        Bitmap opaque = symbol.toBitmap(300);
        check(opaque.getWidth() == 300 && !opaque.hasAlpha(), "toBitmap(300) opaque");
        Bitmap clear = symbol.toBitmap(300, RasterPolicy.COVERAGE, true, null);
        boolean premultiplied = true;
        for (int p = 0; p < clear.memory.length; p += 4)
            for (int c = 0; c < 3; c++) if ((clear.memory[p + c] & 0xff) > (clear.memory[p + 3] & 0xff)) premultiplied = false;
        check(clear.hasAlpha() && premultiplied, "toBitmap(transparent) is premultiplied");

        // Round trip through the public decoder (Bitmap helper composites over white).
        CKeyResult result = org.openckey.ckey.CKeyBitmaps.decode(CKeyDecoder.INSTANCE, clear);
        check(result != null && url.equals(result.getText()), "Bitmap -> CKeyDecoder.decode: " + result);
        check(result.getAction() instanceof CKeyPayloadAction.Web && ((CKeyPayloadAction.Web) result.getAction()).getHost().equals("openckey.org"), "result action Web(openckey.org)");

        byte[] gray = symbol.toGray(256);
        CKeyResult direct = CKeyDecoder.decode(gray, 256, 256);
        check(direct != null && Arrays.equals(direct.getBytes(), url.getBytes(StandardCharsets.UTF_8)), "toGray -> decode(gray, w, h)");

        check(symbol.toPng(128).length > 0 && symbol.toSvg().contains("<svg") && symbol.toPdf().length > 0, "toPng / toSvg / toPdf");

        CKeyStyle concept = CKeyStyle.getCONCEPT();
        check(concept.getAccents().size() == 2 && concept.getPolicy() == StylePolicy.STRICT, "CKeyStyle.CONCEPT from the core");
        check(CKeyStyle.getDEFAULT().getInk() == 0 && CKeyStyle.getDEFAULT().getBackground() == 0xFFFFFF, "CKeyStyle.DEFAULT black on white");
        check(CKeyStyle.getDEFAULT().equals(new CKeyStyle()), "Kotlin defaults equal ckey_style_default");
        try {
            CKeyStyle bad = new CKeyStyle(0xFFFFFF, 0xFFFFFF, 0xF4F4F4, 0xF4F4F4, java.util.Collections.emptyList(), 0,
                    org.openckey.ckey.ModuleShape.ROUNDED, org.openckey.ckey.RingStyle.JOINED, org.openckey.ckey.RayCap.ROUND,
                    0, 0, 0, null, null, 0, false, StylePolicy.STRICT);
            symbol.toBitmap(128, RasterPolicy.COVERAGE, false, bad);
            check(false, "low-contrast style should be refused");
        } catch (CKeyException e) {
            check(e.getStatus() == CKeyException.INVALID_ARGUMENT, "strict style refused: " + e.getMessage());
        }

        // Telemetry property.
        check(CKey.getTelemetry() == 1, "CKey.telemetry defaults to 1");
        try { CKey.setTelemetry(3); check(false, "3 accepted"); } catch (IllegalArgumentException e) { check(true, "CKey.telemetry = 3 -> IllegalArgumentException"); }
        check(CKey.getTelemetry() == 1, "mode unchanged after the invalid value");

        CKeyScanner scanner = new CKeyScanner();
        scanner.start();
        byte[] blank = new byte[64 * 64];
        check(scanner.process(blank, 64, 64, 64, 0) == null, "scanner: blank frame");
        check(scanner.process(gray, 256, 256, 256, 0) != null, "scanner: decode");
        check(scanner.process(gray, 256, 256, 256, 0) == null, "scanner: frames ignored after a decode");
        scanner.stop();
        CKeyTelemetryCounters counters = CKey.telemetryCounters();
        check(counters != null && counters.getSessionsSucceeded() >= 1 && counters.getFrames() >= 2 && counters.getDecoded().get(0) >= 1, "telemetry counters " + counters);
        CKey.setTelemetry(0);
        check(CKey.getTelemetry() == 0 && CKey.telemetryCounters().getFrames() == 0, "CKey.telemetry = 0 purges");
        CKey.setTelemetry(1);

        check(CKeyPayload.classify("javascript:alert(1)") == null, "javascript: is plain text");
        System.out.println("KotlinApiCheck: " + checks + " checks passed");
    }
}
