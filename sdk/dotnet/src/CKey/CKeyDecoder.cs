using System;
using System.Buffers;
using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>A decoded symbol.</summary>
    public sealed class CKeyResult
    {
        private CKeyPayloadAction? _action;
        private bool _classified;

        internal CKeyResult(byte[] payload, CKeyProfile profile, int version, Layout layout, double[] quad)
        {
            Payload = payload;
            Text = CKeyPayload.ReadableText(payload);
            Profile = profile;
            Version = version;
            Layout = layout;
            Quad = quad;
        }

        /// <summary>The payload bytes.</summary>
        public byte[] Payload { get; }
        /// <summary>The payload as text when it is valid UTF-8 per the payload conventions; null for binary payloads.</summary>
        public string? Text { get; }
        /// <summary>The profile the symbol was read as.</summary>
        public CKeyProfile Profile { get; }
        /// <summary>Profile 0 version (1..31); 0 for Profiles 1–3.</summary>
        public int Version { get; }
        /// <summary>Profile 0 layout (standard or print); Standard for Profiles 1–3.</summary>
        public Layout Layout { get; }
        /// <summary>The refined quadrilateral the decode used: x0,y0,…,x3,y3 in frame pixels.</summary>
        public double[] Quad { get; }

        /// <summary>A display name, e.g. "Profile 0 · v3" or "Profile 0.1 · print · v5".</summary>
        public string ProfileName => Profile switch
        {
            CKeyProfile.Scalable => Layout == Layout.Print ? $"Profile 0.1 · print · v{Version}" : $"Profile 0 · v{Version}",
            CKeyProfile.Locator => "Profile 1 · Locator",
            CKeyProfile.Text => "Profile 2 · Text",
            _ => "Profile 3 · Extended",
        };

        /// <summary>Lower-case hex of the payload.</summary>
        public string PayloadHex => CKeyPayload.ToHex(Payload);

        /// <summary>The proposed action for a text payload (never performed automatically), or null.</summary>
        public CKeyPayloadAction? Action
        {
            get
            {
                if (!_classified)
                {
                    _action = CKeyPayload.Classify(Text);
                    _classified = true;
                }
                return _action;
            }
        }

        /// <summary>The telemetry profile code (p0, p0_1, p1, p2, p3 = 0..4).</summary>
        internal uint TelemetryProfileCode => Profile switch
        {
            CKeyProfile.Scalable => Layout == Layout.Print ? 1u : 0u,
            CKeyProfile.Locator => 2u,
            CKeyProfile.Text => 3u,
            _ => 4u,
        };
    }

    /// <summary>
    /// Headless full-frame scanner. Needs only the scan part of the core: no
    /// renderer, no telemetry, no network. The pipeline is the iOS reader's
    /// (ios/CKeyReader/CameraScanner.swift): find up to six border candidates,
    /// and for each one refine the quadrilateral to Profile 0's field first and
    /// decode it, then try the Profile 1, 2 and 3 fields.
    /// </summary>
    public static unsafe class CKeyDecoder
    {
        /// <summary>Border candidates examined per frame (as in the iOS reader).</summary>
        public const int MaxCandidates = 6;

        // Profile 0 payloads reach 4,619 bytes (v31, level M), beyond the
        // 2,048-byte Profile 3 maximum, so Profile 0 gets its own 64 KB buffer.
        private const int ScalableBufferBytes = 65535;

        /// <summary>
        /// Finds and decodes a CKey in an 8-bit grayscale frame (either contrast
        /// polarity). Returns null when no symbol decodes. Thread-safe.
        /// </summary>
        /// <param name="gray">Pixels, at least stride * (height - 1) + width bytes.</param>
        /// <param name="width">Width in pixels.</param>
        /// <param name="height">Height in pixels.</param>
        /// <param name="stride">Bytes per row (&gt;= width).</param>
        public static CKeyResult? Decode(ReadOnlySpan<byte> gray, int width, int height, int stride)
        {
            Validate(gray, width, height, stride);
            Native.EnsureAbi();
            double* borders = stackalloc double[MaxCandidates * 8];
            fixed (byte* g = gray)
            {
                int n = (int)NativeMethods.ckey_find_borders_gray8(g, (nuint)width, (nuint)height, (nuint)stride, borders, MaxCandidates);
                n = Math.Min(n, MaxCandidates);
                for (int i = 0; i < n; i++)
                {
                    CKeyResult? result = DecodeCandidate(g, width, height, stride, borders + 8 * i);
                    if (result != null) return result;
                }
            }
            return null;
        }

        /// <summary>
        /// Detection only: candidate border quadrilaterals (8 doubles each, the
        /// frame stroke's outer edge), most border-like first. Accepts no payload.
        /// </summary>
        public static double[][] FindBorders(ReadOnlySpan<byte> gray, int width, int height, int stride, int maxQuads = MaxCandidates)
        {
            Validate(gray, width, height, stride);
            if (maxQuads < 1 || maxQuads > 64) throw new ArgumentOutOfRangeException(nameof(maxQuads), "maxQuads must be 1..64.");
            Native.EnsureAbi();
            var buffer = new double[maxQuads * 8];
            int n;
            fixed (byte* g = gray)
            fixed (double* b = buffer)
            {
                n = (int)NativeMethods.ckey_find_borders_gray8(g, (nuint)width, (nuint)height, (nuint)stride, b, (nuint)maxQuads);
            }
            n = Math.Min(n, maxQuads);
            var quads = new double[n][];
            for (int i = 0; i < n; i++)
            {
                quads[i] = new double[8];
                Array.Copy(buffer, i * 8, quads[i], 0, 8);
            }
            return quads;
        }

        /// <summary>
        /// Decodes one border candidate (from <see cref="FindBorders"/> or the
        /// host's own detector): refines it per profile and decodes, Profile 0 first.
        /// </summary>
        public static CKeyResult? DecodeCandidate(ReadOnlySpan<byte> gray, int width, int height, int stride, ReadOnlySpan<double> borderQuad)
        {
            Validate(gray, width, height, stride);
            if (borderQuad.Length != 8) throw new ArgumentException("A quadrilateral is 8 doubles (x0,y0,…,x3,y3).", nameof(borderQuad));
            Native.EnsureAbi();
            fixed (byte* g = gray)
            fixed (double* b = borderQuad)
            {
                return DecodeCandidate(g, width, height, stride, b);
            }
        }

        private static CKeyResult? DecodeCandidate(byte* g, int width, int height, int stride, double* border)
        {
            nuint w = (nuint)width, h = (nuint)height, s = (nuint)stride;
            double* quad = stackalloc double[8];
            NativeDiagnostics diagnostics;

            // Profile 0 first: the core aligns the frame edges, identifies the
            // version and layout from the Grid/Ring/Ray Keys, then decodes.
            if (NativeMethods.ckey_refine_quad_gray8(g, w, h, s, border, NativeConstants.FieldScalable, quad) >= 0)
            {
                byte[] output = ArrayPool<byte>.Shared.Rent(ScalableBufferBytes);
                try
                {
                    nuint length = 0;
                    uint version = 0, layout = 0;
                    diagnostics = default;
                    int status;
                    fixed (byte* o = output)
                    {
                        status = NativeMethods.ckey_decode_scalable_gray8(g, w, h, s, quad, o, (nuint)ScalableBufferBytes,
                            &length, &version, &layout, &diagnostics);
                    }
                    if (status == 0 && length <= (nuint)ScalableBufferBytes)
                    {
                        return new CKeyResult(Copy(output, (int)length), CKeyProfile.Scalable, (int)version,
                            layout == 1 ? Layout.Print : Layout.Standard, ToArray(quad));
                    }
                }
                finally
                {
                    ArrayPool<byte>.Shared.Return(output);
                }
            }

            // Compact and dense profiles have deliberately different calibration
            // fields: each decoder gets the quad refined to its own field. The
            // compact profile goes first so ordinary locator reads stay fast.
            if (NativeMethods.ckey_refine_quad_gray8(g, w, h, s, border, NativeConstants.FieldLocator, quad) >= 0)
            {
                byte* locator = stackalloc byte[NativeConstants.LocatorPayloadBytes];
                diagnostics = default;
                if (NativeMethods.ckey_decode_gray8(g, w, h, s, quad, locator, (nuint)NativeConstants.LocatorPayloadBytes, &diagnostics) == 0)
                {
                    var payload = new byte[NativeConstants.LocatorPayloadBytes];
                    for (int i = 0; i < payload.Length; i++) payload[i] = locator[i];
                    return new CKeyResult(payload, CKeyProfile.Locator, 0, Layout.Standard, ToArray(quad));
                }
            }

            byte[] dense = ArrayPool<byte>.Shared.Rent(NativeConstants.ExtendedPayloadMaxBytes);
            try
            {
                fixed (byte* o = dense)
                {
                    nuint length = 0;
                    if (NativeMethods.ckey_refine_quad_gray8(g, w, h, s, border, NativeConstants.FieldText, quad) >= 0)
                    {
                        diagnostics = default;
                        if (NativeMethods.ckey_decode_text_gray8(g, w, h, s, quad, o, (nuint)NativeConstants.ExtendedPayloadMaxBytes,
                                &length, &diagnostics) == 0 && length <= (nuint)NativeConstants.ExtendedPayloadMaxBytes)
                        {
                            return new CKeyResult(Copy(dense, (int)length), CKeyProfile.Text, 0, Layout.Standard, ToArray(quad));
                        }
                    }
                    length = 0;
                    if (NativeMethods.ckey_refine_quad_gray8(g, w, h, s, border, NativeConstants.FieldExtended, quad) >= 0)
                    {
                        diagnostics = default;
                        if (NativeMethods.ckey_decode_extended_gray8(g, w, h, s, quad, o, (nuint)NativeConstants.ExtendedPayloadMaxBytes,
                                &length, &diagnostics) == 0 && length <= (nuint)NativeConstants.ExtendedPayloadMaxBytes)
                        {
                            return new CKeyResult(Copy(dense, (int)length), CKeyProfile.Extended, 0, Layout.Standard, ToArray(quad));
                        }
                    }
                }
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(dense);
            }
            return null;
        }

        private static void Validate(ReadOnlySpan<byte> gray, int width, int height, int stride)
        {
            if (width <= 0) throw new ArgumentOutOfRangeException(nameof(width));
            if (height <= 0) throw new ArgumentOutOfRangeException(nameof(height));
            if (stride < width) throw new ArgumentOutOfRangeException(nameof(stride), "stride must be at least width.");
            if (gray.Length < (long)stride * (height - 1) + width)
                throw new ArgumentException("gray is smaller than stride * (height - 1) + width bytes.", nameof(gray));
        }

        private static byte[] Copy(byte[] source, int length)
        {
            var bytes = new byte[length];
            Array.Copy(source, bytes, length);
            return bytes;
        }

        private static double[] ToArray(double* quad)
        {
            var q = new double[8];
            for (int i = 0; i < 8; i++) q[i] = quad[i];
            return q;
        }
    }
}
