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
    H(3),
    /**
     * Ultra: about 40% (80% of each block is parity). Carries about half of
     * [H]'s payload. Readers before 0.1.0-build.86 reject it.
     */
    U(4),
    /**
     * Extreme: about 47.5% (95% parity). Carries about 5% of [L]'s payload,
     * and nothing at all in the smallest versions (`CKeySymbol.capacity`
     * returns 0 and encoding there fails). Readers before 0.1.0-build.86
     * reject it.
     */
    X(5);

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
    PRINT(1),
    /**
     * Format 0.29: [STANDARD]'s geometry and capacity with byte-grouped
     * placement, so a blot or logo costs only the bytes under it. Readers
     * before 0.1.0-build.86 cannot read it; opt-in.
     */
    STANDARD_GROUPED(2),
    /** Format 0.30: [PRINT]'s geometry and capacity, byte-grouped. Readers before 0.1.0-build.86 cannot read it; opt-in. */
    PRINT_GROUPED(3),
    /** Format 0.31 (experimental): the pixel geometry. Readers before 0.1.0-build.98 cannot read it. */
    PIXEL(4),
    /**
     * Format 0.32: [STANDARD]'s geometry with equal-arc rings (inner-ring marks as far apart as the
     * outermost ring's), about 10% less capacity. Readers before 0.1.0-build.110 cannot read it. [AUTO], the
     * default, uses [STANDARD_ARC_GROUPED] from v8; this scattered form only when asked.
     */
    STANDARD_ARC(5),
    /** Format 0.33: [STANDARD_ARC]'s geometry, byte-grouped. Opt-in. */
    STANDARD_ARC_GROUPED(6),
    /**
     * Encoding only (the default): [STANDARD] for versions 1-7, [STANDARD_ARC_GROUPED] from v8
     * (the core's CKEY_LAYOUT_AUTO). A symbol and a decoder report the layout actually used.
     */
    AUTO(255);

    /** True for the print geometry ("P0.1"): [PRINT] and [PRINT_GROUPED]. */
    val isPrint: Boolean get() = this == PRINT || this == PRINT_GROUPED
    /** True for byte-grouped placement: [STANDARD_GROUPED] and [PRINT_GROUPED]. */
    val isGrouped: Boolean get() = this == STANDARD_GROUPED || this == PRINT_GROUPED || this == STANDARD_ARC_GROUPED
    /** True for the equal-arc ring geometry: [STANDARD_ARC] and [STANDARD_ARC_GROUPED]. */
    val isArc: Boolean get() = this == STANDARD_ARC || this == STANDARD_ARC_GROUPED

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
