// Public C ABI: one-call frame scan, tracking and camera advice. Included
// after api_scan.inc (scan library and the amalgamation).

#include "ml_locator.inc"

namespace {

// Corner order of `quad` (cyclic, either winding) closest to `previous`.
void match_corner_order(const double previous[8], double quad[8]) {
    double best = std::numeric_limits<double>::infinity(); int best_reverse = 0, best_start = 0;
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        double sum = 0;
        for (int i = 0; i < 4; ++i) {
            const int j = (start + (reverse ? -i : i) + 8) % 4;
            const double dx = quad[2 * j] - previous[2 * i], dy = quad[2 * j + 1] - previous[2 * i + 1];
            sum += dx * dx + dy * dy;
        }
        if (sum < best) { best = sum; best_reverse = reverse; best_start = start; }
    }
    double out[8];
    for (int i = 0; i < 4; ++i) { const int j = (best_start + (best_reverse ? -i : i) + 8) % 4; out[2 * i] = quad[2 * j]; out[2 * i + 1] = quad[2 * j + 1]; }
    std::copy(out, out + 8, quad);
}

// Rewrites a Profile 0 field quadrilateral in the symbol's own corner order
// (top-left, top-right, bottom-right, bottom-left of the symbol as designed),
// using the orientation whose keys fit best. Returns false if no orientation
// is decisive; the quadrilateral is then left as it was.
bool canonical_scalable_order(const uint8_t *gray, size_t width, size_t height, size_t stride, double quad[8]) {
    double best = 0, second = 0; int best_reverse = 0, best_start = 0;
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        double h[8]; bool ok; orientation_homography(quad, reverse, start, h, ok); if (!ok) continue;
        double score = 0;
        for (int f = 0; f < kScalableFamilies; ++f) for (int v = 1; v <= kScalableVersions; ++v) {
            const KeyFit keys = scalable_version_fit(scalable_layout(v, f), gray, width, height, stride, h);
            if (keys.valid) score = std::max(score, keys.contrast * keys.evidence * keys.evidence);
        }
        if (score > best) { second = best; best = score; best_reverse = reverse; best_start = start; }
        else if (score > second) second = score;
    }
    if (best <= 0 || best < 1.15 * second) return false;
    double out[8];
    for (int i = 0; i < 4; ++i) { const int j = (best_start + (best_reverse ? -i : i) + 8) % 4; out[2 * i] = quad[2 * j]; out[2 * i + 1] = quad[2 * j + 1]; }
    std::copy(out, out + 8, quad);
    return true;
}

struct FrameScan {
    ckey_status status = CKEY_UNCORRECTABLE;
    int profile = 0; uint32_t version = 0, layout = 0, corrected = 0, erasures = 0, candidates = 0, flags = 0;
    bool located = false; double quad[8] = {}; double score = 0; size_t length = 0; char message[96] = {};
};

// The reference reader loop on one image: border candidates, then for each
// one Profile 0, 1, 2, 3, each refined against its own keys.
// ML proposals are thresholds on the locator's presence score: proposals at or
// above kMlPropose are tried first; with the gate option a frame whose best
// presence is below kMlGate skips the border search.
constexpr double kMlPropose = 0.30, kMlGate = 0.15;
constexpr size_t kMlMaxProposals = 3;

void scan_image(const uint8_t *gray, size_t width, size_t height, size_t stride, uint32_t profiles, size_t max_candidates,
                uint8_t *payload, size_t capacity, FrameScan &r, uint32_t option_flags = 0) {
    if (width < 16 || height < 16) return;
    std::vector<uint8_t> out(65535);
    // One candidate border (or proposal) through refine + decode for each requested profile.
    const auto try_border = [&](const double *border, uint32_t only_profiles, bool from_ml) -> bool {
        for (int field = 0; field < 4; ++field) {
            if (!(profiles & only_profiles & (1u << field))) continue;
            double quad[8];
            double score = ckey_refine_quad_gray8(gray, width, height, stride, border, static_cast<ckey_field>(field), quad);
            if (score < 0) {
                if (!from_ml) continue;
                std::copy(border, border + 8, quad); score = 0;   // decode from the proposal itself
            }
            if (field == CKEY_FIELD_SCALABLE && score > r.score) { r.located = true; r.score = score; std::copy(quad, quad + 8, r.quad); }
            size_t length = 0; uint32_t version = 0, layout = 0; ckey_diagnostics d{}; ckey_status st;
            if (field == CKEY_FIELD_SCALABLE) st = ckey_decode_scalable_gray8(gray, width, height, stride, quad, out.data(), out.size(), &length, &version, &layout, &d);
            else if (field == CKEY_FIELD_LOCATOR) { st = ckey_decode_gray8(gray, width, height, stride, quad, out.data(), CKEY_LOCATOR_PAYLOAD_BYTES, &d); length = CKEY_LOCATOR_PAYLOAD_BYTES; }
            else if (field == CKEY_FIELD_TEXT) st = ckey_decode_text_gray8(gray, width, height, stride, quad, out.data(), out.size(), &length, &d);
            else st = ckey_decode_extended_gray8(gray, width, height, stride, quad, out.data(), out.size(), &length, &d);
            if (st != CKEY_OK) continue;
            r.profile = field; r.version = version; r.layout = layout; r.corrected = d.corrected_symbols; r.erasures = d.erasures_used;
            r.located = true; r.score = score; std::copy(quad, quad + 8, r.quad); r.length = length;
            if (from_ml) r.flags |= CKEY_SCAN_FLAG_ML;
            std::snprintf(r.message, sizeof(r.message), "%s", d.message);
            if (field == CKEY_FIELD_SCALABLE) canonical_scalable_order(gray, width, height, stride, r.quad);
            if (length > capacity) { r.status = CKEY_INSUFFICIENT_BUFFER; return true; }
            if (length) std::memcpy(payload, out.data(), length);
            r.status = CKEY_OK;
            return true;
        }
        return false;
    };
    if (option_flags & CKEY_SCAN_OPTION_ML) {
        MlDetection det[kMlMaxProposals];
        const size_t n = ml_locate(gray, width, height, stride, std::min(kMlPropose, kMlGate), det, kMlMaxProposals);
        double best = 0;
        for (size_t i = 0; i < n; ++i) {
            best = std::max(best, det[i].score);
            if (det[i].score < kMlPropose) continue;
            // The locator knows Profile 0 only; other profiles keep the border search.
            if (try_border(det[i].quad, CKEY_SCAN_PROFILE_SCALABLE, true)) return;
        }
        const bool model = ml_current() != nullptr;
        if (model && (option_flags & CKEY_SCAN_OPTION_ML_GATE) && best < kMlGate) {
            std::snprintf(r.message, sizeof(r.message), "%s", "ML locator: no symbol");
            return;
        }
    }
    std::vector<double> borders(8 * max_candidates);
    bool cut = false;
    const size_t count = find_borders(gray, width, height, stride, borders.data(), max_candidates, &cut);
    r.candidates = static_cast<uint32_t>(count);
    if (cut) r.flags |= CKEY_SCAN_FLAG_SYMBOL_CUT;
    for (size_t i = 0; i < count; ++i) if (try_border(borders.data() + 8 * i, CKEY_SCAN_PROFILE_ALL, false)) return;
}

// Brown radial model on focal-normalised coordinates: distorted = undistorted
// * (1 + k1 r^2). Builds an undistorted copy of a region so the planar
// pipeline applies; `map` converts undistorted region pixels back to frame
// pixels for the reported corners.
struct Undistorted {
    std::vector<uint8_t> px; size_t w = 0, h = 0; double x0 = 0, y0 = 0, k1 = 0, f = 1, cx = 0, cy = 0;
    void to_frame(double ux, double uy, double &fx, double &fy) const {
        const double x = (ux + x0 - cx) / f, y = (uy + y0 - cy) / f, d = 1 + k1 * (x * x + y * y);
        fx = cx + f * x * d; fy = cy + f * y * d;
    }
};

Undistorted undistort_region(const uint8_t *gray, size_t width, size_t height, size_t stride, double rx, double ry, double rw, double rh,
                             double k1, double f, double cx, double cy) {
    // The undistorted image of the region spans the undistorted positions of
    // its corners; solve distorted -> undistorted by fixed-point iteration.
    Undistorted u; u.k1 = k1; u.f = f; u.cx = cx; u.cy = cy;
    double min_x = 1e300, min_y = 1e300, max_x = -1e300, max_y = -1e300;
    for (int i = 0; i <= 8; ++i) for (int j = 0; j <= 8; ++j) {
        if (i && j && i < 8 && j < 8) continue;
        const double xd = (rx + rw * i / 8.0 - cx) / f, yd = (ry + rh * j / 8.0 - cy) / f;
        double x = xd, y = yd;
        for (int it = 0; it < 12; ++it) { const double d = 1 + k1 * (x * x + y * y); x = xd / d; y = yd / d; }
        min_x = std::min(min_x, cx + f * x); max_x = std::max(max_x, cx + f * x);
        min_y = std::min(min_y, cy + f * y); max_y = std::max(max_y, cy + f * y);
    }
    u.x0 = std::floor(min_x); u.y0 = std::floor(min_y);
    u.w = static_cast<size_t>(std::clamp(std::ceil(max_x) - u.x0, 16.0, 8192.0)); u.h = static_cast<size_t>(std::clamp(std::ceil(max_y) - u.y0, 16.0, 8192.0));
    u.px.assign(u.w * u.h, 0);
    for (size_t y = 0; y < u.h; ++y) for (size_t x = 0; x < u.w; ++x) {
        double fx, fy; u.to_frame(static_cast<double>(x), static_cast<double>(y), fx, fy);
        if (fx < 0 || fy < 0 || fx > static_cast<double>(width - 1) || fy > static_cast<double>(height - 1)) continue;
        const size_t x0 = static_cast<size_t>(fx), y0 = static_cast<size_t>(fy), x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
        const double ax = fx - x0, ay = fy - y0;
        const double top = gray[y0 * stride + x0] * (1 - ax) + gray[y0 * stride + x1] * ax, bottom = gray[y1 * stride + x0] * (1 - ax) + gray[y1 * stride + x1] * ax;
        u.px[y * u.w + x] = static_cast<uint8_t>(top * (1 - ay) + bottom * ay + 0.5);
    }
    return u;
}

constexpr double kTrackGate = 15.0, kTrackLock = 150.0;
constexpr uint32_t kTrackMaxMissed = 8;

void quad_bounds(const double q[8], double &min_x, double &min_y, double &max_x, double &max_y) {
    min_x = max_x = q[0]; min_y = max_y = q[1];
    for (int i = 1; i < 4; ++i) { min_x = std::min(min_x, q[2 * i]); max_x = std::max(max_x, q[2 * i]); min_y = std::min(min_y, q[2 * i + 1]); max_y = std::max(max_y, q[2 * i + 1]); }
}

// Best Profile 0 candidate in a rectangle of the frame (full-frame corners).
bool best_candidate_in(const uint8_t *gray, size_t width, size_t height, size_t stride, double rx, double ry, double rw, double rh,
                       size_t tries, const double *near, double out[8], double &score) {
    const size_t x0 = static_cast<size_t>(std::clamp(std::floor(rx), 0.0, static_cast<double>(width - 1)));
    const size_t y0 = static_cast<size_t>(std::clamp(std::floor(ry), 0.0, static_cast<double>(height - 1)));
    const size_t w = std::min(width - x0, static_cast<size_t>(std::max(0.0, std::ceil(rw)))), h = std::min(height - y0, static_cast<size_t>(std::max(0.0, std::ceil(rh))));
    if (w < 32 || h < 32) return false;
    const uint8_t *region = gray + y0 * stride + x0;
    double borders[8 * 6];
    const size_t count = ckey_find_borders_gray8(region, w, h, stride, borders, std::min<size_t>(tries, 6));
    bool found = false; score = 0;
    for (size_t i = 0; i < count; ++i) {
        double quad[8];
        const double s = ckey_refine_quad_gray8(region, w, h, stride, borders + 8 * i, CKEY_FIELD_SCALABLE, quad);
        if (s <= score) continue;
        for (int k = 0; k < 4; ++k) { quad[2 * k] += static_cast<double>(x0); quad[2 * k + 1] += static_cast<double>(y0); }
        if (near) {
            // Continuity: the centre moves less than half the size, and the
            // size changes by less than a quarter.
            double cx = 0, cy = 0, px = 0, py = 0;
            for (int k = 0; k < 4; ++k) { cx += quad[2 * k] / 4; cy += quad[2 * k + 1] / 4; px += near[2 * k] / 4; py += near[2 * k + 1] / 4; }
            const double size = quad_mean_side(quad), previous = quad_mean_side(near);
            if (std::hypot(cx - px, cy - py) > 0.5 * previous || std::abs(size - previous) > 0.25 * previous) continue;
        }
        score = s; std::copy(quad, quad + 8, out); found = true;
    }
    return found;
}

}  // namespace

extern "C" {

void ckey_scan_options_init(ckey_scan_options *o) {
    if (!o) return;
    std::memset(o, 0, sizeof(*o));
    o->abi_version = CKEY_ABI_VERSION; o->profiles = CKEY_SCAN_PROFILE_ALL; o->max_candidates = 6;
}

ckey_status ckey_scan_frame_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride, const ckey_scan_options *options,
                                  uint8_t *out_payload, size_t out_capacity, ckey_scan_result *result) {
    if (result) { std::memset(result, 0, sizeof(*result)); result->abi_version = CKEY_ABI_VERSION; result->status = CKEY_INVALID_ARGUMENT; }
    if (!gray || !result || (!out_payload && out_capacity) || width < 16 || height < 16 || stride < width ||
        height > std::numeric_limits<size_t>::max() / stride) return CKEY_INVALID_ARGUMENT;
    ckey_scan_options o; ckey_scan_options_init(&o);
    if (options) o = *options;
    for (double v : {o.roi_x, o.roi_y, o.roi_width, o.roi_height, o.k1, o.focal_px, o.center_x, o.center_y}) if (!std::isfinite(v)) return CKEY_INVALID_ARGUMENT;
    const uint32_t profiles = (o.profiles & CKEY_SCAN_PROFILE_ALL) ? (o.profiles & CKEY_SCAN_PROFILE_ALL) : CKEY_SCAN_PROFILE_ALL;
    const size_t tries = std::clamp<size_t>(o.max_candidates ? o.max_candidates : 6, 1, 12);
    double rx = 0, ry = 0, rw = static_cast<double>(width), rh = static_cast<double>(height);
    if (o.roi_width > 0 && o.roi_height > 0) {
        rx = std::clamp(std::floor(o.roi_x), 0.0, static_cast<double>(width - 16)); ry = std::clamp(std::floor(o.roi_y), 0.0, static_cast<double>(height - 16));
        rw = std::clamp(std::ceil(o.roi_width), 16.0, static_cast<double>(width) - rx); rh = std::clamp(std::ceil(o.roi_height), 16.0, static_cast<double>(height) - ry);
    }
    FrameScan scan;
    if (o.k1 != 0.0 && std::abs(o.k1) <= 1.0) {
        const double f = o.focal_px > 0 ? o.focal_px : static_cast<double>(std::max(width, height));
        const bool centred = o.center_x == 0 && o.center_y == 0;
        const Undistorted u = undistort_region(gray, width, height, stride, rx, ry, rw, rh, o.k1, f,
                                               centred ? 0.5 * static_cast<double>(width) : o.center_x, centred ? 0.5 * static_cast<double>(height) : o.center_y);
        scan_image(u.px.data(), u.w, u.h, u.w, profiles, tries, out_payload, out_capacity, scan, o.flags);
        for (int i = 0; i < 4; ++i) { double fx, fy; u.to_frame(scan.quad[2 * i], scan.quad[2 * i + 1], fx, fy); scan.quad[2 * i] = fx; scan.quad[2 * i + 1] = fy; }
    } else {
        const size_t x0 = static_cast<size_t>(rx), y0 = static_cast<size_t>(ry);
        scan_image(gray + y0 * stride + x0, static_cast<size_t>(rw), static_cast<size_t>(rh), stride, profiles, tries, out_payload, out_capacity, scan, o.flags);
        for (int i = 0; i < 4; ++i) { scan.quad[2 * i] += rx; scan.quad[2 * i + 1] += ry; }
    }
    result->status = scan.status; result->profile = static_cast<uint32_t>(scan.profile); result->version = scan.version; result->layout = scan.layout;
    result->corrected_symbols = scan.corrected; result->erasures_used = scan.erasures; result->candidates = scan.candidates; result->flags = scan.flags;
    result->located = scan.located ? 1u : 0u; result->payload_length = scan.status == CKEY_OK || scan.status == CKEY_INSUFFICIENT_BUFFER ? scan.length : 0;
    if (scan.located) std::copy(scan.quad, scan.quad + 8, result->quad);
    result->score = scan.score;
    std::snprintf(result->message, sizeof(result->message), "%s", scan.status == CKEY_OK ? scan.message : scan.message[0] ? scan.message : scan.candidates ? "no candidate decoded" : "no symbol border found");
    return scan.status;
}

size_t ckey_ml_locate_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride, double min_score, ckey_ml_detection *out, size_t max_out) {
    if (!gray || !out || !max_out || width < 16 || height < 16 || stride < width || height > std::numeric_limits<size_t>::max() / stride || !std::isfinite(min_score)) return 0;
    std::vector<MlDetection> det(std::min<size_t>(max_out, 16));
    const size_t n = ml_locate(gray, width, height, stride, std::clamp(min_score, 0.0, 1.0), det.data(), det.size());
    for (size_t i = 0; i < n; ++i) {
        std::copy(det[i].quad, det[i].quad + 8, out[i].quad);
        out[i].score = det[i].score; out[i].quality = det[i].quality; out[i].version = det[i].version; out[i].print_layout = det[i].print;
    }
    return n;
}

ckey_status ckey_ml_load_model(const uint8_t *data, size_t length) {
    if (!data) { std::lock_guard<std::mutex> lock(g_ml_mutex); g_ml_model.reset(); g_ml_builtin_tried = false; return CKEY_OK; }
    std::string err;
    auto m = ml_parse(data, length, err);
    if (!m) return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_ml_mutex);
    g_ml_model = std::move(m); g_ml_builtin_tried = true;
    return CKEY_OK;
}

uint64_t ckey_ml_model_id(void) {
    const auto m = ml_current();
    return m ? m->id : 0;
}

void ckey_match_corner_order(const double previous_xy[8], double quad_xy[8]) {
    if (!previous_xy || !quad_xy) return;
    for (int i = 0; i < 8; ++i) if (!std::isfinite(previous_xy[i]) || !std::isfinite(quad_xy[i])) return;
    match_corner_order(previous_xy, quad_xy);
}

void ckey_track_init(ckey_track_state *t) {
    if (!t) return;
    std::memset(t, 0, sizeof(*t));
    t->abi_version = CKEY_ABI_VERSION; t->gate = kTrackGate; t->lock_score = kTrackLock;
}

void ckey_track_seed(ckey_track_state *t, const double quad_xy[8], double score, int decoded) {
    if (!t || !quad_xy || !valid_quad(quad_xy)) return;
    for (int i = 0; i < 8; ++i) if (!std::isfinite(quad_xy[i])) return;
    const double gate = t->gate > 0 ? t->gate : kTrackGate, lock = t->lock_score > 0 ? t->lock_score : kTrackLock;
    if (!decoded && score < gate) return;
    double quad[8]; std::copy(quad_xy, quad_xy + 8, quad);
    if (t->status != CKEY_TRACK_NONE) match_corner_order(t->quad, quad);
    const bool agrees = t->status != CKEY_TRACK_NONE && quad_corner_distance(quad, t->quad) < 0.5 * quad_mean_side(t->quad);
    std::copy(quad, quad + 8, t->quad);
    t->score = score; t->missed_frames = 0; t->fresh = 1;
    t->sightings = agrees ? t->sightings + 1 : 1;
    if (decoded) t->decoded = 1; else if (!agrees) t->decoded = 0;
    t->status = (decoded || score >= lock || t->sightings >= 2 || t->status == CKEY_TRACK_LOCKED) && (agrees || decoded || score >= lock) ? CKEY_TRACK_LOCKED : CKEY_TRACK_TENTATIVE;
}

uint32_t ckey_track_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride, ckey_track_state *t) {
    if (!t) return CKEY_TRACK_NONE;
    if (!gray || width < 32 || height < 32 || stride < width || height > std::numeric_limits<size_t>::max() / stride) return t->status;
    const double gate = t->gate > 0 ? t->gate : kTrackGate, lock = t->lock_score > 0 ? t->lock_score : kTrackLock;
    t->fresh = 0;
    double quad[8], score = 0;
    if (t->status == CKEY_TRACK_NONE) {
        if (best_candidate_in(gray, width, height, stride, 0, 0, static_cast<double>(width), static_cast<double>(height), 3, nullptr, quad, score) && score >= gate) {
            std::copy(quad, quad + 8, t->quad); t->score = score; t->sightings = 1; t->missed_frames = 0; t->decoded = 0; t->fresh = 1;
            t->status = score >= lock ? CKEY_TRACK_LOCKED : CKEY_TRACK_TENTATIVE;
        }
        return t->status;
    }
    // Re-detect in the last box grown by 35% plus a small margin.
    double min_x, min_y, max_x, max_y; quad_bounds(t->quad, min_x, min_y, max_x, max_y);
    const double grow = 0.175 * std::max(max_x - min_x, max_y - min_y) + 0.02 * static_cast<double>(std::min(width, height));
    const double rx = std::max(0.0, min_x - grow), ry = std::max(0.0, min_y - grow);
    const double rw = std::min(static_cast<double>(width), max_x + grow) - rx, rh = std::min(static_cast<double>(height), max_y + grow) - ry;
    if (best_candidate_in(gray, width, height, stride, rx, ry, rw, rh, 3, t->quad, quad, score) && score >= gate) {
        match_corner_order(t->quad, quad);
        std::copy(quad, quad + 8, t->quad); t->score = score; t->missed_frames = 0; t->fresh = 1; ++t->sightings;
        if (score >= lock || t->sightings >= 2) t->status = CKEY_TRACK_LOCKED;
    } else if (++t->missed_frames > kTrackMaxMissed || t->status == CKEY_TRACK_TENTATIVE) {
        const double keep_gate = t->gate, keep_lock = t->lock_score;
        ckey_track_init(t); t->gate = keep_gate; t->lock_score = keep_lock;
    }
    return t->status;
}

void ckey_camera_policy_init(ckey_camera_policy *p) {
    if (!p) return;
    std::memset(p, 0, sizeof(*p));
    p->abi_version = CKEY_ABI_VERSION; p->last_point_x = p->last_point_y = 0.5;
}

ckey_status ckey_camera_advise_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride, const double *quad_xy, int decoded,
                                     ckey_camera_policy *policy, ckey_camera_advice *a) {
    if (a) { std::memset(a, 0, sizeof(*a)); a->abi_version = CKEY_ABI_VERSION; a->point_x = a->point_y = 0.5; a->zoom_factor = 1.0; }
    if (!gray || !policy || !a || width < 16 || height < 16 || stride < width || height > std::numeric_limits<size_t>::max() / stride) return CKEY_INVALID_ARGUMENT;
    const WorkScope work(kFindWorkBudget);
    bool candidate = false;
    if (quad_xy) { candidate = valid_quad(quad_xy); for (int i = 0; i < 8; ++i) candidate = candidate && std::isfinite(quad_xy[i]); }
    constexpr uint32_t kCycleAfter = 12;  // analysed frames without a decode before the bias moves on
    constexpr double kCycle[3] = {0.0, -1.0, -2.0};
    if (decoded) { policy->failed_frames = 0; policy->bias_step = 0; } else ++policy->failed_frames;
    if (candidate) {
        policy->lost_frames = 0;
        double cx = 0, cy = 0; for (int i = 0; i < 4; ++i) { cx += quad_xy[2 * i] / 4; cy += quad_xy[2 * i + 1] / 4; }
        const double side = quad_mean_side(quad_xy), shorter = static_cast<double>(std::min(width, height));
        a->has_candidate = 1; a->size = side / shorter;
        a->point_x = std::clamp(cx / static_cast<double>(width), 0.0, 1.0); a->point_y = std::clamp(cy / static_cast<double>(height), 0.0, 1.0);
        // Levels inside the candidate, on a grid of the canonical field.
        constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
        double h[8]; std::array<size_t, 256> histogram{}; size_t samples = 0, clipped = 0, outside = 0;
        if (solve_homography(canonical, quad_xy, h)) {
            for (int j = 0; j < 48; ++j) for (int i = 0; i < 48; ++i) {
                ImagePoint p{}; if (!map_point(h, 29.0 * (i + .5) / 48, 29.0 * (j + .5) / 48, p)) continue;
                if (p.x < 0 || p.y < 0 || p.x > static_cast<double>(width - 1) || p.y > static_cast<double>(height - 1)) { ++outside; continue; }
                const uint8_t v = gray[static_cast<size_t>(p.y) * stride + static_cast<size_t>(p.x)];
                ++histogram[v]; ++samples; if (v >= 250) ++clipped;
            }
        }
        double low = 0;
        if (samples) {
            const auto percentile = [&](double fraction) { const size_t target = static_cast<size_t>(static_cast<double>(samples - 1) * fraction); size_t total = 0;
                for (int v = 0; v < 256; ++v) { total += histogram[static_cast<size_t>(v)]; if (total > target) return static_cast<double>(v); } return 255.0; };
            low = percentile(0.05); const double high = percentile(0.95);
            a->contrast = high + low > 0 ? (high - low) / (high + low) : 0;
            a->clipped_fraction = static_cast<double>(clipped) / static_cast<double>(samples);
        }
        const double coarse = frame_edge_score(gray, width, height, stride, quad_xy, 4.0), fine = frame_edge_score(gray, width, height, stride, quad_xy, 1.0);
        a->sharpness = coarse > 0 ? std::clamp(fine / coarse, 0.0, 1.0) : 0;
        bool cut = outside > 0;
        for (int i = 0; i < 4; ++i) cut = cut || quad_xy[2 * i] < 2 || quad_xy[2 * i + 1] < 2 || quad_xy[2 * i] > static_cast<double>(width) - 3 || quad_xy[2 * i + 1] > static_cast<double>(height) - 3;
        if (cut) a->flags |= CKEY_ADVICE_MOVE_BACK;
        if (!decoded) {
            // A light background may clip harmlessly. The symbol is washed out
            // when its darkest marks are bright too: nothing is left to tell
            // ink from background until the exposure comes down.
            const bool washed = samples && low >= 150 && (a->contrast < 0.12 || a->clipped_fraction >= 0.6);
            if (washed) { a->exposure_bias_ev = low >= 200 ? -2.0 : -1.0; a->flags |= CKEY_ADVICE_GLARE; }
            if (a->sharpness < 0.35 && a->contrast > 0.15) a->flags |= policy->failed_frames > 6 ? CKEY_ADVICE_REFOCUS : CKEY_ADVICE_HOLD_STEADY;
            if (a->size < 0.22 && !cut) { a->zoom_factor = std::clamp(0.45 / std::max(a->size, 0.02), 1.0, 3.0); a->flags |= CKEY_ADVICE_MOVE_CLOSER; }
        }
        if (cut) a->zoom_factor = std::min(a->zoom_factor, 0.8);
    } else {
        ++policy->lost_frames;
        if (policy->lost_frames == 1 && !decoded) policy->bias_step = 0;  // candidate lost: restart the cycle
    }
    // While nothing decodes, walk the exposure bias through 0 / -1 / -2 EV.
    if (!decoded && policy->failed_frames >= kCycleAfter) {
        policy->bias_step = (policy->failed_frames / kCycleAfter) % 3;
        a->exposure_bias_ev = std::min(a->exposure_bias_ev, kCycle[policy->bias_step]);
    }
    if (std::hypot(a->point_x - policy->last_point_x, a->point_y - policy->last_point_y) > 0.08) {
        a->flags |= CKEY_ADVICE_REAIM; policy->last_point_x = a->point_x; policy->last_point_y = a->point_y;
    }
    return CKEY_OK;
}

}  // extern "C"
