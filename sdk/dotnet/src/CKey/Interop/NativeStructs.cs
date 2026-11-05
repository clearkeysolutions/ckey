using System;
using System.Runtime.InteropServices;

// Managed mirrors of the C structs in include/ckey/ckey.h and
// include/ckey/telemetry.h (CKEY_ABI_VERSION 4).
//
// Every struct is blittable and LayoutKind.Sequential with fields in header
// order and the exact C field types, so the runtime applies the same natural
// alignment the C compiler does. Pointers are IntPtr, size_t is nuint, and
// C enums are int/uint (4 bytes on every supported ABI).
//
// The numbers in the comments are sizeof/offsetof on the 64-bit ABIs we ship
// (win-x64 LLP64, linux-x64 / osx-x64 / osx-arm64 LP64), printed by
// sdk/dotnet/native/tools/layout_probe.c and recorded in
// sdk/dotnet/native/tools/layout_expected.txt. StructLayoutTests asserts the
// same numbers with Marshal.SizeOf / Marshal.OffsetOf.
namespace OpenCKey.Interop
{
    internal static class NativeConstants
    {
        internal const uint AbiVersion = 4;                 // CKEY_ABI_VERSION
        internal const int ScalableVersionCount = 31;       // CKEY_SCALABLE_VERSION_COUNT
        internal const int ScalableMaxCodewordBytes = 8192; // CKEY_SCALABLE_MAX_CODEWORD_BYTES
        internal const int LocatorPayloadBytes = 16;        // CKEY_LOCATOR_PAYLOAD_BYTES
        internal const int ExtendedPayloadMaxBytes = 2048;  // CKEY_EXTENDED_PAYLOAD_MAX_BYTES
        internal const int DiagnosticsMessageBytes = 96;
        internal const int CertificationMessageBytes = 256;
        internal const int TelemetryBuckets = 10;           // CKEY_TELEMETRY_BUCKETS
        internal const int TelemetryProfiles = 5;           // CKEY_TELEMETRY_PROFILES
        internal const int FieldScalable = 0, FieldLocator = 1, FieldText = 2, FieldExtended = 3; // ckey_field
        internal const int PixelsRgba8 = 0, PixelsGray8 = 1;                                    // ckey_pixel_format
        internal const int PngRgba8 = 0, PngGray8 = 1, PngGray1 = 2;                            // ckey_png_format
    }

    /// <summary>ckey_diagnostics: size 112.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeDiagnostics
    {
        public uint AbiVersion;        // 0
        public int Status;             // 4   ckey_status
        public uint CorrectedSymbols;  // 8
        public uint ErasuresUsed;      // 12
        public fixed byte Message[96]; // 16
    }

    /// <summary>ckey_scalable_symbol: size 8216.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeScalableSymbol
    {
        public uint AbiVersion;          // 0
        public uint Version;             // 4
        public uint EccLevel;            // 8
        public uint Layout;              // 12
        public uint PayloadLength;       // 16
        public uint CodewordLength;      // 20
        public fixed byte Codeword[8192]; // 24
    }

    /// <summary>ckey_rgb: size 3, alignment 1.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeRgb
    {
        public byte R, G, B;
    }

    /// <summary>ckey_style: size 96 (ABI 4 added <c>policy</c> at 92, inside the old tail padding).</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeStyle
    {
        public NativeRgb Background;     // 0
        public NativeRgb BackgroundEdge; // 3
        public NativeRgb Ink;            // 6
        public NativeRgb InkShade;       // 9
        public NativeRgb Accent0;        // 12  accents[4]
        public NativeRgb Accent1;        // 15
        public NativeRgb Accent2;        // 18
        public NativeRgb Accent3;        // 21
        public uint AccentCount;         // 24
        public uint AccentPercent;       // 28
        public int Modules;              // 32  ckey_module_shape
        public int Rings;                // 36  ckey_ring_style
        public int Rays;                 // 40  ckey_ray_cap
                                         // 44  4 bytes padding
        public double Bevel;             // 48
        public double Shadow;            // 56
        public double Texture;           // 64
        public IntPtr LogoHref;          // 72  const char* (UTF-8, NUL-terminated)
        public double LogoRadius;        // 80
        public uint LogoPlate;           // 88
        public uint Policy;              // 92  ckey_style_policy
    }

    /// <summary>ckey_logo_image: size 24.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeLogoImage
    {
        public IntPtr Rgba;   // 0  const uint8_t*
        public uint Width;    // 8
        public uint Height;   // 12
        public nuint Stride;  // 16
    }

    /// <summary>ckey_raster_options: size 48.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeRasterOptions
    {
        public uint AbiVersion;     // 0
        public uint SizePx;         // 4
        public uint Policy;         // 8   ckey_raster_policy
        public uint Format;         // 12  ckey_pixel_format
        public uint Transparent;    // 16
        public uint Supersample;    // 20
        public double MinFeaturePx; // 24
        public uint Materials;      // 32
                                    // 36  4 bytes padding
        public IntPtr Logo;         // 40  const ckey_logo_image*
    }

    /// <summary>ckey_backdrop: size 40.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeBackdrop
    {
        public IntPtr Colours;    // 0   const ckey_rgb*
        public uint ColourCount;  // 8
                                  // 12  4 bytes padding
        public IntPtr Rgba;       // 16  const uint8_t*
        public uint Width;        // 24
        public uint Height;       // 28
        public nuint Stride;      // 32
    }

    /// <summary>ckey_scan_model: size 48.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeScanModel
    {
        public uint AbiVersion;     // 0
                                    // 4   4 bytes padding
        public double PxPerModule;  // 8
        public double BlurPx;       // 16
        public double Noise;        // 24
        public double Glare;        // 32
        public uint Trials;         // 40
                                    // 44  4 bytes tail padding
    }

    /// <summary>ckey_certification: size 304.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeCertification
    {
        public uint AbiVersion;           // 0
        public uint Certified;            // 4
        public uint Policy;               // 8
        public uint Adjustments;          // 12
        public uint Trials;               // 16
        public uint TrialsPassed;         // 20
        public double MinContrast;        // 24
        public double FeatureSeparation;  // 32
        public double LogoLoad;           // 40
        public fixed byte Message[256];   // 48
    }

    /// <summary>ckey_telemetry_histogram: size 96.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeTelemetryHistogram
    {
        public ulong Count;              // 0
        public ulong Sum;                // 8
        public fixed ulong Buckets[10];  // 16
    }

    /// <summary>ckey_telemetry_counters: size 264.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeTelemetryCounters
    {
        public ulong SessionsStarted;                   // 0
        public ulong SessionsSucceeded;                 // 8
        public ulong SessionsFailed;                    // 16
        public ulong Frames;                            // 24
        public fixed ulong Decoded[5];                  // 32
        public NativeTelemetryHistogram TimeToDecodeMs; // 72
        public NativeTelemetryHistogram FrameMs;        // 168
    }
}
