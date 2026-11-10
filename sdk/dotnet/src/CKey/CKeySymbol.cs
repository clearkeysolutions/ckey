using System;
using System.Collections.Generic;
using System.Text;
using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>
    /// An encoded Profile 0 (scalable) symbol. This is a managed copy of the C
    /// <c>ckey_scalable_symbol</c> (8,216 bytes, no native resources), so it
    /// needs no disposal and is safe to share between threads.
    /// </summary>
    public sealed unsafe class CKeySymbol
    {
        private NativeScalableSymbol _symbol;

        private CKeySymbol()
        {
        }

        /// <summary>Highest Profile 0 version.</summary>
        public const int MaxVersion = NativeConstants.ScalableVersionCount;

        /// <summary>Encodes UTF-8 text.</summary>
        /// <param name="text">The payload text; encoded as UTF-8 without a BOM.</param>
        /// <param name="version">1..31, or 0 for the smallest version that fits.</param>
        /// <param name="level">Error-correction level.</param>
        /// <param name="layout">Standard (screen) or print layout, or their byte-grouped forms (opt-in: readers before build 86 cannot read those).</param>
        public static CKeySymbol Encode(string text, int version = 0, EccLevel level = EccLevel.M, Layout layout = Layout.Standard)
        {
            if (text == null) throw new ArgumentNullException(nameof(text));
            return Encode(new UTF8Encoding(false).GetBytes(text), version, level, layout);
        }

        /// <summary>Encodes bytes.</summary>
        /// <param name="payload">The payload bytes.</param>
        /// <param name="version">1..31, or 0 for the smallest version that fits.</param>
        /// <param name="level">Error-correction level.</param>
        /// <param name="layout">Standard (screen) or print layout, or their byte-grouped forms (opt-in: readers before build 86 cannot read those).</param>
        public static CKeySymbol Encode(ReadOnlySpan<byte> payload, int version = 0, EccLevel level = EccLevel.M, Layout layout = Layout.Standard)
        {
            if (version < 0 || version > MaxVersion) throw new ArgumentOutOfRangeException(nameof(version), "version must be 0 (auto) or 1..31.");
            if (level < EccLevel.L || level > EccLevel.X) throw new ArgumentOutOfRangeException(nameof(level));
            if (layout < Layout.Standard || layout > Layout.PrintGrouped) throw new ArgumentOutOfRangeException(nameof(layout));
            Native.EnsureAbi();
            var symbol = new CKeySymbol();
            NativeDiagnostics diagnostics = default;
            byte empty = 0;
            fixed (byte* p = payload)
            fixed (NativeScalableSymbol* s = &symbol._symbol)
            {
                byte* data = p != null ? p : &empty;
                int status = NativeMethods.ckey_encode_scalable(data, (nuint)payload.Length, (uint)version, (int)level, (int)layout, s, &diagnostics);
                Native.Check(status, "ckey_encode_scalable", ref diagnostics);
            }
            return symbol;
        }

        /// <summary>Maximum payload bytes for a version, level and layout; 0 if invalid or if the version carries nothing at that level (small versions at U or X).</summary>
        public static int Capacity(int version, EccLevel level = EccLevel.M, Layout layout = Layout.Standard)
        {
            if (version < 1 || version > MaxVersion) return 0;
            Native.EnsureAbi();
            return (int)NativeMethods.ckey_scalable_capacity((uint)version, (int)level, (int)layout);
        }

        /// <summary>Modules across the field for a version (29..232); 0 if invalid.</summary>
        public static int ModulesAcrossFor(int version)
        {
            if (version < 1 || version > MaxVersion) return 0;
            Native.EnsureAbi();
            return (int)NativeMethods.ckey_scalable_modules_across((uint)version);
        }

        /// <summary>Largest logo radius (canonical units) for a version and layout.</summary>
        public static double LogoMaxRadius(int version, Layout layout = Layout.Standard)
        {
            Native.EnsureAbi();
            return NativeMethods.ckey_scalable_logo_max_radius((uint)version, (int)layout);
        }

        /// <summary>Version 1..31.</summary>
        public int Version => (int)_symbol.Version;
        /// <summary>Error-correction level.</summary>
        public EccLevel EccLevel => (EccLevel)_symbol.EccLevel;
        /// <summary>Layout family.</summary>
        public Layout Layout => (Layout)_symbol.Layout;
        /// <summary>Payload length in bytes.</summary>
        public int PayloadLength => (int)_symbol.PayloadLength;
        /// <summary>Codeword bytes in use.</summary>
        public int CodewordLength => (int)_symbol.CodewordLength;
        /// <summary>Modules across the field (29..232).</summary>
        public int ModulesAcross => ModulesAcrossFor(Version);

        /// <summary>A copy of the codeword bytes in use.</summary>
        public byte[] GetCodeword()
        {
            int n = Math.Min(CodewordLength, NativeConstants.ScalableMaxCodewordBytes);
            var bytes = new byte[n];
            fixed (NativeScalableSymbol* s = &_symbol)
            {
                for (int i = 0; i < n; i++) bytes[i] = s->Codeword[i];
            }
            return bytes;
        }

        /// <summary>Share of the worst block's correction capacity hidden under a logo of this radius.</summary>
        public double LogoLoad(double logoRadius)
        {
            fixed (NativeScalableSymbol* s = &_symbol) return NativeMethods.ckey_scalable_logo_load(s, logoRadius);
        }

        // ---- Style check and certification ---------------------------------

        /// <summary>
        /// Checks a style against this symbol (contrast, effect limits, logo
        /// error budget). Returns true when renderers would accept it; otherwise
        /// <paramref name="message"/> explains the refusal.
        /// </summary>
        public bool CheckStyle(CKeyStyle style, out string message)
        {
            if (style == null) throw new ArgumentNullException(nameof(style));
            NativeDiagnostics diagnostics = default;
            byte[]? href = style.LogoHrefUtf8Z();
            int status;
            fixed (byte* h = href)
            fixed (NativeScalableSymbol* s = &_symbol)
            {
                NativeStyle ns = style.ToNative((IntPtr)h);
                status = NativeMethods.ckey_check_scalable_style(s, &ns, &diagnostics);
            }
            message = Native.Message(ref diagnostics);
            return status == 0;
        }

        /// <summary>
        /// Certifies a style by rendering it, compositing it over the backdrops,
        /// degrading it with the scan model and decoding it with the real decoder
        /// (<c>ckey_certify_scalable_style</c>). A transparent symbol needs a
        /// backdrop (colours and/or the host's pixels behind it); without one it
        /// is never certified.
        /// </summary>
        /// <param name="style">The style, or null for the default; its <see cref="CKeyStyle.Policy"/> decides strict/adaptive/override.</param>
        /// <param name="transparent">Certify transparent output.</param>
        /// <param name="backdropColours">Up to 16 solid colours the symbol may sit on.</param>
        /// <param name="backdropImage">Host pixels behind the symbol, stretched over its full canvas.</param>
        /// <param name="model">The acquisition model, or null for the default phone-camera model.</param>
        public CKeyCertification Certify(CKeyStyle? style = null, bool transparent = false,
                                         IReadOnlyList<CKeyColor>? backdropColours = null, CKeyImage? backdropImage = null,
                                         CKeyScanModel? model = null)
        {
            if (backdropColours != null && backdropColours.Count > 16)
                throw new ArgumentOutOfRangeException(nameof(backdropColours), "At most 16 backdrop colours.");
            Native.EnsureAbi();
            NativeRgb[]? colours = null;
            if (backdropColours != null && backdropColours.Count > 0)
            {
                colours = new NativeRgb[backdropColours.Count];
                for (int i = 0; i < colours.Length; i++) colours[i] = backdropColours[i].ToNative();
            }
            NativeScanModel nm;
            NativeMethods.ckey_scan_model_default(&nm);
            if (model != null) model.Apply(ref nm);

            byte[]? href = style?.LogoHrefUtf8Z();
            NativeCertification cert = default;
            NativeStyle adjusted = default;
            int status;
            CKeyStyle? adjustedStyle = null;
            fixed (byte* h = href)
            fixed (NativeRgb* c = colours)
            fixed (byte* bg = backdropImage?.Rgba)
            fixed (NativeScalableSymbol* s = &_symbol)
            {
                NativeStyle ns = style != null ? style.ToNative((IntPtr)h) : default;
                NativeBackdrop backdrop = new NativeBackdrop
                {
                    Colours = (IntPtr)c,
                    ColourCount = colours != null ? (uint)colours.Length : 0u,
                    Rgba = (IntPtr)bg,
                    Width = backdropImage != null ? (uint)backdropImage.Width : 0u,
                    Height = backdropImage != null ? (uint)backdropImage.Height : 0u,
                    Stride = 0,
                };
                bool hasBackdrop = colours != null || backdropImage != null;
                status = NativeMethods.ckey_certify_scalable_style(s, style != null ? &ns : null, transparent ? 1u : 0u,
                    hasBackdrop ? &backdrop : null, &nm, &adjusted, &cert);
                // Read the adjusted style while the logo href it may point to is still pinned.
                if (status == 0 && style != null && style.Policy == StylePolicy.Adaptive)
                {
                    adjustedStyle = new CKeyStyle();
                    adjustedStyle.Load(in adjusted);
                }
            }
            string message = Native.FromUtf8Z(cert.Message, NativeConstants.CertificationMessageBytes) ?? string.Empty;
            return new CKeyCertification((CKeyStatus)status, cert.Certified != 0, (StylePolicy)cert.Policy,
                (StyleAdjustments)cert.Adjustments, (int)cert.Trials, (int)cert.TrialsPassed, cert.MinContrast,
                cert.FeatureSeparation, cert.LogoLoad, message, adjustedStyle);
        }

        // ---- Rendering --------------------------------------------------------

        /// <summary>
        /// Renders native RGBA8 pixels (size x size, covering the full 31-unit
        /// canvas including the quiet margin). No SVG, file system or network.
        /// </summary>
        /// <param name="size">16..16384 pixels.</param>
        /// <param name="policy">Raster policy.</param>
        /// <param name="transparent">Paint no background: unpainted pixels have alpha 0.</param>
        /// <param name="style">Style, or null for the default.</param>
        /// <param name="logo">Optional RGBA logo drawn into the style's logo circle.</param>
        /// <param name="materials">false draws flat ink (no sheen, depth or grain).</param>
        public CKeyImage RenderRgba(int size = 512, RasterPolicy policy = RasterPolicy.Coverage, bool transparent = false,
                                    CKeyStyle? style = null, CKeyImage? logo = null, bool materials = true)
        {
            byte[] px = Raster(size, policy, NativeConstants.PixelsRgba8, transparent, style, logo, materials);
            return new CKeyImage(size, size, px);
        }

        /// <summary>Renders opaque 8-bit gray pixels (Rec. 709 luma), size x size.</summary>
        public CKeyGrayImage RenderGray(int size = 512, RasterPolicy policy = RasterPolicy.Coverage, CKeyStyle? style = null,
                                        bool materials = true)
        {
            byte[] px = Raster(size, policy, NativeConstants.PixelsGray8, false, style, null, materials);
            return new CKeyGrayImage(size, size, px);
        }

        /// <summary>
        /// Print rendering: black ink on paper on the printer's dot grid,
        /// <paramref name="widthInches"/> wide at <paramref name="dpi"/>. Each
        /// pixel is 0 (ink) or 255 (paper). Colours, materials and logos are not printed.
        /// </summary>
        public CKeyGrayImage RenderPrint(double widthInches, double dpi, CKeyStyle? style = null)
        {
            Native.EnsureAbi();
            byte[]? href = style?.LogoHrefUtf8Z();
            fixed (byte* h = href)
            fixed (NativeScalableSymbol* s = &_symbol)
            {
                NativeStyle ns = style != null ? style.ToNative((IntPtr)h) : default;
                NativeStyle* sp = style != null ? &ns : null;
                nuint side = 0;
                int status = NativeMethods.ckey_render_scalable_print(s, sp, widthInches, dpi, null, 0, &side);
                if (status != (int)CKeyStatus.InsufficientBuffer) Native.Check(status, "ckey_render_scalable_print");
                int n = Native.ToInt32Length(side, "Print raster");
                if (n <= 0 || (long)n * n > int.MaxValue) throw new CKeyException(CKeyStatus.InvalidArgument, "Print raster size out of range.");
                var gray = new byte[n * n];
                fixed (byte* o = gray)
                {
                    Native.Check(NativeMethods.ckey_render_scalable_print(s, sp, widthInches, dpi, o, (nuint)gray.Length, &side),
                        "ckey_render_scalable_print");
                }
                return new CKeyGrayImage(n, n, gray);
            }
        }

        /// <summary>Renders RGBA pixels and encodes them as PNG.</summary>
        public byte[] ToPng(int size = 512, RasterPolicy policy = RasterPolicy.Coverage, bool transparent = false,
                            CKeyStyle? style = null, double dpi = 0) =>
            RenderRgba(size, policy, transparent, style).ToPng(dpi);

        /// <summary>Styled SVG export (vector), <paramref name="scale"/> pixels per canonical unit.</summary>
        public string ToSvg(int scale = 8, CKeyStyle? style = null)
        {
            if (scale < 1 || scale > 256) throw new ArgumentOutOfRangeException(nameof(scale), "scale must be 1..256.");
            Native.EnsureAbi();
            style ??= new CKeyStyle();
            byte[]? href = style.LogoHrefUtf8Z();
            fixed (byte* h = href)
            fixed (NativeScalableSymbol* s = &_symbol)
            {
                NativeStyle ns = style.ToNative((IntPtr)h);
                nuint required = 0;
                int status = NativeMethods.ckey_render_scalable_styled_svg(s, &ns, (uint)scale, null, 0, &required);
                if (status != (int)CKeyStatus.InsufficientBuffer) Native.Check(status, "ckey_render_scalable_styled_svg");
                var utf8 = new byte[Native.ToInt32Length(required, "SVG")];
                fixed (byte* o = utf8)
                {
                    Native.Check(NativeMethods.ckey_render_scalable_styled_svg(s, &ns, (uint)scale, o, (nuint)utf8.Length, &required),
                        "ckey_render_scalable_styled_svg");
                    return Native.FromUtf8Z(o, utf8.Length) ?? string.Empty; // required includes the NUL
                }
            }
        }

        /// <summary>One-page PDF 1.4 vector export, <paramref name="widthPt"/> points wide (72 per inch).</summary>
        public byte[] ToPdf(double widthPt = 288, bool transparent = false, CKeyStyle? style = null)
        {
            if (!(widthPt > 0)) throw new ArgumentOutOfRangeException(nameof(widthPt));
            Native.EnsureAbi();
            byte[]? href = style?.LogoHrefUtf8Z();
            fixed (byte* h = href)
            fixed (NativeScalableSymbol* s = &_symbol)
            {
                NativeStyle ns = style != null ? style.ToNative((IntPtr)h) : default;
                NativeStyle* sp = style != null ? &ns : null;
                nuint required = 0;
                uint t = transparent ? 1u : 0u;
                int status = NativeMethods.ckey_render_scalable_pdf(s, sp, widthPt, t, null, 0, &required);
                if (status != (int)CKeyStatus.InsufficientBuffer) Native.Check(status, "ckey_render_scalable_pdf");
                var pdf = new byte[Native.ToInt32Length(required, "PDF")];
                fixed (byte* o = pdf)
                {
                    Native.Check(NativeMethods.ckey_render_scalable_pdf(s, sp, widthPt, t, o, (nuint)pdf.Length, &required),
                        "ckey_render_scalable_pdf");
                }
                if ((int)required < pdf.Length) Array.Resize(ref pdf, (int)required);
                return pdf;
            }
        }

        private byte[] Raster(int size, RasterPolicy policy, int format, bool transparent, CKeyStyle? style, CKeyImage? logo, bool materials)
        {
            if (size < 16 || size > 16384) throw new ArgumentOutOfRangeException(nameof(size), "size must be 16..16384 pixels.");
            if (policy < RasterPolicy.Coverage || policy > RasterPolicy.Hybrid) throw new ArgumentOutOfRangeException(nameof(policy));
            Native.EnsureAbi();
            NativeRasterOptions options;
            NativeMethods.ckey_raster_options_default(&options);
            options.SizePx = (uint)size;
            options.Policy = (uint)policy;
            options.Format = (uint)format;
            options.Transparent = transparent ? 1u : 0u;
            options.Materials = materials ? 1u : 0u;
            byte[]? href = style?.LogoHrefUtf8Z();
            fixed (byte* h = href)
            fixed (byte* lp = logo?.Rgba)
            fixed (NativeScalableSymbol* s = &_symbol)
            {
                NativeLogoImage li = default;
                if (logo != null)
                {
                    li.Rgba = (IntPtr)lp;
                    li.Width = (uint)logo.Width;
                    li.Height = (uint)logo.Height;
                    li.Stride = (nuint)(logo.Width * 4);
                    options.Logo = (IntPtr)(&li);
                }
                NativeStyle ns = style != null ? style.ToNative((IntPtr)h) : default;
                NativeStyle* sp = style != null ? &ns : null;
                nuint required = 0;
                int status = NativeMethods.ckey_render_scalable_raster(s, sp, &options, null, 0, &required);
                if (status != (int)CKeyStatus.InsufficientBuffer) Native.Check(status, "ckey_render_scalable_raster");
                var px = new byte[Native.ToInt32Length(required, "Raster")];
                fixed (byte* o = px)
                {
                    Native.Check(NativeMethods.ckey_render_scalable_raster(s, sp, &options, o, (nuint)px.Length, &required),
                        "ckey_render_scalable_raster");
                }
                return px;
            }
        }
    }
}
