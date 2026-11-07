// SHA-512 (FIPS 180-4) and Ed25519 (RFC 8032) for Safe Actions.
//
// The Ed25519 arithmetic follows TweetNaCl (Bernstein, van Gastel, Janssen,
// Lange, Schwabe, Smetsers; public domain): small, constant-time in the secret
// scalar for signing, and easy to audit. Two changes for verification: S must be
// canonical (S < L, RFC 8032 section 5.1.7), and shifts of possibly negative
// values are written as multiplication. Checked against the RFC 8032 vectors
// and against independent implementations in tests/safe_actions_tests.cpp.

namespace ckey_crypto {

// ---------------------------------------------------------------- SHA-512

static const uint64_t kSha512K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
    0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
    0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

static inline uint64_t ror64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

struct Sha512 {
    uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                     0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    uint8_t buf[128] = {};
    size_t used = 0;
    uint64_t total = 0;

    void block(const uint8_t *p) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 8; ++j) w[i] = (w[i] << 8) | p[8 * i + j];
        }
        for (int i = 16; i < 80; ++i) {
            const uint64_t s0 = ror64(w[i - 15], 1) ^ ror64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = ror64(w[i - 2], 19) ^ ror64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
        for (int i = 0; i < 80; ++i) {
            const uint64_t t1 = k + (ror64(e, 14) ^ ror64(e, 18) ^ ror64(e, 41)) + ((e & f) ^ (~e & g)) + kSha512K[i] + w[i];
            const uint64_t t2 = (ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
    }
    void update(const uint8_t *p, size_t n) {
        total += n;
        while (n) {
            const size_t take = std::min(n, sizeof(buf) - used);
            std::memcpy(buf + used, p, take);
            used += take; p += take; n -= take;
            if (used == sizeof(buf)) { block(buf); used = 0; }
        }
    }
    void finish(uint8_t out[64]) {
        const uint64_t bits = total * 8;
        const uint8_t one = 0x80;
        update(&one, 1);
        const uint8_t zero = 0;
        while (used != 112) update(&zero, 1);
        uint8_t len[16] = {};
        for (int i = 0; i < 8; ++i) len[15 - i] = static_cast<uint8_t>(bits >> (8 * i));
        update(len, 16);
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) out[8 * i + j] = static_cast<uint8_t>(h[i] >> (56 - 8 * j));
    }
};

static void sha512(uint8_t out[64], const uint8_t *a, size_t an, const uint8_t *b = nullptr, size_t bn = 0,
                   const uint8_t *c = nullptr, size_t cn = 0) {
    Sha512 s;
    s.update(a, an);
    if (b) s.update(b, bn);
    if (c) s.update(c, cn);
    s.finish(out);
}

// ---------------------------------------------------------------- field arithmetic mod 2^255 - 19

typedef int64_t gf[16];
static const gf gf0 = {0};
static const gf gf1 = {1};
static const gf kD = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
static const gf kD2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
static const gf kX = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const gf kY = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
static const gf kI = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};

static void set25519(gf r, const gf a) { for (int i = 0; i < 16; ++i) r[i] = a[i]; }

static void car25519(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += (int64_t{1} << 16);
        const int64_t c = o[i] >> 16;  // arithmetic shift (C++20)
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}

static void sel25519(gf p, gf q, int b) {
    const int64_t c = ~(int64_t(b) - 1);
    for (int i = 0; i < 16; ++i) {
        const int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const gf n) {
    gf m, t;
    set25519(t, n);
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = static_cast<int>((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = static_cast<uint8_t>(t[i] & 0xff);
        o[2 * i + 1] = static_cast<uint8_t>(t[i] >> 8);
    }
}

static int verify32(const uint8_t *x, const uint8_t *y) {
    unsigned d = 0;
    for (int i = 0; i < 32; ++i) d |= x[i] ^ y[i];
    return (1 & ((d - 1) >> 8)) - 1;  // 0 when equal, -1 otherwise
}

static int neq25519(const gf a, const gf b) {
    uint8_t c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return verify32(c, d);
}

static uint8_t par25519(const gf a) {
    uint8_t d[32];
    pack25519(d, a);
    return d[0] & 1;
}

static void unpack25519(gf o, const uint8_t *n) {
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (int64_t(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

static void fadd(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
static void fsub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }

static void fmul(gf o, const gf a, const gf b) {
    int64_t t[31] = {0};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    car25519(o);
    car25519(o);
}

static void fsq(gf o, const gf a) { fmul(o, a, a); }

static void inv25519(gf o, const gf in) {
    gf c;
    set25519(c, in);
    for (int a = 253; a >= 0; --a) {
        fsq(c, c);
        if (a != 2 && a != 4) fmul(c, c, in);
    }
    set25519(o, c);
}

static void pow2523(gf o, const gf in) {
    gf c;
    set25519(c, in);
    for (int a = 250; a >= 0; --a) {
        fsq(c, c);
        if (a != 1) fmul(c, c, in);
    }
    set25519(o, c);
}

// ---------------------------------------------------------------- the Edwards curve

static void point_add(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    fsub(a, p[1], p[0]);
    fsub(t, q[1], q[0]);
    fmul(a, a, t);
    fadd(b, p[0], p[1]);
    fadd(t, q[0], q[1]);
    fmul(b, b, t);
    fmul(c, p[3], q[3]);
    fmul(c, c, kD2);
    fmul(d, p[2], q[2]);
    fadd(d, d, d);
    fsub(e, b, a);
    fsub(f, d, c);
    fadd(g, d, c);
    fadd(h, b, a);
    fmul(p[0], e, f);
    fmul(p[1], h, g);
    fmul(p[2], g, f);
    fmul(p[3], e, h);
}

static void cswap(gf p[4], gf q[4], uint8_t b) {
    for (int i = 0; i < 4; ++i) sel25519(p[i], q[i], b);
}

static void pack_point(uint8_t *r, gf p[4]) {
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    fmul(tx, p[0], zi);
    fmul(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= static_cast<uint8_t>(par25519(tx) << 7);
}

static void scalarmult(gf p[4], gf q[4], const uint8_t *s) {
    set25519(p[0], gf0);
    set25519(p[1], gf1);
    set25519(p[2], gf1);
    set25519(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b);
        point_add(q, p);
        point_add(p, p);
        cswap(p, q, b);
    }
}

static void scalarbase(gf p[4], const uint8_t *s) {
    gf q[4];
    set25519(q[0], kX);
    set25519(q[1], kY);
    set25519(q[2], gf1);
    fmul(q[3], kX, kY);
    scalarmult(p, q, s);
}

static const int64_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};

static void modL(uint8_t *r, int64_t x[64]) {
    for (int i = 63; i >= 32; --i) {
        int64_t carry = 0;
        int j;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * kL[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    int64_t carry = 0;
    for (int j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * kL[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; ++j) x[j] -= carry * kL[j];
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = static_cast<uint8_t>(x[i] & 255);
    }
}

static void reduce64(uint8_t *r) {
    int64_t x[64];
    for (int i = 0; i < 64; ++i) x[i] = r[i];
    for (int i = 0; i < 64; ++i) r[i] = 0;
    modL(r, x);
}

static int unpackneg(gf r[4], const uint8_t p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    fsq(num, r[1]);
    fmul(den, num, kD);
    fsub(num, num, r[2]);
    fadd(den, r[2], den);
    fsq(den2, den);
    fsq(den4, den2);
    fmul(den6, den4, den2);
    fmul(t, den6, num);
    fmul(t, t, den);
    pow2523(t, t);
    fmul(t, t, num);
    fmul(t, t, den);
    fmul(t, t, den);
    fmul(r[0], t, den);
    fsq(chk, r[0]);
    fmul(chk, chk, den);
    if (neq25519(chk, num)) fmul(r[0], r[0], kI);
    fsq(chk, r[0]);
    fmul(chk, chk, den);
    if (neq25519(chk, num)) return -1;
    if (par25519(r[0]) == (p[31] >> 7)) fsub(r[0], gf0, r[0]);
    fmul(r[3], r[0], r[1]);
    return 0;
}

/** The public key for a 32-byte seed. */
static void ed25519_public_key(uint8_t pk[32], const uint8_t seed[32]) {
    uint8_t d[64];
    gf p[4];
    sha512(d, seed, 32);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    scalarbase(p, d);
    pack_point(pk, p);
}

/** A detached RFC 8032 signature of msg with a 32-byte seed. */
static void ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t n, const uint8_t seed[32]) {
    uint8_t d[64], r[64], h[64], pk[32];
    gf p[4];
    sha512(d, seed, 32);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    ed25519_public_key(pk, seed);
    sha512(r, d + 32, 32, msg, n);
    reduce64(r);
    scalarbase(p, r);
    pack_point(sig, p);
    sha512(h, sig, 32, pk, 32, msg, n);
    reduce64(h);
    int64_t x[64] = {0};
    for (int i = 0; i < 32; ++i) x[i] = r[i];
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j) x[i + j] += int64_t(h[i]) * int64_t(d[j]);
    modL(sig + 32, x);
}

/** True when S < L, the group order (little-endian comparison). */
static bool scalar_canonical(const uint8_t s[32]) {
    for (int i = 31; i >= 0; --i) {
        if (s[i] < kL[i]) return true;
        if (s[i] > kL[i]) return false;
    }
    return false;  // equal to L
}

/** Verifies a detached signature. */
static bool ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t n, const uint8_t pk[32]) {
    if (!scalar_canonical(sig + 32)) return false;
    gf p[4], q[4];
    if (unpackneg(q, pk)) return false;
    uint8_t h[64], t[32];
    sha512(h, sig, 32, pk, 32, msg, n);
    reduce64(h);
    scalarmult(p, q, h);
    scalarbase(q, sig + 32);
    point_add(p, q);
    pack_point(t, p);
    return verify32(sig, t) == 0;
}

}  // namespace ckey_crypto
