#include "ckey/ckey.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

static size_t occurrences(const std::string &s, const std::string &needle) {
    size_t count=0, pos=0;
    while ((pos=s.find(needle,pos))!=std::string::npos) { ++count; pos+=needle.size(); }
    return count;
}

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
    // CRC-32C("C7 4B 07 01 00 10 00..0F") is a fixed interoperability check.
    assert(a.codeword[22]==0x1c && a.codeword[23]==0x00 && a.codeword[24]==0xf7 && a.codeword[25]==0x8d);
    constexpr uint8_t expected_parity[51]={0x29,0x9b,0x51,0x2d,0x0b,0xf6,0x66,0x26,0xe2,0x14,0x7f,0x85,0x4c,0x9d,0xd4,0xe4,0x38,0xab,0xbd,0x63,0x8c,0x7e,0x65,0x9f,0xec,0xe8,0x56,0xa7,0x2e,0xd9,0x67,0x20,0xd9,0x68,0xed,0x6b,0xf7,0xa4,0x5d,0xd1,0x30,0xe2,0x42,0x2f,0x17,0xbe,0xac,0x5e,0xf0,0xdd,0x43};
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
    assert(svg.find("fill=\"#050505\"")!=std::string::npos);
    assert(svg.find("A 3.0000 3.0000")!=std::string::npos);
    assert(svg.find("A 4.5000 4.5000")!=std::string::npos);
    assert(occurrences(svg,"r=\"0.1500\"")==occurrences(svg,"<circle "));
    assert(occurrences(svg,"<circle ")>3);
    assert(svg.find("stroke-width=\"0.3000\" stroke-linecap=\"round\"/>")!=std::string::npos);
    assert(occurrences(svg,"stroke-width=\"0.4200\"/>")==12);
    assert(occurrences(svg,"stroke-width=\"0.15\" stroke-linecap=\"round\"/>")==69);
    assert(svg.find("x=\"2.1300\" y=\"11.1300\"")!=std::string::npos);
    assert(svg.find("CKey locator symbol")!=std::string::npos);
    std::cout << "CKey core tests passed\n";
}
