package org.openckey.ckey

/** Reed–Solomon correction level of a Profile 0 symbol (`ckey_ecc_level`). */
enum class EccLevel(val code: Int) {
    /** About 7% of each block correctable. */
    L(0),
    /** About 14% (the default). */
    M(1),
    /** About 22%. */
    Q(2),
    /** About 30%. */
    H(3);

    companion object {
        @JvmStatic fun fromCode(code: Int): EccLevel = entries.firstOrNull { it.code == code }
            ?: throw IllegalArgumentException("unknown ECC level $code")
    }
}

/** Profile 0 layout family (`ckey_scalable_layout`). */
enum class Layout(val code: Int) {
    /** Format 0.27, the screen layout. */
    STANDARD(0),
    /** Format 0.28 ("P0.1"): every mark and gap at least about one module, for small prints. */
    PRINT(1);

    companion object {
        @JvmStatic fun fromCode(code: Int): Layout = entries.firstOrNull { it.code == code }
            ?: throw IllegalArgumentException("unknown layout $code")
    }
}

/** Raster pixel policy (`ckey_raster_policy`); benchmark them, none is universally best. */
enum class RasterPolicy(val code: Int) {
    /** Antialiased: each pixel's ink is its supersampled area coverage (default). */
    COVERAGE(0),
    /** Each pixel is ink when at least half its samples are covered. */
    BINARY(1),
    /** Coverage, with marks thinner than the minimum feature widened to it. */
    HYBRID(2),
}

/** Optical-safety policy carried by a style (`ckey_style_policy`, specification §4). */
enum class StylePolicy(val code: Int) {
    /** Renderers refuse a style that fails the safety rules (default). */
    STRICT(0),
    /** Renderers repair the style by fixed rules, then apply the strict rules. */
    ADAPTIVE(1),
    /** Renderers draw the style as given; the caller accepts an uncertified symbol. */
    OVERRIDE(2);

    companion object {
        @JvmStatic fun fromCode(code: Int): StylePolicy = entries.firstOrNull { it.code == code } ?: STRICT
    }
}

/** Payload mark shape (`ckey_module_shape`). */
enum class ModuleShape(val code: Int) { ROUNDED(0), SQUARE(1), CIRCLE(2), DIAMOND(3);
    companion object { @JvmStatic fun fromCode(code: Int): ModuleShape = entries.firstOrNull { it.code == code } ?: ROUNDED }
}

/** Ring mark style (`ckey_ring_style`). */
enum class RingStyle(val code: Int) { JOINED(0), DOTS(1);
    companion object { @JvmStatic fun fromCode(code: Int): RingStyle = entries.firstOrNull { it.code == code } ?: JOINED }
}

/** Ray end cap (`ckey_ray_cap`). */
enum class RayCap(val code: Int) { ROUND(0), FLAT(1);
    companion object { @JvmStatic fun fromCode(code: Int): RayCap = entries.firstOrNull { it.code == code } ?: ROUND }
}

/**
 * The optical profile a symbol was decoded from. [code] is the canonical
 * telemetry profile code (`ckey_telemetry_profile`).
 */
enum class CKeyProfile(val code: Int, val label: String) {
    P0(0, "Profile 0"),
    P0_1(1, "Profile 0.1 (print)"),
    P1(2, "Profile 1 (locator)"),
    P2(3, "Profile 2 (text)"),
    P3(4, "Profile 3 (extended)");

    companion object {
        @JvmStatic fun fromCode(code: Int): CKeyProfile = entries.firstOrNull { it.code == code }
            ?: throw IllegalArgumentException("unknown profile $code")
    }
}
