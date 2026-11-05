#include "ckey/ckey.h"
#include <array>
#include "check.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static size_t occurrences(const std::string &s, const std::string &needle) {
    size_t count=0, pos=0;
    while ((pos=s.find(needle,pos))!=std::string::npos) { ++count; pos+=needle.size(); }
    return count;
}

int main() {
    CKEY_CHECK(ckey_get_abi_version() == 3);
    std::array<uint8_t,16> payload{};
    for (size_t i=0;i<payload.size();++i) payload[i]=static_cast<uint8_t>(i);
    ckey_symbol a{}, b{}; ckey_diagnostics d{};
    CKEY_CHECK(ckey_encode_locator(payload.data(), payload.size(), &a, &d) == CKEY_OK);
    CKEY_CHECK(d.status == CKEY_OK && a.side == 29 && a.abi_version == 3);
    CKEY_CHECK(a.codeword[0]==0xc7 && a.codeword[1]==0x4b && a.codeword[2]==CKEY_FORMAT_VERSION && a.codeword[3]==1);
    CKEY_CHECK(a.codeword[4]==0 && a.codeword[5]==16);
    for (size_t i=0;i<16;++i) CKEY_CHECK(a.codeword[6+i]==i);
    // CRC-32C("C7 4B 0E 01 00 10 00..0F") is a fixed interoperability check.
    CKEY_CHECK(a.codeword[22]==0x94 && a.codeword[23]==0xb0 && a.codeword[24]==0xcb && a.codeword[25]==0x29);
    constexpr uint8_t expected_parity[51]={0x45,0xa4,0xf1,0x66,0x5e,0x75,0x71,0x9e,0x8f,0xc5,0xed,0xfb,0xe2,0x4f,0xc1,0x88,0x93,0x17,0x08,0x46,0x9a,0x90,0x2f,0x84,0xb1,0x67,0xcc,0x40,0x4c,0x2f,0xce,0x04,0xdd,0xf4,0xc4,0xb7,0xda,0x58,0xd2,0xa8,0x56,0x37,0x2d,0x2a,0x28,0x67,0x2b,0x08,0x48,0x2c,0x0b};
    for (size_t i=0;i<51;++i) CKEY_CHECK(a.codeword[26+i]==expected_parity[i]);
    CKEY_CHECK(std::strcmp(ckey_get_software_version(), CKEY_SOFTWARE_VERSION)==0);
    std::array<uint8_t,16> decoded{};
    CKEY_CHECK(ckey_decode_locator(a.codeword, sizeof(a.codeword), decoded.data(), decoded.size(), &d)==CKEY_OK);
    CKEY_CHECK(decoded==payload && d.corrected_symbols==0);
    // Exercise every correctable error weight and the full unknown-error bound.
    for (uint32_t pattern=0; pattern<4; ++pattern) for (size_t error_count=1; error_count<=25; ++error_count) {
        auto damaged=a;
        for (size_t e=0;e<error_count;++e) {
            const size_t pos=(e*5 + pattern*3) % 77;
            damaged.codeword[pos] ^= static_cast<uint8_t>(1 + ((pattern + e*29) % 255));
        }
        decoded.fill(0);
        CKEY_CHECK(ckey_decode_locator(damaged.codeword, sizeof(damaged.codeword), decoded.data(), decoded.size(), &d)==CKEY_OK);
        CKEY_CHECK(decoded==payload && d.corrected_symbols==error_count);
    }
    for (uint32_t pattern=0; pattern<32; ++pattern) {
        auto damaged=a;
        for (size_t e=0;e<25;++e) {
            const size_t pos=(e*5 + pattern*3) % 77;
            damaged.codeword[pos] ^= static_cast<uint8_t>(1 + ((pattern + e*29) % 255));
        }
        decoded.fill(0);
        CKEY_CHECK(ckey_decode_locator(damaged.codeword, sizeof(damaged.codeword), decoded.data(), decoded.size(), &d)==CKEY_OK);
        CKEY_CHECK(decoded==payload && d.corrected_symbols==25);
    }
    auto beyond_bound=a;
    for (size_t e=0;e<26;++e) beyond_bound.codeword[(e*3)%77] ^= static_cast<uint8_t>(0x41u + e);
    decoded.fill(0);
    CKEY_CHECK(ckey_decode_locator(beyond_bound.codeword, sizeof(beyond_bound.codeword), decoded.data(), decoded.size(), &d)!=CKEY_OK);
    CKEY_CHECK(ckey_encode_locator(payload.data(), payload.size(), &b, &d)==CKEY_OK);
    CKEY_CHECK(std::memcmp(a.codeword,b.codeword,sizeof(a.codeword))==0);
    CKEY_CHECK(ckey_encode_locator(payload.data(), 15, &b, &d)==CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(d.status==CKEY_INVALID_ARGUMENT);

    std::array<uint8_t, CKEY_TEXT_PAYLOAD_MAX_BYTES> text_payload{};
    for (size_t i = 0; i < text_payload.size(); ++i) text_payload[i] = static_cast<uint8_t>((i * 37 + 11) & 0xff);
    ckey_text_symbol text_symbol{};
    CKEY_CHECK(ckey_encode_text(text_payload.data(), text_payload.size(), &text_symbol, &d) == CKEY_OK);
    CKEY_CHECK(text_symbol.codeword[0] == 0xc7 && text_symbol.codeword[1] == 0x4b && text_symbol.codeword[2] == CKEY_TEXT_FORMAT_VERSION);
    std::array<uint8_t, CKEY_TEXT_PAYLOAD_MAX_BYTES> text_decoded{}; size_t text_length = 0;
    CKEY_CHECK(ckey_decode_text(text_symbol.codeword, sizeof(text_symbol.codeword), text_decoded.data(), text_decoded.size(), &text_length, &d) == CKEY_OK);
    CKEY_CHECK(text_length == text_payload.size() && text_decoded == text_payload);
    ckey_text_symbol repeated_text{};
    CKEY_CHECK(ckey_encode_text(text_payload.data(), 31, &repeated_text, &d) == CKEY_OK);
    CKEY_CHECK(ckey_decode_text(repeated_text.codeword, sizeof(repeated_text.codeword), text_decoded.data(), text_decoded.size(), &text_length, &d) == CKEY_OK);
    CKEY_CHECK(text_length == 31 && std::equal(text_payload.begin(), text_payload.begin()+31, text_decoded.begin()));
    auto damaged_text = text_symbol;
    for (size_t i = 0; i < 22; ++i) damaged_text.codeword[(i * 11 + 7) % CKEY_TEXT_CODEWORD_BYTES] ^= static_cast<uint8_t>(0x31 + i);
    text_decoded.fill(0); text_length = 0;
    CKEY_CHECK(ckey_decode_text(damaged_text.codeword, sizeof(damaged_text.codeword), text_decoded.data(), text_decoded.size(), &text_length, &d) == CKEY_OK);
    CKEY_CHECK(text_length == text_payload.size() && text_decoded == text_payload && d.corrected_symbols == 22);
    size_t text_needed = 0;
    CKEY_CHECK(ckey_render_text_svg(&text_symbol, 16, nullptr, 0, &text_needed) == CKEY_INSUFFICIENT_BUFFER);
    std::string text_svg(text_needed, '\0');
    CKEY_CHECK(ckey_render_text_svg(&text_symbol, 16, text_svg.data(), text_svg.size(), &text_needed) == CKEY_OK);
    CKEY_CHECK(text_svg.find("CKey text symbol profile 2") != std::string::npos);
    CKEY_CHECK(text_svg.find("stroke-width=\"0.2000\" stroke-linecap=\"round\"") != std::string::npos);
    CKEY_CHECK(occurrences(text_svg,"stroke-width=\"0.8000\"/>")==4);
    CKEY_CHECK(text_svg.find("M 1.7000 4.0000 A 2.3000 2.3000") != std::string::npos);

    std::array<uint8_t, CKEY_EXTENDED_PAYLOAD_MAX_BYTES> extended_payload{};
    for (size_t i=0;i<extended_payload.size();++i) extended_payload[i]=static_cast<uint8_t>((i*73+19)&0xff);
    ckey_extended_symbol extended{};
    CKEY_CHECK(ckey_encode_extended(extended_payload.data(),extended_payload.size(),&extended,&d)==CKEY_OK);
    // Profile 3's public stream is byte-interleaved: the header's bytes 0, 1,
    // and 2 from block zero therefore occur at offsets 0, 10, and 20.
    CKEY_CHECK(extended.side==116 && extended.codeword[0]==0xc7 && extended.codeword[10]==0x4b && extended.codeword[20]==CKEY_EXTENDED_FORMAT_VERSION);
    std::array<uint8_t, CKEY_EXTENDED_PAYLOAD_MAX_BYTES> extended_decoded{}; size_t extended_length=0;
    CKEY_CHECK(ckey_decode_extended(extended.codeword,sizeof(extended.codeword),extended_decoded.data(),extended_decoded.size(),&extended_length,&d)==CKEY_OK);
    CKEY_CHECK(extended_length==extended_payload.size() && extended_decoded==extended_payload);
    const std::string short_extended_text="CKey renderer test";
    ckey_extended_symbol short_extended{}, short_extended_repeat{};
    CKEY_CHECK(ckey_encode_extended(reinterpret_cast<const uint8_t *>(short_extended_text.data()),short_extended_text.size(),&short_extended,&d)==CKEY_OK);
    CKEY_CHECK(short_extended.codeword[20]==CKEY_EXTENDED_FORMAT_VERSION && short_extended.codeword[30]==3 && short_extended.codeword[40]==1);
    CKEY_CHECK(ckey_encode_extended(reinterpret_cast<const uint8_t *>(short_extended_text.data()),short_extended_text.size(),&short_extended_repeat,&d)==CKEY_OK);
    CKEY_CHECK(std::memcmp(short_extended.codeword,short_extended_repeat.codeword,sizeof(short_extended.codeword))==0);
    std::array<uint8_t,64> short_extended_decoded{}; size_t short_extended_length=0;
    CKEY_CHECK(ckey_decode_extended(short_extended.codeword,sizeof(short_extended.codeword),short_extended_decoded.data(),short_extended_decoded.size(),&short_extended_length,&d)==CKEY_OK);
    CKEY_CHECK(short_extended_length==short_extended_text.size() && std::equal(short_extended_text.begin(),short_extended_text.end(),short_extended_decoded.begin()));
    ckey_extended_symbol short_extended_high_ecc{};
    CKEY_CHECK(ckey_encode_extended_tiered(reinterpret_cast<const uint8_t *>(short_extended_text.data()),short_extended_text.size(),CKEY_EXTENDED_TIER_512,&short_extended_high_ecc,&d)==CKEY_OK);
    CKEY_CHECK(short_extended_high_ecc.codeword[40]==3);
    constexpr std::array<ckey_extended_tier,7> tiers={CKEY_EXTENDED_TIER_256,CKEY_EXTENDED_TIER_384,CKEY_EXTENDED_TIER_512,CKEY_EXTENDED_TIER_768,CKEY_EXTENDED_TIER_1024,CKEY_EXTENDED_TIER_1536,CKEY_EXTENDED_TIER_2048};
    constexpr std::array<size_t,7> capacities={256,384,512,768,1024,1536,2048};
    for (size_t tier=0;tier<tiers.size();++tier) {
        const size_t length=capacities[tier]-3;
        std::vector<uint8_t> payload(length);
        for(size_t i=0;i<length;++i) payload[i]=static_cast<uint8_t>((i*41+tier*17+3)&0xff);
        ckey_extended_symbol symbol{};
        CKEY_CHECK(ckey_encode_extended_tiered(payload.data(),payload.size(),tiers[tier],&symbol,&d)==CKEY_OK);
        std::vector<uint8_t> recovered(CKEY_EXTENDED_PAYLOAD_MAX_BYTES); size_t recovered_length=0;
        CKEY_CHECK(ckey_decode_extended(symbol.codeword,sizeof(symbol.codeword),recovered.data(),recovered.size(),&recovered_length,&d)==CKEY_OK);
        CKEY_CHECK(recovered_length==length && std::equal(payload.begin(),payload.end(),recovered.begin()));
        auto damaged=symbol;
        constexpr size_t errors_per_block=6;
        for(size_t block=0;block<CKEY_EXTENDED_BLOCK_COUNT;++block) for(size_t error=0;error<errors_per_block;++error)
            damaged.codeword[(error*19+5)*CKEY_EXTENDED_BLOCK_COUNT+block]^=static_cast<uint8_t>(0x51+error+block);
        recovered_length=0;
        CKEY_CHECK(ckey_decode_extended(damaged.codeword,sizeof(damaged.codeword),recovered.data(),recovered.size(),&recovered_length,&d)==CKEY_OK);
        CKEY_CHECK(recovered_length==length && std::equal(payload.begin(),payload.end(),recovered.begin()));
        CKEY_CHECK(d.corrected_symbols==errors_per_block*CKEY_EXTENDED_BLOCK_COUNT);
    }
    CKEY_CHECK(ckey_encode_extended_tiered(extended_payload.data(),300,CKEY_EXTENDED_TIER_256,&extended,&d)==CKEY_INVALID_ARGUMENT);
    // The automatic 2,048-byte tier is RS(255,206), correcting 24 unknown
    // byte errors per block. Interleaving distributes local loss across blocks.
    auto damaged_extended=extended;
    for (size_t block=0;block<CKEY_EXTENDED_BLOCK_COUNT;++block) for (size_t i=0;i<24;++i)
        damaged_extended.codeword[(i*13%CKEY_EXTENDED_CODEWORD_BYTES_PER_BLOCK)*CKEY_EXTENDED_BLOCK_COUNT+block]^=static_cast<uint8_t>(0x31+i+block);
    extended_decoded.fill(0); extended_length=0;
    CKEY_CHECK(ckey_decode_extended(damaged_extended.codeword,sizeof(damaged_extended.codeword),extended_decoded.data(),extended_decoded.size(),&extended_length,&d)==CKEY_OK);
    CKEY_CHECK(extended_length==extended_payload.size() && extended_decoded==extended_payload && d.corrected_symbols==240);
    size_t extended_needed=0;
    CKEY_CHECK(ckey_render_extended_svg(&extended,16,nullptr,0,&extended_needed)==CKEY_INSUFFICIENT_BUFFER);
    std::string extended_svg(extended_needed,'\0');
    CKEY_CHECK(ckey_render_extended_svg(&extended,16,extended_svg.data(),extended_svg.size(),&extended_needed)==CKEY_OK);
    CKEY_CHECK(extended_svg.find("CKey extended symbol profile 3")!=std::string::npos);
    CKEY_CHECK(extended_svg.find("stroke-width=\"0.2800\"")!=std::string::npos);
    CKEY_CHECK(extended_svg.find("viewBox=\"-4 -4 124 124\"")!=std::string::npos);
    CKEY_CHECK(occurrences(extended_svg,"stroke-width=\"3.2000\"/>")==4);
    CKEY_CHECK(extended_svg.find("M 6.8000 16.0000 A 9.2000 9.2000")!=std::string::npos);

    size_t needed=0;
    CKEY_CHECK(ckey_render_svg(&a, 16, nullptr, 0, &needed)==CKEY_INSUFFICIENT_BUFFER);
    CKEY_CHECK(needed>1000);
    std::string svg(needed,'\0');
    CKEY_CHECK(ckey_render_svg(&a, 16, svg.data(), svg.size(), &needed)==CKEY_OK);
    size_t ignored=0;
    CKEY_CHECK(ckey_render_svg(&a, 0, nullptr, 0, &ignored)==CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_render_svg(&a, CKEY_MAX_RENDER_SCALE + 1, nullptr, 0, &ignored)==CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(svg.find("viewBox=\"-1 -1 31 31\"")!=std::string::npos);
    CKEY_CHECK(svg.find("fill=\"#ffffff\"")!=std::string::npos);
    CKEY_CHECK(svg.find("A 2.3000 2.3000")!=std::string::npos);
    CKEY_CHECK(occurrences(svg,"r=\"0.1500\"")==occurrences(svg,"<circle "));
    CKEY_CHECK(occurrences(svg,"<circle ")>3);
    CKEY_CHECK(svg.find("stroke-width=\"0.3000\" stroke-linecap=\"round\"/>")!=std::string::npos);
    CKEY_CHECK(occurrences(svg,"stroke-width=\"0.8000\"/>")==4);
    CKEY_CHECK(svg.find("M 1.7000 4.0000 A 2.3000 2.3000")!=std::string::npos);
    CKEY_CHECK(occurrences(svg,"stroke-width=\"0.15\" stroke-linecap=\"round\"/>")==69);
    CKEY_CHECK(svg.find("x=\"2.1300\" y=\"11.1300\"")!=std::string::npos);
    CKEY_CHECK(svg.find("CKey locator symbol")!=std::string::npos);
    std::cout << "CKey core tests passed\n";
}
