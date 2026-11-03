#include "ckey/ckey.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    assert(ckey_get_abi_version() == 2);
    std::array<uint8_t,16> payload{};
    for (size_t i=0;i<payload.size();++i) payload[i]=static_cast<uint8_t>(i);
    ckey_symbol a{}, b{}; ckey_diagnostics d{};
    assert(ckey_encode_locator(payload.data(), payload.size(), &a, &d) == CKEY_OK);
    assert(d.status == CKEY_OK && a.side == 29 && a.abi_version == 2);
    assert(a.codeword[0]==0xc7 && a.codeword[1]==0x4b && a.codeword[2]==CKEY_FORMAT_VERSION && a.codeword[3]==1);
    assert(a.codeword[4]==0 && a.codeword[5]==16);
    for (size_t i=0;i<16;++i) assert(a.codeword[6+i]==i);
    // CRC-32C("C7 4B 06 01 00 10 00..0F") is a fixed interoperability check.
    assert(a.codeword[22]==0x2b && a.codeword[23]==0x43 && a.codeword[24]==0x00 && a.codeword[25]==0x30);
    constexpr uint8_t expected_parity[51]={0x00,0x93,0x47,0x35,0x18,0x1d,0xc0,0x80,0x3d,0x8c,0x6d,0xad,0xc5,0x67,0x2a,0xcf,0xbc,0xc9,0x47,0xa2,0x03,0x14,0xd9,0x10,0xb8,0x21,0x67,0x3a,0xc3,0xbe,0x8b,0x80,0xd6,0x7f,0x91,0x0c,0x5f,0x28,0x25,0x3f,0x20,0xb9,0xc9,0x03,0x0a,0x20,0xba,0x57,0xaa,0xb6,0x7c};
    for (size_t i=0;i<51;++i) assert(a.codeword[26+i]==expected_parity[i]);
    assert(std::strcmp(ckey_get_software_version(), CKEY_SOFTWARE_VERSION)==0);
    std::array<uint8_t,16> decoded{};
    assert(ckey_decode_locator(a.codeword, sizeof(a.codeword), decoded.data(), decoded.size(), &d)==CKEY_OK);
    assert(decoded==payload && d.corrected_symbols==0);
    // Exercise every correctable error weight and the full unknown-error bound.
    for (uint32_t pattern=0; pattern<4; ++pattern) for (size_t error_count=1; error_count<=25; ++error_count) {
        auto damaged=a;
        for (size_t e=0;e<error_count;++e) {
            const size_t pos=(e*5 + pattern*3) % 77;
            damaged.codeword[pos] ^= static_cast<uint8_t>(1 + ((pattern + e*29) % 255));
        }
        decoded.fill(0);
        assert(ckey_decode_locator(damaged.codeword, sizeof(damaged.codeword), decoded.data(), decoded.size(), &d)==CKEY_OK);
        assert(decoded==payload && d.corrected_symbols==error_count);
    }
    for (uint32_t pattern=0; pattern<32; ++pattern) {
        auto damaged=a;
        for (size_t e=0;e<25;++e) {
            const size_t pos=(e*5 + pattern*3) % 77;
            damaged.codeword[pos] ^= static_cast<uint8_t>(1 + ((pattern + e*29) % 255));
        }
        decoded.fill(0);
        assert(ckey_decode_locator(damaged.codeword, sizeof(damaged.codeword), decoded.data(), decoded.size(), &d)==CKEY_OK);
        assert(decoded==payload && d.corrected_symbols==25);
    }
    auto beyond_bound=a;
    for (size_t e=0;e<26;++e) beyond_bound.codeword[(e*3)%77] ^= static_cast<uint8_t>(0x41u + e);
    decoded.fill(0);
    assert(ckey_decode_locator(beyond_bound.codeword, sizeof(beyond_bound.codeword), decoded.data(), decoded.size(), &d)!=CKEY_OK);
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
    assert(svg.find("A 4.5000 4.5000")!=std::string::npos);
    assert(svg.find("r=\"0.113")!=std::string::npos);
    assert(svg.find("stroke-width=\"0.376")!=std::string::npos);
    assert(svg.find("stroke-width=\"1.0000\"")!=std::string::npos);
    assert(svg.find("x=\"2.1300\" y=\"11.1300\"")!=std::string::npos);
    assert(svg.find("CKey locator symbol")!=std::string::npos);
    std::cout << "CKey core tests passed\n";
}
