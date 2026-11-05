@file:JvmName("CKeyBitmaps")

package org.openckey.ckey

import android.graphics.Bitmap

/**
 * A raster logo from a Bitmap. `Bitmap.getPixels` returns non-premultiplied
 * ARGB, which is repacked as the straight RGBA8 the core expects.
 */
fun CKeyLogo.Companion.fromBitmap(bitmap: Bitmap): CKeyLogo {
    val w = bitmap.width
    val h = bitmap.height
    val argb = IntArray(w * h)
    bitmap.getPixels(argb, 0, w, 0, 0, w, h)
    val rgba = ByteArray(w * h * 4)
    for (i in argb.indices) {
        val c = argb[i]
        rgba[i * 4] = (c ushr 16).toByte()
        rgba[i * 4 + 1] = (c ushr 8).toByte()
        rgba[i * 4 + 2] = c.toByte()
        rgba[i * 4 + 3] = (c ushr 24).toByte()
    }
    return CKeyLogo(w, h, rgba)
}

/**
 * Decodes a CKey from a Bitmap (a photo or screenshot). Pixels are converted
 * to Rec. 709 luma, with transparent pixels composited over white.
 */
fun CKeyDecoder.decode(bitmap: Bitmap): CKeyResult? {
    val w = bitmap.width
    val h = bitmap.height
    val argb = IntArray(w * h)
    bitmap.getPixels(argb, 0, w, 0, 0, w, h)
    val gray = ByteArray(w * h)
    for (i in argb.indices) {
        val c = argb[i]
        val a = c ushr 24
        val r = (c ushr 16) and 0xff
        val g = (c ushr 8) and 0xff
        val b = c and 0xff
        val y = (2126 * r + 7152 * g + 722 * b + 5000) / 10000
        gray[i] = ((y * a + 255 * (255 - a) + 127) / 255).toByte()
    }
    return decode(gray, w, h, w)
}
