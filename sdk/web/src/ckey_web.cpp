// CKey Web SDK bridge: a thin, malloc-friendly C surface over the public C ABI
// (include/ckey/ckey.h, include/ckey/telemetry.h) for the JavaScript wrapper in
// sdk/web/js/. It never reimplements encoding, decoding or rendering; it only
// marshals arguments and orchestrates the reader pipeline that
// ios/CKeyReader/CameraScanner.swift uses (find borders -> refine per field ->
// decode, Profile 0 first).
//
// Conventions for JS callers:
//  * Every parameter and result is an int, uint32, double or pointer: no
//    64-bit integers cross the boundary (times and batch ids are doubles,
//    which are exact up to 2^53), so no BigInt support is needed.
//  * Functions that produce variable-length output return a malloc'd buffer
//    (or 0 on failure) and set ckw_last_len(); the caller copies it out and
//    frees it with ckw_free().
//  * On failure ckw_last_error() holds a NUL-terminated message (from the
//    core's diagnostics where it gives one).

#include "ckey/ckey.h"
#ifndef CKW_NO_TELEMETRY
#include "ckey/telemetry.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <memory>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define CKW_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define CKW_EXPORT extern "C"
#endif

namespace {

char g_error[300] = "";
size_t g_last_len = 0;
double g_last_batch_id = 0;

void set_error(const char *msg) {
    std::snprintf(g_error, sizeof g_error, "%s", msg ? msg : "");
}

void set_error_diag(const ckey_diagnostics &d, const char *fallback) {
    if (d.message[0]) {
        char tmp[sizeof d.message + 1];
        std::memcpy(tmp, d.message, sizeof d.message);
        tmp[sizeof d.message] = '\0';
        set_error(tmp);
    } else {
        set_error(fallback);
    }
}

// A style plus storage for its logo URL (ckey_style holds a borrowed pointer).
struct WebStyle {
    ckey_style style;
    std::string logo_href;
};

uint8_t *dup_bytes(const void *src, size_t n) {
    uint8_t *out = static_cast<uint8_t *>(std::malloc(n ? n : 1));
    if (!out) { set_error("out of memory"); return nullptr; }
    if (n) std::memcpy(out, src, n);
    g_last_len = n;
    return out;
}

uint64_t to_ms(double v) {
    if (!(v > 0)) return 0;  // also NaN
    if (v >= 18446744073709549568.0) return UINT64_MAX;
    return static_cast<uint64_t>(v);
}

const ckey_style *style_of(const WebStyle *ws, ckey_style &fallback) {
    if (ws) return &ws->style;
    ckey_style_default(&fallback);
    return &fallback;
}

}  // namespace

// ---- Memory and errors ------------------------------------------------------

CKW_EXPORT void *ckw_malloc(uint32_t n) { return std::malloc(n ? n : 1); }
CKW_EXPORT void ckw_free(void *p) { std::free(p); }
CKW_EXPORT const char *ckw_last_error(void) { return g_error; }
CKW_EXPORT uint32_t ckw_last_len(void) { return static_cast<uint32_t>(g_last_len); }
CKW_EXPORT const char *ckw_software_version(void) { return ckey_get_software_version(); }
CKW_EXPORT uint32_t ckw_abi_version(void) { return ckey_get_abi_version(); }

// ---- Safe Actions -------------------------------------------------------------

// Parses (key == null) or verifies a Safe Action and returns a malloc'd JSON
// object of its fields, or null with the reason in ckw_last_error(). The status
// is in "status": 0 OK, 6 bad signature, 7 expired. Every text field is limited by
// the core to a JSON-safe character set; params are hexadecimal.
CKW_EXPORT char *ckw_safe_action(const uint8_t *payload, uint32_t len, const uint8_t *key, double now, uint32_t max_lifetime) {
    auto sa = std::make_unique<ckey_safe_action>();
    ckey_diagnostics diag{};
    const ckey_status st = key
        ? ckey_safe_action_verify(payload, len, key, static_cast<uint64_t>(now), max_lifetime, sa.get(), &diag)
        : ckey_safe_action_parse(payload, len, sa.get(), &diag);
    if (st != CKEY_OK) { set_error_diag(diag, "not a valid Safe Action"); return nullptr; }
    static const char hexd[] = "0123456789abcdef";
    std::string params;
    for (size_t i = 0; i < sa->params_len; ++i) { params.push_back(hexd[sa->params[i] >> 4]); params.push_back(hexd[sa->params[i] & 15]); }
    std::string json = std::string("{\"scope\":") + std::to_string(sa->scope) + ",\"issuer\":\"" + sa->issuer + "\",\"app\":\"" + sa->app +
                       "\",\"action\":\"" + sa->action + "\",\"key_id\":\"" + sa->key_id + "\",\"request_id\":\"" + sa->request_id +
                       "\",\"issued_at\":" + std::to_string(sa->issued_at) + ",\"expires_at\":" + std::to_string(sa->expires_at) +
                       ",\"params\":\"" + params + "\"}";
    char *out = static_cast<char *>(std::malloc(json.size() + 1));
    if (!out) { set_error("out of memory"); return nullptr; }
    std::memcpy(out, json.c_str(), json.size() + 1);
    g_last_len = json.size();
    return out;
}

// ---- Symbols ----------------------------------------------------------------

CKW_EXPORT uint32_t ckw_scalable_capacity(uint32_t version, uint32_t level, uint32_t layout) {
    return static_cast<uint32_t>(ckey_scalable_capacity(version, static_cast<ckey_ecc_level>(level),
                                                        static_cast<ckey_scalable_layout>(layout)));
}

CKW_EXPORT uint32_t ckw_scalable_modules_across(uint32_t version) { return ckey_scalable_modules_across(version); }

// Encodes a Profile 0 symbol. Returns a heap ckey_scalable_symbol (free with
// ckw_symbol_free) or 0 with ckw_last_error() set.
CKW_EXPORT ckey_scalable_symbol *ckw_encode(const uint8_t *payload, uint32_t len, uint32_t version,
                                            uint32_t level, uint32_t layout) {
    auto *sym = static_cast<ckey_scalable_symbol *>(std::calloc(1, sizeof(ckey_scalable_symbol)));
    if (!sym) { set_error("out of memory"); return nullptr; }
    ckey_diagnostics d{};
    const ckey_status st = ckey_encode_scalable(payload, len, version, static_cast<ckey_ecc_level>(level),
                                                static_cast<ckey_scalable_layout>(layout), sym, &d);
    if (st != CKEY_OK) {
        set_error_diag(d, "encode failed");
        std::free(sym);
        return nullptr;
    }
    return sym;
}

CKW_EXPORT void ckw_symbol_free(ckey_scalable_symbol *sym) { std::free(sym); }

// out[0..4] = version, ecc_level, layout, payload_length, codeword_length.
CKW_EXPORT void ckw_symbol_info(const ckey_scalable_symbol *sym, uint32_t *out) {
    if (!sym || !out) return;
    out[0] = sym->version;
    out[1] = sym->ecc_level;
    out[2] = sym->layout;
    out[3] = sym->payload_length;
    out[4] = sym->codeword_length;
}

// ---- Styles -----------------------------------------------------------------

// Returns a style filled from a preset (0 plain, 1 concept), or 0.
CKW_EXPORT WebStyle *ckw_style_new(uint32_t preset) {
    auto *ws = new (std::nothrow) WebStyle();
    if (!ws) { set_error("out of memory"); return nullptr; }
    if (ckey_style_preset_fill(&ws->style, static_cast<ckey_style_preset>(preset)) != CKEY_OK) {
        set_error("unknown style preset");
        delete ws;
        return nullptr;
    }
    return ws;
}

CKW_EXPORT void ckw_style_free(WebStyle *ws) { delete ws; }

// Colour fields: 0 background, 1 background_edge, 2 ink, 3 ink_shade, 4..7 accents[0..3].
CKW_EXPORT int ckw_style_set_rgb(WebStyle *ws, uint32_t field, uint32_t r, uint32_t g, uint32_t b) {
    if (!ws || r > 255 || g > 255 || b > 255) return 0;
    ckey_rgb c{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    ckey_style &s = ws->style;
    switch (field) {
    case 0: s.background = c; break;
    case 1: s.background_edge = c; break;
    case 2: s.ink = c; break;
    case 3: s.ink_shade = c; break;
    case 4: case 5: case 6: case 7: s.accents[field - 4] = c; break;
    default: return 0;
    }
    return 1;
}

// Numeric fields: 0 accent_count, 1 accent_percent, 2 modules, 3 rings, 4 rays,
// 5 bevel, 6 shadow, 7 texture, 8 logo_radius, 9 logo_plate, 10 policy.
// Range checks beyond the type are the core style check's job.
CKW_EXPORT int ckw_style_set_num(WebStyle *ws, uint32_t field, double v) {
    if (!ws || !std::isfinite(v)) return 0;
    ckey_style &s = ws->style;
    const auto u = [&](uint32_t &dst) { if (v < 0 || v > 4294967295.0) return 0; dst = static_cast<uint32_t>(v); return 1; };
    switch (field) {
    case 0: return u(s.accent_count);
    case 1: return u(s.accent_percent);
    case 2: if (v < 0 || v > 3) return 0; s.modules = static_cast<ckey_module_shape>(static_cast<int>(v)); return 1;
    case 3: if (v < 0 || v > 1) return 0; s.rings = static_cast<ckey_ring_style>(static_cast<int>(v)); return 1;
    case 4: if (v < 0 || v > 1) return 0; s.rays = static_cast<ckey_ray_cap>(static_cast<int>(v)); return 1;
    case 5: s.bevel = v; return 1;
    case 6: s.shadow = v; return 1;
    case 7: s.texture = v; return 1;
    case 8: s.logo_radius = v; return 1;
    case 9: return u(s.logo_plate);
    case 10:
#if CKEY_ABI_VERSION >= 4
        return u(s.policy);
#else
        return v == 0 ? 1 : 0;  // this core has only the strict behaviour
#endif
    default: return 0;
    }
}

CKW_EXPORT int ckw_style_set_logo(WebStyle *ws, const char *href) {
    if (!ws) return 0;
    ws->logo_href = href ? href : "";
    ws->style.logo_href = ws->logo_href.empty() ? nullptr : ws->logo_href.c_str();
    return 1;
}

// CKEY_OK (0) or a status with ckw_last_error() explaining the refusal.
CKW_EXPORT int ckw_style_check(const ckey_scalable_symbol *sym, const WebStyle *ws) {
    ckey_style fallback;
    ckey_diagnostics d{};
    const ckey_status st = ckey_check_scalable_style(sym, style_of(ws, fallback), &d);
    if (st != CKEY_OK) set_error_diag(d, "style check failed");
    else set_error_diag(d, "");
    return st;
}

// ---- Rendering --------------------------------------------------------------
// Renderers apply the style's own policy (strict refuses, adaptive repairs,
// override draws as given), so the bridge does not pre-check the style; when
// a render is refused it asks the style check for the reason.

namespace {
void explain_refusal(const ckey_scalable_symbol *sym, const WebStyle *ws, const char *fallback) {
    ckey_style fallback_style;
    ckey_diagnostics d{};
    if (ckey_check_scalable_style(sym, style_of(ws, fallback_style), &d) != CKEY_OK) set_error_diag(d, fallback);
    else set_error(fallback);
}
}  // namespace

// Renders RGBA8 (format 0) or GRAY8 (format 1) pixels: size x size, rows top to
// bottom. logo_rgba (optional, straight RGBA8, logo_w x logo_h) is drawn into
// the style's logo circle. Returns malloc'd pixels or 0.
CKW_EXPORT uint8_t *ckw_render_raster(const ckey_scalable_symbol *sym, const WebStyle *ws, uint32_t size,
                                      uint32_t policy, uint32_t transparent, uint32_t supersample,
                                      double min_feature_px, uint32_t materials, uint32_t format,
                                      const uint8_t *logo_rgba, uint32_t logo_w, uint32_t logo_h) {
    if (!sym) { set_error("missing symbol"); return nullptr; }
    ckey_style fallback;
    const ckey_style *style = style_of(ws, fallback);
    ckey_raster_options o;
    ckey_raster_options_default(&o);
    o.size_px = size;
    o.policy = policy;
    o.format = format;
    o.transparent = transparent;
    o.supersample = supersample;
    o.min_feature_px = min_feature_px;
    o.materials = materials;
    ckey_logo_image logo{};
    if (logo_rgba && logo_w && logo_h) {
        logo.rgba = logo_rgba;
        logo.width = logo_w;
        logo.height = logo_h;
        logo.stride = 0;
        o.logo = &logo;
    }
    size_t need = 0;
    ckey_status st = ckey_render_scalable_raster(sym, style, &o, nullptr, 0, &need);
    if (st != CKEY_INSUFFICIENT_BUFFER || need == 0) {
        explain_refusal(sym, ws, "invalid raster options (size 16..16384, supersample 1..8, "
                                 "min feature > 0; transparent needs RGBA)");
        return nullptr;
    }
    auto *out = static_cast<uint8_t *>(std::malloc(need));
    if (!out) { set_error("out of memory"); return nullptr; }
    st = ckey_render_scalable_raster(sym, style, &o, out, need, &need);
    if (st != CKEY_OK) { std::free(out); set_error("raster render failed"); return nullptr; }
    g_last_len = need;
    return out;
}

// Pixel-perfect lattice (ckey_render_scalable_lattice). Returns malloc'd
// bytes: side (uint32 little-endian), the palette as 6 RGB triples
// (background, ink, accents 0..3; unused accents repeat the ink), then
// side * side cell codes (0 background, 1 ink, 2 + i accent i).
CKW_EXPORT uint8_t *ckw_render_lattice(const ckey_scalable_symbol *sym, const WebStyle *ws, uint32_t cells_per_module) {
    if (!sym) { set_error("missing symbol"); return nullptr; }
    ckey_style fallback;
    const ckey_style *style = style_of(ws, fallback);
    ckey_style colours;
    if (style) colours = *style; else ckey_style_default(&colours);
    uint32_t side = 0;
    size_t need = 0;
    ckey_status st = ckey_render_scalable_lattice(sym, style, cells_per_module, nullptr, 0, &side, &need);
    if (st != CKEY_INSUFFICIENT_BUFFER || need == 0) {
        explain_refusal(sym, ws, "invalid lattice options (cells per module 0..32)");
        return nullptr;
    }
    const size_t head = 4 + 18;
    auto *out = static_cast<uint8_t *>(std::malloc(head + need));
    if (!out) { set_error("out of memory"); return nullptr; }
    st = ckey_render_scalable_lattice(sym, style, cells_per_module, out + head, need, &side, &need);
    if (st != CKEY_OK) { std::free(out); set_error("lattice render failed"); return nullptr; }
    for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(side >> (8 * i));
    const ckey_rgb ink = colours.ink;
    const ckey_rgb palette[6] = {colours.background, ink,
                                 colours.accent_count > 0 ? colours.accents[0] : ink, colours.accent_count > 1 ? colours.accents[1] : ink,
                                 colours.accent_count > 2 ? colours.accents[2] : ink, colours.accent_count > 3 ? colours.accents[3] : ink};
    for (int i = 0; i < 6; ++i) { out[4 + 3 * i] = palette[i].r; out[5 + 3 * i] = palette[i].g; out[6 + 3 * i] = palette[i].b; }
    g_last_len = head + need;
    return out;
}

// Encodes RGBA8 pixels (stride = width * 4) as a PNG. dpi <= 0 omits pHYs.
CKW_EXPORT uint8_t *ckw_encode_png_rgba(const uint8_t *rgba, uint32_t width, uint32_t height, double dpi) {
    size_t need = 0;
    const size_t stride = static_cast<size_t>(width) * 4;
    ckey_status st = ckey_encode_png(rgba, width, height, stride, CKEY_PNG_RGBA8, dpi, nullptr, 0, &need);
    if ((st != CKEY_INSUFFICIENT_BUFFER && st != CKEY_OK) || need == 0) { set_error("PNG encode failed"); return nullptr; }
    auto *out = static_cast<uint8_t *>(std::malloc(need));
    if (!out) { set_error("out of memory"); return nullptr; }
    st = ckey_encode_png(rgba, width, height, stride, CKEY_PNG_RGBA8, dpi, out, need, &need);
    if (st != CKEY_OK) { std::free(out); set_error("PNG encode failed"); return nullptr; }
    g_last_len = need;
    return out;
}

// Styled SVG; scale is pixels per canonical unit (width = 31 * scale).
// Returns malloc'd UTF-8 (NUL-terminated; ckw_last_len excludes the NUL).
CKW_EXPORT char *ckw_render_svg(const ckey_scalable_symbol *sym, const WebStyle *ws, uint32_t scale) {
    if (!sym) { set_error("missing symbol"); return nullptr; }
    ckey_style fallback;
    const ckey_style *style = style_of(ws, fallback);
    size_t need = 0;
    ckey_status st = ckey_render_scalable_styled_svg(sym, style, scale, nullptr, 0, &need);
    if (st != CKEY_INSUFFICIENT_BUFFER || need == 0) { explain_refusal(sym, ws, "SVG render failed (scale must be 1..256)"); return nullptr; }
    auto *out = static_cast<char *>(std::malloc(need));
    if (!out) { set_error("out of memory"); return nullptr; }
    st = ckey_render_scalable_styled_svg(sym, style, scale, out, need, &need);
    if (st != CKEY_OK) { std::free(out); set_error("SVG render failed"); return nullptr; }
    g_last_len = std::strlen(out);
    return out;
}

CKW_EXPORT uint8_t *ckw_render_pdf(const ckey_scalable_symbol *sym, const WebStyle *ws, double width_pt,
                                   uint32_t transparent) {
    if (!sym) { set_error("missing symbol"); return nullptr; }
    ckey_style fallback;
    const ckey_style *style = style_of(ws, fallback);
    size_t need = 0;
    ckey_status st = ckey_render_scalable_pdf(sym, style, width_pt, transparent, nullptr, 0, &need);
    if (st != CKEY_INSUFFICIENT_BUFFER || need == 0) { explain_refusal(sym, ws, "PDF render failed (invalid width)"); return nullptr; }
    auto *out = static_cast<uint8_t *>(std::malloc(need));
    if (!out) { set_error("out of memory"); return nullptr; }
    st = ckey_render_scalable_pdf(sym, style, width_pt, transparent, out, need, &need);
    if (st != CKEY_OK) { std::free(out); set_error("PDF render failed"); return nullptr; }
    g_last_len = need;
    return out;
}

// ---- Scanning ---------------------------------------------------------------

namespace {

constexpr size_t kMaxBorders = 6;
constexpr size_t kScalableOut = 65535;  // P0 reaches 4,619 bytes at v31/M; same buffer as the iOS reader

// out_info[0] = profile code (0 P0, 1 P0.1 print, 2 P1, 3 P2, 4 P3: the
// telemetry profile codes), out_info[1] = P0 version (0 for P1..P3),
// out_info[2] = P0 layout (0 standard, 1 print, 2 standard grouped, 3 print
// grouped; 0 for P1..P3),
// out_info[3] = number of border candidates found.
uint8_t *decode_gray(const uint8_t *gray, size_t w, size_t h, size_t stride, uint32_t *out_info) {
    if (!gray || !out_info || w == 0 || h == 0) { set_error("invalid frame"); return nullptr; }
    double borders[kMaxBorders * 8];
    const size_t n = ckey_find_borders_gray8(gray, w, h, stride, borders, kMaxBorders);
    out_info[3] = static_cast<uint32_t>(n);
    if (n == 0) { set_error("no CKey border found"); return nullptr; }
    std::vector<uint8_t> out(kScalableOut);
    ckey_diagnostics d{};
    for (size_t i = 0; i < n; ++i) {
        const double *border = borders + 8 * i;
        double q[8];
        size_t len = 0;
        // Profile 0 (scalable family) first: the core aligns the frame,
        // identifies layout and version from the keys, then decodes.
        uint32_t version = 0, layout = 0;
        if (ckey_refine_quad_gray8(gray, w, h, stride, border, CKEY_FIELD_SCALABLE, q) >= 0 &&
            ckey_decode_scalable_gray8(gray, w, h, stride, q, out.data(), out.size(), &len, &version, &layout, &d) == CKEY_OK) {
            out_info[0] = (layout == CKEY_LAYOUT_PRINT || layout == CKEY_LAYOUT_PRINT_GROUPED) ? 1u : 0u;  // P0.1 geometry
            out_info[1] = version;
            out_info[2] = layout;
            return dup_bytes(out.data(), len);
        }
        // Legacy profiles, each refined against its own calibration field
        // (never let one profile's fit pick another's transform).
        if (ckey_refine_quad_gray8(gray, w, h, stride, border, CKEY_FIELD_LOCATOR, q) >= 0 &&
            ckey_decode_gray8(gray, w, h, stride, q, out.data(), CKEY_LOCATOR_PAYLOAD_BYTES, &d) == CKEY_OK) {
            out_info[0] = 2; out_info[1] = 0; out_info[2] = 0;
            return dup_bytes(out.data(), CKEY_LOCATOR_PAYLOAD_BYTES);
        }
        if (ckey_refine_quad_gray8(gray, w, h, stride, border, CKEY_FIELD_TEXT, q) >= 0 &&
            ckey_decode_text_gray8(gray, w, h, stride, q, out.data(), out.size(), &len, &d) == CKEY_OK) {
            out_info[0] = 3; out_info[1] = 0; out_info[2] = 0;
            return dup_bytes(out.data(), len);
        }
        if (ckey_refine_quad_gray8(gray, w, h, stride, border, CKEY_FIELD_EXTENDED, q) >= 0 &&
            ckey_decode_extended_gray8(gray, w, h, stride, q, out.data(), out.size(), &len, &d) == CKEY_OK) {
            out_info[0] = 4; out_info[1] = 0; out_info[2] = 0;
            return dup_bytes(out.data(), len);
        }
    }
    set_error_diag(d, "no CKey decoded");
    return nullptr;
}

}  // namespace

// Decodes an 8-bit grayscale frame. Returns malloc'd payload bytes or 0.
CKW_EXPORT uint8_t *ckw_decode_gray(const uint8_t *gray, uint32_t width, uint32_t height, uint32_t stride,
                                    uint32_t *out_info) {
    return decode_gray(gray, width, height, stride ? stride : width, out_info);
}

// Decodes an RGBA8 frame (as from ImageData). Pixels are composited over
// white (so a transparent render reads as ink on paper) and converted to
// Rec. 709 luma, then decoded as by ckw_decode_gray.
CKW_EXPORT uint8_t *ckw_decode_rgba(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t stride,
                                    uint32_t *out_info) {
    if (!rgba || width == 0 || height == 0) { set_error("invalid frame"); return nullptr; }
    const size_t row = stride ? stride : static_cast<size_t>(width) * 4;
    std::vector<uint8_t> gray(static_cast<size_t>(width) * height);
    for (size_t y = 0; y < height; ++y) {
        const uint8_t *p = rgba + y * row;
        uint8_t *g = gray.data() + y * width;
        for (size_t x = 0; x < width; ++x, p += 4) {
            // Rec. 709 luma in 8.8 fixed point: 0.2126, 0.7152, 0.0722.
            const uint32_t luma = (54u * p[0] + 183u * p[1] + 19u * p[2] + 128u) >> 8;
            const uint32_t a = p[3];
            g[x] = static_cast<uint8_t>((luma * a + 255u * (255u - a) + 127u) / 255u);
        }
    }
    return decode_gray(gray.data(), width, height, width, out_info);
}

// ---- Optical certification (ckey_certify; present from ABI 4) ---------------

#ifdef CKEY_ADJUST_INK
CKW_EXPORT uint32_t ckw_has_certify(void) { return 1; }

// Certifies a style (0 = default) under its policy. colours: colour_count RGB
// triplets (3 bytes each) the symbol may sit on; bd_rgba (optional) host
// pixels behind the symbol. Model parameters < 0 keep the default (trials 0).
// out (8 doubles): certified, policy, adjustments, trials, trials_passed,
// min_contrast, feature_separation, logo_load. The certification message is
// in ckw_last_error(). If out_style is non-zero, the style to render (the
// adaptive repair) is written into it. Returns the core status.
CKW_EXPORT int ckw_certify(const ckey_scalable_symbol *sym, const WebStyle *ws, uint32_t transparent,
                           const uint8_t *colours, uint32_t colour_count, const uint8_t *bd_rgba,
                           uint32_t bd_w, uint32_t bd_h, double px_per_module, double blur_px, double noise,
                           double glare, uint32_t trials, double *out, WebStyle *out_style) {
    ckey_style fallback;
    const ckey_style *style = style_of(ws, fallback);
    std::vector<ckey_rgb> cs;
    for (uint32_t i = 0; colours && i < colour_count; ++i) cs.push_back({colours[3 * i], colours[3 * i + 1], colours[3 * i + 2]});
    ckey_backdrop bd{};
    bd.colours = cs.empty() ? nullptr : cs.data();
    bd.colour_count = static_cast<uint32_t>(cs.size());
    if (bd_rgba && bd_w && bd_h) { bd.rgba = bd_rgba; bd.width = bd_w; bd.height = bd_h; bd.stride = 0; }
    const bool has_backdrop = bd.colour_count || bd.rgba;
    ckey_scan_model m;
    ckey_scan_model_default(&m);
    if (px_per_module >= 0) m.px_per_module = px_per_module;
    if (blur_px >= 0) m.blur_px = blur_px;
    if (noise >= 0) m.noise = noise;
    if (glare >= 0) m.glare = glare;
    if (trials) m.trials = trials;
    ckey_style repaired;
    std::memcpy(&repaired, style, sizeof repaired);
    ckey_certification c{};
    const ckey_status st = ckey_certify_scalable_style(sym, style, transparent, has_backdrop ? &bd : nullptr, &m,
                                                       &repaired, &c);
    char msg[sizeof c.message + 1];
    std::memcpy(msg, c.message, sizeof c.message);
    msg[sizeof c.message] = '\0';
    set_error(msg[0] ? msg : (st == CKEY_OK ? "" : "certification failed"));
    if (out) {
        out[0] = c.certified; out[1] = c.policy; out[2] = c.adjustments; out[3] = c.trials;
        out[4] = c.trials_passed; out[5] = c.min_contrast; out[6] = c.feature_separation; out[7] = c.logo_load;
    }
    if (out_style && st == CKEY_OK) {
        out_style->style = repaired;
        // Keep the logo string owned by out_style.
        out_style->logo_href = repaired.logo_href ? repaired.logo_href : "";
        out_style->style.logo_href = out_style->logo_href.empty() ? nullptr : out_style->logo_href.c_str();
    }
    return st;
}
#else
CKW_EXPORT uint32_t ckw_has_certify(void) { return 0; }
#endif

// ---- Telemetry --------------------------------------------------------------
// Thin double-typed wrappers; the core does no networking and reads no clock.

#ifndef CKW_NO_TELEMETRY
CKW_EXPORT int ckw_tel_set_mode(int mode) { return ckey_telemetry_set_mode(mode); }
CKW_EXPORT int ckw_tel_get_mode(void) { return ckey_telemetry_get_mode(); }
CKW_EXPORT int ckw_tel_set_client(const char *sdk, const char *version, const char *platform) {
    return ckey_telemetry_set_client(sdk, version, platform);
}
CKW_EXPORT void ckw_tel_session_begin(double now_ms) { ckey_telemetry_session_begin(to_ms(now_ms)); }
CKW_EXPORT void ckw_tel_frame(double now_ms, double processing_ms) {
    const uint64_t p = to_ms(processing_ms);
    ckey_telemetry_frame(to_ms(now_ms), p > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(p));
}
CKW_EXPORT int ckw_tel_session_success(double now_ms, uint32_t profile_code) {
    return ckey_telemetry_session_success(to_ms(now_ms), profile_code);
}
CKW_EXPORT void ckw_tel_session_end(double now_ms) { ckey_telemetry_session_end(to_ms(now_ms)); }
CKW_EXPORT int ckw_tel_should_send(double now_ms) { return ckey_telemetry_should_send(to_ms(now_ms)); }

// Takes a batch. Returns malloc'd JSON (NUL-terminated) and sets ckw_last_len()
// and ckw_tel_last_batch_id(); returns 0 when there is nothing to send.
CKW_EXPORT char *ckw_tel_take_batch(const char *utc_day) {
    g_last_batch_id = 0;
    g_last_len = 0;
    char buf[CKEY_TELEMETRY_MAX_BODY_BYTES + 1];
    size_t len = 0;
    uint64_t id = 0;
    const ckey_status st = ckey_telemetry_take_batch(utc_day, buf, sizeof buf, &len, &id);
    if (st != CKEY_OK) { set_error("telemetry batch failed"); return nullptr; }
    if (id == 0 || len == 0) return nullptr;
    char *out = static_cast<char *>(std::malloc(len + 1));
    if (!out) { set_error("out of memory"); return nullptr; }
    std::memcpy(out, buf, len + 1);
    g_last_len = len;
    g_last_batch_id = static_cast<double>(id);
    return out;
}
CKW_EXPORT double ckw_tel_last_batch_id(void) { return g_last_batch_id; }
CKW_EXPORT int ckw_tel_batch_result(double batch_id, int http_status, double now_ms) {
    return ckey_telemetry_batch_result(to_ms(batch_id), http_status, to_ms(now_ms));
}

// out (33 doubles): sessions started, succeeded, failed, frames, decoded[5],
// time_to_decode {count, sum, buckets[10]}, frame_ms {count, sum, buckets[10]}.
CKW_EXPORT int ckw_tel_snapshot(double *out) {
    ckey_telemetry_counters c{};
    const ckey_status st = ckey_telemetry_snapshot(&c);
    if (st != CKEY_OK || !out) return st;
    size_t k = 0;
    out[k++] = static_cast<double>(c.sessions_started);
    out[k++] = static_cast<double>(c.sessions_succeeded);
    out[k++] = static_cast<double>(c.sessions_failed);
    out[k++] = static_cast<double>(c.frames);
    for (uint64_t v : c.decoded) out[k++] = static_cast<double>(v);
    for (const ckey_telemetry_histogram *hgm : {&c.time_to_decode_ms, &c.frame_ms}) {
        out[k++] = static_cast<double>(hgm->count);
        out[k++] = static_cast<double>(hgm->sum);
        for (uint64_t v : hgm->buckets) out[k++] = static_cast<double>(v);
    }
    return CKEY_OK;
}

// TEST ONLY: restores the telemetry defaults.
CKW_EXPORT void ckw_tel_reset_for_testing(void) { ckey_telemetry_reset_for_testing(); }
#endif
