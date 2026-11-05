// CKey optical benchmark harness (specification §5, §6).
//
// Generate -> degrade -> decode -> measure, with the real CKey engine and
// the same frames for every symbology. For each payload, acquisition
// condition and printed side (in camera pixels, including each symbology's
// quiet zone, so symbols are compared at equal area) it measures:
//   - payload recovery rate (exact bytes),
//   - corrected Reed-Solomon bytes (a bit-error proxy),
//   - per-frame latency (find borders -> refine -> decode), as percentiles,
//   - the smallest side that decodes in >= 90% of trials, and the payload
//     bytes recoverable per 1,000 px^2 at that size,
// plus a false-positive count on symbol-free clutter frames.
//
// Built with -DCKEY_BENCH_ZXING=ON and zxing-cpp installed, the same frames
// are also generated and read as QR Code and Data Matrix for comparison.
// All frames are synthetic; results are not real-camera measurements.
//
//   ckey-bench [--quick] [--json out.json] [--md out.md]
#include "ckey/ckey.h"

#ifdef CKEY_BENCH_ZXING
#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>
#include <ZXing/ReadBarcode.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Rng { uint32_t s; uint32_t next() { s = s * 1664525u + 1013904223u; return s; } double unit() { return (next() >> 8) / 16777216.0; }
    double gauss() { const double u1 = unit() + 1e-9, u2 = unit(); return std::sqrt(-2 * std::log(u1)) * std::cos(6.283185307179586 * u2); } };

struct Condition { const char *name; double blur, noise, rotation_deg, keystone, glare; };
struct Image { int side = 0; std::vector<uint8_t> px; };  // square symbol raster, quiet zone included
struct Outcome { bool ok = false; double ms = 0; int corrected = 0; };

struct Symbology {
    std::string name;
    std::function<bool(const std::string &, int side, Image &)> render;  // false: payload does not fit
    std::function<Outcome(const std::vector<uint8_t> &, int, int, const std::string &)> scan;
};

double now_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

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

void blur(std::vector<double> &img, int w, int h, double sigma) {
    if (sigma <= 0.05) return;
    const int r = std::max(1, static_cast<int>(std::ceil(3 * sigma)));
    std::vector<double> k(static_cast<size_t>(2 * r + 1)); double sum = 0;
    for (int i = -r; i <= r; ++i) sum += k[static_cast<size_t>(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma));
    for (auto &v : k) v /= sum;
    std::vector<double> t(img.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { double a = 0; for (int i = -r; i <= r; ++i) a += k[static_cast<size_t>(i + r)] * img[static_cast<size_t>(y) * w + std::clamp(x + i, 0, w - 1)]; t[static_cast<size_t>(y) * w + x] = a; }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { double a = 0; for (int i = -r; i <= r; ++i) a += k[static_cast<size_t>(i + r)] * t[static_cast<size_t>(std::clamp(y + i, 0, h - 1)) * w + x]; img[static_cast<size_t>(y) * w + x] = a; }
}

// A camera frame: lit gradient, clutter rectangles, the symbol warped by
// rotation and keystone, then glare, defocus and sensor noise.
std::vector<uint8_t> make_frame(const Image *sym, int W, int H, const Condition &c, uint32_t seed) {
    Rng rng{seed};
    std::vector<double> f(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) f[static_cast<size_t>(y) * W + x] = 0.35 + 0.25 * x / W + 0.15 * y / H;
    for (int k = 0; k < 16; ++k) {
        const int x0 = static_cast<int>(rng.unit() * W), y0 = static_cast<int>(rng.unit() * H), w = 10 + static_cast<int>(rng.unit() * W / 5), h = 10 + static_cast<int>(rng.unit() * H / 5);
        const double v = rng.unit();
        for (int y = y0; y < std::min(H, y0 + h); ++y) for (int x = x0; x < std::min(W, x0 + w); ++x) f[static_cast<size_t>(y) * W + x] = v;
    }
    if (sym) {
        const int S = sym->side;
        const double cx = W / 2.0 + (rng.unit() - .5) * W * 0.2, cy = H / 2.0 + (rng.unit() - .5) * H * 0.2, half = S / 2.0, a = c.rotation_deg * 3.14159265358979 / 180;
        double dst[8]; const double corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int i = 0; i < 4; ++i) {
            double x = corner[i][0] * half, y = corner[i][1] * half;
            x *= 1.0 - c.keystone * (corner[i][1] < 0 ? 1 : 0);
            dst[2 * i] = cx + x * std::cos(a) - y * std::sin(a); dst[2 * i + 1] = cy + x * std::sin(a) + y * std::cos(a);
        }
        const double src[8] = {0, 0, static_cast<double>(S), 0, static_cast<double>(S), static_cast<double>(S), 0, static_cast<double>(S)};
        double inv[8]; solve(dst, src, inv);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const double d = inv[6] * x + inv[7] * y + 1, u = (inv[0] * x + inv[1] * y + inv[2]) / d - .5, v = (inv[3] * x + inv[4] * y + inv[5]) / d - .5;
            if (u < 0 || v < 0 || u >= S - 1 || v >= S - 1) continue;
            const int iu = static_cast<int>(u), iv = static_cast<int>(v); const double fu = u - iu, fv = v - iv;
            const auto at = [&](int xx, int yy) { return sym->px[static_cast<size_t>(yy) * S + xx] / 255.0; };
            f[static_cast<size_t>(y) * W + x] = (at(iu, iv) * (1 - fu) + at(iu + 1, iv) * fu) * (1 - fv) + (at(iu, iv + 1) * (1 - fu) + at(iu + 1, iv + 1) * fu) * fv;
        }
    }
    for (auto &v : f) v = v * (1 - c.glare) + c.glare;
    blur(f, W, H, c.blur);
    std::vector<uint8_t> out(f.size());
    for (size_t i = 0; i < f.size(); ++i) out[i] = static_cast<uint8_t>(std::lround(std::clamp(f[i] + rng.gauss() * c.noise, 0.0, 1.0) * 255));
    return out;
}

// ---- CKey (Profile 0, level M, default style) ----

bool render_ckey(const std::string &text, int side, Image &img) {
    auto s = std::make_unique<ckey_scalable_symbol>(); ckey_diagnostics d{};
    if (ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text.data()), text.size(), 0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, s.get(), &d) != CKEY_OK) return false;
    ckey_raster_options o; ckey_raster_options_default(&o);
    o.size_px = static_cast<uint32_t>(std::max(16, side)); o.format = CKEY_PIXELS_GRAY8;
    img.side = static_cast<int>(o.size_px); img.px.resize(static_cast<size_t>(img.side) * img.side); size_t n = 0;
    return ckey_render_scalable_raster(s.get(), nullptr, &o, img.px.data(), img.px.size(), &n) == CKEY_OK;
}

Outcome scan_ckey(const std::vector<uint8_t> &frame, int w, int h, const std::string &expected) {
    Outcome r; const double t0 = now_ms();
    double borders[6 * 8];
    const size_t n = ckey_find_borders_gray8(frame.data(), w, h, w, borders, 6);
    static std::vector<uint8_t> out(65535);
    for (size_t i = 0; i < n && !r.ok; ++i) {
        double q[8]; size_t len = 0; uint32_t version = 0; ckey_diagnostics d{};
        if (ckey_refine_quad_gray8(frame.data(), w, h, w, borders + 8 * i, CKEY_FIELD_SCALABLE, q) > 0 &&
            ckey_decode_scalable_gray8(frame.data(), w, h, w, q, out.data(), out.size(), &len, &version, nullptr, &d) == CKEY_OK) {
            r.ok = len == expected.size() && std::memcmp(out.data(), expected.data(), len) == 0;
            r.corrected = static_cast<int>(d.corrected_symbols);
            if (!r.ok) r.corrected = -1;  // decoded something else: a false positive
        }
    }
    r.ms = now_ms() - t0;
    return r;
}

#ifdef CKEY_BENCH_ZXING
// zxing-cpp 2.x. Quiet zones: 4 modules (QR), 2 modules (Data Matrix).
bool render_zxing(ZXing::BarcodeFormat format, int quiet, const std::string &text, int side, Image &img) {
    try {
        ZXing::MultiFormatWriter writer(format);
        writer.setMargin(0);
        if (format == ZXing::BarcodeFormat::QRCode) writer.setEccLevel(4);  // zxing-cpp maps 0..8 onto L/M/Q/H; 4 = M
        const ZXing::BitMatrix m = writer.encode(text, 0, 0);
        const int modules = m.width() + 2 * quiet;
        img.side = side; img.px.assign(static_cast<size_t>(side) * side, 255);
        const int ss = 3;
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            int ink = 0;
            for (int sy = 0; sy < ss; ++sy) for (int sx = 0; sx < ss; ++sx) {
                const int mx = static_cast<int>(std::floor((x + (sx + .5) / ss) * modules / side)) - quiet, my = static_cast<int>(std::floor((y + (sy + .5) / ss) * modules / side)) - quiet;
                ink += mx >= 0 && my >= 0 && mx < m.width() && my < m.height() && m.get(mx, my);
            }
            img.px[static_cast<size_t>(y) * side + x] = static_cast<uint8_t>(255 - 255 * ink / (ss * ss));
        }
        return true;
    } catch (...) { return false; }
}

Outcome scan_zxing(ZXing::BarcodeFormat format, const std::vector<uint8_t> &frame, int w, int h, const std::string &expected) {
    Outcome r; const double t0 = now_ms();
    ZXing::DecodeHints hints; hints.setFormats(format); hints.setTryHarder(true);
    const auto result = ZXing::ReadBarcode(ZXing::ImageView(frame.data(), w, h, ZXing::ImageFormat::Lum), hints);
    r.ms = now_ms() - t0;
    if (result.isValid()) { r.ok = result.text() == expected; if (!r.ok) r.corrected = -1; }
    return r;
}
#endif

std::string payload_text(size_t n, uint32_t seed) {
    static const char *alphabet = "abcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::string s = "https://openckey.org/";
    Rng r{seed}; while (s.size() < n) s += alphabet[r.next() % 40];
    s.resize(n); return s;
}

double pct(std::vector<double> v, double q) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, static_cast<size_t>(q * (v.size() - 1) + 0.5))]; }

}  // namespace

int main(int argc, char **argv) {
    bool quick = false; std::string json_path, md_path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--quick") quick = true;
        else if (a == "--json" && i + 1 < argc) json_path = argv[++i];
        else if (a == "--md" && i + 1 < argc) md_path = argv[++i];
        else { std::cerr << "usage: ckey-bench [--quick] [--json out.json] [--md out.md]\n"; return 2; }
    }
    const int W = 960, H = 720, trials = quick ? 4 : 10, fp_frames = quick ? 60 : 300;
    const std::vector<size_t> payloads = quick ? std::vector<size_t>{33, 120} : std::vector<size_t>{24, 33, 80, 120, 300};
    const std::vector<int> sides = quick ? std::vector<int>{70, 100, 140, 200, 280} : std::vector<int>{60, 70, 85, 100, 120, 140, 170, 200, 240, 280, 340};
    const std::vector<Condition> conditions = {
        {"clean", 0.0, 0.01, 0, 0, 0.0},
        {"camera", 1.0, 0.03, 15, 0.06, 0.15},
        {"hard", 1.8, 0.06, 30, 0.10, 0.30},
    };
    std::vector<Symbology> syms = {{"CKey P0 (M)", render_ckey, scan_ckey}};
#ifdef CKEY_BENCH_ZXING
    syms.push_back({"QR Code (M)", [](const std::string &t, int s, Image &i) { return render_zxing(ZXing::BarcodeFormat::QRCode, 4, t, s, i); },
                    [](const std::vector<uint8_t> &f, int w, int h, const std::string &e) { return scan_zxing(ZXing::BarcodeFormat::QRCode, f, w, h, e); }});
    syms.push_back({"Data Matrix", [](const std::string &t, int s, Image &i) { return render_zxing(ZXing::BarcodeFormat::DataMatrix, 2, t, s, i); },
                    [](const std::vector<uint8_t> &f, int w, int h, const std::string &e) { return scan_zxing(ZXing::BarcodeFormat::DataMatrix, f, w, h, e); }});
#endif
    std::ostringstream md, js;
    md << "# CKey optical benchmark (synthetic)\n\nGenerated by `ckey-bench" << (quick ? " --quick" : "") << "` with core " << ckey_get_software_version()
       << ". **Synthetic frames, not real-camera measurements.** Frames are " << W << "×" << H << " px with clutter; sides include each symbology's quiet zone, "
       << "so symbols are compared at equal area. " << trials << " trials per cell.\n\n";
    md << "| Condition | blur σ px | noise σ | rotation | keystone | glare |\n|---|---|---|---|---|---|\n";
    for (const auto &c : conditions) md << "| " << c.name << " | " << c.blur << " | " << c.noise << " | " << c.rotation_deg << "° | " << c.keystone * 100 << "% | " << c.glare << " |\n";
    js << "{\n  \"core\": \"" << ckey_get_software_version() << "\", \"synthetic\": true, \"frame\": [" << W << ", " << H << "], \"trials\": " << trials << ",\n  \"results\": [\n";
    bool first = true;
    for (const auto &sym : syms) {
        md << "\n## " << sym.name << "\n\n| Payload | Condition | Smallest side ≥ 90% | Bytes per 1,000 px² | Success by side (px) | Mean RS bytes corrected | Latency p50 / p90 / p99 ms |\n|---|---|---|---|---|---|---|\n";
        for (size_t bytes : payloads) {
            const std::string text = payload_text(bytes, static_cast<uint32_t>(bytes));
            for (const auto &c : conditions) {
                std::vector<double> latency; std::ostringstream by_side; int best = -1; double corrected = 0; int corrected_n = 0;
                for (int side : sides) {
                    Image img;
                    if (!sym.render(text, side, img)) { by_side << side << ":n/a "; continue; }
                    int ok = 0;
                    for (int t = 0; t < trials; ++t) {
                        const auto frame = make_frame(&img, W, H, c, static_cast<uint32_t>(side * 1000 + t * 7 + bytes));
                        const Outcome o = sym.scan(frame, W, H, text);
                        latency.push_back(o.ms);
                        if (o.ok) { ++ok; corrected += o.corrected; ++corrected_n; }
                    }
                    by_side << side << ":" << 100 * ok / trials << "% ";
                    if (best < 0 && ok * 10 >= trials * 9) best = side;
                }
                const double density = best > 0 ? 1000.0 * bytes / (static_cast<double>(best) * best) : 0;
                md << "| " << bytes << " B | " << c.name << " | " << (best > 0 ? std::to_string(best) + " px" : "none") << " | ";
                if (best > 0) { char b[16]; std::snprintf(b, sizeof(b), "%.2f", density); md << b; } else md << "–";
                char lat[64]; std::snprintf(lat, sizeof(lat), "%.1f / %.1f / %.1f", pct(latency, .5), pct(latency, .9), pct(latency, .99));
                char cor[16]; std::snprintf(cor, sizeof(cor), "%.1f", corrected_n ? corrected / corrected_n : 0.0);
                md << " | " << by_side.str() << "| " << cor << " | " << lat << " |\n";
                js << (first ? "" : ",\n") << "    {\"symbology\": \"" << sym.name << "\", \"payload_bytes\": " << bytes << ", \"condition\": \"" << c.name
                   << "\", \"min_side_px_90\": " << best << ", \"bytes_per_kpx2\": " << density << ", \"latency_ms_p50\": " << pct(latency, .5)
                   << ", \"latency_ms_p90\": " << pct(latency, .9) << ", \"latency_ms_p99\": " << pct(latency, .99) << "}";
                first = false;
                std::cerr << sym.name << " " << bytes << "B " << c.name << ": min side " << best << '\n';
            }
        }
        // False positives: symbol-free clutter frames.
        int fp = 0; std::vector<double> fp_latency;
        for (int i = 0; i < fp_frames; ++i) {
            const auto frame = make_frame(nullptr, W, H, conditions[static_cast<size_t>(i % 3)], 0xF00D0000u + static_cast<uint32_t>(i));
            const Outcome o = sym.scan(frame, W, H, "\x01never");
            fp_latency.push_back(o.ms);
            if (o.ok || o.corrected < 0) ++fp;
        }
        char lat[64]; std::snprintf(lat, sizeof(lat), "%.1f / %.1f ms", pct(fp_latency, .5), pct(fp_latency, .99));
        md << "\nFalse positives on " << fp_frames << " symbol-free clutter frames: **" << fp << "** (scan latency p50 / p99 " << lat << ").\n";
        js << ",\n    {\"symbology\": \"" << sym.name << "\", \"false_positive_frames\": " << fp_frames << ", \"false_positives\": " << fp << "}";
    }
#ifndef CKEY_BENCH_ZXING
    md << "\nQR Code and Data Matrix columns need zxing-cpp: configure with `-DCKEY_BENCH_ZXING=ON` (CI's `benchmarks` job does).\n";
#endif
    js << "\n  ]\n}\n";
    std::cout << md.str();
    if (!md_path.empty()) std::ofstream(md_path) << md.str();
    if (!json_path.empty()) std::ofstream(json_path) << js.str();
    return 0;
}
