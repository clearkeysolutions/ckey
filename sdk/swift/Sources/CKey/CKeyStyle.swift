import CKeyCodec
import CKeyScanKit
import Foundation

/// An sRGB colour, 8 bits per channel.
public struct CKeyColor: Hashable, Sendable {
    public var red: UInt8
    public var green: UInt8
    public var blue: UInt8

    public init(red: UInt8, green: UInt8, blue: UInt8) {
        self.red = red
        self.green = green
        self.blue = blue
    }

    /// From a 0xRRGGBB value.
    public init(hex: UInt32) {
        self.init(red: UInt8((hex >> 16) & 0xff), green: UInt8((hex >> 8) & 0xff), blue: UInt8(hex & 0xff))
    }

    public static let black = CKeyColor(red: 0, green: 0, blue: 0)
    public static let white = CKeyColor(red: 255, green: 255, blue: 255)

    init(_ c: ckey_rgb) { self.init(red: c.r, green: c.g, blue: c.b) }
    var c: ckey_rgb { ckey_rgb(r: red, g: green, b: blue) }
}

/// Optical-safety policy carried by a style (specification §4).
public enum CKeyStylePolicy: Int, Sendable, CaseIterable {
    /// Default: renderers refuse a style that fails the safety rules.
    case strict = 0
    /// Renderers repair the style by fixed rules, then apply the strict rules.
    case adaptive = 1
    /// Renderers draw the style as given; the caller accepts an uncertified symbol.
    case override = 2
}

/// The Profile 0 design system (`ckey_style`). Styling changes how a symbol
/// is painted, never where a reader samples; the core checks every style
/// (contrast, effect limits, logo error budget) and refuses unsafe ones
/// under the `.strict` policy.
public struct CKeyStyle: Hashable, Sendable {
    public enum ModuleShape: Int, Sendable, CaseIterable { case rounded = 0, square = 1, circle = 2, diamond = 3 }
    public enum RingStyle: Int, Sendable, CaseIterable { case joined = 0, dots = 1 }
    public enum RayCap: Int, Sendable, CaseIterable { case round = 0, flat = 1 }

    /// Background centre colour.
    public var background: CKeyColor
    /// Background edge colour (radial vignette); equal to `background` for solid.
    public var backgroundEdge: CKeyColor
    /// Mark colour; the light side of a metallic sheen.
    public var ink: CKeyColor
    /// Sheen shade; equal to `ink` for flat ink.
    public var inkShade: CKeyColor
    /// Up to four accent colours for payload marks.
    public var accents: [CKeyColor]
    /// 0...100: share of payload marks drawn in accents.
    public var accentPercent: Int
    public var modules: ModuleShape
    public var rings: RingStyle
    public var rays: RayCap
    /// 0...1 embossed highlight.
    public var bevel: Double
    /// 0...1 drop-shadow depth.
    public var shadow: Double
    /// 0...1 background grain.
    public var texture: Double
    /// Logo image URL or `data:` URI for SVG export; `nil` for none. The
    /// raster, PNG and PDF outputs draw only the logo plate.
    public var logoHref: String?
    /// Logo radius in canonical units; 0 = the empty centre's size.
    public var logoRadius: Double
    /// Draw a background disc behind the logo.
    public var logoPlate: Bool
    /// Optical-safety policy; `.strict` by default.
    public var policy: CKeyStylePolicy

    /// Black ink on a white background (`ckey_style_default`).
    public static var plain: CKeyStyle { preset(CKEY_PRESET_PLAIN) }
    /// Cream metallic ink on a dark vignette, with accents, depth and grain.
    public static var concept: CKeyStyle { preset(CKEY_PRESET_CONCEPT) }

    /// The default style (same as `.plain`).
    public init() {
        var c = ckey_style()
        ckey_style_default(&c)
        self.init(c)
    }

    private static func preset(_ preset: ckey_style_preset) -> CKeyStyle {
        var c = ckey_style()
        ckey_style_default(&c)
        _ = ckey_style_preset_fill(&c, preset)
        return CKeyStyle(c)
    }

    init(_ c: ckey_style) {
        background = CKeyColor(c.background)
        backgroundEdge = CKeyColor(c.background_edge)
        ink = CKeyColor(c.ink)
        inkShade = CKeyColor(c.ink_shade)
        let count = Int(min(c.accent_count, 4))
        accents = withUnsafeBytes(of: c.accents) { raw in
            raw.bindMemory(to: ckey_rgb.self).prefix(count).map { CKeyColor($0) }
        }
        accentPercent = Int(c.accent_percent)
        modules = ModuleShape(rawValue: Int(c.modules.rawValue)) ?? .rounded
        rings = RingStyle(rawValue: Int(c.rings.rawValue)) ?? .joined
        rays = RayCap(rawValue: Int(c.rays.rawValue)) ?? .round
        bevel = c.bevel
        shadow = c.shadow
        texture = c.texture
        logoHref = c.logo_href.map { String(cString: $0) }.flatMap { $0.isEmpty ? nil : $0 }
        logoRadius = c.logo_radius
        logoPlate = c.logo_plate != 0
        policy = CKeyStylePolicy(rawValue: Int(c.policy)) ?? .strict
    }

    /// Calls `body` with the equivalent `ckey_style`; the logo string is
    /// valid only inside `body`. Values that do not fit the C fields are
    /// clamped so the core's own checks report them.
    func withCStyle<R>(_ body: (UnsafePointer<ckey_style>) throws -> R) rethrows -> R {
        var c = ckey_style()
        ckey_style_default(&c)  // fills every field of the current ABI first
        c.background = background.c
        c.background_edge = backgroundEdge.c
        c.ink = ink.c
        c.ink_shade = inkShade.c
        var slots = accents.prefix(4).map(\.c)
        let accentCount = slots.count
        while slots.count < 4 { slots.append(ckey_rgb(r: 0, g: 0, b: 0)) }
        c.accents = (slots[0], slots[1], slots[2], slots[3])
        c.accent_count = UInt32(accentCount)
        c.accent_percent = UInt32(clamping: accentPercent)
        c.modules = ckey_module_shape(rawValue: UInt32(modules.rawValue))
        c.rings = ckey_ring_style(rawValue: UInt32(rings.rawValue))
        c.rays = ckey_ray_cap(rawValue: UInt32(rays.rawValue))
        c.bevel = bevel
        c.shadow = shadow
        c.texture = texture
        c.logo_radius = logoRadius
        c.logo_plate = logoPlate ? 1 : 0
        c.policy = UInt32(policy.rawValue)
        if let logoHref, !logoHref.isEmpty {
            return try logoHref.withCString { href in
                c.logo_href = href
                return try withUnsafePointer(to: &c) { try body($0) }
            }
        }
        c.logo_href = nil
        return try withUnsafePointer(to: &c) { try body($0) }
    }
}
