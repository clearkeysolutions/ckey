// Shared internal definitions (RS, CRC, layouts, sampling, style checks).
// Included first by every core translation unit.
#include "ckey/ckey.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr size_t kDataBytes = 26;
constexpr size_t kParityBytes = 51;
constexpr size_t kCodewordBytes = kDataBytes + kParityBytes;
constexpr size_t kBits = kCodewordBytes * 8;
constexpr size_t kTextDataBytes = 210;
constexpr size_t kTextParityBytes = 45;
constexpr size_t kTextCodewordBytes = kTextDataBytes + kTextParityBytes;
static_assert(kTextCodewordBytes == CKEY_TEXT_CODEWORD_BYTES);
constexpr size_t kExtendedBlocks = CKEY_EXTENDED_BLOCK_COUNT;
constexpr size_t kExtendedDataBytes = CKEY_EXTENDED_DATA_BYTES_PER_BLOCK;
constexpr size_t kExtendedParityBytes = CKEY_EXTENDED_PARITY_BYTES_PER_BLOCK;
constexpr size_t kExtendedBlockBytes = CKEY_EXTENDED_CODEWORD_BYTES_PER_BLOCK;
constexpr size_t kExtendedCodewordBytes = CKEY_EXTENDED_CODEWORD_BYTES;
constexpr size_t kExtendedBits = kExtendedCodewordBytes * 8;
constexpr int kExtendedSide = 116;
constexpr std::array<size_t, CKEY_EXTENDED_TIER_COUNT> kExtendedTierCapacities = {256, 384, 512, 768, 1024, 1536, 2048};
static_assert(kExtendedBlocks * kExtendedBlockBytes == kExtendedCodewordBytes);
constexpr int kSide = 29;
constexpr double kCenter = 14.5;
constexpr double kTau = 6.283185307179586476925286766559;

struct Gf {
    std::array<uint8_t, 512> exp{};
    std::array<uint8_t, 256> log{};
    Gf() {
        unsigned x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = static_cast<uint8_t>(x); log[x] = static_cast<uint8_t>(i);
            x <<= 1; if (x & 0x100) x ^= 0x11d;
        }
        for (int i = 255; i < 512; ++i) exp[i] = exp[i - 255];
    }
    uint8_t mul(uint8_t a, uint8_t b) const { return (!a || !b) ? 0 : exp[log[a] + log[b]]; }
    uint8_t inv(uint8_t a) const { return a ? exp[255 - log[a]] : 0; }
};
const Gf gf{};

std::array<uint8_t, kParityBytes + 1> rs_generator() {
    std::array<uint8_t, kParityBytes + 1> p{}; p[0] = 1;
    size_t len = 1;
    for (size_t i = 0; i < kParityBytes; ++i) {
        std::array<uint8_t, kParityBytes + 1> q{};
        const uint8_t root = gf.exp[i];
        for (size_t j = 0; j < len; ++j) { q[j] ^= p[j]; q[j + 1] ^= gf.mul(p[j], root); }
        p = q; ++len;
    }
    return p;
}

uint8_t eval_low(const std::array<uint8_t, kParityBytes + 1> &p, size_t degree, uint8_t x) {
    uint8_t y = p[degree];
    while (degree) { --degree; y = gf.mul(y, x) ^ p[degree]; }
    return y;
}

std::array<uint8_t, kParityBytes> syndromes(const std::array<uint8_t, kCodewordBytes> &cw) {
    std::array<uint8_t, kParityBytes> s{};
    for (size_t j = 0; j < kParityBytes; ++j) {
        const uint8_t x = gf.exp[j];
        uint8_t y = 0;
        for (uint8_t b : cw) y = gf.mul(y, x) ^ b;
        s[j] = y;
    }
    return s;
}

bool rs_correct(std::array<uint8_t, kCodewordBytes> &cw, uint32_t &corrected) {
    const auto s = syndromes(cw);
    if (std::all_of(s.begin(), s.end(), [](uint8_t v) { return v == 0; })) { corrected = 0; return true; }

    // Berlekamp–Massey over the 51 syndromes.  Locator coefficients are
    // stored low-degree first: Lambda(z)=prod(1-X_i*z).
    std::array<uint8_t, kParityBytes + 1> lambda{}, previous{};
    lambda[0] = previous[0] = 1;
    size_t degree = 0, shift = 1;
    uint8_t last_discrepancy = 1;
    for (size_t n = 0; n < kParityBytes; ++n) {
        uint8_t discrepancy = s[n];
        for (size_t i = 1; i <= degree; ++i) discrepancy ^= gf.mul(lambda[i], s[n - i]);
        if (discrepancy == 0) { ++shift; continue; }
        const auto saved = lambda;
        const uint8_t factor = gf.mul(discrepancy, gf.inv(last_discrepancy));
        for (size_t i = 0; i + shift <= kParityBytes; ++i) lambda[i + shift] ^= gf.mul(factor, previous[i]);
        if (2 * degree <= n) {
            degree = n + 1 - degree;
            previous = saved;
            last_discrepancy = discrepancy;
            shift = 1;
        } else {
            ++shift;
        }
    }
    if (degree == 0 || degree > kParityBytes / 2) return false;

    std::array<size_t, kParityBytes / 2> positions{};
    std::array<uint8_t, kParityBytes / 2> locations{};
    size_t found = 0;
    for (size_t pos = 0; pos < cw.size(); ++pos) {
        const size_t exponent = cw.size() - 1 - pos;
        const uint8_t x = gf.exp[exponent];
        const uint8_t inverse_x = gf.inv(x);
        if (eval_low(lambda, degree, inverse_x) == 0) {
            if (found >= positions.size()) return false;
            positions[found] = pos; locations[found] = x; ++found;
        }
    }
    if (found != degree) return false;

    // Solve the first degree syndrome equations for error magnitudes using a
    // small GF(256) Vandermonde system. This avoids a second fragile formula
    // convention for Forney magnitudes and is small for a 77-byte codeword.
    uint8_t matrix[kParityBytes / 2][kParityBytes / 2 + 1]{};
    for (size_t row = 0; row < degree; ++row) {
        for (size_t col = 0; col < degree; ++col) {
            uint8_t v = 1;
            for (size_t p = 0; p < row; ++p) v = gf.mul(v, locations[col]);
            matrix[row][col] = v;
        }
        matrix[row][degree] = s[row];
    }
    for (size_t col = 0; col < degree; ++col) {
        size_t pivot = col;
        while (pivot < degree && matrix[pivot][col] == 0) ++pivot;
        if (pivot == degree) return false;
        if (pivot != col) for (size_t j = col; j <= degree; ++j) std::swap(matrix[pivot][j], matrix[col][j]);
        const uint8_t inv = gf.inv(matrix[col][col]);
        for (size_t j = col; j <= degree; ++j) matrix[col][j] = gf.mul(matrix[col][j], inv);
        for (size_t row = 0; row < degree; ++row) if (row != col) {
            const uint8_t factor = matrix[row][col];
            for (size_t j = col; j <= degree; ++j) matrix[row][j] ^= gf.mul(factor, matrix[col][j]);
        }
    }
    for (size_t i = 0; i < degree; ++i) cw[positions[i]] ^= matrix[i][degree];
    const auto check = syndromes(cw);
    if (!std::all_of(check.begin(), check.end(), [](uint8_t v) { return v == 0; })) return false;
    corrected = static_cast<uint32_t>(degree);
    return true;
}

std::vector<uint8_t> rs_generator_generic(size_t parity) {
    std::vector<uint8_t> polynomial(1, 1);
    for (size_t root = 0; root < parity; ++root) {
        std::vector<uint8_t> next(polynomial.size() + 1, 0);
        for (size_t i = 0; i < polynomial.size(); ++i) {
            next[i] ^= polynomial[i];
            next[i + 1] ^= gf.mul(polynomial[i], gf.exp[root]);
        }
        polynomial = std::move(next);
    }
    return polynomial;
}

void rs_encode_generic(std::vector<uint8_t> &codeword, size_t data_bytes, size_t parity_bytes) {
    const auto generator = rs_generator_generic(parity_bytes);
    std::vector<uint8_t> work = codeword;
    for (size_t i = 0; i < data_bytes; ++i) {
        const uint8_t feedback = work[i];
        if (!feedback) continue;
        for (size_t j = 1; j <= parity_bytes; ++j) work[i + j] ^= gf.mul(generator[j], feedback);
    }
    std::copy(work.begin() + static_cast<std::ptrdiff_t>(data_bytes), work.end(),
              codeword.begin() + static_cast<std::ptrdiff_t>(data_bytes));
}

bool rs_correct_generic(std::vector<uint8_t> &cw, size_t parity_bytes, uint32_t &corrected) {
    if (cw.empty() || parity_bytes == 0 || parity_bytes >= cw.size() || parity_bytes > 254) return false;
    std::vector<uint8_t> syndromes(parity_bytes, 0);
    for (size_t j = 0; j < parity_bytes; ++j) for (uint8_t b : cw) syndromes[j] = gf.mul(syndromes[j], gf.exp[j]) ^ b;
    if (std::all_of(syndromes.begin(), syndromes.end(), [](uint8_t v) { return v == 0; })) { corrected = 0; return true; }

    std::vector<uint8_t> lambda(parity_bytes + 1, 0), previous(parity_bytes + 1, 0);
    lambda[0] = previous[0] = 1;
    size_t degree = 0, shift = 1;
    uint8_t last_discrepancy = 1;
    for (size_t n = 0; n < parity_bytes; ++n) {
        uint8_t discrepancy = syndromes[n];
        for (size_t i = 1; i <= degree; ++i) discrepancy ^= gf.mul(lambda[i], syndromes[n - i]);
        if (!discrepancy) { ++shift; continue; }
        const auto saved = lambda;
        const uint8_t factor = gf.mul(discrepancy, gf.inv(last_discrepancy));
        for (size_t i = 0; i + shift <= parity_bytes; ++i) lambda[i + shift] ^= gf.mul(factor, previous[i]);
        if (2 * degree <= n) { degree = n + 1 - degree; previous = saved; last_discrepancy = discrepancy; shift = 1; }
        else ++shift;
    }
    if (degree == 0 || degree > parity_bytes / 2) return false;
    std::vector<size_t> positions; std::vector<uint8_t> locations;
    for (size_t pos = 0; pos < cw.size(); ++pos) {
        const uint8_t location = gf.exp[cw.size() - 1 - pos];
        uint8_t value = lambda[degree];
        for (size_t i = degree; i-- > 0;) value = gf.mul(value, gf.inv(location)) ^ lambda[i];
        if (!value) { positions.push_back(pos); locations.push_back(location); }
    }
    if (positions.size() != degree) return false;
    std::vector<uint8_t> matrix(degree * (degree + 1), 0);
    const auto at = [&](size_t row, size_t col) -> uint8_t & { return matrix[row * (degree + 1) + col]; };
    for (size_t row = 0; row < degree; ++row) {
        for (size_t col = 0; col < degree; ++col) {
            uint8_t value = 1;
            for (size_t power = 0; power < row; ++power) value = gf.mul(value, locations[col]);
            at(row, col) = value;
        }
        at(row, degree) = syndromes[row];
    }
    for (size_t col = 0; col < degree; ++col) {
        size_t pivot = col; while (pivot < degree && at(pivot, col) == 0) ++pivot;
        if (pivot == degree) return false;
        if (pivot != col) for (size_t j = col; j <= degree; ++j) std::swap(at(pivot, j), at(col, j));
        const uint8_t inverse = gf.inv(at(col, col));
        for (size_t j = col; j <= degree; ++j) at(col, j) = gf.mul(at(col, j), inverse);
        for (size_t row = 0; row < degree; ++row) if (row != col) {
            const uint8_t factor = at(row, col);
            for (size_t j = col; j <= degree; ++j) at(row, j) ^= gf.mul(factor, at(col, j));
        }
    }
    for (size_t i = 0; i < degree; ++i) cw[positions[i]] ^= at(i, degree);
    for (size_t j = 0; j < parity_bytes; ++j) {
        uint8_t syndrome = 0; for (uint8_t b : cw) syndrome = gf.mul(syndrome, gf.exp[j]) ^ b;
        if (syndrome) return false;
    }
    corrected = static_cast<uint32_t>(degree);
    return true;
}

// Errors-and-erasures decoding. `erasures` lists codeword positions the reader
// already knows are unreliable. The code corrects any pattern with
// 2 * errors + erasures <= parity, so a known-bad byte costs one parity byte
// where an unknown one costs two. With `margin` > 0 a decode is accepted only
// if 2 * errors + erasures <= parity - margin: the spare syndromes then act as
// a check, which keeps a nearly-all-erased block from "correcting" to noise.
// On success `corrected` is the number of unknown errors fixed. Forney's
// formula gives the magnitudes; the syndromes are recomputed to confirm.
bool rs_correct_erasures(std::vector<uint8_t> &cw, size_t parity_bytes, const std::vector<size_t> &erasures,
                         size_t margin, uint32_t &corrected) {
    const size_t n = cw.size(), P = parity_bytes, E = erasures.size();
    if (n == 0 || n > 255 || P == 0 || P >= n || P > 254 || E + margin > P) return false;
    std::vector<uint8_t> S(P, 0);
    for (size_t j = 0; j < P; ++j) for (uint8_t b : cw) S[j] = gf.mul(S[j], gf.exp[j]) ^ b;
    if (std::all_of(S.begin(), S.end(), [](uint8_t v) { return v == 0; })) { corrected = 0; return true; }
    // Erasure locator G(x) = prod (1 - X_k x), X_k = alpha^(n - 1 - pos).
    std::vector<uint8_t> G(1, 1);
    for (size_t pos : erasures) {
        if (pos >= n) return false;
        const uint8_t X = gf.exp[n - 1 - pos];
        G.push_back(0);
        for (size_t i = G.size() - 1; i > 0; --i) G[i] ^= gf.mul(G[i - 1], X);
    }
    // Forney syndromes T = S * G mod x^P; T[E..P-1] depend on the errors only.
    std::vector<uint8_t> T(P, 0);
    for (size_t i = 0; i < P; ++i) for (size_t j = 0; j <= std::min(i, E); ++j) T[i] ^= gf.mul(S[i - j], G[j]);
    // Berlekamp-Massey on T[E..P-1].
    const size_t M = P - E;
    std::vector<uint8_t> lambda(M + 1, 0), previous(M + 1, 0);
    lambda[0] = previous[0] = 1;
    size_t degree = 0, shift = 1; uint8_t last = 1;
    for (size_t k = 0; k < M; ++k) {
        uint8_t d = T[E + k];
        for (size_t i = 1; i <= degree; ++i) d ^= gf.mul(lambda[i], T[E + k - i]);
        if (!d) { ++shift; continue; }
        const auto saved = lambda;
        const uint8_t factor = gf.mul(d, gf.inv(last));
        for (size_t i = 0; i + shift <= M; ++i) lambda[i + shift] ^= gf.mul(factor, previous[i]);
        if (2 * degree <= k) { degree = k + 1 - degree; previous = saved; last = d; shift = 1; }
        else ++shift;
    }
    if (2 * degree + E + margin > P) return false;
    // Chien search for the error positions.
    std::vector<size_t> positions(erasures.begin(), erasures.end());
    size_t found = 0;
    for (size_t pos = 0; pos < n && degree; ++pos) {
        const uint8_t inverse = gf.inv(gf.exp[n - 1 - pos]);
        uint8_t value = lambda[degree];
        for (size_t i = degree; i-- > 0;) value = gf.mul(value, inverse) ^ lambda[i];
        if (!value) {
            if (std::find(erasures.begin(), erasures.end(), pos) != erasures.end()) return false;
            positions.push_back(pos); ++found;
        }
    }
    if (found != degree) return false;
    // Combined locator Psi = lambda * G, evaluator Omega = S * Psi mod x^P.
    std::vector<uint8_t> psi(degree + E + 1, 0);
    for (size_t i = 0; i <= degree; ++i) for (size_t j = 0; j <= E; ++j) psi[i + j] ^= gf.mul(lambda[i], G[j]);
    std::vector<uint8_t> omega(P, 0);
    for (size_t i = 0; i < P; ++i) for (size_t j = 0; j <= std::min(i, psi.size() - 1); ++j) omega[i] ^= gf.mul(S[i - j], psi[j]);
    for (size_t pos : positions) {
        const uint8_t X = gf.exp[n - 1 - pos], inverse = gf.inv(X);
        uint8_t numerator = 0, denominator = 0, power = 1;
        for (size_t i = 0; i < P; ++i) { numerator ^= gf.mul(omega[i], power); power = gf.mul(power, inverse); }
        // Formal derivative: odd-degree terms only (characteristic 2).
        power = 1; const uint8_t inverse2 = gf.mul(inverse, inverse);
        for (size_t i = 1; i < psi.size(); i += 2) { denominator ^= gf.mul(psi[i], power); power = gf.mul(power, inverse2); }
        if (!denominator) return false;
        cw[pos] ^= gf.mul(X, gf.mul(numerator, gf.inv(denominator)));
    }
    for (size_t j = 0; j < P; ++j) {
        uint8_t syndrome = 0; for (uint8_t b : cw) syndrome = gf.mul(syndrome, gf.exp[j]) ^ b;
        if (syndrome) return false;
    }
    corrected = static_cast<uint32_t>(degree);
    return true;
}

uint32_t crc32c(const uint8_t *data, size_t len) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0x82f63b78u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// Normative SplitMix32 fill for unused frame bytes. This is visual decorrelation,
// not encryption or error correction; the complete padded frame remains under RS.
uint8_t padding_byte(uint32_t &state) {
    state += 0x9e3779b9u;
    uint32_t value = state;
    value = (value ^ (value >> 16)) * 0x85ebca6bu;
    value = (value ^ (value >> 13)) * 0xc2b2ae35u;
    value ^= value >> 16;
    return static_cast<uint8_t>(value);
}

void fill_padding(uint8_t *frame, size_t begin, size_t end, size_t seed_length) {
    uint32_t state = crc32c(frame, seed_length);
    for (size_t i = begin; i < end; ++i) frame[i] = padding_byte(state);
}

bool padding_matches(const uint8_t *frame, size_t begin, size_t end, size_t seed_length) {
    uint32_t state = crc32c(frame, seed_length);
    for (size_t i = begin; i < end; ++i) if (frame[i] != padding_byte(state)) return false;
    return true;
}

size_t extended_tier_index(ckey_extended_tier tier, size_t payload_length) {
    if (tier == CKEY_EXTENDED_TIER_AUTO) {
        for (size_t i = 0; i < kExtendedTierCapacities.size(); ++i)
            if (payload_length <= kExtendedTierCapacities[i]) return i;
        return kExtendedTierCapacities.size();
    }
    for (size_t i = 0; i < kExtendedTierCapacities.size(); ++i)
        if (static_cast<size_t>(tier) == kExtendedTierCapacities[i]) return i;
    return kExtendedTierCapacities.size();
}

struct Node { bool polar; bool dial; double x, y; int row, col; int ring, slot; };

bool in_finder(int r, int c) {
    const bool tl = r < 6 && c < 6, tr = r < 6 && c >= kSide - 6;
    const bool bl = r >= kSide - 6 && c < 6, br = r >= kSide - 6 && c >= kSide - 6;
    return tl || tr || bl || br;
}
bool grid_key(int r, int c) { return (c == 2 || c == 3) && (r == 11 || r == 12); }
bool ray_key(int slot) { return slot == 0 || slot == 54 || slot == 55; }
bool ring_key(int ring, int slot) { return ring == 3 && slot >= 18 && slot <= 25; }

std::vector<Node> build_data_nodes() {
    std::vector<Node> cart, polar, out;
    std::array<std::vector<Node>, 72> cart_by_angle;
    // Fixed Cartesian field is sampled outside the polar aperture. The four
    // corner finders and the left-side Grid Key never carry payload.
    for (int r = 1; r < kSide - 1; ++r) for (int c = 1; c < kSide - 1; ++c) {
        const double x = c + .5, y = r + .5;
        const double dx = x - kCenter, dy = y - kCenter;
        if (in_finder(r, c) || grid_key(r, c) || dx * dx + dy * dy <= 10.4 * 10.4) continue;
        double angle = std::atan2(dx, -dy);
        if (angle < 0) angle += kTau;
        const int slot = static_cast<int>(std::lround(angle * 72.0 / kTau)) % 72;
        cart_by_angle[slot].push_back({false, false, x, y, r, c, -1, -1});
    }
    for (auto &bin : cart_by_angle) std::sort(bin.begin(), bin.end(), [=](const Node &a, const Node &b) {
        const double ax = a.x-kCenter, ay = a.y-kCenter, bx = b.x-kCenter, by = b.y-kCenter;
        const double ar=ax*ax+ay*ay, br=bx*bx+by*by;
        if (ar != br) return ar > br;
        if (a.row != b.row) return a.row < b.row;
        return a.col < b.col;
    });
    for (size_t depth = 0; ; ++depth) {
        bool any = false;
        for (int step = 0; step < 72; ++step) {
            auto &bin = cart_by_angle[(step * 37) % 72];
            if (depth < bin.size()) { cart.push_back(bin[depth]); any = true; }
        }
        if (!any) break;
    }
    // Four concentric payload rings occupy the inner field. A fifth layer of
    // radial data cells is a narrow annular dial band, never spokes to center.
    std::array<std::vector<Node>, 5> polar_layers;
    for (int layer = 0; layer < 5; ++layer) for (int step = 0; step < 72; ++step) {
        const int slot = (step * 37 + layer * 13) % 72;
        if (ray_key(slot)) continue;
        if (layer == 0) polar_layers[layer].push_back({true, true, 0, 0, -1, -1, -2, slot});
        else {
            const int ring = layer - 1;
            if (ring_key(ring, slot)) continue;
            const double radius = 3.6 + 0.8 * ring;
            const double a = kTau * slot / 72.0;
            polar_layers[layer].push_back({true, false, kCenter + radius * std::sin(a), kCenter - radius * std::cos(a), -1, -1, ring, slot});
        }
    }
    // Put all dial sites first so every one of the 69 length-coded data rays
    // is inside the locator codeword, then round-robin the four dot rings.
    polar = polar_layers[0];
    for (size_t depth = 0; ; ++depth) {
        bool any = false;
        for (size_t layer = 1; layer < polar_layers.size(); ++layer)
            if (depth < polar_layers[layer].size()) { polar.push_back(polar_layers[layer][depth]); any = true; }
        if (!any) break;
    }
    // Interleave coordinate systems in the placement sequence.  Local image
    // damage is therefore less likely to erase a consecutive run of bytes.
    size_t ci = 0, pi = 0;
    while (ci < cart.size() || pi < polar.size()) {
        if (ci < cart.size()) out.push_back(cart[ci++]);
        if (pi < polar.size()) out.push_back(polar[pi++]);
    }
    return out;
}

const std::vector<Node> &data_nodes() { static const std::vector<Node> nodes = build_data_nodes(); return nodes; }

bool code_bit(const ckey_symbol *s, size_t i) { return ((s->codeword[i / 8] >> (7 - (i % 8))) & 1u) != 0; }

// ---------------------------------------------------------------------------
// Dense-profile layout (Profile 2 Format 0.25, Profile 3 Format 0.26).
//
// Safe area: a candidate Cartesian module is placed only if its ink (bounded
// by the module's circumscribed disc) keeps a declared clearance from every
// structural mark: the frame stroke, each finder arc including its round
// caps and its concave side, the Grid Key, and the radial alphabet. All
// tests below use only +, *, sqrt, and comparisons on exactly representable
// constants, so every conforming implementation derives the same site set.
//
// No dead space: every site (Cartesian, ring, dial/ray component) carries a
// value. Sites are ordered by a keyed bijective hash of their integer
// identity, so the codeword is spread uniformly over the whole symbol. Sites
// past the codeword carry fill. All site values are XORed with a fixed
// SplitMix32 whitening stream; this is optical modulation (it prevents
// payload-dependent empty or solid regions) and is not error correction.
// ---------------------------------------------------------------------------
struct DenseGeometry {
    double frame_center, frame_half, frame_radius, frame_half_stroke, frame_clear;
    double finder_inset, finder_radius, finder_half_stroke, finder_clear, side;
    double grid_x0, grid_y0, grid_x1, grid_y1, grid_clear;
    double polar_center, ray_ink_outer, polar_clear;
    double key_ray_r0, key_ray_length, key_ray_half_width;
    double key_ray_dir[3][2];   // unit (sin, -cos) for each Ray Key slot
    double key_ray_fraction[3];
    double module_half;         // half side of a square Cartesian module
    // Pixel family only: grid modules are also allowed in the annulus between
    // the ring zone's outer edge and the dial (both radii from polar_center);
    // 0, 0 disables it.
    double annulus_inner = 0, annulus_outer = 0;
};

double rounded_square_sd(double x, double y, double center, double half, double radius) {
    const double qx = std::abs(x - center) - (half - radius), qy = std::abs(y - center) - (half - radius);
    const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - radius;
}

// Distance to a corner finder centerline: the quarter circle around
// (cx, cy) lying in the symbol's outer corner quadrant.
double finder_centerline_distance(double x, double y, double cx, double cy, double r, bool right, bool bottom) {
    double dx = x - cx, dy = y - cy;
    if (right) dx = -dx;
    if (bottom) dy = -dy;
    if (dx <= 0 && dy <= 0) return std::abs(std::sqrt(dx * dx + dy * dy) - r);
    const double ex = dx + r, ey = dy + r;
    return std::min(std::sqrt(ex * ex + dy * dy), std::sqrt(dx * dx + ey * ey));
}

double segment_distance(double x, double y, double x0, double y0, double x1, double y1) {
    const double vx = x1 - x0, vy = y1 - y0, wx = x - x0, wy = y - y0;
    const double t = std::clamp((wx * vx + wy * vy) / (vx * vx + vy * vy), 0.0, 1.0);
    const double px = x0 + t * vx - x, py = y0 + t * vy - y;
    return std::sqrt(px * px + py * py);
}

// Signed safe-area slack for a Cartesian module centered at (x, y): positive
// means every structural clearance is met. Exposed for layout tests.
double dense_site_margin(const DenseGeometry &g, double x, double y) {
    const double e = g.module_half * 1.4142135623730951;  // circumscribed disc
    double m = -rounded_square_sd(x, y, g.frame_center, g.frame_half, g.frame_radius) - g.frame_half_stroke - e - g.frame_clear;
    const double near = g.finder_inset, far = g.side - g.finder_inset;
    const double finder_need = g.finder_half_stroke + e + g.finder_clear;
    m = std::min(m, finder_centerline_distance(x, y, near, near, g.finder_radius, false, false) - finder_need);
    m = std::min(m, finder_centerline_distance(x, y, far, near, g.finder_radius, true, false) - finder_need);
    m = std::min(m, finder_centerline_distance(x, y, near, far, g.finder_radius, false, true) - finder_need);
    m = std::min(m, finder_centerline_distance(x, y, far, far, g.finder_radius, true, true) - finder_need);
    const double gx = std::max({g.grid_x0 - x, 0.0, x - g.grid_x1}), gy = std::max({g.grid_y0 - y, 0.0, y - g.grid_y1});
    m = std::min(m, std::sqrt(gx * gx + gy * gy) - e - g.grid_clear);
    const double dx = x - g.polar_center, dy = y - g.polar_center, d = std::sqrt(dx * dx + dy * dy);
    double polar = d - g.ray_ink_outer;
    if (g.annulus_outer > g.annulus_inner) polar = std::max(polar, std::min(d - g.annulus_inner, g.annulus_outer - d));
    m = std::min(m, polar - e - g.polar_clear);
    for (int k = 0; k < 3; ++k) {
        const double r1 = g.key_ray_r0 + g.key_ray_length * g.key_ray_fraction[k];
        const double ux = g.key_ray_dir[k][0], uy = g.key_ray_dir[k][1];
        m = std::min(m, segment_distance(x, y, g.polar_center + g.key_ray_r0 * ux, g.polar_center + g.key_ray_r0 * uy,
                                         g.polar_center + r1 * ux, g.polar_center + r1 * uy) - g.key_ray_half_width - e - g.polar_clear);
    }
    return m;
}

// Ray Key directions are 0 deg, 270 deg, and 270 + 1/72 turn (P2: slots 0, 108,
// 110 of 144; P3: slots 0, 432, 440 of 576). The third direction is the only
// non-axis value; no candidate site is within 1e-6 of any clearance boundary
// (checked by tests/layout_tests.cpp), so libm rounding cannot change the set.
DenseGeometry text_geometry() {
    const double a = kTau * 110.0 / 144.0;
    return {14.5, 14.0, 3.5, 0.325, 0.5,
            4.0, 2.3, 0.4, 0.5, 29.0,
            2.13, 11.13, 3.87, 12.87, 0.5,
            kCenter, 7.6 + 1.20 + 0.05, 0.5,
            7.6, 1.9, 0.11,
            {{0.0, -1.0}, {-1.0, 0.0}, {std::sin(a), -std::cos(a)}},
            {1.0, 0.5, 0.75},
            0.17};
}

DenseGeometry extended_geometry() {
    const double a = kTau * 440.0 / 576.0;
    return {58.0, 56.0, 14.0, 1.3, 1.5,
            16.0, 9.2, 1.6, 1.5, 116.0,
            8.08, 45.08, 8.92, 45.92, 0.5,
            58.0, 26.30 + 0.80 + 0.85 * 3 + 0.14, 0.5,
            26.30, 3.6, 0.14,
            {{0.0, -1.0}, {-1.0, 0.0}, {std::sin(a), -std::cos(a)}},
            {1.0, 0.5, 0.75},
            0.17};
}

// lowbias32: a bijection on 32-bit words, so distinct site identities never tie.
uint32_t site_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
constexpr uint32_t kTextPlacementKey = 0x434b5032u;      // "CKP2"
constexpr uint32_t kExtendedPlacementKey = 0x434b5033u;  // "CKP3"
constexpr uint32_t kTextWhiteningSeed = 0x434b5732u;     // "CKW2"
constexpr uint32_t kExtendedWhiteningSeed = 0x434b5733u; // "CKW3"

uint32_t site_identity(uint32_t kind, uint32_t a, uint32_t b) { return (kind << 24) | (a << 12) | b; }

template <class NodeT>
void order_sites(std::vector<NodeT> &nodes, uint32_t key) {
    std::sort(nodes.begin(), nodes.end(), [key](const NodeT &l, const NodeT &r) {
        return site_hash(l.id ^ key) < site_hash(r.id ^ key);
    });
}

std::vector<uint8_t> whitening_bits(uint32_t seed, size_t count) {
    std::vector<uint8_t> bits(count);
    uint32_t state = seed; uint8_t byte = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i % 8 == 0) byte = padding_byte(state);
        bits[i] = static_cast<uint8_t>((byte >> (7 - i % 8)) & 1u);
    }
    return bits;
}

// Normative site counts (Format 0.25 / 0.26); tests/layout_tests.cpp asserts
// that the derived layouts match them exactly.
constexpr size_t kTextSiteCount = 2630, kTextCartesianSites = 1377;
constexpr size_t kExtendedSiteCount = 42645, kExtendedCartesianSites = 31244;

struct TextNode { bool cart; bool dial; double x, y; int row, col, ring, slot; uint32_t id; };

constexpr int kTextGrid = 58, kTextSlots = 144, kTextRings = 8;
bool text_reserved_slot(int slot) { return slot == 0 || slot == 108 || slot == 110; }

std::vector<TextNode> build_text_nodes() {
    constexpr double pitch = 29.0 / kTextGrid;
    const DenseGeometry g = text_geometry();
    std::vector<TextNode> nodes;
    for (int row = 0; row < kTextGrid; ++row) for (int col = 0; col < kTextGrid; ++col) {
        const double x = (col + .5) * pitch, y = (row + .5) * pitch;
        if (dense_site_margin(g, x, y) <= 0.0) continue;
        nodes.push_back({true, false, x, y, row, col, -1, -1,
                         site_identity(0, static_cast<uint32_t>(row), static_cast<uint32_t>(col))});
    }
    // Every non-key dial slot carries a short (0) or long (1) ray.
    for (int slot = 0; slot < kTextSlots; ++slot)
        if (!text_reserved_slot(slot)) nodes.push_back({false, true, 0, 0, -1, -1, -1, slot, site_identity(1, 0, static_cast<uint32_t>(slot))});
    for (int ring = 0; ring < kTextRings; ++ring) for (int slot = 0; slot < kTextSlots; ++slot) {
        if (text_reserved_slot(slot) || (ring == kTextRings - 1 && slot >= 36 && slot <= 51)) continue;
        const double radius = 3.4 + .43 * ring;
        const double a = kTau * slot / kTextSlots;
        nodes.push_back({false, false, kCenter + radius * std::sin(a), kCenter - radius * std::cos(a), -1, -1, ring, slot,
                         site_identity(2, static_cast<uint32_t>(ring), static_cast<uint32_t>(slot))});
    }
    order_sites(nodes, kTextPlacementKey);
    return nodes;
}

const std::vector<TextNode> &text_nodes() { static const std::vector<TextNode> nodes = build_text_nodes(); return nodes; }
const std::vector<uint8_t> &text_whitening() { static const auto bits = whitening_bits(kTextWhiteningSeed, text_nodes().size()); return bits; }

bool text_code_bit(const ckey_text_symbol *s, size_t i) { return ((s->codeword[i / 8] >> (7 - (i % 8))) & 1u) != 0; }
// Printed value of site i: codeword bit (or 0 for fill sites) XOR whitening.
bool text_site_value(const ckey_text_symbol *s, size_t i) {
    const bool data = i < kTextCodewordBytes * 8 && text_code_bit(s, i);
    return data != (text_whitening()[i] != 0);
}

enum class ExtendedKind : uint8_t { cart, ring, ray };
struct ExtendedNode { ExtendedKind kind; double x, y; int ring, slot, component; uint32_t id; };

constexpr int kExtendedGrid = 232, kExtendedRingSlots = 288, kExtendedRings = 34, kExtendedRaySlots = 576;
// Format 0.26 extends the ring field outward (outer ring 23.96 -> 24.62,
// Ring Key 24.45 -> 25.11) so the Ring Key-to-ray gap is 1.19 units, as in
// Profile 2, instead of a 1.85-unit dark annulus. Rays stay at r=26.30 because
// moving them inward would make adjacent wide rays overlap.
constexpr double kExtendedRingInner = 7.0, kExtendedRingOuter = 24.62;
constexpr double kExtendedRingKeyRadius = 25.11;
// Never printed: between the Ring Key and the ray alphabet's inner end.
constexpr double kExtendedPolarGapRadius = 25.7;
constexpr double kExtendedRingPitch = (kExtendedRingOuter - kExtendedRingInner) / (kExtendedRings - 1);
bool extended_reserved_ray(int slot) { return slot == 0 || slot == 432 || slot == 440; }

std::vector<ExtendedNode> build_extended_nodes() {
    constexpr double pitch = .5, center = 58.0;
    const DenseGeometry g = extended_geometry();
    std::vector<ExtendedNode> nodes;
    for (int row = 0; row < kExtendedGrid; ++row) for (int col = 0; col < kExtendedGrid; ++col) {
        const double x = (col + .5) * pitch, y = (row + .5) * pitch;
        if (dense_site_margin(g, x, y) <= 0.0) continue;
        nodes.push_back({ExtendedKind::cart, x, y, -1, -1, -1,
                         site_identity(0, static_cast<uint32_t>(row), static_cast<uint32_t>(col))});
    }
    for (int ring = 0; ring < kExtendedRings; ++ring) for (int slot = 0; slot < kExtendedRingSlots; ++slot) {
        if (slot == 0 || slot == 216 || slot == 224 || (ring == kExtendedRings - 1 && slot >= 72 && slot <= 79)) continue;
        const double radius = kExtendedRingInner + kExtendedRingPitch * ring;
        const double a = kTau * slot / kExtendedRingSlots;
        nodes.push_back({ExtendedKind::ring, center + radius * std::sin(a), center - radius * std::cos(a), ring, slot, -1,
                         site_identity(2, static_cast<uint32_t>(ring), static_cast<uint32_t>(slot))});
    }
    // Every non-key ray is always visible: two Gray-coded length bits plus
    // one width bit form an eight-state alphabet. Each component is a site.
    for (int slot = 0; slot < kExtendedRaySlots; ++slot) {
        if (extended_reserved_ray(slot)) continue;
        for (int component = 0; component < 3; ++component)
            nodes.push_back({ExtendedKind::ray, 0, 0, -1, slot, component,
                             site_identity(3, static_cast<uint32_t>(component), static_cast<uint32_t>(slot))});
    }
    order_sites(nodes, kExtendedPlacementKey);
    return nodes;
}

const std::vector<ExtendedNode> &extended_nodes() { static const std::vector<ExtendedNode> nodes = build_extended_nodes(); return nodes; }
const std::vector<uint8_t> &extended_whitening() { static const auto bits = whitening_bits(kExtendedWhiteningSeed, extended_nodes().size()); return bits; }

bool extended_code_bit(const ckey_extended_symbol *s, size_t i) {
    return ((s->codeword[i / 8] >> (7 - (i % 8))) & 1u) != 0;
}
bool extended_site_value(const ckey_extended_symbol *s, size_t i) {
    const bool data = i < kExtendedBits && extended_code_bit(s, i);
    return data != (extended_whitening()[i] != 0);
}

void set_status(ckey_diagnostics *d, ckey_status s, const char *m) {
    if (!d) return;
    d->abi_version = CKEY_ABI_VERSION; d->status = s;
    d->corrected_symbols = 0; d->erasures_used = 0;
    std::snprintf(d->message, sizeof(d->message), "%s", m);
}

double polar_dot_radius() {
    // A single physical dot size keeps all four data rings and the Ring Key uniform.
    return 0.15;
}

void svg_circle(std::ostringstream &o, double x, double y, double r) {
    o << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << r << "\" stroke=\"none\"/>";
}

// A run of adjacent ring sites from slot position `from` to `to` (in slot
// units), drawn as one round-capped arc. Caps add a dot diameter, so the
// centerline is shortened by one dot radius at each end. Runs spanning more
// than half a turn must set the SVG large-arc flag, or renderers draw the
// complementary short arc on the opposite side of the ring.
void svg_ring_run(std::ostringstream &o, double center, double radius, double from, double to,
                  double slots, double dot_radius, const char *stroke_width) {
    const double cap = dot_radius / radius;
    const double a0 = kTau * from / slots + cap, a1 = kTau * to / slots - cap;
    o << "<path d=\"M " << center + radius * std::sin(a0) << ' ' << center - radius * std::cos(a0)
      << " A " << radius << ' ' << radius << " 0 " << (a1 - a0 > kTau / 2 ? 1 : 0) << " 1 "
      << center + radius * std::sin(a1) << ' ' << center - radius * std::cos(a1)
      << "\" fill=\"none\" stroke-width=\"" << stroke_width << "\" stroke-linecap=\"round\"/>";
}

// CKey's finder follows the rounded corner itself. It is deliberately a
// single broad inset arc rather than a nested set of marks that projects into
// the payload field. Only the top-left arc is interrupted; that clear gap is
// the structural orientation signature.
void svg_corner_finders(std::ostringstream &o, double side, double inset,
                        const std::array<double, 3> &radii, double thickness,
                        double visible_split_gap) {
    for (int corner = 0; corner < 4; ++corner) {
        const std::string transform =
            corner == 0 ? "" :
            corner == 1 ? "translate(" + std::to_string(side) + " 0) scale(-1 1)" :
            corner == 2 ? "translate(" + std::to_string(side) + " " + std::to_string(side) + ") scale(-1 -1)" :
                          "translate(0 " + std::to_string(side) + ") scale(1 -1)";
        o << "<g transform=\"" << transform << "\" fill=\"none\" stroke-linecap=\"round\">";
        for (double radius : radii) {
            if (radius <= 0.0) continue;
            if (corner == 0) {
                // Include cap diameter in the angular calculation: the clear
                // visual gap is exactly visible_split_gap, not merely the
                // shorter centerline gap.
                const double half_gap = (visible_split_gap + thickness) / (2.0 * radius);
                // The outer (top-left) quadrant uses angles 180°..270°.
                // Its concave/open side points down-right, toward the polar
                // field, and the outermost path meets the frame tangents.
                const double middle = 5.0 * kTau / 8.0;
                const double a1 = middle - half_gap, a2 = middle + half_gap;
                o << "<path d=\"M " << inset - radius << ' ' << inset
                  << " A " << radius << ' ' << radius << " 0 0 1 "
                  << inset + radius * std::cos(a1) << ' ' << inset + radius * std::sin(a1)
                  << " M " << inset + radius * std::cos(a2) << ' ' << inset + radius * std::sin(a2)
                  << " A " << radius << ' ' << radius << " 0 0 1 " << inset << ' ' << inset - radius
                  << "\" stroke-width=\"" << thickness << "\"/>";
            } else {
                o << "<path d=\"M " << inset - radius << ' ' << inset
                  << " A " << radius << ' ' << radius << " 0 0 1 " << inset << ' ' << inset - radius
                  << "\" stroke-width=\"" << thickness << "\"/>";
            }
        }
        o << "</g>";
    }
}

struct ImagePoint { double x, y; };

// Lens model for the current thread. A camera lens bends straight lines
// (radial distortion), so a symbol's image is a perspective view followed by
// a radial stretch about the image centre:
//     distorted = centre + (undistorted - centre) * (1 + k1 * r^2),
// with r the undistorted distance from the centre in units of `scale`
// pixels. When a lens is set, every mapping from the canonical field to the
// image goes through it: solve_homography fits the perspective part to the
// undistorted positions of the given image points, and map_point applies
// the stretch. With k1 = 0 (the default) both are the plain projective maps.
struct LensModel { double k1 = 0, cx = 0, cy = 0, scale = 1; };
inline LensModel &active_lens() { static thread_local LensModel lens; return lens; }
struct LensScope {
    LensModel saved;
    explicit LensScope(const LensModel &lens) : saved(active_lens()) { active_lens() = lens; }
    ~LensScope() { active_lens() = saved; }
    LensScope(const LensScope &) = delete; LensScope &operator=(const LensScope &) = delete;
};
inline void lens_distort(const LensModel &lens, double &x, double &y) {
    if (lens.k1 == 0) return;
    const double u = (x - lens.cx) / lens.scale, v = (y - lens.cy) / lens.scale, d = 1 + lens.k1 * (u * u + v * v);
    x = lens.cx + lens.scale * u * d; y = lens.cy + lens.scale * v * d;
}
inline void lens_undistort(const LensModel &lens, double &x, double &y) {
    if (lens.k1 == 0) return;
    const double ud = (x - lens.cx) / lens.scale, vd = (y - lens.cy) / lens.scale;
    double u = ud, v = vd;
    for (int i = 0; i < 10; ++i) { const double d = 1 + lens.k1 * (u * u + v * v); if (std::abs(d) < 1e-6) break; u = ud / d; v = vd / d; }
    x = lens.cx + lens.scale * u; y = lens.cy + lens.scale * v;
}

bool solve_homography_raw(const double src[8], const double dst[8], double h[8]) {
    double a[8][9]{};
    for (int i = 0; i < 4; ++i) {
        const double x = src[2*i], y = src[2*i+1], u = dst[2*i], v = dst[2*i+1];
        a[2*i][0]=x; a[2*i][1]=y; a[2*i][2]=1; a[2*i][6]=-u*x; a[2*i][7]=-u*y; a[2*i][8]=u;
        a[2*i+1][3]=x; a[2*i+1][4]=y; a[2*i+1][5]=1; a[2*i+1][6]=-v*x; a[2*i+1][7]=-v*y; a[2*i+1][8]=v;
    }
    for (int col=0; col<8; ++col) {
        int pivot=col;
        for (int row=col+1; row<8; ++row) if (std::abs(a[row][col])>std::abs(a[pivot][col])) pivot=row;
        if (std::abs(a[pivot][col])<1e-12) return false;
        if (pivot!=col) for (int j=col; j<9; ++j) std::swap(a[pivot][j],a[col][j]);
        const double d=a[col][col]; for (int j=col; j<9; ++j) a[col][j]/=d;
        for (int row=0; row<8; ++row) if (row!=col) { const double f=a[row][col]; for (int j=col; j<9; ++j) a[row][j]-=f*a[col][j]; }
    }
    for (int i=0;i<8;++i) h[i]=a[i][8];
    return true;
}

bool map_point_raw(const double h[8], double x, double y, ImagePoint &p) {
    const double d=h[6]*x+h[7]*y+1.0;
    if (!std::isfinite(d) || std::abs(d)<1e-12) return false;
    p={(h[0]*x+h[1]*y+h[2])/d,(h[3]*x+h[4]*y+h[5])/d};
    return std::isfinite(p.x) && std::isfinite(p.y);
}

// Surface model for the current thread: a symbol on a curved surface (a
// bottle, a bent label, a page that does not lie flat). The surface is a
// cylinder: straight along one direction (at angle `axis` in the field) and
// curved across it with curvature `kappa` (radians per canonical unit;
// its sign says convex or concave). A point at distance a across the axis
// from the field centre sits at sin(kappa a) / kappa across and
// Z = (1 - cos(kappa a)) / kappa out of the plane.
//
// A pinhole camera maps a point (X, Y, Z) to H * (X, Y, 1) + p3 * Z in
// homogeneous pixels, where H is the perspective map of the plane Z = 0 and
// p3 depends on where the camera is. Writing p3 = H * g, the image of the
// bent point is H * (X + g0 Z, Y + g1 Z, 1 + g2 Z): the plane's map applied
// to a shifted point. That form is exact, and g follows from H and the
// focal length (surface_pose in the scan library). The perspective map is
// fitted to the bent positions of the corners, so corners stay where they
// were measured. With kappa = 0 (the default) nothing changes. `focal` records
// the focal length (pixels) g was derived with.
struct SurfaceModel { double kappa = 0, ca = 1, sa = 0, g[3] = {0, 0, 0}, focal = 0; };
inline SurfaceModel &active_surface() { static thread_local SurfaceModel surface; return surface; }
struct SurfaceScope {
    SurfaceModel saved;
    explicit SurfaceScope(const SurfaceModel &surface) : saved(active_surface()) { active_surface() = surface; }
    ~SurfaceScope() { active_surface() = saved; }
    SurfaceScope(const SurfaceScope &) = delete; SurfaceScope &operator=(const SurfaceScope &) = delete;
};
inline void surface_bend(const SurfaceModel &m, double &x, double &y) {
    if (m.kappa == 0) return;
    constexpr double c = 14.5;
    const double u = x - c, v = y - c, across = -u * m.sa + v * m.ca, along = u * m.ca + v * m.sa;
    const double bent = std::sin(m.kappa * across) / m.kappa, out = (1 - std::cos(m.kappa * across)) / m.kappa;
    const double X = c + along * m.ca - bent * m.sa, Y = c + along * m.sa + bent * m.ca, w = 1 + m.g[2] * out;
    if (std::abs(w) < 1e-9) return;
    x = (X + m.g[0] * out) / w; y = (Y + m.g[1] * out) / w;
}

bool solve_homography(const double src[8], const double dst[8], double h[8]) {
    const LensModel &lens = active_lens(); const SurfaceModel &surface = active_surface();
    if (lens.k1 == 0 && surface.kappa == 0) return solve_homography_raw(src, dst, h);
    double flat[8], bent[8];
    for (int i = 0; i < 4; ++i) {
        flat[2 * i] = dst[2 * i]; flat[2 * i + 1] = dst[2 * i + 1]; lens_undistort(lens, flat[2 * i], flat[2 * i + 1]);
        bent[2 * i] = src[2 * i]; bent[2 * i + 1] = src[2 * i + 1]; surface_bend(surface, bent[2 * i], bent[2 * i + 1]);
    }
    return solve_homography_raw(bent, flat, h);
}

bool map_point(const double h[8], double x, double y, ImagePoint &p) {
    surface_bend(active_surface(), x, y);
    if (!map_point_raw(h, x, y, p)) return false;
    lens_distort(active_lens(), p.x, p.y);
    return std::isfinite(p.x) && std::isfinite(p.y);
}

// Profile 1's original 3x3 support, in canonical units. It suits P1's
// one-unit modules; dense-profile marks are 0.10-0.34 units wide, so their
// samplers pass a support proportional to the feature they read.
constexpr double kLocatorSampleSpread = 0.12;
// P1 keeps its original support for dots and dial rays: narrower supports
// helped synthetic renders but lost the noise averaging the committed iPhone
// camera fixtures depend on. Per-class thresholds recover the thin rays.
constexpr double kLocatorDotSpread = kLocatorSampleSpread;
constexpr double kLocatorRaySpread = kLocatorSampleSpread;
// Between P1's outer dot ring (r <= 6.15) and its dial band (r >= 7.6):
// never printed, unlike the former (1.5, 11.5) reference, which is a
// Cartesian payload module.
constexpr double kLocatorApertureRadius = 6.9;
// A dense decoder samples every site only for orientations whose fixed keys
// agree: at least this fraction of key samples must match the expected print
// pattern. Wrong orientations and non-symbols are rejected cheaply.
constexpr double kDenseKeyGate = 0.8;
// Least mark-to-background difference (grey levels) for Profile 0 keys.
constexpr double kMinKeyContrast = 14.0;
constexpr size_t kMaxDecodeAttempts = 8;  // version/orientation hypotheses sampled per decode
constexpr double kModuleSpread = 0.06;   // 0.34-unit dense Cartesian modules
constexpr double kDotSpread = 0.025;     // 0.15-0.20-unit ring dots and arcs
constexpr double kRaySpread = 0.02;      // 0.10-0.16-unit radial strokes
// A point inside the P3 Grid Key clearance: guaranteed unprinted.
constexpr double kExtendedGridClearX = 7.75, kExtendedGridClearY = 45.5;

// Work budget. Every image sample draws on a per-thread allowance that the
// outermost public scan call sets. When it runs out, samples report "outside
// the image" and every search loop winds down, so a call returns "no read"
// instead of running on. The limits are far above what a real symbol needs.
struct WorkBudget { uint64_t left = std::numeric_limits<uint64_t>::max(); int depth = 0; };
inline WorkBudget &work_budget() { static thread_local WorkBudget b; return b; }
struct WorkScope {
    explicit WorkScope(uint64_t samples) { WorkBudget &b = work_budget(); if (b.depth++ == 0) b.left = samples; }
    ~WorkScope() { WorkBudget &b = work_budget(); if (--b.depth == 0) b.left = std::numeric_limits<uint64_t>::max(); }
    WorkScope(const WorkScope &) = delete; WorkScope &operator=(const WorkScope &) = delete;
};
inline bool work_exhausted() { return work_budget().left == 0; }
constexpr uint64_t kFindWorkBudget = 400000;      // samples per ckey_find_borders_gray8
constexpr uint64_t kRefineWorkBudget = 8000000;   // per ckey_refine_quad_gray8
constexpr uint64_t kDecodeWorkBudget = 16000000;   // per ckey_decode_*_gray8
constexpr uint64_t kSurfaceWorkBudget = 1200000;  // of which, at most this much on finding a bent surface

double gray_sample(const uint8_t *gray, size_t width, size_t height, size_t stride,
                   const double h[8], double x, double y, double spread = kLocatorSampleSpread) {
    { WorkBudget &b = work_budget(); if (b.left == 0) return -1; --b.left; }
    if (spread == 0) {  // a single point: one bilinear lookup, not nine identical ones
        ImagePoint p{}; if (!map_point(h,x,y,p)) return -1;
        if (p.x<0 || p.y<0 || p.x>static_cast<double>(width-1) || p.y>static_cast<double>(height-1)) return -1;
        const size_t x0=static_cast<size_t>(p.x), y0=static_cast<size_t>(p.y);
        const size_t x1=std::min(x0+1,width-1), y1=std::min(y0+1,height-1);
        const double fx=p.x-x0, fy=p.y-y0;
        const double top=gray[y0*stride+x0]*(1-fx)+gray[y0*stride+x1]*fx;
        const double bottom=gray[y1*stride+x0]*(1-fx)+gray[y1*stride+x1]*fx;
        return top*(1-fy)+bottom*fy;
    }
    // A small center-weighted 3x3 sample tolerates rasterization and mild blur.
    const double offsets[3]={-spread,0.0,spread};
    constexpr double weights[3]={1.0,2.0,1.0};
    double sum=0, weight_sum=0;
    for (int iy=0;iy<3;++iy) for (int ix=0;ix<3;++ix) {
        ImagePoint p{}; if (!map_point(h,x+offsets[ix],y+offsets[iy],p)) return -1;
        if (p.x<0 || p.y<0 || p.x>static_cast<double>(width-1) || p.y>static_cast<double>(height-1)) return -1;
        const size_t x0=static_cast<size_t>(p.x), y0=static_cast<size_t>(p.y);
        const size_t x1=std::min(x0+1,width-1), y1=std::min(y0+1,height-1);
        const double fx=p.x-x0, fy=p.y-y0;
        const double top=gray[y0*stride+x0]*(1-fx)+gray[y0*stride+x1]*fx;
        const double bottom=gray[y1*stride+x0]*(1-fx)+gray[y1*stride+x1]*fx;
        const double w=weights[ix]*weights[iy]; sum+=(top*(1-fy)+bottom*fy)*w; weight_sum+=w;
    }
    return sum/weight_sum;
}

double polar_sample(const uint8_t *gray, size_t width, size_t height, size_t stride,
                    const double h[8], double radius, double slot, double spread = kLocatorSampleSpread) {
    const double a=kTau*slot/72.0;
    return gray_sample(gray,width,height,stride,h,kCenter+radius*std::sin(a),kCenter-radius*std::cos(a),spread);
}

double polar_sample_slots(const uint8_t *gray, size_t width, size_t height, size_t stride,
                         const double h[8], double center, double radius,
                         double slot, double slot_count, double spread = kLocatorSampleSpread) {
    const double a=kTau*slot/slot_count;
    return gray_sample(gray,width,height,stride,h,center+radius*std::sin(a),center-radius*std::cos(a),spread);
}

// Two-cluster (two-means) split of normalized samples (0 = unprinted
// reference, 1 = printed reference). Whitening makes every feature class
// roughly half printed, so a class's own samples calibrate its threshold;
// this absorbs the lower peak contrast of sub-pixel dots and strokes. The
// cut is bounded to [0.05, 0.95]: P3 width coverage legitimately separates
// near 0.85, so a tighter bound would misplace it. `separation` reports the
// distance between the two cluster means.
double two_means_cut(const std::vector<double> &values, double &separation) {
    separation = 0;
    if (values.size() < 16) return 0.5;
    const auto [min_it, max_it] = std::minmax_element(values.begin(), values.end());
    double threshold = std::clamp((*min_it + *max_it) * 0.5, 0.05, 0.95);
    for (int iteration = 0; iteration < 32; ++iteration) {
        double sum_lo = 0, sum_hi = 0; size_t n_lo = 0, n_hi = 0;
        for (double v : values) { if (v > threshold) { sum_hi += v; ++n_hi; } else { sum_lo += v; ++n_lo; } }
        if (!n_lo || !n_hi) { separation = 0; break; }
        const double lo = sum_lo / n_lo, hi = sum_hi / n_hi;
        separation = hi - lo;
        const double next = std::clamp((lo + hi) * 0.5, 0.05, 0.95);
        if (std::abs(next - threshold) < 1e-6) break;
        threshold = next;
    }
    return threshold;
}

double class_threshold(const std::vector<double> &values) {
    double separation = 0;
    return two_means_cut(values, separation);
}

// Ring sites join into arcs, so under blur an isolated dot peaks far lower
// than a dot inside a run, and a gap between two runs rises. Refine each data
// ring site's decision by its neighbour context (0, 1, or 2 printed ring
// neighbours), with a separate two-means cut per context. Fill sites'
// printed values are known exactly (whitening only), which anchors the
// context. A context keeps the class cut unless its own two clusters are
// clearly separated, so a one-sided context is never split spuriously.
// `grid` maps ring * slots + slot to a site index (-1 for reserved slots).
void refine_ring_context_rings(const std::vector<int> &grid, const std::vector<int> &ring_slots, int stride, size_t data_sites,
                               const std::vector<uint8_t> &white, const std::vector<double> &level,
                               double class_cut, std::vector<uint8_t> &printed);

void refine_ring_context(const std::vector<int> &grid, int rings, int slots, size_t data_sites,
                         const std::vector<uint8_t> &white, const std::vector<double> &level,
                         double class_cut, std::vector<uint8_t> &printed) {
    refine_ring_context_rings(grid, std::vector<int>(static_cast<size_t>(rings), slots), slots, data_sites, white, level, class_cut, printed);
}

// grid maps ring * stride + slot -> site index (-1 reserved); ring r has
// ring_slots[r] slots and wraps at that count.
void refine_ring_context_rings(const std::vector<int> &grid, const std::vector<int> &ring_slots, int stride, size_t data_sites,
                               const std::vector<uint8_t> &white, const std::vector<double> &level,
                               double class_cut, std::vector<uint8_t> &printed) {
    const int rings = static_cast<int>(ring_slots.size());
    constexpr double kMinSeparation = 0.2;
    const auto value_at = [&](int ring, int slot) -> int {
        const int slots = ring_slots[static_cast<size_t>(ring)];
        const int index = grid[static_cast<size_t>(ring) * stride + static_cast<size_t>((slot + slots) % slots)];
        return index < 0 ? 0 : printed[static_cast<size_t>(index)];
    };
    for (int pass = 0; pass < 2; ++pass) {
        std::array<std::vector<double>, 3> by_context;
        std::vector<std::pair<size_t, int>> sites;
        for (int ring = 0; ring < rings; ++ring) for (int slot = 0; slot < ring_slots[static_cast<size_t>(ring)]; ++slot) {
            const int index = grid[static_cast<size_t>(ring) * stride + slot];
            if (index < 0 || static_cast<size_t>(index) >= data_sites) continue;
            const int context = value_at(ring, slot - 1) + value_at(ring, slot + 1);
            by_context[context].push_back(level[static_cast<size_t>(index)]);
            sites.push_back({static_cast<size_t>(index), context});
        }
        std::array<double, 3> cut{class_cut, class_cut, class_cut};
        for (int context = 0; context < 3; ++context) {
            double separation = 0;
            const double candidate = two_means_cut(by_context[context], separation);
            if (separation >= kMinSeparation) cut[context] = candidate;
        }
        for (const auto &[index, context] : sites) printed[index] = level[index] > cut[context] ? 1 : 0;
    }
    (void)white;
}

bool is_tiered_extended_format(uint8_t format) {
    return format == 17u || format == 20u || format == 21u || format == 24u || format == CKEY_EXTENDED_FORMAT_VERSION;
}

bool is_text_format(uint8_t format) {
    return format == 9u || format == 15u || format == 18u || format == 19u || format == 22u || format == 23u ||
           format == CKEY_TEXT_FORMAT_VERSION;
}

bool valid_quad(const double q[8]);

// Shared argument contract for every grayscale entry point.
bool valid_image_args(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8]) {
    if (!gray || !quad || width < 2 || height < 2 || stride < width ||
        height > std::numeric_limits<size_t>::max() / stride) return false;
    for (int i = 0; i < 8; ++i) if (!std::isfinite(quad[i])) return false;
    return valid_quad(quad);
}

bool valid_quad(const double q[8]) {
    double area=0; int sign=0;
    for (int i=0;i<4;++i) {
        const double x1=q[2*i], y1=q[2*i+1], x2=q[2*((i+1)%4)], y2=q[2*((i+1)%4)+1];
        area+=x1*y2-x2*y1;
        const double x3=q[2*((i+2)%4)], y3=q[2*((i+2)%4)+1];
        const double cross=(x2-x1)*(y3-y2)-(y2-y1)*(x3-x2);
        if (std::abs(cross)<1e-6) return false;
        const int current=cross>0?1:-1;
        if (sign && sign!=current) return false;
        sign=current;
    }
    return std::abs(area)>4.0;
}
}

namespace {
// Fixed-key evidence for one candidate homography: contrast between printed
// and never-printed key samples, and the fraction of key samples that agree
// with the expected print pattern. Used by the calibration scores and as a
// cheap per-orientation gate before a dense decoder samples ~2k-21k sites.
struct KeyFit { bool valid = false; double contrast = 0, evidence = 0; };

KeyFit text_key_fit(const uint8_t *gray, size_t width, size_t height, size_t stride, const double h[8]) {
        std::vector<double> marks, empty;
        for (int r : {11,12}) for (int c : {2,3})
            marks.push_back(gray_sample(gray,width,height,stride,h,c+.5,r+.5));
        for (int slot : {36,38,42})
            marks.push_back(polar_sample_slots(gray,width,height,stride,h,kCenter,6.41,slot,144.0,kDotSpread));
        marks.push_back(polar_sample_slots(gray,width,height,stride,h,kCenter,6.41,47.5,144.0,kDotSpread));
        marks.push_back(polar_sample_slots(gray,width,height,stride,h,kCenter,8.8,0.0,144.0,kRaySpread));
        marks.push_back(polar_sample_slots(gray,width,height,stride,h,kCenter,8.4,108.0,144.0,kRaySpread));
        marks.push_back(polar_sample_slots(gray,width,height,stride,h,kCenter,8.8,110.0,144.0,kRaySpread));
        constexpr double finder_inset=4.0, finder_radius=2.3;
        const double diagonal=kTau/8.0;
        for (const auto &[cx,cy,a] : {std::tuple{25.0,4.0,kTau*315.0/360.0},
                                      std::tuple{4.0,25.0,kTau*135.0/360.0},
                                      std::tuple{25.0,25.0,kTau*45.0/360.0}})
            marks.push_back(gray_sample(gray,width,height,stride,h,cx+finder_radius*std::cos(a),cy+finder_radius*std::sin(a)));
        for (double a : {kTau*198.0/360.0,kTau*252.0/360.0})
            marks.push_back(gray_sample(gray,width,height,stride,h,finder_inset+finder_radius*std::cos(a),finder_inset+finder_radius*std::sin(a)));
        // The top-left finder's split gap (225 deg) is never printed. (The 45 deg
        // point used by Profile 1 is a payload site under Profile 2's safe area.)
        empty.push_back(gray_sample(gray,width,height,stride,h,finder_inset+finder_radius*std::cos(5.0*diagonal),finder_inset+finder_radius*std::sin(5.0*diagonal)));
        empty.push_back(gray_sample(gray,width,height,stride,h,1.5,11.5));
        empty.push_back(polar_sample_slots(gray,width,height,stride,h,kCenter,6.41,40.0,144.0,kDotSpread));
        empty.push_back(polar_sample_slots(gray,width,height,stride,h,kCenter,8.8,108.0,144.0,kRaySpread));
        if (std::any_of(marks.begin(),marks.end(),[](double v){return v<0;}) || std::any_of(empty.begin(),empty.end(),[](double v){return v<0;})) return {};
        const double mark_mean=std::accumulate(marks.begin(),marks.end(),0.0)/marks.size();
        const double empty_mean=std::accumulate(empty.begin(),empty.end(),0.0)/empty.size();
        const double contrast=std::abs(mark_mean-empty_mean), threshold=(mark_mean+empty_mean)*.5;
        const bool dark=mark_mean<empty_mean;
        const auto marked=[&](double value){return dark ? value<threshold : value>threshold;};
        const size_t evidence=std::count_if(marks.begin(),marks.end(),marked)+
            std::count_if(empty.begin(),empty.end(),[&](double value){return !marked(value);});
        return {true, contrast, static_cast<double>(evidence)/static_cast<double>(marks.size()+empty.size())};
}

KeyFit extended_key_fit(const uint8_t *gray, size_t width, size_t height, size_t stride, const double h[8]) {
        std::vector<double> marks, empty;
        // Grid Key, Ring Key, and Ray Key are all fixed in physical modules.
        for (int r=0;r<2;++r) for (int c=0;c<2;++c) marks.push_back(gray_sample(gray,width,height,stride,h,8.25+c*.5,45.25+r*.5,kModuleSpread));
        for (int slot : {72,73,75}) marks.push_back(polar_sample_slots(gray,width,height,stride,h,58.0,kExtendedRingKeyRadius,slot,288.0,kDotSpread));
        marks.push_back(polar_sample_slots(gray,width,height,stride,h,58.0,kExtendedRingKeyRadius,77.5,288.0,kDotSpread));
        marks.push_back(polar_sample_slots(gray,width,height,stride,h,58.0,28.0,0.0,576.0,kRaySpread));
        marks.push_back(polar_sample_slots(gray,width,height,stride,h,58.0,27.9,432.0,576.0,kRaySpread));
        marks.push_back(polar_sample_slots(gray,width,height,stride,h,58.0,28.4,440.0,576.0,kRaySpread));
        // Three whole finder arcs plus both halves of the split orientation
        // arc. These are measured on the actual *outer* corner quadrants: a
        // finder mouth faces the polar field but its ink follows the frame.
        constexpr double finder_radius=9.2;
        for (const auto &[cx,cy,a] : {std::tuple{100.0,16.0,kTau*315.0/360.0},
                                      std::tuple{16.0,100.0,kTau*135.0/360.0},
                                      std::tuple{100.0,100.0,kTau*45.0/360.0}})
            marks.push_back(gray_sample(gray,width,height,stride,h,cx+finder_radius*std::cos(a),cy+finder_radius*std::sin(a)));
        for (double a : {kTau*195.0/360.0,kTau*255.0/360.0})
            marks.push_back(gray_sample(gray,width,height,stride,h,16.0+finder_radius*std::cos(a),16.0+finder_radius*std::sin(a)));
        empty.push_back(gray_sample(gray,width,height,stride,h,16.0+finder_radius*std::cos(kTau*225.0/360.0),16.0+finder_radius*std::sin(kTau*225.0/360.0)));
        empty.push_back(gray_sample(gray,width,height,stride,h,kExtendedGridClearX,kExtendedGridClearY,kModuleSpread));
        empty.push_back(polar_sample_slots(gray,width,height,stride,h,58.0,kExtendedRingKeyRadius,74.0,288.0,kDotSpread));
        empty.push_back(polar_sample_slots(gray,width,height,stride,h,58.0,kExtendedPolarGapRadius,216.0,576.0,kRaySpread));
        if (std::any_of(marks.begin(),marks.end(),[](double v){return v<0;}) || std::any_of(empty.begin(),empty.end(),[](double v){return v<0;})) return {};
        const double mark_mean=std::accumulate(marks.begin(),marks.end(),0.0)/marks.size();
        const double empty_mean=std::accumulate(empty.begin(),empty.end(),0.0)/empty.size();
        const double contrast=std::abs(mark_mean-empty_mean); const bool dark=mark_mean<empty_mean;
        const double threshold=(mark_mean+empty_mean)*.5;
        const auto marked=[&](double value){ return dark ? value<threshold : value>threshold; };
        const size_t evidence=std::count_if(marks.begin(),marks.end(),marked)+
            std::count_if(empty.begin(),empty.end(),[&](double value){return !marked(value);});
        return {true, contrast, static_cast<double>(evidence)/static_cast<double>(marks.size()+empty.size())};
}

}


// ---------------------------------------------------------------------------
// Profile 0 (Format 0.27): the scalable family. All versions share Profile
// 2's skeleton in the same 29-unit canonical field: frame, finders, a polar
// centre whose rings start at r = 3.4, and a dial whose rays start at r = 7.6.
// Version v sets the module pitch p = 29 / G(v); every payload mark, key, and
// clearance scales with p. The fixed integer tables below are normative.
// ---------------------------------------------------------------------------
namespace {

constexpr int kScalableVersions = static_cast<int>(CKEY_SCALABLE_VERSION_COUNT);
constexpr std::array<int, kScalableVersions> kScalableGrid = {29, 31, 33, 36, 38, 41, 44, 47, 50, 54, 58, 62, 67, 71, 77, 82, 88, 94, 101, 108, 116, 124, 133, 143, 153, 164, 176, 188, 202, 216, 232};
constexpr std::array<int, kScalableVersions> kScalableSlots = {72, 80, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152, 168, 176, 192, 200, 216, 232, 248, 272, 288, 304, 328, 352, 376, 408, 440, 464, 504, 536, 576};
constexpr std::array<int, kScalableVersions> kScalableRings = {3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 22, 23, 25, 27, 29, 32, 34, 37};
constexpr std::array<int, kScalableVersions> kScalableGridCol = {2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 23};
constexpr std::array<int, kScalableVersions> kScalableGridRow = {10, 12, 13, 14, 15, 16, 17, 18, 20, 21, 23, 25, 27, 28, 31, 33, 35, 38, 41, 44, 47, 50, 54, 58, 62, 67, 72, 77, 83, 88, 95};
// Parity as a percentage of each RS block. A level corrects half that
// fraction in unknown bad bytes, or all of it in known-missing bytes
// (erasures). U (Ultra) and X (Extreme) trade most of the payload for damage
// tolerance: same code, same decoder, more parity.
constexpr int kScalableLevels = 6;
constexpr std::array<int, kScalableLevels> kScalableParityPercent = {14, 28, 44, 60, 80, 95};  // L, M, Q, H, U, X
constexpr char kScalableLevelNames[] = "LMQHUX";
constexpr double kScalableRingInner = 3.4, kScalableDialInner = 7.6;
constexpr size_t kScalableHeaderBytes = 8, kScalableOverheadBytes = 12;  // header + CRC-32C

struct ScalableNode { uint8_t kind; double x, y; int ring, slot; uint32_t id; };  // kind: 0 Cartesian, 1 dial, 2 ring

// Geometry families. 0 = standard. 1 = print: every mark and gap is at least
// about one module, for small printed sizes. A layout (ckey_scalable_layout)
// is a family plus a placement rule: layout & 1 is the family, layout >> 1 is
// the placement (0 = bit-scattered, Formats 0.27/0.28; 1 = byte-grouped,
// Formats 0.29/0.30). Both placements of a family share every site, mark and
// key, so a symbol looks the same and a reader samples it the same way; only
// the site-to-codeword-bit assignment differs.
constexpr int kScalableFamilies = 4;
constexpr int kScalableLayouts = 7;
// Pixel family (layout 4, Format 0.31, experimental): the Print family's frame,
// finders, grid pitch, dial and keys, with a single ring at the ring zone's
// inner radius that carries only the Ring Key; the annulus out to the dial
// holds grid modules instead of rings, and short rays are 0.6 module. At one cell per module every
// grid and ring data site is a distinct cell. No byte-grouped placement yet.
// Standard arc family (3; layouts 5 and 6, Formats 0.32 and 0.33): the
// Standard family's grid, dial, marks and keys, but each ring's slot count
// scales with its radius (8 * floor(D * r / r_outer / 8), the outermost ring
// keeping the dial's D slots), so inner-ring marks are as far apart as the
// outermost ring's. The Ring Key sits at 6:00, where Standard's is at 3:00,
// so a reader's version fit tells the two families apart.
constexpr int scalable_family(int layout) { return layout == 4 ? 2 : layout >= 5 ? 3 : (layout & 1); }
constexpr bool scalable_grouped(int layout) { return layout == 6 || (layout < 4 && (layout >> 1) != 0); }
// The layout of a family and placement, or -1 when that pair does not exist.
constexpr int scalable_layout_of(int family, bool grouped) {
    return family == 2 ? (grouped ? -1 : 4) : family == 3 ? (grouped ? 6 : 5) : family + (grouped ? 2 : 0);
}
// Families drawn with Standard's fine marks and keys (Standard and Standard arc).
constexpr bool scalable_fine(int family) { return family == 0 || family == 3; }
// " (arc)" etc. after "Profile 0" in messages.
constexpr const char *scalable_family_label(int family) { return family == 1 ? ".1 (print)" : family == 2 ? " (pixel)" : family == 3 ? " (arc)" : ""; }
constexpr std::array<int, kScalableVersions> kScalablePrintRings = {2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 7, 7, 8, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 22, 24, 25, 27, 29, 32};

struct ScalableLayout {
    int version = 0, family = 0, grid = 0, rings = 0, col0 = 0, row0 = 0;
    bool grouped = false;          // byte-grouped placement (Formats 0.29/0.30)
    std::vector<uint32_t> base_index;  // grouped only: this site's index in the family's scattered layout
    int dial_slots = 0;            // dial ray slots (Ray Keys use these)
    int ring_stride = 0;           // max ring slots (ring_grid row length)
    std::vector<int> ring_slots;   // slots on each ring
    double pitch = 0;
    double ring_pitch = 0, dot = 0, ray_width = 0, long_len = 0, short_len = 0, dial_sample = 0;
    int ring_key_span = 16;
    DenseGeometry geom{};
    std::vector<ScalableNode> nodes;
    std::vector<uint8_t> white;
    std::vector<int> ring_grid;    // ring * ring_stride + slot -> site index, -1 reserved
    size_t codeword_bytes = 0;
    uint8_t format = CKEY_SCALABLE_FORMAT_VERSION;
    double ring_radius(int k) const { return kScalableRingInner + ring_pitch * k; }
    int ray_key_half() const { return 3 * dial_slots / 4; }       // 9:00, half length
    int ray_key_three() const { return 3 * dial_slots / 4 - 2; }  // two slots before, 3/4 length
    int ring_key_slot() const { return family == 3 ? ring_slots.back() / 2 : ring_slots.back() / 4; }  // 3:00 (6:00 for Standard arc), on the outermost ring
    bool reserved_ray(int slot) const { return slot == 0 || slot == ray_key_half() || slot == ray_key_three(); }
    double long_ray() const { return long_len; }
    double short_ray() const { return short_len; }
    double key_ray() const { return 3.4 * pitch; }
    // Ring Key on the outermost ring, in slots relative to ring_key_slot():
    // standard 16-slot pattern (dots 0, 2, 6; run 7.5..15.5), print 8-slot
    // pattern (dots 0, 2; run 3.5..7.5).
    std::vector<int> ring_key_dots() const { return scalable_fine(family) ? std::vector<int>{0, 2, 6} : std::vector<int>{0, 2}; }
    std::pair<double, double> ring_key_run() const { return scalable_fine(family) ? std::pair{7.5, 15.5} : std::pair{3.5, 7.5}; }
    std::vector<double> ring_key_marks() const { return scalable_fine(family) ? std::vector<double>{0, 2, 6, 11.5} : std::vector<double>{0, 2, 5.5}; }
    std::vector<double> ring_key_empties() const { return scalable_fine(family) ? std::vector<double>{4} : std::vector<double>{1, 3}; }
};

DenseGeometry scalable_geometry(int col0, int row0, double p, int slots, double ray_ink_outer) {
    const double a = kTau * (3.0 * slots / 4.0 - 2.0) / slots;
    const double clear = std::max(0.375, p);
    return {14.5, 14.0, 3.5, 0.325, clear,
            4.0, 2.3, 0.4, clear, 29.0,
            col0 * p + 0.16 * p, row0 * p + 0.16 * p, (col0 + 2) * p - 0.16 * p, (row0 + 2) * p - 0.16 * p, p,
            kCenter, ray_ink_outer, p,
            kScalableDialInner, 3.4 * p, 0.22 * p,
            {{0.0, -1.0}, {-1.0, 0.0}, {std::sin(a), -std::cos(a)}},
            {1.0, 0.5, 0.75},
            0.34 * p};
}

// Equal-arc slot count: the largest multiple of 8 whose arc spacing at
// radius r is at least one module (print family).
int equal_arc_slots(double r, double p) { return 8 * static_cast<int>(std::floor(kTau * r / (8.0 * p))); }

// Byte-grouped placement (Formats 0.29 and 0.30). Each codeword byte's eight
// bits go to eight neighbouring sites (a "clump"), so local damage costs the
// bytes under it and no others.
//   1. Each site kind has a path. Cartesian sites: the four quadrants in
//      turn (top-left, top-right, bottom-right, bottom-left), each walked in
//      a serpentine over its row pairs (pair k is the quadrant's rows 2k and
//      2k+1; its columns ascending for even k, descending for odd k; at
//      each column the upper row first). Dial rays by slot. Ring sites by
//      ring, then slot.
//   2. Each path is cut into runs of 8. The fewer-than-8 sites left over on
//      each path are joined (Cartesian, dial, ring) and cut the same way. At
//      most 7 sites then remain; they carry whitening only.
//   3. Clumps are numbered in that order (Cartesian, dial, ring, mixed).
//      Codeword byte j uses the clump with the j-th smallest value of
//      (lowbias32(g XOR key), g), where g is the clump number and
//      key = lowbias32(placement XOR version). This keyed shuffle spreads each
//      RS block's bytes over the whole field.
//   4. Bit 7 (the first) of a byte is the first site of its clump's run.
void order_sites_grouped(std::vector<ScalableNode> &nodes, int grid, uint32_t key) {
    std::array<std::vector<size_t>, 3> path;
    std::vector<int> lattice(static_cast<size_t>(grid) * grid, -1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].kind == 0) lattice[static_cast<size_t>((nodes[i].id >> 12) & 0xfffu) * grid + (nodes[i].id & 0xfffu)] = static_cast<int>(i);
        else path[nodes[i].kind].push_back(i);  // generated by slot, and by ring then slot
    }
    // Quadrants in the order top-left, top-right, bottom-right, bottom-left.
    // Rows [0, half) are "top" and columns [0, half) are "left", half = ceil(grid / 2).
    const int half = (grid + 1) / 2;
    const int quadrant[4][4] = {{0, half, 0, half}, {0, half, half, grid}, {half, grid, half, grid}, {half, grid, 0, half}};  // row0, row1, col0, col1
    for (const auto &q : quadrant) for (int pair = 0; q[0] + 2 * pair < q[1]; ++pair) for (int step = 0; step < q[3] - q[2]; ++step) {
        const int col = pair % 2 == 0 ? q[2] + step : q[3] - 1 - step;
        for (int row = q[0] + 2 * pair; row < std::min(q[0] + 2 * pair + 2, q[1]); ++row) {
            const int index = lattice[static_cast<size_t>(row) * grid + col];
            if (index >= 0) path[0].push_back(static_cast<size_t>(index));
        }
    }
    std::vector<std::array<size_t, 8>> clumps; std::vector<size_t> rest;
    for (const auto &run : path) {
        const size_t whole = run.size() / 8;
        for (size_t c = 0; c < whole; ++c) { std::array<size_t, 8> clump{}; std::copy_n(run.begin() + static_cast<std::ptrdiff_t>(8 * c), 8, clump.begin()); clumps.push_back(clump); }
        rest.insert(rest.end(), run.begin() + static_cast<std::ptrdiff_t>(8 * whole), run.end());
    }
    const size_t mixed = rest.size() / 8;
    for (size_t c = 0; c < mixed; ++c) { std::array<size_t, 8> clump{}; std::copy_n(rest.begin() + static_cast<std::ptrdiff_t>(8 * c), 8, clump.begin()); clumps.push_back(clump); }
    std::vector<uint32_t> order(clumps.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [key](uint32_t l, uint32_t r) {
        const uint32_t hl = site_hash(l ^ key), hr = site_hash(r ^ key);
        return hl != hr ? hl < hr : l < r;
    });
    std::vector<ScalableNode> out; out.reserve(nodes.size());
    for (uint32_t g : order) for (size_t index : clumps[g]) out.push_back(nodes[index]);
    for (size_t i = 8 * mixed; i < rest.size(); ++i) out.push_back(nodes[rest[i]]);
    nodes.swap(out);
}

const ScalableLayout &scalable_layout(int v, int layout = 0);

std::unique_ptr<ScalableLayout> build_scalable_layout(int v, int layout) {
    auto L = std::make_unique<ScalableLayout>();
    const int family = scalable_family(layout);
    const size_t i = static_cast<size_t>(v - 1);
    L->version = v; L->family = family; L->grouped = scalable_grouped(layout); L->grid = kScalableGrid[i];
    L->col0 = kScalableGridCol[i]; L->row0 = kScalableGridRow[i]; L->pitch = 29.0 / L->grid;
    const double p = L->pitch;
    if (scalable_fine(family)) {
        L->rings = kScalableRings[i]; L->dial_slots = kScalableSlots[i];
        L->ring_slots.assign(static_cast<size_t>(L->rings), L->dial_slots);
        L->ring_pitch = 0.86 * p; L->dot = 0.2 * p; L->ray_width = 0.2 * p; L->long_len = 2.2 * p; L->short_len = 1.0 * p;
        L->dial_sample = 1.6 * p; L->ring_key_span = 16;
        if (family == 3) {
            // Equal arc: the outermost ring keeps D slots; inner rings scale with radius.
            const double outer = L->ring_radius(L->rings - 1);
            for (int k = 0; k < L->rings; ++k)
                L->ring_slots[static_cast<size_t>(k)] = std::max(8, 8 * static_cast<int>(std::floor(L->dial_slots * L->ring_radius(k) / outer / 8.0 + 1e-9)));
            L->format = L->grouped ? CKEY_SCALABLE_ARC_GROUPED_FORMAT_VERSION : CKEY_SCALABLE_ARC_FORMAT_VERSION;
        } else {
            L->format = L->grouped ? CKEY_SCALABLE_GROUPED_FORMAT_VERSION : CKEY_SCALABLE_FORMAT_VERSION;
        }
    } else if (family == 2) {
        L->rings = 1; L->ring_pitch = 1.0 * p;
        // Rays two modules apart at the dial, so at one cell per module a
        // slanted ray never crosses its neighbour's sampled cell.
        L->dial_slots = equal_arc_slots(kScalableDialInner, 2 * p);
        L->ring_slots.push_back(equal_arc_slots(kScalableRingInner, p));
        // Short rays end 1.1 modules short of the sample point, so at one
        // cell per module a short ray never reaches the sampled cell.
        L->dot = 0.3 * p; L->ray_width = 0.35 * p; L->long_len = 2.4 * p; L->short_len = 0.6 * p;
        L->dial_sample = 1.7 * p; L->ring_key_span = 8;
        L->format = CKEY_SCALABLE_PIXEL_FORMAT_VERSION;
        if (v == 1) L->row0 = 9;
    } else {
        L->rings = kScalablePrintRings[i]; L->ring_pitch = 1.0 * p;
        L->dial_slots = equal_arc_slots(kScalableDialInner, p);
        for (int k = 0; k < L->rings; ++k) L->ring_slots.push_back(equal_arc_slots(L->ring_radius(k), p));
        L->dot = 0.3 * p; L->ray_width = 0.35 * p; L->long_len = 2.4 * p; L->short_len = 1.0 * p;
        L->dial_sample = 1.7 * p; L->ring_key_span = 8;
        L->format = L->grouped ? CKEY_SCALABLE_PRINT_GROUPED_FORMAT_VERSION : CKEY_SCALABLE_PRINT_FORMAT_VERSION;
        if (v == 1) L->row0 = 9;  // its longer rays need the v1 Grid Key one row higher for a pitch of clearance
    }
    L->ring_stride = *std::max_element(L->ring_slots.begin(), L->ring_slots.end());
    L->geom = scalable_geometry(L->col0, L->row0, p, L->dial_slots, kScalableDialInner + L->long_len + L->ray_width / 2);
    if (family == 2) { L->geom.annulus_inner = L->ring_radius(0) + L->dot; L->geom.annulus_outer = kScalableDialInner; }
    for (int row = 0; row < L->grid; ++row) for (int col = 0; col < L->grid; ++col) {
        const double x = (col + .5) * p, y = (row + .5) * p;
        if (dense_site_margin(L->geom, x, y) <= 0.0) continue;
        L->nodes.push_back({0, x, y, -1, -1, site_identity(0, static_cast<uint32_t>(row), static_cast<uint32_t>(col))});
    }
    for (int slot = 0; slot < L->dial_slots; ++slot)
        if (!L->reserved_ray(slot)) L->nodes.push_back({1, 0, 0, -1, slot, site_identity(1, 0, static_cast<uint32_t>(slot))});
    const int key = L->ring_key_slot();
    for (int ring = 0; ring < L->rings; ++ring) {
        const int S = L->ring_slots[static_cast<size_t>(ring)];
        for (int slot = 0; slot < S; ++slot) {
            // Standard families: ring slots are also cleared on the Ray Key bearings
            // (inherited from Profile 2): the slot nearest each bearing on this ring.
            bool bearing = false;
            if (scalable_fine(family)) {
                if (S == L->dial_slots) bearing = L->reserved_ray(slot);
                else for (const int ray : {0, L->ray_key_half(), L->ray_key_three()})
                    if ((ray * S * 2 + L->dial_slots) / (2 * L->dial_slots) % S == slot) bearing = true;
            }
            if (bearing || (ring == L->rings - 1 && slot >= key && slot < key + L->ring_key_span)) continue;
            if (family == 2) continue;  // Pixel: the ring carries only the Ring Key; its data is on the grid
            const double r = L->ring_radius(ring), a = kTau * slot / S;
            L->nodes.push_back({2, kCenter + r * std::sin(a), kCenter - r * std::cos(a), ring, slot,
                                site_identity(2, static_cast<uint32_t>(ring), static_cast<uint32_t>(slot))});
        }
    }
    const uint32_t whitening = 0x434b5730u + static_cast<uint32_t>(family);  // "CKW0" .. "CKW3"
    if (L->grouped) {
        const uint32_t placement = family == 0 ? 0x434b4730u : family == 3 ? 0x434b4733u : 0x434b4731u;  // "CKG0" / "CKG3" / "CKG1"
        order_sites_grouped(L->nodes, L->grid, site_hash(placement ^ static_cast<uint32_t>(v)));
        const ScalableLayout &base = scalable_layout(v, scalable_layout_of(family, false));
        std::vector<std::pair<uint32_t, uint32_t>> ids; ids.reserve(base.nodes.size());
        for (size_t n = 0; n < base.nodes.size(); ++n) ids.push_back({base.nodes[n].id, static_cast<uint32_t>(n)});
        std::sort(ids.begin(), ids.end());
        L->base_index.resize(L->nodes.size());
        for (size_t n = 0; n < L->nodes.size(); ++n)
            L->base_index[n] = std::lower_bound(ids.begin(), ids.end(), std::pair<uint32_t, uint32_t>{L->nodes[n].id, 0u})->second;
    } else {
        const uint32_t placement = 0x434b5030u + static_cast<uint32_t>(family);  // "CKP0" .. "CKP3"
        order_sites(L->nodes, site_hash(placement ^ static_cast<uint32_t>(v)));
    }
    L->white = whitening_bits(whitening + static_cast<uint32_t>(v), L->nodes.size());
    L->codeword_bytes = L->nodes.size() / 8;
    L->ring_grid.assign(static_cast<size_t>(L->rings) * L->ring_stride, -1);
    for (size_t n = 0; n < L->nodes.size(); ++n)
        if (L->nodes[n].kind == 2) L->ring_grid[static_cast<size_t>(L->nodes[n].ring) * L->ring_stride + L->nodes[n].slot] = static_cast<int>(n);
    return L;
}

const ScalableLayout &scalable_layout(int v, int layout) {
    static std::array<std::once_flag, kScalableVersions * kScalableLayouts> once;
    static std::array<std::unique_ptr<ScalableLayout>, kScalableVersions * kScalableLayouts> cache;
    const size_t i = static_cast<size_t>(layout * kScalableVersions + v - 1);
    std::call_once(once[i], [&] { cache[i] = build_scalable_layout(v, layout); });
    return *cache[i];
}

// The bit-scattered layout of a geometry family (0 standard, 1 print, 2 pixel).
const ScalableLayout &scalable_family_layout(int v, int family) { return scalable_layout(v, scalable_layout_of(family, false)); }

struct RsBlock { size_t n, k; };

// The codeword is split into ceil(C/255) near-equal RS blocks (longer ones
// first); each keeps 2*round(pct*n/200) parity bytes (integer arithmetic).
std::vector<RsBlock> scalable_blocks(size_t codeword_bytes, int level) {
    std::vector<RsBlock> blocks;
    const size_t count = (codeword_bytes + 254) / 255;
    for (size_t b = 0; b < count; ++b) {
        const size_t n = codeword_bytes / count + (b < codeword_bytes % count ? 1 : 0);
        const size_t parity = std::max<size_t>(2, 2 * ((static_cast<size_t>(kScalableParityPercent[static_cast<size_t>(level)]) * n + 100) / 200));
        blocks.push_back({n, n - std::min(parity, n - 1)});
    }
    return blocks;
}

size_t scalable_data_bytes(size_t codeword_bytes, int level) {
    size_t k = 0; for (const auto &b : scalable_blocks(codeword_bytes, level)) k += b.k; return k;
}

size_t scalable_capacity(int v, int level, int family = 0) {
    if (v < 1 || v > kScalableVersions || level < 0 || level >= kScalableLevels || family < 0 || family >= kScalableLayouts) return 0;
    const size_t k = scalable_data_bytes(scalable_layout(v, family).codeword_bytes, level);
    return k > kScalableOverheadBytes ? std::min<size_t>(k - kScalableOverheadBytes, 65535) : 0;
}

bool scalable_site_value(const ScalableLayout &L, const ckey_scalable_symbol *s, size_t i) {
    const bool data = i < L.codeword_bytes * 8 && ((s->codeword[i / 8] >> (7 - i % 8)) & 1u) != 0;
    return data != (L.white[i] != 0);
}

// Contrast/agreement fit of fixed samples (marks expected printed, empties
// expected clear).
KeyFit sample_fit(const std::vector<double> &all_marks, const std::vector<double> &all_empty) {
    // Samples outside the image are dropped, so a symbol partly out of frame
    // can still be fitted; at least 60% of each kind must remain.
    std::vector<double> marks, empty;
    for (double v : all_marks) if (v >= 0) marks.push_back(v);
    for (double v : all_empty) if (v >= 0) empty.push_back(v);
    if (marks.empty() || empty.empty() || marks.size() * 10 < all_marks.size() * 6 || empty.size() * 10 < all_empty.size() * 6) return {};
    const double mark_mean = std::accumulate(marks.begin(), marks.end(), 0.0) / marks.size();
    const double empty_mean = std::accumulate(empty.begin(), empty.end(), 0.0) / empty.size();
    const bool dark = mark_mean < empty_mean; const double threshold = (mark_mean + empty_mean) * .5;
    const auto marked = [&](double v) { return dark ? v < threshold : v > threshold; };
    const size_t evidence = std::count_if(marks.begin(), marks.end(), marked) +
                            std::count_if(empty.begin(), empty.end(), [&](double v) { return !marked(v); });
    return {true, std::abs(mark_mean - empty_mean), static_cast<double>(evidence) / static_cast<double>(marks.size() + empty.size())};
}

// Version- and style-independent skeleton: the frame stroke and both arms of
// all four finders, sampled near the arm ends (common to every finder style:
// arc, pill, squircle, bracket), against the frame moat and the outer quiet
// margin. It is symmetric under the 8 orientations on purpose: finder styles
// change the top-left split's shape, and a logo may occupy the centre, so
// orientation comes from the asymmetric Grid, Ring, and Ray Keys instead.
KeyFit scalable_skeleton_fit(const uint8_t *gray, size_t width, size_t height, size_t stride, const double h[8]) {
    std::vector<double> marks, empty;
    for (const auto &[x, y] : {std::pair{14.5, 0.5}, {28.5, 14.5}, {14.5, 28.5}, {0.5, 14.5}, {8.0, 0.5}, {21.0, 0.5}, {0.5, 21.0}, {28.5, 8.0}})
        marks.push_back(gray_sample(gray, width, height, stride, h, x, y));
    // Arm points at 198 and 252 degrees of the canonical arc (radius 2.3 about
    // the corner inset 4), mirrored into each corner.
    const double arm[2][2] = {{2.3 * std::cos(kTau * 198.0 / 360.0), 2.3 * std::sin(kTau * 198.0 / 360.0)},
                              {2.3 * std::cos(kTau * 252.0 / 360.0), 2.3 * std::sin(kTau * 252.0 / 360.0)}};
    for (const auto &[cx, cy, mx, my] : {std::tuple{4.0, 4.0, 1.0, 1.0}, {25.0, 4.0, -1.0, 1.0}, {4.0, 25.0, 1.0, -1.0}, {25.0, 25.0, -1.0, -1.0}})
        for (const auto &o : arm) marks.push_back(gray_sample(gray, width, height, stride, h, cx + mx * o[0], cy + my * o[1]));
    // Frame moat (payload ink starts at least 0.825 + 0.375 from each edge)
    // and the mandatory outer quiet margin (-1..0).
    for (const auto &[x, y] : {std::pair{14.5, 1.0}, {28.0, 14.5}, {14.5, 28.0}, {1.0, 14.5},
                               {14.5, -0.4}, {29.4, 14.5}, {14.5, 29.4}, {-0.4, 14.5}})
        empty.push_back(gray_sample(gray, width, height, stride, h, x, y, 0.05));
    return sample_fit(marks, empty);
}

// Version-specific keys at the version's own module and slot pitch: the 2x2
// Grid Key on the payload lattice, the Ring Key's dot/gap/run pattern on the
// outermost ring, and the three Ray Keys. A wrong version misses them.
KeyFit scalable_version_fit(const ScalableLayout &L, const uint8_t *gray, size_t width, size_t height, size_t stride, const double h[8]) {
    const double p = L.pitch, rk = L.ring_radius(L.rings - 1), S = L.ring_slots.back(), D = L.dial_slots, ds = 0.25 * L.dot;
    // Three key groups, each with its own marks and blanks: the Grid Key,
    // the Ring Key and the Ray Keys.
    std::array<std::vector<double>, 3> marks, empty;
    for (int r = 0; r < 2; ++r) for (int c = 0; c < 2; ++c)
        marks[0].push_back(gray_sample(gray, width, height, stride, h, (L.col0 + c + .5) * p, (L.row0 + r + .5) * p, 0.12 * p));
    empty[0].push_back(gray_sample(gray, width, height, stride, h, (L.col0 - 1 + .5) * p, (L.row0 + .5) * p, 0.12 * p));
    const int k = L.ring_key_slot();
    for (double slot : L.ring_key_marks()) marks[1].push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, rk, k + slot, S, ds));
    for (double slot : L.ring_key_empties()) empty[1].push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, rk, k + slot, S, ds));
    marks[2].push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 3.0 * p, 0, D, 0.04 * p));
    marks[2].push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 1.3 * p, L.ray_key_half(), D, 0.04 * p));
    marks[2].push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 2.3 * p, L.ray_key_three(), D, 0.04 * p));
    empty[2].push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 2.6 * p, L.ray_key_half(), D, 0.04 * p));
    std::vector<double> all_marks, all_empty;
    for (int g = 0; g < 3; ++g) { all_marks.insert(all_marks.end(), marks[g].begin(), marks[g].end()); all_empty.insert(all_empty.end(), empty[g].begin(), empty[g].end()); }
    KeyFit fit = sample_fit(all_marks, all_empty);
    if (!fit.valid) return fit;
    // Judge each group against its own blanks. One global threshold sits
    // between the darkest keys (grid, rays) and paper, so in a small or
    // blurred code the Ring Key's faint dots, clearly darker than their own
    // ring's blanks, fell on the paper side and the true version failed its
    // keys while its data read. A group whose marks are not clearly apart
    // from its blanks (under a quarter of the global contrast) earns no
    // evidence, so noise in a wrong hypothesis does not pass.
    double sum_m = 0, sum_e = 0; size_t n_m = 0, n_e = 0;
    for (double v : all_marks) if (v >= 0) { sum_m += v; ++n_m; }
    for (double v : all_empty) if (v >= 0) { sum_e += v; ++n_e; }
    const bool dark = sum_m / n_m < sum_e / n_e;  // sample_fit made sure both are non-empty
    size_t evidence = 0, total = 0;
    for (int g = 0; g < 3; ++g) {
        std::vector<double> m, e;
        for (double v : marks[g]) if (v >= 0) m.push_back(v);
        for (double v : empty[g]) if (v >= 0) e.push_back(v);
        total += m.size() + e.size();  // samples outside the image are dropped, as in sample_fit
        if (m.empty() || e.empty()) continue;
        const double mm = std::accumulate(m.begin(), m.end(), 0.0) / m.size(), em = std::accumulate(e.begin(), e.end(), 0.0) / e.size();
        if ((dark ? em - mm : mm - em) < 0.25 * fit.contrast) continue;
        const double t = (mm + em) * .5;
        const auto marked = [&](double v) { return dark ? v < t : v > t; };
        evidence += std::count_if(m.begin(), m.end(), marked) + std::count_if(e.begin(), e.end(), [&](double v) { return !marked(v); });
    }
    fit.evidence = static_cast<double>(evidence) / static_cast<double>(total);
    return fit;
}

void orientation_homography(const double quad[8], int reverse, int start, double h[8], bool &ok) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    double dst[8];
    for (int i = 0; i < 4; ++i) { const int q = (start + (reverse ? -i : i) + 8) % 4; dst[2 * i] = quad[2 * q]; dst[2 * i + 1] = quad[2 * q + 1]; }
    ok = solve_homography(canonical, dst, h);
}

// Sub-module alignment from the frame stroke's two edges. For points along
// the straight middle of all four sides, compare samples just inside the
// stroke with samples just outside it, at both its outer (0.175) and inner
// (0.825) edge. The sum peaks sharply when every edge is exactly aligned and,
// averaged over 64 points per edge, resolves well below a module even at
// Profile 0's finest pitch. The frame is symmetric, so no orientation search.
// eps_px is the half-separation of each edge's sample pair, in pixels. The
// score is flat within about +/-eps of alignment, so callers shrink eps with
// their search step (coarse-to-fine): wide to capture, sub-pixel to finish.
double frame_edge_score(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8], double eps_px) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    double h[8]; if (!solve_homography(canonical, quad, h)) return 0;
    const double side_px = (std::hypot(quad[2] - quad[0], quad[3] - quad[1]) + std::hypot(quad[6] - quad[0], quad[7] - quad[1])) / 2;
    const double eps = std::clamp(eps_px * 29.0 / std::max(side_px, 1.0), 0.004, 0.3);
    double centre = 0, clear = 0, edges = 0; int n = 0;
    for (int side = 0; side < 4; ++side) for (int i = 0; i < 16; ++i) {
        const double t = 5.0 + 19.0 * i / 15.0;
        // Point on the stroke centreline (inset 0.5) and outward normal.
        double x, y, nx, ny;
        if (side == 0) { x = t; y = 0.5; nx = 0; ny = -1; }
        else if (side == 1) { x = 28.5; y = t; nx = 1; ny = 0; }
        else if (side == 2) { x = 29 - t; y = 28.5; nx = 0; ny = 1; }
        else { x = 0.5; y = 29 - t; nx = -1; ny = 0; }
        const auto at = [&](double d) { return gray_sample(gray, width, height, stride, h, x + nx * d, y + ny * d, 0.0); };
        const double c = at(0), out_in = at(0.325 - eps), out_out = at(0.325 + eps), in_in = at(-0.325 + eps), in_out = at(-0.325 - eps);
        if (c < 0 || out_in < 0 || out_out < 0 || in_in < 0 || in_out < 0) return 0;
        centre += c; clear += (out_out + in_out) / 2; edges += (out_in - out_out) + (in_in - in_out); ++n;
    }
    const double polarity = centre > clear ? 1.0 : -1.0;  // light-on-dark or dark-on-light
    return std::max(0.0, polarity * edges / n);
}

double scalable_skeleton_score(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8]) {
    double best = 0;
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        double h[8]; bool ok; orientation_homography(quad, reverse, start, h, ok); if (!ok) continue;
        const KeyFit fit = scalable_skeleton_fit(gray, width, height, stride, h);
        if (fit.valid) best = std::max(best, fit.contrast * fit.evidence);
    }
    return best;
}

// Combined skeleton + version-key score; the version terms dominate once the
// skeleton is aligned, which is what pins the module pitch.
double scalable_version_score(const ScalableLayout &L, const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8]) {
    double best = 0;
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        double h[8]; bool ok; orientation_homography(quad, reverse, start, h, ok); if (!ok) continue;
        const KeyFit skeleton = scalable_skeleton_fit(gray, width, height, stride, h);
        const KeyFit keys = scalable_version_fit(L, gray, width, height, stride, h);
        if (skeleton.valid && keys.valid) best = std::max(best, skeleton.contrast * skeleton.evidence + 2.0 * keys.contrast * keys.evidence * keys.evidence);
    }
    return best;
}

int best_scalable_version(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8], double &score, int *family = nullptr) {
    int best_v = 0, best_f = 0; score = 0;
    for (int f = 0; f < kScalableFamilies; ++f) for (int v = 1; v <= kScalableVersions; ++v) {
        const double s = scalable_version_score(scalable_family_layout(v, f), gray, width, height, stride, quad);
        if (s > score) { score = s; best_v = v; best_f = f; }
    }
    if (family) *family = best_f;
    return best_v;
}

std::string fixed4(double v) { char b[32]; std::snprintf(b, sizeof(b), "%.4f", v); return b; }

}  // namespace



namespace {

std::string hex_colour(ckey_rgb c) { char b[8]; std::snprintf(b, sizeof(b), "#%02x%02x%02x", c.r, c.g, c.b); return b; }
bool same_colour(ckey_rgb a, ckey_rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// WCAG relative luminance and contrast ratio: the reader decodes luminance,
// so every ink colour must stand clear of every background colour.
double relative_luminance(ckey_rgb c) {
    const auto channel = [](uint8_t v) { const double x = v / 255.0; return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}
double contrast_ratio(ckey_rgb a, ckey_rgb b) {
    const double la = relative_luminance(a), lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

std::string xml_escape(const char *text) {
    std::string out;
    for (const char *c = text; *c; ++c) {
        if (*c == '&') out += "&amp;"; else if (*c == '<') out += "&lt;"; else if (*c == '>') out += "&gt;";
        else if (*c == '"') out += "&quot;"; else out += *c;
    }
    return out;
}

// The empty centre (inside ring 0's ink) holds a logo without hiding marks.
double logo_quiet_radius(const ScalableLayout &L) { return kScalableRingInner - L.dot - 0.15; }
// Keep the outermost ring (which carries the Ring Key) fully visible.
double logo_max_radius(const ScalableLayout &L) { return L.ring_radius(L.rings - 1) - L.dot - 0.25; }

double logo_load(const ScalableLayout &L, int level, double radius) {
    if (radius <= logo_quiet_radius(L)) return 0.0;
    const auto blocks = scalable_blocks(L.codeword_bytes, level);
    std::vector<size_t> block_of(L.codeword_bytes); size_t pos = 0;
    for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) block_of[pos++] = b;
    std::vector<uint8_t> hidden(L.codeword_bytes, 0);
    for (size_t i = 0; i < L.codeword_bytes * 8; ++i) {
        const auto &n = L.nodes[i];
        if (n.kind != 2) continue;  // only ring marks can lie under an allowed logo
        if (std::hypot(n.x - kCenter, n.y - kCenter) - L.dot <= radius + 0.05) hidden[i / 8] = 1;
    }
    std::vector<size_t> per_block(blocks.size(), 0);
    for (size_t byte = 0; byte < hidden.size(); ++byte) per_block[block_of[byte]] += hidden[byte];
    double worst = 0;
    for (size_t b = 0; b < blocks.size(); ++b) worst = std::max(worst, per_block[b] / std::max(1.0, (blocks[b].n - blocks[b].k) / 2.0));
    return worst;
}

// The default style (spec §4: black on white). Shared so that libraries
// other than ckey_render (the .ckey reader in ckey_codec) need not link it.
void fill_default_style(ckey_style &st) {
    std::memset(&st, 0, sizeof(st));
    st.background = st.background_edge = {0xff, 0xff, 0xff};
    st.ink = st.ink_shade = {0, 0, 0};
    st.modules = CKEY_MODULE_ROUNDED; st.rings = CKEY_RING_JOINED; st.rays = CKEY_RAY_ROUND;
    st.policy = CKEY_POLICY_STRICT;
}

// Values a renderer cannot draw at all, whatever the policy.
std::string structural_problem(const ScalableLayout &L, const ckey_style &st) {
    const auto finite01 = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
    // A caller can store any integer in an enum field. Read the raw bytes so an
    // out-of-range value is refused without loading it as the enum type.
    const auto raw = [](const auto &field) {
        using T = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::is_enum_v<T>) {
            std::underlying_type_t<T> v;
            std::memcpy(&v, &field, sizeof v);
            return static_cast<long long>(v);
        } else {
            return static_cast<long long>(field);
        }
    };
    if (raw(st.policy) < CKEY_POLICY_STRICT || raw(st.policy) > CKEY_POLICY_OVERRIDE) return "unknown style policy";
    if (raw(st.modules) < CKEY_MODULE_ROUNDED || raw(st.modules) > CKEY_MODULE_DIAMOND) return "unknown module shape";
    if (raw(st.rings) < CKEY_RING_JOINED || raw(st.rings) > CKEY_RING_DOTS) return "unknown ring style";
    if (raw(st.rays) < CKEY_RAY_ROUND || raw(st.rays) > CKEY_RAY_FLAT) return "unknown ray cap";
    if (st.accent_count > 4 || st.accent_percent > 100) return "accents: at most 4 colours and 100%";
    if (!finite01(st.bevel) || !finite01(st.shadow) || !finite01(st.texture)) return "bevel, shadow, and texture must be 0..1";
    if (!std::isfinite(st.logo_radius) || st.logo_radius < 0) return "logo radius must be 0 or positive";
    if (st.logo_href && *st.logo_href && st.logo_radius > 14.5) return "logo radius must be at most 14.5 units";
    (void)L;
    return {};
}

// Scan-safety rules. Only what reliably breaks a code is a hard refusal:
// near-invisible marks (any ink or accent below kHardContrast against a
// background), mixed polarity, and logos that hide too much. Lower contrast
// and strong depth are warnings: the photo sweep in ckey_certify decides
// whether such a style scans, and it often does.
constexpr double kHardContrast = 1.5;
constexpr double kAdviseContrast = 3.0;

std::string safety_problem(const ScalableLayout &L, const ckey_scalable_symbol &sym, const ckey_style &st,
                           const std::vector<ckey_rgb> &extra_backgrounds, double &min_contrast, double &load,
                           std::string *warnings = nullptr) {
    min_contrast = 1e9; load = 0;
    std::vector<ckey_rgb> inks = {st.ink, st.ink_shade};
    if (st.accent_percent > 0) for (uint32_t i = 0; i < st.accent_count; ++i) inks.push_back(st.accents[i]);
    std::vector<ckey_rgb> backgrounds = {st.background, st.background_edge};
    backgrounds.insert(backgrounds.end(), extra_backgrounds.begin(), extra_backgrounds.end());
    int polarity = 0; std::string problem, advice;
    for (const auto &bg : backgrounds) for (const auto &ink : inks) {
        const double ratio = contrast_ratio(ink, bg);
        min_contrast = std::min(min_contrast, ratio);
        char why[112];
        if (ratio < kHardContrast && problem.empty()) { std::snprintf(why, sizeof(why), "ink %s on background %s is %.1f:1, too faint to read (minimum %.1f:1)", hex_colour(ink).c_str(), hex_colour(bg).c_str(), ratio, kHardContrast); problem = why; }
        else if (ratio < kAdviseContrast && advice.empty()) { std::snprintf(why, sizeof(why), "ink %s on background %s is only %.1f:1", hex_colour(ink).c_str(), hex_colour(bg).c_str(), ratio); advice = why; }
        const int side = relative_luminance(ink) > relative_luminance(bg) ? 1 : -1;
        if (polarity && side != polarity && problem.empty()) problem = "all inks must be lighter than the background, or all darker";
        polarity = side;
    }
    if (polarity < 0 && st.shadow > 0.4 && advice.empty()) advice = "strong shadow on dark-on-light marks";
    if (polarity < 0 && st.bevel > 0.5 && advice.empty()) advice = "strong bevel on dark-on-light marks";
    if (warnings) *warnings = advice;
    if (!problem.empty()) return problem;
    if (st.logo_href && *st.logo_href) {
        const double radius = st.logo_radius > 0 ? st.logo_radius : logo_quiet_radius(L);
        char why[96];
        if (radius > logo_max_radius(L)) { std::snprintf(why, sizeof(why), "logo radius %.2f exceeds %.2f (it would hide the Ring Key)", radius, logo_max_radius(L)); return why; }
        if (radius > logo_quiet_radius(L) && !st.logo_plate) return "a logo larger than the empty centre needs a plate";
        load = logo_load(L, static_cast<int>(sym.ecc_level), radius);
        if (load > 0.5) { std::snprintf(why, sizeof(why), "logo hides %.0f%% of a block's correction (max 50%%); raise the level or shrink it", 100 * load); return why; }
    }
    return {};
}

// Policy-aware check. STRICT and ADAPTIVE styles must pass the safety rules
// (renderers adapt an ADAPTIVE style first, see effective_style); OVERRIDE
// skips them: the caller has explicitly accepted an uncertified symbol.
std::string check_style(const ScalableLayout &L, const ckey_scalable_symbol &sym, const ckey_style &st, double &min_contrast, double &load) {
    min_contrast = 0; load = 0;
    std::string problem = structural_problem(L, st);
    if (!problem.empty()) return problem;
    problem = safety_problem(L, sym, st, {}, min_contrast, load);
    return st.policy == CKEY_POLICY_OVERRIDE ? std::string() : problem;
}

// ---- Adaptive policy: rule-based repairs, each reported in a bitmask ----

ckey_rgb best_ink_for(const std::vector<ckey_rgb> &backgrounds) {
    double black = 1e9, white = 1e9;
    for (const auto &bg : backgrounds) { black = std::min(black, contrast_ratio({0, 0, 0}, bg)); white = std::min(white, contrast_ratio({255, 255, 255}, bg)); }
    return black >= white ? ckey_rgb{0, 0, 0} : ckey_rgb{255, 255, 255};
}

uint32_t adapt_style(const ScalableLayout &L, const ckey_scalable_symbol &sym, ckey_style &st, const std::vector<ckey_rgb> &extra_backgrounds) {
    uint32_t changed = 0;
    std::vector<ckey_rgb> backgrounds = {st.background, st.background_edge};
    backgrounds.insert(backgrounds.end(), extra_backgrounds.begin(), extra_backgrounds.end());
    const auto ink_ok = [&](ckey_rgb ink, int &side) {
        for (const auto &bg : backgrounds) {
            if (contrast_ratio(ink, bg) < 3.0) return false;
            const int s = relative_luminance(ink) > relative_luminance(bg) ? 1 : -1;
            if (side && s != side) return false;
            side = s;
        }
        return true;
    };
    int side = 0;
    if (!ink_ok(st.ink, side) || !ink_ok(st.ink_shade, side)) {
        st.ink = st.ink_shade = best_ink_for(backgrounds);
        changed |= CKEY_ADJUST_INK; side = 0;
        if (!ink_ok(st.ink, side)) {  // no ink can work on these backgrounds: give the symbol its own ground
            st.background = st.background_edge = st.ink.r ? ckey_rgb{0, 0, 0} : ckey_rgb{255, 255, 255};
            backgrounds = {st.background};
            changed |= CKEY_ADJUST_BACKGROUND; side = 0; ink_ok(st.ink, side);
        }
    }
    if (st.accent_percent > 0 && st.accent_count > 0) {
        int s = side; bool ok = true;
        for (uint32_t i = 0; i < st.accent_count; ++i) { int t = s; ok = ok && ink_ok(st.accents[i], t); }
        if (!ok) { st.accent_percent = 0; st.accent_count = 0; changed |= CKEY_ADJUST_ACCENTS; }
    }
    if (side < 0 && (st.shadow > 0.4 || st.bevel > 0.5)) { st.shadow = std::min(st.shadow, 0.4); st.bevel = std::min(st.bevel, 0.5); changed |= CKEY_ADJUST_EFFECTS; }
    if (st.logo_href && *st.logo_href) {
        double radius = st.logo_radius > 0 ? st.logo_radius : logo_quiet_radius(L);
        if (radius > logo_quiet_radius(L) && !st.logo_plate) { st.logo_plate = 1; changed |= CKEY_ADJUST_LOGO; }
        const double original = radius;
        radius = std::min(radius, logo_max_radius(L));
        while (radius > logo_quiet_radius(L) && logo_load(L, static_cast<int>(sym.ecc_level), radius) > 0.5) radius -= 0.05;
        radius = std::max(radius, logo_quiet_radius(L));
        if (radius < original - 1e-9) { st.logo_radius = radius; changed |= CKEY_ADJUST_LOGO; }
    }
    return changed;
}

// The style a renderer draws: an ADAPTIVE style after its rule-based repairs.
ckey_style effective_style(const ScalableLayout &L, const ckey_scalable_symbol &sym, const ckey_style &st) {
    ckey_style out = st;
    double contrast = 0, load = 0;
    if (st.policy == CKEY_POLICY_ADAPTIVE && structural_problem(L, st).empty() && !safety_problem(L, sym, st, {}, contrast, load).empty())
        adapt_style(L, sym, out, {});
    return out;
}

void emit_module(std::ostringstream &o, double cx, double cy, double p, ckey_module_shape shape, const std::string &colour) {
    const std::string fill = colour.empty() ? "" : " fill=\"" + colour + "\"";
    const double h = 0.34 * p;
    switch (shape) {
    case CKEY_MODULE_CIRCLE: o << "<circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << h << "\" stroke=\"none\"" << fill << "/>"; break;
    case CKEY_MODULE_DIAMOND: {
        const double d = 0.40 * p;  // inscribed radius 0.28p covers the sample support
        o << "<polygon points=\"" << cx << ',' << cy - d << ' ' << cx + d << ',' << cy << ' ' << cx << ',' << cy + d << ' ' << cx - d << ',' << cy << "\" stroke=\"none\"" << fill << "/>"; break;
    }
    default:
        o << "<rect x=\"" << cx - h << "\" y=\"" << cy - h << "\" width=\"" << 2 * h << "\" height=\"" << 2 * h << "\" rx=\""
          << (shape == CKEY_MODULE_SQUARE ? 0.0 : 0.16 * p) << "\" stroke=\"none\"" << fill << "/>";
    }
}

// Deterministic accent choice per site (decorative; luminance is checked).
int accent_of(uint32_t id, const ckey_style &st) {
    if (!st.accent_count || !st.accent_percent) return -1;
    const uint32_t h = site_hash(id ^ 0x41434345u);  // "ACCE"
    return (h % 100) < st.accent_percent ? static_cast<int>((h / 100) % st.accent_count) : -1;
}

}  // namespace




// ---------------------------------------------------------------------------
// Full-frame localization (moved from the iPhone reader so it is shared and
// testable). Border candidates are connected components of the continuous
// frame stroke; each is fitted with a perspective quadrilateral, then refined
// against a profile's fixed keys. Neither step can accept a payload.
// ---------------------------------------------------------------------------
namespace {

struct Point2 { double x, y; };

double cross(const Point2 &o, const Point2 &a, const Point2 &b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

std::vector<Point2> convex_hull(std::vector<Point2> p) {
    std::sort(p.begin(), p.end(), [](const Point2 &a, const Point2 &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    p.erase(std::unique(p.begin(), p.end(), [](const Point2 &a, const Point2 &b) { return a.x == b.x && a.y == b.y; }), p.end());
    if (p.size() < 3) return p;
    std::vector<Point2> h(2 * p.size()); size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i) { while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    for (size_t i = p.size() - 1, t = k + 1; i-- > 0;) { while (k >= t && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    h.resize(k - 1);
    return h;
}

// Fits the outer edge of a square border component. Second moments cannot
// orient a square (its inertia is isotropic, so the old principal-axis fit
// returned an essentially random angle, often 45 degrees off). The minimum-
// area enclosing rectangle of the convex hull gives the orientation; each
// side is then re-fitted as a line through the stroke's outer edge, so a
// tilted (keystoned) display yields a true perspective quadrilateral.
bool fit_border_quad(const std::vector<Point2> &points, double step, std::array<double, 8> &quad) {
    const auto hull = convex_hull(points);
    if (hull.size() < 4) return false;
    double best_area = std::numeric_limits<double>::infinity(), ux = 1, uy = 0;
    for (size_t i = 0; i < hull.size(); ++i) {
        const Point2 &a = hull[i], &b = hull[(i + 1) % hull.size()];
        double ex = b.x - a.x, ey = b.y - a.y; const double len = std::hypot(ex, ey);
        if (len < 1e-9) continue;
        ex /= len; ey /= len;
        double min_u = 1e300, max_u = -1e300, min_v = 1e300, max_v = -1e300;
        for (const auto &p : hull) {
            const double u = p.x * ex + p.y * ey, v = -p.x * ey + p.y * ex;
            min_u = std::min(min_u, u); max_u = std::max(max_u, u); min_v = std::min(min_v, v); max_v = std::max(max_v, v);
        }
        const double area = (max_u - min_u) * (max_v - min_v);
        if (area < best_area) { best_area = area; ux = ex; uy = ey; }
    }
    const double vx = -uy, vy = ux;
    double cx = 0, cy = 0; for (const auto &p : points) { cx += p.x; cy += p.y; } cx /= points.size(); cy /= points.size();
    double min_u = 1e300, max_u = -1e300, min_v = 1e300, max_v = -1e300;
    for (const auto &p : points) {
        const double u = (p.x - cx) * ux + (p.y - cy) * uy, v = (p.x - cx) * vx + (p.y - cy) * vy;
        min_u = std::min(min_u, u); max_u = std::max(max_u, u); min_v = std::min(min_v, v); max_v = std::max(max_v, v);
    }
    const double half_u = (max_u - min_u) / 2, half_v = (max_v - min_v) / 2, mid_u = (max_u + min_u) / 2, mid_v = (max_v + min_v) / 2;
    // Side lines: normal n (outward), offset d, so that n . p = d on the edge.
    struct Line { double nx, ny, d; };
    std::array<Line, 4> lines{};
    const std::array<std::array<double, 2>, 4> normals = {{{-vx, -vy}, {ux, uy}, {vx, vy}, {-ux, -uy}}};  // top, right, bottom, left
    for (int s = 0; s < 4; ++s) {
        const double nx = normals[s][0], ny = normals[s][1];
        const bool along_u = s == 0 || s == 2;
        const double half_along = along_u ? half_u : half_v, half_across = along_u ? half_v : half_u;
        const double band = std::max(3.0 * step, 0.10 * half_across);
        std::vector<Point2> side;
        for (const auto &p : points) {
            const double u = (p.x - cx) * ux + (p.y - cy) * uy - mid_u, v = (p.x - cx) * vx + (p.y - cy) * vy - mid_v;
            const double across = along_u ? (s == 0 ? -v : v) : (s == 1 ? u : -u);
            const double along = along_u ? u : v;
            // Skip the rounded corners: only the straight middle of each side.
            if (across > half_across - band && std::abs(along) < 0.62 * half_along) side.push_back(p);
        }
        Line line{nx, ny, 0};
        if (side.size() >= 8) {
            double mx = 0, my = 0; for (const auto &p : side) { mx += p.x; my += p.y; } mx /= side.size(); my /= side.size();
            double sxx = 0, syy = 0, sxy = 0;
            for (const auto &p : side) { sxx += (p.x - mx) * (p.x - mx); syy += (p.y - my) * (p.y - my); sxy += (p.x - mx) * (p.y - my); }
            const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);   // direction of the side
            double lx = -std::sin(theta), ly = std::cos(theta);           // its normal
            if (lx * nx + ly * ny < 0) { lx = -lx; ly = -ly; }
            if (lx * nx + ly * ny > 0.94) { line.nx = lx; line.ny = ly; }  // accept tilt up to ~20 degrees
        }
        // Outer edge: a high percentile of the outward coordinate of the side's
        // points (the whole stroke lies in the band), plus half a sample.
        std::vector<double> outward;
        for (const auto &p : side.size() >= 8 ? side : points) outward.push_back(p.x * line.nx + p.y * line.ny);
        std::sort(outward.begin(), outward.end());
        line.d = outward[static_cast<size_t>(0.95 * (outward.size() - 1))] + 0.5 * step;
        lines[s] = line;
    }
    for (int c = 0; c < 4; ++c) {  // corners: top-left, top-right, bottom-right, bottom-left
        const Line &a = lines[c == 0 ? 3 : c - 1], &b = lines[c];
        const double det = a.nx * b.ny - a.ny * b.nx;
        if (std::abs(det) < 1e-9) return false;
        quad[2 * c] = (a.d * b.ny - a.ny * b.d) / det;
        quad[2 * c + 1] = (a.nx * b.d - a.d * b.nx) / det;
    }
    return std::all_of(quad.begin(), quad.end(), [](double v) { return std::isfinite(v); });
}

// Candidate ordering only (generated by tools/train_locator_ranker.py).
double border_rank(double aspect_error, double fill_ratio, double relative_side) {
    const double z = 0.81059192 - 10.65985208 * aspect_error + 0.18755525 * fill_ratio - 0.55068352 * std::abs(relative_side - 0.32);
    return 1.0 / (1.0 + std::exp(-z));
}

// The outer ink edge of the frame stroke sits at canonical 0.175..28.825 in
// Profiles 1/2 and 0.7..115.3 in Profile 3: both 1.0122x inside the field.
constexpr double kBorderToField = 29.0 / 28.65;


}  // namespace
