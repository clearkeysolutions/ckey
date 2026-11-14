// SHA-256 (FIPS 180-4) and ECDSA P-256 verification (FIPS 186-5, SEC 1) for
// Safe Actions signed with ES256 (spec/CKey-Safe-Actions.md).
//
// Verification only: the issuer signs in its own key service. Everything here
// handles public data (keys, signatures, messages), so variable-time
// arithmetic is fine. Numbers are 256-bit, eight 32-bit limbs, least
// significant first; products use 64-bit intermediates only, so this builds
// on every compiler the core supports. Field and scalar multiplication use
// Montgomery multiplication (CIOS) over the field prime p and the group order
// n. Points use Jacobian coordinates with a = -3.
//
// Checked against signatures made by OpenSSL and by Node's crypto
// (tests/safe_actions_tests.cpp, tests/vectors/safe-actions-v1.json).

namespace ckey_crypto {

// ---------------------------------------------------------------- SHA-256

static const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t sha256_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline void sha256_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) | (uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t t1 = k + (sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25)) + ((e & f) ^ (~e & g)) + kSha256K[i] + w[i];
        const uint32_t t2 = (sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

inline void sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= len; i += 64) sha256_block(h, data + i);
    uint8_t tail[128] = {0};
    const size_t rest = len - i;
    if (rest) std::memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    const size_t blocks = rest + 9 <= 64 ? 1 : 2;
    const uint64_t bits = static_cast<uint64_t>(len) * 8;
    for (int b = 0; b < 8; ++b) tail[blocks * 64 - 1 - b] = static_cast<uint8_t>(bits >> (8 * b));
    for (size_t b = 0; b < blocks; ++b) sha256_block(h, tail + 64 * b);
    for (int j = 0; j < 8; ++j) for (int b = 0; b < 4; ++b) out[4 * j + b] = static_cast<uint8_t>(h[j] >> (24 - 8 * b));
}

// ---------------------------------------------------------------- 256-bit numbers

struct U256 { uint32_t w[8]; };

inline U256 u256_from_be(const uint8_t *b) {
    U256 r{};
    for (int i = 0; i < 8; ++i) r.w[7 - i] = (uint32_t(b[4 * i]) << 24) | (uint32_t(b[4 * i + 1]) << 16) | (uint32_t(b[4 * i + 2]) << 8) | b[4 * i + 3];
    return r;
}
inline void u256_to_be(const U256 &a, uint8_t *b) {
    for (int i = 0; i < 8; ++i) for (int k = 0; k < 4; ++k) b[4 * i + k] = static_cast<uint8_t>(a.w[7 - i] >> (24 - 8 * k));
}
inline int u256_cmp(const U256 &a, const U256 &b) {
    for (int i = 7; i >= 0; --i) if (a.w[i] != b.w[i]) return a.w[i] < b.w[i] ? -1 : 1;
    return 0;
}
inline bool u256_zero(const U256 &a) { for (uint32_t v : a.w) if (v) return false; return true; }
// r = a + b, returns the carry.
inline uint32_t u256_add(U256 &r, const U256 &a, const U256 &b) {
    uint64_t c = 0;
    for (int i = 0; i < 8; ++i) { c += uint64_t(a.w[i]) + b.w[i]; r.w[i] = static_cast<uint32_t>(c); c >>= 32; }
    return static_cast<uint32_t>(c);
}
// r = a - b, returns the borrow.
inline uint32_t u256_sub(U256 &r, const U256 &a, const U256 &b) {
    int64_t c = 0;
    for (int i = 0; i < 8; ++i) { c += int64_t(a.w[i]) - b.w[i]; r.w[i] = static_cast<uint32_t>(c); c = c < 0 ? -1 : 0; }
    return c ? 1u : 0u;
}

// Arithmetic modulo an odd 256-bit modulus m, in Montgomery form (R = 2^256).
struct Mod {
    U256 m, r2, one;   // r2 = R^2 mod m; one = R mod m (1 in Montgomery form)
    uint32_t minv;     // -m^-1 mod 2^32
    explicit Mod(const U256 &modulus) : m(modulus) {
        uint32_t x = 1;  // Newton iteration for m^-1 mod 2^32
        for (int i = 0; i < 5; ++i) x *= 2 - m.w[0] * x;
        minv = static_cast<uint32_t>(0u - x);
        // R mod m, then R^2 mod m by 256 doublings.
        U256 t{}; t.w[0] = 1;
        for (int i = 0; i < 256; ++i) t = add(t, t);
        one = t;
        for (int i = 0; i < 256; ++i) t = add(t, t);
        r2 = t;
    }
    U256 add(const U256 &a, const U256 &b) const {
        U256 r; const uint32_t c = u256_add(r, a, b);
        if (c || u256_cmp(r, m) >= 0) u256_sub(r, r, m);
        return r;
    }
    U256 sub(const U256 &a, const U256 &b) const {
        U256 r; if (u256_sub(r, a, b)) u256_add(r, r, m);
        return r;
    }
    // Montgomery product a * b / R mod m (CIOS).
    U256 mul(const U256 &a, const U256 &b) const {
        uint32_t t[10] = {0};
        for (int i = 0; i < 8; ++i) {
            uint64_t c = 0;
            for (int j = 0; j < 8; ++j) { c += uint64_t(t[j]) + uint64_t(a.w[j]) * b.w[i]; t[j] = static_cast<uint32_t>(c); c >>= 32; }
            c += t[8]; t[8] = static_cast<uint32_t>(c); t[9] = static_cast<uint32_t>(c >> 32);
            const uint32_t q = t[0] * minv;
            c = uint64_t(t[0]) + uint64_t(q) * m.w[0]; c >>= 32;
            for (int j = 1; j < 8; ++j) { c += uint64_t(t[j]) + uint64_t(q) * m.w[j]; t[j - 1] = static_cast<uint32_t>(c); c >>= 32; }
            c += t[8]; t[7] = static_cast<uint32_t>(c); c >>= 32;
            t[8] = t[9] + static_cast<uint32_t>(c); t[9] = 0;
        }
        U256 r; for (int i = 0; i < 8; ++i) r.w[i] = t[i];
        if (t[8] || u256_cmp(r, m) >= 0) u256_sub(r, r, m);
        return r;
    }
    U256 to(const U256 &a) const { return mul(a, r2); }        // into Montgomery form (a < m)
    U256 from(const U256 &a) const { U256 o{}; o.w[0] = 1; return mul(a, o); }
    // a^e (a in Montgomery form, e plain), left to right.
    U256 pow(const U256 &a, const U256 &e) const {
        U256 r = one;
        for (int i = 255; i >= 0; --i) {
            r = mul(r, r);
            if ((e.w[i / 32] >> (i % 32)) & 1) r = mul(r, a);
        }
        return r;
    }
    U256 inv(const U256 &a) const {  // Fermat: a^(m-2); m is prime
        U256 e = m, two{}; two.w[0] = 2; u256_sub(e, e, two);
        return pow(a, e);
    }
    // Reduces any 256-bit value below m (m > 2^255 here, so one subtraction).
    U256 reduce(const U256 &a) const { U256 r = a; if (u256_cmp(r, m) >= 0) u256_sub(r, r, m); return r; }
};

inline U256 u256_hex(const char *h) {
    uint8_t b[32];
    for (int i = 0; i < 32; ++i) {
        auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
        b[i] = static_cast<uint8_t>(v(h[2 * i]) * 16 + v(h[2 * i + 1]));
    }
    return u256_from_be(b);
}

struct P256 {
    Mod fp, fn;
    U256 b, gx, gy, half_n;  // b, G in Montgomery form (mod p); half_n plain
    P256() : fp(u256_hex("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff")),
             fn(u256_hex("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551")) {
        b = fp.to(u256_hex("5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b"));
        gx = fp.to(u256_hex("6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"));
        gy = fp.to(u256_hex("4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5"));
        half_n = fn.m;  // n >> 1
        for (int i = 0; i < 8; ++i) half_n.w[i] = (fn.m.w[i] >> 1) | (i < 7 ? fn.m.w[i + 1] << 31 : 0);
    }
};
inline const P256 &p256() { static const P256 curve; return curve; }

// Jacobian point (X, Y, Z) in Montgomery form; Z = 0 is the point at infinity.
struct Jac { U256 x, y, z; };

inline Jac jac_double(const Jac &p) {
    const Mod &f = p256().fp;
    if (u256_zero(p.z) || u256_zero(p.y)) return {f.one, f.one, U256{}};
    // dbl-2001-b (a = -3)
    const U256 delta = f.mul(p.z, p.z), gamma = f.mul(p.y, p.y), beta = f.mul(p.x, gamma);
    const U256 t = f.mul(f.sub(p.x, delta), f.add(p.x, delta));
    const U256 alpha = f.add(f.add(t, t), t);
    const U256 b4 = f.add(f.add(beta, beta), f.add(beta, beta)), b8 = f.add(b4, b4);
    const U256 x3 = f.sub(f.mul(alpha, alpha), b8);
    const U256 yz = f.add(p.y, p.z);
    const U256 z3 = f.sub(f.sub(f.mul(yz, yz), gamma), delta);
    const U256 g2 = f.mul(gamma, gamma), g8 = f.add(f.add(f.add(g2, g2), f.add(g2, g2)), f.add(f.add(g2, g2), f.add(g2, g2)));
    const U256 y3 = f.sub(f.mul(alpha, f.sub(b4, x3)), g8);
    return {x3, y3, z3};
}

inline Jac jac_add(const Jac &p, const Jac &q) {
    const Mod &f = p256().fp;
    if (u256_zero(p.z)) return q;
    if (u256_zero(q.z)) return p;
    // add-1998-cmo-2
    const U256 z1z1 = f.mul(p.z, p.z), z2z2 = f.mul(q.z, q.z);
    const U256 u1 = f.mul(p.x, z2z2), u2 = f.mul(q.x, z1z1);
    const U256 s1 = f.mul(p.y, f.mul(q.z, z2z2)), s2 = f.mul(q.y, f.mul(p.z, z1z1));
    const U256 h = f.sub(u2, u1), r = f.sub(s2, s1);
    if (u256_zero(h)) {
        if (u256_zero(r)) return jac_double(p);
        return {f.one, f.one, U256{}};  // P = -Q
    }
    const U256 hh = f.mul(h, h), hhh = f.mul(h, hh), v = f.mul(u1, hh);
    const U256 x3 = f.sub(f.sub(f.mul(r, r), hhh), f.add(v, v));
    const U256 y3 = f.sub(f.mul(r, f.sub(v, x3)), f.mul(s1, hhh));
    const U256 z3 = f.mul(f.mul(p.z, q.z), h);
    return {x3, y3, z3};
}

// u1 * G + u2 * Q, both scalars plain, by Shamir's trick.
inline Jac jac_mul2(const U256 &u1, const Jac &g, const U256 &u2, const Jac &q) {
    const Mod &f = p256().fp;
    const Jac gq = jac_add(g, q);
    Jac r{f.one, f.one, U256{}};
    for (int i = 255; i >= 0; --i) {
        r = jac_double(r);
        const bool a = (u1.w[i / 32] >> (i % 32)) & 1, b = (u2.w[i / 32] >> (i % 32)) & 1;
        if (a && b) r = jac_add(r, gq); else if (a) r = jac_add(r, g); else if (b) r = jac_add(r, q);
    }
    return r;
}

// Decodes a SEC 1 public key: 33 bytes (02/03, compressed) or 65 bytes (04, uncompressed).
inline bool p256_public_key(const uint8_t *key, size_t len, Jac &out) {
    const P256 &c = p256(); const Mod &f = c.fp;
    if (!key || (len != 33 && len != 65)) return false;
    if ((len == 33 && key[0] != 2 && key[0] != 3) || (len == 65 && key[0] != 4)) return false;
    const U256 x = u256_from_be(key + 1);
    if (u256_cmp(x, f.m) >= 0) return false;
    const U256 xm = f.to(x);
    // rhs = x^3 - 3x + b
    U256 rhs = f.mul(f.mul(xm, xm), xm);
    rhs = f.sub(rhs, f.add(f.add(xm, xm), xm));
    rhs = f.add(rhs, c.b);
    U256 ym;
    if (len == 65) {
        const U256 y = u256_from_be(key + 33);
        if (u256_cmp(y, f.m) >= 0) return false;
        ym = f.to(y);
        if (u256_cmp(f.mul(ym, ym), rhs) != 0) return false;  // not on the curve
    } else {
        // p = 3 mod 4: y = rhs^((p + 1) / 4)
        U256 e = f.m, one{}; one.w[0] = 1; u256_add(e, e, one);
        for (int i = 0; i < 8; ++i) e.w[i] = (e.w[i] >> 2) | (i < 7 ? e.w[i + 1] << 30 : 0);
        ym = f.pow(rhs, e);
        if (u256_cmp(f.mul(ym, ym), rhs) != 0) return false;  // x is not on the curve
        const U256 y = f.from(ym);
        if ((y.w[0] & 1) != (key[0] & 1u)) ym = f.sub(U256{}, ym);
    }
    out = {xm, ym, f.one};
    return true;
}

enum class Es256 { ok, bad_key, bad_signature, high_s, mismatch };

// ES256: ECDSA P-256 over SHA-256(message); signature is raw r || s, 32 bytes
// each, big-endian, with 1 <= r, s < n and s <= n / 2 (low-S).
inline Es256 es256_verify(const uint8_t sig[64], const uint8_t *message, size_t len, const uint8_t *key, size_t key_len) {
    const P256 &c = p256(); const Mod &f = c.fp, &n = c.fn;
    Jac q;
    if (!p256_public_key(key, key_len, q)) return Es256::bad_key;
    const U256 r = u256_from_be(sig), s = u256_from_be(sig + 32);
    if (u256_zero(r) || u256_zero(s) || u256_cmp(r, n.m) >= 0 || u256_cmp(s, n.m) >= 0) return Es256::bad_signature;
    if (u256_cmp(s, c.half_n) > 0) return Es256::high_s;
    uint8_t digest[32]; sha256(message, len, digest);
    const U256 e = n.reduce(u256_from_be(digest));
    const U256 w = n.inv(n.to(s));                       // s^-1, Montgomery form
    const U256 u1 = n.from(n.mul(n.to(e), w)), u2 = n.from(n.mul(n.to(r), w));
    const Jac x = jac_mul2(u1, Jac{c.gx, c.gy, f.one}, u2, q);
    if (u256_zero(x.z)) return Es256::mismatch;
    // affine x = X / Z^2, compared mod n
    const U256 zi = f.inv(x.z);
    const U256 ax = f.from(f.mul(x.x, f.mul(zi, zi)));
    return u256_cmp(n.reduce(ax), r) == 0 ? Es256::ok : Es256::mismatch;
}

// DER ECDSA-Sig-Value (SEQUENCE { r INTEGER, s INTEGER }), as most key
// services return, to raw r || s with s normalised to the low half.
inline bool es256_from_der(const uint8_t *der, size_t len, uint8_t out[64]) {
    if (!der || len < 8 || len > 72 || der[0] != 0x30 || der[1] != len - 2) return false;
    size_t at = 2;
    for (int part = 0; part < 2; ++part) {
        if (at + 2 > len || der[at] != 0x02) return false;
        size_t n = der[at + 1]; at += 2;
        if (n < 1 || n > 33 || at + n > len) return false;
        const uint8_t *v = der + at;
        if (n > 1 && v[0] == 0 && !(v[1] & 0x80)) return false;  // not minimal
        if (v[0] & 0x80) return false;                             // negative
        if (n == 33) { if (v[0] != 0) return false; ++v; --n; }
        std::memset(out + 32 * part, 0, 32);
        std::memcpy(out + 32 * part + (32 - n), v, n);
        at += der[at - 1];
    }
    if (at != len) return false;
    const P256 &c = p256();
    const U256 r = u256_from_be(out); U256 s = u256_from_be(out + 32);
    if (u256_zero(r) || u256_zero(s) || u256_cmp(r, c.fn.m) >= 0 || u256_cmp(s, c.fn.m) >= 0) return false;
    if (u256_cmp(s, c.half_n) > 0) { u256_sub(s, c.fn.m, s); u256_to_be(s, out + 32); }
    return true;
}

}  // namespace ckey_crypto
