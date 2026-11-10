using System;
using System.Text;
using Xunit;

namespace OpenCKey.Tests
{
    public class DecoderTests
    {
        [Fact]
        public void RoundTripsTextThroughTheFullFramePipeline()
        {
            var symbol = CKeySymbol.Encode("https://openckey.org/hello");
            byte[] frame = Frames.Frame(symbol, out int w, out int h, out int stride);
            CKeyResult? result = CKeyDecoder.Decode(frame, w, h, stride);
            Assert.NotNull(result);
            Assert.Equal(CKeyProfile.Scalable, result!.Profile);
            Assert.Equal(symbol.Version, result.Version);
            Assert.Equal(Layout.Standard, result.Layout);
            Assert.Equal("https://openckey.org/hello", result.Text);
            Assert.Equal(8, result.Quad.Length);
            Assert.Equal(CKeyActionKind.Web, result.Action!.Kind);
            Assert.Equal("Open openckey.org", result.Action.Label);
            Assert.StartsWith("Profile 0 · v", result.ProfileName);
        }

        [Theory]
        [InlineData(Layout.StandardGrouped)]
        [InlineData(Layout.PrintGrouped)]
        public void RoundTripsGroupedLayouts(Layout layout)
        {
            var payload = new byte[] { 0, 1, 2, 0xff, 0xfe, 7, 0x80 };
            var symbol = CKeySymbol.Encode(payload, layout: layout);
            byte[] frame = Frames.Frame(symbol, out int w, out int h, out int stride);
            CKeyResult? result = CKeyDecoder.Decode(frame, w, h, stride);
            Assert.NotNull(result);
            Assert.Equal(layout, result!.Layout);
            Assert.Equal(payload, result.Payload);
        }

        [Fact]
        public void RoundTripsPrintLayoutAndBinaryPayloads()
        {
            var payload = new byte[] { 0, 1, 2, 0xff, 0xfe, 7, 0x80 };
            var symbol = CKeySymbol.Encode(payload, layout: Layout.Print);
            byte[] frame = Frames.Frame(symbol, out int w, out int h, out int stride);
            CKeyResult? result = CKeyDecoder.Decode(frame, w, h, stride);
            Assert.NotNull(result);
            Assert.Equal(Layout.Print, result!.Layout);
            Assert.Equal(payload, result.Payload);
            Assert.Null(result.Text);
            Assert.Null(result.Action);
            Assert.Equal("000102fffe0780", result.PayloadHex);
        }

        [Fact]
        public void RoundTripsALargerVersion()
        {
            string text = new string('x', 600);
            var symbol = CKeySymbol.Encode(text);
            Assert.True(symbol.Version > 5);
            // About 4 pixels per module.
            int size = (int)Math.Ceiling(31.0 * 4 * symbol.ModulesAcross / 29.0);
            var gray = symbol.RenderGray(size, RasterPolicy.Binary);
            int w = size + 200, h = size + 160, stride = w;
            byte[] frame = Frames.Embed(gray, w, h, stride, 100, 80);
            Assert.Equal(text, CKeyDecoder.Decode(frame, w, h, stride)?.Text);
        }

        [Fact]
        public void FindBordersThenDecodeCandidate()
        {
            var symbol = CKeySymbol.Encode("split detection");
            byte[] frame = Frames.Frame(symbol, out int w, out int h, out int stride);
            double[][] borders = CKeyDecoder.FindBorders(frame, w, h, stride);
            Assert.NotEmpty(borders);
            Assert.Equal("split detection", CKeyDecoder.DecodeCandidate(frame, w, h, stride, borders[0])?.Text);
        }

        [Fact]
        public void BlankFrameDecodesNothing()
        {
            var frame = new byte[320 * 240];
            Array.Fill(frame, (byte)200);
            Assert.Null(CKeyDecoder.Decode(frame, 320, 240, 320));
        }

        [Fact]
        public void ValidatesFrameArguments()
        {
            var frame = new byte[100];
            Assert.Throws<ArgumentOutOfRangeException>(() => CKeyDecoder.Decode(frame, 0, 10, 10));
            Assert.Throws<ArgumentOutOfRangeException>(() => CKeyDecoder.Decode(frame, 10, 10, 9));
            Assert.Throws<ArgumentException>(() => CKeyDecoder.Decode(frame, 10, 11, 10));
            Assert.Throws<ArgumentException>(() => CKeyDecoder.DecodeCandidate(frame, 10, 10, 10, new double[7]));
        }

        [Fact]
        public void DecodeIsThreadSafe()
        {
            var symbol = CKeySymbol.Encode("parallel");
            byte[] frame = Frames.Frame(symbol, out int w, out int h, out int stride);
            var texts = new string?[8];
            System.Threading.Tasks.Parallel.For(0, texts.Length, i => texts[i] = CKeyDecoder.Decode(frame, w, h, stride)?.Text);
            Assert.All(texts, t => Assert.Equal("parallel", t));
        }

        [Fact]
        public void UnicodeText()
        {
            const string text = "Grüße · 東京 · ✓";
            var symbol = CKeySymbol.Encode(text);
            Assert.Equal(Encoding.UTF8.GetByteCount(text), symbol.PayloadLength);
            byte[] frame = Frames.Frame(symbol, out int w, out int h, out int stride);
            Assert.Equal(text, CKeyDecoder.Decode(frame, w, h, stride)?.Text);
        }
    }
}
