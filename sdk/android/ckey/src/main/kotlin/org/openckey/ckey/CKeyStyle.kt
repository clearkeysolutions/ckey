package org.openckey.ckey

/** Named style presets (`ckey_style_preset`). */
enum class StylePreset(val code: Int) {
    /** The default style: black ink on white. */
    PLAIN(0),
    /** Cream metallic ink on a dark vignette, accents, depth and grain. */
    CONCEPT(1),
}

/**
 * A logo bitmap for the raster backend: straight (non-premultiplied) RGBA8,
 * tightly packed, `width * height * 4` bytes. On Android build one from a
 * Bitmap with [CKeyLogo.fromBitmap].
 */
class CKeyLogo(val width: Int, val height: Int, rgba: ByteArray) {
    internal val rgba: ByteArray = rgba.copyOf()

    init {
        require(width > 0 && height > 0) { "logo size must be positive" }
        require(rgba.size.toLong() == width.toLong() * height * 4) { "logo needs width * height * 4 bytes" }
    }

    companion object
}

/**
 * Profile 0 design style (`ckey_style`). Styling changes how a symbol is
 * painted, never where a reader samples. The core checks every style
 * (contrast, effect limits, logo error budget) and, under
 * [StylePolicy.STRICT], refuses one that fails with a [CKeyException] that
 * explains why.
 *
 * Colours are `0xRRGGBB` (an alpha byte, as in `android.graphics.Color`, is
 * ignored). Start from [DEFAULT] or a preset and use `copy(...)`.
 */
data class CKeyStyle(
    val background: Int = 0xFFFFFF,
    val backgroundEdge: Int = background,
    val ink: Int = 0x000000,
    val inkShade: Int = ink,
    val accents: List<Int> = emptyList(),
    val accentPercent: Int = 0,
    val modules: ModuleShape = ModuleShape.ROUNDED,
    val rings: RingStyle = RingStyle.JOINED,
    val rays: RayCap = RayCap.ROUND,
    val bevel: Double = 0.0,
    val shadow: Double = 0.0,
    val texture: Double = 0.0,
    /** Logo image URL or data: URI for SVG export; null for none. */
    val logoHref: String? = null,
    /** Logo pixels for the raster backend (PNG, Bitmap); null for none. */
    val logo: CKeyLogo? = null,
    /** Canonical units; 0 = the empty centre's size. */
    val logoRadius: Double = 0.0,
    val logoPlate: Boolean = false,
    val policy: StylePolicy = StylePolicy.STRICT,
) {
    init {
        require(accents.size <= 4) { "at most 4 accent colours" }
    }

    internal fun ints(): IntArray = IntArray(CKeyNative.STYLE_INTS).also { i ->
        i[0] = background and 0xFFFFFF
        i[1] = backgroundEdge and 0xFFFFFF
        i[2] = ink and 0xFFFFFF
        i[3] = inkShade and 0xFFFFFF
        for (k in 0 until 4) i[4 + k] = (accents.getOrNull(k) ?: 0) and 0xFFFFFF
        i[8] = accents.size
        i[9] = accentPercent
        i[10] = modules.code
        i[11] = rings.code
        i[12] = rays.code
        i[13] = if (logoPlate) 1 else 0
        i[14] = policy.code
    }

    internal fun doubles(): DoubleArray = doubleArrayOf(bevel, shadow, texture, logoRadius)

    companion object {
        internal fun fromNative(i: IntArray, d: DoubleArray, logoHref: String? = null, logo: CKeyLogo? = null) = CKeyStyle(
            background = i[0], backgroundEdge = i[1], ink = i[2], inkShade = i[3],
            accents = (0 until i[8].coerceIn(0, 4)).map { i[4 + it] },
            accentPercent = i[9],
            modules = ModuleShape.fromCode(i[10]), rings = RingStyle.fromCode(i[11]), rays = RayCap.fromCode(i[12]),
            bevel = d[0], shadow = d[1], texture = d[2],
            logoHref = logoHref, logo = logo, logoRadius = d[3], logoPlate = i[13] != 0,
            policy = StylePolicy.fromCode(i[14]),
        )

        /** A preset as filled by the core (`ckey_style_preset_fill`). */
        @JvmStatic
        fun preset(preset: StylePreset): CKeyStyle {
            CKeyNative.require(CKeyNative.FEATURE_RENDER, "the renderer")
            val i = IntArray(CKeyNative.STYLE_INTS)
            val d = DoubleArray(CKeyNative.STYLE_DOUBLES)
            CKeyNative.nativeStylePreset(preset.code, i, d)
            return fromNative(i, d)
        }

        /** The core's default style (`ckey_style_default`): black ink on white. */
        @JvmStatic val DEFAULT: CKeyStyle by lazy { preset(StylePreset.PLAIN) }
        @JvmStatic val PLAIN: CKeyStyle get() = DEFAULT
        @JvmStatic val CONCEPT: CKeyStyle by lazy { preset(StylePreset.CONCEPT) }
    }
}
