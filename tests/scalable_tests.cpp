// Profile 0 (scalable family) tests. White-box: includes the core
// translation unit to check layout invariants; built without ckey_core.
#include "../src/ckey.cpp"
#include "check.h"
#include "svg_raster.h"

#include <cstdio>

namespace {

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
    if (ckey_decode_scalable_gray8(px.data(), side, side, side, q, out.data(), out.size(), &len, &version, &d) != CKEY_OK) return 0;
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
    if (ckey_decode_scalable_gray8(px.data(), side, side, side, q, out.data(), out.size(), &len, &version, &d) != CKEY_OK) return 0;
    if (len != payload.size() || !std::equal(payload.begin(), payload.end(), out.begin())) return 0;
    return version;
}

}  // namespace

int main() {
    // ---- Table invariants ----
    for (int v = 1; v <= kScalableVersions; ++v) {
        const ScalableLayout &L = scalable_layout(v); const double p = L.pitch;
        CKEY_CHECK(L.slots % 8 == 0);
        CKEY_CHECK(L.ring_radius(L.rings - 1) <= kScalableDialInner - 2.38 * p + 1e-9);      // ring band clears the dial
        CKEY_CHECK(L.ring_radius(L.rings) > kScalableDialInner - 2.38 * p + 1e-9 || L.rings == kScalableRings[v - 1]);
        CKEY_CHECK(L.ring_key_slot() + 15 < L.ray_key_three());
        CKEY_CHECK(L.codeword_bytes <= CKEY_SCALABLE_MAX_CODEWORD_BYTES);
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

    // ---- Capacity grows smoothly ----
    std::printf(" v  modules  sites   L     M     Q     H  (payload bytes)\n");
    size_t previous_m = 0;
    for (int v = 1; v <= kScalableVersions; ++v) {
        const size_t m = ckey_scalable_capacity(static_cast<uint32_t>(v), CKEY_ECC_M);
        std::printf("%2d %6d %7zu %5zu %5zu %5zu %5zu\n", v, kScalableGrid[v - 1], scalable_layout(v).nodes.size(),
                    ckey_scalable_capacity(v, CKEY_ECC_L), m, ckey_scalable_capacity(v, CKEY_ECC_Q), ckey_scalable_capacity(v, CKEY_ECC_H));
        CKEY_CHECK(m > previous_m);
        if (previous_m) CKEY_CHECK(static_cast<double>(m) / previous_m < 1.6);  // no large jumps
        CKEY_CHECK(ckey_scalable_capacity(v, CKEY_ECC_L) > m && m > ckey_scalable_capacity(v, CKEY_ECC_Q) && ckey_scalable_capacity(v, CKEY_ECC_Q) > ckey_scalable_capacity(v, CKEY_ECC_H));
        previous_m = m;
    }
    CKEY_CHECK(ckey_scalable_capacity(0, CKEY_ECC_M) == 0 && ckey_scalable_capacity(32, CKEY_ECC_M) == 0);

    // ---- Auto version selection is the smallest that fits ----
    ckey_scalable_symbol s{}; ckey_diagnostics d{};
    const std::string text33 = "https://citeory.com/tour?ref=ckey";  // 33 characters
    CKEY_CHECK(text33.size() == 33);
    CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text33.data()), text33.size(), 0, CKEY_ECC_M, &s, &d) == CKEY_OK);
    CKEY_CHECK(ckey_scalable_capacity(s.version, CKEY_ECC_M) >= 33 && (s.version == 1 || ckey_scalable_capacity(s.version - 1, CKEY_ECC_M) < 33));
    std::printf("33-character payload -> Profile 0 v%u (%d modules across) at level M\n", s.version, kScalableGrid[s.version - 1]);

    // ---- Byte-level ECC: correct up to parity/2 errors in every block ----
    for (int level = 0; level < 4; ++level) {
        const auto payload = pseudo(ckey_scalable_capacity(14, static_cast<ckey_ecc_level>(level)), 5u + level);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 14, static_cast<ckey_ecc_level>(level), &s, &d) == CKEY_OK);
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
        CKEY_CHECK(ckey_decode_scalable(damaged.codeword, damaged.codeword_length, 14, out.data(), out.size(), &len, &d) == CKEY_OK);
        CKEY_CHECK(len == payload.size() && std::equal(payload.begin(), payload.end(), out.begin()));
    }

    // ---- Every version: render, rasterize, decode; the keys identify the version ----
    for (int v = 1; v <= kScalableVersions; ++v) {
        for (int kind = 0; kind < 3; ++kind) {
            const size_t cap = ckey_scalable_capacity(v, CKEY_ECC_M);
            const auto payload = kind == 0 ? pseudo(cap, 100u + v) : std::vector<uint8_t>(cap, kind == 1 ? 0x00 : 0xff);
            CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), static_cast<uint32_t>(v), CKEY_ECC_M, &s, &d) == CKEY_OK);
            const uint32_t detected = image_round_trip(s, payload, 6.0, false);
            if (detected != static_cast<uint32_t>(v)) std::printf("version %d payload kind %d: detected %u\n", v, kind, detected);
            CKEY_CHECK(detected == static_cast<uint32_t>(v));
        }
        const auto payload = pseudo(ckey_scalable_capacity(v, CKEY_ECC_M), 900u + v);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), static_cast<uint32_t>(v), CKEY_ECC_M, &s, &d) == CKEY_OK);
        CKEY_CHECK(image_round_trip(s, payload, 8.0, true) == static_cast<uint32_t>(v));
    }
    std::printf("all %d versions decode from rendered images (6 px/module clean, 8 px/module blurred) and are identified by their keys\n", kScalableVersions);
    // ---- Known-answer vectors: FNV-1a-64 over every site in placement order
    // of (identity little-endian, printed value), payload "CKey renderer
    // test" at level M. Integer-only, so platform independent. ----
    {
        const std::string text = "CKey renderer test";
        for (const auto &[version, expected] : {std::pair{2u, kScalableKatV2}, std::pair{31u, kScalableKatV31}}) {
            CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text.data()), text.size(), version, CKEY_ECC_M, &s, &d) == CKEY_OK);
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
        const auto payload = pseudo(ckey_scalable_capacity(11, CKEY_ECC_M), 4242);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 11, CKEY_ECC_M, &s, &d) == CKEY_OK);
        CKEY_CHECK(ckey_check_scalable_style(&s, &base, &d) == CKEY_OK);
        CKEY_CHECK(render_styled(s, base) == render(s));  // the plain renderer is the default style

        // Every payload-mark shape option decodes, at a coarse and a fine version.
        for (uint32_t version : {2u, 20u}) {
            const auto data = pseudo(ckey_scalable_capacity(version, CKEY_ECC_M), 77 + version);
            CKEY_CHECK(ckey_encode_scalable(data.data(), data.size(), version, CKEY_ECC_M, &s, &d) == CKEY_OK);
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
        ckey_style art = base;
        art.background = {0x14, 0x16, 0x1e}; art.background_edge = {0x07, 0x08, 0x0c};
        art.ink = {0xf6, 0xee, 0xe0}; art.ink_shade = {0xc8, 0xb6, 0x9a};
        art.accents[0] = {0xa9, 0xb8, 0x96}; art.accents[1] = {0x9e, 0xa1, 0xa6}; art.accent_count = 2; art.accent_percent = 18;
        art.bevel = 0.6; art.shadow = 0.5; art.texture = 0.4;
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 11, CKEY_ECC_M, &s, &d) == CKEY_OK);
        CKEY_CHECK(ckey_check_scalable_style(&s, &art, &d) == CKEY_OK);
        std::printf("concept style: %s\n", d.message);
        CKEY_CHECK(decode_svg(render_styled(s, art), 7.0 / 0.5, payload) == 11);

        // The checker refuses unscannable styles, with a reason.
        ckey_style low = base; low.ink = low.ink_shade = {0x44, 0x44, 0x44};
        CKEY_CHECK(ckey_check_scalable_style(&s, &low, &d) == CKEY_INVALID_ARGUMENT);
        std::printf("refused: %s\n", d.message);
        ckey_style mixed = base; mixed.accents[0] = {0x01, 0x01, 0x01}; mixed.accent_count = 1; mixed.accent_percent = 10;
        CKEY_CHECK(ckey_check_scalable_style(&s, &mixed, &d) == CKEY_INVALID_ARGUMENT);
        ckey_style light = base; light.background = light.background_edge = {0xf4, 0xf1, 0xe8}; light.ink = light.ink_shade = {0x10, 0x10, 0x10};
        CKEY_CHECK(ckey_check_scalable_style(&s, &light, &d) == CKEY_OK);
        light.shadow = 0.6; CKEY_CHECK(ckey_check_scalable_style(&s, &light, &d) == CKEY_INVALID_ARGUMENT);
        size_t n = 0; CKEY_CHECK(ckey_render_scalable_styled_svg(&s, &low, 8, nullptr, 0, &n) == CKEY_INVALID_ARGUMENT);

        // Logos: the empty centre costs nothing; a larger logo needs a plate and
        // must stay within half the worst block's correction. The largest logo
        // the checker accepts at each level still decodes (hidden marks are
        // corrected by Reed-Solomon, as with QR).
        ckey_style logo = base; logo.logo_href = "data:image/png;base64,iVBORw0KGgo=";
        CKEY_CHECK(ckey_check_scalable_style(&s, &logo, &d) == CKEY_OK && ckey_scalable_logo_load(&s, 0) == 0);
        logo.logo_radius = 5.0; CKEY_CHECK(ckey_check_scalable_style(&s, &logo, &d) == CKEY_INVALID_ARGUMENT);  // no plate
        logo.logo_plate = 1; logo.logo_radius = ckey_scalable_logo_max_radius(11) + 0.1;
        CKEY_CHECK(ckey_check_scalable_style(&s, &logo, &d) == CKEY_INVALID_ARGUMENT);  // would hide the Ring Key
        for (uint32_t version : {6u, 11u, 20u}) for (int level = 0; level < 4; ++level) {
            const auto data = pseudo(ckey_scalable_capacity(version, static_cast<ckey_ecc_level>(level)), 900 + version * 4 + level);
            CKEY_CHECK(ckey_encode_scalable(data.data(), data.size(), version, static_cast<ckey_ecc_level>(level), &s, &d) == CKEY_OK);
            double best = 0;
            for (double r = 3.0; r <= ckey_scalable_logo_max_radius(version); r += 0.05) { logo.logo_radius = r; if (ckey_check_scalable_style(&s, &logo, &d) == CKEY_OK) best = r; }
            logo.logo_radius = best;
            const uint32_t got = best > 0 ? decode_svg(render_styled(s, logo), 7.0 / (29.0 / ckey_scalable_modules_across(version)), data) : version;
            std::printf("v%u level %c: largest accepted logo radius %.2f (load %.0f%%) -> %s\n", version, "LMQH"[level], best,
                        100 * ckey_scalable_logo_load(&s, best), got == version ? "decodes" : "FAILS");
            CKEY_CHECK(got == version);
        }
    }
    std::printf("CKey Profile 0 tests passed\n");
}
