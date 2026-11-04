// Simulated reader: renders symbols, composites them into full camera-sized
// frames with rotation, keystone, blur, noise, and clutter, then runs the
// exact pipeline the iPhone reader uses -- ckey_find_borders_gray8, then
// ckey_refine_quad_gray8 per profile, then the ckey_decode_*_gray8 calls.
#include "ckey/ckey.h"
#include "check.h"
#include "svg_raster.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Rng { uint32_t s; uint32_t next() { s = s * 1664525u + 1013904223u; return s; } double unit() { return (next() >> 8) / 16777216.0; } };

// Profile 0 symbols are encoded at this version (set per case).
uint32_t g_scalable_version = 1;

std::vector<uint8_t> payload_for(int profile, uint32_t seed) {
    const size_t n = profile == 0 ? ckey_scalable_capacity(g_scalable_version, CKEY_ECC_M, CKEY_LAYOUT_STANDARD)
                   : profile == 1 ? 16 : profile == 2 ? 120 : 1200;
    std::vector<uint8_t> p(n); Rng r{seed}; for (auto &b : p) b = static_cast<uint8_t>(r.next() >> 24); return p;
}

std::string render(int profile, const std::vector<uint8_t> &payload) {
    ckey_diagnostics d{}; size_t n = 0; std::string svg;
    if (profile == 0) { ckey_scalable_symbol s{}; ckey_encode_scalable(payload.data(), payload.size(), g_scalable_version, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d); ckey_render_scalable_svg(&s, 8, nullptr, 0, &n); svg.resize(n); ckey_render_scalable_svg(&s, 8, svg.data(), n, &n); }
    else if (profile == 1) { ckey_symbol s{}; ckey_encode_locator(payload.data(), payload.size(), &s, &d); ckey_render_svg(&s, 8, nullptr, 0, &n); svg.resize(n); ckey_render_svg(&s, 8, svg.data(), n, &n); }
    else if (profile == 2) { ckey_text_symbol s{}; ckey_encode_text(payload.data(), payload.size(), &s, &d); ckey_render_text_svg(&s, 8, nullptr, 0, &n); svg.resize(n); ckey_render_text_svg(&s, 8, svg.data(), n, &n); }
    else { ckey_extended_symbol s{}; ckey_encode_extended(payload.data(), payload.size(), &s, &d); ckey_render_extended_svg(&s, 8, nullptr, 0, &n); svg.resize(n); ckey_render_extended_svg(&s, 8, svg.data(), n, &n); }
    svg.resize(n - 1); return svg;
}

void solve(const double src[8], const double dst[8], double h[8]) {
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

struct Scene { int profile; int frame_w, frame_h; double symbol_px; double rotation_deg; double keystone; bool blur; bool dark_on_light; uint32_t seed; };

// Builds a camera frame: clutter background, the symbol (including its own
// quiet margin) warped by rotation + keystone, optional 3x3 blur, noise.
std::vector<uint8_t> make_frame(const Scene &sc, const std::string &svg) {
    const int units = sc.profile == 3 ? 124 : 31;
    const int raster = static_cast<int>(std::ceil(sc.symbol_px * units / (sc.profile == 3 ? 116.0 : 29.0)));
    auto sym = svgtest::rasterize(svgtest::parse(svg), raster, raster, 3);
    if (sc.dark_on_light) for (auto &v : sym) v = static_cast<uint8_t>(250 - v);
    std::vector<uint8_t> frame(static_cast<size_t>(sc.frame_w) * sc.frame_h);
    Rng rng{sc.seed};
    for (int y = 0; y < sc.frame_h; ++y) for (int x = 0; x < sc.frame_w; ++x)
        frame[static_cast<size_t>(y) * sc.frame_w + x] = static_cast<uint8_t>(70 + 50.0 * x / sc.frame_w + 30.0 * y / sc.frame_h);
    for (int k = 0; k < 14; ++k) {  // clutter rectangles of varied brightness
        const int x0 = static_cast<int>(rng.unit() * sc.frame_w), y0 = static_cast<int>(rng.unit() * sc.frame_h);
        const int w = 20 + static_cast<int>(rng.unit() * sc.frame_w / 5), h = 20 + static_cast<int>(rng.unit() * sc.frame_h / 5);
        const uint8_t v = static_cast<uint8_t>(rng.unit() * 255);
        for (int y = y0; y < std::min(sc.frame_h, y0 + h); ++y) for (int x = x0; x < std::min(sc.frame_w, x0 + w); ++x) frame[static_cast<size_t>(y) * sc.frame_w + x] = v;
    }
    // Destination corners of the symbol raster in the frame.
    const double cx = sc.frame_w / 2.0, cy = sc.frame_h / 2.0, half = raster / 2.0, a = sc.rotation_deg * 3.14159265358979 / 180;
    double dst[8]; const double corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 4; ++i) {
        double x = corner[i][0] * half, y = corner[i][1] * half;
        x *= 1.0 - sc.keystone * (corner[i][1] < 0 ? 1 : 0);  // narrow the top edge
        dst[2 * i] = cx + x * std::cos(a) - y * std::sin(a); dst[2 * i + 1] = cy + x * std::sin(a) + y * std::cos(a);
    }
    const double src[8] = {0, 0, static_cast<double>(raster), 0, static_cast<double>(raster), static_cast<double>(raster), 0, static_cast<double>(raster)};
    double inv[8]; solve(dst, src, inv);
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9; for (int i = 0; i < 4; ++i) { x0 = std::min(x0, dst[2 * i]); x1 = std::max(x1, dst[2 * i]); y0 = std::min(y0, dst[2 * i + 1]); y1 = std::max(y1, dst[2 * i + 1]); }
    for (int y = std::max(0, static_cast<int>(y0)); y < std::min(sc.frame_h, static_cast<int>(y1) + 1); ++y)
        for (int x = std::max(0, static_cast<int>(x0)); x < std::min(sc.frame_w, static_cast<int>(x1) + 1); ++x) {
            const double d = inv[6] * x + inv[7] * y + 1, u = (inv[0] * x + inv[1] * y + inv[2]) / d - .5, v = (inv[3] * x + inv[4] * y + inv[5]) / d - .5;
            if (u < 0 || v < 0 || u >= raster - 1 || v >= raster - 1) continue;
            const int iu = static_cast<int>(u), iv = static_cast<int>(v); const double fu = u - iu, fv = v - iv;
            const auto at = [&](int xx, int yy) { return static_cast<double>(sym[static_cast<size_t>(yy) * raster + xx]); };
            frame[static_cast<size_t>(y) * sc.frame_w + x] = static_cast<uint8_t>(std::lround(
                (at(iu, iv) * (1 - fu) + at(iu + 1, iv) * fu) * (1 - fv) + (at(iu, iv + 1) * (1 - fu) + at(iu + 1, iv + 1) * fu) * fv));
        }
    if (sc.blur) {
        std::vector<uint8_t> out(frame);
        for (int y = 1; y < sc.frame_h - 1; ++y) for (int x = 1; x < sc.frame_w - 1; ++x) {
            int t = 0; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) t += frame[static_cast<size_t>(y + dy) * sc.frame_w + x + dx];
            out[static_cast<size_t>(y) * sc.frame_w + x] = static_cast<uint8_t>(t / 9);
        }
        frame.swap(out);
    }
    for (auto &v : frame) v = static_cast<uint8_t>(std::clamp(static_cast<int>(v) + static_cast<int>(rng.unit() * 13) - 6, 0, 255));
    return frame;
}

// The reader's loop: up to six border candidates; for each, refine to every
// profile's field and try each decoder. Returns the decoded profile or 0.
int scan(const std::vector<uint8_t> &frame, int w, int h, const std::vector<uint8_t> &expected, size_t *borders_found) {
    double borders[6 * 8];
    const size_t n = ckey_find_borders_gray8(frame.data(), w, h, w, borders, 6);
    if (borders_found) *borders_found = n;
    std::vector<uint8_t> out(65535); size_t len = 0; ckey_diagnostics d{};  // P0 payloads exceed 2,048 bytes
    for (size_t i = 0; i < n; ++i) {
        double q[8]; uint32_t version = 0;
        if (ckey_refine_quad_gray8(frame.data(), w, h, w, borders + 8 * i, CKEY_FIELD_SCALABLE, q) > 0 &&
            ckey_decode_scalable_gray8(frame.data(), w, h, w, q, out.data(), out.size(), &len, &version, nullptr, &d) == CKEY_OK &&
            len == expected.size() && std::equal(expected.begin(), expected.end(), out.begin())) return 0;
        if (ckey_refine_quad_gray8(frame.data(), w, h, w, borders + 8 * i, CKEY_FIELD_LOCATOR, q) > 0 &&
            ckey_decode_gray8(frame.data(), w, h, w, q, out.data(), out.size(), &d) == CKEY_OK &&
            expected.size() == 16 && std::equal(expected.begin(), expected.end(), out.begin())) return 1;
        if (ckey_refine_quad_gray8(frame.data(), w, h, w, borders + 8 * i, CKEY_FIELD_TEXT, q) > 0 &&
            ckey_decode_text_gray8(frame.data(), w, h, w, q, out.data(), out.size(), &len, &d) == CKEY_OK &&
            len == expected.size() && std::equal(expected.begin(), expected.end(), out.begin())) return 2;
        if (ckey_refine_quad_gray8(frame.data(), w, h, w, borders + 8 * i, CKEY_FIELD_EXTENDED, q) > 0 &&
            ckey_decode_extended_gray8(frame.data(), w, h, w, q, out.data(), out.size(), &len, &d) == CKEY_OK &&
            len == expected.size() && std::equal(expected.begin(), expected.end(), out.begin())) return 3;
    }
    return -1;
}

}  // namespace

int main() {
    struct Case { int profile, frame_w, frame_h; double px_per_unit; uint32_t version; };
    // Field width in pixels = px/unit x field units; frame sizes match the
    // reader's portrait capture (1080p and 4K).
    // Profile 0 cases give px/unit as 8 px per module (px/unit = 8 / pitch).
    const Case cases[] = {{1, 1080, 1920, 10, 0}, {1, 1080, 1920, 14, 0}, {2, 1080, 1920, 14, 0}, {2, 1080, 1920, 20, 0},
                          {3, 2160, 3840, 13, 0}, {3, 2160, 3840, 16, 0},
                          {0, 1080, 1920, 8.0 * 29 / 29, 1}, {0, 1080, 1920, 8.0 * 31 / 29, 2}, {0, 1080, 1920, 8.0 * 58 / 29, 11},
                          {0, 1080, 1920, 8.0 * 108 / 29, 20}, {0, 2160, 3840, 7.0 * 232 / 29, 31}};
    const double rotations[] = {0, 7, 18, 33};
    int total = 0, passed = 0, skipped = 0;
    for (const auto &c : cases) {
        g_scalable_version = c.version;
        int ok = 0, n = 0;
        for (double rotation : rotations) for (double keystone : {0.0, 0.08}) for (int variant = 0; variant < 2; ++variant) {
            const Scene sc{c.profile, c.frame_w, c.frame_h, c.px_per_unit * (c.profile == 3 ? 116 : 29), rotation, keystone,
                           variant == 1, variant == 1 && c.profile != 3, static_cast<uint32_t>(rotation * 13 + keystone * 100 + variant)};
            // Skip scenes where the rotated symbol cannot fit inside the frame
            // (no reader could see its whole border).
            const double extent = sc.symbol_px * (c.profile == 3 ? 124.0 / 116 : 31.0 / 29) *
                                  (std::abs(std::cos(rotation * 3.14159265358979 / 180)) + std::abs(std::sin(rotation * 3.14159265358979 / 180)));
            if (extent > 0.98 * std::min(c.frame_w, c.frame_h)) { ++skipped; continue; }
            const auto payload = payload_for(c.profile, 7u + static_cast<uint32_t>(rotation) + variant);
            const auto frame = make_frame(sc, render(c.profile, payload));
            size_t borders = 0;
            const int result = scan(frame, c.frame_w, c.frame_h, payload, &borders);
            ++n; ok += result == c.profile;
            if (result != c.profile)
                std::printf("  miss: P%d %.0f px/unit rot %.0f keystone %.2f %s%s (borders found %zu)\n", c.profile, c.px_per_unit, rotation,
                            keystone, variant ? "blur" : "sharp", sc.dark_on_light ? " dark-on-light" : "", borders);
        }
        if (c.profile == 0) std::printf("P0 v%u %dx%d @ %.0f px/unit: %d/%d scenes decoded\n", c.version, c.frame_w, c.frame_h, c.px_per_unit, ok, n);
        else std::printf("P%d %dx%d @ %.0f px/unit: %d/%d scenes decoded\n", c.profile, c.frame_w, c.frame_h, c.px_per_unit, ok, n);
        total += n; passed += ok;
    }
    std::printf("simulated reader: %d/%d scenes decoded (%d impossible scenes skipped)\n", passed, total, skipped);
    CKEY_CHECK(passed == total);
    std::printf("CKey reader pipeline tests passed\n");
}
