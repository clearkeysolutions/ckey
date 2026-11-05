// Style policies and optical certification (specification §4; acceptance
// criteria 5 and 6): certification comes from a modelled scan, transparent
// output is never certified without the host's backdrop, adaptive repairs
// are reported, and override renders but does not certify.
#include "ckey/ckey.h"
#include "check.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

ckey_scalable_symbol encode(uint32_t version, const char *text, ckey_ecc_level level = CKEY_ECC_M) {
    ckey_scalable_symbol s{}; ckey_diagnostics d{};
    CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text), std::strlen(text), version, level, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
    return s;
}

bool renders(const ckey_scalable_symbol &s, const ckey_style &st, bool transparent = false) {
    ckey_raster_options o; ckey_raster_options_default(&o); o.size_px = 128; o.transparent = transparent ? 1 : 0;
    std::vector<uint8_t> px(128 * 128 * 4); size_t need = 0;
    return ckey_render_scalable_raster(&s, &st, &o, px.data(), px.size(), &need) == CKEY_OK;
}

}  // namespace

int main() {
    const char *url = "https://openckey.org/tour";
    ckey_style plain; ckey_style_default(&plain);
    CKEY_CHECK(plain.policy == CKEY_POLICY_STRICT);
    ckey_certification c{};

    // The default style certifies through the modelled scan at small, middle and largest versions.
    for (uint32_t v : {1u, 11u, 31u}) {
        const auto s = encode(v, url);
        const auto t0 = std::chrono::steady_clock::now();
        CKEY_CHECK(ckey_certify_scalable_style(&s, nullptr, 0, nullptr, nullptr, nullptr, &c) == CKEY_OK);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CKEY_CHECK(c.certified == 1 && c.trials == 3 && c.trials_passed == 3 && c.feature_separation > 0.5 && c.min_contrast > 20);
        std::printf("v%u plain: %s (%.0f ms)\n", v, c.message, ms);
    }
    const auto s = encode(6, url);

    // A coarser, blurrier model can fail a dense symbol: the verdict depends on the model, not the colours.
    {
        const auto dense = encode(31, url);
        ckey_scan_model hard; ckey_scan_model_default(&hard); hard.px_per_module = 2.5; hard.blur_px = 1.6;
        CKEY_CHECK(ckey_certify_scalable_style(&dense, nullptr, 0, nullptr, &hard, nullptr, &c) == CKEY_INVALID_ARGUMENT && c.certified == 0);
        std::printf("v31 under a 2.5 px/module blurred model: %s\n", c.message);
        hard.trials = 0; CKEY_CHECK(ckey_certify_scalable_style(&dense, nullptr, 0, nullptr, &hard, nullptr, &c) == CKEY_INVALID_ARGUMENT);
    }

    // Strict refuses low contrast everywhere: check, render, certify.
    ckey_style grey = plain; grey.ink = grey.ink_shade = {0xbb, 0xbb, 0xbb};
    ckey_diagnostics d{};
    CKEY_CHECK(ckey_check_scalable_style(&s, &grey, &d) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(!renders(s, grey));
    CKEY_CHECK(ckey_certify_scalable_style(&s, &grey, 0, nullptr, nullptr, nullptr, &c) == CKEY_INVALID_ARGUMENT && c.certified == 0);
    std::printf("strict grey: %s\n", c.message);

    // Override renders it, says so, and never certifies it.
    ckey_style forced = grey; forced.policy = CKEY_POLICY_OVERRIDE;
    CKEY_CHECK(ckey_check_scalable_style(&s, &forced, &d) == CKEY_OK && std::string(d.message).find("NOT scan-safe") != std::string::npos);
    CKEY_CHECK(renders(s, forced));
    CKEY_CHECK(ckey_certify_scalable_style(&s, &forced, 0, nullptr, nullptr, nullptr, &c) == CKEY_OK && c.certified == 0);
    std::printf("override grey: %s\n", c.message);
    ckey_style junk = forced; junk.modules = static_cast<ckey_module_shape>(9);
    CKEY_CHECK(!renders(s, junk));  // structural problems are refused under every policy
    junk = plain; junk.policy = 7; CKEY_CHECK(ckey_check_scalable_style(&s, &junk, &d) == CKEY_INVALID_ARGUMENT);

    // Adaptive repairs and reports; renderers draw the repaired style.
    ckey_style adaptive = grey; adaptive.policy = CKEY_POLICY_ADAPTIVE;
    adaptive.accents[0] = {0xfe, 0xfe, 0xfe}; adaptive.accent_count = 1; adaptive.accent_percent = 20;
    adaptive.shadow = 0.9;
    CKEY_CHECK(renders(s, adaptive));
    ckey_style repaired{};
    CKEY_CHECK(ckey_certify_scalable_style(&s, &adaptive, 0, nullptr, nullptr, &repaired, &c) == CKEY_OK && c.certified == 1);
    CKEY_CHECK((c.adjustments & CKEY_ADJUST_INK) && (c.adjustments & CKEY_ADJUST_ACCENTS) && (c.adjustments & CKEY_ADJUST_EFFECTS));
    CKEY_CHECK(repaired.ink.r == 0 && repaired.accent_percent == 0 && repaired.shadow <= 0.4 && repaired.policy == CKEY_POLICY_STRICT);
    CKEY_CHECK(ckey_check_scalable_style(&s, &repaired, &d) == CKEY_OK && renders(s, repaired));
    std::printf("adaptive grey: %s\n", c.message);

    // Transparent output: no backdrop, no certification (and no false claim).
    CKEY_CHECK(renders(s, plain, true));
    CKEY_CHECK(ckey_certify_scalable_style(&s, nullptr, 1, nullptr, nullptr, nullptr, &c) == CKEY_INVALID_ARGUMENT && c.certified == 0);
    CKEY_CHECK(std::string(c.message).find("background unknown") != std::string::npos);
    ckey_style over = plain; over.policy = CKEY_POLICY_OVERRIDE;
    CKEY_CHECK(ckey_certify_scalable_style(&s, &over, 1, nullptr, nullptr, nullptr, &c) == CKEY_OK && c.certified == 0);
    std::printf("transparent, no backdrop: %s\n", c.message);

    // With the host's backdrop: white passes; dark grey fails black ink strictly; adaptive flips the ink.
    const ckey_rgb white[1] = {{255, 255, 255}}, dark[1] = {{0x33, 0x33, 0x33}};
    ckey_backdrop on_white{white, 1, nullptr, 0, 0, 0}, on_dark{dark, 1, nullptr, 0, 0, 0};
    CKEY_CHECK(ckey_certify_scalable_style(&s, nullptr, 1, &on_white, nullptr, nullptr, &c) == CKEY_OK && c.certified == 1);
    CKEY_CHECK(ckey_certify_scalable_style(&s, nullptr, 1, &on_dark, nullptr, nullptr, &c) == CKEY_INVALID_ARGUMENT && c.certified == 0);
    std::printf("transparent black ink on #333333: %s\n", c.message);
    ckey_style flip = plain; flip.policy = CKEY_POLICY_ADAPTIVE;
    CKEY_CHECK(ckey_certify_scalable_style(&s, &flip, 1, &on_dark, nullptr, &repaired, &c) == CKEY_OK && c.certified == 1 && repaired.ink.r == 255);
    std::printf("adaptive on #333333: %s\n", c.message);

    // A busy host image: half black, half white. No single ink works, and certification says so.
    std::vector<uint8_t> photo(64 * 64 * 4, 255);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 32; ++x) { uint8_t *p = &photo[(y * 64 + x) * 4]; p[0] = p[1] = p[2] = 0; }
    ckey_backdrop busy{nullptr, 0, photo.data(), 64, 64, 0};
    CKEY_CHECK(ckey_certify_scalable_style(&s, nullptr, 1, &busy, nullptr, nullptr, &c) == CKEY_INVALID_ARGUMENT && c.certified == 0);
    std::printf("transparent on a half-black image: %s\n", c.message);

    // The concept preset (light on dark, depth, grain, accents) certifies opaque.
    ckey_style art; ckey_style_preset_fill(&art, CKEY_PRESET_CONCEPT);
    const auto s11 = encode(11, url);
    CKEY_CHECK(ckey_certify_scalable_style(&s11, &art, 0, nullptr, nullptr, nullptr, &c) == CKEY_OK && c.certified == 1);
    std::printf("concept preset: %s\n", c.message);
    std::printf("certification tests passed\n");
    return 0;
}
