// Print output for the CLI: 1-bit PNG on the printer's exact dot grid (no
// anti-aliasing, so no grey for the printer to halftone into noise), with a
// pHYs chunk so the image prints at its true physical size, plus a tiny 5x7
// bitmap font for calibration-sheet labels. No dependencies.
#ifndef CKEY_CLI_PRINT_OUTPUT_H
#define CKEY_CLI_PRINT_OUTPUT_H

#include "svg_raster.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace printout {

struct Page { int width = 0, height = 0; std::vector<uint8_t> ink; };  // 1 = ink

inline uint32_t crc32_ieee(const uint8_t *data, size_t len, uint32_t crc = 0xffffffffu) {
    for (size_t i = 0; i < len; ++i) { crc ^= data[i]; for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u))); }
    return crc;
}

inline bool write_png(const std::string &path, const Page &page, double dpi) {
    const auto be32 = [](std::vector<uint8_t> &v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s)); };
    std::vector<uint8_t> raw;  // filter byte + packed 1-bit rows (1 = white paper)
    const size_t row_bytes = (static_cast<size_t>(page.width) + 7) / 8;
    for (int y = 0; y < page.height; ++y) {
        raw.push_back(0); const size_t start = raw.size(); raw.resize(start + row_bytes, 0);
        for (int x = 0; x < page.width; ++x) if (!page.ink[static_cast<size_t>(y) * page.width + x]) raw[start + x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
    }
    std::vector<uint8_t> z = {0x78, 0x01};  // zlib stream of stored (uncompressed) deflate blocks
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n)); z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n)); z.push_back(static_cast<uint8_t>((~n) >> 8));
        for (size_t i = 0; i < n; ++i) { const uint8_t c = raw[off + i]; z.push_back(c); a = (a + c) % 65521; b = (b + a) % 65521; }
        off += n;
    }
    be32(z, (b << 16) | a);
    std::ofstream out(path, std::ios::binary); if (!out) return false;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}; out.write(reinterpret_cast<const char *>(sig), 8);
    const auto chunk = [&](const char *type, const std::vector<uint8_t> &data) {
        std::vector<uint8_t> c; be32(c, static_cast<uint32_t>(data.size())); c.insert(c.end(), type, type + 4); c.insert(c.end(), data.begin(), data.end());
        be32(c, ~crc32_ieee(c.data() + 4, c.size() - 4)); out.write(reinterpret_cast<const char *>(c.data()), static_cast<std::streamsize>(c.size()));
    };
    std::vector<uint8_t> ihdr; be32(ihdr, static_cast<uint32_t>(page.width)); be32(ihdr, static_cast<uint32_t>(page.height));
    ihdr.insert(ihdr.end(), {1, 0, 0, 0, 0});  // bit depth 1, greyscale
    chunk("IHDR", ihdr);
    std::vector<uint8_t> phys; const uint32_t ppm = static_cast<uint32_t>(std::lround(dpi / 0.0254)); be32(phys, ppm); be32(phys, ppm); phys.push_back(1);
    chunk("pHYs", phys);
    chunk("IDAT", z); chunk("IEND", {});
    return static_cast<bool>(out);
}

// Draws a CKey SVG as black ink on paper at a physical width (the 31-unit
// canvas including its quiet margin) on the device dot grid.
inline void draw_symbol(Page &page, const std::string &svg, int x0, int y0, int side_px) {
    const auto cover = svgtest::rasterize(svgtest::parse(svg), side_px, side_px, 3, 0, 255);
    for (int y = 0; y < side_px; ++y) for (int x = 0; x < side_px; ++x) {
        const int px = x0 + x, py = y0 + y;
        if (px >= 0 && py >= 0 && px < page.width && py < page.height && cover[static_cast<size_t>(y) * side_px + x] >= 128)
            page.ink[static_cast<size_t>(py) * page.width + px] = 1;
    }
}

inline void draw_text(Page &page, const std::string &text, int x, int y, int scale) {
    static const std::map<char, std::array<uint8_t, 7>> font = {
        {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}}, {'2', {14, 17, 1, 2, 4, 8, 31}}, {'3', {31, 2, 4, 2, 1, 17, 14}},
        {'4', {2, 6, 10, 18, 31, 2, 2}}, {'5', {31, 16, 30, 1, 1, 17, 14}}, {'6', {6, 8, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
        {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 2, 12}}, {'.', {0, 0, 0, 0, 0, 12, 12}}, {' ', {0, 0, 0, 0, 0, 0, 0}},
        {'P', {30, 17, 17, 30, 16, 16, 16}}, {'v', {0, 0, 17, 17, 17, 10, 4}}, {'B', {30, 17, 17, 30, 17, 17, 30}}, {'i', {4, 0, 12, 4, 4, 4, 14}},
        {'n', {0, 0, 22, 25, 17, 17, 17}}, {'M', {17, 27, 21, 21, 17, 17, 17}}, {'d', {1, 1, 13, 19, 17, 17, 15}}, {'p', {0, 0, 30, 17, 30, 16, 16}},
        {'C', {14, 17, 16, 16, 16, 17, 14}}, {'K', {17, 18, 20, 24, 20, 18, 17}}, {'e', {0, 0, 14, 17, 31, 16, 14}}, {'y', {0, 0, 17, 17, 15, 1, 14}},
        {'r', {0, 0, 22, 25, 16, 16, 16}}, {'t', {8, 8, 28, 8, 8, 9, 6}}, {'a', {0, 0, 14, 1, 15, 17, 15}}, {'s', {0, 0, 15, 16, 14, 1, 30}},
        {'l', {12, 4, 4, 4, 4, 4, 14}}, {'o', {0, 0, 14, 17, 17, 17, 14}}, {'g', {0, 0, 15, 17, 15, 1, 14}}, {'@', {14, 17, 23, 21, 23, 16, 15}},
        {'-', {0, 0, 0, 31, 0, 0, 0}}, {'h', {16, 16, 22, 25, 17, 17, 17}}, {'c', {0, 0, 14, 16, 16, 17, 14}}, {'b', {16, 16, 22, 25, 17, 17, 30}},
        {'L', {16, 16, 16, 16, 16, 16, 31}}, {'S', {15, 16, 16, 14, 1, 1, 30}}, {'A', {14, 17, 17, 31, 17, 17, 17}}, {'D', {28, 18, 17, 17, 17, 18, 28}},
        {'I', {14, 4, 4, 4, 4, 4, 14}}, {'x', {0, 0, 17, 10, 4, 10, 17}}, {'m', {0, 0, 26, 21, 21, 17, 17}}, {'u', {0, 0, 17, 17, 17, 19, 13}},
        {'f', {6, 9, 8, 28, 8, 8, 8}}, {'%', {24, 25, 2, 4, 8, 19, 3}}, {'z', {0, 0, 31, 2, 4, 8, 31}}, {'/', {1, 1, 2, 4, 8, 16, 16}}, {'k', {16, 16, 18, 20, 24, 20, 18}}, {'w', {0, 0, 17, 17, 21, 21, 10}}, {'=', {0, 0, 31, 0, 31, 0, 0}}};
    for (char ch : text) {
        const auto it = font.find(ch);
        if (it != font.end()) for (int r = 0; r < 7; ++r) for (int c = 0; c < 5; ++c) if (it->second[r] & (16 >> c))
            for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx) {
                const int px = x + c * scale + dx, py = y + r * scale + dy;
                if (px >= 0 && py >= 0 && px < page.width && py < page.height) page.ink[static_cast<size_t>(py) * page.width + px] = 1;
            }
        x += 6 * scale;
    }
}

}  // namespace printout
#endif
