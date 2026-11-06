// Renders the example symbols in docs/gallery: each one is drawn with the
// public API, read back with the real decoder, and certified. Writes one
// PNG per entry plus docs/gallery/README.md.
//
//   cmake --build build --target ckey-gallery
//   ./build/ckey-gallery docs/gallery
#include <ckey/ckey.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Entry {
    const char *file;
    const char *title;
    const char *about;
    std::string payload;
    uint32_t version;  // 0 = smallest that fits
    ckey_ecc_level level;
    ckey_scalable_layout layout;
    std::function<void(ckey_style &)> style;
};

ckey_rgb hex(uint32_t v) { return ckey_rgb{uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }

bool write_file(const std::string &path, const std::vector<uint8_t> &bytes) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

// Reads the symbol back from a GRAY8 rendering with the reader pipeline.
bool reads_back(const ckey_scalable_symbol &symbol, const ckey_style &style, const std::string &expected) {
    const uint32_t size = 768;
    ckey_raster_options options;
    ckey_raster_options_default(&options);
    options.size_px = size;
    options.format = CKEY_PIXELS_GRAY8;
    std::vector<uint8_t> gray(size_t(size) * size);
    size_t needed = 0;
    if (ckey_render_scalable_raster(&symbol, &style, &options, gray.data(), gray.size(), &needed) != CKEY_OK) return false;
    double borders[8 * 8];
    double quad[8];
    const size_t found = ckey_find_borders_gray8(gray.data(), size, size, size, borders, 8);
    std::vector<uint8_t> out(65535);
    for (size_t i = 0; i < found; ++i) {
        size_t len = 0;
        uint32_t version = 0;
        ckey_diagnostics diag{};
        if (ckey_refine_quad_gray8(gray.data(), size, size, size, borders + 8 * i, CKEY_FIELD_SCALABLE, quad) > 0 &&
            ckey_decode_scalable_gray8(gray.data(), size, size, size, quad, out.data(), out.size(), &len, &version,
                                       nullptr, &diag) == CKEY_OK)
            return std::string(reinterpret_cast<const char *>(out.data()), len) == expected;
    }
    return false;
}

std::string grade_of(const char *message) {
    for (const char *g : {"excellent", "good", "marginal", "fails"}) {
        const std::string token = std::string(g) + " scan margin";
        if (std::strstr(message, token.c_str())) return g;
    }
    return "n/a";
}

}  // namespace

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : "docs/gallery";
    const std::string license_excerpt =
        "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and "
        "associated documentation files (the \"Software\"), to deal in the Software without restriction, including "
        "without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
        "copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the "
        "following conditions: The above copyright notice and this permission notice shall be included in all "
        "copies or substantial portions of the Software.";
    const std::string vcard =
        "BEGIN:VCARD\nVERSION:3.0\nN:Hart;Amelia;;Dr.;\nFN:Dr. Amelia Hart\nORG:Cardiology\nTEL:+15550100\nEND:VCARD";

    const std::vector<Entry> entries = {
        {"link-plain.png", "A link, plain", "The default style: black on white.", "https://openckey.org", 0,
         CKEY_ECC_M, CKEY_LAYOUT_STANDARD, [](ckey_style &) {}},
        {"link-print.png", "A link, print layout (Profile 0.1)", "Larger marks and gaps for small printed labels.",
         "https://openckey.org", 0, CKEY_ECC_M, CKEY_LAYOUT_PRINT, [](ckey_style &) {}},
        {"contact-evergreen.png", "A contact card, evergreen on paper", "Custom ink and background.", vcard, 0,
         CKEY_ECC_M, CKEY_LAYOUT_STANDARD,
         [](ckey_style &s) {
             s.ink = s.ink_shade = hex(0x215D3B);
             s.background = s.background_edge = hex(0xFCFAF6);
         }},
        {"concept.png", "The Concept preset", "Cream metallic ink on a dark vignette, with accents, depth and grain.",
         "https://openckey.org/about", 0, CKEY_ECC_Q, CKEY_LAYOUT_STANDARD,
         [](ckey_style &s) { ckey_style_preset_fill(&s, CKEY_PRESET_CONCEPT); }},
        {"dots-navy.png", "Dotted rings, circle modules", "Ring marks drawn as separate dots, circular modules.",
         "Visiting hours: 8 am to 8 pm, every day.", 0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD,
         [](ckey_style &s) {
             s.rings = CKEY_RING_DOTS;
             s.modules = CKEY_MODULE_CIRCLE;
             s.rays = CKEY_RAY_FLAT;
             s.ink = s.ink_shade = hex(0x1C2B4A);
         }},
        {"accents.png", "Accent colours", "A share of the data marks drawn in accent colours.",
         "https://openckey.org/scan", 0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD,
         [](ckey_style &s) {
             s.accents[0] = hex(0x9A3130);
             s.accents[1] = hex(0x215D3B);
             s.accent_count = 2;
             s.accent_percent = 35;
         }},
        {"dark.png", "Light on dark", "Paper-coloured ink on an ink-coloured background.", "https://openckey.org",
         0, CKEY_ECC_M, CKEY_LAYOUT_STANDARD,
         [](ckey_style &s) {
             s.ink = s.ink_shade = hex(0xFCFAF6);
             s.background = s.background_edge = hex(0x18211A);
         }},
        {"dense.png", "A dense symbol", "About 560 bytes of text at level M.", license_excerpt, 0, CKEY_ECC_M,
         CKEY_LAYOUT_STANDARD, [](ckey_style &) {}},
    };

    std::string md =
        "# Gallery\n\n"
        "Every image here is rendered by the CKey core with `tools/gallery/ckey_gallery.cpp`, read back with the "
        "real decoder, and certified with the modelled photo sweep (a model, not a measurement). Rebuild them "
        "with:\n\n```sh\ncmake --build build --target ckey-gallery\n./build/ckey-gallery docs/gallery\n```\n\n"
        "| | Symbol | Holds | Version | Reads back | Certification |\n|---|---|---|---|---|---|\n";
    int failures = 0;
    for (const Entry &e : entries) {
        ckey_scalable_symbol symbol{};
        ckey_diagnostics diag{};
        if (ckey_encode_scalable(reinterpret_cast<const uint8_t *>(e.payload.data()), e.payload.size(), e.version,
                                 e.level, e.layout, &symbol, &diag) != CKEY_OK) {
            std::fprintf(stderr, "%s: encode failed: %s\n", e.file, diag.message);
            return 1;
        }
        ckey_style style;
        ckey_style_default(&style);
        e.style(style);

        const uint32_t size = 640;
        ckey_raster_options options;
        ckey_raster_options_default(&options);
        options.size_px = size;
        std::vector<uint8_t> rgba(size_t(size) * size * 4);
        size_t needed = 0;
        if (ckey_render_scalable_raster(&symbol, &style, &options, rgba.data(), rgba.size(), &needed) != CKEY_OK) {
            std::fprintf(stderr, "%s: render refused\n", e.file);
            return 1;
        }
        ckey_encode_png(rgba.data(), size, size, 0, CKEY_PNG_RGBA8, 0, nullptr, 0, &needed);
        std::vector<uint8_t> png(needed);
        if (ckey_encode_png(rgba.data(), size, size, 0, CKEY_PNG_RGBA8, 0, png.data(), png.size(), &needed) != CKEY_OK ||
            !write_file(dir + "/" + e.file, png)) {
            std::fprintf(stderr, "%s: could not write PNG\n", e.file);
            return 1;
        }

        const bool read = reads_back(symbol, style, e.payload);
        ckey_style adapted{};
        ckey_certification report{};
        ckey_certify_scalable_style(&symbol, &style, 0, nullptr, nullptr, &adapted, &report);
        const std::string grade = report.certified ? "certified, " + grade_of(report.message) : "not certified (" + grade_of(report.message) + ")";
        if (!read) ++failures;

        char version[48];
        std::snprintf(version, sizeof version, "%u%s", symbol.version, e.layout == CKEY_LAYOUT_PRINT ? " (print)" : "");
        md += "| <img src=\"" + std::string(e.file) + "\" width=\"180\" alt=\"" + e.title + "\"> | **" + e.title +
              "**<br>" + e.about + " | " + std::to_string(e.payload.size()) + " bytes | " + version + " | " +
              (read ? "yes" : "**no**") + " | " + grade + " |\n";
        std::printf("%-22s v%-3u %4zu bytes  read=%s  %s\n", e.file, symbol.version, e.payload.size(),
                    read ? "yes" : "NO", grade.c_str());
    }
    md += "\nCKey ";
    md += ckey_get_software_version();
    md += ".\n";
    write_file(dir + "/README.md", std::vector<uint8_t>(md.begin(), md.end()));
    return failures == 0 ? 0 : 2;
}
