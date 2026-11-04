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
    std::printf("CKey Profile 0 tests passed\n");
}
