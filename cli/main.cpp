#include "ckey/ckey.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

static bool from_hex(const std::string &s, std::array<uint8_t,16> &out) {
    if (s.size() != 32) return false;
    for (size_t i=0;i<16;i++) {
        unsigned v=0;
        if (std::sscanf(s.c_str()+2*i, "%2x", &v)!=1) return false;
        out[i]=static_cast<uint8_t>(v);
    }
    return true;
}
int main(int argc, char **argv) {
    if (argc != 6 || std::string(argv[1]) != "encode" || std::string(argv[2]) != "--hex" || std::string(argv[4]) != "--out") {
        std::cerr << "usage: ckey encode --hex <32-hex-chars> --out <symbol.svg>\n"; return 2;
    }
    std::array<uint8_t,16> payload{};
    if (!from_hex(argv[3], payload)) { std::cerr << "locator input must be 16 bytes encoded as 32 hexadecimal characters\n"; return 2; }
    ckey_symbol symbol{}; ckey_diagnostics diag{};
    if (ckey_encode_locator(payload.data(), payload.size(), &symbol, &diag) != CKEY_OK) { std::cerr << diag.message << '\n'; return 1; }
    size_t n=0; ckey_render_svg(&symbol, 16, nullptr, 0, &n);
    std::string svg(n, '\0');
    if (ckey_render_svg(&symbol, 16, svg.data(), svg.size(), &n) != CKEY_OK) { std::cerr << "SVG rendering failed\n"; return 1; }
    std::ofstream f(argv[5], std::ios::binary);
    if (!f) { std::cerr << "cannot open output path\n"; return 1; }
    f.write(svg.data(), static_cast<std::streamsize>(n-1));
    std::cout << diag.message << "; profile=locator-v1; codeword-bytes=42; svg=" << argv[5] << '\n';
    return f ? 0 : 1;
}
