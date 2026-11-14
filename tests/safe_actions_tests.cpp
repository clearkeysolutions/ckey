// Safe Actions: Ed25519 against RFC 8032, the envelope against vectors from an
// independent implementation (tools/safe_actions_vectors.mjs), and sign/parse
// round trips through the public C ABI.
#include <ckey/ckey.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"

namespace {

std::vector<uint8_t> hex(const std::string &s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return v;
}

// A small JSON reader: enough for the vector file (objects, arrays, strings, integers).
struct Json {
    enum Kind { Null, Num, Str, Arr, Obj } kind = Null;
    long long num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;
    const Json &operator[](const std::string &k) const { static Json none; auto it = obj.find(k); return it == obj.end() ? none : it->second; }
};

struct Reader {
    const std::string &s;
    size_t i = 0;
    void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
    Json value() {
        ws();
        Json j;
        if (s[i] == '{') {
            j.kind = Json::Obj; ++i; ws();
            if (s[i] == '}') { ++i; return j; }
            while (true) { ws(); std::string k = string(); ws(); CKEY_CHECK(s[i] == ':'); ++i; j.obj[k] = value(); ws(); if (s[i] == ',') { ++i; continue; } CKEY_CHECK(s[i] == '}'); ++i; return j; }
        }
        if (s[i] == '[') {
            j.kind = Json::Arr; ++i; ws();
            if (s[i] == ']') { ++i; return j; }
            while (true) { j.arr.push_back(value()); ws(); if (s[i] == ',') { ++i; continue; } CKEY_CHECK(s[i] == ']'); ++i; return j; }
        }
        if (s[i] == '"') { j.kind = Json::Str; j.str = string(); return j; }
        j.kind = Json::Num;
        size_t end = i;
        while (end < s.size() && (std::isdigit(static_cast<unsigned char>(s[end])) || s[end] == '-')) ++end;
        j.num = std::stoll(s.substr(i, end - i));
        i = end;
        return j;
    }
    std::string string() {
        CKEY_CHECK(s[i] == '"'); ++i;
        std::string out;
        while (s[i] != '"') {
            if (s[i] == '\\') { ++i; out.push_back(s[i] == 'n' ? '\n' : s[i]); ++i; }
            else out.push_back(s[i++]);
        }
        ++i;
        return out;
    }
};

void test_rfc8032() {
    // RFC 8032 section 7.1, tests 1 and 2, through the public key derivation and the envelope signer's key.
    uint8_t pk[32];
    ckey_ed25519_public_key(hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60").data(), pk);
    CKEY_CHECK(std::memcmp(pk, hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a").data(), 32) == 0);
    ckey_ed25519_public_key(hex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb").data(), pk);
    CKEY_CHECK(std::memcmp(pk, hex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c").data(), 32) == 0);
}

void test_vectors(const char *path) {
    std::ifstream f(path);
    CKEY_CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    Reader r{text};
    const Json root = r.value();
    CKEY_CHECK(root["schema"].num == 1);
    int checked = 0;
    for (const Json &v : root["vectors"].arr) {
        const std::string &payload = v["payload"].str;
        const std::vector<uint8_t> key = hex(v["public_key"].str);
        auto out = std::make_unique<ckey_safe_action>();
        ckey_diagnostics diag{};
        const bool es = v["alg"].str == "ES256";
        const ckey_status st = es ? ckey_safe_action_verify_key(reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), CKEY_SAFE_ACTION_ES256,
                                                                key.data(), key.size(), static_cast<uint64_t>(v["now"].num),
                                                                static_cast<uint32_t>(v["max_lifetime"].num), out.get(), &diag)
                                  : ckey_safe_action_verify(reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), key.data(),
                                                            static_cast<uint64_t>(v["now"].num), static_cast<uint32_t>(v["max_lifetime"].num), out.get(), &diag);
        const std::string &expect = v["expect"].str;
        const bool ok = (expect == "ok" && st == CKEY_OK) || (expect == "integrity" && st == CKEY_INTEGRITY_FAILURE) ||
                        (expect == "expired" && st == CKEY_EXPIRED) || (expect == "invalid" && st == CKEY_INVALID_ARGUMENT) ||
                        (expect == "invalid_or_integrity" && (st == CKEY_INVALID_ARGUMENT || st == CKEY_INTEGRITY_FAILURE));
        if (!ok) std::fprintf(stderr, "vector \"%s\": expected %s, got status %d (%s)\n", v["name"].str.c_str(), expect.c_str(), st, diag.message);
        CKEY_CHECK(ok);
        if (expect == "ok") {
            const Json &fields = v["fields"];
            CKEY_CHECK(out->scope == static_cast<uint32_t>(fields["scope"].num));
            CKEY_CHECK(fields["issuer"].str == out->issuer && fields["app"].str == out->app && fields["action"].str == out->action);
            CKEY_CHECK(fields["key_id"].str == out->key_id && fields["request_id"].str == out->request_id);
            CKEY_CHECK(out->issued_at == static_cast<uint64_t>(fields["issued_at"].num) && out->expires_at == static_cast<uint64_t>(fields["expires_at"].num));
            CKEY_CHECK(fields["params"].str == std::string(reinterpret_cast<const char *>(out->params), out->params_len));
        }
        ++checked;
    }
    CKEY_CHECK(checked >= 9);
    std::printf("safe actions: %d vectors in %s\n", checked, path);
}

// ES256 issuing without the private key: the signing input and assembly
// rebuild a payload signed elsewhere byte for byte, and DER signatures (as key
// services return) convert to raw r || s with s made low.
void test_es256_issuing(const char *path) {
    std::ifstream f(path);
    CKEY_CHECK(f.good());
    std::stringstream ss; ss << f.rdbuf();
    const std::string text = ss.str();
    Reader r{text};
    const Json root = r.value();
    const Json *valid = nullptr;
    for (const Json &v : root["vectors"].arr) if (v["expect"].str == "ok") { valid = &v; break; }
    CKEY_CHECK(valid != nullptr);
    const std::string &payload = (*valid)["payload"].str;
    const std::vector<uint8_t> key = hex((*valid)["public_key"].str);
    auto fields = std::make_unique<ckey_safe_action>();
    ckey_diagnostics diag{};
    CKEY_CHECK(ckey_safe_action_parse(reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), fields.get(), &diag) == CKEY_OK);
    // The signature is the envelope's last 64 bytes.
    std::vector<uint8_t> env;
    {
        const std::string b64 = payload.substr(std::strlen("ckey-action:1:"));
        uint32_t acc = 0; int bits = 0;
        for (char c : b64) {
            const int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '-' ? 62 : 63;
            acc = (acc << 6) | static_cast<uint32_t>(v); bits += 6;
            if (bits >= 8) { bits -= 8; env.push_back(static_cast<uint8_t>(acc >> bits)); }
        }
    }
    const uint8_t *sig = env.data() + env.size() - 64;
    size_t need = 0;
    CKEY_CHECK(ckey_safe_action_signing_input(fields.get(), nullptr, 0, &need, &diag) == CKEY_INSUFFICIENT_BUFFER);
    std::vector<uint8_t> input(need);
    CKEY_CHECK(ckey_safe_action_signing_input(fields.get(), input.data(), input.size(), &need, &diag) == CKEY_OK);
    const char context[] = "CKey Safe Action v1";
    CKEY_CHECK(need == sizeof(context) + env.size() - 64 && std::memcmp(input.data(), context, sizeof(context)) == 0 &&
               std::memcmp(input.data() + sizeof(context), env.data(), env.size() - 64) == 0);
    std::vector<char> rebuilt(payload.size() + 1);
    CKEY_CHECK(ckey_safe_action_assemble(fields.get(), sig, rebuilt.data(), rebuilt.size(), &need, &diag) == CKEY_OK);
    CKEY_CHECK(need == payload.size() && payload == rebuilt.data());
    // DER round trip, including a high-S signature normalised to the same raw form.
    const auto der_of = [](const uint8_t *rs) {
        std::vector<uint8_t> out = {0x30, 0};
        for (int part = 0; part < 2; ++part) {
            const uint8_t *v = rs + 32 * part; size_t i = 0;
            while (i < 31 && v[i] == 0) ++i;
            std::vector<uint8_t> n(v + i, v + 32);
            if (n[0] & 0x80) n.insert(n.begin(), 0);
            out.push_back(0x02); out.push_back(static_cast<uint8_t>(n.size())); out.insert(out.end(), n.begin(), n.end());
        }
        out[1] = static_cast<uint8_t>(out.size() - 2);
        return out;
    };
    uint8_t raw[64];
    const std::vector<uint8_t> der = der_of(sig);
    CKEY_CHECK(ckey_es256_signature_from_der(der.data(), der.size(), raw) == CKEY_OK && std::memcmp(raw, sig, 64) == 0);
    // n - s (high-S) as DER converts back to the low-S signature.
    static const uint8_t n[32] = {0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51};
    uint8_t high[64]; std::memcpy(high, sig, 32);
    int borrow = 0;
    for (int i = 31; i >= 0; --i) { int d = n[i] - sig[32 + i] - borrow; borrow = d < 0; high[32 + i] = static_cast<uint8_t>(d + (borrow ? 256 : 0)); }
    const std::vector<uint8_t> high_der = der_of(high);
    CKEY_CHECK(ckey_es256_signature_from_der(high_der.data(), high_der.size(), raw) == CKEY_OK && std::memcmp(raw, sig, 64) == 0);
    // ... while the raw high-S signature itself is refused.
    CKEY_CHECK(ckey_safe_action_assemble(fields.get(), high, rebuilt.data(), rebuilt.size(), &need, &diag) == CKEY_OK);
    auto out = std::make_unique<ckey_safe_action>();
    CKEY_CHECK(ckey_safe_action_verify_key(reinterpret_cast<const uint8_t *>(rebuilt.data()), need, CKEY_SAFE_ACTION_ES256, key.data(), key.size(),
                                           static_cast<uint64_t>((*valid)["now"].num), 600, out.get(), &diag) == CKEY_INTEGRITY_FAILURE);
    // Malformed DER and wrong key lengths are refused.
    const uint8_t junk[] = {0x30, 0x06, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01};
    CKEY_CHECK(ckey_es256_signature_from_der(junk, sizeof junk, raw) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_safe_action_verify_key(reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), CKEY_SAFE_ACTION_ES256, key.data(), 32,
                                           0, 600, out.get(), &diag) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_safe_action_verify_key(reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), static_cast<ckey_safe_action_alg>(9), key.data(), key.size(),
                                           0, 600, out.get(), &diag) == CKEY_INVALID_ARGUMENT);
    std::puts("safe actions: ES256 signing input, assembly, DER conversion and low-S");
}

void test_round_trip() {
    const std::vector<uint8_t> seed = hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    uint8_t pk[32];
    ckey_ed25519_public_key(seed.data(), pk);
    auto fields = std::make_unique<ckey_safe_action>();
    fields->scope = CKEY_SCOPE_CROSS_DEVICE;
    std::strcpy(fields->issuer, "citeory.com");
    std::strcpy(fields->app, "com.viadere");
    std::strcpy(fields->action, "signin.request");
    std::strcpy(fields->key_id, "2026-11");
    std::strcpy(fields->request_id, "sess-0123456789ab");
    fields->issued_at = 1794000000;
    fields->expires_at = 1794000060;
    const char params[] = "{\"return\":\"/dashboard\"}";
    std::memcpy(fields->params, params, sizeof(params) - 1);
    fields->params_len = sizeof(params) - 1;
    size_t need = 0;
    ckey_diagnostics diag{};
    CKEY_CHECK(ckey_safe_action_sign(fields.get(), seed.data(), nullptr, 0, &need, &diag) == CKEY_INSUFFICIENT_BUFFER);
    std::vector<char> text(need + 1);
    CKEY_CHECK(ckey_safe_action_sign(fields.get(), seed.data(), text.data(), text.size(), &need, &diag) == CKEY_OK);
    const auto *bytes = reinterpret_cast<const uint8_t *>(text.data());
    CKEY_CHECK(ckey_is_safe_action(bytes, need) == 1);
    CKEY_CHECK(ckey_is_safe_action(reinterpret_cast<const uint8_t *>("https://x"), 9) == 0);
    auto parsed = std::make_unique<ckey_safe_action>();
    CKEY_CHECK(ckey_safe_action_parse(bytes, need, parsed.get(), &diag) == CKEY_OK);
    CKEY_CHECK(std::strcmp(parsed->action, "signin.request") == 0 && parsed->scope == CKEY_SCOPE_CROSS_DEVICE);
    auto verified = std::make_unique<ckey_safe_action>();
    CKEY_CHECK(ckey_safe_action_verify(bytes, need, pk, 1794000030, 600, verified.get(), &diag) == CKEY_OK);
    CKEY_CHECK(std::memcmp(verified->params, params, sizeof(params) - 1) == 0);
    CKEY_CHECK(ckey_safe_action_verify(bytes, need, pk, 1794000061, 600, verified.get(), &diag) == CKEY_EXPIRED);
    CKEY_CHECK(ckey_safe_action_verify(bytes, need, pk, 1794000030, 30, verified.get(), &diag) == CKEY_EXPIRED);
    // Every single-character change to the envelope is refused.
    const size_t prefix = std::strlen("ckey-action:1:");
    int refused = 0;
    for (size_t i = prefix; i < need; ++i) {
        std::string changed(text.data(), need);
        changed[i] = changed[i] == 'A' ? 'B' : 'A';
        if (ckey_safe_action_verify(reinterpret_cast<const uint8_t *>(changed.data()), changed.size(), pk, 1794000030, 600, verified.get(), &diag) != CKEY_OK) ++refused;
    }
    CKEY_CHECK(refused == static_cast<int>(need - prefix));
    // Fields the format cannot carry are refused at signing time.
    std::strcpy(fields->issuer, "Not A Domain");
    CKEY_CHECK(ckey_safe_action_sign(fields.get(), seed.data(), text.data(), text.size(), &need, &diag) == CKEY_INVALID_ARGUMENT);
    std::strcpy(fields->issuer, "citeory.com");
    fields->params_len = 2000;
    CKEY_CHECK(ckey_safe_action_sign(fields.get(), seed.data(), text.data(), text.size(), &need, &diag) == CKEY_INVALID_ARGUMENT);
    std::puts("safe actions: round trip, tamper and field checks");
}

}  // namespace

int main(int argc, char **argv) {
    test_rfc8032();
    test_round_trip();
    test_vectors(argc > 1 ? argv[1] : "tests/vectors/safe-actions-v1.json");
    test_vectors(argc > 2 ? argv[2] : "tests/vectors/safe-actions-es256-v1.json");
    test_es256_issuing(argc > 2 ? argv[2] : "tests/vectors/safe-actions-es256-v1.json");
    return 0;
}
