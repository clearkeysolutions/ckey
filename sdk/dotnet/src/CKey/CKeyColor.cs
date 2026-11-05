using System;
using System.Globalization;
using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>An opaque sRGB colour (<c>ckey_rgb</c>).</summary>
    public readonly struct CKeyColor : IEquatable<CKeyColor>
    {
        /// <summary>Creates a colour from its channels.</summary>
        public CKeyColor(byte r, byte g, byte b)
        {
            R = r;
            G = g;
            B = b;
        }

        /// <summary>Red channel.</summary>
        public byte R { get; }
        /// <summary>Green channel.</summary>
        public byte G { get; }
        /// <summary>Blue channel.</summary>
        public byte B { get; }

        /// <summary>Black.</summary>
        public static CKeyColor Black => new CKeyColor(0, 0, 0);
        /// <summary>White.</summary>
        public static CKeyColor White => new CKeyColor(255, 255, 255);

        /// <summary>From 0xRRGGBB.</summary>
        public static CKeyColor FromRgb(int rgb) => new CKeyColor((byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);

        /// <summary>Parses "#rrggbb" or "rrggbb".</summary>
        public static CKeyColor Parse(string hex)
        {
            if (hex == null) throw new ArgumentNullException(nameof(hex));
            string s = hex.StartsWith("#", StringComparison.Ordinal) ? hex.Substring(1) : hex;
            if (s.Length != 6 || !int.TryParse(s, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out int v))
                throw new FormatException($"'{hex}' is not a #rrggbb colour.");
            return FromRgb(v);
        }

        /// <summary>"#rrggbb".</summary>
        public override string ToString() => $"#{R:x2}{G:x2}{B:x2}";

        /// <inheritdoc />
        public bool Equals(CKeyColor other) => R == other.R && G == other.G && B == other.B;
        /// <inheritdoc />
        public override bool Equals(object? obj) => obj is CKeyColor c && Equals(c);
        /// <inheritdoc />
        public override int GetHashCode() => (R << 16) | (G << 8) | B;
        /// <summary>Equality.</summary>
        public static bool operator ==(CKeyColor a, CKeyColor b) => a.Equals(b);
        /// <summary>Inequality.</summary>
        public static bool operator !=(CKeyColor a, CKeyColor b) => !a.Equals(b);

        internal NativeRgb ToNative() => new NativeRgb { R = R, G = G, B = B };
        internal static CKeyColor FromNative(NativeRgb c) => new CKeyColor(c.R, c.G, c.B);
    }
}
