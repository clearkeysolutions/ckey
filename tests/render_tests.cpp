// Native backends (specification §2, acceptance criteria 2, 4 and 5): the
// direct raster agrees with the rasterized SVG export, decodes, is
// deterministic, composites transparently; PNG and PDF are well formed.
#include "ckey/ckey.h"
#include "check.h"
#include "svg_raster.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

std::vector<uint8_t> pseudo(size_t n, uint32_t seed) {
    std::vector<uint8_t> out(n);
    for (auto &b : out) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(seed >> 24); }
    return out;
}

ckey_scalable_symbol encode(uint32_t version, ckey_scalable_layout layout, size_t bytes, uint32_t seed) {
    ckey_scalable_symbol s{}; ckey_diagnostics d{};
    const auto payload = pseudo(bytes, seed);
    CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), version, CKEY_ECC_M, layout, &s, &d) == CKEY_OK);
    return s;
}

std::vector<uint8_t> raster(const ckey_scalable_symbol &s, const ckey_style *st, ckey_raster_options o) {
    size_t need = 0;
    CKEY_CHECK(ckey_render_scalable_raster(&s, st, &o, nullptr, 0, &need) == CKEY_INSUFFICIENT_BUFFER);
    std::vector<uint8_t> px(need);
    CKEY_CHECK(ckey_render_scalable_raster(&s, st, &o, px.data(), px.size(), &need) == CKEY_OK);
    return px;
}

std::string svg_of(const ckey_scalable_symbol &s, const ckey_style &st) {
    size_t n = 0; CKEY_CHECK(ckey_render_scalable_styled_svg(&s, &st, 8, nullptr, 0, &n) == CKEY_INSUFFICIENT_BUFFER);
    std::string svg(n, '\0'); CKEY_CHECK(ckey_render_scalable_styled_svg(&s, &st, 8, svg.data(), n, &n) == CKEY_OK);
    svg.resize(n - 1); return svg;
}

bool decodes(const std::vector<uint8_t> &gray, int side, const std::vector<uint8_t> &payload) {
    const double k = side / 31.0, lo = k, hi = 30 * k;
    const double q[8] = {lo, lo, hi, lo, hi, hi, lo, hi};
    std::vector<uint8_t> out(65535); size_t len = 0; uint32_t version = 0; ckey_diagnostics d{};
    if (ckey_decode_scalable_gray8(gray.data(), side, side, side, q, out.data(), out.size(), &len, &version, nullptr, &d) != CKEY_OK) return false;
    return len == payload.size() && std::equal(payload.begin(), payload.end(), out.begin());
}

uint64_t fnv1a(const std::vector<uint8_t> &v) { uint64_t h = 0xcbf29ce484222325ull; for (uint8_t b : v) { h ^= b; h *= 0x100000001b3ull; } return h; }

// Minimal inflate for fixed-Huffman DEFLATE streams (what the PNG writer emits).
struct BitReader {
    const uint8_t *p; size_t n, pos = 0; int bit = 0;
    int get() { if (pos >= n) throw 1; const int v = (p[pos] >> bit) & 1; if (++bit == 8) { bit = 0; ++pos; } return v; }
    int bits(int k) { int v = 0; for (int i = 0; i < k; ++i) v |= get() << i; return v; }
    int huff(int k) { int v = 0; for (int i = 0; i < k; ++i) v = (v << 1) | get(); return v; }
};
std::vector<uint8_t> inflate_fixed(const uint8_t *data, size_t n) {
    static const int lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int lextra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    BitReader r{data + 2, n - 6};
    std::vector<uint8_t> out;
    CKEY_CHECK(r.bits(1) == 1 && r.bits(2) == 1);
    for (;;) {
        int code = r.huff(7), sym;
        if (code <= 0x17) sym = 256 + code;
        else { code = (code << 1) | r.get();
            if (code >= 0x30 && code <= 0xbf) sym = code - 0x30;
            else if (code >= 0xc0 && code <= 0xc7) sym = 280 + code - 0xc0;
            else { code = (code << 1) | r.get(); sym = 144 + code - 0x190; } }
        if (sym < 256) { out.push_back(static_cast<uint8_t>(sym)); continue; }
        if (sym == 256) break;
        const int li = sym - 257, len = lbase[li] + r.bits(lextra[li]);
        const int di = r.huff(5), dist = dbase[di] + r.bits(dextra[di]);
        for (int i = 0; i < len; ++i) out.push_back(out[out.size() - dist]);
    }
    return out;
}

uint32_t be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

// Decodes the writer's own PNG back to pixels (8-bit gray/RGBA, filters 0/2).
std::vector<uint8_t> read_png(const std::vector<uint8_t> &png, uint32_t &w, uint32_t &h, int &channels) {
    CKEY_CHECK(png.size() > 33 && std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
    size_t at = 8; std::vector<uint8_t> idat;
    while (at + 12 <= png.size()) {
        const uint32_t len = be32(&png[at]); const std::string type(reinterpret_cast<const char *>(&png[at + 4]), 4);
        if (type == "IHDR") { w = be32(&png[at + 8]); h = be32(&png[at + 12]); channels = png[at + 17] == 6 ? 4 : 1; }
        if (type == "IDAT") idat.insert(idat.end(), png.begin() + static_cast<long>(at + 8), png.begin() + static_cast<long>(at + 8 + len));
        at += 12 + len;
        if (type == "IEND") break;
    }
    const auto raw = inflate_fixed(idat.data(), idat.size());
    const size_t row = static_cast<size_t>(w) * channels;
    CKEY_CHECK(raw.size() == (row + 1) * h);
    std::vector<uint8_t> px(row * h), above(row, 0);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t f = raw[y * (row + 1)];
        for (size_t i = 0; i < row; ++i) { const uint8_t v = raw[y * (row + 1) + 1 + i]; px[y * row + i] = f == 2 ? static_cast<uint8_t>(v + above[i]) : v; }
        std::copy(px.begin() + static_cast<long>(y * row), px.begin() + static_cast<long>((y + 1) * row), above.begin());
    }
    return px;
}

}  // namespace

int main() {
    ckey_style plain; ckey_style_default(&plain);

    // 1. The direct raster draws exactly what the SVG export describes:
    // binary 3x3-sampled pixels agree with the reference SVG rasterizer.
    for (const auto &[version, layout, shape] : {std::tuple{2u, CKEY_LAYOUT_STANDARD, CKEY_MODULE_ROUNDED}, std::tuple{11u, CKEY_LAYOUT_STANDARD, CKEY_MODULE_DIAMOND},
                                                  std::tuple{20u, CKEY_LAYOUT_PRINT, CKEY_MODULE_CIRCLE}, std::tuple{31u, CKEY_LAYOUT_STANDARD, CKEY_MODULE_SQUARE}}) {
        const auto s = encode(version, layout, 20, version);
        ckey_style st = plain; st.modules = shape; st.rings = version == 11 ? CKEY_RING_DOTS : CKEY_RING_JOINED; st.rays = version == 20 ? CKEY_RAY_FLAT : CKEY_RAY_ROUND;
        const int side = 31 * 10 * static_cast<int>((version + 9) / 10);
        ckey_raster_options o; ckey_raster_options_default(&o);
        o.size_px = static_cast<uint32_t>(side); o.policy = CKEY_RASTER_BINARY; o.format = CKEY_PIXELS_GRAY8; o.supersample = 3;
        const auto direct = raster(s, &st, o);
        const auto ref = svgtest::rasterize(svgtest::parse(svg_of(s, st)), side, side, 3, 0, 255);
        size_t differ = 0;
        for (size_t i = 0; i < direct.size(); ++i) differ += (direct[i] < 128) != (ref[i] >= 128);
        std::printf("v%u layout %d: direct raster vs SVG raster, %zu of %zu pixels differ\n", version, layout, differ, direct.size());
        CKEY_CHECK(differ * 2000 <= direct.size());  // <= 0.05%: only SVG's 4-decimal rounding
    }

    // 2. Every policy decodes; determinism; GRAY8 vs RGBA8 agree.
    for (uint32_t version : {1u, 9u, 31u}) {
        for (auto layout : {CKEY_LAYOUT_STANDARD, CKEY_LAYOUT_PRINT}) {
            const auto payload = pseudo(std::min<size_t>(20, ckey_scalable_capacity(version, CKEY_ECC_M, layout)), 77 + version);
            ckey_scalable_symbol s{}; ckey_diagnostics d{};
            CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), version, CKEY_ECC_M, layout, &s, &d) == CKEY_OK);
            const uint32_t modules = ckey_scalable_modules_across(version);
            for (auto policy : {CKEY_RASTER_COVERAGE, CKEY_RASTER_BINARY, CKEY_RASTER_HYBRID}) {
                ckey_raster_options o; ckey_raster_options_default(&o);
                o.size_px = static_cast<uint32_t>(std::ceil(31.0 * 7 * modules / 29.0));  // 7 px per module
                o.policy = policy; o.format = CKEY_PIXELS_GRAY8;
                const auto a = raster(s, nullptr, o), b = raster(s, nullptr, o);
                CKEY_CHECK(a == b);
                CKEY_CHECK(decodes(a, static_cast<int>(o.size_px), payload));
                o.format = CKEY_PIXELS_RGBA8;
                const auto rgba = raster(s, nullptr, o);
                for (size_t i = 0; i < a.size(); i += 97) CKEY_CHECK(rgba[4 * i] == a[i] && rgba[4 * i + 3] == 255);
            }
        }
    }
    std::printf("coverage, binary and hybrid rasters decode at 7 px/module for v1, v9, v31 (both layouts); output is deterministic\n");
    {
        const auto s = encode(2, CKEY_LAYOUT_STANDARD, 20, 5);
        ckey_raster_options o; ckey_raster_options_default(&o); o.size_px = 600;
        std::printf("v2 600 px coverage raster hash %016llx\n", static_cast<unsigned long long>(fnv1a(raster(s, nullptr, o))));
    }

    // 3. HYBRID keeps thin features: at a small size it paints at least as
    // much ink as COVERAGE, and every v31 ray stays visible.
    {
        const auto s = encode(31, CKEY_LAYOUT_STANDARD, 100, 9);
        ckey_raster_options o; ckey_raster_options_default(&o); o.size_px = 400; o.format = CKEY_PIXELS_GRAY8;
        const auto cov = raster(s, nullptr, o); o.policy = CKEY_RASTER_HYBRID; const auto hyb = raster(s, nullptr, o);
        size_t ink_cov = 0, ink_hyb = 0; for (size_t i = 0; i < cov.size(); ++i) { ink_cov += 255 - cov[i]; ink_hyb += 255 - hyb[i]; }
        CKEY_CHECK(ink_hyb > ink_cov);
        o.min_feature_px = 0; CKEY_CHECK(ckey_render_scalable_raster(&s, nullptr, &o, nullptr, 0, nullptr) == CKEY_INVALID_ARGUMENT);
    }

    // 4. Transparency (spec §2, §11.5): no background, unpainted alpha 0.
    {
        const auto s = encode(5, CKEY_LAYOUT_STANDARD, 20, 3);
        ckey_raster_options o; ckey_raster_options_default(&o); o.size_px = 310; o.transparent = 1;
        const auto px = raster(s, nullptr, o);
        CKEY_CHECK(px[3] == 0 && px[4 * (310 * 155 + 2) + 3] == 0);       // quiet margin
        const size_t frame = 4 * (static_cast<size_t>(310) * 155 + 15);   // on the frame stroke (x = 0.5 units)
        CKEY_CHECK(px[frame + 3] == 255 && px[frame] == 0);
        size_t zero = 0; for (size_t i = 3; i < px.size(); i += 4) zero += px[i] == 0;
        CKEY_CHECK(zero > px.size() / 8);
        o.format = CKEY_PIXELS_GRAY8; size_t need = 0;
        CKEY_CHECK(ckey_render_scalable_raster(&s, nullptr, &o, nullptr, 0, &need) == CKEY_INVALID_ARGUMENT);  // gray has no alpha
    }

    // 5. Materials and a logo image render natively and still decode.
    {
        const auto payload = pseudo(40, 11);
        ckey_scalable_symbol s{}; ckey_diagnostics d{};
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 11, CKEY_ECC_Q, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
        ckey_style art; CKEY_CHECK(ckey_style_preset_fill(&art, CKEY_PRESET_CONCEPT) == CKEY_OK);
        std::vector<uint8_t> logo(64 * 64 * 4);
        for (size_t i = 0; i < 64 * 64; ++i) { logo[4 * i] = 200; logo[4 * i + 1] = 40; logo[4 * i + 2] = 40; logo[4 * i + 3] = 255; }
        art.logo_href = "logo.png"; art.logo_plate = 1; art.logo_radius = 3.0;
        CKEY_CHECK(ckey_check_scalable_style(&s, &art, &d) == CKEY_OK);
        ckey_logo_image img{logo.data(), 64, 64, 0};
        ckey_raster_options o; ckey_raster_options_default(&o); o.size_px = 31 * 14 * 67 / 29; o.logo = &img;
        const auto rgba = raster(s, &art, o);
        const size_t c = 4 * (static_cast<size_t>(o.size_px / 2) * o.size_px + o.size_px / 2);
        CKEY_CHECK(rgba[c] == 200 && rgba[c + 1] == 40);  // the logo is drawn at the centre
        std::vector<uint8_t> gray(rgba.size() / 4);
        for (size_t i = 0; i < gray.size(); ++i) gray[i] = static_cast<uint8_t>((54 * rgba[4 * i] + 183 * rgba[4 * i + 1] + 19 * rgba[4 * i + 2]) >> 8);
        CKEY_CHECK(decodes(gray, static_cast<int>(o.size_px), payload));
        std::printf("concept preset with depth, grain and a logo: native raster decodes\n");
    }

    // 6. Geometry API.
    {
        const auto s = encode(3, CKEY_LAYOUT_STANDARD, 10, 1);
        size_t count = 0;
        CKEY_CHECK(ckey_scalable_geometry(&s, nullptr, nullptr, 0, &count) == CKEY_INSUFFICIENT_BUFFER && count > 100);
        std::vector<ckey_shape> shapes(count);
        CKEY_CHECK(ckey_scalable_geometry(&s, nullptr, shapes.data(), shapes.size(), &count) == CKEY_OK);
        CKEY_CHECK(shapes[0].kind == CKEY_SHAPE_RECT_OUTLINE && shapes[0].layer == CKEY_LAYER_FRAME);
        size_t finders = 0; for (const auto &sh : shapes) { finders += sh.layer == CKEY_LAYER_FINDERS; CKEY_CHECK(sh.kind != CKEY_SHAPE_LOGO); }
        CKEY_CHECK(finders == 5);  // three corner arcs plus the split top-left pair
        ckey_style bad = plain; bad.ink = {0xee, 0xee, 0xee};
        CKEY_CHECK(ckey_scalable_geometry(&s, &bad, nullptr, 0, &count) == CKEY_INVALID_ARGUMENT);
    }

    // 7. PNG round trip through an independent inflate; 1-bit and pHYs.
    {
        const auto s = encode(7, CKEY_LAYOUT_STANDARD, 30, 2);
        ckey_raster_options o; ckey_raster_options_default(&o); o.size_px = 257;
        const auto rgba = raster(s, nullptr, o);
        size_t need = 0;
        CKEY_CHECK(ckey_encode_png(rgba.data(), 257, 257, 0, CKEY_PNG_RGBA8, 300, nullptr, 0, &need) == CKEY_INSUFFICIENT_BUFFER);
        std::vector<uint8_t> png(need);
        CKEY_CHECK(ckey_encode_png(rgba.data(), 257, 257, 0, CKEY_PNG_RGBA8, 300, png.data(), png.size(), &need) == CKEY_OK);
        uint32_t w = 0, h = 0; int ch = 0;
        CKEY_CHECK(read_png(png, w, h, ch) == rgba && w == 257 && h == 257 && ch == 4);
        CKEY_CHECK(png.size() < rgba.size() / 4);  // compressed
        std::string bytes(png.begin(), png.end());
        CKEY_CHECK(bytes.find("pHYs") != std::string::npos);
        std::vector<uint8_t> gray(257 * 257); for (size_t i = 0; i < gray.size(); ++i) gray[i] = rgba[4 * i];
        CKEY_CHECK(ckey_encode_png(gray.data(), 257, 257, 0, CKEY_PNG_GRAY8, 0, png.data(), png.size(), &need) == CKEY_OK);
        png.resize(need); CKEY_CHECK(read_png(png, w, h, ch) == gray && ch == 1);
        std::vector<uint8_t> one(64 * 1024);
        CKEY_CHECK(ckey_encode_png(gray.data(), 257, 257, 0, CKEY_PNG_GRAY1, 600, one.data(), one.size(), &need) == CKEY_OK && one[24] == 1);
        std::printf("PNG: RGBA8 %zu bytes for %zu pixel bytes; decodes back exactly\n", png.size(), rgba.size());
    }

    // 8. PDF: valid structure and cross-reference offsets.
    {
        const auto s = encode(4, CKEY_LAYOUT_STANDARD, 20, 4);
        size_t need = 0;
        CKEY_CHECK(ckey_render_scalable_pdf(&s, nullptr, 144, 0, nullptr, 0, &need) == CKEY_INSUFFICIENT_BUFFER);
        std::vector<uint8_t> pdf(need);
        CKEY_CHECK(ckey_render_scalable_pdf(&s, nullptr, 144, 0, pdf.data(), pdf.size(), &need) == CKEY_OK);
        const std::string text(pdf.begin(), pdf.end());
        CKEY_CHECK(text.rfind("%PDF-1.4", 0) == 0 && text.find("%%EOF") != std::string::npos && text.find("/MediaBox [0 0 144 144]") != std::string::npos);
        const size_t sx = text.rfind("startxref\n");
        const size_t xref = std::stoul(text.substr(sx + 10));
        CKEY_CHECK(text.compare(xref, 4, "xref") == 0);
        for (int obj = 1; obj <= 5; ++obj) {
            const size_t line = xref + 5 + std::to_string(6).size() + 3 + 20 * obj;  // "xref\n0 6\n" then 20-byte entries
            const size_t off = std::stoul(text.substr(line, 10));
            CKEY_CHECK(text.compare(off, std::to_string(obj).size() + 6, std::to_string(obj) + " 0 obj") == 0);
        }
        CKEY_CHECK(ckey_render_scalable_pdf(&s, nullptr, 144, 1, pdf.data(), pdf.size(), &need) == CKEY_OK);
        CKEY_CHECK(std::string(pdf.begin(), pdf.begin() + static_cast<long>(need)).find("1 1 1 rg\n0 0 144 144 re f") == std::string::npos);
        std::printf("PDF: %zu bytes, valid xref; transparent mode omits the background\n", need);
    }

    // 9. The print path no longer needs SVG and still emits pure ink/paper.
    {
        const auto s = encode(3, CKEY_LAYOUT_PRINT, 20, 6);
        size_t side = 0;
        CKEY_CHECK(ckey_render_scalable_print(&s, nullptr, 0.5, 600, nullptr, 0, &side) == CKEY_INSUFFICIENT_BUFFER && side == 300);
        std::vector<uint8_t> px(side * side);
        CKEY_CHECK(ckey_render_scalable_print(&s, nullptr, 0.5, 600, px.data(), px.size(), &side) == CKEY_OK);
        for (uint8_t v : px) CKEY_CHECK(v == 0 || v == 255);
    }
    // Pixel-perfect lattice: whole cells, grid modules exactly c x c cells,
    // decodes at the layout's default size (0) across versions, deterministic,
    // and refuses invalid sizes.
    {
        for (const auto &[version, layout] : {std::pair{1u, CKEY_LAYOUT_STANDARD}, std::pair{9u, CKEY_LAYOUT_PRINT}, std::pair{18u, CKEY_LAYOUT_STANDARD},
                                              std::pair{26u, CKEY_LAYOUT_PRINT}, std::pair{31u, CKEY_LAYOUT_STANDARD}}) {
            const size_t bytes = std::min<size_t>(40, ckey_scalable_capacity(version, CKEY_ECC_M, layout));
            const auto s = encode(version, layout, bytes, version * 3);
            uint32_t side = 0; size_t need = 0;
            CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 0, nullptr, 0, &side, &need) == CKEY_INSUFFICIENT_BUFFER);
            const uint32_t c = layout == CKEY_LAYOUT_PRINT ? 3 : 6, modules = ckey_scalable_modules_across(version);
            CKEY_CHECK(side == modules * c + 2 * ((modules * c + 28) / 29) && need == static_cast<size_t>(side) * side);
            std::vector<uint8_t> cells(need), again(need);
            CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 0, cells.data(), cells.size(), &side, &need) == CKEY_OK);
            CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 0, again.data(), again.size(), &side, &need) == CKEY_OK && cells == again);
            for (uint8_t v : cells) CKEY_CHECK(v <= 1);  // the plain style has no accents
            for (const uint32_t px : {1u, 2u}) {
                const uint32_t w = side * px + 80;
                std::vector<uint8_t> frame(static_cast<size_t>(w) * w, 255);
                for (uint32_t y = 0; y < side * px; ++y) for (uint32_t x = 0; x < side * px; ++x)
                    frame[static_cast<size_t>(y + 40) * w + x + 40] = cells[(y / px) * side + x / px] ? 0 : 255;
                ckey_scan_options so{}; so.abi_version = CKEY_ABI_VERSION;
                ckey_scan_result r{}; r.abi_version = CKEY_ABI_VERSION;
                std::vector<uint8_t> out(4096);
                CKEY_CHECK(ckey_scan_frame_gray8(frame.data(), w, w, w, &so, out.data(), out.size(), &r) == CKEY_OK);
                const auto payload = pseudo(bytes, version * 3);
                CKEY_CHECK(r.payload_length == payload.size() && std::equal(payload.begin(), payload.end(), out.begin()));
            }
        }
        // Pixel layout: decodes from its lattice at 2 cells per module (one and
        // two pixels per cell) and from the antialiased raster; the default is
        // 2 cells per module.
        for (const uint32_t version : {3u, 12u, 26u}) {
            const size_t bytes = std::min<size_t>(60, ckey_scalable_capacity(version, CKEY_ECC_M, CKEY_LAYOUT_PIXEL));
            const auto s = encode(version, CKEY_LAYOUT_PIXEL, bytes, version * 5);
            const auto payload = pseudo(bytes, version * 5);
            uint32_t side = 0; size_t need = 0;
            CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 0, nullptr, 0, &side, &need) == CKEY_INSUFFICIENT_BUFFER);
            const uint32_t modules = ckey_scalable_modules_across(version);
            CKEY_CHECK(side == 2 * modules + 2 * ((2 * modules + 28) / 29));
            CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 2, nullptr, 0, &side, &need) == CKEY_INSUFFICIENT_BUFFER);
            std::vector<uint8_t> cells(need);
            CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 2, cells.data(), cells.size(), &side, &need) == CKEY_OK);
            for (const uint32_t px : {1u, 2u}) {
                const uint32_t w = side * px + 80;
                std::vector<uint8_t> frame(static_cast<size_t>(w) * w, 255);
                for (uint32_t y = 0; y < side * px; ++y) for (uint32_t x = 0; x < side * px; ++x)
                    frame[static_cast<size_t>(y + 40) * w + x + 40] = cells[(y / px) * side + x / px] ? 0 : 255;
                ckey_scan_options so{}; so.abi_version = CKEY_ABI_VERSION;
                ckey_scan_result r{}; r.abi_version = CKEY_ABI_VERSION;
                std::vector<uint8_t> out(4096);
                CKEY_CHECK(ckey_scan_frame_gray8(frame.data(), w, w, w, &so, out.data(), out.size(), &r) == CKEY_OK);
                CKEY_CHECK(r.layout == CKEY_LAYOUT_PIXEL && r.payload_length == payload.size() && std::equal(payload.begin(), payload.end(), out.begin()));
            }
        }
        const auto s = encode(5, CKEY_LAYOUT_STANDARD, 10, 7);
        uint32_t side = 0; size_t need = 0;
        CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 33, nullptr, 0, &side, &need) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 0, nullptr, 0, nullptr, &need) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_render_scalable_lattice(nullptr, nullptr, 0, nullptr, 0, &side, &need) == CKEY_INVALID_ARGUMENT);
        std::vector<uint8_t> small(10);
        CKEY_CHECK(ckey_render_scalable_lattice(&s, nullptr, 4, small.data(), small.size(), &side, &need) == CKEY_INSUFFICIENT_BUFFER);
    }
    std::printf("native render tests passed\n");
    return 0;
}
