// Minimal host-JVM stand-in for android.graphics.Bitmap (compile + behaviour
// checks only; never shipped). Stores the bytes copyPixelsFromBuffer receives.
package android.graphics;

import java.nio.Buffer;
import java.nio.ByteBuffer;

public final class Bitmap {
    public enum Config { ARGB_8888 }
    private final int width, height;
    private boolean hasAlpha = true;
    public byte[] memory;     // premultiplied RGBA bytes, as Android stores ARGB_8888
    public int[] argb;        // getPixels source for logo/decode helpers

    private Bitmap(int w, int h) { width = w; height = h; memory = new byte[w * h * 4]; }
    public static Bitmap createBitmap(int w, int h, Config c) { return new Bitmap(w, h); }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public void setHasAlpha(boolean a) { hasAlpha = a; }
    public boolean hasAlpha() { return hasAlpha; }
    public void copyPixelsFromBuffer(Buffer src) {
        ByteBuffer b = (ByteBuffer) src;
        if (b.remaining() < memory.length) throw new RuntimeException("Buffer not large enough for pixels");
        b.get(memory);
    }
    public void getPixels(int[] pixels, int offset, int stride, int x, int y, int w, int h) {
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                int p = ((y + r) * width + (x + c)) * 4;
                int a = memory[p + 3] & 0xff;
                int R = memory[p] & 0xff, G = memory[p + 1] & 0xff, B = memory[p + 2] & 0xff;
                if (a != 0 && a != 255) { R = Math.min(255, (R * 255 + a / 2) / a); G = Math.min(255, (G * 255 + a / 2) / a); B = Math.min(255, (B * 255 + a / 2) / a); }
                pixels[offset + r * stride + c] = (a << 24) | (R << 16) | (G << 8) | B;
            }
    }
}
