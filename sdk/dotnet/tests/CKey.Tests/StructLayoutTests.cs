using System;
using System.Runtime.InteropServices;
using OpenCKey.Interop;
using Xunit;

namespace OpenCKey.Tests
{
    // The expected numbers come from sdk/dotnet/native/tools/layout_probe.c
    // (recorded in layout_expected.txt) on 64-bit ABIs.
    public unsafe class StructLayoutTests
    {
        private static int Off<T>(string field) => (int)Marshal.OffsetOf<T>(field);

        [Fact]
        public void RunsOn64Bit() => Assert.Equal(8, IntPtr.Size);

        [Fact]
        public void Sizes()
        {
            Assert.Equal(112, Marshal.SizeOf<NativeDiagnostics>());
            Assert.Equal(8216, Marshal.SizeOf<NativeScalableSymbol>());
            Assert.Equal(3, Marshal.SizeOf<NativeRgb>());
            Assert.Equal(96, Marshal.SizeOf<NativeStyle>());
            Assert.Equal(24, Marshal.SizeOf<NativeLogoImage>());
            Assert.Equal(48, Marshal.SizeOf<NativeRasterOptions>());
            Assert.Equal(40, Marshal.SizeOf<NativeBackdrop>());
            Assert.Equal(48, Marshal.SizeOf<NativeScanModel>());
            Assert.Equal(304, Marshal.SizeOf<NativeCertification>());
            Assert.Equal(96, Marshal.SizeOf<NativeTelemetryHistogram>());
            Assert.Equal(264, Marshal.SizeOf<NativeTelemetryCounters>());
            // The blittable (unmanaged) size the P/Invokes actually use agrees.
            Assert.Equal(112, sizeof(NativeDiagnostics));
            Assert.Equal(8216, sizeof(NativeScalableSymbol));
            Assert.Equal(96, sizeof(NativeStyle));
            Assert.Equal(48, sizeof(NativeRasterOptions));
            Assert.Equal(40, sizeof(NativeBackdrop));
            Assert.Equal(48, sizeof(NativeScanModel));
            Assert.Equal(304, sizeof(NativeCertification));
            Assert.Equal(264, sizeof(NativeTelemetryCounters));
        }

        [Fact]
        public void DiagnosticsAndSymbolOffsets()
        {
            Assert.Equal(4, Off<NativeDiagnostics>(nameof(NativeDiagnostics.Status)));
            Assert.Equal(16, Off<NativeDiagnostics>(nameof(NativeDiagnostics.Message)));
            Assert.Equal(20, Off<NativeScalableSymbol>(nameof(NativeScalableSymbol.CodewordLength)));
            Assert.Equal(24, Off<NativeScalableSymbol>(nameof(NativeScalableSymbol.Codeword)));
        }

        [Fact]
        public void StyleOffsets()
        {
            Assert.Equal(3, Off<NativeStyle>(nameof(NativeStyle.BackgroundEdge)));
            Assert.Equal(6, Off<NativeStyle>(nameof(NativeStyle.Ink)));
            Assert.Equal(9, Off<NativeStyle>(nameof(NativeStyle.InkShade)));
            Assert.Equal(12, Off<NativeStyle>(nameof(NativeStyle.Accent0)));
            Assert.Equal(21, Off<NativeStyle>(nameof(NativeStyle.Accent3)));
            Assert.Equal(24, Off<NativeStyle>(nameof(NativeStyle.AccentCount)));
            Assert.Equal(28, Off<NativeStyle>(nameof(NativeStyle.AccentPercent)));
            Assert.Equal(32, Off<NativeStyle>(nameof(NativeStyle.Modules)));
            Assert.Equal(36, Off<NativeStyle>(nameof(NativeStyle.Rings)));
            Assert.Equal(40, Off<NativeStyle>(nameof(NativeStyle.Rays)));
            Assert.Equal(48, Off<NativeStyle>(nameof(NativeStyle.Bevel)));
            Assert.Equal(56, Off<NativeStyle>(nameof(NativeStyle.Shadow)));
            Assert.Equal(64, Off<NativeStyle>(nameof(NativeStyle.Texture)));
            Assert.Equal(72, Off<NativeStyle>(nameof(NativeStyle.LogoHref)));
            Assert.Equal(80, Off<NativeStyle>(nameof(NativeStyle.LogoRadius)));
            Assert.Equal(88, Off<NativeStyle>(nameof(NativeStyle.LogoPlate)));
            Assert.Equal(92, Off<NativeStyle>(nameof(NativeStyle.Policy)));
        }

        [Fact]
        public void RasterBackdropModelCertificationOffsets()
        {
            Assert.Equal(16, Off<NativeLogoImage>(nameof(NativeLogoImage.Stride)));
            Assert.Equal(20, Off<NativeRasterOptions>(nameof(NativeRasterOptions.Supersample)));
            Assert.Equal(24, Off<NativeRasterOptions>(nameof(NativeRasterOptions.MinFeaturePx)));
            Assert.Equal(32, Off<NativeRasterOptions>(nameof(NativeRasterOptions.Materials)));
            Assert.Equal(40, Off<NativeRasterOptions>(nameof(NativeRasterOptions.Logo)));
            Assert.Equal(8, Off<NativeBackdrop>(nameof(NativeBackdrop.ColourCount)));
            Assert.Equal(16, Off<NativeBackdrop>(nameof(NativeBackdrop.Rgba)));
            Assert.Equal(24, Off<NativeBackdrop>(nameof(NativeBackdrop.Width)));
            Assert.Equal(32, Off<NativeBackdrop>(nameof(NativeBackdrop.Stride)));
            Assert.Equal(8, Off<NativeScanModel>(nameof(NativeScanModel.PxPerModule)));
            Assert.Equal(40, Off<NativeScanModel>(nameof(NativeScanModel.Trials)));
            Assert.Equal(24, Off<NativeCertification>(nameof(NativeCertification.MinContrast)));
            Assert.Equal(40, Off<NativeCertification>(nameof(NativeCertification.LogoLoad)));
            Assert.Equal(48, Off<NativeCertification>(nameof(NativeCertification.Message)));
        }

        [Fact]
        public void TelemetryOffsets()
        {
            Assert.Equal(16, Off<NativeTelemetryHistogram>(nameof(NativeTelemetryHistogram.Buckets)));
            Assert.Equal(32, Off<NativeTelemetryCounters>(nameof(NativeTelemetryCounters.Decoded)));
            Assert.Equal(72, Off<NativeTelemetryCounters>(nameof(NativeTelemetryCounters.TimeToDecodeMs)));
            Assert.Equal(168, Off<NativeTelemetryCounters>(nameof(NativeTelemetryCounters.FrameMs)));
        }

        [Fact]
        public void NativeAbiMatches()
        {
            Assert.Equal((int)NativeConstants.AbiVersion, CKey.AbiVersion);
        }
    }
}
