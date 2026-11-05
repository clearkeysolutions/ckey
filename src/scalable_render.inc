// Profile 0 geometry engine and its native backends: direct raster, PNG and
// PDF (specification §2). Included by ckey.cpp after the Profile 0 layout and
// the style checker; it uses their internal definitions.
//
// The scene builder is the canonical geometry. ckey_render_scalable_styled_svg
// emits the same shapes as SVG text, and tests/render_tests.cpp checks that
// rasterizing that SVG and rasterizing this scene give the same pixels.

namespace {

ckey_shape make_shape(ckey_shape_kind kind, ckey_layer layer, std::initializer_list<double> p, double width = 0,
                      bool round_cap = true, int32_t paint = CKEY_PAINT_INK) {
    ckey_shape s{};
    s.kind = kind; s.layer = layer; s.paint = paint; s.round_cap = round_cap ? 1u : 0u; s.width = width;
    size_t i = 0; for (double v : p) if (i < 5) s.p[i++] = v;
    return s;
}

// A payload or Grid Key module in its cell (same sizes as emit_module).
ckey_shape module_shape(double cx, double cy, double p, ckey_module_shape shape, ckey_layer layer, int32_t paint) {
    const double h = 0.34 * p;
    switch (shape) {
    case CKEY_MODULE_CIRCLE: return make_shape(CKEY_SHAPE_CIRCLE, layer, {cx, cy, h}, 0, true, paint);
    case CKEY_MODULE_DIAMOND: return make_shape(CKEY_SHAPE_DIAMOND, layer, {cx, cy, 0.40 * p}, 0, true, paint);
    default: return make_shape(CKEY_SHAPE_RECT, layer, {cx - h, cy - h, 2 * h, 2 * h, shape == CKEY_MODULE_SQUARE ? 0.0 : 0.16 * p}, 0, true, paint);
    }
}

// A radial stroke at clock angle a (0 = 12:00, clockwise) from radius r0 to r1.
ckey_shape ray_shape(double a, double r0, double r1, double width, bool round_cap, ckey_layer layer) {
    return make_shape(CKEY_SHAPE_LINE, layer, {kCenter + r0 * std::sin(a), kCenter - r0 * std::cos(a), kCenter + r1 * std::sin(a), kCenter - r1 * std::cos(a)},
                      width, round_cap, CKEY_PAINT_INK);
}

// A run of ring sites, as svg_ring_run: slot positions from..to on a ring of
// `slots`, the centerline shortened by one dot radius at each end for the caps.
ckey_shape ring_run_shape(double radius, double from, double to, double slots, double dot, ckey_layer layer, int32_t paint) {
    const double cap = dot / radius;
    const double a0 = kTau * from / slots + cap, a1 = kTau * to / slots - cap;
    return make_shape(CKEY_SHAPE_ARC, layer, {kCenter, kCenter, radius, a0 - kTau / 4, a1 - a0}, 2 * dot, true, paint);
}

std::vector<ckey_shape> build_scalable_scene(const ScalableLayout &L, const ckey_scalable_symbol &sym, const ckey_style &st) {
    std::vector<ckey_shape> out;
    const double p = L.pitch, dot = L.dot;
    const bool round = st.rays != CKEY_RAY_FLAT;
    out.reserve(L.nodes.size() + 64);
    out.push_back(make_shape(CKEY_SHAPE_RECT_OUTLINE, CKEY_LAYER_FRAME, {0.5, 0.5, 28, 28, 3.5}, 0.65));
    // Finders: one round-capped quarter arc per corner (radius 2.3 about the
    // point 4 units in from each corner, 0.8 wide); the top-left arc is split
    // by a 0.95-unit visible gap, as svg_corner_finders.
    {
        const double inset = 4.0, radius = 2.30, thickness = 0.80, gap = 0.95, side = 29.0;
        const double half_gap = (gap + thickness) / (2.0 * radius), middle = 5.0 * kTau / 8.0;
        const double a1 = middle - half_gap, a2 = middle + half_gap;
        out.push_back(make_shape(CKEY_SHAPE_ARC, CKEY_LAYER_FINDERS, {inset, inset, radius, kTau / 2, a1 - kTau / 2}, thickness));
        out.push_back(make_shape(CKEY_SHAPE_ARC, CKEY_LAYER_FINDERS, {inset, inset, radius, a2, 3 * kTau / 4 - a2}, thickness));
        // Mirror images of the unsplit quarter arc (start pi, sweep +pi/2).
        out.push_back(make_shape(CKEY_SHAPE_ARC, CKEY_LAYER_FINDERS, {side - inset, inset, radius, 0, -kTau / 4}, thickness));
        out.push_back(make_shape(CKEY_SHAPE_ARC, CKEY_LAYER_FINDERS, {side - inset, side - inset, radius, 0, kTau / 4}, thickness));
        out.push_back(make_shape(CKEY_SHAPE_ARC, CKEY_LAYER_FINDERS, {inset, side - inset, radius, kTau / 2, -kTau / 4}, thickness));
    }
    for (int r = 0; r < 2; ++r) for (int c = 0; c < 2; ++c)
        out.push_back(module_shape((L.col0 + c + .5) * p, (L.row0 + r + .5) * p, p, st.modules, CKEY_LAYER_GRID_KEY, CKEY_PAINT_INK));
    for (const auto &[slot, fraction] : {std::pair{0, 1.0}, std::pair{L.ray_key_half(), 0.5}, std::pair{L.ray_key_three(), 0.75}})
        out.push_back(ray_shape(kTau * slot / L.dial_slots, kScalableDialInner, kScalableDialInner + L.key_ray() * fraction, 0.44 * p, round, CKEY_LAYER_POLAR_KEYS));
    const double rk = L.ring_radius(L.rings - 1); const int k = L.ring_key_slot(); const double SK = L.ring_slots.back();
    for (int slot : L.ring_key_dots()) {
        const double a = kTau * (k + slot) / SK;
        out.push_back(make_shape(CKEY_SHAPE_CIRCLE, CKEY_LAYER_POLAR_KEYS, {kCenter + rk * std::sin(a), kCenter - rk * std::cos(a), dot}));
    }
    out.push_back(ring_run_shape(rk, k + L.ring_key_run().first, k + L.ring_key_run().second, SK, dot, CKEY_LAYER_POLAR_KEYS, CKEY_PAINT_INK));

    const size_t stride = static_cast<size_t>(L.ring_stride);
    std::vector<uint8_t> ring_bits(static_cast<size_t>(L.rings) * stride, 0);
    std::vector<int> ring_accent(static_cast<size_t>(L.rings) * stride, -1);
    std::vector<ckey_shape> cartesian;
    for (size_t i = 0; i < L.nodes.size(); ++i) {
        const auto &n = L.nodes[i]; const bool bit = scalable_site_value(L, &sym, i);
        const int accent = accent_of(n.id, st);
        if (n.kind == 0) { if (bit) cartesian.push_back(module_shape(n.x, n.y, p, st.modules, CKEY_LAYER_DATA_CARTESIAN, accent >= 0 ? accent : CKEY_PAINT_INK)); }
        else if (n.kind == 1) out.push_back(ray_shape(kTau * n.slot / L.dial_slots, kScalableDialInner, kScalableDialInner + (bit ? L.long_ray() : L.short_ray()), L.ray_width, round, CKEY_LAYER_DATA_POLAR));
        else { ring_bits[static_cast<size_t>(n.ring) * stride + n.slot] = bit; ring_accent[static_cast<size_t>(n.ring) * stride + n.slot] = accent; }
    }
    for (int ring = 0; ring < L.rings; ++ring) {
        const double radius = L.ring_radius(ring); const int S = L.ring_slots[static_cast<size_t>(ring)];
        for (int slot = 0; slot < S;) {
            const size_t at = static_cast<size_t>(ring) * stride + slot;
            if (!ring_bits[at]) { ++slot; continue; }
            int end = slot + 1;
            if (st.rings == CKEY_RING_JOINED) while (end < S && ring_bits[static_cast<size_t>(ring) * stride + end]) ++end;
            const int32_t paint = ring_accent[at] >= 0 ? ring_accent[at] : CKEY_PAINT_INK;
            if (end - slot == 1) {
                const double a = kTau * slot / S;
                out.push_back(make_shape(CKEY_SHAPE_CIRCLE, CKEY_LAYER_DATA_POLAR, {kCenter + radius * std::sin(a), kCenter - radius * std::cos(a), dot}, 0, true, paint));
            } else out.push_back(ring_run_shape(radius, slot - .5, end - .5, S, dot, CKEY_LAYER_DATA_POLAR, paint));
            slot = end;
        }
    }
    out.insert(out.end(), cartesian.begin(), cartesian.end());
    if (st.logo_href && *st.logo_href) {
        const double r = st.logo_radius > 0 ? st.logo_radius : logo_quiet_radius(L);
        if (st.logo_plate) out.push_back(make_shape(CKEY_SHAPE_CIRCLE, CKEY_LAYER_LOGO_PLATE, {kCenter, kCenter, r + 0.1}, 0, true, CKEY_PAINT_BACKGROUND));
        out.push_back(make_shape(CKEY_SHAPE_LOGO, CKEY_LAYER_LOGO, {kCenter, kCenter, r}, 0, true, CKEY_PAINT_INK));
    }
    return out;
}

// Validates arguments shared by every scene consumer and returns the layout.
const ScalableLayout *scene_layout(const ckey_scalable_symbol *s, const ckey_style *style, ckey_style &st) {
    if (!s || s->abi_version != CKEY_ABI_VERSION || s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT ||
        s->ecc_level > 3 || s->layout >= static_cast<uint32_t>(kScalableFamilies)) return nullptr;
    const ScalableLayout &L = scalable_layout(static_cast<int>(s->version), static_cast<int>(s->layout));
    if (s->codeword_length != L.codeword_bytes) return nullptr;
    if (style) st = *style; else ckey_style_default(&st);
    double contrast = 0, load = 0;
    if (!check_style(L, *s, st, contrast, load).empty()) return nullptr;
    return &L;
}

// ---- Coverage tests, in canonical units (same rules as src/svg_raster.h) ----

double sdf_round_rect(double px, double py, double x, double y, double w, double h, double r) {
    const double cx = x + w / 2, cy = y + h / 2;
    const double qx = std::abs(px - cx) - (w / 2 - r), qy = std::abs(py - cy) - (h / 2 - r);
    return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r;
}

double arc_angle_offset(double a, double from, double sweep) {
    double d = a - from;
    if (sweep >= 0) { while (d < 0) d += kTau; while (d >= kTau) d -= kTau; }
    else { while (d > 0) d -= kTau; while (d <= -kTau) d += kTau; }
    return d;
}

struct PreparedShape {
    ckey_shape s;
    double hw = 0;                         // half stroke width
    double ex0 = 0, ey0 = 0, ex1 = 0, ey1 = 0;  // arc endpoints
    double bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;  // bounding box
};

PreparedShape prepare(const ckey_shape &shape, double min_feature) {
    PreparedShape q; q.s = shape; ckey_shape &s = q.s;
    if (min_feature > 0) {  // HYBRID: widen marks thinner than the minimum feature
        switch (s.kind) {
        case CKEY_SHAPE_CIRCLE: if (s.layer != CKEY_LAYER_LOGO_PLATE) s.p[2] = std::max(s.p[2], min_feature / 2); break;
        case CKEY_SHAPE_DIAMOND: s.p[2] = std::max(s.p[2], min_feature * 0.7071067811865476); break;
        case CKEY_SHAPE_RECT: {
            if (s.p[2] < min_feature) { s.p[0] -= (min_feature - s.p[2]) / 2; s.p[2] = min_feature; }
            if (s.p[3] < min_feature) { s.p[1] -= (min_feature - s.p[3]) / 2; s.p[3] = min_feature; }
            break;
        }
        case CKEY_SHAPE_RECT_OUTLINE: case CKEY_SHAPE_LINE: case CKEY_SHAPE_ARC: s.width = std::max(s.width, min_feature); break;
        default: break;
        }
    }
    q.hw = s.width / 2;
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    const auto add = [&](double x, double y, double pad) { x0 = std::min(x0, x - pad); y0 = std::min(y0, y - pad); x1 = std::max(x1, x + pad); y1 = std::max(y1, y + pad); };
    switch (s.kind) {
    case CKEY_SHAPE_RECT: add(s.p[0], s.p[1], 0); add(s.p[0] + s.p[2], s.p[1] + s.p[3], 0); break;
    case CKEY_SHAPE_RECT_OUTLINE: add(s.p[0], s.p[1], q.hw); add(s.p[0] + s.p[2], s.p[1] + s.p[3], q.hw); break;
    case CKEY_SHAPE_CIRCLE: case CKEY_SHAPE_DIAMOND: case CKEY_SHAPE_LOGO: add(s.p[0], s.p[1], s.p[2]); break;
    case CKEY_SHAPE_LINE: add(s.p[0], s.p[1], q.hw); add(s.p[2], s.p[3], q.hw); break;
    case CKEY_SHAPE_ARC: {
        const double cx = s.p[0], cy = s.p[1], r = s.p[2], a = s.p[3], sw = s.p[4];
        q.ex0 = cx + r * std::cos(a); q.ey0 = cy + r * std::sin(a);
        q.ex1 = cx + r * std::cos(a + sw); q.ey1 = cy + r * std::sin(a + sw);
        add(q.ex0, q.ey0, q.hw); add(q.ex1, q.ey1, q.hw);
        for (int k = -8; k <= 8; ++k) {
            const double e = k * kTau / 4;
            if (std::abs(arc_angle_offset(e, a, sw)) <= std::abs(sw)) add(cx + r * std::cos(e), cy + r * std::sin(e), q.hw);
        }
        break;
    }
    default: break;
    }
    q.bx0 = x0; q.by0 = y0; q.bx1 = x1; q.by1 = y1;
    return q;
}

bool shape_hit(const PreparedShape &q, double x, double y) {
    const ckey_shape &s = q.s;
    switch (s.kind) {
    case CKEY_SHAPE_RECT: return sdf_round_rect(x, y, s.p[0], s.p[1], s.p[2], s.p[3], std::min(s.p[4], std::min(s.p[2], s.p[3]) / 2)) <= 0;
    case CKEY_SHAPE_RECT_OUTLINE: return std::abs(sdf_round_rect(x, y, s.p[0], s.p[1], s.p[2], s.p[3], s.p[4])) <= q.hw;
    case CKEY_SHAPE_CIRCLE: return std::hypot(x - s.p[0], y - s.p[1]) <= s.p[2];
    case CKEY_SHAPE_DIAMOND: {  // even-odd crossing test, so samples on an edge resolve as in the SVG reference
        const double cx = s.p[0], cy = s.p[1], d = s.p[2];
        const double vx[4] = {cx, cx + d, cx, cx - d}, vy[4] = {cy - d, cy, cy + d, cy};
        bool inside = false;
        for (int i = 0, j = 3; i < 4; j = i++)
            if ((vy[i] > y) != (vy[j] > y) && x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i]) inside = !inside;
        return inside;
    }
    case CKEY_SHAPE_LINE: {
        const double vx = s.p[2] - s.p[0], vy = s.p[3] - s.p[1], len2 = vx * vx + vy * vy;
        const double t = len2 > 0 ? ((x - s.p[0]) * vx + (y - s.p[1]) * vy) / len2 : 0;
        if (!s.round_cap && (t < 0 || t > 1)) return false;
        const double c = std::clamp(t, 0.0, 1.0);
        return std::hypot(s.p[0] + c * vx - x, s.p[1] + c * vy - y) <= q.hw;
    }
    case CKEY_SHAPE_ARC: {
        const double d = arc_angle_offset(std::atan2(y - s.p[1], x - s.p[0]), s.p[3], s.p[4]);
        if (std::abs(d) <= std::abs(s.p[4])) return std::abs(std::hypot(x - s.p[0], y - s.p[1]) - s.p[2]) <= q.hw;
        if (!s.round_cap) return false;
        return std::hypot(x - q.ex0, y - q.ey0) <= q.hw || std::hypot(x - q.ex1, y - q.ey1) <= q.hw;
    }
    default: return false;
    }
}

struct Coverage {
    int side = 0, samples = 0;
    std::vector<uint8_t> ink;    // covered samples per pixel (0..samples)
    std::vector<int8_t> paint;   // last paint drawn into the pixel
};

// Rasterizes a scene's ink into per-pixel sample counts. Sample positions
// are the centres of an ss x ss grid in each pixel, the same as the
// reference SVG rasterizer, so the two backends agree sample for sample.
// Rows are processed in bands so the per-sample masks stay small.
Coverage rasterize_scene(const std::vector<ckey_shape> &scene, int side, int ss, double min_feature_units) {
    Coverage cov; cov.side = side; cov.samples = ss * ss;
    cov.ink.assign(static_cast<size_t>(side) * side, 0);
    cov.paint.assign(static_cast<size_t>(side) * side, CKEY_PAINT_INK);
    std::vector<PreparedShape> shapes;
    shapes.reserve(scene.size());
    for (const auto &s : scene) if (s.kind != CKEY_SHAPE_LOGO) shapes.push_back(prepare(s, min_feature_units));
    const int W = side * ss;
    const double k = 31.0 / W;  // canonical units per sample
    constexpr int kBandPixels = 32;
    const int band_samples = kBandPixels * ss;
    std::vector<uint8_t> hit(static_cast<size_t>(W) * band_samples);
    std::vector<int8_t> paint(static_cast<size_t>(W) * band_samples);
    for (int band = 0; band * kBandPixels < side; ++band) {
        const int sy0 = band * band_samples, sy1 = std::min(side * ss, sy0 + band_samples) - 1;
        std::fill(hit.begin(), hit.end(), 0);
        const double uy0 = -1 + sy0 * k, uy1 = -1 + (sy1 + 1) * k;
        for (const auto &q : shapes) {
            if (q.by1 < uy0 - k || q.by0 > uy1 + k) continue;
            const int x0 = std::max(0, static_cast<int>(std::floor((q.bx0 + 1) / k)) - 1);
            const int x1 = std::min(W - 1, static_cast<int>(std::ceil((q.bx1 + 1) / k)) + 1);
            const int y0 = std::max(sy0, static_cast<int>(std::floor((q.by0 + 1) / k)) - 1);
            const int y1 = std::min(sy1, static_cast<int>(std::ceil((q.by1 + 1) / k)) + 1);
            const bool erase = q.s.paint == CKEY_PAINT_BACKGROUND;
            for (int y = y0; y <= y1; ++y) {
                const double uy = -1 + (y + .5) * k;
                uint8_t *row = &hit[static_cast<size_t>(y - sy0) * W];
                int8_t *prow = &paint[static_cast<size_t>(y - sy0) * W];
                for (int x = x0; x <= x1; ++x) {
                    if (erase) { if (row[x] && shape_hit(q, -1 + (x + .5) * k, uy)) row[x] = 0; }
                    else if (!row[x] && shape_hit(q, -1 + (x + .5) * k, uy)) { row[x] = 1; prow[x] = static_cast<int8_t>(q.s.paint); }
                }
            }
        }
        const int py0 = band * kBandPixels, py1 = std::min(side, py0 + kBandPixels);
        for (int py = py0; py < py1; ++py) for (int px = 0; px < side; ++px) {
            int count = 0, accent = CKEY_PAINT_INK;
            for (int sy = 0; sy < ss; ++sy) for (int sx = 0; sx < ss; ++sx) {
                const size_t at = static_cast<size_t>((py - py0) * ss + sy) * W + px * ss + sx;
                if (hit[at]) { ++count; if (paint[at] >= 0) accent = paint[at]; }
            }
            cov.ink[static_cast<size_t>(py) * side + px] = static_cast<uint8_t>(count);
            cov.paint[static_cast<size_t>(py) * side + px] = static_cast<int8_t>(accent);
        }
    }
    return cov;
}

// Separable box blur, three passes approximating a Gaussian of sigma pixels.
void blur(std::vector<float> &img, int side, double sigma) {
    const int r = std::max(1, static_cast<int>(std::lround(sigma * 0.8660254)));  // 3 box passes: sigma^2 = r(r+1)
    std::vector<float> tmp(img.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < side; ++y) {  // horizontal
            double acc = 0; const size_t row = static_cast<size_t>(y) * side;
            for (int x = -r; x <= r; ++x) acc += img[row + std::clamp(x, 0, side - 1)];
            for (int x = 0; x < side; ++x) {
                tmp[row + x] = static_cast<float>(acc / (2 * r + 1));
                acc += img[row + std::clamp(x + r + 1, 0, side - 1)] - img[row + std::clamp(x - r, 0, side - 1)];
            }
        }
        for (int x = 0; x < side; ++x) {  // vertical
            double acc = 0;
            for (int y = -r; y <= r; ++y) acc += tmp[static_cast<size_t>(std::clamp(y, 0, side - 1)) * side + x];
            for (int y = 0; y < side; ++y) {
                img[static_cast<size_t>(y) * side + x] = static_cast<float>(acc / (2 * r + 1));
                acc += tmp[static_cast<size_t>(std::clamp(y + r + 1, 0, side - 1)) * side + x] - tmp[static_cast<size_t>(std::clamp(y - r, 0, side - 1)) * side + x];
            }
        }
    }
}

// Deterministic value noise for the background grain (non-normative paint).
double grain_noise(double x, double y) {
    const auto lattice = [](int ix, int iy) { return site_hash(static_cast<uint32_t>(ix) * 0x9e3779b1u ^ static_cast<uint32_t>(iy) * 0x85ebca77u ^ 0x47524149u) / 4294967295.0; };
    double total = 0, amp = 0.5;
    for (int octave = 0; octave < 2; ++octave, x *= 2, y *= 2, amp /= 2) {
        const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        const double fx = x - ix, fy = y - iy;
        const double a = lattice(ix, iy), b = lattice(ix + 1, iy), c = lattice(ix, iy + 1), d = lattice(ix + 1, iy + 1);
        total += amp * ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy);
    }
    return total / 0.75;
}

struct Rgbaf { double r, g, b, a; };

Rgbaf mix(const Rgbaf &a, const Rgbaf &b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
Rgbaf rgbf(ckey_rgb c) { return {c.r / 255.0, c.g / 255.0, c.b / 255.0, 1.0}; }
// Straight-alpha "source over".
Rgbaf over(const Rgbaf &src, double src_alpha, const Rgbaf &dst) {
    const double a = src_alpha + dst.a * (1 - src_alpha);
    if (a <= 0) return {0, 0, 0, 0};
    return {(src.r * src_alpha + dst.r * dst.a * (1 - src_alpha)) / a, (src.g * src_alpha + dst.g * dst.a * (1 - src_alpha)) / a,
            (src.b * src_alpha + dst.b * dst.a * (1 - src_alpha)) / a, a};
}

void draw_logo(std::vector<Rgbaf> &px, int side, const ckey_logo_image &logo, double cx, double cy, double r) {
    if (!logo.rgba || !logo.width || !logo.height) return;
    const size_t stride = logo.stride ? logo.stride : static_cast<size_t>(logo.width) * 4;
    const double scale = 2 * r / std::max(logo.width, logo.height);  // "meet": fit inside the circle's box
    const double w = logo.width * scale, h = logo.height * scale, x0 = cx - w / 2, y0 = cy - h / 2;
    const double unit = 31.0 / side;
    for (int py = 0; py < side; ++py) for (int pxi = 0; pxi < side; ++pxi) {
        const double ux = -1 + (pxi + .5) * unit, uy = -1 + (py + .5) * unit;
        if (std::hypot(ux - cx, uy - cy) > r || ux < x0 || uy < y0 || ux >= x0 + w || uy >= y0 + h) continue;
        const double sx = (ux - x0) / scale - .5, sy = (uy - y0) / scale - .5;
        const int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
        const double fx = sx - ix, fy = sy - iy;
        Rgbaf acc{0, 0, 0, 0};
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
            const int X = std::clamp(ix + dx, 0, static_cast<int>(logo.width) - 1), Y = std::clamp(iy + dy, 0, static_cast<int>(logo.height) - 1);
            const uint8_t *s = logo.rgba + static_cast<size_t>(Y) * stride + static_cast<size_t>(X) * 4;
            const double wgt = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy), a = s[3] / 255.0;
            acc.r += wgt * a * s[0] / 255.0; acc.g += wgt * a * s[1] / 255.0; acc.b += wgt * a * s[2] / 255.0; acc.a += wgt * a;
        }
        if (acc.a <= 0) continue;
        Rgbaf &d = px[static_cast<size_t>(py) * side + pxi];
        d = over({acc.r / acc.a, acc.g / acc.a, acc.b / acc.a, 1}, acc.a, d);
    }
}

ckey_status render_raster(const ScalableLayout &L, const ckey_scalable_symbol &sym, const ckey_style &st,
                          const ckey_raster_options &o, uint8_t *out) {
    const int side = static_cast<int>(o.size_px), ss = static_cast<int>(o.supersample);
    const double unit = 31.0 / side;  // canonical units per pixel
    const auto scene = build_scalable_scene(L, sym, st);
    const Coverage cov = rasterize_scene(scene, side, ss, o.policy == CKEY_RASTER_HYBRID ? o.min_feature_px * unit : 0.0);
    const size_t n = static_cast<size_t>(side) * side;
    std::vector<float> alpha(n);
    for (size_t i = 0; i < n; ++i) {
        const int c = cov.ink[i];
        alpha[i] = o.policy == CKEY_RASTER_BINARY ? (2 * c >= cov.samples ? 1.0f : 0.0f) : static_cast<float>(c) / cov.samples;
    }
    const bool materials = o.materials != 0, transparent = o.transparent != 0;
    const bool sheen = materials && !same_colour(st.ink, st.ink_shade);
    const bool vignette = materials && !same_colour(st.background, st.background_edge);
    // Depth: drop shadow and embossed highlight, from the ink coverage.
    std::vector<float> shadow, highlight;
    if (materials && st.shadow > 0) {
        shadow.assign(n, 0.f);
        const int dx = static_cast<int>(std::lround(0.05 / unit)), dy = static_cast<int>(std::lround(0.09 / unit));
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            const int sx = x - dx, sy = y - dy;
            if (sx >= 0 && sy >= 0 && sx < side && sy < side) shadow[static_cast<size_t>(y) * side + x] = alpha[static_cast<size_t>(sy) * side + sx];
        }
        blur(shadow, side, 0.07 / unit);
        for (auto &v : shadow) v = static_cast<float>(v * 0.75 * st.shadow);
    }
    if (materials && st.bevel > 0) {
        std::vector<float> bump = alpha;
        blur(bump, side, std::max(0.6, 0.045 / unit));
        highlight.assign(n, 0.f);
        // Light from azimuth 225 deg (upper left), elevation 40 deg; Blinn
        // half-vector specular with exponent 16, scaled like the SVG filter
        // at its reference 8 pixels per unit.
        const double el = 40.0 * kTau / 360, az = 225.0 * kTau / 360;
        const double lx = std::cos(az) * std::cos(el), ly = std::sin(az) * std::cos(el), lz = std::sin(el);
        double hx = lx, hy = ly, hz = lz + 1; const double hn = std::sqrt(hx * hx + hy * hy + hz * hz); hx /= hn; hy /= hn; hz /= hn;
        const double surface = 3.0 * (8.0 * unit);
        for (int y = 1; y + 1 < side; ++y) for (int x = 1; x + 1 < side; ++x) {
            const size_t i = static_cast<size_t>(y) * side + x;
            const double gx = (bump[i + 1] - bump[i - 1]) / (2 * unit) * unit * surface / unit / 8.0 * 8.0;
            const double gy = (bump[i + side] - bump[i - side]) / (2 * unit) * unit * surface / unit / 8.0 * 8.0;
            double nx = -gx, ny = -gy, nz = 1; const double nn = std::sqrt(nx * nx + ny * ny + nz * nz); nx /= nn; ny /= nn; nz /= nn;
            const double ndoth = std::max(0.0, nx * hx + ny * hy + nz * hz);
            highlight[i] = static_cast<float>(std::min(1.0, 1.1 * st.bevel * std::pow(ndoth, 16)) * alpha[i]);
        }
    }
    std::vector<Rgbaf> px(n);
    const Rgbaf bg_centre = rgbf(st.background), bg_edge = rgbf(st.background_edge), ink = rgbf(st.ink), shade = rgbf(st.ink_shade);
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        const size_t i = static_cast<size_t>(y) * side + x;
        const double ux = -1 + (x + .5) * unit, uy = -1 + (y + .5) * unit;
        Rgbaf c{0, 0, 0, 0};
        if (!transparent) {
            c = vignette ? mix(bg_centre, bg_edge, std::clamp(std::hypot(ux - kCenter, uy - kCenter) / 21.0, 0.0, 1.0)) : bg_centre;
            if (materials && st.texture > 0) { const double g = grain_noise(ux * 1.7 * 4, uy * 1.7 * 4); c = mix(c, {g, g, g, 1}, 0.18 * st.texture); }
        }
        if (!shadow.empty() && shadow[i] > 0) c = over({0, 0, 0, 1}, shadow[i], c);
        if (alpha[i] > 0) {
            Rgbaf m;
            if (cov.paint[i] >= 0) m = rgbf(st.accents[cov.paint[i]]);
            else if (sheen) {
                const double t = std::clamp((ux + uy) / 58.0, 0.0, 1.0);
                m = t < 0.55 ? mix(ink, shade, t / 0.55) : mix(shade, ink, (t - 0.55) / 0.45);
            } else m = ink;
            if (!highlight.empty()) { const double h = highlight[i] / std::max(1e-6f, alpha[i]); m = {std::min(1.0, m.r + h), std::min(1.0, m.g + h), std::min(1.0, m.b + h), 1}; }
            c = over(m, alpha[i], c);
        }
        px[i] = c;
    }
    if (o.logo) for (const auto &s : scene) if (s.kind == CKEY_SHAPE_LOGO) draw_logo(px, side, *o.logo, s.p[0], s.p[1], s.p[2]);
    const auto byte = [](double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    for (size_t i = 0; i < n; ++i) {
        const Rgbaf &c = px[i];
        if (o.format == CKEY_PIXELS_GRAY8) out[i] = byte(0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b);
        else { out[4 * i] = byte(c.r); out[4 * i + 1] = byte(c.g); out[4 * i + 2] = byte(c.b); out[4 * i + 3] = byte(c.a); }
    }
    return CKEY_OK;
}

// ---- PNG: zlib stream with fixed-Huffman DEFLATE and greedy LZ77 ----

uint32_t crc32_ieee(const uint8_t *data, size_t len, uint32_t crc = 0) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1; t[i] = c; }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

struct BitWriter {
    std::vector<uint8_t> out; uint32_t acc = 0; int bits = 0;
    void put(uint32_t value, int count) { acc |= value << bits; bits += count; while (bits >= 8) { out.push_back(static_cast<uint8_t>(acc)); acc >>= 8; bits -= 8; } }
    void put_reversed(uint32_t code, int count) { uint32_t r = 0; for (int i = 0; i < count; ++i) r |= ((code >> i) & 1u) << (count - 1 - i); put(r, count); }
    void flush() { if (bits) out.push_back(static_cast<uint8_t>(acc)); acc = 0; bits = 0; }
};

void put_literal(BitWriter &w, int lit) {  // RFC 1951 §3.2.6 fixed codes
    if (lit < 144) w.put_reversed(0x30 + lit, 8);
    else if (lit < 256) w.put_reversed(0x190 + lit - 144, 9);
    else if (lit < 280) w.put_reversed(lit - 256, 7);
    else w.put_reversed(0xc0 + lit - 280, 8);
}

void put_match(BitWriter &w, int length, int distance) {
    static constexpr int lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static constexpr int lextra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static constexpr int dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static constexpr int dextra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    int li = 28; while (lbase[li] > length) --li;
    put_literal(w, 257 + li); w.put(static_cast<uint32_t>(length - lbase[li]), lextra[li]);
    int di = 29; while (dbase[di] > distance) --di;
    w.put_reversed(static_cast<uint32_t>(di), 5); w.put(static_cast<uint32_t>(distance - dbase[di]), dextra[di]);
}

std::vector<uint8_t> zlib_compress(const std::vector<uint8_t> &data) {
    BitWriter w; w.out = {0x78, 0x01};
    w.put(1, 1); w.put(1, 2);  // final block, fixed Huffman
    constexpr int kWindow = 32768, kHashBits = 15, kChain = 32;
    std::vector<int> head(1 << kHashBits, -1), prev(data.size(), -1);
    const auto hash = [&](size_t i) { return ((data[i] << 10) ^ (data[i + 1] << 5) ^ data[i + 2]) & ((1 << kHashBits) - 1); };
    size_t i = 0;
    while (i < data.size()) {
        int best_len = 0, best_dist = 0;
        if (i + 2 < data.size()) {
            const int h = hash(i);
            int candidate = head[h], chain = 0;
            while (candidate >= 0 && static_cast<int>(i) - candidate <= kWindow && chain++ < kChain) {
                int len = 0;
                while (len < 258 && i + len < data.size() && data[candidate + len] == data[i + len]) ++len;
                if (len > best_len) { best_len = len; best_dist = static_cast<int>(i) - candidate; if (len == 258) break; }
                candidate = prev[candidate];
            }
        }
        const size_t step = best_len >= 3 ? static_cast<size_t>(best_len) : 1;
        if (best_len >= 3) put_match(w, best_len, best_dist); else put_literal(w, data[i]);
        for (size_t j = 0; j < step; ++j, ++i) if (i + 2 < data.size()) { const int h = hash(i); prev[i] = head[h]; head[h] = static_cast<int>(i); }
    }
    put_literal(w, 256);
    w.flush();
    uint32_t a = 1, b = 0;
    for (uint8_t v : data) { a = (a + v) % 65521; b = (b + a) % 65521; }
    const uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) w.out.push_back(static_cast<uint8_t>(adler >> s));
    return w.out;
}

void png_chunk(std::vector<uint8_t> &png, const char *type, const std::vector<uint8_t> &body) {
    const auto be32 = [&](uint32_t v) { for (int s = 24; s >= 0; s -= 8) png.push_back(static_cast<uint8_t>(v >> s)); };
    be32(static_cast<uint32_t>(body.size()));
    const size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), body.begin(), body.end());
    be32(crc32_ieee(png.data() + start, png.size() - start));
}

std::vector<uint8_t> encode_png(const uint8_t *px, uint32_t w, uint32_t h, size_t stride, ckey_png_format format, double dpi) {
    const int channels = format == CKEY_PNG_RGBA8 ? 4 : 1;
    const size_t row_bytes = format == CKEY_PNG_GRAY1 ? (w + 7) / 8 : static_cast<size_t>(w) * channels;
    std::vector<uint8_t> raw; raw.reserve((row_bytes + 1) * h);
    std::vector<uint8_t> line(row_bytes), above(row_bytes, 0);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *src = px + static_cast<size_t>(y) * stride;
        if (format == CKEY_PNG_GRAY1) {
            std::fill(line.begin(), line.end(), 0);
            for (uint32_t x = 0; x < w; ++x) if (src[x] >= 128) line[x / 8] |= static_cast<uint8_t>(0x80u >> (x % 8));
        } else std::memcpy(line.data(), src, row_bytes);
        // Filter: Up (2) for continuous images compresses flat regions well;
        // None (0) for 1-bit rows.
        if (format == CKEY_PNG_GRAY1) { raw.push_back(0); raw.insert(raw.end(), line.begin(), line.end()); }
        else { raw.push_back(2); for (size_t i = 0; i < row_bytes; ++i) raw.push_back(static_cast<uint8_t>(line[i] - above[i])); }
        above = line;
    }
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> ihdr;
    const auto be32 = [](std::vector<uint8_t> &v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s)); };
    be32(ihdr, w); be32(ihdr, h);
    ihdr.push_back(format == CKEY_PNG_GRAY1 ? 1 : 8);
    ihdr.push_back(format == CKEY_PNG_RGBA8 ? 6 : 0);
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    png_chunk(png, "IHDR", ihdr);
    if (dpi > 0) {
        std::vector<uint8_t> phys; const uint32_t ppm = static_cast<uint32_t>(std::lround(dpi / 0.0254));
        be32(phys, ppm); be32(phys, ppm); phys.push_back(1);
        png_chunk(png, "pHYs", phys);
    }
    png_chunk(png, "IDAT", zlib_compress(raw));
    png_chunk(png, "IEND", {});
    return png;
}

// ---- PDF ----

std::string pdf_num(double v) {
    char b[32]; std::snprintf(b, sizeof(b), "%.4f", v);
    std::string s = b; while (s.size() > 1 && s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

// Cubic Bézier approximation of a circular arc, in at most 90-degree pieces.
void pdf_arc_path(std::ostringstream &o, const std::function<std::string(double, double)> &pt, double cx, double cy, double r, double a0, double sweep, bool move) {
    const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (kTau / 4) - 1e-9)));
    const double step = sweep / pieces, k = 4.0 / 3.0 * std::tan(step / 4);
    double a = a0;
    if (move) o << pt(cx + r * std::cos(a), cy + r * std::sin(a)) << " m\n";
    for (int i = 0; i < pieces; ++i, a += step) {
        const double b = a + step;
        const double x0 = cx + r * std::cos(a), y0 = cy + r * std::sin(a), x3 = cx + r * std::cos(b), y3 = cy + r * std::sin(b);
        o << pt(x0 - k * r * std::sin(a), y0 + k * r * std::cos(a)) << ' ' << pt(x3 + k * r * std::sin(b), y3 - k * r * std::cos(b)) << ' ' << pt(x3, y3) << " c\n";
    }
}

void pdf_round_rect(std::ostringstream &o, const std::function<std::string(double, double)> &pt, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2);
    if (r <= 0) { o << pt(x, y) << " m " << pt(x + w, y) << " l " << pt(x + w, y + h) << " l " << pt(x, y + h) << " l h\n"; return; }
    o << pt(x + r, y) << " m " << pt(x + w - r, y) << " l\n";
    pdf_arc_path(o, pt, x + w - r, y + r, r, -kTau / 4, kTau / 4, false);
    o << pt(x + w, y + h - r) << " l\n";
    pdf_arc_path(o, pt, x + w - r, y + h - r, r, 0, kTau / 4, false);
    o << pt(x + r, y + h) << " l\n";
    pdf_arc_path(o, pt, x + r, y + h - r, r, kTau / 4, kTau / 4, false);
    o << pt(x, y + r) << " l\n";
    pdf_arc_path(o, pt, x + r, y + r, r, kTau / 2, kTau / 4, false);
    o << "h\n";
}

std::string render_pdf(const std::vector<ckey_shape> &scene, const ckey_style &st, double width_pt, bool transparent) {
    const double s = width_pt / 31.0;  // points per canonical unit; PDF y grows up
    const auto pt = [&](double x, double y) { return pdf_num((x + 1) * s) + ' ' + pdf_num((30 - y) * s); };
    const auto colour = [](ckey_rgb c, const char *op) { return pdf_num(c.r / 255.0) + ' ' + pdf_num(c.g / 255.0) + ' ' + pdf_num(c.b / 255.0) + ' ' + op + '\n'; };
    std::ostringstream o;
    if (!transparent) o << colour(st.background, "rg") << "0 0 " << pdf_num(width_pt) << ' ' << pdf_num(width_pt) << " re f\n";
    o << "1 J 1 j\n";
    for (const auto &sh : scene) {
        if (sh.kind == CKEY_SHAPE_LOGO) continue;
        const ckey_rgb c = sh.paint >= 0 ? st.accents[sh.paint] : sh.paint == CKEY_PAINT_BACKGROUND ? st.background : st.ink;
        if (sh.paint == CKEY_PAINT_BACKGROUND && transparent) continue;  // nothing to erase onto: plate omitted
        const bool stroke = sh.kind == CKEY_SHAPE_RECT_OUTLINE || sh.kind == CKEY_SHAPE_LINE || sh.kind == CKEY_SHAPE_ARC;
        o << colour(c, stroke ? "RG" : "rg");
        if (stroke) o << pdf_num(sh.width * s) << " w " << (sh.round_cap || sh.kind == CKEY_SHAPE_RECT_OUTLINE ? 1 : 0) << " J\n";
        switch (sh.kind) {
        case CKEY_SHAPE_RECT: pdf_round_rect(o, pt, sh.p[0], sh.p[1], sh.p[2], sh.p[3], sh.p[4]); o << "f\n"; break;
        case CKEY_SHAPE_RECT_OUTLINE: pdf_round_rect(o, pt, sh.p[0], sh.p[1], sh.p[2], sh.p[3], sh.p[4]); o << "S\n"; break;
        case CKEY_SHAPE_CIRCLE: pdf_arc_path(o, pt, sh.p[0], sh.p[1], sh.p[2], 0, kTau, true); o << "h f\n"; break;
        case CKEY_SHAPE_DIAMOND: {
            const double x = sh.p[0], y = sh.p[1], d = sh.p[2];
            o << pt(x, y - d) << " m " << pt(x + d, y) << " l " << pt(x, y + d) << " l " << pt(x - d, y) << " l h f\n"; break;
        }
        case CKEY_SHAPE_LINE: o << pt(sh.p[0], sh.p[1]) << " m " << pt(sh.p[2], sh.p[3]) << " l S\n"; break;
        case CKEY_SHAPE_ARC: pdf_arc_path(o, pt, sh.p[0], sh.p[1], sh.p[2], sh.p[3], sh.p[4], true); o << "S\n"; break;
        default: break;
        }
    }
    const std::string content = o.str();
    std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + pdf_num(width_pt) + ' ' + pdf_num(width_pt) + "] /Contents 4 0 R /Resources << >> >>",
        "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream",
        "<< /Producer (CKey " CKEY_SOFTWARE_VERSION ") >>"};
    std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); ++i) { offsets.push_back(pdf.size()); pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n"; }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t off : offsets) { char b[24]; std::snprintf(b, sizeof(b), "%010zu 00000 n \n", off); pdf += b; }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R /Info 5 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

}  // namespace

extern "C" {

ckey_status ckey_scalable_geometry(const ckey_scalable_symbol *s, const ckey_style *style, ckey_shape *out, size_t cap, size_t *count) {
    ckey_style st{};
    const ScalableLayout *L = count ? scene_layout(s, style, st) : nullptr;
    if (!L) return CKEY_INVALID_ARGUMENT;
    const auto scene = build_scalable_scene(*L, *s, st);
    *count = scene.size();
    if (!out || cap < scene.size()) return CKEY_INSUFFICIENT_BUFFER;
    std::copy(scene.begin(), scene.end(), out);
    return CKEY_OK;
}

void ckey_raster_options_default(ckey_raster_options *o) {
    if (!o) return;
    *o = ckey_raster_options{};
    o->abi_version = CKEY_ABI_VERSION; o->size_px = 512; o->policy = CKEY_RASTER_COVERAGE; o->format = CKEY_PIXELS_RGBA8;
    o->transparent = 0; o->supersample = 4; o->min_feature_px = 1.0; o->materials = 1; o->logo = nullptr;
}

ckey_status ckey_render_scalable_raster(const ckey_scalable_symbol *s, const ckey_style *style, const ckey_raster_options *o,
                                        uint8_t *out, size_t cap, size_t *required) {
    if (!o || !required || o->abi_version != CKEY_ABI_VERSION || o->size_px < 16 || o->size_px > 16384 || o->policy > CKEY_RASTER_HYBRID ||
        o->format > CKEY_PIXELS_GRAY8 || o->supersample < 1 || o->supersample > 8 || !(o->min_feature_px > 0) || !std::isfinite(o->min_feature_px) ||
        (o->transparent && o->format == CKEY_PIXELS_GRAY8)) return CKEY_INVALID_ARGUMENT;
    ckey_style st{};
    const ScalableLayout *L = scene_layout(s, style, st);
    if (!L) return CKEY_INVALID_ARGUMENT;
    *required = static_cast<size_t>(o->size_px) * o->size_px * (o->format == CKEY_PIXELS_RGBA8 ? 4 : 1);
    if (!out || cap < *required) return CKEY_INSUFFICIENT_BUFFER;
    try { return render_raster(*L, *s, st, *o, out); } catch (...) { return CKEY_INTERNAL_ERROR; }
}

ckey_status ckey_encode_png(const uint8_t *px, uint32_t w, uint32_t h, size_t stride, ckey_png_format format, double dpi,
                            uint8_t *out, size_t cap, size_t *required) {
    if (!px || !required || w == 0 || h == 0 || w > 65535 || h > 65535 || format > CKEY_PNG_GRAY1 || !std::isfinite(dpi) || dpi < 0) return CKEY_INVALID_ARGUMENT;
    const size_t min_stride = static_cast<size_t>(w) * (format == CKEY_PNG_RGBA8 ? 4 : 1);
    if (stride == 0) stride = min_stride;
    if (stride < min_stride) return CKEY_INVALID_ARGUMENT;
    try {
        const auto png = encode_png(px, w, h, stride, format, dpi);
        *required = png.size();
        if (!out || cap < png.size()) return CKEY_INSUFFICIENT_BUFFER;
        std::memcpy(out, png.data(), png.size());
        return CKEY_OK;
    } catch (...) { return CKEY_INTERNAL_ERROR; }
}

ckey_status ckey_render_scalable_pdf(const ckey_scalable_symbol *s, const ckey_style *style, double width_pt, uint32_t transparent,
                                     uint8_t *out, size_t cap, size_t *required) {
    if (!required || !std::isfinite(width_pt) || width_pt < 1 || width_pt > 14400) return CKEY_INVALID_ARGUMENT;
    ckey_style st{};
    const ScalableLayout *L = scene_layout(s, style, st);
    if (!L) return CKEY_INVALID_ARGUMENT;
    try {
        const std::string pdf = render_pdf(build_scalable_scene(*L, *s, st), st, width_pt, transparent != 0);
        *required = pdf.size();
        if (!out || cap < pdf.size()) return CKEY_INSUFFICIENT_BUFFER;
        std::memcpy(out, pdf.data(), pdf.size());
        return CKEY_OK;
    } catch (...) { return CKEY_INTERNAL_ERROR; }
}

}  // extern "C"
