// Links CKey::scan only: no renderer, no telemetry. Reads a binary PGM (P5).
#include <ckey/ckey.h>
#include <cstdio>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: scan_only image.pgm\n"); return 2; }
    FILE *f = std::fopen(argv[1], "rb"); if (!f) return 2;
    int w = 0, h = 0, max = 0;
    if (std::fscanf(f, "P5 %d %d %d", &w, &h, &max) != 3 || w <= 0 || h <= 0 || max != 255) return 2;
    std::fgetc(f);
    std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
    if (std::fread(gray.data(), 1, gray.size(), f) != gray.size()) return 2;
    std::fclose(f);
    double borders[8 * 6], quad[8]; std::vector<uint8_t> out(65535); size_t len = 0; uint32_t version = 0; ckey_diagnostics d{};
    const size_t n = ckey_find_borders_gray8(gray.data(), w, h, w, borders, 6);
    for (size_t i = 0; i < n; ++i)
        if (ckey_refine_quad_gray8(gray.data(), w, h, w, borders + 8 * i, CKEY_FIELD_SCALABLE, quad) > 0 &&
            ckey_decode_scalable_gray8(gray.data(), w, h, w, quad, out.data(), out.size(), &len, &version, nullptr, &d) == CKEY_OK) {
            std::fwrite(out.data(), 1, len, stdout); std::puts(""); return 0;
        }
    return 1;
}
