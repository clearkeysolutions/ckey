package org.openckey.ckey

import android.graphics.ImageFormat
import android.media.Image
import java.nio.ByteBuffer

/** A decoded CKey. */
class CKeyResult internal constructor(
    bytes: ByteArray,
    /** The optical profile it was read from. */
    val profile: CKeyProfile,
    /** Profile 0 version 1..31; 0 for Profiles 1–3. */
    val version: Int,
    /** Profile 0 layout; null for Profiles 1–3. */
    val layout: Layout?,
    quad: DoubleArray,
    /** Border candidates found in the frame. */
    val candidates: Int,
) {
    private val payload = bytes
    private val corners = quad

    /** The payload bytes (a copy). */
    val bytes: ByteArray get() = payload.copyOf()

    /** The payload as text, when it is valid UTF-8 per the payload conventions; else null (show [hex]). */
    val text: String? by lazy { CKeyPayload.text(payload) }

    val hex: String get() = CKeyPayload.hex(payload)

    /** The allowlisted action the text offers, if any. Never performed automatically. */
    val action: CKeyPayloadAction? by lazy { text?.let(CKeyPayload::classify) }

    /** The decoded symbol's quadrilateral in frame pixels: x0,y0,...,x3,y3 (a copy). */
    val quad: DoubleArray get() = corners.copyOf()

    override fun toString(): String = "CKeyResult(${profile.label}${if (version > 0) " v$version" else ""}, ${payload.size} bytes)"
}

/**
 * Stateless grayscale decoding: full-frame localization, then Profile 0 and
 * Profiles 1, 2 and 3, as the iOS reader does. Thread-safe; call it off the
 * main thread. Records no telemetry: use [CKeyScanner] (or the CameraX
 * `CKeyAnalyzer`) for scan sessions.
 */
object CKeyDecoder {
    /** Border candidates tried per frame (the iOS reader tries 6). */
    const val DEFAULT_MAX_CANDIDATES = 6

    /**
     * Decodes an 8-bit grayscale frame: [width] x [height] pixels, row
     * [stride] bytes, starting at [offset]. Either contrast polarity.
     * Returns null when no CKey is read.
     */
    @JvmStatic
    @JvmOverloads
    fun decode(
        gray: ByteArray, width: Int, height: Int, stride: Int = width,
        offset: Int = 0, maxCandidates: Int = DEFAULT_MAX_CANDIDATES,
    ): CKeyResult? {
        CKeyNative.require(CKeyNative.FEATURE_SCAN, "the scanner")
        val info = IntArray(CKeyNative.DECODE_INFO)
        val quad = DoubleArray(8)
        val bytes = CKeyNative.nativeDecodeArray(gray, offset, width, height, stride, maxCandidates, info, quad)
        return result(bytes, info, quad)
    }

    /**
     * Decodes from a direct [ByteBuffer] (no copy), e.g. a camera Y plane
     * whose pixel stride is 1. [offset] is relative to the buffer's start
     * (position is ignored). A heap buffer is copied first.
     */
    @JvmStatic
    @JvmOverloads
    fun decode(
        gray: ByteBuffer, width: Int, height: Int, stride: Int,
        offset: Int = 0, maxCandidates: Int = DEFAULT_MAX_CANDIDATES,
    ): CKeyResult? {
        CKeyNative.require(CKeyNative.FEATURE_SCAN, "the scanner")
        if (!gray.isDirect) {
            val copy = ByteArray(gray.capacity())
            gray.duplicate().apply { clear() }.get(copy)
            return decode(copy, width, height, stride, offset, maxCandidates)
        }
        val info = IntArray(CKeyNative.DECODE_INFO)
        val quad = DoubleArray(8)
        val bytes = CKeyNative.nativeDecodeBuffer(gray, offset, width, height, stride, maxCandidates, info, quad)
        return result(bytes, info, quad)
    }

    /**
     * Decodes the luma (Y) plane of a camera [Image] (YUV_420_888, YUV_422_888,
     * YUV_444_888 or Y8). The caller keeps ownership and closes the image.
     */
    @JvmStatic
    @JvmOverloads
    fun decode(image: Image, maxCandidates: Int = DEFAULT_MAX_CANDIDATES): CKeyResult? {
        val format = image.format
        require(
            format == ImageFormat.YUV_420_888 || format == ImageFormat.YUV_422_888 ||
                format == ImageFormat.YUV_444_888 || format == FORMAT_Y8
        ) { "unsupported image format $format (need a YUV or Y8 image)" }
        val plane = image.planes[0]
        return decodeLuma(plane.buffer, plane.rowStride, plane.pixelStride, image.width, image.height, maxCandidates)
    }

    /**
     * Decodes a luma plane with any pixel stride (e.g. CameraX ImageProxy
     * planes). A pixel stride of 1 is decoded in place.
     */
    @JvmStatic
    fun decodeLuma(buffer: ByteBuffer, rowStride: Int, pixelStride: Int, width: Int, height: Int,
                   maxCandidates: Int = DEFAULT_MAX_CANDIDATES): CKeyResult? {
        if (pixelStride == 1) return decode(buffer, width, height, rowStride, 0, maxCandidates)
        require(pixelStride > 1) { "pixelStride must be positive" }
        // Rare: pack the samples into a tight gray frame.
        val packed = ByteArray(width * height)
        val src = buffer.duplicate().apply { clear() }
        for (y in 0 until height) {
            val row = y * rowStride
            for (x in 0 until width) packed[y * width + x] = src.get(row + x * pixelStride)
        }
        return decode(packed, width, height, width, 0, maxCandidates)
    }

    /** android.graphics.ImageFormat.Y8 (API 29); a literal so minSdk 24 compiles. */
    private const val FORMAT_Y8 = 0x20203859

    private fun result(bytes: ByteArray?, info: IntArray, quad: DoubleArray): CKeyResult? {
        if (bytes == null) return null
        val profile = CKeyProfile.fromCode(info[0])
        val p0 = profile == CKeyProfile.P0 || profile == CKeyProfile.P0_1
        return CKeyResult(bytes, profile, if (p0) info[1] else 0, if (p0) Layout.fromCode(info[2]) else null, quad, info[3])
    }
}
