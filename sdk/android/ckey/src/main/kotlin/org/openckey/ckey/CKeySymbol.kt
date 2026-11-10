package org.openckey.ckey

import android.graphics.Bitmap
import java.nio.ByteBuffer

/**
 * An encoded Profile 0 symbol (the scalable family, versions 1..31).
 *
 * Immutable and thread-safe. Rendering returns native pixels ([toBitmap],
 * [toRgba], [toGray]); [toPng], [toSvg] and [toPdf] are exports. Rendering
 * needs the native renderer (present in the published AAR).
 */
class CKeySymbol private constructor(private val blob: ByteArray) {
    /** Version 1..31 (module pitch). */
    val version: Int
    val level: EccLevel
    val layout: Layout
    val payloadLength: Int

    init {
        val info = CKeyNative.nativeSymbolInfo(blob)
        version = info[0]
        level = EccLevel.fromCode(info[1])
        layout = Layout.fromCode(info[2])
        payloadLength = info[3]
    }

    /** Modules across the field (29..232). */
    val modulesAcross: Int get() = CKeyNative.nativeModulesAcross(version)

    /** Validates [style] against this symbol; returns the core's report or throws [CKeyException]. */
    fun checkStyle(style: CKeyStyle): String {
        CKeyNative.require(CKeyNative.FEATURE_RENDER, "the renderer")
        return CKeyNative.nativeCheckStyle(blob, style.ints(), style.doubles(), style.logoHref)
    }

    /**
     * Straight-alpha RGBA8 pixels, `sizePx * sizePx * 4` bytes, rows top to
     * bottom, covering the full 31-unit canvas (quiet margin included).
     */
    @JvmOverloads
    fun toRgba(
        sizePx: Int,
        policy: RasterPolicy = RasterPolicy.COVERAGE,
        transparent: Boolean = false,
        style: CKeyStyle? = null,
        options: RasterOptions = RasterOptions.DEFAULT,
    ): ByteArray = raster(sizePx, policy, transparent, style, options, gray = false, premultiply = false)

    /** GRAY8 pixels (Rec. 709 luma of the opaque result), `sizePx * sizePx` bytes. */
    @JvmOverloads
    fun toGray(
        sizePx: Int,
        policy: RasterPolicy = RasterPolicy.COVERAGE,
        style: CKeyStyle? = null,
        options: RasterOptions = RasterOptions.DEFAULT,
    ): ByteArray = raster(sizePx, policy, false, style, options, gray = true, premultiply = false)

    /**
     * An ARGB_8888 [Bitmap], `sizePx` square. The core renders straight
     * alpha; the bridge premultiplies it because `Bitmap.copyPixelsFromBuffer`
     * reads ARGB_8888 memory as premultiplied RGBA bytes. With
     * `transparent = true` unpainted pixels have alpha 0 and the host decides
     * what lies behind (check readability with [certify]).
     */
    @JvmOverloads
    fun toBitmap(
        sizePx: Int,
        policy: RasterPolicy = RasterPolicy.COVERAGE,
        transparent: Boolean = false,
        style: CKeyStyle? = null,
        options: RasterOptions = RasterOptions.DEFAULT,
    ): Bitmap {
        val pixels = raster(sizePx, policy, transparent, style, options, gray = false, premultiply = true)
        val bitmap = Bitmap.createBitmap(sizePx, sizePx, Bitmap.Config.ARGB_8888)
        bitmap.setHasAlpha(transparent)
        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(pixels))
        return bitmap
    }

    /** PNG (RGBA8) bytes; [dpi] > 0 records the physical resolution. */
    @JvmOverloads
    fun toPng(
        sizePx: Int,
        policy: RasterPolicy = RasterPolicy.COVERAGE,
        transparent: Boolean = false,
        style: CKeyStyle? = null,
        dpi: Double = 0.0,
        options: RasterOptions = RasterOptions.DEFAULT,
    ): ByteArray {
        CKeyNative.require(CKeyNative.FEATURE_RENDER, "the renderer")
        val logo = style?.logo
        return CKeyNative.nativeRenderPng(
            blob, style?.ints(), style?.doubles(), style?.logoHref, sizePx, policy.code, transparent,
            options.supersample, options.minFeaturePx, options.materials,
            logo?.rgba, logo?.width ?: 0, logo?.height ?: 0, dpi,
        )
    }

    /** SVG export; [scale] is pixels per module of the viewBox. */
    @JvmOverloads
    fun toSvg(scale: Int = 8, style: CKeyStyle? = null): String {
        CKeyNative.require(CKeyNative.FEATURE_RENDER, "the renderer")
        return CKeyNative.nativeRenderSvg(blob, style?.ints(), style?.doubles(), style?.logoHref, scale)
    }

    /** One-page vector PDF, [widthPt] points wide (72 per inch); flat colours only. */
    @JvmOverloads
    fun toPdf(widthPt: Double = 144.0, transparent: Boolean = false, style: CKeyStyle? = null): ByteArray {
        CKeyNative.require(CKeyNative.FEATURE_RENDER, "the renderer")
        return CKeyNative.nativeRenderPdf(blob, style?.ints(), style?.doubles(), style?.logoHref, widthPt, transparent)
    }

    /**
     * Certifies [style] by rendering, compositing over [backdrop], degrading
     * with [model] and decoding with the real decoder
     * (`ckey_certify_scalable_style`). Transparent symbols need a backdrop, or
     * the result is never certified ("background unknown").
     */
    @JvmOverloads
    fun certify(
        style: CKeyStyle? = null,
        transparent: Boolean = false,
        backdrop: CKeyBackdrop? = null,
        model: CKeyScanModel? = null,
    ): CKeyCertification {
        CKeyNative.require(CKeyNative.FEATURE_CERTIFY, "certification")
        val outInts = IntArray(CKeyNative.STYLE_INTS)
        val outDoubles = DoubleArray(CKeyNative.STYLE_DOUBLES)
        val cert = DoubleArray(9)
        val message = CKeyNative.nativeCertify(
            blob, style?.ints(), style?.doubles(), style?.logoHref, transparent,
            backdrop?.colors?.toIntArray(), backdrop?.pixels?.rgba, backdrop?.pixels?.width ?: 0, backdrop?.pixels?.height ?: 0,
            model?.toArray(), outInts, outDoubles, cert,
        )
        return CKeyCertification(
            status = cert[0].toInt(), certified = cert[1] != 0.0, policy = StylePolicy.fromCode(cert[2].toInt()),
            adjustments = cert[3].toInt(), trials = cert[4].toInt(), trialsPassed = cert[5].toInt(),
            minContrast = cert[6], featureSeparation = cert[7], logoLoad = cert[8], message = message,
            effectiveStyle = CKeyStyle.fromNative(outInts, outDoubles, style?.logoHref, style?.logo),
        )
    }

    private fun raster(
        sizePx: Int, policy: RasterPolicy, transparent: Boolean, style: CKeyStyle?, options: RasterOptions,
        gray: Boolean, premultiply: Boolean,
    ): ByteArray {
        CKeyNative.require(CKeyNative.FEATURE_RENDER, "the renderer")
        require(sizePx in 16..16384) { "sizePx must be 16..16384" }
        val logo = style?.logo
        return CKeyNative.nativeRenderRaster(
            blob, style?.ints(), style?.doubles(), style?.logoHref, sizePx, policy.code, gray, transparent, premultiply,
            options.supersample, options.minFeaturePx, options.materials,
            logo?.rgba, logo?.width ?: 0, logo?.height ?: 0,
        )
    }

    override fun toString(): String = "CKeySymbol(v$version, $level, $layout, $payloadLength bytes)"

    companion object {
        /**
         * Encodes [text] (as UTF-8). [version] 0 picks the smallest version
         * that fits. Throws [CKeyException] when the payload does not fit.
         */
        @JvmStatic
        @JvmOverloads
        fun encode(text: String, version: Int = 0, level: EccLevel = EccLevel.M, layout: Layout = Layout.STANDARD): CKeySymbol =
            encode(text.toByteArray(Charsets.UTF_8), version, level, layout)

        /** Encodes raw payload bytes. */
        @JvmStatic
        @JvmOverloads
        fun encode(bytes: ByteArray, version: Int = 0, level: EccLevel = EccLevel.M, layout: Layout = Layout.STANDARD): CKeySymbol {
            require(version in 0..31) { "version must be 0 (auto) or 1..31" }
            return CKeySymbol(CKeyNative.nativeEncode(bytes, version, level.code, layout.code))
        }

        /** Maximum payload bytes for a version, level and layout; 0 if invalid. */
        @JvmStatic
        @JvmOverloads
        fun capacity(version: Int, level: EccLevel = EccLevel.M, layout: Layout = Layout.STANDARD): Int =
            CKeyNative.nativeCapacity(version, level.code, layout.code)
    }
}

/** Advanced raster options (`ckey_raster_options`). */
data class RasterOptions(
    /** Samples per pixel axis, 1..8. */
    val supersample: Int = 4,
    /** HYBRID only: thinnest drawn mark in pixels, > 0. */
    val minFeaturePx: Double = 1.0,
    /** false draws flat ink (no sheen, depth or grain). */
    val materials: Boolean = true,
) {
    companion object {
        @JvmField val DEFAULT = RasterOptions()
    }
}

/** What lies behind a (transparent) symbol, for [CKeySymbol.certify]. */
data class CKeyBackdrop(
    /** Solid 0xRRGGBB backgrounds the symbol may sit on (at most 16). */
    val colors: List<Int> = emptyList(),
    /** Optional host pixels behind the symbol, stretched over the 31-unit canvas. */
    val pixels: CKeyLogo? = null,
)

/** Modelled acquisition for certification (`ckey_scan_model`). */
data class CKeyScanModel(
    val pxPerModule: Double = 8.0,
    val blurPx: Double = 0.9,
    val noise: Double = 0.03,
    val glare: Double = 0.2,
    val trials: Int = 3,
) {
    internal fun toArray() = doubleArrayOf(pxPerModule, blurPx, noise, glare, trials.toDouble())
}

/** Result of [CKeySymbol.certify] (`ckey_certification`). */
data class CKeyCertification(
    /** Core status: 0 OK; non-zero when a STRICT style is refused. */
    val status: Int,
    val certified: Boolean,
    val policy: StylePolicy,
    /** CKEY_ADJUST_* bits applied under ADAPTIVE. */
    val adjustments: Int,
    val trials: Int,
    val trialsPassed: Int,
    val minContrast: Double,
    val featureSeparation: Double,
    val logoLoad: Double,
    val message: String,
    /** The style to render (repaired under ADAPTIVE). */
    val effectiveStyle: CKeyStyle,
)
