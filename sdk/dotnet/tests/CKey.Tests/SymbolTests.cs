using System;
using System.Text;
using Xunit;

namespace OpenCKey.Tests
{
    public class SymbolTests
    {
        [Fact]
        public void EncodesText()
        {
            var symbol = CKeySymbol.Encode("https://openckey.org/hello");
            Assert.InRange(symbol.Version, 1, CKeySymbol.MaxVersion);
            Assert.Equal(26, symbol.PayloadLength);
            Assert.Equal(EccLevel.M, symbol.EccLevel);
            Assert.Equal(Layout.Standard, symbol.Layout);
            Assert.InRange(symbol.ModulesAcross, 29, 232);
            Assert.Equal(symbol.CodewordLength, symbol.GetCodeword().Length);
        }

        [Fact]
        public void HonoursVersionLevelAndLayout()
        {
            var symbol = CKeySymbol.Encode(Encoding.ASCII.GetBytes("abc"), version: 5, level: EccLevel.H, layout: Layout.Print);
            Assert.Equal(5, symbol.Version);
            Assert.Equal(EccLevel.H, symbol.EccLevel);
            Assert.Equal(Layout.Print, symbol.Layout);
        }

        [Fact]
        public void EncodesUltraAndExtremeLevelsAndGroupedLayouts()
        {
            byte[] payload = Encoding.ASCII.GetBytes("abc");
            var ultra = CKeySymbol.Encode(payload, version: 8, level: EccLevel.U, layout: Layout.StandardGrouped);
            Assert.Equal(8, ultra.Version);
            Assert.Equal(EccLevel.U, ultra.EccLevel);
            Assert.Equal(Layout.StandardGrouped, ultra.Layout);
            var extreme = CKeySymbol.Encode(payload, level: EccLevel.X, layout: Layout.PrintGrouped);
            Assert.Equal(EccLevel.X, extreme.EccLevel);
            Assert.Equal(Layout.PrintGrouped, extreme.Layout);
            Assert.Equal(4, (int)EccLevel.U);
            Assert.Equal(5, (int)EccLevel.X);
            Assert.Equal(2, (int)Layout.StandardGrouped);
            Assert.Equal(3, (int)Layout.PrintGrouped);

            // Grouped layouts keep the capacity of the layout they are built on.
            Assert.Equal(CKeySymbol.Capacity(8, EccLevel.U), CKeySymbol.Capacity(8, EccLevel.U, Layout.StandardGrouped));
            Assert.Equal(CKeySymbol.Capacity(8, EccLevel.M, Layout.Print), CKeySymbol.Capacity(8, EccLevel.M, Layout.PrintGrouped));
            // Version 1 carries nothing at X: capacity 0 and the core refuses to encode.
            Assert.Equal(0, CKeySymbol.Capacity(1, EccLevel.X));
            Assert.Throws<CKeyException>(() => CKeySymbol.Encode(payload, version: 1, level: EccLevel.X));
            Assert.Throws<ArgumentOutOfRangeException>(() => CKeySymbol.Encode(payload, level: (EccLevel)6));
            Assert.Throws<ArgumentOutOfRangeException>(() => CKeySymbol.Encode(payload, layout: (Layout)4));
        }

        [Fact]
        public void Capacities()
        {
            Assert.Equal(0, CKeySymbol.Capacity(0));
            Assert.Equal(0, CKeySymbol.Capacity(32));
            Assert.True(CKeySymbol.Capacity(31) > CKeySymbol.Capacity(1));
            Assert.Equal(4619, CKeySymbol.Capacity(31, EccLevel.M)); // spec/CKey-Payload-Conventions.md
            Assert.Equal(29, CKeySymbol.ModulesAcrossFor(1));
            Assert.Equal(232, CKeySymbol.ModulesAcrossFor(31));
        }

        [Fact]
        public void RejectsBadArguments()
        {
            Assert.Throws<ArgumentNullException>(() => CKeySymbol.Encode((string)null!));
            Assert.Throws<ArgumentOutOfRangeException>(() => CKeySymbol.Encode("x", version: 32));
            Assert.Throws<ArgumentOutOfRangeException>(() => CKeySymbol.Encode("x", version: -1));
            var tooBig = new byte[CKeySymbol.Capacity(1) + 1];
            var e = Assert.Throws<CKeyException>(() => CKeySymbol.Encode(tooBig, version: 1));
            Assert.NotEqual(CKeyStatus.Ok, e.Status);
        }

        [Fact]
        public void RendersOpaqueAndTransparentRgba()
        {
            var symbol = CKeySymbol.Encode("hello");
            CKeyImage opaque = symbol.RenderRgba(256);
            Assert.Equal(256, opaque.Width);
            Assert.Equal(256, opaque.Height);
            Assert.Equal(256 * 256 * 4, opaque.Rgba.Length);
            Assert.Equal(255, opaque.Rgba[3]); // corner pixel: painted background

            CKeyImage clear = symbol.RenderRgba(256, RasterPolicy.Binary, transparent: true);
            Assert.Equal(0, clear.Rgba[3]); // quiet margin: unpainted
            Assert.Throws<ArgumentOutOfRangeException>(() => symbol.RenderRgba(8));
        }

        [Fact]
        public void RenderIsDeterministic()
        {
            var symbol = CKeySymbol.Encode("determinism");
            Assert.Equal(symbol.RenderRgba(200).Rgba, symbol.RenderRgba(200).Rgba);
        }

        [Fact]
        public void Exports()
        {
            var symbol = CKeySymbol.Encode("exports");
            byte[] png = symbol.ToPng(128);
            Assert.Equal(new byte[] { 0x89, (byte)'P', (byte)'N', (byte)'G' }, png[..4]);
            byte[] grayPng = symbol.RenderGray(128).ToPng(oneBit: true);
            Assert.Equal(0x89, grayPng[0]);
            string svg = symbol.ToSvg();
            Assert.Contains("<svg", svg);
            Assert.EndsWith(">", svg.TrimEnd());
            byte[] pdf = symbol.ToPdf();
            Assert.Equal("%PDF", Encoding.ASCII.GetString(pdf, 0, 4));
        }

        [Fact]
        public void PrintRasterIsBinary()
        {
            var symbol = CKeySymbol.Encode("print", layout: Layout.Print);
            CKeyGrayImage print = symbol.RenderPrint(1.0, 300);
            Assert.InRange(print.Width, 290, 310);
            Assert.All(print.Gray, v => Assert.True(v == 0 || v == 255));
        }

        [Fact]
        public void StylePresetsAndChecks()
        {
            CKeyStyle plain = CKeyStyle.Plain;
            Assert.Equal(CKeyColor.Black, plain.Ink);
            Assert.Equal(CKeyColor.White, plain.Background);
            Assert.Equal(StylePolicy.Strict, plain.Policy);
            CKeyStyle concept = CKeyStyle.Concept;
            Assert.NotEqual(plain.Background, concept.Background);

            var symbol = CKeySymbol.Encode("styled");
            Assert.True(symbol.CheckStyle(concept, out _));
            Assert.Equal(512, symbol.RenderRgba(512, style: concept).Width);

            var unreadable = CKeyStyle.Plain;
            unreadable.Ink = new CKeyColor(250, 250, 250); // white on white
            Assert.False(symbol.CheckStyle(unreadable, out string why));
            Assert.False(string.IsNullOrEmpty(why));
            Assert.Throws<CKeyException>(() => symbol.RenderRgba(256, style: unreadable));
        }

        [Fact]
        public void Certification()
        {
            var symbol = CKeySymbol.Encode("certify");
            CKeyCertification concept = symbol.Certify(CKeyStyle.Concept);
            Assert.True(concept.IsAccepted, concept.Message);
            Assert.True(concept.IsCertified, concept.Message);
            // The photo sweep goes down to sizes too small to read, so a
            // certified style passes some trials, not necessarily all.
            Assert.InRange(concept.TrialsPassed, 1, concept.Trials);

            // Transparent output over an unknown background is never certified.
            CKeyCertification unknown = symbol.Certify(null, transparent: true);
            Assert.False(unknown.IsCertified);
            Assert.Contains("background unknown", unknown.Message);

            CKeyCertification onWhite = symbol.Certify(null, transparent: true, backdropColours: new[] { CKeyColor.White });
            Assert.True(onWhite.IsCertified, onWhite.Message);
        }

        [Fact]
        public void ColourParsing()
        {
            Assert.Equal(new CKeyColor(0x12, 0x34, 0x56), CKeyColor.Parse("#123456"));
            Assert.Equal("#abcdef", CKeyColor.FromRgb(0xabcdef).ToString());
            Assert.Throws<FormatException>(() => CKeyColor.Parse("#12345"));
        }
    }
}
