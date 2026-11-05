using System;
using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>
    /// A native RGBA8 pixel buffer: rows top to bottom with no padding, straight
    /// (non-premultiplied) sRGB alpha. Hand it to a host image type (for WPF,
    /// see the CKey.Wpf package) or export it with <see cref="ToPng"/>.
    /// </summary>
    public sealed class CKeyImage
    {
        /// <summary>Wraps an RGBA8 buffer of width * height * 4 bytes (not copied).</summary>
        public CKeyImage(int width, int height, byte[] rgba)
        {
            if (width <= 0) throw new ArgumentOutOfRangeException(nameof(width));
            if (height <= 0) throw new ArgumentOutOfRangeException(nameof(height));
            if (rgba == null) throw new ArgumentNullException(nameof(rgba));
            if (rgba.Length != (long)width * height * 4) throw new ArgumentException("rgba must hold width * height * 4 bytes.", nameof(rgba));
            Width = width;
            Height = height;
            Rgba = rgba;
        }

        /// <summary>Width in pixels.</summary>
        public int Width { get; }
        /// <summary>Height in pixels.</summary>
        public int Height { get; }
        /// <summary>RGBA8 pixels, straight alpha, stride Width * 4.</summary>
        public byte[] Rgba { get; }

        /// <summary>Encodes the pixels as an RGBA PNG. dpi &gt; 0 records the physical resolution.</summary>
        public byte[] ToPng(double dpi = 0) => Png.Encode(Rgba, Width, Height, Width * 4, NativeConstants.PngRgba8, dpi);
    }

    /// <summary>An 8-bit grayscale pixel buffer: rows top to bottom with no padding.</summary>
    public sealed class CKeyGrayImage
    {
        /// <summary>Wraps a gray buffer of width * height bytes (not copied).</summary>
        public CKeyGrayImage(int width, int height, byte[] gray)
        {
            if (width <= 0) throw new ArgumentOutOfRangeException(nameof(width));
            if (height <= 0) throw new ArgumentOutOfRangeException(nameof(height));
            if (gray == null) throw new ArgumentNullException(nameof(gray));
            if (gray.Length != (long)width * height) throw new ArgumentException("gray must hold width * height bytes.", nameof(gray));
            Width = width;
            Height = height;
            Gray = gray;
        }

        /// <summary>Width in pixels.</summary>
        public int Width { get; }
        /// <summary>Height in pixels.</summary>
        public int Height { get; }
        /// <summary>Gray pixels (0 black .. 255 white), stride Width.</summary>
        public byte[] Gray { get; }

        /// <summary>
        /// Encodes the pixels as a PNG: 8-bit gray, or with <paramref name="oneBit"/>
        /// a 1-bit image (&lt; 128 is black) for print bitmaps.
        /// </summary>
        public byte[] ToPng(double dpi = 0, bool oneBit = false) =>
            Png.Encode(Gray, Width, Height, Width, oneBit ? NativeConstants.PngGray1 : NativeConstants.PngGray8, dpi);
    }

    internal static unsafe class Png
    {
        internal static byte[] Encode(byte[] pixels, int width, int height, int stride, int format, double dpi)
        {
            Native.EnsureAbi();
            nuint required = 0;
            fixed (byte* p = pixels)
            {
                int status = NativeMethods.ckey_encode_png(p, (uint)width, (uint)height, (nuint)stride, format, dpi, null, 0, &required);
                if (status != (int)CKeyStatus.InsufficientBuffer) Native.Check(status, "ckey_encode_png");
                var png = new byte[Native.ToInt32Length(required, "PNG")];
                fixed (byte* o = png)
                {
                    Native.Check(NativeMethods.ckey_encode_png(p, (uint)width, (uint)height, (nuint)stride, format, dpi, o, (nuint)png.Length, &required),
                        "ckey_encode_png");
                }
                if ((int)required != png.Length) Array.Resize(ref png, (int)required);
                return png;
            }
        }
    }
}
