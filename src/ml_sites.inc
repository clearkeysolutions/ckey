// ML site model: reads Profile 0 sites from a restored ink map.
//
// The decoder has a homography for a candidate (canonical 29-unit field ->
// image). The field is rectified at kMlSitesPerModule samples per module
// pitch; a site model (CKML, stride 1 or 2, one output channel) turns the
// rectified field into an ink logit per sample, learning to undo blur, glare,
// ink spread and moire. Each site's logit is read off the map at the site's
// canonical position: the sign gives printed / not printed, the magnitude
// the confidence that feeds the per-byte reliabilities of the soft decoder.
// It is one more reading strategy: Reed-Solomon and CRC-32C still decide.
// Included by the scan library after ml_locator.inc.

namespace {

constexpr uint32_t kMlSitesPerModule = 2;

// Canonical position of site i (dial rays: the sample point on the ray).
void ml_site_xy(const ScalableLayout &L, size_t i, double &x, double &y) {
    const auto &n = L.nodes[i];
    if (n.kind == 1) {
        const double a = kTau * n.slot / L.dial_slots, r = kScalableDialInner + L.dial_sample;
        x = kCenter + r * std::sin(a); y = kCenter - r * std::cos(a);
    } else { x = n.x; y = n.y; }
}

struct MlRectified {
    std::vector<uint8_t> px;     // side x side samples of the field
    std::vector<uint8_t> known;  // 0 where the sample fell outside the image
    uint32_t side = 0;           // samples across (a multiple of the model's alignment)
    double spacing = 0;          // canonical units per sample
    bool dark = true;            // ink darker than paper
};

// Rectifies the 29-unit field through h at `per_module` samples per module
// pitch, padded to a multiple of `align`. Polarity from the frame stroke
// (solid ink at the field edge) against the field's mean, as the reader does.
bool ml_rectify(const uint8_t *gray, size_t width, size_t height, size_t stride, const double h[8], double pitch,
                uint32_t per_module, uint32_t align, MlRectified &r) {
    if (!(pitch > 0) || per_module == 0) return false;
    const double spacing = pitch / per_module;
    uint32_t side = static_cast<uint32_t>(std::ceil(29.0 / spacing - 1e-9));
    if (align > 1) side = (side + align - 1) / align * align;
    if (side < 16 || side > 2048) return false;
    r.side = side; r.spacing = spacing;
    r.px.assign(static_cast<size_t>(side) * side, 0); r.known.assign(r.px.size(), 0);
    // Area-average over the sample cell when the image has more pixels than
    // samples (a 3x3 spread across a third of the cell).
    const double spread = spacing / 3.0;
    double sum = 0; size_t count = 0;
    for (uint32_t j = 0; j < side; ++j) for (uint32_t i = 0; i < side; ++i) {
        const double x = (i + 0.5) * spacing, y = (j + 0.5) * spacing;
        if (x > 29.0 || y > 29.0) continue;
        const double v = gray_sample(gray, width, height, stride, h, x, y, spread);
        if (v < 0) { if (work_exhausted()) return false; continue; }
        const size_t k = static_cast<size_t>(j) * side + i;
        r.px[k] = static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)); r.known[k] = 1; sum += v; ++count;
    }
    if (count * 2 < static_cast<size_t>(side) * side / 2) return false;
    const double mean = sum / static_cast<double>(count);
    for (size_t k = 0; k < r.px.size(); ++k) if (!r.known[k]) r.px[k] = static_cast<uint8_t>(std::lround(mean));
    double stroke = 0; int strokes = 0;
    for (const double t : {7.0, 11.0, 14.5, 18.0, 22.0}) for (const auto &[x, y] : {std::pair{t, 0.5}, {28.5, t}, {t, 28.5}, {0.5, t}}) {
        const double v = gray_sample(gray, width, height, stride, h, x, y, 0.05);
        if (v >= 0) { stroke += v; ++strokes; }
    }
    if (!strokes) return false;
    r.dark = stroke / strokes < mean;
    return true;
}

// Standardised, ink-positive model input (identical in ml/sites.py).
std::vector<float> ml_sites_input(const MlRectified &r) {
    std::vector<float> x(r.px.size());
    double mean = 0; for (uint8_t v : r.px) mean += v; mean /= static_cast<double>(r.px.size());
    double var = 0; for (uint8_t v : r.px) var += (v - mean) * (v - mean); var /= static_cast<double>(r.px.size());
    const double sd = std::max(4.0, std::sqrt(var)), sign = r.dark ? -1.0 : 1.0;
    for (size_t k = 0; k < x.size(); ++k) x[k] = static_cast<float>(sign * (r.px[k] - mean) / sd);
    return x;
}

// Bilinear read of a logit map (side x side cells of `spacing` canonical
// units, cell centres at +0.5) at canonical (x, y).
float ml_map_at(const std::vector<float> &map, uint32_t side, double spacing, double x, double y) {
    const double fx = std::clamp(x / spacing - 0.5, 0.0, side - 1.0), fy = std::clamp(y / spacing - 0.5, 0.0, side - 1.0);
    const size_t x0 = static_cast<size_t>(fx), y0 = static_cast<size_t>(fy);
    const size_t x1 = std::min<size_t>(x0 + 1, side - 1), y1 = std::min<size_t>(y0 + 1, side - 1);
    const double ax = fx - x0, ay = fy - y0;
    const auto at = [&](size_t xx, size_t yy) { return static_cast<double>(map[yy * side + xx]); };
    return static_cast<float>((at(x0, y0) * (1 - ax) + at(x1, y0) * ax) * (1 - ay) + (at(x0, y1) * (1 - ax) + at(x1, y1) * ax) * ay);
}

bool ml_known_at(const MlRectified &r, double x, double y) {
    const long i = static_cast<long>(x / r.spacing), j = static_cast<long>(y / r.spacing);
    if (i < 0 || j < 0 || i >= static_cast<long>(r.side) || j >= static_cast<long>(r.side)) return false;
    return r.known[static_cast<size_t>(j) * r.side + static_cast<size_t>(i)] != 0;
}

// Site logits for layout L through homography h: positive = printed.
// Unknown sites (outside the image) get NaN. Empty when no site model is
// loaded or the field cannot be rectified.
std::vector<float> ml_site_logits(const ScalableLayout &L, const uint8_t *gray, size_t width, size_t height, size_t stride, const double h[8]) {
    const auto model = ml_sites_current();
    if (!model || model->kind != kMlSites) return {};
    MlRectified r;
    if (!ml_rectify(gray, width, height, stride, h, L.pitch, kMlSitesPerModule, model->align, r)) return {};
    const std::vector<float> map = ml_forward(*model, ml_sites_input(r), r.side, r.side);
    const uint32_t out_side = r.side / model->stride;
    if (map.size() != static_cast<size_t>(out_side) * out_side) return {};
    const double out_spacing = r.spacing * model->stride;
    std::vector<float> logits(L.nodes.size());
    for (size_t i = 0; i < L.nodes.size(); ++i) {
        double x, y; ml_site_xy(L, i, x, y);
        logits[i] = ml_known_at(r, x, y) ? ml_map_at(map, out_side, out_spacing, x, y) : std::numeric_limits<float>::quiet_NaN();
    }
    return logits;
}

// SiteRead from logits (the reader's confidence is in noise deviations; a
// logit's magnitude plays that role here).
void ml_site_read(const std::vector<float> &logits, SiteRead &out) {
    out.printed.assign(logits.size(), 0); out.confidence.assign(logits.size(), 0.0f); out.unknown = 0;
    for (size_t i = 0; i < logits.size(); ++i) {
        if (!std::isfinite(logits[i])) { ++out.unknown; continue; }
        out.printed[i] = logits[i] > 0 ? 1 : 0;
        out.confidence[i] = std::min(6.0f, std::abs(logits[i]));
    }
}

// ---- multi-frame capture ---------------------------------------------------------------
// While a capture is active (thread-local, set by the accumulating frame scan),
// the Profile 0 decoder records the geometry of its most plausible reading of
// the frame (the one whose raw header matched best): the homography of the
// canonical field. The accumulator only needs where the field is, not which
// version the keys named.
struct EvidenceCapture {
    bool active = false, have = false;
    int version = 0, header = -1;
    double mean_conf = 0;
    double quad[8] = {};   // field corners (image pixels)
    double h[8] = {};      // canonical field -> image
};
inline EvidenceCapture &evidence_capture() { static thread_local EvidenceCapture c; return c; }

void capture_evidence(const ScalableLayout &L, const SiteRead &read, int header_matches, const double h[8]) {
    EvidenceCapture &c = evidence_capture();
    if (!c.active) return;
    double mean = 0; for (float v : read.confidence) mean += v; mean /= std::max<size_t>(1, read.confidence.size());
    if (c.have && (header_matches < c.header || (header_matches == c.header && mean <= c.mean_conf))) return;
    c.have = true; c.version = L.version; c.header = header_matches; c.mean_conf = mean;
    std::copy(h, h + 8, c.h);
    constexpr double corners[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    for (int k = 0; k < 4; ++k) { ImagePoint p{}; if (map_point(h, corners[2 * k], corners[2 * k + 1], p)) { c.quad[2 * k] = p.x; c.quad[2 * k + 1] = p.y; } }
}

}  // namespace
