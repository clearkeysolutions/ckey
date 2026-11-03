#include "ckey/ckey.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static bool from_hex(const std::string &input, std::vector<uint8_t> &out) {
    if (input.size() % 2 != 0) return false;
    out.resize(input.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned value = 0;
        if (std::sscanf(input.c_str() + 2 * i, "%2x", &value) != 1) return false;
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

static bool write_svg(const std::string &path, const std::string &svg) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(svg.data(), static_cast<std::streamsize>(svg.size() - 1));
    return static_cast<bool>(file);
}

int main(int argc, char **argv) {
    if (argc != 6 || (std::string(argv[2]) != "--hex" && std::string(argv[2]) != "--utf8") || std::string(argv[4]) != "--out") {
        std::cerr << "usage:\n"
                  << "  ckey encode --hex <32-hex-chars> --out <locator.svg>\n"
                  << "  ckey encode-text --utf8 <up-to-200-byte-text> --out <text.svg>\n"
                  << "  ckey encode-text --hex <up-to-400-hex-chars> --out <text.svg>\n";
        return 2;
    }
    const std::string command = argv[1], mode = argv[2], value = argv[3], output = argv[5];
    ckey_diagnostics diag{};
    std::vector<uint8_t> payload;
    if (mode == "--hex") {
        if (!from_hex(value, payload)) { std::cerr << "hex input must contain complete bytes\n"; return 2; }
    } else payload.assign(value.begin(), value.end());
    if (command == "encode") {
        if (mode != "--hex" || payload.size() != CKEY_LOCATOR_PAYLOAD_BYTES) { std::cerr << "locator input must be exactly 16 bytes encoded as 32 hexadecimal characters\n"; return 2; }
        ckey_symbol symbol{};
        if (ckey_encode_locator(payload.data(), payload.size(), &symbol, &diag) != CKEY_OK) { std::cerr << diag.message << '\n'; return 1; }
        size_t needed = 0; ckey_render_svg(&symbol, 16, nullptr, 0, &needed);
        std::string svg(needed, '\0');
        if (ckey_render_svg(&symbol, 16, svg.data(), svg.size(), &needed) != CKEY_OK || !write_svg(output, svg)) { std::cerr << "SVG rendering failed\n"; return 1; }
        std::cout << diag.message << "; profile=locator-v1; payload-bytes=16; svg=" << output << '\n';
        return 0;
    }
    if (command == "encode-text") {
        if (payload.empty() || payload.size() > CKEY_TEXT_PAYLOAD_MAX_BYTES) { std::cerr << "text input must contain 1 to 200 UTF-8 bytes\n"; return 2; }
        ckey_text_symbol symbol{};
        if (ckey_encode_text(payload.data(), payload.size(), &symbol, &diag) != CKEY_OK) { std::cerr << diag.message << '\n'; return 1; }
        size_t needed = 0; ckey_render_text_svg(&symbol, 16, nullptr, 0, &needed);
        std::string svg(needed, '\0');
        if (ckey_render_text_svg(&symbol, 16, svg.data(), svg.size(), &needed) != CKEY_OK || !write_svg(output, svg)) { std::cerr << "SVG rendering failed\n"; return 1; }
        std::cout << diag.message << "; profile=text-v2; payload-bytes=" << payload.size() << "; codeword-bytes=255; svg=" << output << '\n';
        return 0;
    }
    std::cerr << "unknown command\n"; return 2;
}
