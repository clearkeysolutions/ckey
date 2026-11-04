// Render -> rasterize -> decode round trips for all three profiles. The
// raster comes from the emitted SVG (tests/svg_raster.h), so these tests fail
// if the printed geometry and the image decoder ever disagree.
#include "ckey/ckey.h"
#include "check.h"
#include "svg_raster.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Image { std::vector<uint8_t> px; int side = 0; double lo = 0, hi = 0; };

std::vector<uint8_t> pseudo_random(size_t n, uint32_t seed) {
    std::vector<uint8_t> out(n);
    for (auto &b : out) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(seed >> 24); }
    return out;
}

std::string render(int profile, const std::vector<uint8_t> &payload) {
    ckey_diagnostics d{}; size_t n = 0; std::string svg;
    if (profile == 1) {
        ckey_symbol s{}; CKEY_CHECK(ckey_encode_locator(payload.data(), payload.size(), &s, &d) == CKEY_OK);
        CKEY_CHECK(ckey_render_svg(&s, 8, nullptr, 0, &n) == CKEY_INSUFFICIENT_BUFFER); svg.resize(n);
        CKEY_CHECK(ckey_render_svg(&s, 8, svg.data(), n, &n) == CKEY_OK);
    } else if (profile == 2) {
        ckey_text_symbol s{}; CKEY_CHECK(ckey_encode_text(payload.data(), payload.size(), &s, &d) == CKEY_OK);
        CKEY_CHECK(ckey_render_text_svg(&s, 8, nullptr, 0, &n) == CKEY_INSUFFICIENT_BUFFER); svg.resize(n);
        CKEY_CHECK(ckey_render_text_svg(&s, 8, svg.data(), n, &n) == CKEY_OK);
    } else {
        ckey_extended_symbol s{}; CKEY_CHECK(ckey_encode_extended(payload.data(), payload.size(), &s, &d) == CKEY_OK);
        CKEY_CHECK(ckey_render_extended_svg(&s, 8, nullptr, 0, &n) == CKEY_INSUFFICIENT_BUFFER); svg.resize(n);
        CKEY_CHECK(ckey_render_extended_svg(&s, 8, svg.data(), n, &n) == CKEY_OK);
    }
    svg.resize(n - 1);
    return svg;
}

// Canonical symbol coordinates span [0,29] (P1/P2) or [0,116] (P3) inside a
// 31- or 124-unit viewBox with a 1- or 4-unit margin.
Image raster(int profile, const std::string &svg, int px_per_unit) {
    const int units = profile == 3 ? 124 : 31, margin = profile == 3 ? 4 : 1, field = profile == 3 ? 116 : 29;
    Image img; img.side = units * px_per_unit;
    img.px = svgtest::rasterize(svgtest::parse(svg), img.side, img.side, 4);
    img.lo = static_cast<double>(margin * px_per_unit);
    img.hi = static_cast<double>((margin + field) * px_per_unit);
    return img;
}

ckey_status decode(int profile, const std::vector<uint8_t> &px, size_t side, const double quad[8],
                   std::vector<uint8_t> &out) {
    std::vector<uint8_t> buffer(CKEY_EXTENDED_PAYLOAD_MAX_BYTES); size_t length = 0; ckey_diagnostics d{};
    ckey_status st;
    if (profile == 1) { st = ckey_decode_gray8(px.data(), side, side, side, quad, buffer.data(), buffer.size(), &d); length = 16; }
    else if (profile == 2) st = ckey_decode_text_gray8(px.data(), side, side, side, quad, buffer.data(), buffer.size(), &length, &d);
    else st = ckey_decode_extended_gray8(px.data(), side, side, side, quad, buffer.data(), buffer.size(), &length, &d);
    out.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(st == CKEY_OK ? length : 0));
    return st;
}

void expect_decodes(int profile, const Image &img, const std::vector<uint8_t> &payload, const char *label) {
    const double q[8] = {img.lo, img.lo, img.hi, img.lo, img.hi, img.hi, img.lo, img.hi};
    std::vector<uint8_t> out;
    const ckey_status st = decode(profile, img.px, static_cast<size_t>(img.side), q, out);
    if (st != CKEY_OK || out != payload) std::fprintf(stderr, "profile %d round trip failed: %s (status %d)\n", profile, label, st);
    CKEY_CHECK(st == CKEY_OK && out == payload);
}

// Projective map from four source corners to four destination corners.
void homography(const double src[8], const double dst[8], double h[8]) {
    double a[8][9]{};
    for (int i = 0; i < 4; ++i) {
        const double x = src[2 * i], y = src[2 * i + 1], u = dst[2 * i], v = dst[2 * i + 1];
        const double r0[9] = {x, y, 1, 0, 0, 0, -u * x, -u * y, u}, r1[9] = {0, 0, 0, x, y, 1, -v * x, -v * y, v};
        for (int j = 0; j < 9; ++j) { a[2 * i][j] = r0[j]; a[2 * i + 1][j] = r1[j]; }
    }
    for (int c = 0; c < 8; ++c) {
        int p = c; for (int r = c + 1; r < 8; ++r) if (std::abs(a[r][c]) > std::abs(a[p][c])) p = r;
        for (int j = 0; j < 9; ++j) std::swap(a[p][j], a[c][j]);
        for (int j = 8; j >= c; --j) a[c][j] /= a[c][c];
        for (int r = 0; r < 8; ++r) if (r != c) { const double f = a[r][c]; for (int j = c; j < 9; ++j) a[r][j] -= f * a[c][j]; }
    }
    for (int i = 0; i < 8; ++i) h[i] = a[i][8];
}

void apply(const double h[8], double x, double y, double &u, double &v) {
    const double d = h[6] * x + h[7] * y + 1; u = (h[0] * x + h[1] * y + h[2]) / d; v = (h[3] * x + h[4] * y + h[5]) / d;
}

// Resample through a mild keystone (top edge narrowed by 6% per side) with
// bilinear inverse mapping; returns the warped image and the symbol quad.
std::vector<uint8_t> keystone(const Image &img, double quad_out[8]) {
    const double s = img.side, inset = 0.06 * s;
    const double full[8] = {0, 0, s, 0, s, s, 0, s}, warped[8] = {inset, 0, s - inset, 0, s, s, 0, s};
    double forward[8], inverse[8]; homography(full, warped, forward); homography(warped, full, inverse);
    const int n = img.side; std::vector<uint8_t> out(img.px.size(), 5);
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
        double u, v; apply(inverse, x + .5, y + .5, u, v); u -= .5; v -= .5;
        if (u < 0 || v < 0 || u >= n - 1 || v >= n - 1) continue;
        const int ix = static_cast<int>(u), iy = static_cast<int>(v); const double fx = u - ix, fy = v - iy;
        const auto at = [&](int xx, int yy) { return static_cast<double>(img.px[static_cast<size_t>(yy) * n + xx]); };
        out[static_cast<size_t>(y) * n + x] = static_cast<uint8_t>(std::lround(
            (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) + (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy));
    }
    const double corners[8] = {img.lo, img.lo, img.hi, img.lo, img.hi, img.hi, img.lo, img.hi};
    for (int i = 0; i < 4; ++i) apply(forward, corners[2 * i], corners[2 * i + 1], quad_out[2 * i], quad_out[2 * i + 1]);
    return out;
}

void box_blur(Image &img) {
    const int n = img.side; std::vector<uint8_t> out(img.px);
    for (int y = 1; y < n - 1; ++y) for (int x = 1; x < n - 1; ++x) {
        int sum = 0; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) sum += img.px[static_cast<size_t>(y + dy) * n + x + dx];
        out[static_cast<size_t>(y) * n + x] = static_cast<uint8_t>(sum / 9);
    }
    img.px = out;
}

}  // namespace

int main() {
    // Pixels per symbol unit. Measured clean-render floors with an exact quad
    // are P1 5-6, P2 10, P3 11; these densities leave margin for the blur and
    // keystone cases below (P1 needs 10 to survive the 3x3 blur).
    const int density[4] = {0, 10, 14, 16};
    for (int profile = 1; profile <= 3; ++profile) {
        const size_t max = profile == 1 ? 16 : profile == 2 ? CKEY_TEXT_PAYLOAD_MAX_BYTES : CKEY_EXTENDED_PAYLOAD_MAX_BYTES;
        std::vector<std::pair<const char *, std::vector<uint8_t>>> payloads = {
            {"random", pseudo_random(max, 0xC0FFEEu + profile)},
            {"all-zero", std::vector<uint8_t>(max, 0x00)},
            {"all-0xFF", std::vector<uint8_t>(max, 0xFF)},
        };
        if (profile != 1) payloads.push_back({"short", std::vector<uint8_t>{'C', 'K', 'e', 'y'}});
        if (profile == 3) payloads.push_back({"tier-768", pseudo_random(700, 7)});
        for (const auto &[label, payload] : payloads) expect_decodes(profile, raster(profile, render(profile, payload), density[profile]), payload, label);

        // Orientation, winding, and polarity are resolved by the decoder.
        const auto payload = pseudo_random(profile == 1 ? 16 : profile == 2 ? 120 : 1500, 99u + profile);
        Image img = raster(profile, render(profile, payload), density[profile]);
        std::vector<uint8_t> out;
        const double rotated[8] = {img.hi, img.lo, img.hi, img.hi, img.lo, img.hi, img.lo, img.lo};
        const double reversed[8] = {img.lo, img.lo, img.lo, img.hi, img.hi, img.hi, img.hi, img.lo};
        CKEY_CHECK(decode(profile, img.px, img.side, rotated, out) == CKEY_OK && out == payload);
        CKEY_CHECK(decode(profile, img.px, img.side, reversed, out) == CKEY_OK && out == payload);
        Image inverted = img; for (auto &p : inverted.px) p = static_cast<uint8_t>(255 - p);
        expect_decodes(profile, inverted, payload, "inverted polarity");

        // Mild perspective (6% keystone) and a 3x3 box blur.
        double quad[8]; const auto warped = keystone(img, quad);
        CKEY_CHECK(decode(profile, warped, img.side, quad, out) == CKEY_OK && out == payload);
        Image blurred = img; box_blur(blurred);
        expect_decodes(profile, blurred, payload, "3x3 box blur");
        std::printf("profile %d: round trips passed at %d px/unit\n", profile, density[profile]);
    }
    // Profile 1 near its clean-render floor: 24 random locators at 6 px/unit.
    // (Dev.48 failed about half of these: its empty reference was a payload module.)
    for (uint32_t t = 0; t < 24; ++t) {
        const auto payload = pseudo_random(16, 1234u + 77u * t);
        expect_decodes(1, raster(1, render(1, payload), 6), payload, "6 px/unit sweep");
    }
    std::printf("profile 1: 24 random locators decoded at 6 px/unit\n");
    std::printf("CKey render/decode round-trip tests passed\n");
}
