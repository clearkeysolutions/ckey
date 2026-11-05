#include "ckey/ckey.h"
#include "print_output.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <string>
#include <vector>

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Strict: an even number of hexadecimal digits and nothing else (no sign,
// whitespace, or 0x prefix).
static bool from_hex(const std::string &input, std::vector<uint8_t> &out) {
    if (input.size() % 2 != 0) return false;
    out.resize(input.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = hex_digit(input[2 * i]), lo = hex_digit(input[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi << 4 | lo);
    }
    return true;
}

static bool write_svg(const std::string &path, const std::string &svg) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(svg.data(), static_cast<std::streamsize>(svg.size() - 1));
    return static_cast<bool>(file);
}

static std::string base64(const std::string &in) {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i < in.size()) {
        const uint32_t v = (uint8_t(in[i]) << 16) | (i + 1 < in.size() ? uint8_t(in[i + 1]) << 8 : 0);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += i + 1 < in.size() ? t[(v >> 6) & 63] : '='; out += '=';
    }
    return out;
}

// ckey encode-scalable (--utf8 <text> | --hex <hex>) --out <file.svg>
//   [--version 1-31] [--level L|M|Q|H] [--preset plain|concept]
//   [--modules rounded|square|circle|diamond] [--rings joined|dots] [--rays round|flat]
//   [--logo <png|jpg|svg file>] [--logo-radius <units>]
static int encode_scalable(int argc, char **argv) {
    std::string mode, value, output, logo_href, png; uint32_t version = 0; ckey_ecc_level level = CKEY_ECC_M;
    double size_in = 1.0, dpi = 600;
    ckey_scalable_layout layout = CKEY_LAYOUT_STANDARD;
    ckey_style style; ckey_style_default(&style);
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string flag = argv[i], arg = argv[i + 1];
        if (flag == "--utf8" || flag == "--hex") { mode = flag; value = arg; }
        else if (flag == "--out") output = arg;
        else if (flag == "--version") {
            char *end = nullptr; const long v = std::strtol(arg.c_str(), &end, 10);
            if (!end || *end || v < 1 || v > static_cast<long>(CKEY_SCALABLE_VERSION_COUNT)) { std::cerr << "--version must be 1 to " << CKEY_SCALABLE_VERSION_COUNT << '\n'; return 2; }
            version = static_cast<uint32_t>(v);
        } else if (flag == "--level") {
            if (arg == "L") level = CKEY_ECC_L; else if (arg == "M") level = CKEY_ECC_M; else if (arg == "Q") level = CKEY_ECC_Q; else if (arg == "H") level = CKEY_ECC_H;
            else { std::cerr << "--level must be L, M, Q, or H\n"; return 2; }
        } else if (flag == "--png") { png = arg;
        } else if (flag == "--size-in" || flag == "--dpi") {
            char *end = nullptr; const double v = std::strtod(arg.c_str(), &end);
            if (!end || *end || !(v > 0)) { std::cerr << flag << " must be a positive number\n"; return 2; }
            (flag == "--dpi" ? dpi : size_in) = v;
        } else if (flag == "--layout") {
            if (arg == "standard") layout = CKEY_LAYOUT_STANDARD; else if (arg == "print") layout = CKEY_LAYOUT_PRINT;
            else { std::cerr << "--layout must be standard or print\n"; return 2; }
        } else if (flag == "--preset") {
            if (arg == "concept") ckey_style_preset_fill(&style, CKEY_PRESET_CONCEPT);
            else if (arg == "plain") ckey_style_preset_fill(&style, CKEY_PRESET_PLAIN);
            else { std::cerr << "--preset must be plain or concept\n"; return 2; }
        } else if (flag == "--modules") {
            if (arg == "rounded") style.modules = CKEY_MODULE_ROUNDED; else if (arg == "square") style.modules = CKEY_MODULE_SQUARE;
            else if (arg == "circle") style.modules = CKEY_MODULE_CIRCLE; else if (arg == "diamond") style.modules = CKEY_MODULE_DIAMOND;
            else { std::cerr << "--modules must be rounded, square, circle, or diamond\n"; return 2; }
        } else if (flag == "--rings") {
            if (arg == "joined") style.rings = CKEY_RING_JOINED; else if (arg == "dots") style.rings = CKEY_RING_DOTS;
            else { std::cerr << "--rings must be joined or dots\n"; return 2; }
        } else if (flag == "--rays") {
            if (arg == "round") style.rays = CKEY_RAY_ROUND; else if (arg == "flat") style.rays = CKEY_RAY_FLAT;
            else { std::cerr << "--rays must be round or flat\n"; return 2; }
        } else if (flag == "--logo") {
            std::ifstream file(arg, std::ios::binary);
            if (!file) { std::cerr << "cannot read logo " << arg << '\n'; return 2; }
            const std::string bytes((std::istreambuf_iterator<char>(file)), {});
            const std::string ext = arg.size() > 4 ? arg.substr(arg.size() - 4) : "";
            const std::string type = ext == ".svg" ? "image/svg+xml" : ext == ".png" ? "image/png" : "image/jpeg";
            logo_href = "data:" + type + ";base64," + base64(bytes); style.logo_plate = 1;
        } else if (flag == "--logo-radius") {
            char *end = nullptr; style.logo_radius = std::strtod(arg.c_str(), &end);
            if (!end || *end || !(style.logo_radius >= 0)) { std::cerr << "--logo-radius must be a non-negative number\n"; return 2; }
        } else { std::cerr << "unknown option " << flag << '\n'; return 2; }
    }
    if (!logo_href.empty()) style.logo_href = logo_href.c_str();
    if ((argc % 2) != 0 || mode.empty() || (output.empty() && png.empty())) {
        std::cerr << "usage: ckey encode-scalable (--utf8 <text> | --hex <hex>) --out <file.svg> [--version 1-31] [--level L|M|Q|H] [--layout standard|print]\n"
                     "         [--preset plain|concept] [--modules rounded|square|circle|diamond] [--rings joined|dots] [--rays round|flat]\n"
                     "         [--logo <file>] [--logo-radius <units>]\n"; return 2;
    }
    std::vector<uint8_t> payload;
    if (mode == "--hex") { if (!from_hex(value, payload)) { std::cerr << "hex input must be an even number of hexadecimal digits\n"; return 2; } }
    else payload.assign(value.begin(), value.end());
    auto symbol = std::make_unique<ckey_scalable_symbol>(); ckey_diagnostics diag{};
    if (ckey_encode_scalable(payload.data(), payload.size(), version, level, layout, symbol.get(), &diag) != CKEY_OK) { std::cerr << diag.message << '\n'; return 1; }
    ckey_diagnostics check{};
    if (ckey_check_scalable_style(symbol.get(), &style, &check) != CKEY_OK) { std::cerr << "design refused: " << check.message << '\n'; return 1; }
    size_t needed = 0; ckey_render_scalable_styled_svg(symbol.get(), &style, 16, nullptr, 0, &needed);
    std::string svg(needed, '\0');
    if (ckey_render_scalable_styled_svg(symbol.get(), &style, 16, svg.data(), svg.size(), &needed) != CKEY_OK || (!output.empty() && !write_svg(output, svg))) { std::cerr << "SVG rendering failed\n"; return 1; }
    if (!png.empty()) {
        // Print export: black ink on paper on the printer's dot grid. Colours,
        // materials, and logo images are screen styling and are not printed.
        printout::Page page; page.width = page.height = static_cast<int>(std::lround(size_in * dpi)); page.ink.assign(static_cast<size_t>(page.width) * page.height, 0);
        svg.resize(needed - 1); printout::draw_symbol(page, svg, 0, 0, page.width);
        if (!printout::write_png(png, page, dpi)) { std::cerr << "could not write " << png << '\n'; return 1; }
        std::cout << "print PNG: " << png << " (" << size_in << " in at " << dpi << " dpi, 1-bit, black on paper)\n";
    }
    std::cout << diag.message << "; profile=0" << (layout == CKEY_LAYOUT_PRINT ? ".1" : "") << "; format=0."
              << (layout == CKEY_LAYOUT_PRINT ? CKEY_SCALABLE_PRINT_FORMAT_VERSION : CKEY_SCALABLE_FORMAT_VERSION) << ".0; version=" << symbol->version
              << "; payload-bytes=" << payload.size() << "; capacity=" << ckey_scalable_capacity(symbol->version, level, layout) << "; svg=" << output << '\n';
    return 0;
}

// ckey print-sheet --out sheet.png [--dpi 600] [--set small|large]
// A US Letter calibration ladder of P0 and P0.1 symbols at several payloads
// and printed sizes, as a 1-bit device-grid PNG. Each symbol's payload names
// its own cell ("CKEY-CAL P0.1 120B 0.50in ..."), so a scan identifies it.
static int print_sheet(int argc, char **argv) {
    std::string out, set = "small"; double dpi = 600;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string flag = argv[i], arg = argv[i + 1];
        if (flag == "--out") out = arg; else if (flag == "--set") set = arg;
        else if (flag == "--dpi") { dpi = std::strtod(arg.c_str(), nullptr); if (!(dpi >= 150 && dpi <= 2400)) { std::cerr << "--dpi must be 150..2400\n"; return 2; } }
        else { std::cerr << "unknown option " << flag << '\n'; return 2; }
    }
    if (out.empty() || (set != "small" && set != "large") || (argc % 2) != 0) { std::cerr << "usage: ckey print-sheet --out sheet.png [--dpi 600] [--set small|large]\n"; return 2; }
    const std::vector<size_t> payloads = set == "small" ? std::vector<size_t>{8, 30, 120, 400} : std::vector<size_t>{1200, 2400};
    const std::vector<double> sizes = set == "small" ? std::vector<double>{0.25, 0.375, 0.5, 0.75, 1.0} : std::vector<double>{1.0, 1.25, 1.5, 2.0};
    printout::Page page; page.width = static_cast<int>(std::lround(8.5 * dpi)); page.height = static_cast<int>(std::lround(11 * dpi));
    page.ink.assign(static_cast<size_t>(page.width) * page.height, 0);
    const int text = std::max(2, static_cast<int>(std::lround(dpi / 150)));  // label scale
    const auto in = [&](double v) { return static_cast<int>(std::lround(v * dpi)); };
    printout::draw_text(page, "CKey print calibration - print at 100% actual size", in(0.5), in(0.35), text + 1);
    printout::draw_text(page, "dpi " + std::to_string(static_cast<int>(dpi)) + "  level M  black on paper", in(0.5), in(0.35) + 9 * (text + 1), text);
    int y = in(0.9); const double max_size = sizes.back();
    for (size_t bytes : payloads) for (auto layout : {CKEY_LAYOUT_STANDARD, CKEY_LAYOUT_PRINT}) {
        printout::draw_text(page, std::string(layout ? "P0.1 " : "P0 ") + std::to_string(bytes) + "B", in(0.5), y + in(max_size / 2), text);
        int x = in(1.5);
        for (double size : sizes) {
            char label[64]; std::snprintf(label, sizeof(label), "CKEY-CAL %s %zuB %.2fin ", layout ? "P0.1" : "P0", bytes, size);
            std::string payload = label; while (payload.size() < bytes) payload += "0123456789";
            payload.resize(bytes);
            auto symbol = std::make_unique<ckey_scalable_symbol>(); ckey_diagnostics diag{};
            if (ckey_encode_scalable(reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), 0, CKEY_ECC_M, layout, symbol.get(), &diag) != CKEY_OK) {
                std::cerr << "cannot encode " << label << ": " << diag.message << '\n'; return 1;
            }
            size_t needed = 0; ckey_render_scalable_svg(symbol.get(), 8, nullptr, 0, &needed);
            std::string svg(needed, '\0'); ckey_render_scalable_svg(symbol.get(), 8, svg.data(), needed, &needed); svg.resize(needed - 1);
            printout::draw_symbol(page, svg, x, y, in(size));
            char caption[32]; std::snprintf(caption, sizeof(caption), "v%u %.2fin", symbol->version, size);
            printout::draw_text(page, caption, x, y + in(size) + text * 2, text);
            x += in(size) + in(0.3);
        }
        y += in(max_size) + in(0.3);
    }
    if (!printout::write_png(out, page, dpi)) { std::cerr << "could not write " << out << '\n'; return 1; }
    std::cout << "calibration sheet: " << out << " (US Letter, " << dpi << " dpi, 1-bit). Print at 100% / actual size.\n";
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "encode-scalable") return encode_scalable(argc, argv);
    if (argc >= 2 && std::string(argv[1]) == "print-sheet") return print_sheet(argc, argv);
    const bool tierOption = argc == 8 && std::string(argv[2]) == "--tier";
    const int modeIndex = tierOption ? 4 : 2;
    const int outIndex = tierOption ? 6 : 4;
    if ((argc != 6 && !tierOption) || (std::string(argv[modeIndex]) != "--hex" && std::string(argv[modeIndex]) != "--utf8") || std::string(argv[outIndex]) != "--out") {
        std::cerr << "usage:\n"
                  << "  ckey encode-scalable (--utf8 <text> | --hex <hex>) --out <file.svg> [--version 1-31] [--level L|M|Q|H]\n"
                  << "  ckey encode --hex <32-hex-chars> --out <locator.svg>\n"
                  << "  ckey encode-text --utf8 <up-to-200-byte-text> --out <text.svg>\n"
                  << "  ckey encode-text --hex <up-to-400-hex-chars> --out <text.svg>\n"
                  << "  ckey encode-extended --utf8 <up-to-2048-byte-text> --out <extended.svg>\n"
                  << "  ckey encode-extended --tier <256|384|512|768|1024|1536|2048> --utf8 <text> --out <extended.svg>\n"
                  << "  ckey encode-extended --hex <up-to-4096-hex-chars> --out <extended.svg>\n";
        return 2;
    }
    const std::string command = argv[1], mode = argv[modeIndex], value = argv[modeIndex+1], output = argv[outIndex+1];
    if (tierOption && command != "encode-extended") { std::cerr << "--tier applies only to encode-extended\n"; return 2; }
    ckey_extended_tier requestedTier = CKEY_EXTENDED_TIER_AUTO;
    if (tierOption) {
        char *end = nullptr;
        const long parsed = std::strtol(argv[3], &end, 10);
        if (!end || *end != '\0') { std::cerr << "invalid capacity tier\n"; return 2; }
        switch (parsed) {
        case 256: requestedTier=CKEY_EXTENDED_TIER_256; break;
        case 384: requestedTier=CKEY_EXTENDED_TIER_384; break;
        case 512: requestedTier=CKEY_EXTENDED_TIER_512; break;
        case 768: requestedTier=CKEY_EXTENDED_TIER_768; break;
        case 1024: requestedTier=CKEY_EXTENDED_TIER_1024; break;
        case 1536: requestedTier=CKEY_EXTENDED_TIER_1536; break;
        case 2048: requestedTier=CKEY_EXTENDED_TIER_2048; break;
        default: std::cerr << "unsupported capacity tier\n"; return 2;
        }
    }
    ckey_diagnostics diag{};
    std::vector<uint8_t> payload;
    if (mode == "--hex") {
        if (!from_hex(value, payload)) { std::cerr << "hex input must be an even number of hexadecimal digits\n"; return 2; }
    } else payload.assign(value.begin(), value.end());
    if (command == "encode") {
        if (mode != "--hex" || payload.size() != CKEY_LOCATOR_PAYLOAD_BYTES) { std::cerr << "locator input must be exactly 16 bytes encoded as 32 hexadecimal characters\n"; return 2; }
        ckey_symbol symbol{};
        if (ckey_encode_locator(payload.data(), payload.size(), &symbol, &diag) != CKEY_OK) { std::cerr << diag.message << '\n'; return 1; }
        size_t needed = 0; ckey_render_svg(&symbol, 16, nullptr, 0, &needed);
        std::string svg(needed, '\0');
        if (ckey_render_svg(&symbol, 16, svg.data(), svg.size(), &needed) != CKEY_OK || !write_svg(output, svg)) { std::cerr << "SVG rendering failed\n"; return 1; }
        std::cout << diag.message << "; profile=1; format=0." << CKEY_FORMAT_VERSION << ".0; payload-bytes=16; svg=" << output << '\n';
        return 0;
    }
    if (command == "encode-text") {
        if (payload.empty() || payload.size() > CKEY_TEXT_PAYLOAD_MAX_BYTES) { std::cerr << "text input must contain 1 to 200 UTF-8 bytes\n"; return 2; }
        ckey_text_symbol symbol{};
        if (ckey_encode_text(payload.data(), payload.size(), &symbol, &diag) != CKEY_OK) { std::cerr << diag.message << '\n'; return 1; }
        size_t needed = 0; ckey_render_text_svg(&symbol, 16, nullptr, 0, &needed);
        std::string svg(needed, '\0');
        if (ckey_render_text_svg(&symbol, 16, svg.data(), svg.size(), &needed) != CKEY_OK || !write_svg(output, svg)) { std::cerr << "SVG rendering failed\n"; return 1; }
        std::cout << diag.message << "; profile=2; format=0." << CKEY_TEXT_FORMAT_VERSION << ".0; payload-bytes=" << payload.size() << "; codeword-bytes=255; svg=" << output << '\n';
        return 0;
    }
    if (command == "encode-extended") {
        if (payload.empty() || payload.size() > CKEY_EXTENDED_PAYLOAD_MAX_BYTES) { std::cerr << "extended input must contain 1 to 2048 bytes\n"; return 2; }
        ckey_extended_symbol symbol{};
        const ckey_status encoded = ckey_encode_extended_tiered(payload.data(), payload.size(), requestedTier, &symbol, &diag);
        if (encoded != CKEY_OK) { std::cerr << diag.message << '\n'; return 1; }
        size_t needed = 0; ckey_render_extended_svg(&symbol, 16, nullptr, 0, &needed);
        std::string svg(needed, '\0');
        if (ckey_render_extended_svg(&symbol, 16, svg.data(), svg.size(), &needed) != CKEY_OK || !write_svg(output, svg)) { std::cerr << "extended SVG rendering failed\n"; return 1; }
        std::cout << diag.message << "; profile=3; format=0." << CKEY_EXTENDED_FORMAT_VERSION << ".0; payload-bytes=" << payload.size() << "; codeword-bytes=" << CKEY_EXTENDED_CODEWORD_BYTES << "; svg=" << output << '\n';
        return 0;
    }
    std::cerr << "unknown command\n"; return 2;
}
