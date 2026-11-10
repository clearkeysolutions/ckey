// CKey conformance vectors (specification §6, acceptance criterion 1).
//
//   ckey-conformance --write tests/vectors/conformance-v1.json
//   ckey-conformance --check tests/vectors/conformance-v1.json
//
// Each vector fixes an input (profile, version, level, layout, payload) and
// the expected outputs: the codeword (hex, plus FNV-1a 64), the canonical
// geometry (shape count and an FNV-1a 64 over every shape with coordinates
// rounded to 1e-6 units), the .ckey file hash, and a behavioural check that
// a binary raster at 8 px/module decodes back to the payload. Any SDK can
// load the same JSON and compare its own results. Hashes are FNV-1a 64 over
// the bytes described in tools/conformance/README.md.
#include "ckey/ckey.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

uint64_t fnv(const uint8_t *p, size_t n, uint64_t h = 0xcbf29ce484222325ull) { for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; } return h; }
std::string hex(const uint8_t *p, size_t n) { static const char *d = "0123456789abcdef"; std::string s; for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; } return s; }
std::string hex64(uint64_t v) { char b[17]; std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(v)); return b; }
std::vector<uint8_t> unhex(const std::string &s) { std::vector<uint8_t> o; for (size_t i = 0; i + 1 < s.size(); i += 2) o.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16))); return o; }

struct Input { std::string id, profile; uint32_t version = 0, level = 1, layout = 0; std::vector<uint8_t> payload; };
using Result = std::map<std::string, std::string>;

// Geometry hash: for each shape, kind, layer, paint, round_cap as int32 LE,
// then p[0..4] and width as int64 LE of round(value * 1e6).
uint64_t geometry_hash(const std::vector<ckey_shape> &shapes) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (const auto &s : shapes) {
        const int32_t ints[4] = {static_cast<int32_t>(s.kind), static_cast<int32_t>(s.layer), s.paint, static_cast<int32_t>(s.round_cap)};
        for (int32_t v : ints) { uint8_t b[4]; for (int i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(static_cast<uint32_t>(v) >> (8 * i)); h = fnv(b, 4, h); }
        const double vals[6] = {s.p[0], s.p[1], s.p[2], s.p[3], s.p[4], s.width};
        for (double v : vals) { const int64_t q = std::llround(v * 1e6); uint8_t b[8]; for (int i = 0; i < 8; ++i) b[i] = static_cast<uint8_t>(static_cast<uint64_t>(q) >> (8 * i)); h = fnv(b, 8, h); }
    }
    return h;
}

Result run(const Input &in) {
    Result r; ckey_diagnostics d{};
    if (in.profile == "0") {
        auto s = std::make_unique<ckey_scalable_symbol>();
        if (ckey_encode_scalable(in.payload.data(), in.payload.size(), in.version, static_cast<ckey_ecc_level>(in.level), static_cast<ckey_scalable_layout>(in.layout), s.get(), &d) != CKEY_OK) { r["error"] = d.message; return r; }
        r["version"] = std::to_string(s->version);
        r["codeword_fnv1a64"] = hex64(fnv(s->codeword, s->codeword_length));
        if (s->codeword_length <= 160) r["codeword_hex"] = hex(s->codeword, s->codeword_length);
        size_t count = 0; ckey_scalable_geometry(s.get(), nullptr, nullptr, 0, &count);
        std::vector<ckey_shape> shapes(count); ckey_scalable_geometry(s.get(), nullptr, shapes.data(), shapes.size(), &count);
        r["shape_count"] = std::to_string(count);
        r["geometry_fnv1a64"] = hex64(geometry_hash(shapes));
        size_t n = 0; ckey_write_interchange(s.get(), nullptr, nullptr, 0, &n);
        std::vector<uint8_t> file(n); ckey_write_interchange(s.get(), nullptr, file.data(), n, &n);
        r["ckey_file_fnv1a64"] = hex64(fnv(file.data(), n));
        // Behavioural: binary raster at 8 px/module decodes to the payload.
        const uint32_t modules = ckey_scalable_modules_across(s->version);
        ckey_raster_options o; ckey_raster_options_default(&o);
        o.size_px = static_cast<uint32_t>(std::ceil(31.0 * 8 * modules / 29.0)); o.policy = CKEY_RASTER_BINARY; o.format = CKEY_PIXELS_GRAY8;
        std::vector<uint8_t> px(static_cast<size_t>(o.size_px) * o.size_px);
        bool ok = ckey_render_scalable_raster(s.get(), nullptr, &o, px.data(), px.size(), &n) == CKEY_OK;
        const double k = o.size_px / 31.0, q[8] = {k, k, 30 * k, k, 30 * k, 30 * k, k, 30 * k};
        std::vector<uint8_t> out(65535); size_t len = 0; uint32_t v = 0, lay = 0;
        ok = ok && ckey_decode_scalable_gray8(px.data(), o.size_px, o.size_px, o.size_px, q, out.data(), out.size(), &len, &v, &lay, &d) == CKEY_OK &&
             len == in.payload.size() && std::equal(in.payload.begin(), in.payload.end(), out.begin()) && v == s->version && lay == in.layout;
        r["raster_decodes"] = ok ? "true" : "false";
    } else if (in.profile == "1") {
        ckey_symbol s{}; if (ckey_encode_locator(in.payload.data(), in.payload.size(), &s, &d) != CKEY_OK) { r["error"] = d.message; return r; }
        r["codeword_hex"] = hex(s.codeword, sizeof(s.codeword)); r["codeword_fnv1a64"] = hex64(fnv(s.codeword, sizeof(s.codeword)));
    } else if (in.profile == "2") {
        auto s = std::make_unique<ckey_text_symbol>(); if (ckey_encode_text(in.payload.data(), in.payload.size(), s.get(), &d) != CKEY_OK) { r["error"] = d.message; return r; }
        r["codeword_fnv1a64"] = hex64(fnv(s->codeword, sizeof(s->codeword)));
    } else if (in.profile == "3") {
        auto s = std::make_unique<ckey_extended_symbol>(); if (ckey_encode_extended(in.payload.data(), in.payload.size(), s.get(), &d) != CKEY_OK) { r["error"] = d.message; return r; }
        r["codeword_fnv1a64"] = hex64(fnv(s->codeword, sizeof(s->codeword)));
    }
    return r;
}

std::vector<Input> default_inputs() {
    std::vector<Input> v;
    const auto bytes = [](size_t n, uint32_t seed) { std::vector<uint8_t> p(n); for (auto &b : p) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(seed >> 24); } return p; };
    const auto text = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    v.push_back({"p0-auto-url", "0", 0, 1, 0, text("https://openckey.org/tour")});
    v.push_back({"p0-empty", "0", 1, 1, 0, {}});
    v.push_back({"p0-v1-L", "0", 1, 0, 0, bytes(30, 1)});
    for (uint32_t ver : {2u, 7u, 16u, 31u}) for (uint32_t level : {0u, 3u}) {
        const size_t cap = ckey_scalable_capacity(ver, static_cast<ckey_ecc_level>(level), CKEY_LAYOUT_STANDARD);
        v.push_back({"p0-v" + std::to_string(ver) + "-" + "LMQHUX"[level] + "-full", "0", ver, level, 0, bytes(cap, ver * 7 + level)});
    }
    for (uint32_t ver : {1u, 3u, 12u, 31u}) {
        const size_t cap = ckey_scalable_capacity(ver, CKEY_ECC_M, CKEY_LAYOUT_PRINT);
        v.push_back({"p01-v" + std::to_string(ver) + "-M", "0", ver, 1, 1, bytes(std::min<size_t>(cap, 100), ver * 13)});
    }
    v.push_back({"p0-vcard", "0", 0, 2, 0, text("BEGIN:VCARD\nVERSION:4.0\nFN:Ada Lovelace\nTEL:+15551234567\nEND:VCARD")});
    v.push_back({"p1-locator", "1", 0, 0, 0, bytes(16, 99)});
    v.push_back({"p2-text", "2", 0, 0, 0, text("CKey Profile 2 conformance vector")});
    v.push_back({"p3-extended", "3", 0, 0, 0, bytes(600, 5)});
    return v;
}

// --- minimal JSON for this file's own shape (objects, arrays, strings, numbers) ---
struct J { enum T { Null, Num, Str, Arr, Obj } t = Null; double n = 0; std::string s; std::vector<J> a; std::vector<std::string> keys; std::vector<J> values;  // object members (vector<J> allows the incomplete type)
    const J *get(const std::string &k) const { for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == k) return &values[i]; return nullptr; } };
struct P { const std::string &s; size_t i = 0;
    void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
    J parse() { ws(); J j; if (s[i] == '{') { ++i; j.t = J::Obj; ws(); if (s[i] == '}') { ++i; return j; }
            for (;;) { ws(); J k = parse(); ws(); ++i; J v = parse(); j.keys.push_back(k.s); j.values.push_back(v); ws(); if (s[i++] == '}') break; } }
        else if (s[i] == '[') { ++i; j.t = J::Arr; ws(); if (s[i] == ']') { ++i; return j; } for (;;) { j.a.push_back(parse()); ws(); if (s[i++] == ']') break; } }
        else if (s[i] == '"') { ++i; j.t = J::Str; while (s[i] != '"') { if (s[i] == '\\') ++i; j.s += s[i++]; } ++i; }
        else { j.t = J::Num; size_t e = i; while (e < s.size() && (std::isdigit(static_cast<unsigned char>(s[e])) || s[e] == '-' || s[e] == '.')) ++e; j.n = std::stod(s.substr(i, e - i)); i = e; }
        return j; } };

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3 || (std::string(argv[1]) != "--write" && std::string(argv[1]) != "--check")) { std::cerr << "usage: ckey-conformance --write|--check <file.json>\n"; return 2; }
    const std::string path = argv[2];
    if (std::string(argv[1]) == "--write") {
        std::ofstream f(path);
        f << "{\n  \"suite\": \"ckey-conformance\",\n  \"suite_version\": 1,\n  \"generated_by\": \"" << ckey_get_software_version() << "\",\n"
          << "  \"hash\": \"FNV-1a 64; see tools/conformance/README.md\",\n  \"vectors\": [\n";
        const auto inputs = default_inputs();
        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto &in = inputs[i]; const Result r = run(in);
            f << "    {\"id\": \"" << in.id << "\", \"profile\": \"" << in.profile << "\", \"version\": " << in.version << ", \"level\": " << in.level
              << ", \"layout\": " << in.layout << ", \"payload_hex\": \"" << hex(in.payload.data(), in.payload.size()) << "\",\n     \"expect\": {";
            size_t k = 0; for (const auto &[key, value] : r) f << (k++ ? ", " : "") << '"' << key << "\": \"" << value << '"';
            f << "}}" << (i + 1 < inputs.size() ? "," : "") << "\n";
        }
        f << "  ]\n}\n";
        std::cout << "wrote " << inputs.size() << " vectors to " << path << '\n';
        return 0;
    }
    std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); const std::string text = ss.str();
    if (text.empty()) { std::cerr << "cannot read " << path << '\n'; return 2; }
    P parser{text}; const J root = parser.parse();
    size_t passed = 0, failed = 0;
    for (const J &v : root.get("vectors")->a) {
        Input in; in.id = v.get("id")->s; in.profile = v.get("profile")->s;
        in.version = static_cast<uint32_t>(v.get("version")->n); in.level = static_cast<uint32_t>(v.get("level")->n); in.layout = static_cast<uint32_t>(v.get("layout")->n);
        in.payload = unhex(v.get("payload_hex")->s);
        const Result got = run(in);
        bool ok = true;
        const J &expect = *v.get("expect");
        for (size_t e = 0; e < expect.keys.size(); ++e) {
            const std::string &key = expect.keys[e]; const J &want = expect.values[e];
            const auto it = got.find(key);
            if (it == got.end() || it->second != want.s) { std::cerr << in.id << ": " << key << " expected " << want.s << " got " << (it == got.end() ? "(missing)" : it->second) << '\n'; ok = false; }
        }
        (ok ? passed : failed)++;
    }
    std::cout << "conformance: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
