// Print output for the CLI: 1-bit PNG on the printer's exact dot grid (no
// anti-aliasing, so no grey for the printer to halftone into noise), with a
// pHYs chunk so the image prints at its true physical size, plus a tiny 5x7
// bitmap font for calibration-sheet labels. Uses only the core's C ABI.
#ifndef CKEY_CLI_PRINT_OUTPUT_H
#define CKEY_CLI_PRINT_OUTPUT_H

#include "ckey/ckey.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace printout {

struct Page { int width = 0, height = 0; std::vector<uint8_t> ink; };  // 1 = ink

// 1-bit PNG with pHYs, through the core's PNG encoder.
inline bool write_png(const std::string &path, const Page &page, double dpi) {
    std::vector<uint8_t> gray(page.ink.size());
    for (size_t i = 0; i < gray.size(); ++i) gray[i] = page.ink[i] ? 0 : 255;
    size_t need = 0;
    if (ckey_encode_png(gray.data(), static_cast<uint32_t>(page.width), static_cast<uint32_t>(page.height), 0, CKEY_PNG_GRAY1, dpi, nullptr, 0, &need) != CKEY_INSUFFICIENT_BUFFER) return false;
    std::vector<uint8_t> png(need);
    if (ckey_encode_png(gray.data(), static_cast<uint32_t>(page.width), static_cast<uint32_t>(page.height), 0, CKEY_PNG_GRAY1, dpi, png.data(), png.size(), &need) != CKEY_OK) return false;
    std::ofstream out(path, std::ios::binary); if (!out) return false;
    out.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

// Draws a Profile 0 symbol as black ink on paper at a physical width (the
// 31-unit canvas including its quiet margin) on the device dot grid, with
// the core's direct print rasterizer.
inline bool draw_symbol(Page &page, const ckey_scalable_symbol &symbol, const ckey_style *style, int x0, int y0, int side_px, double dpi) {
    size_t side = 0;
    if (ckey_render_scalable_print(&symbol, style, side_px / dpi, dpi, nullptr, 0, &side) != CKEY_INSUFFICIENT_BUFFER) return false;
    std::vector<uint8_t> px(side * side);
    if (ckey_render_scalable_print(&symbol, style, side_px / dpi, dpi, px.data(), px.size(), &side) != CKEY_OK) return false;
    for (size_t y = 0; y < side; ++y) for (size_t x = 0; x < side; ++x) {
        const long px_x = x0 + static_cast<long>(x), px_y = y0 + static_cast<long>(y);
        if (px_x >= 0 && px_y >= 0 && px_x < page.width && px_y < page.height && px[y * side + x] == 0)
            page.ink[static_cast<size_t>(px_y) * page.width + px_x] = 1;
    }
    return true;
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
