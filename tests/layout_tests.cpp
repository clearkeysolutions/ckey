// White-box layout checks for the dense profiles. This file includes the
// core translation unit so it can inspect site geometry directly; it is
// built as its own executable and does not link ckey_core.
#include "../src/ckey.cpp"
#include "check.h"
#include "svg_raster.h"

#include <cstdio>

namespace {

// Filled from the reference implementation; see the format specifications.
constexpr uint64_t kTextLayoutKat = 0x17a92d978ee7b3cfull, kExtendedLayoutKat = 0x762d03b44db9c502ull;

std::string render_text(const std::vector<uint8_t> &payload) {
    ckey_text_symbol s{}; ckey_diagnostics d{}; size_t n = 0;
    CKEY_CHECK(ckey_encode_text(payload.data(), payload.size(), &s, &d) == CKEY_OK);
    ckey_render_text_svg(&s, 8, nullptr, 0, &n); std::string svg(n, '\0');
    CKEY_CHECK(ckey_render_text_svg(&s, 8, svg.data(), n, &n) == CKEY_OK); svg.resize(n - 1); return svg;
}

std::string render_extended(const std::vector<uint8_t> &payload) {
    ckey_extended_symbol s{}; ckey_diagnostics d{}; size_t n = 0;
    CKEY_CHECK(ckey_encode_extended(payload.data(), payload.size(), &s, &d) == CKEY_OK);
    ckey_render_extended_svg(&s, 8, nullptr, 0, &n); std::string svg(n, '\0');
    CKEY_CHECK(ckey_render_extended_svg(&s, 8, svg.data(), n, &n) == CKEY_OK); svg.resize(n - 1); return svg;
}

std::vector<uint8_t> pseudo_random(size_t n, uint32_t seed) {
    std::vector<uint8_t> out(n);
    for (auto &b : out) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(seed >> 24); }
    return out;
}

// Every candidate lattice site must be decided with slack: no implementation
// whose sqrt/sin/cos differ in the last place may derive another site set.
void check_margin_stability(const DenseGeometry &g, int grid, double pitch, double documented, const char *label) {
    double closest = 1e9;
    for (int row = 0; row < grid; ++row) for (int col = 0; col < grid; ++col)
        closest = std::min(closest, std::abs(dense_site_margin(g, (col + .5) * pitch, (row + .5) * pitch)));
    std::printf("%s: closest site-to-clearance slack %.6f units\n", label, closest);
    CKEY_CHECK(closest > 1e-6 && closest >= documented);  // the spec states this bound
}

struct Clearances { double frame, finder, grid; double inner_radius, center; int px_per_unit, units, margin; DenseGeometry g; };

// Measures, on the rendered SVG, (a) the minimum gap between payload ink and
// the frame, finders, and Grid Key, and (b) the largest distance from any
// point of the payload field to the nearest payload ink ("dead space").
void check_rendered_layout(const std::string &svg_text, const Clearances &c, double dead_limit, const char *label) {
    const auto svg = svgtest::parse(svg_text);
    const int side = c.units * c.px_per_unit; const double unit = c.px_per_unit;
    const auto mask = [&](const char *layer) {
        auto px = svgtest::rasterize(svg, side, side, 1, 0, 1, layer); return px;
    };
    const auto data = mask("ckey-data");
    const auto frame = svgtest::distance_to_set(mask("ckey-frame"), side, side);
    const auto finders = svgtest::distance_to_set(mask("ckey-finders"), side, side);
    const auto grid = svgtest::distance_to_set(mask("ckey-grid-key"), side, side);
    const auto to_data = svgtest::distance_to_set(data, side, side);
    const auto cartesian = mask("ckey-data-cartesian");
    const auto polar_keys = svgtest::distance_to_set(mask("ckey-polar-keys"), side, side);
    const auto polar_data = svgtest::distance_to_set(mask("ckey-data-polar"), side, side);
    double gap_frame = 1e9, gap_finder = 1e9, gap_grid = 1e9, gap_polar = 1e9, dead = 0, dead_x = 0, dead_y = 0;
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        const size_t i = static_cast<size_t>(y) * side + x;
        if (cartesian[i]) gap_polar = std::min(gap_polar, std::min(polar_keys[i], polar_data[i]) / unit);
        if (data[i]) {
            gap_frame = std::min(gap_frame, frame[i] / unit);
            gap_finder = std::min(gap_finder, finders[i] / unit);
            gap_grid = std::min(gap_grid, grid[i] / unit);
            continue;
        }
        // Payload field: inside the frame, outside every declared clearance
        // and the central quiet zone.
        const double ux = (x + .5) / unit - c.margin, uy = (y + .5) / unit - c.margin;
        const double r = std::hypot(ux - c.center, uy - c.center);
        if (rounded_square_sd(ux, uy, c.g.frame_center, c.g.frame_half, c.g.frame_radius) > 0 || r < c.inner_radius) continue;
        if (frame[i] / unit < c.frame + 0.5 || finders[i] / unit < c.finder + 0.5 || grid[i] / unit < c.grid + 0.5) continue;
        // The top-left finder's split gap is the orientation mark: structural
        // clear space, measured against the complete arc.
        if (finder_centerline_distance(ux, uy, c.g.finder_inset, c.g.finder_inset, c.g.finder_radius, false, false) <
            c.g.finder_half_stroke + c.finder + 0.5) continue;
        if (to_data[i] / unit > dead) { dead = to_data[i] / unit; dead_x = ux; dead_y = uy; }
    }
    const double tolerance = 1.5 / unit;  // point-sampled mask quantization
    std::printf("%s: gaps frame %.3f finders %.3f grid %.3f radial %.3f; largest payload-free distance %.3f units at (%.1f, %.1f)\n",
                label, gap_frame, gap_finder, gap_grid, gap_polar, dead, dead_x, dead_y);
    CKEY_CHECK(gap_frame >= c.frame - tolerance);
    CKEY_CHECK(gap_finder >= c.finder - tolerance);
    CKEY_CHECK(gap_grid >= c.grid - tolerance);
    CKEY_CHECK(gap_polar >= 0.5 - tolerance);  // Cartesian ink vs Ray Keys and the radial alphabet
    CKEY_CHECK(dead <= dead_limit);
}

// The codeword must be spread over the symbol, not packed into one region:
// each of the four quadrants (about the center) carries a near-proportional
// share of codeword-bearing Cartesian sites.
template <class NodeT, class IsCart>
void check_spread(const std::vector<NodeT> &nodes, size_t bits, double center, IsCart is_cart, const char *label) {
    std::array<size_t, 4> all{}, data{};
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!is_cart(nodes[i])) continue;
        const int q = (nodes[i].x >= center ? 1 : 0) + (nodes[i].y >= center ? 2 : 0);
        ++all[q]; if (i < bits) ++data[q];
    }
    for (int q = 0; q < 4; ++q) {
        const double share = static_cast<double>(data[q]) / all[q];
        std::printf("%s quadrant %d: %zu of %zu Cartesian sites carry codeword bits (%.2f)\n", label, q, data[q], all[q], share);
    }
    const double expected = static_cast<double>(data[0] + data[1] + data[2] + data[3]) / (all[0] + all[1] + all[2] + all[3]);
    for (int q = 0; q < 4; ++q) CKEY_CHECK(std::abs(static_cast<double>(data[q]) / all[q] - expected) < 0.05);
}

}  // namespace

int main() {
    // ---- Site sets (normative counts for Format 0.25 / 0.26) ----
    const auto &text = text_nodes();
    const auto &extended = extended_nodes();
    size_t text_cart = 0, ext_cart = 0, ext_ring = 0, ext_ray = 0;
    for (const auto &n : text) text_cart += n.cart;
    for (const auto &n : extended) { ext_cart += n.kind == ExtendedKind::cart; ext_ring += n.kind == ExtendedKind::ring; ext_ray += n.kind == ExtendedKind::ray; }
    std::printf("Profile 2 sites: %zu total (%zu Cartesian), codeword bits %zu\n", text.size(), text_cart, kTextCodewordBytes * 8);
    std::printf("Profile 3 sites: %zu total (%zu Cartesian, %zu ring, %zu ray components), codeword bits %zu\n",
                extended.size(), ext_cart, ext_ring, ext_ray, kExtendedBits);
    CKEY_CHECK(text.size() == kTextSiteCount && text_cart == kTextCartesianSites);
    CKEY_CHECK(extended.size() == kExtendedSiteCount && ext_cart == kExtendedCartesianSites);
    CKEY_CHECK(ext_ray == 573 * 3 && ext_ring == 9682);
    CKEY_CHECK(text_whitening().size() == text.size() && extended_whitening().size() == extended.size());
    check_margin_stability(text_geometry(), kTextGrid, 0.5, 1.8e-2, "Profile 2");
    check_margin_stability(extended_geometry(), kExtendedGrid, 0.5, 3e-4, "Profile 3");
    check_spread(text, kTextCodewordBytes * 8, kCenter, [](const TextNode &n) { return n.cart; }, "Profile 2");
    check_spread(extended, kExtendedBits, 58.0, [](const ExtendedNode &n) { return n.kind == ExtendedKind::cart; }, "Profile 3");

    // Each site kind carries a proportional share of the codeword. (Before
    // Format 0.26 only 173 of 573 rays carried data and the rest were blank
    // stubs; Format 0.25/0.26 fill sites are whitened, so none look empty.)
    {
        std::array<size_t, 3> all{}, data{};
        for (size_t i = 0; i < extended.size(); ++i) { const auto k = static_cast<size_t>(extended[i].kind); ++all[k]; if (i < kExtendedBits) ++data[k]; }
        const double expected = static_cast<double>(kExtendedBits) / extended.size();
        const char *names[3] = {"Cartesian", "ring", "ray"};
        for (size_t k = 0; k < 3; ++k) {
            const double share = static_cast<double>(data[k]) / all[k];
            std::printf("Profile 3 %s sites carrying codeword bits: %.3f (overall %.3f)\n", names[k], share, expected);
            CKEY_CHECK(std::abs(share - expected) < 0.03);
        }
        std::array<size_t, 3> text_all{}, text_data{};
        for (size_t i = 0; i < text.size(); ++i) { const size_t k = text[i].cart ? 0 : text[i].dial ? 1 : 2; ++text_all[k]; if (i < kTextCodewordBytes * 8) ++text_data[k]; }
        const double text_expected = static_cast<double>(kTextCodewordBytes * 8) / text.size();
        for (size_t k = 0; k < 3; ++k) CKEY_CHECK(std::abs(static_cast<double>(text_data[k]) / text_all[k] - text_expected) < 0.06);
    }

    // ---- Rendered safe area and dead space ----
    const Clearances p2{0.5, 0.5, 0.5, 3.4, 14.5, 40, 31, 1, text_geometry()};
    const Clearances p3{1.5, 1.5, 0.5, 7.0, 58.0, 16, 124, 4, extended_geometry()};
    const std::vector<std::pair<const char *, std::vector<uint8_t>>> p2_payloads = {
        {"P2 random", pseudo_random(200, 1)}, {"P2 all-zero", std::vector<uint8_t>(200, 0)},
        {"P2 all-0xFF", std::vector<uint8_t>(200, 0xff)}, {"P2 short", {'C', 'K'}}};
    for (const auto &[label, payload] : p2_payloads) check_rendered_layout(render_text(payload), p2, 1.15, label);
    const std::vector<std::pair<const char *, std::vector<uint8_t>>> p3_payloads = {
        {"P3 random", pseudo_random(2048, 2)}, {"P3 all-zero", std::vector<uint8_t>(2048, 0)},
        {"P3 all-0xFF", std::vector<uint8_t>(2048, 0xff)}, {"P3 short", {'C', 'K'}}};
    for (const auto &[label, payload] : p3_payloads) check_rendered_layout(render_extended(payload), p3, 1.4, label);

    // ---- Known-answer vectors for the optical layout (integer-only, so
    // platform independent): FNV-1a-64 over every site in placement order of
    // its 32-bit identity (little-endian) followed by its printed value. The
    // payload is the 18 ASCII bytes "CKey renderer test". Values are also
    // stated in the format specifications. ----
    {
        const std::string text_payload = "CKey renderer test";
        const std::vector<uint8_t> bytes(text_payload.begin(), text_payload.end());
        ckey_text_symbol ts{}; ckey_extended_symbol es{}; ckey_diagnostics d{};
        CKEY_CHECK(ckey_encode_text(bytes.data(), bytes.size(), &ts, &d) == CKEY_OK);
        CKEY_CHECK(ckey_encode_extended(bytes.data(), bytes.size(), &es, &d) == CKEY_OK);
        const auto fnv = [](uint64_t h, uint8_t b) { return (h ^ b) * 0x100000001b3ull; };
        uint64_t text_hash = 0xcbf29ce484222325ull, extended_hash = 0xcbf29ce484222325ull;
        for (size_t i = 0; i < text.size(); ++i) {
            for (int k = 0; k < 4; ++k) text_hash = fnv(text_hash, static_cast<uint8_t>(text[i].id >> (8 * k)));
            text_hash = fnv(text_hash, text_site_value(&ts, i) ? 1 : 0);
        }
        for (size_t i = 0; i < extended.size(); ++i) {
            for (int k = 0; k < 4; ++k) extended_hash = fnv(extended_hash, static_cast<uint8_t>(extended[i].id >> (8 * k)));
            extended_hash = fnv(extended_hash, extended_site_value(&es, i) ? 1 : 0);
        }
        std::printf("Layout KAT: Profile 2 %016llx, Profile 3 %016llx; first P2 site ids %08x %08x %08x\n",
                    static_cast<unsigned long long>(text_hash), static_cast<unsigned long long>(extended_hash), text[0].id, text[1].id, text[2].id);
        CKEY_CHECK(text_hash == kTextLayoutKat && extended_hash == kExtendedLayoutKat);
    }

    // ---- Ring runs over half a turn need the SVG large-arc flag ----
    {
        std::ostringstream o; o << std::fixed << std::setprecision(4);
        o << "<svg viewBox=\"0 0 29 29\"><g fill=\"#000000\" stroke=\"#000000\">";
        svg_ring_run(o, 14.5, 5.0, 0.5, 100.5, 144.0, 0.1, "0.2000");  // 250 degrees clockwise from 12:00
        o << "</g></svg>";
        const auto svg = svgtest::parse(o.str());
        CKEY_CHECK(svg.prims.size() == 1);
        const auto on_ring = [&](double degrees) {
            const double a = degrees * kTau / 360.0;
            return svgtest::prim_hit(svg.prims[0], 14.5 + 5.0 * std::sin(a), 14.5 - 5.0 * std::cos(a));
        };
        CKEY_CHECK(on_ring(10) && on_ring(125) && on_ring(240));
        CKEY_CHECK(!on_ring(300) && !on_ring(330));
    }

    // ---- A corrupt Profile 2 length field is an integrity failure ----
    {
        std::vector<uint8_t> cw(kTextCodewordBytes, 0);
        cw[0] = 0xc7; cw[1] = 0x4b; cw[2] = CKEY_TEXT_FORMAT_VERSION; cw[3] = 2; cw[4] = 0x01; cw[5] = 0x00;  // 256 > 200
        rs_encode_generic(cw, kTextDataBytes, kTextParityBytes);
        std::array<uint8_t, 256> out{}; size_t len = 0; ckey_diagnostics d{};
        CKEY_CHECK(ckey_decode_text(cw.data(), cw.size(), out.data(), out.size(), &len, &d) == CKEY_INTEGRITY_FAILURE);
    }
    std::printf("CKey layout tests passed\n");
}
