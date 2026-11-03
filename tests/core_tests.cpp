#include "ckey/ckey.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    assert(ckey_get_abi_version() == 1);
    std::array<uint8_t,16> payload{};
    for (size_t i=0;i<payload.size();++i) payload[i]=static_cast<uint8_t>(i);
    ckey_symbol a{}, b{}; ckey_diagnostics d{};
    assert(ckey_encode_locator(payload.data(), payload.size(), &a, &d) == CKEY_OK);
    assert(d.status == CKEY_OK && a.side == 29 && a.abi_version == 1);
    assert(a.codeword[0]==0xc7 && a.codeword[1]==0x4b && a.codeword[2]==CKEY_FORMAT_VERSION && a.codeword[3]==1);
    assert(a.codeword[4]==0 && a.codeword[5]==16);
    for (size_t i=0;i<16;++i) assert(a.codeword[6+i]==i);
    // CRC-32C("C7 4B 03 01 00 10 00..0F") is a fixed interoperability check.
    assert(a.codeword[22]==0xc1 && a.codeword[23]==0x0f && a.codeword[24]==0x29 && a.codeword[25]==0x79);
    constexpr uint8_t expected_parity[16]={0x75,0xcd,0xe8,0xd7,0x67,0xe3,0x7a,0x85,0x63,0x59,0x0b,0xc0,0xc1,0x41,0x3a,0xb7};
    for (size_t i=0;i<16;++i) assert(a.codeword[26+i]==expected_parity[i]);
    assert(std::strcmp(ckey_get_software_version(), CKEY_SOFTWARE_VERSION)==0);
    std::array<uint8_t,16> decoded{};
    assert(ckey_decode_locator(a.codeword, sizeof(a.codeword), decoded.data(), decoded.size(), &d)==CKEY_OK);
    assert(decoded==payload && d.corrected_symbols==0);
    // Exercise the full unknown-error correction bound at varying positions.
    for (uint32_t pattern=0; pattern<32; ++pattern) {
        auto damaged=a;
        for (size_t e=0;e<8;++e) {
            const size_t pos=(e*5 + pattern*3) % 42;
            damaged.codeword[pos] ^= static_cast<uint8_t>(1 + ((pattern + e*29) % 255));
        }
        decoded.fill(0);
        assert(ckey_decode_locator(damaged.codeword, sizeof(damaged.codeword), decoded.data(), decoded.size(), &d)==CKEY_OK);
        assert(decoded==payload && d.corrected_symbols==8);
    }
    assert(ckey_encode_locator(payload.data(), payload.size(), &b, &d)==CKEY_OK);
    assert(std::memcmp(a.codeword,b.codeword,sizeof(a.codeword))==0);
    assert(ckey_encode_locator(payload.data(), 15, &b, &d)==CKEY_INVALID_ARGUMENT);
    assert(d.status==CKEY_INVALID_ARGUMENT);

    size_t needed=0;
    assert(ckey_render_svg(&a, 16, nullptr, 0, &needed)==CKEY_INSUFFICIENT_BUFFER);
    assert(needed>1000);
    std::string svg(needed,'\0');
    assert(ckey_render_svg(&a, 16, svg.data(), svg.size(), &needed)==CKEY_OK);
    size_t ignored=0;
    assert(ckey_render_svg(&a, 0, nullptr, 0, &ignored)==CKEY_INVALID_ARGUMENT);
    assert(ckey_render_svg(&a, CKEY_MAX_RENDER_SCALE + 1, nullptr, 0, &ignored)==CKEY_INVALID_ARGUMENT);
    assert(svg.find("viewBox=\"-1 -1 31 31\"")!=std::string::npos);
    assert(svg.find("stroke-linecap=\"butt\"")!=std::string::npos);
    assert(svg.find("fill=\"#050505\"")!=std::string::npos);
    assert(svg.find("A 5.0000 5.0000")!=std::string::npos);
    assert(svg.find("CKey locator symbol")!=std::string::npos);
    std::cout << "CKey core tests passed\n";
}
