// Encode a URL, render native pixels, write a PNG, and scan it back.
#include <ckey/ckey.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    const char *url = "https://openckey.org/tour";
    ckey_scalable_symbol symbol{}; ckey_diagnostics diag{};
    if (ckey_encode_scalable(reinterpret_cast<const uint8_t *>(url), std::strlen(url), 0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD, &symbol, &diag) != CKEY_OK) { std::puts(diag.message); return 1; }
    ckey_raster_options options; ckey_raster_options_default(&options);
    options.size_px = 512; options.format = CKEY_PIXELS_GRAY8;
    std::vector<uint8_t> pixels(512 * 512); size_t needed = 0;
    if (ckey_render_scalable_raster(&symbol, nullptr, &options, pixels.data(), pixels.size(), &needed) != CKEY_OK) return 1;
    std::vector<uint8_t> png(1 << 20);
    if (ckey_encode_png(pixels.data(), 512, 512, 0, CKEY_PNG_GRAY8, 0, png.data(), png.size(), &needed) == CKEY_OK)
        if (FILE *f = std::fopen("ckey.png", "wb")) { std::fwrite(png.data(), 1, needed, f); std::fclose(f); }
    double borders[8 * 4]; double quad[8];
    const size_t found = ckey_find_borders_gray8(pixels.data(), 512, 512, 512, borders, 4);
    std::vector<uint8_t> out(65535); size_t len = 0; uint32_t version = 0;
    for (size_t i = 0; i < found; ++i)
        if (ckey_refine_quad_gray8(pixels.data(), 512, 512, 512, borders + 8 * i, CKEY_FIELD_SCALABLE, quad) > 0 &&
            ckey_decode_scalable_gray8(pixels.data(), 512, 512, 512, quad, out.data(), out.size(), &len, &version, nullptr, &diag) == CKEY_OK) {
            std::printf("v%u: %.*s (CKey %s)\n", version, static_cast<int>(len), reinterpret_cast<const char *>(out.data()), ckey_get_software_version());
            return 0;
        }
    std::puts("no symbol found");
    return 1;
}
