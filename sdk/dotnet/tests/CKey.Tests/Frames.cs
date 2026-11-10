namespace OpenCKey.Tests
{
    internal static class Frames
    {
        /// <summary>Places a gray symbol on a larger white frame, with row padding.</summary>
        internal static byte[] Embed(CKeyGrayImage symbol, int width, int height, int stride, int left, int top)
        {
            var frame = new byte[stride * height];
            for (int i = 0; i < frame.Length; i++) frame[i] = 255;
            for (int y = 0; y < symbol.Height; y++)
                System.Array.Copy(symbol.Gray, y * symbol.Width, frame, (top + y) * stride + left, symbol.Width);
            return frame;
        }

        internal static byte[] Frame(CKeySymbol symbol, out int width, out int height, out int stride, int size = 320)
        {
            width = 640;
            height = 480;
            stride = 704;
            return Embed(symbol.RenderGray(size), width, height, stride, 150, 80);
        }
    }
}
