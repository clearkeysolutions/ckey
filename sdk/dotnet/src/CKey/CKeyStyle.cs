using System;
using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>
    /// How a Profile 0 symbol is painted (<c>ckey_style</c>). Styling never
    /// moves where a reader samples: site geometry, the frame, finders, keys
    /// and the quiet margin are fixed. The core checks every style
    /// (<see cref="CKeySymbol.CheckStyle"/>) and, under the default
    /// <see cref="StylePolicy.Strict"/> policy, renderers refuse one that fails.
    /// </summary>
    public sealed class CKeyStyle
    {
        private CKeyColor[] _accents = Array.Empty<CKeyColor>();

        /// <summary>A default style: black ink on white (the same as <see cref="Plain"/>).</summary>
        public CKeyStyle()
        {
            unsafe
            {
                NativeStyle s;
                Native.EnsureAbi();
                NativeMethods.ckey_style_default(&s);
                Load(in s);
            }
        }

        /// <summary>A new copy of the <see cref="CKeyStylePreset.Plain"/> preset.</summary>
        public static CKeyStyle Plain => FromPreset(CKeyStylePreset.Plain);

        /// <summary>A new copy of the <see cref="CKeyStylePreset.Concept"/> preset.</summary>
        public static CKeyStyle Concept => FromPreset(CKeyStylePreset.Concept);

        /// <summary>A new style filled from a named preset.</summary>
        public static unsafe CKeyStyle FromPreset(CKeyStylePreset preset)
        {
            Native.EnsureAbi();
            NativeStyle s;
            Native.Check(NativeMethods.ckey_style_preset_fill(&s, (int)preset), "ckey_style_preset_fill");
            var style = new CKeyStyle(skipDefault: true);
            style.Load(in s);
            return style;
        }

        private CKeyStyle(bool skipDefault)
        {
            _ = skipDefault;
        }

        /// <summary>Background colour at the centre.</summary>
        public CKeyColor Background { get; set; }
        /// <summary>Background colour at the edge (radial vignette); equal to <see cref="Background"/> for a solid fill.</summary>
        public CKeyColor BackgroundEdge { get; set; }
        /// <summary>Mark colour; the light side of a metallic sheen.</summary>
        public CKeyColor Ink { get; set; }
        /// <summary>Sheen shade across the symbol; equal to <see cref="Ink"/> for flat ink.</summary>
        public CKeyColor InkShade { get; set; }

        /// <summary>Up to four accent colours for payload marks.</summary>
        public CKeyColor[] Accents
        {
            get => (CKeyColor[])_accents.Clone();
            set
            {
                if (value == null) throw new ArgumentNullException(nameof(value));
                if (value.Length > 4) throw new ArgumentOutOfRangeException(nameof(value), "A style has at most four accents.");
                _accents = (CKeyColor[])value.Clone();
            }
        }

        /// <summary>0..100: share of payload marks drawn in accent colours.</summary>
        public int AccentPercent { get; set; }
        /// <summary>Payload module shape.</summary>
        public ModuleShape Modules { get; set; }
        /// <summary>Ring mark style.</summary>
        public RingStyle Rings { get; set; }
        /// <summary>Ray caps.</summary>
        public RayCap Rays { get; set; }
        /// <summary>0..1 embossed highlight.</summary>
        public double Bevel { get; set; }
        /// <summary>0..1 drop-shadow depth.</summary>
        public double Shadow { get; set; }
        /// <summary>0..1 background grain.</summary>
        public double Texture { get; set; }
        /// <summary>Logo image URL or data: URI (SVG export); null or empty for none.</summary>
        public string? LogoHref { get; set; }
        /// <summary>Logo radius in canonical units; 0 = the empty centre's size.</summary>
        public double LogoRadius { get; set; }
        /// <summary>Draw a background disc behind the logo.</summary>
        public bool LogoPlate { get; set; }
        /// <summary>Optical-safety policy (default <see cref="StylePolicy.Strict"/>).</summary>
        public StylePolicy Policy { get; set; }

        /// <summary>A deep copy.</summary>
        public CKeyStyle Clone()
        {
            var c = (CKeyStyle)MemberwiseClone();
            c._accents = (CKeyColor[])_accents.Clone();
            return c;
        }

        internal unsafe void Load(in NativeStyle s)
        {
            Background = CKeyColor.FromNative(s.Background);
            BackgroundEdge = CKeyColor.FromNative(s.BackgroundEdge);
            Ink = CKeyColor.FromNative(s.Ink);
            InkShade = CKeyColor.FromNative(s.InkShade);
            int count = (int)Math.Min(s.AccentCount, 4u);
            var all = new[] { s.Accent0, s.Accent1, s.Accent2, s.Accent3 };
            _accents = new CKeyColor[count];
            for (int i = 0; i < count; i++) _accents[i] = CKeyColor.FromNative(all[i]);
            AccentPercent = (int)s.AccentPercent;
            Modules = (ModuleShape)s.Modules;
            Rings = (RingStyle)s.Rings;
            Rays = (RayCap)s.Rays;
            Bevel = s.Bevel;
            Shadow = s.Shadow;
            Texture = s.Texture;
            LogoHref = Native.FromUtf8Z((byte*)s.LogoHref);
            LogoRadius = s.LogoRadius;
            LogoPlate = s.LogoPlate != 0;
            Policy = (StylePolicy)s.Policy;
        }

        /// <summary>UTF-8, NUL-terminated logo href to pin for the duration of a native call.</summary>
        internal byte[]? LogoHrefUtf8Z() => string.IsNullOrEmpty(LogoHref) ? null : Native.ToUtf8Z(LogoHref);

        /// <summary>Fills a native style. logoHref must stay pinned while the native style is in use.</summary>
        internal NativeStyle ToNative(IntPtr logoHref)
        {
            if (AccentPercent < 0) throw new ArgumentOutOfRangeException(nameof(AccentPercent));
            var a = new NativeRgb[4];
            for (int i = 0; i < _accents.Length; i++) a[i] = _accents[i].ToNative();
            return new NativeStyle
            {
                Background = Background.ToNative(),
                BackgroundEdge = BackgroundEdge.ToNative(),
                Ink = Ink.ToNative(),
                InkShade = InkShade.ToNative(),
                Accent0 = a[0],
                Accent1 = a[1],
                Accent2 = a[2],
                Accent3 = a[3],
                AccentCount = (uint)_accents.Length,
                AccentPercent = (uint)AccentPercent,
                Modules = (int)Modules,
                Rings = (int)Rings,
                Rays = (int)Rays,
                Bevel = Bevel,
                Shadow = Shadow,
                Texture = Texture,
                LogoHref = logoHref,
                LogoRadius = LogoRadius,
                LogoPlate = LogoPlate ? 1u : 0u,
                Policy = (uint)Policy,
            };
        }
    }
}
