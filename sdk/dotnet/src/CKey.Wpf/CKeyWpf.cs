using System;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace OpenCKey.Wpf
{
    /// <summary>WPF interop for CKey pixels and scanning.</summary>
    public static class CKeyWpf
    {
        /// <summary>
        /// Copies an RGBA image into a new <see cref="WriteableBitmap"/>
        /// (<see cref="PixelFormats.Bgra32"/>, straight alpha, like the source).
        /// Use it as an <c>Image.Source</c>; transparent symbols composite over the host's background.
        /// </summary>
        public static WriteableBitmap ToWriteableBitmap(this CKeyImage image, double dpi = 96)
        {
            if (image == null) throw new ArgumentNullException(nameof(image));
            var bitmap = new WriteableBitmap(image.Width, image.Height, dpi, dpi, PixelFormats.Bgra32, null);
            CopyTo(image, bitmap);
            return bitmap;
        }

        /// <summary>Renders a symbol straight into a new <see cref="WriteableBitmap"/>.</summary>
        public static WriteableBitmap ToWriteableBitmap(this CKeySymbol symbol, int size = 512,
                                                        RasterPolicy policy = RasterPolicy.Coverage,
                                                        bool transparent = false, CKeyStyle? style = null, double dpi = 96)
        {
            if (symbol == null) throw new ArgumentNullException(nameof(symbol));
            return symbol.RenderRgba(size, policy, transparent, style).ToWriteableBitmap(dpi);
        }

        /// <summary>
        /// Writes an RGBA image into an existing Bgra32 bitmap of the same size
        /// (reuse one bitmap when re-rendering). Call on the bitmap's dispatcher thread.
        /// </summary>
        public static void CopyTo(this CKeyImage image, WriteableBitmap target)
        {
            if (image == null) throw new ArgumentNullException(nameof(image));
            if (target == null) throw new ArgumentNullException(nameof(target));
            if (target.PixelWidth != image.Width || target.PixelHeight != image.Height || target.Format != PixelFormats.Bgra32)
                throw new ArgumentException("The target must be a Bgra32 bitmap of the image's size.", nameof(target));
            byte[] rgba = image.Rgba;
            var bgra = new byte[rgba.Length];
            for (int i = 0; i < rgba.Length; i += 4)
            {
                bgra[i] = rgba[i + 2];
                bgra[i + 1] = rgba[i + 1];
                bgra[i + 2] = rgba[i];
                bgra[i + 3] = rgba[i + 3];
            }
            target.WritePixels(new Int32Rect(0, 0, image.Width, image.Height), bgra, image.Width * 4, 0);
        }

        /// <summary>
        /// Converts any <see cref="BitmapSource"/> (camera frame, screenshot,
        /// loaded image) to 8-bit gray and scans it with <see cref="CKeyDecoder"/>.
        /// </summary>
        public static CKeyResult? Decode(BitmapSource frame)
        {
            byte[] gray = ToGray8(frame, out int width, out int height, out int stride);
            return CKeyDecoder.Decode(gray, width, height, stride);
        }

        /// <summary>Converts a bitmap to Gray8 pixels for <see cref="CKeyDecoder"/> or <see cref="CKeyScanSession"/>.</summary>
        public static byte[] ToGray8(BitmapSource frame, out int width, out int height, out int stride)
        {
            if (frame == null) throw new ArgumentNullException(nameof(frame));
            BitmapSource gray = frame.Format == PixelFormats.Gray8 ? frame : new FormatConvertedBitmap(frame, PixelFormats.Gray8, null, 0);
            width = gray.PixelWidth;
            height = gray.PixelHeight;
            stride = (width + 3) & ~3;
            var pixels = new byte[stride * height];
            gray.CopyPixels(pixels, stride, 0);
            return pixels;
        }
    }
}
