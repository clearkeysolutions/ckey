// Reader-hardening tests (0.1.0-build.86). White-box: includes the core
// translation unit.
//   erasure Reed-Solomon, soft decoding, levels U and X, byte-grouped
//   placement (Formats 0.29 / 0.30), blot and partial-frame reads, the frame
//   scan with a region of interest, tracking, camera advice, work budgets.
#include "../src/ckey.cpp"
#include "check.h"
#include "svg_raster.h"

#include <chrono>
#include <cstdio>

namespace {

struct Rng { uint32_t s; uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; } double unit() { return (next() & 0xffffff) / 16777216.0; } };

std::vector<uint8_t> pseudo(size_t n, uint32_t seed) {
    std::vector<uint8_t> out(n); Rng r{seed};
    for (auto &b : out) b = static_cast<uint8_t>(r.next());
    return out;
}

std::string render(const ckey_scalable_symbol &s) {
    size_t n = 0; CKEY_CHECK(ckey_render_scalable_svg(&s, 8, nullptr, 0, &n) == CKEY_INSUFFICIENT_BUFFER);
    std::string svg(n, '\0'); CKEY_CHECK(ckey_render_scalable_svg(&s, 8, svg.data(), n, &n) == CKEY_OK);
    svg.resize(n - 1); return svg;
}

// A symbol rasterised alone: `side` pixels across its 31-unit canvas.
struct Raster { std::vector<uint8_t> px; int side = 0; double quad[8] = {}; };
Raster rasterise(const ckey_scalable_symbol &s, double px_per_module) {
    Raster r; const ScalableLayout &L = scalable_layout(static_cast<int>(s.version));
    r.side = static_cast<int>(std::ceil(31 * px_per_module / L.pitch));
    r.px = svgtest::rasterize(svgtest::parse(render(s)), r.side, r.side, 3);
    const double k = static_cast<double>(r.side) / 31.0, lo = k, hi = 30 * k;
    const double q[8] = {lo, lo, hi, lo, hi, hi, lo, hi}; std::copy(q, q + 8, r.quad);
    return r;
}

// The symbol placed in a larger grey frame at (ox, oy); quad in frame pixels.
struct Frame { std::vector<uint8_t> px; int w = 0, h = 0; double quad[8] = {}; };
Frame place(const Raster &r, int w, int h, int ox, int oy, uint8_t background = 120) {
    Frame f; f.w = w; f.h = h; f.px.assign(static_cast<size_t>(w) * h, background);
    for (int y = 0; y < r.side; ++y) for (int x = 0; x < r.side; ++x) {
        const int fx = ox + x, fy = oy + y;
        if (fx >= 0 && fy >= 0 && fx < w && fy < h) f.px[static_cast<size_t>(fy) * w + fx] = r.px[static_cast<size_t>(y) * r.side + x];
    }
    for (int i = 0; i < 4; ++i) { f.quad[2 * i] = r.quad[2 * i] + ox; f.quad[2 * i + 1] = r.quad[2 * i + 1] + oy; }
    return f;
}

ckey_scalable_symbol *symbol() { static ckey_scalable_symbol s; return &s; }

}  // namespace

int main() {
    ckey_diagnostics d{};
    ckey_scalable_symbol &s = *symbol();
    std::vector<uint8_t> out(65535); size_t len = 0;

    // ---- Errors-and-erasures Reed-Solomon: 2 * errors + erasures <= parity ----
    {
        Rng rng{7}; int failures = 0;
        for (int it = 0; it < 2000; ++it) {
            const size_t n = 20 + rng.next() % 236, parity = 2 + 2 * (rng.next() % std::min<size_t>(121, (n - 1) / 2));
            if (parity >= n) continue;
            std::vector<uint8_t> cw(n); for (size_t i = 0; i < n - parity; ++i) cw[i] = static_cast<uint8_t>(rng.next());
            rs_encode_generic(cw, n - parity, parity); const auto reference = cw;
            const size_t erased = rng.next() % (parity + 1); size_t errors = (parity - erased) / 2; if (errors) errors = rng.next() % (errors + 1);
            std::vector<size_t> pos(n); std::iota(pos.begin(), pos.end(), size_t{0});
            for (size_t i = n - 1; i > 0; --i) std::swap(pos[i], pos[rng.next() % (i + 1)]);
            const std::vector<size_t> erasures(pos.begin(), pos.begin() + static_cast<std::ptrdiff_t>(erased));
            for (size_t i = 0; i < erased; ++i) if (rng.next() % 4) cw[pos[i]] = static_cast<uint8_t>(rng.next());
            for (size_t i = 0; i < errors; ++i) cw[pos[erased + i]] ^= static_cast<uint8_t>(1 + rng.next() % 255);
            uint32_t fixed = 0;
            if (!rs_correct_erasures(cw, parity, erasures, 0, fixed) || cw != reference || fixed != errors) ++failures;
        }
        CKEY_CHECK(failures == 0);
        // One error too many is refused, never "corrected" into something else that passes.
        std::vector<uint8_t> cw(100); for (size_t i = 0; i < 80; ++i) cw[i] = static_cast<uint8_t>(i * 7);
        rs_encode_generic(cw, 80, 20); const auto reference = cw;
        for (size_t i = 0; i < 11; ++i) cw[i * 3] ^= 0x55;
        uint32_t fixed = 0; const bool accepted = rs_correct_erasures(cw, 20, {}, 0, fixed);
        CKEY_CHECK(!accepted || cw != reference);
    }

    // ---- Levels U (80%) and X (95%): the capacity table stated in the repair notes ----
    {
        const struct { int v; size_t cap[6]; } standard[] = {{1, {33, 27, 17, 9, 0, 0}}, {4, {66, 52, 38, 24, 6, 0}}, {8, {159, 131, 99, 67, 27, 0}},
            {11, {276, 228, 176, 124, 56, 4}}, {16, {607, 505, 391, 277, 133, 25}}, {21, {1289, 1085, 835, 593, 293, 65}},
            {26, {2684, 2242, 1748, 1244, 620, 152}}, {31, {5555, 4619, 3579, 2585, 1285, 303}}};
        for (const auto &row : standard) for (int level = 0; level < 6; ++level)
            CKEY_CHECK(ckey_scalable_capacity(static_cast<uint32_t>(row.v), static_cast<ckey_ecc_level>(level), CKEY_LAYOUT_STANDARD) == row.cap[level]);
        CKEY_CHECK(ckey_scalable_capacity(31, CKEY_ECC_U, CKEY_LAYOUT_PRINT) == 945 && ckey_scalable_capacity(31, CKEY_ECC_X, CKEY_LAYOUT_PRINT) == 223);
        CKEY_CHECK(ckey_scalable_capacity(15, CKEY_ECC_X, CKEY_LAYOUT_PRINT) == 9 && ckey_scalable_capacity(14, CKEY_ECC_X, CKEY_LAYOUT_PRINT) < 9);
        // Grouped placement never changes capacity.
        for (int v = 1; v <= 31; ++v) for (int level = 0; level < 6; ++level) for (int family = 0; family < 2; ++family)
            CKEY_CHECK(ckey_scalable_capacity(static_cast<uint32_t>(v), static_cast<ckey_ecc_level>(level), static_cast<ckey_scalable_layout>(family)) ==
                       ckey_scalable_capacity(static_cast<uint32_t>(v), static_cast<ckey_ecc_level>(level), static_cast<ckey_scalable_layout>(family + 2)));
        // A version that carries nothing at a level is refused, with a reason.
        const uint8_t one = 1;
        CKEY_CHECK(ckey_encode_scalable(&one, 1, 1, CKEY_ECC_X, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_INVALID_ARGUMENT && d.message[0]);
        // Auto version at U and X picks the smallest version that fits.
        const auto payload = pseudo(64, 3);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), 64, 0, CKEY_ECC_U, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK && s.version == 13);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), 64, 0, CKEY_ECC_X, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK && s.version == 21);
        // U corrects 40% unknown bad bytes per block; H's limit is 30%.
        for (const int level : {CKEY_ECC_U, CKEY_ECC_X}) {
            const size_t cap = ckey_scalable_capacity(16, static_cast<ckey_ecc_level>(level), CKEY_LAYOUT_STANDARD); const auto p = pseudo(cap, 11);
            CKEY_CHECK(ckey_encode_scalable(p.data(), cap, 16, static_cast<ckey_ecc_level>(level), CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
            std::vector<uint8_t> cw(s.codeword, s.codeword + s.codeword_length);
            const auto blocks = scalable_blocks(cw.size(), level); size_t pos = 0;
            for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) { if (j < (blocks[b].n - blocks[b].k) / 2) cw[pos] ^= 0xa5; ++pos; }
            CKEY_CHECK(ckey_decode_scalable(cw.data(), cw.size(), 16, CKEY_LAYOUT_STANDARD, out.data(), out.size(), &len, &d) == CKEY_OK && len == cap && std::equal(p.begin(), p.end(), out.begin()));
        }
    }

    // ---- Soft decoding: known-bad bytes cost one parity byte, not two ----
    for (int layout = 0; layout < 4; ++layout) for (const int v : {5, 16, 31}) for (int level = 0; level < 6; ++level) {
        const size_t cap = scalable_capacity(v, level, layout); if (!cap) continue;
        const auto payload = pseudo(cap, static_cast<uint32_t>(v * 31 + level));
        CKEY_CHECK(ckey_encode_scalable(payload.data(), cap, static_cast<uint32_t>(v), static_cast<ckey_ecc_level>(level), static_cast<ckey_scalable_layout>(layout), &s, &d) == CKEY_OK);
        std::vector<uint8_t> cw(s.codeword, s.codeword + s.codeword_length), reliability(cw.size(), 255);
        const auto blocks = scalable_blocks(cw.size(), level); size_t pos = 0; bool damaged = false;
        for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) {
            const size_t parity = blocks[b].n - blocks[b].k;
            if (parity >= 8 && j < static_cast<size_t>(0.8 * static_cast<double>(parity))) { cw[pos] ^= 0x5a; reliability[pos] = 0; damaged = true; }
            ++pos;
        }
        const auto layout_id = static_cast<ckey_scalable_layout>(layout);
        if (damaged) CKEY_CHECK(ckey_decode_scalable(cw.data(), cw.size(), static_cast<uint32_t>(v), layout_id, out.data(), out.size(), &len, &d) != CKEY_OK);
        CKEY_CHECK(ckey_decode_scalable_soft(cw.data(), reliability.data(), cw.size(), static_cast<uint32_t>(v), layout_id, out.data(), out.size(), &len, &d) == CKEY_OK);
        CKEY_CHECK(len == cap && std::equal(payload.begin(), payload.end(), out.begin()) && (!damaged || d.erasures_used > 0));
        // A codeword of the other placement must not decode as this one.
        const auto other = static_cast<ckey_scalable_layout>(layout ^ 2);
        CKEY_CHECK(ckey_decode_scalable(s.codeword, s.codeword_length, static_cast<uint32_t>(v), other, out.data(), out.size(), &len, &d) != CKEY_OK);
    }

    // ---- Byte-grouped placement (Formats 0.29 / 0.30) ----
    for (int family = 0; family < 2; ++family) for (int v = 1; v <= 31; ++v) {
        const ScalableLayout &B = scalable_layout(v, family), &G = scalable_layout(v, family + 2);
        CKEY_CHECK(G.grouped && !B.grouped && G.nodes.size() == B.nodes.size() && G.codeword_bytes == B.codeword_bytes);
        CKEY_CHECK(G.format == (family ? CKEY_SCALABLE_PRINT_GROUPED_FORMAT_VERSION : CKEY_SCALABLE_GROUPED_FORMAT_VERSION));
        // Same sites: the grouped order is a permutation of the scattered one.
        std::vector<uint8_t> seen(B.nodes.size(), 0);
        for (size_t n = 0; n < G.nodes.size(); ++n) { CKEY_CHECK(G.base_index[n] < B.nodes.size() && B.nodes[G.base_index[n]].id == G.nodes[n].id); seen[G.base_index[n]] = 1; }
        CKEY_CHECK(std::all_of(seen.begin(), seen.end(), [](uint8_t x) { return x == 1; }));
        // Locality: a byte's eight sites sit together. Kind-pure bytes (all but
        // at most three "mixed" ones) span a few modules; scattered bytes span the field.
        size_t compact = 0, mixed = 0; double scattered_span = 0;
        const auto position = [&](const ScalableLayout &L, size_t n, double &x, double &y) {
            const auto &node = L.nodes[n];
            if (node.kind == 1) { const double a = kTau * node.slot / L.dial_slots, r = kScalableDialInner + L.dial_sample; x = kCenter + r * std::sin(a); y = kCenter - r * std::cos(a); }
            else { x = node.x; y = node.y; }
        };
        for (size_t b = 0; b < G.codeword_bytes; ++b) {
            double span = 0, base_span = 0; bool pure = true;
            for (int i = 0; i < 8; ++i) for (int j = i + 1; j < 8; ++j) {
                double x0, y0, x1, y1; position(G, 8 * b + i, x0, y0); position(G, 8 * b + j, x1, y1); span = std::max(span, std::hypot(x1 - x0, y1 - y0));
                position(B, 8 * b + i, x0, y0); position(B, 8 * b + j, x1, y1); base_span = std::max(base_span, std::hypot(x1 - x0, y1 - y0));
                pure = pure && G.nodes[8 * b + i].kind == G.nodes[8 * b + j].kind;
            }
            if (!pure) ++mixed; else if (span <= 9.0 * G.pitch) ++compact;
            scattered_span += base_span;
        }
        CKEY_CHECK(mixed <= 3);
        if (v == 1 || v == 11 || v == 31) std::printf("grouped family %d v%d: %zu of %zu bytes within 9 modules; scattered mean span %.1f units\n", family, v, compact, G.codeword_bytes, scattered_span / static_cast<double>(G.codeword_bytes));
        CKEY_CHECK(static_cast<double>(compact) >= 0.80 * static_cast<double>(G.codeword_bytes));
        CKEY_CHECK(scattered_span / static_cast<double>(G.codeword_bytes) > 10.0);
        // Regional interleaving: every RS block's bytes reach all four quadrants.
        const auto blocks = scalable_blocks(G.codeword_bytes, CKEY_ECC_M);
        if (blocks.front().n >= 24) {
            std::vector<std::array<int, 4>> quadrant(blocks.size(), std::array<int, 4>{});
            size_t pos = 0;
            for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) {
                double x, y; position(G, 8 * pos, x, y); ++quadrant[b][(x > kCenter ? 1 : 0) + (y > kCenter ? 2 : 0)]; ++pos;
            }
            for (const auto &q : quadrant) for (int k = 0; k < 4; ++k) CKEY_CHECK(q[static_cast<size_t>(k)] * 12 >= static_cast<int>(blocks.front().n));
        }
    }
    // Known-answer hashes (FNV-1a-64 over every site in placement order of
    // identity little-endian and printed value; "CKey renderer test", level M),
    // stated in spec/CKey-Format-v0.29.0.md and v0.30.0.md.
    {
        const std::string text = "CKey renderer test";
        const struct { ckey_scalable_layout layout; uint32_t version; uint64_t expected; } vectors[] = {
            {CKEY_LAYOUT_STANDARD_GROUPED, 2, 0xdd98dc05a83ab143ull}, {CKEY_LAYOUT_STANDARD_GROUPED, 31, 0xb10ece43c83bda6dull},
            {CKEY_LAYOUT_PRINT_GROUPED, 3, 0x06c7aeb60f425ff8ull}, {CKEY_LAYOUT_PRINT_GROUPED, 31, 0x5f8333c52d1f4266ull}};
        for (const auto &vec : vectors) {
            CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text.data()), text.size(), vec.version, CKEY_ECC_M, vec.layout, &s, &d) == CKEY_OK);
            const ScalableLayout &L = scalable_layout(static_cast<int>(vec.version), vec.layout);
            uint64_t hash = 0xcbf29ce484222325ull;
            const auto fnv = [&](uint8_t b) { hash = (hash ^ b) * 0x100000001b3ull; };
            for (size_t i = 0; i < L.nodes.size(); ++i) { for (int k = 0; k < 4; ++k) fnv(static_cast<uint8_t>(L.nodes[i].id >> (8 * k))); fnv(scalable_site_value(L, &s, i) ? 1 : 0); }
            std::printf("layout %d v%u conformance hash %016llx\n", static_cast<int>(vec.layout), vec.version, static_cast<unsigned long long>(hash));
            CKEY_CHECK(hash == vec.expected);
        }
    }
    // Image round trips: the reader identifies the placement by itself.
    for (int layout = 0; layout < 4; ++layout) for (const int v : {1, 4, 11, 20, 31}) {
        const size_t cap = scalable_capacity(v, CKEY_ECC_M, layout); const auto payload = pseudo(cap, static_cast<uint32_t>(v + 100 * layout));
        CKEY_CHECK(ckey_encode_scalable(payload.data(), cap, static_cast<uint32_t>(v), CKEY_ECC_M, static_cast<ckey_scalable_layout>(layout), &s, &d) == CKEY_OK);
        const Raster r = rasterise(s, 6.0);
        uint32_t version = 0, found = 99;
        CKEY_CHECK(ckey_decode_scalable_gray8(r.px.data(), r.side, r.side, r.side, r.quad, out.data(), out.size(), &len, &version, &found, &d) == CKEY_OK);
        CKEY_CHECK(version == static_cast<uint32_t>(v) && found == static_cast<uint32_t>(layout) && len == cap && std::equal(payload.begin(), payload.end(), out.begin()));
    }

    // ---- A blot: grouped placement loses only the bytes under it ----
    {
        const int v = 11; int grouped_ok = 0, scattered_ok = 0; uint32_t erasures = 0;
        for (int layout : {CKEY_LAYOUT_STANDARD, CKEY_LAYOUT_STANDARD_GROUPED}) for (int trial = 0; trial < 6; ++trial) {
            const size_t cap = scalable_capacity(v, CKEY_ECC_M, layout); const auto payload = pseudo(cap, static_cast<uint32_t>(trial + 1));
            CKEY_CHECK(ckey_encode_scalable(payload.data(), cap, v, CKEY_ECC_M, static_cast<ckey_scalable_layout>(layout), &s, &d) == CKEY_OK);
            Raster r = rasterise(s, 8.0);
            // A flat grey disc over about 5% of the field, in the Cartesian area.
            const double unit = r.side / 31.0, radius = 3.7 * unit, cx = (1 + 5.0 + 3.0 * (trial % 3)) * unit, cy = (1 + 23.5 - 1.5 * (trial % 2)) * unit;
            for (int y = 0; y < r.side; ++y) for (int x = 0; x < r.side; ++x) if (std::hypot(x - cx, y - cy) < radius) r.px[static_cast<size_t>(y) * r.side + x] = 128;
            const bool ok = ckey_decode_scalable_gray8(r.px.data(), r.side, r.side, r.side, r.quad, out.data(), out.size(), &len, nullptr, nullptr, &d) == CKEY_OK &&
                            len == cap && std::equal(payload.begin(), payload.end(), out.begin());
            if (layout == CKEY_LAYOUT_STANDARD) scattered_ok += ok; else { grouped_ok += ok; if (ok) erasures += d.erasures_used; }
        }
        std::printf("5%% blot at level M, v11: grouped %d/6 decoded (%u bytes erased), scattered %d/6\n", grouped_ok, erasures, scattered_ok);
        CKEY_CHECK(grouped_ok == 6 && grouped_ok >= scattered_ok);
    }

    // ---- A symbol partly out of frame: the missing sites are erasures ----
    {
        const int v = 8; const size_t cap = scalable_capacity(v, CKEY_ECC_M, CKEY_LAYOUT_STANDARD_GROUPED); const auto payload = pseudo(cap, 77);
        CKEY_CHECK(ckey_encode_scalable(payload.data(), cap, v, CKEY_ECC_M, CKEY_LAYOUT_STANDARD_GROUPED, &s, &d) == CKEY_OK);
        const Raster r = rasterise(s, 8.0);
        // Cut 20% of the canvas off the right-hand side: more bytes than level M
        // corrects as unknown errors (14%), fewer than it corrects as erasures (28%).
        const int width = static_cast<int>(r.side * 0.80);
        std::vector<uint8_t> cut(static_cast<size_t>(width) * r.side);
        for (int y = 0; y < r.side; ++y) std::copy_n(r.px.begin() + static_cast<std::ptrdiff_t>(y) * r.side, width, cut.begin() + static_cast<std::ptrdiff_t>(y) * width);
        CKEY_CHECK(ckey_decode_scalable_gray8(cut.data(), width, r.side, width, r.quad, out.data(), out.size(), &len, nullptr, nullptr, &d) == CKEY_OK);
        CKEY_CHECK(len == cap && std::equal(payload.begin(), payload.end(), out.begin()) && d.erasures_used > 0);
        std::printf("20%% out of frame at level M, v8 grouped: decoded with %u erased bytes\n", d.erasures_used);
    }

    // ---- Frame scan, region of interest, canonical corner order ----
    const std::string url = "https://openckey.org";
    CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(url.data()), url.size(), 0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
    const Raster small = rasterise(s, 9.0);
    {
        const Frame f = place(small, 1080, 1920, 380, 700);
        ckey_scan_options options; ckey_scan_options_init(&options);
        CKEY_CHECK(options.abi_version == CKEY_ABI_VERSION && options.profiles == CKEY_SCAN_PROFILE_ALL);
        ckey_scan_result result{};
        CKEY_CHECK(ckey_scan_frame_gray8(f.px.data(), f.w, f.h, f.w, &options, out.data(), out.size(), &result) == CKEY_OK);
        CKEY_CHECK(result.status == CKEY_OK && result.profile == CKEY_FIELD_SCALABLE && result.version == s.version && result.layout == CKEY_LAYOUT_STANDARD);
        CKEY_CHECK(result.payload_length == url.size() && std::equal(url.begin(), url.end(), out.begin()) && result.located == 1 && result.score > 15);
        // Corners come back in the symbol's own order (top-left first), within a pixel or two.
        for (int i = 0; i < 4; ++i) CKEY_CHECK(std::hypot(result.quad[2 * i] - f.quad[2 * i], result.quad[2 * i + 1] - f.quad[2 * i + 1]) < 2.5);
        // Region of interest: same answer in full-frame coordinates; a region without the symbol finds nothing.
        options.roi_x = 300; options.roi_y = 600; options.roi_width = 500; options.roi_height = 500;
        ckey_scan_result in_region{};
        CKEY_CHECK(ckey_scan_frame_gray8(f.px.data(), f.w, f.h, f.w, &options, out.data(), out.size(), &in_region) == CKEY_OK);
        for (int i = 0; i < 4; ++i) CKEY_CHECK(std::hypot(in_region.quad[2 * i] - f.quad[2 * i], in_region.quad[2 * i + 1] - f.quad[2 * i + 1]) < 2.5);
        options.roi_x = 0; options.roi_y = 0; options.roi_width = 300; options.roi_height = 300;
        ckey_scan_result empty{};
        CKEY_CHECK(ckey_scan_frame_gray8(f.px.data(), f.w, f.h, f.w, &options, out.data(), out.size(), &empty) == CKEY_UNCORRECTABLE && empty.located == 0);
        CKEY_CHECK(ckey_scan_frame_gray8(nullptr, f.w, f.h, f.w, &options, out.data(), out.size(), &empty) == CKEY_INVALID_ARGUMENT);
        // A symbol running off the frame raises the "cut" flag.
        const Frame off = place(small, 1080, 1920, 1080 - small.side * 3 / 4, 700);
        ckey_scan_options whole; ckey_scan_options_init(&whole); ckey_scan_result cut{};
        ckey_scan_frame_gray8(off.px.data(), off.w, off.h, off.w, &whole, out.data(), out.size(), &cut);
        CKEY_CHECK(cut.status == CKEY_OK || (cut.flags & CKEY_SCAN_FLAG_SYMBOL_CUT));

        // ---- Tracking ----
        ckey_track_state track; ckey_track_init(&track);
        CKEY_CHECK(track.status == CKEY_TRACK_NONE && track.gate == 15 && track.lock_score == 150);
        CKEY_CHECK(ckey_track_gray8(f.px.data(), f.w, f.h, f.w, &track) == CKEY_TRACK_LOCKED && track.fresh == 1 && track.score >= 150);
        double previous[8]; std::copy(track.quad, track.quad + 8, previous);
        // The symbol drifts 14 px a frame: corners follow, in the same order.
        for (int step = 1; step <= 6; ++step) {
            const Frame moved = place(small, 1080, 1920, 380 + 14 * step, 700 + 9 * step);
            CKEY_CHECK(ckey_track_gray8(moved.px.data(), moved.w, moved.h, moved.w, &track) == CKEY_TRACK_LOCKED && track.fresh == 1);
            for (int i = 0; i < 4; ++i) {
                CKEY_CHECK(std::abs(track.quad[2 * i] - previous[2 * i] - 14) < 2.0 && std::abs(track.quad[2 * i + 1] - previous[2 * i + 1] - 9) < 2.0);
            }
            std::copy(track.quad, track.quad + 8, previous);
        }
        // Nothing in view: the last corners are held for 8 frames, then dropped.
        const std::vector<uint8_t> blank(static_cast<size_t>(1080) * 1920, 120);
        for (int miss = 1; miss <= 8; ++miss) CKEY_CHECK(ckey_track_gray8(blank.data(), 1080, 1920, 1080, &track) == CKEY_TRACK_LOCKED && track.fresh == 0);
        CKEY_CHECK(ckey_track_gray8(blank.data(), 1080, 1920, 1080, &track) == CKEY_TRACK_NONE);
        // Seeding: a decode locks at once; a weak sighting needs a second one.
        ckey_track_seed(&track, f.quad, 40.0, 0); CKEY_CHECK(track.status == CKEY_TRACK_TENTATIVE);
        ckey_track_seed(&track, f.quad, 40.0, 0); CKEY_CHECK(track.status == CKEY_TRACK_LOCKED);
        ckey_track_init(&track); ckey_track_seed(&track, f.quad, 5.0, 0); CKEY_CHECK(track.status == CKEY_TRACK_NONE);
        ckey_track_seed(&track, f.quad, 5.0, 1); CKEY_CHECK(track.status == CKEY_TRACK_LOCKED && track.decoded == 1);
        // Corner correspondence: any cyclic order or winding maps back.
        double shuffled[8]; for (int i = 0; i < 4; ++i) { const int j = (5 - i) % 4; shuffled[2 * i] = f.quad[2 * j] + 1; shuffled[2 * i + 1] = f.quad[2 * j + 1] - 1; }
        ckey_match_corner_order(f.quad, shuffled);
        for (int i = 0; i < 4; ++i) CKEY_CHECK(std::abs(shuffled[2 * i] - f.quad[2 * i] - 1) < 1e-9 && std::abs(shuffled[2 * i + 1] - f.quad[2 * i + 1] + 1) < 1e-9);

        // ---- Camera advice ----
        ckey_camera_policy policy; ckey_camera_policy_init(&policy); ckey_camera_advice advice{};
        policy.last_point_x = 0.9; policy.last_point_y = 0.1;  // as if the camera was last aimed at a corner
        CKEY_CHECK(ckey_camera_advise_gray8(f.px.data(), f.w, f.h, f.w, f.quad, 0, &policy, &advice) == CKEY_OK);
        CKEY_CHECK(advice.has_candidate == 1 && advice.exposure_bias_ev == 0 && std::abs(advice.point_x - (380 + small.side / 2.0) / 1080) < 0.02 && advice.contrast > 0.5 && advice.sharpness > 0.5);
        CKEY_CHECK((advice.flags & CKEY_ADVICE_REAIM) != 0);  // the metering point moved
        CKEY_CHECK(ckey_camera_advise_gray8(f.px.data(), f.w, f.h, f.w, f.quad, 0, &policy, &advice) == CKEY_OK && !(advice.flags & CKEY_ADVICE_REAIM));
        // Washed out: even the darkest parts are bright. Advice: darker, and say so.
        Frame bright = f; for (auto &p : bright.px) p = static_cast<uint8_t>(std::min(255, 215 + p / 4));
        ckey_camera_policy_init(&policy);
        CKEY_CHECK(ckey_camera_advise_gray8(bright.px.data(), bright.w, bright.h, bright.w, bright.quad, 0, &policy, &advice) == CKEY_OK);
        CKEY_CHECK(advice.exposure_bias_ev < 0 && (advice.flags & CKEY_ADVICE_GLARE));
        // A small symbol far away: zoom in (the SDK applies it only up to real detail).
        const Raster tiny = rasterise(s, 3.0); const Frame far = place(tiny, 1080, 1920, 480, 900);
        ckey_camera_policy_init(&policy);
        CKEY_CHECK(ckey_camera_advise_gray8(far.px.data(), far.w, far.h, far.w, far.quad, 0, &policy, &advice) == CKEY_OK);
        CKEY_CHECK(advice.zoom_factor > 1.5 && (advice.flags & CKEY_ADVICE_MOVE_CLOSER));
        // Cut by the frame edge: move back.
        ckey_camera_policy_init(&policy);
        CKEY_CHECK(ckey_camera_advise_gray8(off.px.data(), off.w, off.h, off.w, off.quad, 0, &policy, &advice) == CKEY_OK && (advice.flags & CKEY_ADVICE_MOVE_BACK) && advice.zoom_factor < 1);
        // Nothing found for a while: the bias walks 0, -1, -2 and resets on a decode.
        ckey_camera_policy_init(&policy); double seen_bias[3] = {1, 1, 1};
        for (int frame = 0; frame < 40; ++frame) {
            CKEY_CHECK(ckey_camera_advise_gray8(blank.data(), 1080, 1920, 1080, nullptr, 0, &policy, &advice) == CKEY_OK && advice.has_candidate == 0);
            if (advice.exposure_bias_ev == 0) seen_bias[0] = 0; else if (advice.exposure_bias_ev == -1) seen_bias[1] = 0; else if (advice.exposure_bias_ev == -2) seen_bias[2] = 0;
        }
        CKEY_CHECK(seen_bias[0] == 0 && seen_bias[1] == 0 && seen_bias[2] == 0);
        CKEY_CHECK(ckey_camera_advise_gray8(f.px.data(), f.w, f.h, f.w, f.quad, 1, &policy, &advice) == CKEY_OK && advice.exposure_bias_ev == 0 && policy.failed_frames == 0);
    }

    // ---- Lens distortion given by the caller ----
    {
        const Frame f = place(small, 1080, 1920, 560, 300);
        // Barrel-distort the frame (Brown k1 on focal-normalised coordinates).
        const double k1 = -0.15, focal = 1200, cx = 540, cy = 960;  // the ultrawide class in the campaign
        std::vector<uint8_t> bent(f.px.size(), 120);
        for (int y = 0; y < f.h; ++y) for (int x = 0; x < f.w; ++x) {
            // Output pixel (distorted) -> source (undistorted) by fixed-point iteration.
            const double xd = (x - cx) / focal, yd = (y - cy) / focal; double u = xd, w = yd;
            for (int it = 0; it < 12; ++it) { const double g = 1 + k1 * (u * u + w * w); u = xd / g; w = yd / g; }
            const double sx = cx + focal * u, sy = cy + focal * w;
            if (sx < 0 || sy < 0 || sx > f.w - 2 || sy > f.h - 2) continue;
            const int ix = static_cast<int>(sx), iy = static_cast<int>(sy); const double ax = sx - ix, ay = sy - iy;
            const auto at = [&](int xx, int yy) { return static_cast<double>(f.px[static_cast<size_t>(yy) * f.w + xx]); };
            bent[static_cast<size_t>(y) * f.w + x] = static_cast<uint8_t>((at(ix, iy) * (1 - ax) + at(ix + 1, iy) * ax) * (1 - ay) + (at(ix, iy + 1) * (1 - ax) + at(ix + 1, iy + 1) * ax) * ay + 0.5);
        }
        ckey_scan_options options; ckey_scan_options_init(&options); options.k1 = k1; options.focal_px = focal;
        ckey_scan_result result{};
        CKEY_CHECK(ckey_scan_frame_gray8(bent.data(), f.w, f.h, f.w, &options, out.data(), out.size(), &result) == CKEY_OK);
        CKEY_CHECK(result.payload_length == url.size() && std::equal(url.begin(), url.end(), out.begin()));
    }

    // ---- A symbol on a bent surface (a label on a bottle, a curled page) ----
    {
        // The raster wrapped on a cylinder and photographed at a tilt by a
        // pinhole camera. Built from the 3D geometry, independently of the
        // reader's own surface model. Lengths are in raster pixels.
        const int W = 1080, H = 1920; const double focal = 1250, cx = W / 2.0, cy = H / 2.0;
        const auto render_bent = [&](const Raster &raster, double radius_sides, double axis_degrees, double tilt_degrees, std::vector<uint8_t> &image, double quad[8]) {
            const double side = raster.side, radius = radius_sides * side, distance = 2.6 * side;
            const double phi = kTau * axis_degrees / 360.0, tilt = kTau * tilt_degrees / 360.0;
            // Raster point (u, v) -> image pixel.
            const auto project = [&](double u, double v, double &px, double &py) {
                const double x0 = u - side / 2, y0 = v - side / 2;
                const double across = x0 * std::cos(phi) + y0 * std::sin(phi), along = -x0 * std::sin(phi) + y0 * std::cos(phi);
                const double bent = radius * std::sin(across / radius), lift = radius * (1 - std::cos(across / radius));
                const double X = bent * std::cos(phi) - along * std::sin(phi), Y = bent * std::sin(phi) + along * std::cos(phi), Z = -lift;
                // Tilt about the x axis, then stand the camera `distance` away.
                const double Yc = Y * std::cos(tilt) - Z * std::sin(tilt), Zc = Y * std::sin(tilt) + Z * std::cos(tilt) + distance;
                px = cx + focal * X / Zc; py = cy + focal * Yc / Zc;
            };
            image.assign(static_cast<size_t>(W) * H, 120);
            // Image pixel -> raster point by Newton's method, warm-started along each row.
            for (int y = 0; y < H; ++y) {
                double u = side / 2, v = side / 2; bool warm = false;
                for (int x = 0; x < W; ++x) {
                    if (!warm) { u = side / 2 + (x - cx) * distance / focal; v = side / 2 + (y - cy) * distance / focal; }
                    bool ok = false;
                    for (int it = 0; it < 12; ++it) {
                        double px, py, pxu, pyu, pxv, pyv; project(u, v, px, py); project(u + 0.5, v, pxu, pyu); project(u, v + 0.5, pxv, pyv);
                        const double a = (pxu - px) * 2, b = (pxv - px) * 2, c = (pyu - py) * 2, e = (pyv - py) * 2, det = a * e - b * c;
                        if (std::abs(det) < 1e-9) break;
                        const double rx = x - px, ry = y - py; u += (rx * e - b * ry) / det; v += (a * ry - rx * c) / det;
                        if (std::abs(rx) + std::abs(ry) < 0.01) { ok = true; break; }
                    }
                    warm = ok && std::abs(u - side / 2) < side && std::abs(v - side / 2) < side;
                    if (!ok || u < 0 || v < 0 || u > side - 2 || v > side - 2) continue;
                    const int iu = static_cast<int>(u), iv = static_cast<int>(v); const double au = u - iu, av = v - iv;
                    const auto at = [&](int xx, int yy) { return static_cast<double>(raster.px[static_cast<size_t>(yy) * raster.side + xx]); };
                    image[static_cast<size_t>(y) * W + x] = static_cast<uint8_t>((at(iu, iv) * (1 - au) + at(iu + 1, iv) * au) * (1 - av) + (at(iu, iv + 1) * (1 - au) + at(iu + 1, iv + 1) * au) * av + 0.5);
                }
            }
            for (int i = 0; i < 4; ++i) project(raster.quad[2 * i], raster.quad[2 * i + 1], quad[2 * i], quad[2 * i + 1]);
        };
        // A longer payload gives a mid-size symbol (more modules, finer pitch).
        const std::vector<uint8_t> longer = pseudo(180, 31); ckey_scalable_symbol mid{};
        CKEY_CHECK(ckey_encode_scalable(longer.data(), longer.size(), 0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &mid, &d) == CKEY_OK && mid.version >= 6);
        const Raster medium = rasterise(mid, 5.0);
        struct Bend { const Raster *raster; const ckey_scalable_symbol *symbol; const uint8_t *payload; size_t length; double radius_sides, axis, tilt; };
        const uint8_t *const url_bytes = reinterpret_cast<const uint8_t *>(url.data());
        for (const Bend &bend : {Bend{&small, &s, url_bytes, url.size(), 1.2, 80.0, 35.0}, Bend{&small, &s, url_bytes, url.size(), -1.6, 20.0, -40.0},
                                 Bend{&medium, &mid, longer.data(), longer.size(), 1.1, 150.0, 30.0}, Bend{&medium, &mid, longer.data(), longer.size(), -2.0, 60.0, 40.0}}) {
            std::vector<uint8_t> image; double quad[8];
            render_bent(*bend.raster, bend.radius_sides, bend.axis, bend.tilt, image, quad);
            std::printf("bent surface: v%u, radius %.1f sides, axis %.0f deg, tilt %.0f deg\n", bend.symbol->version, bend.radius_sides, bend.axis, bend.tilt);
            // The flat map cannot read it: the bend moves the middle of the symbol by more than a module.
            {
                const SurfaceModel flat{}; const SurfaceScope scope(flat); const LensScope no_lens(LensModel{});
                constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29}; double h[8]; CKEY_CHECK(solve_homography(canonical, quad, h));
                CKEY_CHECK(scalable_version_fit(scalable_layout(static_cast<int>(bend.symbol->version), 0), image.data(), W, H, W, h).evidence < 0.85);
            }
            // Given the true corners.
            len = 0; uint32_t version = 0;
            CKEY_CHECK(ckey_decode_scalable_gray8(image.data(), W, H, W, quad, out.data(), out.size(), &len, &version, nullptr, &d) == CKEY_OK);
            CKEY_CHECK(len == bend.length && std::equal(bend.payload, bend.payload + bend.length, out.begin()) && version == bend.symbol->version);
            // And found from the frame alone.
            ckey_scan_options options; ckey_scan_options_init(&options); ckey_scan_result result{};
            CKEY_CHECK(ckey_scan_frame_gray8(image.data(), W, H, W, &options, out.data(), out.size(), &result) == CKEY_OK);
            CKEY_CHECK(result.payload_length == bend.length && std::equal(bend.payload, bend.payload + bend.length, out.begin()));
        }
        // A flat symbol is not taken for a bent one.
        {
            const Frame f = place(small, W, H, 380, 700);
            const LensScope no_lens(LensModel{});
            CKEY_CHECK(surface_from_marks(scalable_layout(static_cast<int>(s.version), 0), f.px.data(), f.w, f.h, f.w, f.quad).kappa == 0);
            int versions[2]; CKEY_CHECK(versions_from_lattice(f.px.data(), f.w, f.h, f.w, f.quad, versions, 2) >= 1 && versions[0] == static_cast<int>(s.version));
        }
    }

    // ---- Work budget: noise cannot keep a call running ----
    {
        Rng rng{99}; std::vector<uint8_t> noise(static_cast<size_t>(1080) * 1920);
        for (auto &p : noise) p = static_cast<uint8_t>(rng.next());
        // Coarse blocks so the border search finds plenty of outlines to chase.
        for (int y = 0; y < 1920; ++y) for (int x = 0; x < 1080; ++x) if (((x / 37) + (y / 41)) % 3 == 0) noise[static_cast<size_t>(y) * 1080 + x] /= 4;
        const auto start = std::chrono::steady_clock::now();
        ckey_scan_options options; ckey_scan_options_init(&options); ckey_scan_result result{};
        const ckey_status status = ckey_scan_frame_gray8(noise.data(), 1080, 1920, 1080, &options, out.data(), out.size(), &result);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("noise frame: status %d, %u candidates, %.2f s\n", static_cast<int>(status), result.candidates, seconds);
        CKEY_CHECK(status != CKEY_OK && seconds < 20.0);
        // An exhausted budget ends a decode with "no read".
        {
            const WorkScope tiny_budget(1000);
            const Raster r = rasterise(s, 6.0);
            CKEY_CHECK(ckey_decode_scalable_gray8(r.px.data(), r.side, r.side, r.side, r.quad, out.data(), out.size(), &len, nullptr, nullptr, &d) == CKEY_UNCORRECTABLE);
        }
        const Raster r = rasterise(s, 6.0);
        CKEY_CHECK(ckey_decode_scalable_gray8(r.px.data(), r.side, r.side, r.side, r.quad, out.data(), out.size(), &len, nullptr, nullptr, &d) == CKEY_OK);
    }

    std::printf("CKey hardening tests passed\n");
    return 0;
}
