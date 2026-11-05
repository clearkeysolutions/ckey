// Public C ABI: optical certification (specification §4). Included after
// core_internal.inc. It renders with ckey_render and decodes with ckey_scan,
// so it is its own library, ckey_certify.

namespace {

struct ModelOutcome { uint32_t trials = 0, passed = 0; double separation = 1.0; };

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

ModelOutcome run_model(const ScalableLayout &L, const ckey_scalable_symbol &sym, const ckey_style &style, bool transparent,
                       const ckey_backdrop *backdrop, const ckey_scan_model &m, const std::vector<uint8_t> &payload) {
    ModelOutcome result;
    const double units_per_module = L.pitch;
    const int side = std::clamp(static_cast<int>(std::ceil(31.0 * m.px_per_module / units_per_module)), 64, 8192);
    ckey_style draw = style; draw.policy = CKEY_POLICY_OVERRIDE;  // test exactly this style
    ckey_raster_options o; ckey_raster_options_default(&o);
    o.size_px = static_cast<uint32_t>(side); o.supersample = 2; o.transparent = transparent ? 1u : 0u;
    size_t need = 0;
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4);
    if (ckey_render_scalable_raster(&sym, &draw, &o, rgba.data(), rgba.size(), &need) != CKEY_OK) return result;
    // Which pixels are marks and which are gaps: the same shapes in flat black on white.
    ckey_style mask_style; ckey_style_default(&mask_style);
    mask_style.modules = style.modules; mask_style.rings = style.rings; mask_style.rays = style.rays;
    mask_style.logo_href = style.logo_href; mask_style.logo_radius = style.logo_radius; mask_style.logo_plate = style.logo_plate;
    mask_style.policy = CKEY_POLICY_OVERRIDE;
    ckey_raster_options mo = o; mo.transparent = 0; mo.format = CKEY_PIXELS_GRAY8; mo.materials = 0;
    std::vector<uint8_t> mask(static_cast<size_t>(side) * side);
    if (ckey_render_scalable_raster(&sym, &mask_style, &mo, mask.data(), mask.size(), &need) != CKEY_OK) return result;

    std::vector<std::pair<const ckey_rgb *, const ckey_backdrop *>> grounds;
    if (!transparent) grounds.push_back({nullptr, nullptr});
    else if (backdrop) {
        for (uint32_t i = 0; i < backdrop->colour_count; ++i) grounds.push_back({&backdrop->colours[i], nullptr});
        if (backdrop->rgba && backdrop->width && backdrop->height) grounds.push_back({nullptr, backdrop});
    }
    const double lo = side / 31.0, hi = 30.0 * side / 31.0;
    const double quad[8] = {lo, lo, hi, lo, hi, hi, lo, hi};
    std::vector<uint8_t> out(65535), gray(static_cast<size_t>(side) * side);
    uint32_t seed = 0x43455254u;  // "CERT": the model is deterministic
    for (const auto &[colour, image] : grounds) {
        auto luma = composite_luma(rgba, side, colour, image);
        std::vector<double> marks, gaps;
        for (size_t i = 0; i < luma.size(); i += 3) { if (mask[i] < 16) marks.push_back(luma[i]); else if (mask[i] > 239) gaps.push_back(luma[i]); }
        const bool dark_marks = percentile(marks, 0.5) < percentile(gaps, 0.5);
        const double sep = dark_marks ? percentile(gaps, 0.05) - percentile(marks, 0.95) : percentile(marks, 0.05) - percentile(gaps, 0.95);
        result.separation = std::min(result.separation, sep);
        for (auto &v : luma) v = v * (1 - m.glare) + m.glare;
        gaussian_blur(luma, side, m.blur_px);
        for (uint32_t t = 0; t < m.trials; ++t) {
            ++result.trials;
            for (size_t i = 0; i < luma.size(); ++i) {
                // Box-Muller from a seeded LCG.
                seed = seed * 1664525u + 1013904223u; const double u1 = (seed >> 8) / 16777216.0 + 1e-9;
                seed = seed * 1664525u + 1013904223u; const double u2 = (seed >> 8) / 16777216.0;
                const double n = std::sqrt(-2 * std::log(u1)) * std::cos(kTau * u2) * m.noise;
                gray[i] = static_cast<uint8_t>(std::lround(std::clamp(luma[i] + n, 0.0, 1.0) * 255));
            }
            size_t len = 0; uint32_t version = 0, layout = 0; ckey_diagnostics d{};
            if (ckey_decode_scalable_gray8(gray.data(), side, side, side, quad, out.data(), out.size(), &len, &version, &layout, &d) == CKEY_OK &&
                len == payload.size() && std::equal(payload.begin(), payload.end(), out.begin()))
                ++result.passed;
        }
    }
    return result;
}

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
    if (st.policy == CKEY_POLICY_ADAPTIVE) out->adjustments |= adapt_style(L, *s, work, extra);
    double contrast = 0, load = 0;
    std::string problem = safety_problem(L, *s, work, extra, contrast, load);
    ModelOutcome model_result = run_model(L, *s, work, transparent != 0, backdrop, m, payload);
    if (st.policy == CKEY_POLICY_ADAPTIVE && (!problem.empty() || model_result.passed < model_result.trials)) {
        strip_materials(work); out->adjustments |= CKEY_ADJUST_MATERIALS;
        problem = safety_problem(L, *s, work, extra, contrast, load);
        model_result = run_model(L, *s, work, transparent != 0, backdrop, m, payload);
    }
    out->min_contrast = contrast; out->logo_load = load;
    out->trials = model_result.trials; out->trials_passed = model_result.passed; out->feature_separation = model_result.separation;
    out->certified = problem.empty() && model_result.trials > 0 && model_result.passed == model_result.trials ? 1u : 0u;
    char model_text[96];
    std::snprintf(model_text, sizeof(model_text), "model %.1f px/module, blur %.2f px, noise %.3f, glare %.2f: %u/%u decoded",
                  m.px_per_module, m.blur_px, m.noise, m.glare, model_result.passed, model_result.trials);
    std::string text = out->certified ? std::string("certified; ") + model_text
                     : !problem.empty() ? "not certified: " + problem + "; " + model_text
                                        : std::string("not certified: the modelled scan failed; ") + model_text;
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
