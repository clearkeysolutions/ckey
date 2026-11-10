// Print + camera simulation for CKey symbols (tests and tools only).
// A symbol SVG is printed at a device resolution (binary ink dots, no grey),
// ink spreads by a radius and blurs into the paper, then a camera with a
// given resolution (pixels per millimetre), optical blur, and noise views it.
// The output is the grayscale frame a decoder would see.
#ifndef CKEY_TESTS_PRINT_SIM_H
#define CKEY_TESTS_PRINT_SIM_H

#include "svg_raster.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace printsim {

struct Printer { double dpi; double spread_mm; double blur_mm; double compensate_mm = 0; };  // ink spread (dot gain), paper wicking, pre-shrink of ink
struct Camera { double px_per_mm; double blur_px; double noise; };  // optics blur sigma; noise amplitude (grey levels)

inline void gaussian(std::vector<double> &img, int w, int h, double sigma) {
    if (sigma < 0.05) return;
    const int r = static_cast<int>(std::ceil(3 * sigma)); std::vector<double> k(2 * r + 1); double sum = 0;
    for (int i = -r; i <= r; ++i) sum += k[i + r] = std::exp(-0.5 * i * i / (sigma * sigma));
    for (auto &v : k) v /= sum;
    std::vector<double> tmp(img.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { double a = 0; for (int i = -r; i <= r; ++i) a += k[i + r] * img[static_cast<size_t>(y) * w + std::clamp(x + i, 0, w - 1)]; tmp[static_cast<size_t>(y) * w + x] = a; }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { double a = 0; for (int i = -r; i <= r; ++i) a += k[i + r] * tmp[static_cast<size_t>(std::clamp(y + i, 0, h - 1)) * w + x]; img[static_cast<size_t>(y) * w + x] = a; }
}

// marks_are_ink: print the symbol's marks in ink on white paper (dark-on-
// light). Otherwise the background is inked and the marks are bare paper.
// Returns the camera frame; side_px receives its size. The symbol's 31-unit
// canvas is printed at width_mm.
inline std::vector<uint8_t> print_and_capture(const std::string &svg_text, double width_mm, const Printer &pr, const Camera &cam,
                                              bool marks_are_ink, uint32_t seed, int &side_px) {
    const auto svg = svgtest::parse(svg_text);
    const int dev = std::max(8, static_cast<int>(std::lround(width_mm / 25.4 * pr.dpi)));
    const auto cover = svgtest::rasterize(svg, dev, dev, 3, 0, 255);
    std::vector<uint8_t> ink(cover.size());
    for (size_t i = 0; i < cover.size(); ++i) ink[i] = marks_are_ink ? cover[i] >= 128 : cover[i] < 128;  // binary device dots
    if (pr.compensate_mm > 0) {  // print-side compensation: erode ink, keeping at least one dot of every mark
        std::vector<uint8_t> paper_mask(ink.size()); for (size_t i = 0; i < ink.size(); ++i) paper_mask[i] = !ink[i];
        const auto to_paper = svgtest::distance_to_set(paper_mask, dev, dev);
        const double c = pr.compensate_mm / 25.4 * pr.dpi;
        for (size_t i = 0; i < ink.size(); ++i) if (ink[i] && to_paper[i] <= c) ink[i] = 0;
    }
    // Dot gain: ink spreads outward by spread_mm.
    const auto dist = svgtest::distance_to_set(ink, dev, dev);
    const double spread_px = pr.spread_mm / 25.4 * pr.dpi;
    std::vector<double> paper(cover.size());
    for (size_t i = 0; i < paper.size(); ++i) paper[i] = dist[i] <= spread_px ? 28.0 : 236.0;
    gaussian(paper, dev, dev, pr.blur_mm / 25.4 * pr.dpi);
    // Camera: area-sample the printed sheet at px_per_mm, then optics + noise.
    side_px = std::max(8, static_cast<int>(std::lround(width_mm * cam.px_per_mm)));
    std::vector<double> frame(static_cast<size_t>(side_px) * side_px);
    const double scale = static_cast<double>(dev) / side_px;
    for (int y = 0; y < side_px; ++y) for (int x = 0; x < side_px; ++x) {
        const double x0 = x * scale, y0 = y * scale; double a = 0; int n = 0;
        for (double yy = y0; yy < y0 + scale; yy += std::max(0.5, scale / 4)) for (double xx = x0; xx < x0 + scale; xx += std::max(0.5, scale / 4)) {
            a += paper[static_cast<size_t>(std::min(dev - 1, static_cast<int>(yy))) * dev + std::min(dev - 1, static_cast<int>(xx))]; ++n;
        }
        frame[static_cast<size_t>(y) * side_px + x] = a / n;
    }
    gaussian(frame, side_px, side_px, cam.blur_px);
    std::vector<uint8_t> out(frame.size());
    for (size_t i = 0; i < out.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        out[i] = static_cast<uint8_t>(std::clamp(frame[i] + cam.noise * (((seed >> 8) & 0xffff) / 32768.0 - 1.0), 0.0, 255.0));
    }
    return out;
}

}  // namespace printsim
#endif
