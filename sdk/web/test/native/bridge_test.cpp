// Native test of the Web SDK bridge (sdk/web/src/ckey_web.cpp), compiled with
// the system C++ compiler against the same core sources the wasm build uses.
// It exercises every function the JavaScript wrapper calls, so the bridge
// logic is verified even where Emscripten is not installed.
//   scripts/test-native.sh  (or see .github/workflows/sdk-web.yml)

#include "ckey/ckey.h"
#include "ckey/telemetry.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
void ckw_free(void *);
const char *ckw_last_error(void);
uint32_t ckw_last_len(void);
const char *ckw_software_version(void);
uint32_t ckw_scalable_capacity(uint32_t, uint32_t, uint32_t);
void *ckw_encode(const uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t);
void ckw_symbol_free(void *);
void ckw_symbol_info(const void *, uint32_t *);
void *ckw_style_new(uint32_t);
void ckw_style_free(void *);
int ckw_style_set_rgb(void *, uint32_t, uint32_t, uint32_t, uint32_t);
int ckw_style_set_num(void *, uint32_t, double);
int ckw_style_check(const void *, const void *);
uint8_t *ckw_render_raster(const void *, const void *, uint32_t, uint32_t, uint32_t, uint32_t, double, uint32_t,
                           uint32_t, const uint8_t *, uint32_t, uint32_t);
uint8_t *ckw_encode_png_rgba(const uint8_t *, uint32_t, uint32_t, double);
char *ckw_render_svg(const void *, const void *, uint32_t);
uint8_t *ckw_render_pdf(const void *, const void *, double, uint32_t);
uint8_t *ckw_decode_rgba(const uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t *);
uint8_t *ckw_decode_gray(const uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t *);
int ckw_tel_set_mode(int);
int ckw_tel_get_mode(void);
int ckw_tel_set_client(const char *, const char *, const char *);
void ckw_tel_session_begin(double);
void ckw_tel_frame(double, double);
int ckw_tel_session_success(double, uint32_t);
void ckw_tel_session_end(double);
int ckw_tel_should_send(double);
char *ckw_tel_take_batch(const char *);
double ckw_tel_last_batch_id(void);
int ckw_tel_batch_result(double, int, double);
int ckw_tel_snapshot(double *);
void ckw_tel_reset_for_testing(void);
uint32_t ckw_has_certify(void);
int ckw_certify(const void *, const void *, uint32_t, const uint8_t *, uint32_t, const uint8_t *, uint32_t, uint32_t,
                double, double, double, double, uint32_t, double *, void *);
}

static int failures = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } \
    } while (0)

// Places an RGBA render in the middle of a larger white frame, like a photo.
static std::vector<uint8_t> pad(const uint8_t *px, uint32_t size, uint32_t margin, uint32_t &side) {
    side = size + 2 * margin;
    std::vector<uint8_t> f(static_cast<size_t>(side) * side * 4, 255);
    for (uint32_t y = 0; y < size; ++y)
        std::memcpy(&f[(static_cast<size_t>(y + margin) * side + margin) * 4], px + static_cast<size_t>(y) * size * 4, size * 4);
    return f;
}

static bool roundtrip(const std::string &text, uint32_t layout, uint32_t transparent, const void *style) {
    void *sym = ckw_encode(reinterpret_cast<const uint8_t *>(text.data()), static_cast<uint32_t>(text.size()), 0, 1, layout);
    if (!sym) { std::fprintf(stderr, "encode: %s\n", ckw_last_error()); return false; }
    uint8_t *px = ckw_render_raster(sym, style, 512, 0, transparent, 4, 1.0, 1, 0, nullptr, 0, 0);
    if (!px) { std::fprintf(stderr, "raster: %s\n", ckw_last_error()); ckw_symbol_free(sym); return false; }
    uint32_t side = 0;
    auto frame = pad(px, 512, 64, side);
    uint32_t info[4] = {9, 9, 9, 9};
    uint8_t *out = ckw_decode_rgba(frame.data(), side, side, 0, info);
    bool ok = out && ckw_last_len() == text.size() && std::memcmp(out, text.data(), text.size()) == 0 &&
              info[0] == (layout ? 1u : 0u) && info[2] == layout && info[1] >= 1;
    if (!out) std::fprintf(stderr, "decode: %s (borders %u)\n", ckw_last_error(), info[3]);
    ckw_free(out);
    ckw_free(px);
    ckw_symbol_free(sym);
    return ok;
}

int main() {
    CHECK(std::strcmp(ckw_software_version(), CKEY_SOFTWARE_VERSION) == 0);

    // Encode + info.
    const std::string url = "https://openckey.org";
    void *sym = ckw_encode(reinterpret_cast<const uint8_t *>(url.data()), static_cast<uint32_t>(url.size()), 0, 1, 0);
    CHECK(sym != nullptr);
    uint32_t info[5] = {};
    ckw_symbol_info(sym, info);
    CHECK(info[0] >= 1 && info[0] <= 31);
    CHECK(info[1] == 1 && info[2] == 0 && info[3] == url.size());
    CHECK(ckw_scalable_capacity(info[0], 1, 0) >= url.size());

    // Encode failure carries the core's message.
    std::vector<uint8_t> big(20000, 'x');
    CHECK(ckw_encode(big.data(), static_cast<uint32_t>(big.size()), 0, 1, 0) == nullptr);
    CHECK(std::strlen(ckw_last_error()) > 0);
    std::printf("oversize message: %s\n", ckw_last_error());

    // Round trips through the reader pipeline (standard, print, transparent, concept).
    CHECK(roundtrip(url, 0, 0, nullptr));
    CHECK(roundtrip("Hello, CKey! Grüße", 1, 0, nullptr));
    CHECK(roundtrip(url, 0, 1, nullptr));
    void *concept_style = ckw_style_new(1);
    CHECK(concept_style != nullptr);
    CHECK(roundtrip(url, 0, 0, concept_style));

    // Transparent corner has alpha 0; opaque corner is white.
    uint8_t *px = ckw_render_raster(sym, nullptr, 64, 0, 1, 4, 1.0, 1, 0, nullptr, 0, 0);
    CHECK(px && ckw_last_len() == 64u * 64u * 4u && px[3] == 0);
    ckw_free(px);
    px = ckw_render_raster(sym, nullptr, 64, 1, 0, 4, 1.0, 1, 0, nullptr, 0, 0);
    CHECK(px && px[0] == 255 && px[3] == 255);
    uint8_t *png = ckw_encode_png_rgba(px, 64, 64, 0);
    CHECK(png && ckw_last_len() > 8 && std::memcmp(png, "\x89PNG\r\n\x1a\n", 8) == 0);
    ckw_free(png);
    ckw_free(px);
    CHECK(ckw_render_raster(sym, nullptr, 8, 0, 0, 4, 1.0, 1, 0, nullptr, 0, 0) == nullptr);  // size < 16

    char *svg = ckw_render_svg(sym, nullptr, 16);
    CHECK(svg && std::strncmp(svg, "<svg", 4) == 0 && std::strlen(svg) == ckw_last_len());
    ckw_free(svg);
    uint8_t *pdf = ckw_render_pdf(sym, nullptr, 144, 0);
    CHECK(pdf && std::memcmp(pdf, "%PDF-", 5) == 0);
    ckw_free(pdf);

    // A low-contrast style is refused with an explanation.
    void *bad = ckw_style_new(0);
    CHECK(ckw_style_set_rgb(bad, 2, 250, 250, 250) == 1);
    CHECK(ckw_style_set_rgb(bad, 3, 250, 250, 250) == 1);
    CHECK(ckw_style_check(sym, bad) != 0);
    std::printf("bad style message: %s\n", ckw_last_error());
    CHECK(ckw_render_svg(sym, bad, 16) == nullptr);
    CHECK(ckw_style_set_num(bad, 2, 7) == 0);
#if CKEY_ABI_VERSION >= 4
    // Override draws the refused style as given; the check still reports it.
    CHECK(ckw_style_set_num(bad, 10, 2) == 1);
    char *over = ckw_render_svg(sym, bad, 16);
    CHECK(over != nullptr);
    ckw_free(over);
    CHECK(ckw_style_check(sym, bad) == 0 && std::strncmp(ckw_last_error(), "override", 8) == 0);
    CHECK(ckw_style_set_num(bad, 10, 0) == 1);
#endif
    if (ckw_has_certify()) {
        double cert[8] = {};
        CHECK(ckw_certify(sym, nullptr, 0, nullptr, 0, nullptr, 0, 0, -1, -1, -1, -1, 0, cert, nullptr) == 0);
        CHECK(cert[0] == 1 && cert[3] >= 1);
        std::printf("certify default: certified=%g trials=%g/%g msg=%s\n", cert[0], cert[4], cert[3], ckw_last_error());
        // Transparent with no backdrop: background unknown, never certified.
        CHECK(ckw_certify(sym, nullptr, 1, nullptr, 0, nullptr, 0, 0, -1, -1, -1, -1, 0, cert, nullptr) != 0 || cert[0] == 0);
        std::printf("certify transparent/no backdrop: certified=%g msg=%s\n", cert[0], ckw_last_error());
        const uint8_t grey[3] = {40, 40, 40};
        ckw_certify(sym, nullptr, 1, grey, 1, nullptr, 0, 0, -1, -1, -1, -1, 0, cert, nullptr);
        std::printf("certify transparent on dark grey: certified=%g msg=%s\n", cert[0], ckw_last_error());
    }
    ckw_style_free(bad);
    CHECK(ckw_style_new(9) == nullptr);
    ckw_style_free(concept_style);

    // Blank frame: nothing decoded.
    std::vector<uint8_t> blank(100 * 100, 255);
    uint32_t dinfo[4];
    CHECK(ckw_decode_gray(blank.data(), 100, 100, 0, dinfo) == nullptr);

    // Telemetry wrappers.
    ckw_tel_reset_for_testing();
    CHECK(ckw_tel_get_mode() == 1);
    CHECK(ckw_tel_set_mode(3) != 0 && ckw_tel_get_mode() == 1);
    CHECK(ckw_tel_set_client("web", CKEY_SOFTWARE_VERSION, "web") == 0);
    ckw_tel_session_begin(1000);
    ckw_tel_frame(1010, 7.6);
    CHECK(ckw_tel_session_success(1020, 0) == 0);
    ckw_tel_session_end(1030);
    double snap[33] = {};
    CHECK(ckw_tel_snapshot(snap) == 0);
    CHECK(snap[0] == 1 && snap[1] == 1 && snap[3] == 1 && snap[4] == 1);
    CHECK(ckw_tel_should_send(2000) == 0);           // delta younger than a minute
    CHECK(ckw_tel_should_send(1000 + 60000) != 0);
    char *json = ckw_tel_take_batch("2026-11-05");
    CHECK(json && std::strstr(json, "\"sdk\":\"web\"") && std::strstr(json, "\"platform\":\"web\""));
    const double id = ckw_tel_last_batch_id();
    CHECK(id > 0);
    std::printf("batch: %s\n", json ? json : "(null)");
    ckw_free(json);
    CHECK(ckw_tel_take_batch("2026-11-05") == nullptr);  // one in flight
    CHECK(ckw_tel_batch_result(id, 204, 62000) == 0);
    CHECK(ckw_tel_batch_result(id, 204, 62000) != 0);    // reported once
    ckw_tel_reset_for_testing();

    ckw_symbol_free(sym);
    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::printf("bridge native tests passed\n");
    return 0;
}
