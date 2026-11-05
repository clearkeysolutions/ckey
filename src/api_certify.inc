// Public C ABI: optical certification (specification §4). Included after
// core_internal.inc. It renders with ckey_render and decodes with ckey_scan,
// so it is its own library, ckey_certify.

namespace {


// Straight-alpha RGBA over an opaque backdrop colour or image, to luma 0..1.
std::vector<double> composite_luma(const std::vector<uint8_t> &rgba, int side, const ckey_rgb *colour, const ckey_backdrop *image) {
    std::vector<double> luma(static_cast<size_t>(side) * side);
    const size_t stride = image && image->rgba ? (image->stride ? image->stride : static_cast<size_t>(image->width) * 4) : 0;
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        const size_t i = static_cast<size_t>(y) * side + x;
        double br = 1, bg = 1, bb = 1;
        if (colour) { br = colour->r / 255.0; bg = colour->g / 255.0; bb = colour->b / 255.0; }
        else if (image && image->rgba) {
            const uint32_t sx = std::min<uint32_t>(image->width - 1, static_cast<uint32_t>((x + .5) * image->width / side));
            const uint32_t sy = std::min<uint32_t>(image->height - 1, static_cast<uint32_t>((y + .5) * image->height / side));
            const uint8_t *p = image->rgba + sy * stride + static_cast<size_t>(sx) * 4;
            const double a = p[3] / 255.0;  // the host's pixels are composited over white
            br = 1 - a + a * p[0] / 255.0; bg = 1 - a + a * p[1] / 255.0; bb = 1 - a + a * p[2] / 255.0;
        }
        const double a = rgba[4 * i + 3] / 255.0;
        const double r = a * rgba[4 * i] / 255.0 + (1 - a) * br, g = a * rgba[4 * i + 1] / 255.0 + (1 - a) * bg, b = a * rgba[4 * i + 2] / 255.0 + (1 - a) * bb;
        luma[i] = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    }
    return luma;
}

void gaussian_blur(std::vector<double> &img, int side, double sigma) {
    if (sigma <= 0.05) return;
    const int r = std::max(1, static_cast<int>(std::ceil(3 * sigma)));
    std::vector<double> k(static_cast<size_t>(2 * r + 1)); double sum = 0;
    for (int i = -r; i <= r; ++i) { k[static_cast<size_t>(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma)); sum += k[static_cast<size_t>(i + r)]; }
    for (auto &v : k) v /= sum;
    std::vector<double> tmp(img.size());
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        double acc = 0; for (int i = -r; i <= r; ++i) acc += k[static_cast<size_t>(i + r)] * img[static_cast<size_t>(y) * side + std::clamp(x + i, 0, side - 1)];
        tmp[static_cast<size_t>(y) * side + x] = acc;
    }
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        double acc = 0; for (int i = -r; i <= r; ++i) acc += k[static_cast<size_t>(i + r)] * tmp[static_cast<size_t>(std::clamp(y + i, 0, side - 1)) * side + x];
        img[static_cast<size_t>(y) * side + x] = acc;
    }
}

double percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(q * (v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}

// ---- Realistic scan sweep ----
// A style is judged by how small it can be before a real reader can no longer
// find and decode it in a photograph, compared with plain black on white.
// Each scene places the symbol in a cluttered frame, tilts it (rotation and
// keystone), adds glare, defocus and sensor noise, and runs the full reader
// pipeline: ckey_find_borders_gray8 -> ckey_refine_quad_gray8 -> decode. The
// sweep walks down a ladder of camera resolutions (pixels per module).

constexpr double kLadder[] = {9.0, 7.0, 5.5, 4.5, 3.75, 3.0};
constexpr int kLadderSteps = 6;
constexpr int kMaxFrame = 2600;  // keeps the sweep fast enough for browsers
constexpr int kStartStep = 2;    // the search starts mid-ladder (5.5 px per module) and walks out

struct Scene { double rotation_deg, keystone; uint32_t seed; };
constexpr Scene kScenes[] = {{8.0, 0.04, 0x53434e31u}, {-5.0, 0.0, 0x53434e32u}};

bool scene_decodes(const ckey_scalable_symbol &sym, const ckey_style &draw, bool transparent, const ckey_rgb *ground_colour,
                   const ckey_backdrop *ground_image, const ckey_scan_model &m, double ppm, const Scene &sc,
                   const std::vector<uint8_t> &payload, bool &too_big) {
    const ScalableLayout &L = scalable_layout(static_cast<int>(sym.version), static_cast<int>(sym.layout));
    const int side = std::max(64, static_cast<int>(std::ceil(31.0 * ppm / L.pitch)));
    const int margin = std::max(40, side < 900 ? side * 3 / 10 : side * 3 / 25);  // dense symbols: a tighter crop
    const int F = side + 2 * margin;
    too_big = F > kMaxFrame;
    if (too_big) return false;
    ckey_raster_options o; ckey_raster_options_default(&o);
    o.size_px = static_cast<uint32_t>(side); o.supersample = 2; o.transparent = transparent ? 1u : 0u;
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4); size_t need = 0;
    if (ckey_render_scalable_raster(&sym, &draw, &o, rgba.data(), rgba.size(), &need) != CKEY_OK) return false;
    const auto symbol = composite_luma(rgba, side, ground_colour, ground_image);
    // The frame: a lit desk with clutter (deterministic per scene).
    std::vector<double> frame(static_cast<size_t>(F) * F);
    uint32_t seed = sc.seed;
    const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
    for (int y = 0; y < F; ++y) for (int x = 0; x < F; ++x) frame[static_cast<size_t>(y) * F + x] = 0.42 + 0.22 * x / F + 0.12 * y / F;
    for (int k = 0; k < 10; ++k) {
        const int x0 = static_cast<int>(rnd() * F), y0 = static_cast<int>(rnd() * F), w = 8 + static_cast<int>(rnd() * F / 4), h = 8 + static_cast<int>(rnd() * F / 4);
        const double v = rnd();
        for (int y = y0; y < std::min(F, y0 + h); ++y) for (int x = x0; x < std::min(F, x0 + w); ++x) frame[static_cast<size_t>(y) * F + x] = v;
    }
    // Symbol corners in the frame: rotated about the centre, top edge narrowed.
    const double c = F / 2.0, half = side / 2.0, a = sc.rotation_deg * kTau / 360;
    const double corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    double dst[8];
    for (int i = 0; i < 4; ++i) {
        double x = corner[i][0] * half, y = corner[i][1] * half;
        if (corner[i][1] < 0) x *= 1.0 - sc.keystone;
        dst[2 * i] = c + x * std::cos(a) - y * std::sin(a); dst[2 * i + 1] = c + x * std::sin(a) + y * std::cos(a);
    }
    const double src[8] = {0, 0, static_cast<double>(side), 0, static_cast<double>(side), static_cast<double>(side), 0, static_cast<double>(side)};
    double h[8];
    if (!solve_homography(dst, src, h)) return false;
    for (int y = 0; y < F; ++y) for (int x = 0; x < F; ++x) {
        ImagePoint p;
        if (!map_point(h, x + 0.5, y + 0.5, p)) continue;
        const double u = p.x - 0.5, v = p.y - 0.5;
        if (u < 0 || v < 0 || u >= side - 1 || v >= side - 1) continue;
        const int iu = static_cast<int>(u), iv = static_cast<int>(v); const double fu = u - iu, fv = v - iv;
        const auto at = [&](int X, int Y) { return symbol[static_cast<size_t>(Y) * side + X]; };
        frame[static_cast<size_t>(y) * F + x] = (at(iu, iv) * (1 - fu) + at(iu + 1, iv) * fu) * (1 - fv) + (at(iu, iv + 1) * (1 - fu) + at(iu + 1, iv + 1) * fu) * fv;
    }
    for (auto &v : frame) v = v * (1 - m.glare) + m.glare;
    gaussian_blur(frame, F, m.blur_px);
    std::vector<uint8_t> gray(frame.size());
    for (size_t i = 0; i < frame.size(); ++i) {
        const double u1 = rnd() + 1e-9, u2 = rnd();
        const double n = std::sqrt(-2 * std::log(u1)) * std::cos(kTau * u2) * m.noise;
        gray[i] = static_cast<uint8_t>(std::lround(std::clamp(frame[i] + n, 0.0, 1.0) * 255));
    }
    double borders[6 * 8];
    const size_t found = ckey_find_borders_gray8(gray.data(), F, F, F, borders, 6);
    std::vector<uint8_t> out(65535);
    for (size_t i = 0; i < found; ++i) {
        double q[8]; size_t len = 0; uint32_t version = 0, layout = 0; ckey_diagnostics d{};
        if (ckey_refine_quad_gray8(gray.data(), F, F, F, borders + 8 * i, CKEY_FIELD_SCALABLE, q) > 0 &&
            ckey_decode_scalable_gray8(gray.data(), F, F, F, q, out.data(), out.size(), &len, &version, &layout, &d) == CKEY_OK)
            return len == payload.size() && std::equal(payload.begin(), payload.end(), out.begin());
    }
    return false;
}

struct Sweep { double smallest_ppm = 0; int smallest_step = -1; uint32_t scenes = 0, passed = 0; };

// Walks the ladder from large to small; the result is the smallest resolution
// at which every scene decoded (and every larger one did too).
Sweep sweep(const ckey_scalable_symbol &sym, const ckey_style &style, bool transparent, const ckey_rgb *colour,
            const ckey_backdrop *image, const ckey_scan_model &m, const std::vector<uint8_t> &payload) {
    Sweep r;
    ckey_style draw = style; draw.policy = CKEY_POLICY_OVERRIDE;  // test exactly this style
    // 1 = every scene decoded, 0 = one failed, -1 = too large to simulate (treated as decoding
    // when a smaller size decodes, since a larger image of the same symbol only gets easier).
    const auto step_result = [&](int step) {
        for (const Scene &sc : kScenes) {
            bool too_big = false;
            const bool ok = scene_decodes(sym, draw, transparent, colour, image, m, kLadder[step], sc, payload, too_big);
            if (too_big) return -1;
            ++r.scenes; r.passed += ok ? 1 : 0;
            if (!ok) return 0;
        }
        return 1;
    };
    int start = kStartStep;
    int res = step_result(start);
    while (res == -1 && start + 1 < kLadderSteps) res = step_result(++start);  // huge symbols: start smaller
    if (res == 1) {
        r.smallest_step = start; r.smallest_ppm = kLadder[start];
        for (int step = start + 1; step < kLadderSteps && step_result(step) == 1; ++step) { r.smallest_step = step; r.smallest_ppm = kLadder[step]; }
    } else {
        for (int step = start - 1; step >= 0; --step) {
            const int up = step_result(step);
            if (up == 1) { r.smallest_step = step; r.smallest_ppm = kLadder[step]; break; }
        }
    }
    return r;
}

// Plain black on white for the same symbol: the reference a style is held to.
Sweep plain_sweep(const ckey_scalable_symbol &sym, const ckey_scan_model &m, const std::vector<uint8_t> &payload) {
    // Cached per symbol and model: the reference does not depend on the style being judged.
    static std::mutex lock;
    static std::vector<std::pair<std::string, Sweep>> cache;
    std::string key(reinterpret_cast<const char *>(sym.codeword), sym.codeword_length);
    char tail[96]; std::snprintf(tail, sizeof(tail), "|%u|%u|%.3f|%.3f|%.3f|%.3f", sym.version, sym.layout, m.px_per_module, m.blur_px, m.noise, m.glare);
    key += tail;
    { std::lock_guard<std::mutex> g(lock); for (const auto &[k, v] : cache) if (k == key) return v; }
    ckey_style plain; ckey_style_default(&plain);
    const Sweep r = sweep(sym, plain, false, nullptr, nullptr, m, payload);
    std::lock_guard<std::mutex> g(lock);
    if (cache.size() >= 16) cache.erase(cache.begin());
    cache.push_back({key, r});
    return r;
}

// Luma separation of marks vs gaps after compositing, at the ladder's top size.
double separation_of(const ckey_scalable_symbol &sym, const ckey_style &style, bool transparent, const ckey_rgb *colour, const ckey_backdrop *image) {
    const ScalableLayout &L = scalable_layout(static_cast<int>(sym.version), static_cast<int>(sym.layout));
    const int side = std::clamp(static_cast<int>(std::ceil(31.0 * 6.0 / L.pitch)), 64, 1600);
    ckey_style draw = style; draw.policy = CKEY_POLICY_OVERRIDE;
    ckey_raster_options o; ckey_raster_options_default(&o);
    o.size_px = static_cast<uint32_t>(side); o.supersample = 2; o.transparent = transparent ? 1u : 0u;
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4), mask(static_cast<size_t>(side) * side); size_t need = 0;
    if (ckey_render_scalable_raster(&sym, &draw, &o, rgba.data(), rgba.size(), &need) != CKEY_OK) return 0;
    ckey_style mask_style; ckey_style_default(&mask_style);
    mask_style.modules = style.modules; mask_style.rings = style.rings; mask_style.rays = style.rays;
    mask_style.logo_href = style.logo_href; mask_style.logo_radius = style.logo_radius; mask_style.logo_plate = style.logo_plate;
    mask_style.policy = CKEY_POLICY_OVERRIDE;
    ckey_raster_options mo = o; mo.transparent = 0; mo.format = CKEY_PIXELS_GRAY8; mo.materials = 0;
    if (ckey_render_scalable_raster(&sym, &mask_style, &mo, mask.data(), mask.size(), &need) != CKEY_OK) return 0;
    const auto luma = composite_luma(rgba, side, colour, image);
    std::vector<double> marks, gaps;
    for (size_t i = 0; i < luma.size(); i += 3) { if (mask[i] < 16) marks.push_back(luma[i]); else if (mask[i] > 239) gaps.push_back(luma[i]); }
    const bool dark_marks = percentile(marks, 0.5) < percentile(gaps, 0.5);
    return dark_marks ? percentile(gaps, 0.05) - percentile(marks, 0.95) : percentile(marks, 0.05) - percentile(gaps, 0.95);
}

const char *grade_name(int g) { return g >= 3 ? "excellent" : g == 2 ? "good" : g == 1 ? "marginal" : "fails"; }

void strip_materials(ckey_style &st) {
    st.ink_shade = st.ink; st.background_edge = st.background;
    st.bevel = st.shadow = st.texture = 0;
}

}  // namespace

extern "C" {

void ckey_scan_model_default(ckey_scan_model *m) {
    if (!m) return;
    *m = ckey_scan_model{};
    m->abi_version = CKEY_ABI_VERSION; m->px_per_module = 8.0; m->blur_px = 0.9; m->noise = 0.03; m->glare = 0.2; m->trials = 3;
}

ckey_status ckey_certify_scalable_style(const ckey_scalable_symbol *s, const ckey_style *style, uint32_t transparent,
                                        const ckey_backdrop *backdrop, const ckey_scan_model *model, ckey_style *out_style,
                                        ckey_certification *out) {
    if (!out) return CKEY_INVALID_ARGUMENT;
    *out = ckey_certification{}; out->abi_version = CKEY_ABI_VERSION;
    const auto refuse = [&](const char *why) { std::snprintf(out->message, sizeof(out->message), "%s", why); return CKEY_INVALID_ARGUMENT; };
    if (!s || s->abi_version != CKEY_ABI_VERSION || s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT || s->ecc_level > 3 ||
        s->layout >= static_cast<uint32_t>(kScalableFamilies)) return refuse("certification needs a Profile 0 symbol");
    const ScalableLayout &L = scalable_layout(static_cast<int>(s->version), static_cast<int>(s->layout));
    if (s->codeword_length != L.codeword_bytes) return refuse("symbol codeword does not match its version");
    ckey_scan_model m; ckey_scan_model_default(&m);
    if (model) m = *model;
    if (m.abi_version != CKEY_ABI_VERSION || !std::isfinite(m.px_per_module) || m.px_per_module < 2 || m.px_per_module > 40 ||
        !std::isfinite(m.blur_px) || m.blur_px < 0 || m.blur_px > 10 || !std::isfinite(m.noise) || m.noise < 0 || m.noise > 0.5 ||
        !std::isfinite(m.glare) || m.glare < 0 || m.glare > 0.9 || m.trials < 1 || m.trials > 16) return refuse("invalid scan model");
    if (backdrop && (backdrop->colour_count > 16 || (backdrop->colour_count && !backdrop->colours) ||
                     (backdrop->rgba && (!backdrop->width || !backdrop->height || (backdrop->stride && backdrop->stride < static_cast<size_t>(backdrop->width) * 4)))))
        return refuse("invalid backdrop");
    ckey_style st; if (style) st = *style; else ckey_style_default(&st);
    out->policy = st.policy;
    const std::string structural = structural_problem(L, st);
    if (!structural.empty()) return refuse(structural.c_str());
    std::vector<uint8_t> payload(65535); size_t payload_len = 0; ckey_diagnostics d{};
    if (ckey_decode_scalable(s->codeword, s->codeword_length, s->version, static_cast<ckey_scalable_layout>(s->layout), payload.data(), payload.size(), &payload_len, &d) != CKEY_OK)
        return refuse("the symbol's own codeword does not decode");
    payload.resize(payload_len);

    std::vector<ckey_rgb> extra;
    const bool has_backdrop = backdrop && (backdrop->colour_count || backdrop->rgba);
    if (transparent && !has_backdrop) {
        std::snprintf(out->message, sizeof(out->message), "background unknown: transparent output cannot be certified; pass the host backdrop");
        return st.policy == CKEY_POLICY_OVERRIDE ? CKEY_OK : CKEY_INVALID_ARGUMENT;
    }
    if (transparent) {
        // The symbol's own background is not drawn: rules test the backdrop.
        if (backdrop->colour_count) { st.background = backdrop->colours[0]; st.background_edge = backdrop->colours[0]; }
        for (uint32_t i = 0; i < backdrop->colour_count; ++i) extra.push_back(backdrop->colours[i]);
        if (backdrop->rgba) {  // the backdrop image's darkest and lightest pixels join the contrast rules
            const size_t stride = backdrop->stride ? backdrop->stride : static_cast<size_t>(backdrop->width) * 4;
            ckey_rgb dark{255, 255, 255}, light{0, 0, 0}; double dl = 2, ll = -1;
            for (uint32_t y = 0; y < backdrop->height; y += std::max(1u, backdrop->height / 64)) for (uint32_t x = 0; x < backdrop->width; x += std::max(1u, backdrop->width / 64)) {
                const uint8_t *p = backdrop->rgba + y * stride + static_cast<size_t>(x) * 4; const double a = p[3] / 255.0;
                const ckey_rgb c{static_cast<uint8_t>(std::lround(255 * (1 - a) + a * p[0])), static_cast<uint8_t>(std::lround(255 * (1 - a) + a * p[1])), static_cast<uint8_t>(std::lround(255 * (1 - a) + a * p[2]))};
                const double l = relative_luminance(c);
                if (l < dl) { dl = l; dark = c; } if (l > ll) { ll = l; light = c; }
            }
            extra.push_back(dark); extra.push_back(light);
            if (!backdrop->colour_count) { st.background = st.background_edge = dark; }
        }
    }
    ckey_style work = st;
    double contrast = 0, load = 0;
    std::string advice;
    // Adaptive repairs a hard failure up front; anything else is left for the sweep to judge.
    if (st.policy == CKEY_POLICY_ADAPTIVE && !safety_problem(L, *s, work, extra, contrast, load).empty())
        out->adjustments |= adapt_style(L, *s, work, extra);
    std::string problem = safety_problem(L, *s, work, extra, contrast, load, &advice);
    // Grounds the symbol is judged on: its own background, or every backdrop.
    std::vector<std::pair<const ckey_rgb *, const ckey_backdrop *>> grounds;
    if (!transparent) grounds.push_back({nullptr, nullptr});
    else {
        for (uint32_t i = 0; i < backdrop->colour_count; ++i) grounds.push_back({&backdrop->colours[i], nullptr});
        if (backdrop->rgba && backdrop->width && backdrop->height) grounds.push_back({nullptr, backdrop});
    }
    const auto evaluate = [&](const ckey_style &w, Sweep &worst, double &sep) {
        worst = Sweep{}; worst.smallest_step = kLadderSteps; sep = 1.0; bool first = true;
        for (const auto &[colour, image] : grounds) {
            const Sweep r = sweep(*s, w, transparent != 0, colour, image, m, payload);
            worst.scenes += r.scenes; worst.passed += r.passed;
            if (first || r.smallest_step < worst.smallest_step) { worst.smallest_step = r.smallest_step; worst.smallest_ppm = r.smallest_ppm; }
            first = false;
            sep = std::min(sep, separation_of(*s, w, transparent != 0, colour, image));
        }
    };
    const Sweep plain = plain_sweep(*s, m, payload);
    const auto grade_of = [&](const Sweep &r) {
        if (r.smallest_step < 0 || r.smallest_ppm > m.px_per_module + 1e-9) return 0;
        if (plain.smallest_step < 0) return 2;  // the reference itself fails: judge by the style alone
        const int behind = plain.smallest_step - r.smallest_step;  // ladder steps larger than plain needs
        return behind <= 0 ? 3 : behind == 1 ? 2 : behind == 2 ? 1 : 0;
    };
    Sweep result; double separation = 1.0;
    evaluate(work, result, separation);
    int grade = grade_of(result);
    if (st.policy == CKEY_POLICY_ADAPTIVE && (!problem.empty() || grade < 2)) {
        out->adjustments |= adapt_style(L, *s, work, extra);  // now also the advisory contrast repairs
        strip_materials(work); out->adjustments |= CKEY_ADJUST_MATERIALS;
        problem = safety_problem(L, *s, work, extra, contrast, load, &advice);
        evaluate(work, result, separation);
        grade = grade_of(result);
    }
    out->min_contrast = contrast; out->logo_load = load;
    out->trials = result.scenes; out->trials_passed = result.passed; out->feature_separation = separation;
    out->certified = problem.empty() && grade >= 2 ? 1u : 0u;
    char sweep_text[320];
    const auto ppm_text = [](const Sweep &r) { char b[48]; if (r.smallest_step < 0) std::snprintf(b, sizeof(b), "not even at %.0f px per module", kLadder[0]); else std::snprintf(b, sizeof(b), "down to %.2g px per module", r.smallest_ppm); return std::string(b); };
    std::snprintf(sweep_text, sizeof(sweep_text), "%s scan margin: found and decoded in modelled photos %s; plain black on white %s",
                  grade_name(grade), ppm_text(result).c_str(), ppm_text(plain).c_str());
    if (!advice.empty()) { std::strncat(sweep_text, "; note: ", sizeof(sweep_text) - std::strlen(sweep_text) - 1); std::strncat(sweep_text, advice.c_str(), sizeof(sweep_text) - std::strlen(sweep_text) - 1); }
    std::string text = out->certified ? std::string("certified (") + grade_name(grade) + "); " + sweep_text
                     : !problem.empty() ? "not certified: " + problem + "; " + sweep_text
                                        : std::string("not certified: ") + sweep_text;
    if (out->adjustments) {
        text += "; adaptive changes:";
        if (out->adjustments & CKEY_ADJUST_INK) text += " ink";
        if (out->adjustments & CKEY_ADJUST_BACKGROUND) text += " background";
        if (out->adjustments & CKEY_ADJUST_ACCENTS) text += " accents";
        if (out->adjustments & CKEY_ADJUST_EFFECTS) text += " depth";
        if (out->adjustments & CKEY_ADJUST_LOGO) text += " logo";
        if (out->adjustments & CKEY_ADJUST_MATERIALS) text += " materials";
    }
    if (st.logo_href && *st.logo_href) text += "; the logo image itself is not modelled";
    std::snprintf(out->message, sizeof(out->message), "%s", text.c_str());
    if (out_style && st.policy == CKEY_POLICY_ADAPTIVE && out->certified) {
        // Hand back the repaired style with the caller's own background
        // fields restored for transparent output (they are not drawn).
        if (transparent && style) { work.background = style->background; work.background_edge = style->background_edge; }
        work.policy = CKEY_POLICY_STRICT; *out_style = work;
    } else if (out_style && st.policy != CKEY_POLICY_ADAPTIVE) *out_style = style ? *style : st;
    if (st.policy == CKEY_POLICY_OVERRIDE) return CKEY_OK;
    return out->certified ? CKEY_OK : CKEY_INVALID_ARGUMENT;
}

}  // extern "C"
