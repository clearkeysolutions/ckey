#include "ckey/ckey.h"
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

// ckey encode-scalable (--utf8 <text> | --hex <hex>) --out <file.svg>
//                      [--version <1-31>] [--level L|M|Q|H]
static int encode_scalable(int argc, char **argv) {
    std::string mode, value, output; uint32_t version = 0; ckey_ecc_level level = CKEY_ECC_M;
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
        } else { std::cerr << "unknown option " << flag << '\n'; return 2; }
    }
    if ((argc % 2) != 0 || mode.empty() || output.empty()) {
        std::cerr << "usage: ckey encode-scalable (--utf8 <text> | --hex <hex>) --out <file.svg> [--version 1-31] [--level L|M|Q|H]\n"; return 2;
    }
    std::vector<uint8_t> payload;
    if (mode == "--hex") { if (!from_hex(value, payload)) { std::cerr << "hex input must be an even number of hexadecimal digits\n"; return 2; } }
    else payload.assign(value.begin(), value.end());
    auto symbol = std::make_unique<ckey_scalable_symbol>(); ckey_diagnostics diag{};
    if (ckey_encode_scalable(payload.data(), payload.size(), version, level, symbol.get(), &diag) != CKEY_OK) { std::cerr << diag.message << '\n'; return 1; }
    size_t needed = 0; ckey_render_scalable_svg(symbol.get(), 16, nullptr, 0, &needed);
    std::string svg(needed, '\0');
    if (ckey_render_scalable_svg(symbol.get(), 16, svg.data(), svg.size(), &needed) != CKEY_OK || !write_svg(output, svg)) { std::cerr << "SVG rendering failed\n"; return 1; }
    std::cout << diag.message << "; profile=0; format=0." << CKEY_SCALABLE_FORMAT_VERSION << ".0; version=" << symbol->version
              << "; payload-bytes=" << payload.size() << "; capacity=" << ckey_scalable_capacity(symbol->version, level) << "; svg=" << output << '\n';
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "encode-scalable") return encode_scalable(argc, argv);
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
