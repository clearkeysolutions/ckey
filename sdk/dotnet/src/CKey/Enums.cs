using System;

namespace OpenCKey
{
    /// <summary>Status codes of the C core (<c>ckey_status</c>).</summary>
    public enum CKeyStatus
    {
        /// <summary>CKEY_OK.</summary>
        Ok = 0,
        /// <summary>CKEY_INVALID_ARGUMENT.</summary>
        InvalidArgument = 1,
        /// <summary>CKEY_UNSUPPORTED_PROFILE.</summary>
        UnsupportedProfile = 2,
        /// <summary>CKEY_INSUFFICIENT_BUFFER.</summary>
        InsufficientBuffer = 3,
        /// <summary>CKEY_INTERNAL_ERROR.</summary>
        InternalError = 4,
        /// <summary>CKEY_UNCORRECTABLE: too many errors for Reed–Solomon.</summary>
        Uncorrectable = 5,
        /// <summary>CKEY_INTEGRITY_FAILURE: the CRC did not match.</summary>
        IntegrityFailure = 6,
        /// <summary>CKEY_EXPIRED: a Safe Action outside its lifetime, or longer-lived than allowed.</summary>
        Expired = 7,
    }

    /// <summary>Error-correction level of a Profile 0 symbol (<c>ckey_ecc_level</c>).</summary>
    public enum EccLevel
    {
        /// <summary>About 7% of each block correctable.</summary>
        L = 0,
        /// <summary>About 14% (the default).</summary>
        M = 1,
        /// <summary>About 22%.</summary>
        Q = 2,
        /// <summary>About 30%.</summary>
        H = 3,
    }

    /// <summary>Profile 0 layout family (<c>ckey_scalable_layout</c>).</summary>
    public enum Layout
    {
        /// <summary>Format 0.27, the screen layout.</summary>
        Standard = 0,
        /// <summary>Format 0.28 ("Profile 0.1"): every mark and gap at least about one module, for small prints.</summary>
        Print = 1,
    }

    /// <summary>How the raster backend turns geometry into pixels (<c>ckey_raster_policy</c>).</summary>
    public enum RasterPolicy
    {
        /// <summary>Antialiased: each pixel's ink is its supersampled area coverage.</summary>
        Coverage = 0,
        /// <summary>Each pixel is ink when at least half its samples are covered.</summary>
        Binary = 1,
        /// <summary>Coverage, with marks thinner than the minimum feature widened to it.</summary>
        Hybrid = 2,
    }

    /// <summary>Shape of payload modules (<c>ckey_module_shape</c>).</summary>
    public enum ModuleShape
    {
        /// <summary>Rounded square (default).</summary>
        Rounded = 0,
        /// <summary>Square.</summary>
        Square = 1,
        /// <summary>Circle.</summary>
        Circle = 2,
        /// <summary>Diamond.</summary>
        Diamond = 3,
    }

    /// <summary>How ring marks are drawn (<c>ckey_ring_style</c>).</summary>
    public enum RingStyle
    {
        /// <summary>Adjacent dots join into arcs (default).</summary>
        Joined = 0,
        /// <summary>Every ring mark is a separate dot.</summary>
        Dots = 1,
    }

    /// <summary>Ray end caps (<c>ckey_ray_cap</c>).</summary>
    public enum RayCap
    {
        /// <summary>Round caps (default).</summary>
        Round = 0,
        /// <summary>Flat caps.</summary>
        Flat = 1,
    }

    /// <summary>Optical-safety policy carried by a style (<c>ckey_style_policy</c>).</summary>
    public enum StylePolicy
    {
        /// <summary>Renderers refuse a style that fails the safety rules (default).</summary>
        Strict = 0,
        /// <summary>Renderers repair the style by fixed rules, then apply the strict rules.</summary>
        Adaptive = 1,
        /// <summary>Renderers draw the style as given; the caller accepts an uncertified symbol.</summary>
        Override = 2,
    }

    /// <summary>Named style presets (<c>ckey_style_preset</c>).</summary>
    public enum CKeyStylePreset
    {
        /// <summary>Black ink on white: the default style.</summary>
        Plain = 0,
        /// <summary>Cream metallic ink on a dark vignette, with accents, depth and grain.</summary>
        Concept = 1,
    }

    /// <summary>The CKey profile a symbol was decoded as.</summary>
    public enum CKeyProfile
    {
        /// <summary>Profile 0, the scalable family (versions 1..31, standard or print layout).</summary>
        Scalable = 0,
        /// <summary>Profile 1, the 16-byte locator.</summary>
        Locator = 1,
        /// <summary>Profile 2, text (up to 200 bytes).</summary>
        Text = 2,
        /// <summary>Profile 3, extended (up to 2,048 bytes).</summary>
        Extended = 3,
    }

    /// <summary>Changes the adaptive policy made to a style (<c>CKEY_ADJUST_*</c>).</summary>
    [Flags]
    public enum StyleAdjustments
    {
        /// <summary>No change.</summary>
        None = 0,
        /// <summary>Ink replaced by black or white.</summary>
        Ink = 1,
        /// <summary>Accents removed.</summary>
        Accents = 2,
        /// <summary>Bevel or shadow limited.</summary>
        Effects = 4,
        /// <summary>Logo plated or shrunk.</summary>
        Logo = 8,
        /// <summary>The symbol was given its own background.</summary>
        Background = 16,
        /// <summary>Sheen, vignette, depth and grain removed after a failed model test.</summary>
        Materials = 32,
    }
}
