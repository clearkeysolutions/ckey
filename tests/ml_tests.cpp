// ML locator runtime tests (0.1.0-build.87). White-box: includes the core
// translation unit.
//   - the CKML parser accepts a valid model and rejects every truncation,
//     header corruption and random file (a model can arrive as a data asset);
//   - ML proposals never let a wrong payload through: a model that proposes
//     garbage everywhere still yields only the true payload or no read;
//   - with no model, the ML option changes nothing;
//   - the gate skips the border search only when a model is loaded and sees
//     nothing, and ML-only (no gate) still falls back to the border search;
//   - multi-frame: frames too noisy to decode alone decode together, and two
//     different symbols alternating in one place never yield a wrong payload.
#include "../src/ckey.cpp"
#include "check.h"
#include "svg_raster.h"

#include <cstdio>
#include <random>
#include <chrono>

namespace {

struct Blob {
    std::vector<uint8_t> b;
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void f32(float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(v); }
};

// A tiny valid model with the 24-channel detection head: 64x64 input, stride 4.
//   b1 = conv3x3 s2 (1->8, relu)           32x32
//   b2 = depthwise3x3 s2 (8, relu)          16x16
//   b3 = up2add(b2, b1)                     32x32
//   b4 = add(b3, b1)                        32x32
//   b5 = conv1x1 s2 (8->24)                 16x16  (bias sets the heatmaps)
std::vector<uint8_t> tiny_model(float centre_bias) {
    Blob m; m.b = {'C', 'K', 'M', 'L'};
    m.u32(2); m.u32(6); m.u32(5); m.u32(64); m.u32(4); m.u32(24);
    auto conv = [&](uint32_t src, uint32_t dst, uint32_t cin, uint32_t cout, uint32_t k, uint32_t s, uint32_t g, uint32_t relu, float w, std::vector<float> bias) {
        m.u32(1); for (uint32_t v : {src, dst, cin, cout, k, s, g, relu}) m.u32(v);
        const size_t n = static_cast<size_t>(cout) * (cin / g) * k * k;
        for (size_t i = 0; i < n; ++i) m.b.push_back(static_cast<uint8_t>(static_cast<int8_t>((i * 37) % 255 - 127)));
        for (uint32_t o = 0; o < cout; ++o) m.f32(w);
        for (uint32_t o = 0; o < cout; ++o) m.f32(o < bias.size() ? bias[o] : 0.0f);
    };
    conv(0, 1, 1, 8, 3, 2, 1, 1, 0.01f, {});
    conv(1, 2, 8, 8, 3, 2, 8, 1, 0.01f, {});
    m.u32(3); m.u32(2); m.u32(1); m.u32(3);
    m.u32(2); m.u32(3); m.u32(1); m.u32(4);
    std::vector<float> bias(24, 0.0f); bias[0] = centre_bias; for (int k = 1; k < 5; ++k) bias[k] = centre_bias;
    conv(4, 5, 8, 24, 1, 2, 1, 0, 0.0001f, bias);
    return m.b;
}


// A tiny valid corner refiner: 32 x 32 input, five stride-2 convolutions to 1 x 1,
// then 8 outputs whose bias is `shift` (canonical units moved per round).
std::vector<uint8_t> tiny_refiner(float shift) {
    Blob m; m.b = {'C', 'K', 'M', 'L'};
    m.u32(2); m.u32(7); m.u32(6); m.u32(32); m.u32(32); m.u32(8);
    auto conv = [&](uint32_t src, uint32_t dst, uint32_t cin, uint32_t cout, uint32_t k, uint32_t s, uint32_t g, uint32_t relu, float w, float bias) {
        m.u32(1); for (uint32_t v : {src, dst, cin, cout, k, s, g, relu}) m.u32(v);
        const size_t n = static_cast<size_t>(cout) * (cin / g) * k * k;
        for (size_t i = 0; i < n; ++i) m.b.push_back(static_cast<uint8_t>(static_cast<int8_t>((i * 29) % 255 - 127)));
        for (uint32_t o = 0; o < cout; ++o) m.f32(w);
        for (uint32_t o = 0; o < cout; ++o) m.f32(bias);
    };
    conv(0, 1, 1, 4, 3, 2, 1, 1, 0.01f, 0);
    for (uint32_t i = 1; i < 5; ++i) conv(i, i + 1, 4, 4, 3, 2, 4, 1, 0.01f, 0);
    conv(5, 6, 4, 8, 1, 1, 1, 0, 0.0f, shift);
    return m.b;
}

std::string render(const ckey_scalable_symbol &s) {
    size_t n = 0; CKEY_CHECK(ckey_render_scalable_svg(&s, 8, nullptr, 0, &n) == CKEY_INSUFFICIENT_BUFFER);
    std::string svg(n, '\0'); CKEY_CHECK(ckey_render_scalable_svg(&s, 8, svg.data(), n, &n) == CKEY_OK);
    svg.resize(n - 1); return svg;
}

}  // namespace

int main() {
    // ---- Parser: valid model, truncations, corruptions, random files ----
    const std::vector<uint8_t> good = tiny_model(-8.0f);
    std::string err;
    CKEY_CHECK(ml_parse(good.data(), good.size(), err) != nullptr);
    for (size_t n = 0; n < good.size(); ++n) CKEY_CHECK(ml_parse(good.data(), n, err) == nullptr);
    {
        std::vector<uint8_t> longer = good; longer.push_back(0);
        CKEY_CHECK(ml_parse(longer.data(), longer.size(), err) == nullptr);
    }
    // Every single-byte change in the header and op descriptors either is
    // rejected or still describes a consistent graph; parsing never crashes.
    for (size_t i = 0; i < 64 && i < good.size(); ++i) for (int bit = 0; bit < 8; ++bit) {
        std::vector<uint8_t> c = good; c[i] ^= static_cast<uint8_t>(1u << bit);
        auto m = ml_parse(c.data(), c.size(), err);
        if (m) { std::vector<float> in(static_cast<size_t>(m->side) * m->side, 0.0f); (void)ml_forward(*m, in); }
    }
    {
        uint32_t s = 12345;
        for (int it = 0; it < 2000; ++it) {
            std::vector<uint8_t> r(32 + it % 400);
            for (auto &x : r) { s = s * 1664525u + 1013904223u; x = static_cast<uint8_t>(s >> 24); }
            if (it % 2) std::memcpy(r.data(), "CKML", 4);
            CKEY_CHECK(ml_parse(r.data(), r.size(), err) == nullptr);
        }
    }
    CKEY_CHECK(ckey_ml_load_model(good.data(), good.size() - 1) == CKEY_INVALID_ARGUMENT);

    // ---- A symbol in a frame ----
    const char *text = "https://openckey.org/ml";
    ckey_scalable_symbol s{}; ckey_diagnostics d{};
    CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text), std::strlen(text), 4, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
    const int side = 300, W = 480, H = 400, ox = 90, oy = 50;
    const std::vector<uint8_t> sym = svgtest::rasterize(svgtest::parse(render(s)), side, side, 3);
    std::vector<uint8_t> frame(static_cast<size_t>(W) * H, 120);
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) frame[static_cast<size_t>(y + oy) * W + x + ox] = sym[static_cast<size_t>(y) * side + x];
    std::vector<uint8_t> out(4096);
    auto scan = [&](uint32_t flags, ckey_scan_result &r) {
        ckey_scan_options o; ckey_scan_options_init(&o); o.flags = flags; r = ckey_scan_result{};
        return ckey_scan_frame_gray8(frame.data(), W, H, W, &o, out.data(), out.size(), &r);
    };
    auto is_text = [&](const ckey_scan_result &r) { return r.payload_length == std::strlen(text) && std::memcmp(out.data(), text, r.payload_length) == 0; };

#if defined(CKEY_ML_HAVE_BUILTIN_SITES)
    // ---- The built-in site model loads, and the symbol still reads with it ----
    CKEY_CHECK(ckey_ml_load_model(nullptr, 0) == CKEY_OK);
    CKEY_CHECK(ckey_ml_site_model_id() != 0);
#endif
    // ---- Clearing removes every model, built-ins included; NULL restores them ----
    ckey_ml_clear_models();
    CKEY_CHECK(ckey_ml_model_id() == 0 && ckey_ml_site_model_id() == 0);
    CKEY_CHECK(ckey_ml_load_model(nullptr, 0) == CKEY_OK);
#if defined(CKEY_ML_HAVE_BUILTIN_SITES)
    CKEY_CHECK(ckey_ml_site_model_id() != 0);
#endif
    // ---- No locator: the ML option changes nothing ----
    CKEY_CHECK(ckey_ml_load_model(nullptr, 0) == CKEY_OK);
    ckey_scan_result classical{}, with_ml{};
    CKEY_CHECK(scan(0, classical) == CKEY_OK && is_text(classical));
#if !defined(CKEY_ML_HAVE_BUILTIN_LOCATOR)
    CKEY_CHECK(ckey_ml_model_id() == 0);
    CKEY_CHECK(scan(CKEY_SCAN_OPTION_ML | CKEY_SCAN_OPTION_ML_GATE, with_ml) == CKEY_OK && is_text(with_ml));
    CKEY_CHECK(!(with_ml.flags & CKEY_SCAN_FLAG_ML));
    ckey_ml_detection none[4];
    CKEY_CHECK(ckey_ml_locate_gray8(frame.data(), W, H, W, 0.1, none, 4) == 0);
#endif

    // ---- A model that proposes garbage everywhere: no wrong payload, and the
    //      border search still reads the symbol ----
    const std::vector<uint8_t> noisy = tiny_model(8.0f);
    CKEY_CHECK(ckey_ml_load_model(noisy.data(), noisy.size()) == CKEY_OK);
    CKEY_CHECK(ckey_ml_model_id() != 0);
    ckey_ml_detection det[8];
    const size_t n = ckey_ml_locate_gray8(frame.data(), W, H, W, 0.3, det, 8);
    CKEY_CHECK(n >= 1 && n <= 8);
    for (size_t i = 0; i < n; ++i) for (double v : det[i].quad) CKEY_CHECK(std::isfinite(v));
    CKEY_CHECK(scan(CKEY_SCAN_OPTION_ML, with_ml) == CKEY_OK && is_text(with_ml));
    CKEY_CHECK(scan(CKEY_SCAN_OPTION_ML | CKEY_SCAN_OPTION_ML_GATE, with_ml) == CKEY_OK && is_text(with_ml));
    // ML_AFTER: a frame the border search reads never uses a proposal.
    CKEY_CHECK(scan(CKEY_SCAN_OPTION_ML_AFTER, with_ml) == CKEY_OK && is_text(with_ml));
    CKEY_CHECK(!(with_ml.flags & CKEY_SCAN_FLAG_ML));
    // Blank frames with the noisy model: proposals, but never a payload.
    {
        std::vector<uint8_t> blank(static_cast<size_t>(W) * H);
        uint32_t r = 99; for (auto &x : blank) { r = r * 1664525u + 1013904223u; x = static_cast<uint8_t>(100 + (r >> 26)); }
        ckey_scan_options o; ckey_scan_options_init(&o); o.flags = CKEY_SCAN_OPTION_ML; ckey_scan_result res{};
        CKEY_CHECK(ckey_scan_frame_gray8(blank.data(), W, H, W, &o, out.data(), out.size(), &res) != CKEY_OK);
        o.flags = CKEY_SCAN_OPTION_ML_AFTER; res = ckey_scan_result{};
        CKEY_CHECK(ckey_scan_frame_gray8(blank.data(), W, H, W, &o, out.data(), out.size(), &res) != CKEY_OK);
    }

    // ---- A model that sees nothing: the gate skips the search, ML alone falls back ----
    CKEY_CHECK(ckey_ml_load_model(good.data(), good.size()) == CKEY_OK);
    CKEY_CHECK(ckey_ml_locate_gray8(frame.data(), W, H, W, 0.1, det, 8) == 0);
    CKEY_CHECK(scan(CKEY_SCAN_OPTION_ML, with_ml) == CKEY_OK && is_text(with_ml));
    CKEY_CHECK(scan(CKEY_SCAN_OPTION_ML | CKEY_SCAN_OPTION_ML_GATE, with_ml) != CKEY_OK);
    CKEY_CHECK(std::strstr(with_ml.message, "ML locator") != nullptr);


    // ---- Multi-frame accumulation ----
    {
        CKEY_CHECK(ckey_ml_load_model(nullptr, 0) == CKEY_OK);
        const char *texts[2] = {"https://openckey.org/ml/accumulate", "https://openckey.org/ml/other-one"};
        std::vector<uint8_t> raster[2];
        for (int t = 0; t < 2; ++t) {
            ckey_scalable_symbol a{};
            CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(texts[t]), std::strlen(texts[t]), 8, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &a, &d) == CKEY_OK);
            raster[t] = svgtest::rasterize(svgtest::parse(render(a)), 320, 320, 3);
        }
        const int FW = 480, FH = 420, fx = 80, fy = 50;
        std::mt19937 rng(1); std::normal_distribution<double> noise(0, 60);
        auto noisy = [&](int t) {
            std::vector<uint8_t> fr(static_cast<size_t>(FW) * FH);
            for (int y = 0; y < FH; ++y) for (int x = 0; x < FW; ++x) {
                double v = 128;
                if (x >= fx && y >= fy && x < fx + 320 && y < fy + 320) v = 128 + 60 * (raster[t][static_cast<size_t>(y - fy) * 320 + x - fx] - 128.0) / 128.0;
                fr[static_cast<size_t>(y) * FW + x] = static_cast<uint8_t>(std::clamp(v + noise(rng), 0.0, 255.0));
            }
            return fr;
        };
        ckey_frame_accumulator *acc = ckey_accumulator_create();
        CKEY_CHECK(acc && ckey_accumulator_frames(acc) == 0);
        ckey_scan_options o; ckey_scan_options_init(&o);
        int decoded_at = -1, single = 0;
        for (int f = 0; f < 8 && decoded_at < 0; ++f) {
            const auto fr = noisy(0);
            ckey_scan_result r{};
            single += ckey_scan_frame_gray8(fr.data(), FW, FH, FW, &o, out.data(), out.size(), &r) == CKEY_OK;
            const ckey_status st = ckey_scan_frame_accumulate_gray8(fr.data(), FW, FH, FW, &o, acc, out.data(), out.size(), &r);
            if (st == CKEY_OK) {
                CKEY_CHECK(r.payload_length == std::strlen(texts[0]) && std::memcmp(out.data(), texts[0], r.payload_length) == 0);
                CKEY_CHECK(r.flags & CKEY_SCAN_FLAG_ACCUMULATED);
                CKEY_CHECK(ckey_accumulator_frames(acc) == 0);
                decoded_at = f;
            }
        }
        CKEY_CHECK(single == 0);            // no frame decodes alone ...
        CKEY_CHECK(decoded_at >= 1);        // ... but together they do
        // Region-of-interest frames accumulate too (the SDK's default).
        {
            ckey_frame_accumulator *ra = ckey_accumulator_create();
            ckey_scan_options ro; ckey_scan_options_init(&ro);
            ro.roi_x = fx - 30; ro.roi_y = fy - 30; ro.roi_width = 380; ro.roi_height = 380;
            int at = -1, alone = 0;
            for (int f = 0; f < 8 && at < 0; ++f) {
                const auto fr = noisy(0);
                ckey_scan_result r{};
                alone += ckey_scan_frame_gray8(fr.data(), FW, FH, FW, &ro, out.data(), out.size(), &r) == CKEY_OK;
                if (ckey_scan_frame_accumulate_gray8(fr.data(), FW, FH, FW, &ro, ra, out.data(), out.size(), &r) == CKEY_OK) {
                    CKEY_CHECK(r.payload_length == std::strlen(texts[0]) && std::memcmp(out.data(), texts[0], r.payload_length) == 0);
                    // Corners are reported in full-frame pixels, inside the region.
                    for (int k = 0; k < 4; ++k) CKEY_CHECK(r.quad[2 * k] >= ro.roi_x - 2 && r.quad[2 * k] <= ro.roi_x + ro.roi_width + 2);
                    at = f;
                }
            }
            CKEY_CHECK(alone == 0 && at >= 1);
            ckey_accumulator_destroy(ra);
        }
        // Two different symbols alternating in the same place: only a true payload, never a mix.
        ckey_accumulator_reset(acc);
        for (int f = 0; f < 16; ++f) {
            const auto fr = noisy(f % 2);
            ckey_scan_result r{};
            if (ckey_scan_frame_accumulate_gray8(fr.data(), FW, FH, FW, &o, acc, out.data(), out.size(), &r) == CKEY_OK) {
                const bool a = r.payload_length == std::strlen(texts[0]) && std::memcmp(out.data(), texts[0], r.payload_length) == 0;
                const bool b = r.payload_length == std::strlen(texts[1]) && std::memcmp(out.data(), texts[1], r.payload_length) == 0;
                CKEY_CHECK(a || b);
            }
        }
        ckey_scan_result r{};
        CKEY_CHECK(ckey_scan_frame_accumulate_gray8(nullptr, FW, FH, FW, &o, acc, out.data(), out.size(), &r) == CKEY_INVALID_ARGUMENT);
        ckey_accumulator_destroy(acc);
        ckey_accumulator_destroy(nullptr);
    }


    // ---- Corner refiner: recognised, used only after a failed decode, never a wrong payload ----
    {
        ckey_ml_clear_models();
        const std::vector<uint8_t> still = tiny_refiner(0.0f), pushy = tiny_refiner(2.5f);
        std::string e2;
        auto rm = ml_parse(still.data(), still.size(), e2);
        CKEY_CHECK(rm && rm->kind == kMlRefiner);
        for (size_t n2 = 0; n2 < still.size(); ++n2) CKEY_CHECK(ml_parse(still.data(), n2, e2) == nullptr);
        CKEY_CHECK(ckey_ml_load_model(pushy.data(), pushy.size()) == CKEY_OK && ckey_ml_refiner_model_id() != 0);
        ckey_scan_result rr{};
        CKEY_CHECK(scan(0, rr) == CKEY_OK && is_text(rr));          // a decode that works is not touched
        double q[8]; std::copy(classical.quad, classical.quad + 8, q);
        double moved[8]; CKEY_CHECK(ml_refine_quad(frame.data(), W, H, W, q, moved));
        bool differs = false; for (int k = 0; k < 8; ++k) differs = differs || std::abs(moved[k] - q[k]) > 1;
        CKEY_CHECK(differs);
        // From far-off corners: either the true payload or nothing.
        double off[8]; for (int k = 0; k < 8; ++k) off[k] = q[k] + ((k % 3) - 1) * 9.0;
        size_t len2 = 0; uint32_t v2 = 0, l2 = 0; ckey_diagnostics d2{};
        if (ckey_decode_scalable_gray8(frame.data(), W, H, W, off, out.data(), out.size(), &len2, &v2, &l2, &d2) == CKEY_OK)
            CKEY_CHECK(len2 == std::strlen(text) && std::memcmp(out.data(), text, len2) == 0);
        ckey_ml_clear_models();
        CKEY_CHECK(ckey_ml_refiner_model_id() == 0);
    }

    // ---- Version hint: only the order of attempts changes ----
    {
        ckey_ml_clear_models();
        for (uint32_t hint : {4u, 3u, 5u, 20u, 31u}) {
            ckey_scan_result rh{};
            CKEY_CHECK(scan(CKEY_SCAN_VERSION_HINT(hint), rh) == CKEY_OK && is_text(rh));
        }
        CKEY_CHECK(CKEY_SCAN_VERSION_HINT_OF(CKEY_SCAN_VERSION_HINT(17) | CKEY_SCAN_OPTION_ML) == 17);
        CKEY_CHECK(ckey_ml_load_model(nullptr, 0) == CKEY_OK);
    }

    // ---- Invalid arguments ----
    CKEY_CHECK(ckey_ml_locate_gray8(nullptr, W, H, W, 0.3, det, 8) == 0);
    CKEY_CHECK(ckey_ml_locate_gray8(frame.data(), W, H, W - 1, 0.3, det, 8) == 0);
    CKEY_CHECK(ckey_ml_locate_gray8(frame.data(), W, H, W, std::nan(""), det, 8) == 0);
    CKEY_CHECK(ckey_ml_load_model(nullptr, 0) == CKEY_OK);
    // CKEY_SCAN_OPTION_FAST: a readable frame still decodes, without the
    // locator, and a frame that will not read is not slower than the full ladder.
    {
        std::vector<uint8_t> payload(300); for (size_t k = 0; k < payload.size(); ++k) payload[k] = static_cast<uint8_t>('a' + k % 23);
        ckey_scalable_symbol sym{}; ckey_diagnostics diag{};
        CKEY_CHECK(ckey_encode_scalable(payload.data(), payload.size(), 0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &sym, &diag) == CKEY_OK);
        ckey_raster_options ro; ckey_raster_options_default(&ro); ro.size_px = 600; ro.format = CKEY_PIXELS_GRAY8;
        std::vector<uint8_t> img(600 * 600); size_t need = 0;
        CKEY_CHECK(ckey_render_scalable_raster(&sym, nullptr, &ro, img.data(), img.size(), &need) == CKEY_OK);
        const size_t W = 900; std::vector<uint8_t> frame(W * W, 230);
        for (size_t y = 0; y < 600; ++y) std::copy_n(&img[y * 600], 600, &frame[(y + 150) * W + 150]);
        ckey_scan_options so; ckey_scan_options_init(&so); so.flags = CKEY_SCAN_OPTION_ML_AFTER | CKEY_SCAN_OPTION_FAST; so.max_candidates = 2;
        std::vector<uint8_t> out(4096); ckey_scan_result r{}; r.abi_version = CKEY_ABI_VERSION;
        CKEY_CHECK(ckey_scan_frame_gray8(frame.data(), W, W, W, &so, out.data(), out.size(), &r) == CKEY_OK && r.payload_length == payload.size());
        CKEY_CHECK((r.flags & CKEY_SCAN_FLAG_ML) == 0);
        std::vector<uint8_t> smeared(frame);
        for (size_t y = 0; y < W; ++y) for (size_t x = 0; x + 14 < W; ++x) { unsigned acc = 0; for (int k = 0; k < 14; ++k) acc += frame[y * W + x + k]; smeared[y * W + x] = static_cast<uint8_t>(acc / 14); }
        const auto timed = [&](uint32_t flags, size_t cands) {
            ckey_scan_options o; ckey_scan_options_init(&o); o.flags = flags; o.max_candidates = static_cast<uint32_t>(cands);
            ckey_scan_result rr{}; rr.abi_version = CKEY_ABI_VERSION;
            const auto t0 = std::chrono::steady_clock::now();
            const ckey_status st = ckey_scan_frame_gray8(smeared.data(), W, W, W, &o, out.data(), out.size(), &rr);
            CKEY_CHECK(st != CKEY_OK || rr.payload_length == payload.size());
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        };
        const double full = timed(CKEY_SCAN_OPTION_ML_AFTER, 6), fast = timed(CKEY_SCAN_OPTION_ML_AFTER | CKEY_SCAN_OPTION_FAST, 2);
        std::printf("fast scan of a smeared frame: %.0f ms (full ladder %.0f ms)\n", fast, full);
        CKEY_CHECK(fast <= full * 1.1 + 5);
        // ckey_read_sites_gray8: the clean frame re-reads with (almost) every
        // site as printed, in any corner order; the smeared one reads worse.
        size_t sites = 0; uint32_t errors = 0, smearedErrors = 0;
        CKEY_CHECK(ckey_read_sites_gray8(frame.data(), W, W, W, r.quad, &sym, nullptr, nullptr, 0, &sites, nullptr) == CKEY_INSUFFICIENT_BUFFER);
        size_t mapped = 0; (void)ckey_ml_site_map(&sym, nullptr, nullptr, nullptr, 0, &mapped);
        CKEY_CHECK(mapped == sites && sites > 0);
        std::vector<uint8_t> correct(sites); std::vector<float> confidence(sites);
        CKEY_CHECK(ckey_read_sites_gray8(frame.data(), W, W, W, r.quad, &sym, correct.data(), confidence.data(), sites, &sites, &errors) == CKEY_OK);
        CKEY_CHECK(errors * 100 < sites);
        double rotated[8]; for (int k = 0; k < 8; ++k) rotated[k] = r.quad[(k + 2) % 8];
        uint32_t rotatedErrors = 0;
        CKEY_CHECK(ckey_read_sites_gray8(frame.data(), W, W, W, rotated, &sym, correct.data(), confidence.data(), sites, &sites, &rotatedErrors) == CKEY_OK);
        CKEY_CHECK(rotatedErrors == errors);
        CKEY_CHECK(ckey_read_sites_gray8(smeared.data(), W, W, W, r.quad, &sym, correct.data(), confidence.data(), sites, &sites, &smearedErrors) == CKEY_OK);
        std::printf("site re-read: %u of %zu wrong clean, %u smeared\n", errors, sites, smearedErrors);
        CKEY_CHECK(smearedErrors > errors);
    }
    std::puts("ml tests passed");
    return 0;
}
