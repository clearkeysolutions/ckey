// Public C ABI: Safe Actions (spec/CKey-Safe-Actions.md). Included after core_internal.inc.
//
// A Safe Action payload is the text "ckey-action:1:" followed by the base64url
// (no padding) of a signed envelope:
//
//   version u8 (1) | scope u8 (0 same-device, 1 cross-device)
//   issuer   u8 len + bytes   DNS name of the issuing site, lower case
//   app      u8 len + bytes   ID of the receiving app, e.g. com.viadere
//   action   u8 len + bytes   an action the receiving app registered
//   key_id   u8 len + bytes   which of the issuer's keys signed it
//   request  u8 len + bytes   single-use request ID, 8..64 characters
//   issued_at u64 BE, expires_at u64 BE   Unix seconds
//   params   u16 BE len + bytes   at most 1024; UTF-8 JSON is recommended
//   signature 64 bytes   Ed25519 over "CKey Safe Action v1\0" + every byte above
//
// The core parses, signs and verifies. Whether an action may run is decided by
// the SDKs from the verified result and what the receiving app registered.

#include "ed25519.inc"

namespace {

constexpr char kSafeActionPrefix[] = "ckey-action:1:";
constexpr size_t kSafeActionPrefixLen = sizeof(kSafeActionPrefix) - 1;
constexpr char kSafeActionContext[] = "CKey Safe Action v1";  // signed with its terminating NUL

bool sa_issuer_ok(const uint8_t *p, size_t n) {
    if (n < 1 || n > 253 || p[0] == '.' || p[n - 1] == '.') return false;
    bool dot = false;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = p[i];
        if (c == '.') { if (i && p[i - 1] == '.') return false; dot = true; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return dot;
}

bool sa_token_ok(const uint8_t *p, size_t n, size_t min, size_t max, bool lower_only, bool allow_dot) {
    if (n < min || n > max) return false;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = p[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                        (!lower_only && c >= 'A' && c <= 'Z') || (allow_dot && c == '.');
        if (!ok) return false;
    }
    return true;
}

int b64url_value(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

bool b64url_decode(const uint8_t *in, size_t n, std::vector<uint8_t> &out) {
    if (n % 4 == 1) return false;
    out.clear();
    out.reserve(n * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const int v = b64url_value(in[i]);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
        }
    }
    // Unused low bits must be zero, so each envelope has exactly one text form.
    return (acc & ((1u << bits) - 1)) == 0;
}

void b64url_encode(const std::vector<uint8_t> &in, std::string &out) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t acc = 0;
    int bits = 0;
    for (uint8_t b : in) {
        acc = (acc << 8) | b;
        bits += 8;
        while (bits >= 6) { bits -= 6; out.push_back(kAlphabet[(acc >> bits) & 63]); }
    }
    if (bits) out.push_back(kAlphabet[(acc << (6 - bits)) & 63]);
}

// Parses the decoded envelope into out; returns the offset of the signature, or 0 on error.
size_t sa_parse_bytes(const std::vector<uint8_t> &b, ckey_safe_action *out, const char **why) {
    size_t at = 0;
    auto need = [&](size_t n) { return at + n <= b.size(); };
    if (!need(2) || b[0] != 1) { *why = "unsupported Safe Action version"; return 0; }
    if (b[1] > 1) { *why = "unknown scope"; return 0; }
    std::memset(out, 0, sizeof(*out));
    out->abi_version = CKEY_ABI_VERSION;
    out->scope = b[1];
    at = 2;
    struct Field { char *dest; size_t cap; const char *name; int kind; size_t min, max; };
    const Field fields[] = {
        {out->issuer, sizeof(out->issuer), "issuer", 0, 1, 253},
        {out->app, sizeof(out->app), "app", 1, 1, 255},
        {out->action, sizeof(out->action), "action", 2, 1, 64},
        {out->key_id, sizeof(out->key_id), "key ID", 3, 1, 32},
        {out->request_id, sizeof(out->request_id), "request ID", 4, 8, 64},
    };
    for (const Field &f : fields) {
        if (!need(1)) { *why = "truncated envelope"; return 0; }
        const size_t n = b[at++];
        if (!need(n)) { *why = "truncated envelope"; return 0; }
        const uint8_t *p = b.data() + at;
        const bool ok = f.kind == 0 ? sa_issuer_ok(p, n)
                      : f.kind == 1 ? sa_token_ok(p, n, f.min, f.max, false, true)
                      : f.kind == 2 ? sa_token_ok(p, n, f.min, f.max, true, true)
                      : f.kind == 3 ? sa_token_ok(p, n, f.min, f.max, false, true)
                                    : sa_token_ok(p, n, f.min, f.max, false, false);
        if (!ok || n + 1 > f.cap) { *why = f.name; return 0; }
        std::memcpy(f.dest, p, n);
        f.dest[n] = '\0';
        at += n;
    }
    if (!need(18)) { *why = "truncated envelope"; return 0; }
    for (int i = 0; i < 8; ++i) out->issued_at = (out->issued_at << 8) | b[at++];
    for (int i = 0; i < 8; ++i) out->expires_at = (out->expires_at << 8) | b[at++];
    const size_t plen = (size_t(b[at]) << 8) | b[at + 1];
    at += 2;
    if (plen > sizeof(out->params)) { *why = "params longer than 1024 bytes"; return 0; }
    if (!need(plen)) { *why = "truncated envelope"; return 0; }
    std::memcpy(out->params, b.data() + at, plen);
    out->params_len = plen;
    at += plen;
    if (b.size() != at + 64) { *why = "missing or extra signature bytes"; return 0; }
    return at;
}

ckey_status sa_decode(const uint8_t *payload, size_t len, std::vector<uint8_t> &bytes, ckey_safe_action *out,
                      size_t *sig_at, ckey_diagnostics *diag) {
    if (!payload || !out) { set_status(diag, CKEY_INVALID_ARGUMENT, "payload and output are required"); return CKEY_INVALID_ARGUMENT; }
    if (len <= kSafeActionPrefixLen || std::memcmp(payload, kSafeActionPrefix, kSafeActionPrefixLen) != 0) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "not a Safe Action (no ckey-action:1: prefix)");
        return CKEY_INVALID_ARGUMENT;
    }
    if (len > kSafeActionPrefixLen + 4096 || !b64url_decode(payload + kSafeActionPrefixLen, len - kSafeActionPrefixLen, bytes)) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "Safe Action envelope is not valid base64url");
        return CKEY_INVALID_ARGUMENT;
    }
    const char *why = "malformed envelope";
    *sig_at = sa_parse_bytes(bytes, out, &why);
    if (!*sig_at) {
        std::memset(out, 0, sizeof(*out));
        set_status(diag, CKEY_INVALID_ARGUMENT, (std::string("invalid Safe Action: ") + why).c_str());
        return CKEY_INVALID_ARGUMENT;
    }
    return CKEY_OK;
}

}  // namespace

extern "C" {

int ckey_is_safe_action(const uint8_t *payload, size_t len) {
    return payload && len > kSafeActionPrefixLen && std::memcmp(payload, kSafeActionPrefix, kSafeActionPrefixLen) == 0;
}

ckey_status ckey_safe_action_parse(const uint8_t *payload, size_t len, ckey_safe_action *out, ckey_diagnostics *diag) {
    std::vector<uint8_t> bytes;
    size_t sig_at = 0;
    const ckey_status st = sa_decode(payload, len, bytes, out, &sig_at, diag);
    if (st == CKEY_OK) set_status(diag, CKEY_OK, "parsed; the signature has not been checked");
    return st;
}

ckey_status ckey_safe_action_verify(const uint8_t *payload, size_t len, const uint8_t public_key[32], uint64_t now,
                                    uint32_t max_lifetime_seconds, ckey_safe_action *out, ckey_diagnostics *diag) {
    if (!public_key) { set_status(diag, CKEY_INVALID_ARGUMENT, "a public key is required"); return CKEY_INVALID_ARGUMENT; }
    std::vector<uint8_t> bytes;
    size_t sig_at = 0;
    ckey_status st = sa_decode(payload, len, bytes, out, &sig_at, diag);
    if (st != CKEY_OK) return st;
    std::vector<uint8_t> message(kSafeActionContext, kSafeActionContext + sizeof(kSafeActionContext));
    message.insert(message.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(sig_at));
    if (!ckey_crypto::ed25519_verify(bytes.data() + sig_at, message.data(), message.size(), public_key)) {
        std::memset(out, 0, sizeof(*out));
        set_status(diag, CKEY_INTEGRITY_FAILURE, "the signature does not match this key");
        return CKEY_INTEGRITY_FAILURE;
    }
    const uint64_t lifetime = max_lifetime_seconds ? max_lifetime_seconds : 600;
    const uint64_t skew = 120;  // tolerated clock difference, seconds
    const char *expired = nullptr;
    if (out->expires_at <= out->issued_at) expired = "expires before it is issued";
    else if (out->expires_at - out->issued_at > lifetime) expired = "valid for longer than this app allows";
    else if (now + skew < out->issued_at) expired = "issued in the future";
    else if (now >= out->expires_at) expired = "expired";
    if (expired) {
        set_status(diag, CKEY_EXPIRED, (std::string("Safe Action ") + expired).c_str());
        return CKEY_EXPIRED;  // out keeps the parsed, verified fields for diagnostics
    }
    set_status(diag, CKEY_OK, "verified");
    return CKEY_OK;
}

ckey_status ckey_safe_action_sign(const ckey_safe_action *fields, const uint8_t seed[32], char *out, size_t out_capacity,
                                  size_t *out_len, ckey_diagnostics *diag) {
    if (!fields || !seed) { set_status(diag, CKEY_INVALID_ARGUMENT, "fields and a signing seed are required"); return CKEY_INVALID_ARGUMENT; }
    if (fields->scope > 1) { set_status(diag, CKEY_INVALID_ARGUMENT, "unknown scope"); return CKEY_INVALID_ARGUMENT; }
    std::vector<uint8_t> b = {1, static_cast<uint8_t>(fields->scope)};
    const char *strings[] = {fields->issuer, fields->app, fields->action, fields->key_id, fields->request_id};
    const size_t caps[] = {sizeof(fields->issuer), sizeof(fields->app), sizeof(fields->action), sizeof(fields->key_id), sizeof(fields->request_id)};
    for (int i = 0; i < 5; ++i) {
        const size_t n = strnlen(strings[i], caps[i]);
        if (n == caps[i] || n > 255) { set_status(diag, CKEY_INVALID_ARGUMENT, "a text field is not terminated"); return CKEY_INVALID_ARGUMENT; }
        b.push_back(static_cast<uint8_t>(n));
        b.insert(b.end(), strings[i], strings[i] + n);
    }
    for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>(fields->issued_at >> (8 * i)));
    for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>(fields->expires_at >> (8 * i)));
    if (fields->params_len > sizeof(fields->params)) { set_status(diag, CKEY_INVALID_ARGUMENT, "params longer than 1024 bytes"); return CKEY_INVALID_ARGUMENT; }
    b.push_back(static_cast<uint8_t>(fields->params_len >> 8));
    b.push_back(static_cast<uint8_t>(fields->params_len));
    b.insert(b.end(), fields->params, fields->params + fields->params_len);
    // Validate with the same parser readers use, so a signed envelope always parses.
    {
        std::vector<uint8_t> probe = b;
        probe.resize(probe.size() + 64);
        ckey_safe_action check;
        const char *why = "malformed";
        if (!sa_parse_bytes(probe, &check, &why)) { set_status(diag, CKEY_INVALID_ARGUMENT, (std::string("invalid Safe Action: ") + why).c_str()); return CKEY_INVALID_ARGUMENT; }
    }
    std::vector<uint8_t> message(kSafeActionContext, kSafeActionContext + sizeof(kSafeActionContext));
    message.insert(message.end(), b.begin(), b.end());
    uint8_t sig[64];
    ckey_crypto::ed25519_sign(sig, message.data(), message.size(), seed);
    b.insert(b.end(), sig, sig + 64);
    std::string text(kSafeActionPrefix);
    b64url_encode(b, text);
    if (out_len) *out_len = text.size();
    if (!out || out_capacity < text.size() + 1) { set_status(diag, CKEY_INSUFFICIENT_BUFFER, "output buffer too small"); return CKEY_INSUFFICIENT_BUFFER; }
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    set_status(diag, CKEY_OK, "signed");
    return CKEY_OK;
}

void ckey_ed25519_public_key(const uint8_t seed[32], uint8_t out_public_key[32]) {
    if (seed && out_public_key) ckey_crypto::ed25519_public_key(out_public_key, seed);
}

}  // extern "C"
