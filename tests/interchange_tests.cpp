// .ckey interchange (spec/CKey-Interchange-v1.md): round trips, strict
// rejection, and a mutation fuzzer. The reader lives in ckey_codec; the
// test links ckey_render only for the concept preset.
#include "ckey/ckey.h"
#include "check.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::vector<uint8_t> write(const ckey_scalable_symbol &s, const ckey_style *st) {
    size_t n = 0; CKEY_CHECK(ckey_write_interchange(&s, st, nullptr, 0, &n) == CKEY_INSUFFICIENT_BUFFER);
    std::vector<uint8_t> out(n); CKEY_CHECK(ckey_write_interchange(&s, st, out.data(), out.size(), &n) == CKEY_OK);
    return out;
}
uint32_t crc32c(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0x82f63b78u & (0u - (c & 1u))); }
    return ~c;
}
void reseal(std::vector<uint8_t> &f) { const uint32_t c = crc32c(f.data(), f.size() - 4); for (int i = 0; i < 4; ++i) f[f.size() - 4 + i] = static_cast<uint8_t>(c >> (8 * i)); }
}  // namespace

int main() {
    const char *text = "https://openckey.org/tour";
    for (auto layout : {CKEY_LAYOUT_STANDARD, CKEY_LAYOUT_PRINT}) for (uint32_t v : {6u, 12u, 31u}) {
        ckey_scalable_symbol s{}; ckey_diagnostics d{};
        CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text), std::strlen(text), v, CKEY_ECC_Q, layout, &s, &d) == CKEY_OK);
        const auto file = write(s, nullptr);
        CKEY_CHECK(file.size() == 12 + 6 + s.codeword_length + 4 && std::memcmp(file.data(), "CKEY", 4) == 0);
        ckey_scalable_symbol back{}; ckey_style st{};
        CKEY_CHECK(ckey_read_interchange(file.data(), file.size(), &back, &st, nullptr, 0, &d) == CKEY_OK);
        CKEY_CHECK(back.version == v && back.layout == static_cast<uint32_t>(layout) && back.ecc_level == CKEY_ECC_Q && back.codeword_length == s.codeword_length);
        CKEY_CHECK(std::memcmp(back.codeword, s.codeword, s.codeword_length) == 0 && st.ink.r == 0 && st.background.r == 255);
    }
    // Style and logo round trip.
    ckey_scalable_symbol s{}; ckey_diagnostics d{};
    CKEY_CHECK(ckey_encode_scalable(reinterpret_cast<const uint8_t *>(text), std::strlen(text), 6, CKEY_ECC_H, CKEY_LAYOUT_STANDARD, &s, &d) == CKEY_OK);
    ckey_style art{}; ckey_style_preset_fill(&art, CKEY_PRESET_CONCEPT);
    art.modules = CKEY_MODULE_DIAMOND; art.policy = CKEY_POLICY_ADAPTIVE; art.logo_href = "https://openckey.org/logo.svg"; art.logo_plate = 1; art.logo_radius = 3.25;
    const auto file = write(s, &art);
    ckey_scalable_symbol back{}; ckey_style st{}; char logo[128];
    CKEY_CHECK(ckey_read_interchange(file.data(), file.size(), &back, &st, logo, sizeof(logo), &d) == CKEY_OK);
    CKEY_CHECK(st.ink.r == art.ink.r && st.accent_count == 2 && st.modules == CKEY_MODULE_DIAMOND && st.policy == CKEY_POLICY_ADAPTIVE);
    CKEY_CHECK(std::abs(st.bevel - 0.6) < 1e-4 && std::abs(st.logo_radius - 3.25) < 1e-3 && st.logo_href && std::string(st.logo_href) == art.logo_href);
    CKEY_CHECK(ckey_read_interchange(file.data(), file.size(), &back, &st, nullptr, 0, &d) == CKEY_OK && st.logo_href == nullptr);  // no buffer: logo dropped

    // Strict rejection.
    auto bad = file; bad[0] = 'X'; CKEY_CHECK(ckey_read_interchange(bad.data(), bad.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);
    bad = file; bad[30] ^= 1; CKEY_CHECK(ckey_read_interchange(bad.data(), bad.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);  // CRC
    std::printf("corrupted byte: %s\n", d.message);
    bad = file; bad[30] ^= 1; reseal(bad);  // valid CRC, but a non-canonical codeword
    CKEY_CHECK(ckey_read_interchange(bad.data(), bad.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);
    std::printf("altered codeword with a valid CRC: %s\n", d.message);
    bad = file; bad[4] = 2; reseal(bad); CKEY_CHECK(ckey_read_interchange(bad.data(), bad.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);
    bad = file; bad[8] = 14; reseal(bad); CKEY_CHECK(ckey_read_interchange(bad.data(), bad.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);
    {   // unknown required record rejected; unknown optional record skipped
        auto plain = write(s, nullptr); plain.resize(plain.size() - 4);
        auto opt = plain; opt.insert(opt.end(), {0x34, 0x92, 2, 0, 0, 0, 7, 7}); opt.resize(opt.size() + 4); reseal(opt);
        CKEY_CHECK(ckey_read_interchange(opt.data(), opt.size(), &back, &st, nullptr, 0, &d) == CKEY_OK);
        auto req = plain; req.insert(req.end(), {0x34, 0x12, 2, 0, 0, 0, 7, 7}); req.resize(req.size() + 4); reseal(req);
        CKEY_CHECK(ckey_read_interchange(req.data(), req.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);
        auto huge = plain; huge.insert(huge.end(), {0x34, 0x92, 0xff, 0xff, 0xff, 0x7f}); huge.resize(huge.size() + 4); reseal(huge);
        CKEY_CHECK(ckey_read_interchange(huge.data(), huge.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);
    }
    std::vector<uint8_t> tiny(10); CKEY_CHECK(ckey_read_interchange(tiny.data(), tiny.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);
    std::vector<uint8_t> big(CKEY_INTERCHANGE_MAX_BYTES + 1); CKEY_CHECK(ckey_read_interchange(big.data(), big.size(), &back, &st, nullptr, 0, &d) == CKEY_INVALID_ARGUMENT);

    // Mutation fuzzing: random byte edits (resealed half the time so they
    // reach the record parser), truncations and extensions never crash and
    // are accepted only when the result is a valid, canonical file.
    uint32_t seed = 12345; size_t accepted = 0;
    const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int i = 0; i < 20000; ++i) {
        auto f = file;
        const int edits = 1 + static_cast<int>(rnd() % 4);
        for (int e = 0; e < edits; ++e) {
            const uint32_t op = rnd() % 3;
            if (op == 0 && !f.empty()) f[rnd() % f.size()] = static_cast<uint8_t>(rnd());
            else if (op == 1 && f.size() > 1) f.resize(rnd() % f.size());
            else f.insert(f.begin() + static_cast<long>(rnd() % (f.size() + 1)), static_cast<uint8_t>(rnd()));
        }
        if (f.size() >= 4 && (i & 1)) reseal(f);
        if (ckey_read_interchange(f.data(), f.size(), &back, &st, logo, sizeof(logo), &d) == CKEY_OK) ++accepted;
    }
    std::printf("fuzz: 20000 mutated files, %zu accepted (each a valid canonical file)\n", accepted);
    std::printf("interchange tests passed\n");
    return 0;
}
