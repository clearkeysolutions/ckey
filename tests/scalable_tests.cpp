// Profile 0 (scalable family) tests. White-box: includes the core
// translation unit to check layout invariants; built without ckey_core.
#include "../src/ckey.cpp"
#include "check.h"
#include "svg_raster.h"
#include "print_sim.h"

#include <cstdio>

namespace {

constexpr uint64_t kPrintKatV3 = 0x1908543413f369b8ull, kPrintKatV31 = 0x684c56624d3558a8ull;  // stated in spec/CKey-Format-v0.28.0.md
constexpr uint64_t kArcKatV3 = 0x9a0d790928793db3ull, kArcKatV31 = 0x296ac7423c01b6b4ull;  // stated in spec/CKey-Format-v0.32.0.md
constexpr uint64_t kArcGroupedKatV3 = 0xbf5acc916edc47b1ull, kArcGroupedKatV31 = 0xbd3d8d6f9c7eeadeull;  // stated in spec/CKey-Format-v0.33.0.md
constexpr uint64_t kScalableKatV2 = 0xb97ef7acabd3c8d7ull, kScalableKatV31 = 0xc7df4dfd1c36458bull;  // stated in spec/CKey-Format-v0.27.0.md

std::vector<uint8_t> pseudo(size_t n, uint32_t seed) {
    std::vector<uint8_t> out(n);
    for (auto &b : out) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(seed >> 24); }
    return out;
}

std::string render_styled(const ckey_scalable_symbol &s, const ckey_style &st) {
    size_t n = 0;
    if (ckey_render_scalable_styled_svg(&s, &st, 8, nullptr, 0, &n) != CKEY_INSUFFICIENT_BUFFER) return {};
    std::string svg(n, '\0'); CKEY_CHECK(ckey_render_scalable_styled_svg(&s, &st, 8, svg.data(), n, &n) == CKEY_OK);
    svg.resize(n - 1); return svg;
}

// Decodes a rendered SVG with the exact quad; returns the detected version or 0.
uint32_t decode_svg(const std::string &svg, double unit, const std::vector<uint8_t> &payload) {
    const int side = static_cast<int>(std::ceil(31 * unit));
    const auto px = svgtest::rasterize(svgtest::parse(svg), side, side, 3);
    const double k = static_cast<double>(side) / 31.0, lo = k, hi = 30 * k;
    const double q[8] = {lo, lo, hi, lo, hi, hi, lo, hi};
    std::vector<uint8_t> out(65535); size_t len = 0; uint32_t version = 0; ckey_diagnostics d{};
    if (ckey_decode_scalable_gray8(px.data(), side, side, side, q, out.data(), out.size(), &len, &version, nullptr, &d) != CKEY_OK) return 0;
    return len == payload.size() && std::equal(payload.begin(), payload.end(), out.begin()) ? version : 0;
}

std::string render(const ckey_scalable_symbol &s) {
    size_t n = 0; CKEY_CHECK(ckey_render_scalable_svg(&s, 8, nullptr, 0, &n) == CKEY_INSUFFICIENT_BUFFER);
    std::string svg(n, '\0'); CKEY_CHECK(ckey_render_scalable_svg(&s, 8, svg.data(), n, &n) == CKEY_OK);
    svg.resize(n - 1); return svg;
}

// Renders and decodes with the exact quad; returns the detected version or 0.
uint32_t image_round_trip(const ckey_scalable_symbol &s, const std::vector<uint8_t> &payload, double px_per_module, bool blur) {
    const ScalableLayout &L = scalable_layout(static_cast<int>(s.version));
    const double unit = px_per_module / L.pitch;  // pixels per canonical unit
    const int side = static_cast<int>(std::ceil(31 * unit));
    auto px = svgtest::rasterize(svgtest::parse(render(s)), side, side, 3);
    if (blur) {
        auto out = px;
        for (int y = 1; y < side - 1; ++y) for (int x = 1; x < side - 1; ++x) {
            int t = 0; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) t += px[static_cast<size_t>(y + dy) * side + x + dx];
            out[static_cast<size_t>(y) * side + x] = static_cast<uint8_t>(t / 9);
        }
        px.swap(out);
    }
    const double k = static_cast<double>(side) / 31.0, lo = k, hi = 30 * k;
    const double q[8] = {lo, lo, hi, lo, hi, hi, lo, hi};
    std::vector<uint8_t> out(65535); size_t len = 0; uint32_t version = 0; ckey_diagnostics d{};
    if (ckey_decode_scalable_gray8(px.data(), side, side, side, q, out.data(), out.size(), &len, &version, nullptr, &d) != CKEY_OK) return 0;
    if (len != payload.size() || !std::equal(payload.begin(), payload.end(), out.begin())) return 0;
    return version;
}

}  // namespace

int main() {
    // ---- Table invariants (both layout families) ----
    for (int f = 0; f < kScalableFamilies; ++f) for (int v = 1; v <= kScalableVersions; ++v) {
        const ScalableLayout &L = scalable_family_layout(v, f); const double p = L.pitch;
        const double ring_limit = kScalableDialInner - (scalable_fine(f) ? 2.38 : 2.4) * p;
        CKEY_CHECK(L.dial_slots % 8 == 0);
        for (int S : L.ring_slots) CKEY_CHECK(S % 8 == 0);
        if (f != 2) {
            CKEY_CHECK(L.ring_radius(L.rings - 1) <= ring_limit + 1e-9);   // ring band clears the dial
            CKEY_CHECK(L.ring_radius(L.rings) > ring_limit - 1e-9);         // and uses all the room it has
        } else {
            // Pixel family: one ring (the Ring Key, no data); rays two modules
            // apart; a short ray ends at least a module before the ray sample.
            CKEY_CHECK(L.rings == 1 && L.format == CKEY_SCALABLE_PIXEL_FORMAT_VERSION);
            for (const auto &n : L.nodes) CKEY_CHECK(n.kind != 2);
            CKEY_CHECK(kTau * kScalableDialInner / L.dial_slots >= 2 * p - 1e-9);
            CKEY_CHECK(L.dial_sample - L.short_ray() >= p - 1e-9);
            size_t annulus = 0;
            for (const auto &n : L.nodes) if (n.kind == 0 && std::hypot(n.x - kCenter, n.y - kCenter) < kScalableDialInner) ++annulus;
            CKEY_CHECK(annulus > 0);  // the ring zone holds grid modules
        }
        CKEY_CHECK(L.ring_key_slot() + L.ring_key_span <= L.ring_slots.back());
        if (f == 3) {
            // Standard arc: Standard's rings and keys, but no ring's marks are
            // closer together than the outermost ring's, which keeps the dial's
            // slots; the Ring Key is at 6:00; formats 0.32 and 0.33.
            const ScalableLayout &S0 = scalable_family_layout(v, 0);
            CKEY_CHECK(L.rings == S0.rings && L.ring_pitch == S0.ring_pitch && L.dot == S0.dot && L.dial_slots == S0.dial_slots);
            CKEY_CHECK(L.ring_slots.back() == L.dial_slots && L.ring_key_slot() == L.dial_slots / 2);
            const double outer = kTau * L.ring_radius(L.rings - 1) / L.ring_slots.back();
            for (int k = 0; k < L.rings; ++k) {
                CKEY_CHECK(kTau * L.ring_radius(k) / L.ring_slots[static_cast<size_t>(k)] >= outer - 1e-9);
                if (k + 1 < L.rings) CKEY_CHECK(L.ring_slots[static_cast<size_t>(k)] <= L.ring_slots[static_cast<size_t>(k + 1)]);
            }
            CKEY_CHECK(L.format == CKEY_SCALABLE_ARC_FORMAT_VERSION && scalable_layout(v, 6).format == CKEY_SCALABLE_ARC_GROUPED_FORMAT_VERSION);
            CKEY_CHECK(L.codeword_bytes < S0.codeword_bytes);
            // No ring site sits on a Ray Key bearing.
            for (const auto &n : L.nodes) if (n.kind == 2) {
                const int S = L.ring_slots[static_cast<size_t>(n.ring)];
                for (const int ray : {0, L.ray_key_half(), L.ray_key_three()}) {
                    double d = std::fabs(static_cast<double>(n.slot) / S - static_cast<double>(ray) / L.dial_slots);
                    d = std::min(d, 1 - d);
                    CKEY_CHECK(d * S >= 0.5 - 1e-9);
                }
            }
        }
        CKEY_CHECK(L.codeword_bytes <= CKEY_SCALABLE_MAX_CODEWORD_BYTES);
        if (f == 1) {
            // Print family: every mark and gap is at least about one module.
            for (int k = 0; k < L.rings; ++k) {
                const double spacing = kTau * L.ring_radius(k) / L.ring_slots[static_cast<size_t>(k)];
                CKEY_CHECK(spacing >= p - 1e-9 && spacing - 2 * L.dot >= 0.39 * p);
                const double exact = kTau * L.ring_radius(k) / (8.0 * p);  // slot count decided with slack
                CKEY_CHECK(std::abs(exact - std::round(exact)) > 1e-6);
            }
            CKEY_CHECK(kTau * kScalableDialInner / L.dial_slots - L.ray_width >= 0.6 * p);
            CKEY_CHECK(L.ring_pitch - 2 * L.dot >= 0.39 * p);
        }
        // Grid Key ink keeps a pitch of clearance from the frame, finders, and radial alphabet.
        const DenseGeometry &g = L.geom; double worst = 1e9;
        for (double x = g.grid_x0; x <= g.grid_x1 + 1e-12; x += (g.grid_x1 - g.grid_x0) / 8)
            for (double y = g.grid_y0; y <= g.grid_y1 + 1e-12; y += (g.grid_y1 - g.grid_y0) / 8) {
                DenseGeometry probe = g; probe.grid_x0 = probe.grid_y0 = 1e6; probe.grid_x1 = probe.grid_y1 = 1e6;  // ignore itself
                probe.module_half = 0; probe.frame_clear = probe.finder_clear = probe.polar_clear = 0;
                worst = std::min(worst, dense_site_margin(probe, x, y));
            }
        CKEY_CHECK(worst >= p - 1e-9);
        // Site set decided with slack (interoperable across libm implementations).
        double slack = 1e9;
        for (int row = 0; row < L.grid; ++row) for (int col = 0; col < L.grid; ++col)
            slack = std::min(slack, std::abs(dense_site_margin(L.geom, (col + .5) * p, (row + .5) * p)));
        CKEY_CHECK(slack > 1e-6);
    }

    // ---- P0.1 (print family): capacities, and every version decodes ----
    std::printf("P0.1 (print) payload bytes at level M:");
    size_t previous_print = 0;
    for (uint32_t v = 1; v <= kScalableVersions; ++v) {
        const size_t m = ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_PRINT);
        std::printf(" %zu", m);
        CKEY_CHECK(m > previous_print && m < ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_STANDARD));
        previous_print = m;
    }
    std::printf("\n");
    {
        ckey_scalable_symbol ps{}; ckey_diagnostics pd{};
        for (uint32_t v = 1; v <= kScalableVersions; ++v) {
            const auto payload = pseudo(ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_PRINT), 3000u + v);
            CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), v, CKEY_ECC_M, CKEY_LAYOUT_PRINT, &ps, &pd) == CKEY_OK && ps.layout == CKEY_LAYOUT_PRINT);
            const ScalableLayout &L = scalable_layout(static_cast<int>(v), 1);
            const double unit = 6.0 / L.pitch; const int side = static_cast<int>(std::ceil(31 * unit));
            const auto px = svgtest::rasterize(svgtest::parse(render(ps)), side, side, 3);
            const double k = static_cast<double>(side) / 31.0, q[8] = {k, k, 30 * k, k, 30 * k, 30 * k, k, 30 * k};
            std::vector<uint8_t> out(65535); size_t len = 0; uint32_t version = 0, layout = 9;
            const ckey_status st = ckey_decode_scalable_gray8(px.data(), side, side, side, q, out.data(), out.size(), &len, &version, &layout, &pd);
            if (st != CKEY_OK || version != v || layout != CKEY_LAYOUT_PRINT) std::printf("P0.1 v%u: status %d version %u layout %u\n", v, st, version, layout);
            CKEY_CHECK(st == CKEY_OK && version == v && layout == CKEY_LAYOUT_PRINT && len == payload.size() && std::equal(payload.begin(), payload.end(), out.begin()));
        }
        std::printf("P0.1: all 31 versions decode at 6 px/module and are told apart from P0 by their keys\n");
        // Standard arc (Formats 0.32 and 0.33): every version, both
        // placements, decodes and is told apart from Standard; capacity is
        // a little below Standard's.
        std::printf("Standard arc payload bytes at level M (Standard):");
        for (uint32_t v = 1; v <= kScalableVersions; ++v) {
            const size_t a = ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_STANDARD_ARC), b = ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_STANDARD);
            std::printf(" %zu(%zu)", a, b);
            CKEY_CHECK(a <= b && a == ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_STANDARD_ARC_GROUPED));
        }
        std::printf("\n");
        for (const ckey_scalable_layout arc : {CKEY_LAYOUT_STANDARD_ARC, CKEY_LAYOUT_STANDARD_ARC_GROUPED})
        for (uint32_t v = 1; v <= kScalableVersions; ++v) {
            const auto payload = pseudo(ckey_scalable_capacity(v, CKEY_ECC_M, arc), 5000u + v + 100u * arc);
            CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), v, CKEY_ECC_M, arc, &ps, &pd) == CKEY_OK && ps.layout == static_cast<uint32_t>(arc));
            const ScalableLayout &L = scalable_layout(static_cast<int>(v), arc);
            const double unit = 6.0 / L.pitch; const int side = static_cast<int>(std::ceil(31 * unit));
            const auto px = svgtest::rasterize(svgtest::parse(render(ps)), side, side, 3);
            const double k = static_cast<double>(side) / 31.0, q[8] = {k, k, 30 * k, k, 30 * k, 30 * k, k, 30 * k};
            std::vector<uint8_t> out(65535); size_t len = 0; uint32_t version = 0, layout = 9;
            const ckey_status st = ckey_decode_scalable_gray8(px.data(), side, side, side, q, out.data(), out.size(), &len, &version, &layout, &pd);
            if (st != CKEY_OK || version != v || layout != static_cast<uint32_t>(arc)) std::printf("arc %d v%u: status %d version %u layout %u (%s)\n", arc, v, st, version, layout, pd.message);
            CKEY_CHECK(st == CKEY_OK && version == v && layout == static_cast<uint32_t>(arc) && len == payload.size() && std::equal(payload.begin(), payload.end(), out.begin()));
        }
        std::printf("Standard arc: all 31 versions decode at 6 px/module in both placements and are told apart from Standard\n");
        // CKEY_LAYOUT_AUTO: Standard for v1-v7, Standard arc grouped from v8,
        // with version 0 the smallest version that fits under that rule.
        for (uint32_t v = 1; v <= kScalableVersions; ++v) {
            const ckey_scalable_layout expect = v < 8 ? CKEY_LAYOUT_STANDARD : CKEY_LAYOUT_STANDARD_ARC_GROUPED;
            CKEY_CHECK(ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_AUTO) == ckey_scalable_capacity(v, CKEY_ECC_M, expect));
            const auto payload = pseudo(ckey_scalable_capacity(v, CKEY_ECC_M, expect), 7000u + v);
            CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), v, CKEY_ECC_M, CKEY_LAYOUT_AUTO, &ps, &pd) == CKEY_OK && ps.version == v && ps.layout == static_cast<uint32_t>(expect));
            CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 0, CKEY_ECC_M, CKEY_LAYOUT_AUTO, &ps, &pd) == CKEY_OK && ps.version == v && ps.layout == static_cast<uint32_t>(expect));
        }
        {
            // One byte past Standard v7's capacity goes to Arc v8.
            const auto payload = pseudo(ckey_scalable_capacity(7, CKEY_ECC_M, CKEY_LAYOUT_STANDARD) + 1, 7100u);
            CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 0, CKEY_ECC_M, CKEY_LAYOUT_AUTO, &ps, &pd) == CKEY_OK && ps.version == 8 && ps.layout == CKEY_LAYOUT_STANDARD_ARC_GROUPED);
        }
        std::printf("CKEY_LAYOUT_AUTO: Standard for v1-v7, Standard arc grouped for v8-v31\n");
        // Conformance vectors (same method as Profile 0's), payload "CKey renderer test" at level M.
        const std::string kat = "CKey renderer test";
        const std::array<std::array<uint64_t, 2>, 2> expected_arc = {{{kArcKatV3, kArcKatV31}, {kArcGroupedKatV3, kArcGroupedKatV31}}};
        for (int g = 0; g < 2; ++g) for (int j = 0; j < 2; ++j) {
            const uint32_t version = j ? 31u : 3u; const ckey_scalable_layout arc = g ? CKEY_LAYOUT_STANDARD_ARC_GROUPED : CKEY_LAYOUT_STANDARD_ARC;
            CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(kat.data()), kat.size(), version, CKEY_ECC_M, arc, &ps, &pd) == CKEY_OK);
            const ScalableLayout &L = scalable_layout(static_cast<int>(version), arc);
            uint64_t hash = 0xcbf29ce484222325ull;
            const auto fnv = [&](uint8_t b) { hash = (hash ^ b) * 0x100000001b3ull; };
            for (size_t i = 0; i < L.nodes.size(); ++i) { for (int k = 0; k < 4; ++k) fnv(static_cast<uint8_t>(L.nodes[i].id >> (8 * k))); fnv(scalable_site_value(L, &ps, i) ? 1 : 0); }
            std::printf("Format 0.%u v%u conformance hash %016llx\n", L.format, version, static_cast<unsigned long long>(hash));
            CKEY_CHECK(hash == expected_arc[static_cast<size_t>(g)][static_cast<size_t>(j)]);
        }
    }

    // ---- Capacity grows smoothly ----
    std::printf(" v  modules  sites   L     M     Q     H  (payload bytes)\n");
    size_t previous_m = 0;
    for (int v = 1; v <= kScalableVersions; ++v) {
        const size_t m = ckey_scalable_capacity(static_cast<uint32_t>(v), CKEY_ECC_M, CKEY_LAYOUT_STANDARD);
        std::printf("%2d %6d %7zu %5zu %5zu %5zu %5zu\n", v, kScalableGrid[v - 1], scalable_layout(v).nodes.size(),
                    ckey_scalable_capacity(v, CKEY_ECC_L, CKEY_LAYOUT_STANDARD), m, ckey_scalable_capacity(v, CKEY_ECC_Q, CKEY_LAYOUT_STANDARD), ckey_scalable_capacity(v, CKEY_ECC_H, CKEY_LAYOUT_STANDARD));
        CKEY_CHECK(m > previous_m);
        if (previous_m) CKEY_CHECK(static_cast<double>(m) / previous_m < 1.6);  // no large jumps
        CKEY_CHECK(ckey_scalable_capacity(v, CKEY_ECC_L, CKEY_LAYOUT_STANDARD) > m && m > ckey_scalable_capacity(v, CKEY_ECC_Q, CKEY_LAYOUT_STANDARD) && ckey_scalable_capacity(v, CKEY_ECC_Q, CKEY_LAYOUT_STANDARD) > ckey_scalable_capacity(v, CKEY_ECC_H, CKEY_LAYOUT_STANDARD));
        previous_m = m;
    }
    CKEY_CHECK(ckey_scalable_capacity(0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD) == 0 && ckey_scalable_capacity(32, CKEY_ECC_M, CKEY_LAYOUT_STANDARD) == 0);

    // ---- Auto version selection is the smallest that fits ----
    ckey_scalable_symbol s{}; ckey_diagnostics d{};
    const std::string text33 = "https://citeory.com/tour?ref=ckey";  // 33 characters
    CKEY_CHECK(text33.size() == 33);
    CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text33.data()), text33.size(), 0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
    CKEY_CHECK(ckey_scalable_capacity(s.version, CKEY_ECC_M, CKEY_LAYOUT_STANDARD) >= 33 && (s.version == 1 || ckey_scalable_capacity(s.version - 1, CKEY_ECC_M, CKEY_LAYOUT_STANDARD) < 33));
    std::printf("33-character payload -> Profile 0 v%u (%d modules across) at level M\n", s.version, kScalableGrid[s.version - 1]);

    // ---- Byte-level ECC: correct up to parity/2 errors in every block ----
    for (int level = 0; level < 4; ++level) {
        const auto payload = pseudo(ckey_scalable_capacity(14, static_cast<ckey_ecc_level>(level), CKEY_LAYOUT_STANDARD), 5u + level);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 14, static_cast<ckey_ecc_level>(level), CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
        const auto blocks = scalable_blocks(s.codeword_length, level);
        auto damaged = s;
        // Interleaved position of (block b, byte j) is the count of earlier bytes.
        for (size_t b = 0; b < blocks.size(); ++b) {
            const size_t t = (blocks[b].n - blocks[b].k) / 2;
            for (size_t e = 0; e < t; ++e) {
                const size_t j = (e * 7 + 3) % blocks[b].n; size_t pos = 0;
                for (size_t jj = 0; jj < j; ++jj) for (size_t bb = 0; bb < blocks.size(); ++bb) pos += jj < blocks[bb].n;
                for (size_t bb = 0; bb < b; ++bb) pos += j < blocks[bb].n;
                damaged.codeword[pos] ^= static_cast<uint8_t>(0x5a + e);
            }
        }
        std::vector<uint8_t> out(65535); size_t len = 0;
        CKEY_CHECK(ckey_decode_scalable(damaged.codeword, damaged.codeword_length, 14, CKEY_LAYOUT_STANDARD, out.data(), out.size(), &len, &d) == CKEY_OK);
        CKEY_CHECK(len == payload.size() && std::equal(payload.begin(), payload.end(), out.begin()));
    }

    // ---- Every version: render, rasterize, decode; the keys identify the version ----
    for (int v = 1; v <= kScalableVersions; ++v) {
        for (int kind = 0; kind < 3; ++kind) {
            const size_t cap = ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_STANDARD);
            const auto payload = kind == 0 ? pseudo(cap, 100u + v) : std::vector<uint8_t>(cap, kind == 1 ? 0x00 : 0xff);
            CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), static_cast<uint32_t>(v), CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
            const uint32_t detected = image_round_trip(s, payload, 6.0, false);
            if (detected != static_cast<uint32_t>(v)) std::printf("version %d payload kind %d: detected %u\n", v, kind, detected);
            CKEY_CHECK(detected == static_cast<uint32_t>(v));
        }
        const auto payload = pseudo(ckey_scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_STANDARD), 900u + v);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), static_cast<uint32_t>(v), CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
        CKEY_CHECK(image_round_trip(s, payload, 8.0, true) == static_cast<uint32_t>(v));
    }
    std::printf("all %d versions decode from rendered images (6 px/module clean, 8 px/module blurred) and are identified by their keys\n", kScalableVersions);
    // ---- Known-answer vectors: FNV-1a-64 over every site in placement order
    // of (identity little-endian, printed value), payload "CKey renderer
    // test" at level M. Integer-only, so platform independent. ----
    {
        const std::string text = "CKey renderer test";
        for (const auto &[version, expected] : {std::pair{2u, kScalableKatV2}, std::pair{31u, kScalableKatV31}}) {
            CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text.data()), text.size(), version, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
            const ScalableLayout &L = scalable_layout(static_cast<int>(version));
            uint64_t hash = 0xcbf29ce484222325ull;
            const auto fnv = [&](uint8_t b) { hash = (hash ^ b) * 0x100000001b3ull; };
            for (size_t i = 0; i < L.nodes.size(); ++i) { for (int k = 0; k < 4; ++k) fnv(static_cast<uint8_t>(L.nodes[i].id >> (8 * k))); fnv(scalable_site_value(L, &s, i) ? 1 : 0); }
            std::printf("Profile 0 v%u conformance hash %016llx\n", version, static_cast<unsigned long long>(hash));
            CKEY_CHECK(hash == expected);
        }
    }
    // ---- Design system ----
    {
        ckey_style base; ckey_style_default(&base);
        const auto payload = pseudo(ckey_scalable_capacity(11, CKEY_ECC_M, CKEY_LAYOUT_STANDARD), 4242);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 11, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
        CKEY_CHECK(ckey_check_scalable_style(&s, &base, &d) == CKEY_OK);
        CKEY_CHECK(render_styled(s, base) == render(s));  // the plain renderer is the default style

        // Every payload-mark shape option decodes, at a coarse and a fine version.
        for (uint32_t version : {2u, 20u}) {
            const auto data = pseudo(ckey_scalable_capacity(version, CKEY_ECC_M, CKEY_LAYOUT_STANDARD), 77 + version);
            CKEY_CHECK(ckey_encode_scalable(data.data(), data.size(), version, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
            const double unit = 7.0 / (29.0 / ckey_scalable_modules_across(version));
            for (int shape = 0; shape < 4; ++shape) for (int rings = 0; rings < 2; ++rings) for (int rays = 0; rays < 2; ++rays) {
                ckey_style st = base;
                st.modules = static_cast<ckey_module_shape>(shape); st.rings = static_cast<ckey_ring_style>(rings); st.rays = static_cast<ckey_ray_cap>(rays);
                const uint32_t got = decode_svg(render_styled(s, st), unit, data);
                if (got != version) std::printf("style shape %d rings %d rays %d failed at v%u\n", shape, rings, rays, version);
                CKEY_CHECK(got == version);
            }
        }
        std::printf("design system: 16 mark-shape combinations decode at v2 and v20\n");

        // Concept-art look: champagne sheen, navy vignette, sage/grey accents, depth, grain.
        ckey_style art; CKEY_CHECK(ckey_style_preset_fill(&art, CKEY_PRESET_CONCEPT) == CKEY_OK);
        CKEY_CHECK(ckey_style_preset_fill(&art, static_cast<ckey_style_preset>(7)) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_style_preset_fill(&art, CKEY_PRESET_CONCEPT) == CKEY_OK);
        // The default is black on white (specification §4).
        CKEY_CHECK(base.ink.r == 0 && base.ink.g == 0 && base.background.r == 255 && base.background.b == 255);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 11, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
        CKEY_CHECK(ckey_check_scalable_style(&s, &art, &d) == CKEY_OK);
        std::printf("concept style: %s\n", d.message);
        CKEY_CHECK(decode_svg(render_styled(s, art), 7.0 / 0.5, payload) == 11);

        // The checker refuses near-invisible styles, with a reason; lower contrast is a warning
        // (certification's photo sweep decides whether it scans).
        ckey_style low = base; low.ink = low.ink_shade = {0xe8, 0xe8, 0xe8};
        CKEY_CHECK(ckey_check_scalable_style(&s, &low, &d) == CKEY_INVALID_ARGUMENT);
        std::printf("refused: %s\n", d.message);
        ckey_style mixed = base; mixed.background = mixed.background_edge = {0x80, 0x80, 0x80}; mixed.accents[0] = {0xff, 0xff, 0xff}; mixed.accent_count = 1; mixed.accent_percent = 10;
        CKEY_CHECK(ckey_check_scalable_style(&s, &mixed, &d) == CKEY_INVALID_ARGUMENT);
        ckey_style light = base; light.background = light.background_edge = {0xf4, 0xf1, 0xe8}; light.ink = light.ink_shade = {0x10, 0x10, 0x10};
        CKEY_CHECK(ckey_check_scalable_style(&s, &light, &d) == CKEY_OK);
        light.shadow = 0.6; CKEY_CHECK(ckey_check_scalable_style(&s, &light, &d) == CKEY_OK && std::string(d.message).find("certify it") != std::string::npos);
        size_t n = 0; CKEY_CHECK(ckey_render_scalable_styled_svg(&s, &low, 8, nullptr, 0, &n) == CKEY_INVALID_ARGUMENT);

        // Logos: the empty centre costs nothing; a larger logo needs a plate and
        // must stay within half the worst block's correction. The largest logo
        // the checker accepts at each level still decodes (hidden marks are
        // corrected by Reed-Solomon, as with QR).
        ckey_style logo = base; logo.logo_href = "data:image/png;base64,iVBORw0KGgo=";
        CKEY_CHECK(ckey_check_scalable_style(&s, &logo, &d) == CKEY_OK && ckey_scalable_logo_load(&s, 0) == 0);
        logo.logo_radius = 5.0; CKEY_CHECK(ckey_check_scalable_style(&s, &logo, &d) == CKEY_INVALID_ARGUMENT);  // no plate
        logo.logo_plate = 1; logo.logo_radius = ckey_scalable_logo_max_radius(11, CKEY_LAYOUT_STANDARD) + 0.1;
        CKEY_CHECK(ckey_check_scalable_style(&s, &logo, &d) == CKEY_INVALID_ARGUMENT);  // would hide the Ring Key
        for (uint32_t version : {6u, 11u, 20u}) for (int level = 0; level < 4; ++level) {
            const auto data = pseudo(ckey_scalable_capacity(version, static_cast<ckey_ecc_level>(level), CKEY_LAYOUT_STANDARD), 900 + version * 4 + level);
            CKEY_CHECK(ckey_encode_scalable(data.data(), data.size(), version, static_cast<ckey_ecc_level>(level), CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
            double best = 0;
            for (double r = 3.0; r <= ckey_scalable_logo_max_radius(version, CKEY_LAYOUT_STANDARD); r += 0.05) { logo.logo_radius = r; if (ckey_check_scalable_style(&s, &logo, &d) == CKEY_OK) best = r; }
            logo.logo_radius = best;
            const uint32_t got = best > 0 ? decode_svg(render_styled(s, logo), 7.0 / (29.0 / ckey_scalable_modules_across(version)), data) : version;
            std::printf("v%u level %c: largest accepted logo radius %.2f (load %.0f%%) -> %s\n", version, "LMQH"[level], best,
                        100 * ckey_scalable_logo_load(&s, best), got == version ? "decodes" : "FAILS");
            CKEY_CHECK(got == version);
        }
    }
    // ---- Print: device-grid bitmaps, and P0.1's advantage on a printer model ----
    {
        const std::string text = "CKEY-CAL P0.1 120B 0.42in ";
        std::string payload = text; while (payload.size() < 120) payload += "0123456789";
        payload.resize(120);
        const std::vector<uint8_t> bytes(payload.begin(), payload.end());
        // Old inkjet on plain paper (600 dpi, 0.045 mm ink spread), black on paper, camera 23 px/mm.
        const printsim::Printer inkjet{600, 0.045, 0.03}; const printsim::Camera camera{23, 0.7, 4};
        bool decoded[2] = {false, false};
        for (int family = 0; family < 2; ++family) {
            ckey_scalable_symbol ps{}; ckey_diagnostics pd{};
            CKEY_CHECK(ckey_encode_scalable(bytes.data(), bytes.size(), 0, CKEY_ECC_M, static_cast<ckey_scalable_layout>(family), &ps, &pd) == CKEY_OK);
            size_t side = 0;
            CKEY_CHECK(ckey_render_scalable_print(&ps, nullptr, 0.5, 600, nullptr, 0, &side) == CKEY_INSUFFICIENT_BUFFER && side == 300);
            std::vector<uint8_t> bitmap(side * side);
            CKEY_CHECK(ckey_render_scalable_print(&ps, nullptr, 0.5, 600, bitmap.data(), bitmap.size(), &side) == CKEY_OK);
            for (uint8_t v : bitmap) CKEY_CHECK(v == 0 || v == 255);  // no grey for a printer to halftone
            int cam_side = 0;
            const auto frame = printsim::print_and_capture(render(ps), 0.42 * 25.4, inkjet, camera, true, 99, cam_side);
            const double k = cam_side / 31.0, q[8] = {k, k, 30 * k, k, 30 * k, 30 * k, k, 30 * k};
            std::vector<uint8_t> out(65535); size_t len = 0; uint32_t version = 0, layout = 0;
            decoded[family] = ckey_decode_scalable_gray8(frame.data(), cam_side, cam_side, cam_side, q, out.data(), out.size(), &len, &version, &layout, &pd) == CKEY_OK &&
                              len == bytes.size() && std::equal(bytes.begin(), bytes.end(), out.begin());
        }
        std::printf("120-byte payload printed 0.42 in wide on the old-inkjet model: P0 %s, P0.1 %s\n", decoded[0] ? "decodes" : "fails", decoded[1] ? "decodes" : "fails");
        CKEY_CHECK(!decoded[0] && decoded[1]);

        // P0.1 conformance vectors (same method as Profile 0's).
        const std::string kat = "CKey renderer test";
        for (const auto &[version, expected] : {std::pair{3u, kPrintKatV3}, std::pair{31u, kPrintKatV31}}) {
            ckey_scalable_symbol ps{}; ckey_diagnostics pd{};
            CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(kat.data()), kat.size(), version, CKEY_ECC_M, CKEY_LAYOUT_PRINT, &ps, &pd) == CKEY_OK);
            const ScalableLayout &L = scalable_layout(static_cast<int>(version), 1);
            uint64_t hash = 0xcbf29ce484222325ull;
            const auto fnv = [&](uint8_t b) { hash = (hash ^ b) * 0x100000001b3ull; };
            for (size_t i = 0; i < L.nodes.size(); ++i) { for (int k = 0; k < 4; ++k) fnv(static_cast<uint8_t>(L.nodes[i].id >> (8 * k))); fnv(scalable_site_value(L, &ps, i) ? 1 : 0); }
            std::printf("Profile 0.1 v%u conformance hash %016llx\n", version, static_cast<unsigned long long>(hash));
            CKEY_CHECK(hash == expected);
        }
    }
    std::printf("CKey Profile 0 tests passed\n");
}
