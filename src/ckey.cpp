#include "ckey/ckey.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
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
    const double dx = x - g.polar_center, dy = y - g.polar_center;
    m = std::min(m, std::sqrt(dx * dx + dy * dy) - g.ray_ink_outer - e - g.polar_clear);
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

bool solve_homography(const double src[8], const double dst[8], double h[8]) {
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

bool map_point(const double h[8], double x, double y, ImagePoint &p) {
    const double d=h[6]*x+h[7]*y+1.0;
    if (!std::isfinite(d) || std::abs(d)<1e-12) return false;
    p={(h[0]*x+h[1]*y+h[2])/d,(h[3]*x+h[4]*y+h[5])/d};
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
constexpr double kModuleSpread = 0.06;   // 0.34-unit dense Cartesian modules
constexpr double kDotSpread = 0.025;     // 0.15-0.20-unit ring dots and arcs
constexpr double kRaySpread = 0.02;      // 0.10-0.16-unit radial strokes
// A point inside the P3 Grid Key clearance: guaranteed unprinted.
constexpr double kExtendedGridClearX = 7.75, kExtendedGridClearY = 45.5;

double gray_sample(const uint8_t *gray, size_t width, size_t height, size_t stride,
                   const double h[8], double x, double y, double spread = kLocatorSampleSpread) {
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
void refine_ring_context(const std::vector<int> &grid, int rings, int slots, size_t data_sites,
                         const std::vector<uint8_t> &white, const std::vector<double> &level,
                         double class_cut, std::vector<uint8_t> &printed) {
    constexpr double kMinSeparation = 0.2;
    const auto value_at = [&](int ring, int slot) -> int {
        const int index = grid[static_cast<size_t>(ring) * slots + static_cast<size_t>((slot + slots) % slots)];
        return index < 0 ? 0 : printed[static_cast<size_t>(index)];
    };
    for (int pass = 0; pass < 2; ++pass) {
        std::array<std::vector<double>, 3> by_context;
        std::vector<std::pair<size_t, int>> sites;
        for (int ring = 0; ring < rings; ++ring) for (int slot = 0; slot < slots; ++slot) {
            const int index = grid[static_cast<size_t>(ring) * slots + slot];
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

extern "C" {
uint32_t ckey_get_abi_version(void) { return CKEY_ABI_VERSION; }
const char *ckey_get_software_version(void) { return CKEY_SOFTWARE_VERSION; }

ckey_status ckey_encode_locator(const uint8_t *payload, size_t payload_len,
                                ckey_symbol *out, ckey_diagnostics *diag) {
    if (!payload || !out) { set_status(diag, CKEY_INVALID_ARGUMENT, "payload and output are required"); return CKEY_INVALID_ARGUMENT; }
    if (payload_len != CKEY_LOCATOR_PAYLOAD_BYTES) { set_status(diag, CKEY_INVALID_ARGUMENT, "locator payload must be exactly 16 bytes"); return CKEY_INVALID_ARGUMENT; }

    std::array<uint8_t, kCodewordBytes> cw{};
    // Frame: magic C7 4B, current format, profile 01, length u16be, 16 payload
    // bytes, CRC-32C over the first 22 bytes, followed by 51 RS parity bytes.
    cw[0] = 0xc7; cw[1] = 0x4b; cw[2] = CKEY_FORMAT_VERSION; cw[3] = 1; cw[4] = 0; cw[5] = 16;
    std::memcpy(cw.data() + 6, payload, 16);
    const uint32_t crc = crc32c(cw.data(), 22);
    cw[22] = static_cast<uint8_t>(crc >> 24); cw[23] = static_cast<uint8_t>(crc >> 16);
    cw[24] = static_cast<uint8_t>(crc >> 8); cw[25] = static_cast<uint8_t>(crc);
    const auto gen = rs_generator();
    std::array<uint8_t, kCodewordBytes> work = cw;
    for (size_t i = 0; i < kDataBytes; ++i) {
        const uint8_t factor = work[i];
        for (size_t j = 0; j <= kParityBytes; ++j) work[i + j] ^= gf.mul(factor, gen[j]);
    }
    std::memcpy(cw.data() + kDataBytes, work.data() + kDataBytes, kParityBytes);
    std::memset(out, 0, sizeof(*out)); out->abi_version = CKEY_ABI_VERSION; out->side = kSide;
    std::memcpy(out->codeword, cw.data(), cw.size());
    set_status(diag, CKEY_OK, "encoded Profile 1 locator symbol");
    return CKEY_OK;
}

ckey_status ckey_decode_locator(const uint8_t *codeword, size_t codeword_len,
                                uint8_t *payload, size_t out_capacity,
                                ckey_diagnostics *diag) {
    if (!codeword || !payload) { set_status(diag, CKEY_INVALID_ARGUMENT, "codeword and payload output are required"); return CKEY_INVALID_ARGUMENT; }
    if (codeword_len != kCodewordBytes || out_capacity < CKEY_LOCATOR_PAYLOAD_BYTES) { set_status(diag, CKEY_INVALID_ARGUMENT, "locator decode requires 77 bytes and a 16-byte output buffer"); return CKEY_INVALID_ARGUMENT; }
    std::array<uint8_t,kCodewordBytes> cw{}; std::memcpy(cw.data(), codeword, cw.size());
    uint32_t corrected = 0;
    if (!rs_correct(cw, corrected)) { set_status(diag, CKEY_UNCORRECTABLE, "Reed-Solomon correction failed"); return CKEY_UNCORRECTABLE; }
    // Formats 8 and 11--13 remain readable for historic camera fixtures.
    // Encoder output is the current format; the newer inset-finder geometry
    // is unambiguously declared by format 11.
    if (cw[0] != 0xc7 || cw[1] != 0x4b || (cw[2] != 8u && cw[2] != 11u && cw[2] != 12u && cw[2] != 13u && cw[2] != CKEY_FORMAT_VERSION) || cw[3] != 1 || cw[4] != 0 || cw[5] != 16) {
        set_status(diag, CKEY_INTEGRITY_FAILURE, "corrected frame has invalid magic, version, profile, or length"); return CKEY_INTEGRITY_FAILURE;
    }
    const uint32_t expected = (static_cast<uint32_t>(cw[22]) << 24) | (static_cast<uint32_t>(cw[23]) << 16) | (static_cast<uint32_t>(cw[24]) << 8) | cw[25];
    if (crc32c(cw.data(), 22) != expected) { set_status(diag, CKEY_INTEGRITY_FAILURE, "CRC-32C validation failed"); return CKEY_INTEGRITY_FAILURE; }
    std::memcpy(payload, cw.data() + 6, CKEY_LOCATOR_PAYLOAD_BYTES);
    set_status(diag, CKEY_OK, corrected ? "decoded locator; Reed-Solomon symbols corrected" : "decoded locator; no correction needed");
    if (diag) diag->corrected_symbols = corrected;
    return CKEY_OK;
}

ckey_status ckey_encode_text(const uint8_t *payload, size_t payload_len,
                              ckey_text_symbol *out, ckey_diagnostics *diag) {
    if (!payload || !out) { set_status(diag, CKEY_INVALID_ARGUMENT, "text payload and output are required"); return CKEY_INVALID_ARGUMENT; }
    if (payload_len > CKEY_TEXT_PAYLOAD_MAX_BYTES) { set_status(diag, CKEY_INVALID_ARGUMENT, "text profile payload exceeds 200 bytes"); return CKEY_INVALID_ARGUMENT; }
    std::memset(out, 0, sizeof(*out));
    out->abi_version = CKEY_ABI_VERSION; out->side = kSide; out->payload_length = static_cast<uint32_t>(payload_len);
    // Frame: magic, current format, profile 2, u16be payload length, payload,
    // CRC-32C, then deterministic SplitMix32 visual fill. All 210 data bytes
    // remain protected by the full RS(255,210) code.
    std::vector<uint8_t> codeword(kTextCodewordBytes, 0);
    codeword[0] = 0xc7; codeword[1] = 0x4b; codeword[2] = CKEY_TEXT_FORMAT_VERSION; codeword[3] = 2;
    codeword[4] = static_cast<uint8_t>(payload_len >> 8); codeword[5] = static_cast<uint8_t>(payload_len);
    std::memcpy(codeword.data() + 6, payload, payload_len);
    const uint32_t crc = crc32c(codeword.data(), 6 + payload_len);
    const size_t crc_index = 6 + payload_len;
    codeword[crc_index] = static_cast<uint8_t>(crc >> 24); codeword[crc_index + 1] = static_cast<uint8_t>(crc >> 16);
    codeword[crc_index + 2] = static_cast<uint8_t>(crc >> 8); codeword[crc_index + 3] = static_cast<uint8_t>(crc);
    fill_padding(codeword.data(), crc_index + 4, kTextDataBytes, crc_index + 4);
    rs_encode_generic(codeword, kTextDataBytes, kTextParityBytes);
    std::memcpy(out->codeword, codeword.data(), codeword.size());
    set_status(diag, CKEY_OK, "encoded Profile 2 text symbol");
    return CKEY_OK;
}

ckey_status ckey_decode_text(const uint8_t *codeword, size_t codeword_len,
                              uint8_t *payload, size_t out_capacity, size_t *out_payload_len,
                              ckey_diagnostics *diag) {
    if (!codeword || !payload || !out_payload_len || codeword_len != kTextCodewordBytes) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "text decode requires a 255-byte codeword and output length"); return CKEY_INVALID_ARGUMENT;
    }
    std::vector<uint8_t> corrected(codeword, codeword + codeword_len);
    uint32_t corrections = 0;
    if (!rs_correct_generic(corrected, kTextParityBytes, corrections)) { set_status(diag, CKEY_UNCORRECTABLE, "text Reed-Solomon correction failed"); return CKEY_UNCORRECTABLE; }
    if (corrected[0] != 0xc7 || corrected[1] != 0x4b ||
        !is_text_format(corrected[2]) || corrected[3] != 2) {
        set_status(diag, CKEY_INTEGRITY_FAILURE, "text frame has invalid magic, format, or profile"); return CKEY_INTEGRITY_FAILURE;
    }
    const size_t length = (static_cast<size_t>(corrected[4]) << 8) | corrected[5];
    if (length > CKEY_TEXT_PAYLOAD_MAX_BYTES || 6 + length + 4 > kTextDataBytes) {
        set_status(diag, CKEY_INTEGRITY_FAILURE, "text frame declares a length beyond the profile"); return CKEY_INTEGRITY_FAILURE;
    }
    if (out_capacity < length) {
        set_status(diag, CKEY_INSUFFICIENT_BUFFER, "text payload exceeds output capacity"); return CKEY_INSUFFICIENT_BUFFER;
    }
    const size_t crc_index = 6 + length;
    const uint32_t expected = (static_cast<uint32_t>(corrected[crc_index]) << 24) |
                              (static_cast<uint32_t>(corrected[crc_index + 1]) << 16) |
                              (static_cast<uint32_t>(corrected[crc_index + 2]) << 8) | corrected[crc_index + 3];
    if (crc32c(corrected.data(), crc_index) != expected) { set_status(diag, CKEY_INTEGRITY_FAILURE, "text CRC-32C validation failed"); return CKEY_INTEGRITY_FAILURE; }
    // Formats 0.23 and later are checked against the normative SplitMix32 fill.
    if ((corrected[2] == 23u || corrected[2] == CKEY_TEXT_FORMAT_VERSION) &&
        !padding_matches(corrected.data(), crc_index + 4, kTextDataBytes, crc_index + 4)) {
        set_status(diag, CKEY_INTEGRITY_FAILURE, "text visual-fill validation failed"); return CKEY_INTEGRITY_FAILURE;
    }
    std::memcpy(payload, corrected.data() + 6, length); *out_payload_len = length;
    set_status(diag, CKEY_OK, corrections ? "decoded text profile; Reed-Solomon symbols corrected" : "decoded text profile; no correction needed");
    if (diag) diag->corrected_symbols = corrections;
    return CKEY_OK;
}

ckey_status ckey_encode_extended(const uint8_t *payload, size_t payload_len,
                                 ckey_extended_symbol *out, ckey_diagnostics *diag) {
    return ckey_encode_extended_tiered(payload, payload_len, CKEY_EXTENDED_TIER_AUTO, out, diag);
}

ckey_status ckey_encode_extended_tiered(const uint8_t *payload, size_t payload_len,
                                        ckey_extended_tier requested_tier,
                                        ckey_extended_symbol *out, ckey_diagnostics *diag) {
    if (!payload || !out) { set_status(diag, CKEY_INVALID_ARGUMENT, "extended payload and output are required"); return CKEY_INVALID_ARGUMENT; }
    if (payload_len == 0 || payload_len > CKEY_EXTENDED_PAYLOAD_MAX_BYTES) { set_status(diag, CKEY_INVALID_ARGUMENT, "extended profile payload must contain 1 to 2048 bytes"); return CKEY_INVALID_ARGUMENT; }
    const size_t tier_index = extended_tier_index(requested_tier, payload_len);
    if (tier_index == kExtendedTierCapacities.size() || payload_len > kExtendedTierCapacities[tier_index]) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "payload does not fit the selected extended capacity tier"); return CKEY_INVALID_ARGUMENT;
    }
    const size_t data_bytes = (11 + kExtendedTierCapacities[tier_index] + kExtendedBlocks - 1) / kExtendedBlocks;
    const size_t parity_bytes = kExtendedBlockBytes - data_bytes;
    std::vector<uint8_t> framed(kExtendedBlocks * data_bytes, 0);
    framed[0]=0xc7; framed[1]=0x4b; framed[2]=CKEY_EXTENDED_FORMAT_VERSION; framed[3]=3;
    framed[4]=static_cast<uint8_t>(tier_index + 1);
    framed[5]=static_cast<uint8_t>(payload_len >> 8); framed[6]=static_cast<uint8_t>(payload_len);
    std::memcpy(framed.data()+7,payload,payload_len);
    const size_t crc_index=7+payload_len; const uint32_t crc=crc32c(framed.data(),crc_index);
    framed[crc_index]=static_cast<uint8_t>(crc>>24); framed[crc_index+1]=static_cast<uint8_t>(crc>>16);
    framed[crc_index+2]=static_cast<uint8_t>(crc>>8); framed[crc_index+3]=static_cast<uint8_t>(crc);
    fill_padding(framed.data(), crc_index + 4, framed.size(), crc_index + 4);
    std::array<std::array<uint8_t,kExtendedBlockBytes>,kExtendedBlocks> blocks{};
    for (size_t block=0;block<kExtendedBlocks;++block) {
        std::copy_n(framed.data()+block*data_bytes,data_bytes,blocks[block].begin());
        std::vector<uint8_t> work(blocks[block].begin(),blocks[block].end());
        rs_encode_generic(work,data_bytes,parity_bytes);
        std::copy(work.begin(),work.end(),blocks[block].begin());
    }
    std::memset(out,0,sizeof(*out)); out->abi_version=CKEY_ABI_VERSION; out->side=kExtendedSide; out->payload_length=static_cast<uint32_t>(payload_len);
    // Byte interleaving maps local optical damage across all independent blocks.
    for (size_t byte=0;byte<kExtendedBlockBytes;++byte) for (size_t block=0;block<kExtendedBlocks;++block)
        out->codeword[byte*kExtendedBlocks+block]=blocks[block][byte];
    char message[96]; std::snprintf(message, sizeof(message), "encoded profile 3 tier %zu; RS(255,%zu) x 10", kExtendedTierCapacities[tier_index], data_bytes);
    set_status(diag,CKEY_OK,message); return CKEY_OK;
}

ckey_status ckey_decode_extended(const uint8_t *codeword, size_t codeword_len,
                                 uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len,
                                 ckey_diagnostics *diag) {
    if (!codeword || !out_payload || !out_payload_len || codeword_len != kExtendedCodewordBytes) { set_status(diag,CKEY_INVALID_ARGUMENT,"extended decode requires a 2550-byte codeword and output length"); return CKEY_INVALID_ARGUMENT; }
    const auto attempt = [&](size_t data_bytes, size_t parity_bytes, bool tiered) -> ckey_status {
        std::vector<uint8_t> framed(kExtendedBlocks * data_bytes, 0);
        uint32_t corrections = 0;
        for (size_t block=0; block<kExtendedBlocks; ++block) {
            std::vector<uint8_t> work(kExtendedBlockBytes);
            for (size_t byte=0; byte<kExtendedBlockBytes; ++byte)
                work[byte] = codeword[byte*kExtendedBlocks+block];
            uint32_t fixed=0;
            if (!rs_correct_generic(work, parity_bytes, fixed)) return CKEY_UNCORRECTABLE;
            corrections += fixed;
            std::copy_n(work.begin(), data_bytes, framed.data()+block*data_bytes);
        }
        if (framed[0]!=0xc7 || framed[1]!=0x4b || framed[3]!=3) return CKEY_INTEGRITY_FAILURE;
        size_t length=0, payload_offset=0, crc_index=0;
        if (tiered) {
            if (!is_tiered_extended_format(framed[2]) || framed[4] == 0 || framed[4] > kExtendedTierCapacities.size()) return CKEY_INTEGRITY_FAILURE;
            const size_t selected = framed[4]-1;
            if (data_bytes != (11+kExtendedTierCapacities[selected]+kExtendedBlocks-1)/kExtendedBlocks) return CKEY_INTEGRITY_FAILURE;
            length=(static_cast<size_t>(framed[5])<<8)|framed[6]; payload_offset=7; crc_index=payload_offset+length;
            if (length==0 || length>kExtendedTierCapacities[selected] || crc_index+4>framed.size()) return CKEY_INTEGRITY_FAILURE;
        } else {
            if (framed[2]!=10u && framed[2]!=16u) return CKEY_INTEGRITY_FAILURE;
            length=(static_cast<size_t>(framed[4])<<8)|framed[5]; payload_offset=6; crc_index=payload_offset+length;
            if (length==0 || length>CKEY_EXTENDED_PAYLOAD_MAX_BYTES || crc_index+4>framed.size()) return CKEY_INTEGRITY_FAILURE;
        }
        const uint32_t expected=(static_cast<uint32_t>(framed[crc_index])<<24)|(static_cast<uint32_t>(framed[crc_index+1])<<16)|(static_cast<uint32_t>(framed[crc_index+2])<<8)|framed[crc_index+3];
        if (crc32c(framed.data(),crc_index)!=expected) return CKEY_INTEGRITY_FAILURE;
        if (tiered && !padding_matches(framed.data(),crc_index+4,framed.size(),crc_index+4)) return CKEY_INTEGRITY_FAILURE;
        if (out_capacity<length) {
            set_status(diag,CKEY_INSUFFICIENT_BUFFER,"extended output buffer is smaller than decoded payload");
            return CKEY_INSUFFICIENT_BUFFER;
        }
        std::memcpy(out_payload,framed.data()+payload_offset,length); *out_payload_len=length;
        set_status(diag,CKEY_OK,corrections ? "decoded extended profile; Reed-Solomon symbols corrected" : "decoded extended profile; no correction needed");
        if (diag) diag->corrected_symbols=corrections;
        return CKEY_OK;
    };

    // Preserve the original Profile 3 RS(255,223) frame reader. The format and
    // profile bytes are the third and fourth data bytes in block zero. Checking
    // them before trying every new parity split avoids doing eight full RS
    // decodes for an unrelated square candidate in the live camera path.
    const bool tier_hint_valid = codeword[40] > 0 && codeword[40] <= kExtendedTierCapacities.size();
    const bool likely_tiered = is_tiered_extended_format(codeword[20]) || (codeword[30]==3 && tier_hint_valid);
    if (!is_tiered_extended_format(codeword[20])) {
        const ckey_status legacy=attempt(kExtendedDataBytes,kExtendedParityBytes,false);
        if (legacy==CKEY_OK || legacy==CKEY_INSUFFICIENT_BUFFER) return legacy;
    }
    if (!likely_tiered) { set_status(diag,CKEY_UNCORRECTABLE,"sampled frame does not identify a supported Profile 3 format"); return CKEY_UNCORRECTABLE; }

    // New tiers share the same 2,550-byte optical codeword. The selected tier
    // changes data/parity split per block, trading capacity for ECC while
    // keeping the full visual lattice populated. Try the indicated tier first.
    std::array<size_t, CKEY_EXTENDED_TIER_COUNT> order{};
    for(size_t i=0;i<order.size();++i) order[i]=i;
    const size_t hinted = codeword[40] > 0 && codeword[40] <= order.size() ? codeword[40]-1 : order.size();
    if(hinted<order.size()) { order[0]=hinted; size_t next=1; for(size_t i=0;i<order.size();++i) if(i!=hinted) order[next++]=i; }
    for(size_t tier:order) {
        const size_t data_bytes=(11+kExtendedTierCapacities[tier]+kExtendedBlocks-1)/kExtendedBlocks;
        const ckey_status result=attempt(data_bytes,kExtendedBlockBytes-data_bytes,true);
        if(result==CKEY_OK || result==CKEY_INSUFFICIENT_BUFFER) return result;
    }
    set_status(diag,CKEY_UNCORRECTABLE,"no Profile 3 capacity tier passed Reed-Solomon, frame, padding, and CRC checks"); return CKEY_UNCORRECTABLE;
}

double ckey_calibration_score_gray8(const uint8_t *gray, size_t width, size_t height,
                                    size_t stride, const double quad_xy[8]) {
    if (!valid_image_args(gray, width, height, stride, quad_xy)) return -1.0;

    constexpr double canonical[8] = {0,0,29,0,29,29,0,29};
    double best = 0.0;
    // Key polarity and orientation are intentionally unknown to localization.
    // Score every cyclic winding exactly as the decoder does, but retain a
    // smooth contrast/evidence measure rather than requiring a valid payload.
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        double dst[8];
        for (int i = 0; i < 4; ++i) {
            const int qi = (start + (reverse ? -i : i) + 8) % 4;
            dst[2 * i] = quad_xy[2 * qi]; dst[2 * i + 1] = quad_xy[2 * qi + 1];
        }
        double h[8]; if (!solve_homography(canonical, dst, h)) continue;
        std::vector<double> grid, ring, ray, finder, empty;
        for (int r : {11,12}) for (int c : {2,3}) grid.push_back(gray_sample(gray,width,height,stride,h,c+.5,r+.5));
        for (int slot : {18,19,21}) ring.push_back(polar_sample(gray,width,height,stride,h,6.0,slot));
        ring.push_back(polar_sample(gray,width,height,stride,h,6.0,23.5));
        ray.push_back(polar_sample(gray,width,height,stride,h,8.8,0));
        ray.push_back(polar_sample(gray,width,height,stride,h,8.4,54));
        ray.push_back(polar_sample(gray,width,height,stride,h,8.8,55));
        // The one-arc rounded-corner finder is localization evidence. It is
        // not an acceptance shortcut: keys, RS, and CRC remain authoritative.
        constexpr double finder_inset=4.00;
        const double diagonal=kTau/8.0;
        for (double finder_radius : {2.30}) {
            finder.push_back(gray_sample(gray,width,height,stride,h,29.0-(finder_inset+finder_radius*std::cos(diagonal)),finder_inset+finder_radius*std::sin(diagonal)));
            finder.push_back(gray_sample(gray,width,height,stride,h,29.0-(finder_inset+finder_radius*std::cos(diagonal)),29.0-(finder_inset+finder_radius*std::sin(diagonal))));
            finder.push_back(gray_sample(gray,width,height,stride,h,finder_inset+finder_radius*std::cos(diagonal),29.0-(finder_inset+finder_radius*std::sin(diagonal))));
            const double first=kTau*18.0/360.0, second=kTau*72.0/360.0;
            finder.push_back(gray_sample(gray,width,height,stride,h,finder_inset+finder_radius*std::cos(first),finder_inset+finder_radius*std::sin(first)));
            finder.push_back(gray_sample(gray,width,height,stride,h,finder_inset+finder_radius*std::cos(second),finder_inset+finder_radius*std::sin(second)));
            empty.push_back(gray_sample(gray,width,height,stride,h,finder_inset+finder_radius*std::cos(diagonal),finder_inset+finder_radius*std::sin(diagonal)));
        }
        empty.push_back(polar_sample(gray,width,height,stride,h,kLocatorApertureRadius,36));
        empty.push_back(polar_sample(gray,width,height,stride,h,6.0,20));
        empty.push_back(polar_sample(gray,width,height,stride,h,8.8,54));
        std::vector<double> marks = grid;
        marks.insert(marks.end(), ring.begin(), ring.end());
        marks.insert(marks.end(), ray.begin(), ray.end());
        marks.insert(marks.end(), finder.begin(), finder.end());
        if (std::any_of(marks.begin(),marks.end(),[](double v){ return v < 0; }) ||
            std::any_of(empty.begin(),empty.end(),[](double v){ return v < 0; })) continue;
        const double mark_mean = std::accumulate(marks.begin(),marks.end(),0.0) / marks.size();
        const double empty_mean = std::accumulate(empty.begin(),empty.end(),0.0) / empty.size();
        const double contrast = std::abs(mark_mean - empty_mean);
        const bool dark_marks = mark_mean < empty_mean;
        const double threshold = (mark_mean + empty_mean) * .5;
        const auto marked = [&](double v) { return dark_marks ? v < threshold : v > threshold; };
        const size_t evidence = std::count_if(marks.begin(),marks.end(),marked) +
                                std::count_if(empty.begin(),empty.end(),[&](double v){ return !marked(v); });
        // Contrast supplies a continuous hill for sub-pixel geometry search;
        // expected key evidence prevents a bright non-CKey rectangle winning.
        best = std::max(best, contrast * static_cast<double>(evidence) / static_cast<double>(marks.size() + empty.size()));
    }
    return best;
}

double ckey_calibration_score_text_gray8(const uint8_t *gray, size_t width, size_t height,
                                         size_t stride, const double quad_xy[8]) {
    if (!valid_image_args(gray, width, height, stride, quad_xy)) return -1.0;
    constexpr double canonical[8] = {0,0,29,0,29,29,0,29};
    double best = 0.0;
    for (int reverse=0; reverse<2; ++reverse) for (int start=0; start<4; ++start) {
        double dst[8];
        for (int i=0; i<4; ++i) {
            const int q=(start+(reverse ? -i : i)+8)%4;
            dst[2*i]=quad_xy[2*q]; dst[2*i+1]=quad_xy[2*q+1];
        }
        double h[8]; if (!solve_homography(canonical,dst,h)) continue;
        const KeyFit fit=text_key_fit(gray,width,height,stride,h);
        if (fit.valid) best=std::max(best,fit.contrast*fit.evidence);
    }
    return best;
}

double ckey_calibration_score_extended_gray8(const uint8_t *gray, size_t width, size_t height,
                                             size_t stride, const double quad_xy[8]) {
    if (!valid_image_args(gray, width, height, stride, quad_xy)) return -1.0;
    constexpr double canonical[8] = {0,0,116,0,116,116,0,116};
    double best = 0.0;
    for (int reverse=0; reverse<2; ++reverse) for (int start=0; start<4; ++start) {
        double dst[8];
        for (int i=0; i<4; ++i) { const int q=(start+(reverse ? -i : i)+8)%4; dst[2*i]=quad_xy[2*q]; dst[2*i+1]=quad_xy[2*q+1]; }
        double h[8]; if (!solve_homography(canonical,dst,h)) continue;
        const KeyFit fit=extended_key_fit(gray,width,height,stride,h);
        if (fit.valid) best=std::max(best,fit.contrast*fit.evidence);
    }
    return best;
}

ckey_status ckey_decode_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                               const double quad_xy[8], uint8_t *payload, size_t out_capacity,
                               ckey_diagnostics *diag) {
    if (!gray || !quad_xy || !payload || width<2 || height<2 || stride<width ||
        out_capacity<CKEY_LOCATOR_PAYLOAD_BYTES || height>std::numeric_limits<size_t>::max()/stride) {
        set_status(diag,CKEY_INVALID_ARGUMENT,"invalid grayscale image, quadrilateral, or output buffer"); return CKEY_INVALID_ARGUMENT;
    }
    for (int i=0;i<8;++i) if (!std::isfinite(quad_xy[i])) { set_status(diag,CKEY_INVALID_ARGUMENT,"quadrilateral coordinates must be finite"); return CKEY_INVALID_ARGUMENT; }
    if (!valid_quad(quad_xy)) { set_status(diag,CKEY_INVALID_ARGUMENT,"quadrilateral must be a non-degenerate convex cyclic perimeter"); return CKEY_INVALID_ARGUMENT; }

    std::array<uint8_t,CKEY_LOCATOR_PAYLOAD_BYTES> accepted{};
    bool found=false, ambiguous=false; uint32_t accepted_corrections=0;
    constexpr double canonical[8]={0,0,29,0,29,29,0,29};
    // Try all cyclic starts and both winding directions; orientation is validated by keys and frame CRC.
    for (int reverse=0;reverse<2;++reverse) for (int start=0;start<4;++start) {
        double dst[8];
        for (int i=0;i<4;++i) { int qi=(start+(reverse?-i:i)+8)%4; dst[2*i]=quad_xy[2*qi]; dst[2*i+1]=quad_xy[2*qi+1]; }
        double h[8]; if (!solve_homography(canonical,dst,h)) continue;
        // Require the full symbol footprint, including every sample patch, to be in the image.
        ImagePoint corners[4]; bool inside=true;
        for(int i=0;i<4;++i) { if(!map_point(h,canonical[2*i],canonical[2*i+1],corners[i]) || corners[i].x<0 || corners[i].y<0 || corners[i].x>=width || corners[i].y>=height) inside=false; }
        if(!inside) continue;

        std::vector<double> grid_marks, ring_marks, ray_marks, empties;
        for (int r : {11,12}) for (int c : {2,3}) grid_marks.push_back(gray_sample(gray,width,height,stride,h,c+.5,r+.5));
        for (int slot : {18,19,21}) ring_marks.push_back(polar_sample(gray,width,height,stride,h,6.0,slot));
        ring_marks.push_back(polar_sample(gray,width,height,stride,h,6.0,23.5)); // center of the reserved continuous four-slot arc
        ray_marks.push_back(polar_sample(gray,width,height,stride,h,8.8,0));
        ray_marks.push_back(polar_sample(gray,width,height,stride,h,8.4,54));
        ray_marks.push_back(polar_sample(gray,width,height,stride,h,8.8,55));
        empties.push_back(polar_sample(gray,width,height,stride,h,kLocatorApertureRadius,36));
        empties.push_back(polar_sample(gray,width,height,stride,h,6.0,20));
        empties.push_back(polar_sample(gray,width,height,stride,h,8.8,54));
        std::vector<double> marks=grid_marks;
        marks.insert(marks.end(),ring_marks.begin(),ring_marks.end());
        marks.insert(marks.end(),ray_marks.begin(),ray_marks.end());
        if (std::any_of(marks.begin(),marks.end(),[](double v){return v<0;}) || std::any_of(empties.begin(),empties.end(),[](double v){return v<0;})) continue;
        const double mark_mean=std::accumulate(marks.begin(),marks.end(),0.0)/marks.size();
        const double empty_mean=std::accumulate(empties.begin(),empties.end(),0.0)/empties.size();
        if (std::abs(mark_mean-empty_mean)<24.0) continue;
        const bool dark_marks=mark_mean<empty_mean;
        const double threshold=(mark_mean+empty_mean)*.5;
        auto marked=[&](double v){return dark_marks?v<threshold:v>threshold;};
        const size_t grid_valid=std::count_if(grid_marks.begin(),grid_marks.end(),marked);
        const size_t ring_valid=std::count_if(ring_marks.begin(),ring_marks.end(),marked);
        const size_t ray_valid=std::count_if(ray_marks.begin(),ray_marks.end(),marked);
        const size_t empty_keys=std::count_if(empties.begin(),empties.end(),[&](double v){return !marked(v);});
        if (grid_valid<3 || ring_valid<3 || ray_valid<2 || empty_keys<2) continue;

        // Data sites are normalized between the solid Grid Key modules and the
        // empty references, then each feature class (modules, ring dots, dial
        // rays) is thresholded from its own samples. Dots and 0.15-unit rays
        // use a support narrower than the stroke so it is not averaged away.
        const double grid_mean=std::accumulate(grid_marks.begin(),grid_marks.end(),0.0)/grid_marks.size();
        std::array<uint8_t,kCodewordBytes> cw{};
        bool samples_ok=true;
        const auto &nodes=data_nodes();
        std::vector<double> level(kBits); std::array<std::vector<double>,3> classes;
        const auto feature=[](const Node &n){ return n.dial ? 2 : n.polar ? 1 : 0; };
        for (size_t bit=0;bit<kBits;++bit) {
            const Node &n=nodes[bit]; double v;
            if (n.dial) v=polar_sample(gray,width,height,stride,h,8.8,n.slot,kLocatorRaySpread); // outer band separates short from long ray
            else if (n.polar) v=gray_sample(gray,width,height,stride,h,n.x,n.y,kLocatorDotSpread);
            else v=gray_sample(gray,width,height,stride,h,n.x,n.y);
            if (v<0) { samples_ok=false; break; }
            level[bit]=(v-empty_mean)/(grid_mean-empty_mean);
            classes[feature(n)].push_back(level[bit]);
        }
        if(!samples_ok) continue;
        const std::array<double,3> cut={class_threshold(classes[0]),class_threshold(classes[1]),class_threshold(classes[2])};
        for (size_t bit=0;bit<kBits;++bit)
            if (level[bit]>cut[feature(nodes[bit])]) cw[bit/8]|=static_cast<uint8_t>(1u<<(7-(bit%8)));
        std::array<uint8_t,CKEY_LOCATOR_PAYLOAD_BYTES> candidate{}; ckey_diagnostics local{};
        const ckey_status status=ckey_decode_locator(cw.data(),cw.size(),candidate.data(),candidate.size(),&local);
        if(status!=CKEY_OK) continue;
        if(!found) {accepted=candidate; accepted_corrections=local.corrected_symbols; found=true;}
        else if(candidate!=accepted) ambiguous=true;
    }
    if (!found || ambiguous) { set_status(diag,ambiguous?CKEY_INTEGRITY_FAILURE:CKEY_UNCORRECTABLE,ambiguous?"multiple orientations yielded different valid payloads":"no orientation passed calibration keys, Reed-Solomon, and CRC-32C"); return ambiguous?CKEY_INTEGRITY_FAILURE:CKEY_UNCORRECTABLE; }
    std::memcpy(payload,accepted.data(),accepted.size());
    set_status(diag,CKEY_OK,"decoded rectified locator image; caller supplied symbol quadrilateral");
    if(diag) diag->corrected_symbols=accepted_corrections;
    return CKEY_OK;
}

// The dense profiles deliberately keep the same projective contract as the
// locator: callers locate a plausible outer boundary, while this core owns
// orientation, sampling, ECC, and integrity.  Their calibration samples use
// actual payload-sized Grid Key marks, not a second oversized marker.
ckey_status ckey_decode_text_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                   const double quad_xy[8], uint8_t *payload, size_t out_capacity,
                                   size_t *out_payload_len, ckey_diagnostics *diag) {
    if (!payload || !out_payload_len || !valid_image_args(gray, width, height, stride, quad_xy)) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 2 image, quadrilateral, or output buffer"); return CKEY_INVALID_ARGUMENT;
    }
    constexpr double canonical[8]={0,0,29,0,29,29,0,29};
    constexpr size_t bits=kTextCodewordBytes*8;
    const auto &nodes=text_nodes(); const auto &white=text_whitening();
    if (nodes.size() < bits) { set_status(diag,CKEY_INTERNAL_ERROR,"Profile 2 sampling layout unavailable"); return CKEY_INTERNAL_ERROR; }
    bool found=false, ambiguous=false; std::vector<uint8_t> accepted; uint32_t accepted_corrections=0;
    for (int reverse=0;reverse<2;++reverse) for (int start=0;start<4;++start) {
        double dst[8],h[8];
        for (int i=0;i<4;++i) { const int q=(start+(reverse?-i:i)+8)%4; dst[2*i]=quad_xy[2*q]; dst[2*i+1]=quad_xy[2*q+1]; }
        if (!solve_homography(canonical,dst,h)) continue;
        { const KeyFit fit=text_key_fit(gray,width,height,stride,h); if (!fit.valid || fit.evidence<kDenseKeyGate) continue; }
        std::vector<double> marks, empties;
        for (int r:{11,12}) for(int c:{2,3}) marks.push_back(gray_sample(gray,width,height,stride,h,c+.5,r+.5));
        // Both points lie inside declared clearances, so they are never printed.
        empties.push_back(gray_sample(gray,width,height,stride,h,1.5,11.5,kModuleSpread));
        empties.push_back(gray_sample(gray,width,height,stride,h,5.5,14.5,kModuleSpread));
        if (std::any_of(marks.begin(),marks.end(),[](double x){return x<0;}) || std::any_of(empties.begin(),empties.end(),[](double x){return x<0;})) continue;
        const double mark_mean=std::accumulate(marks.begin(),marks.end(),0.0)/marks.size(), empty_mean=std::accumulate(empties.begin(),empties.end(),0.0)/empties.size();
        if (std::abs(mark_mean-empty_mean)<18.0) continue;
        const bool dark=mark_mean<empty_mean; const double threshold=(mark_mean+empty_mean)*.5;
        const auto marked=[&](double x){return dark?x<threshold:x>threshold;};
        if (std::count_if(marks.begin(),marks.end(),marked)<3 || std::count_if(empties.begin(),empties.end(),[&](double x){return !marked(x);})<1) continue;
        (void)marked;
        // Pass 1: normalized samples per feature class (Cartesian, dial, ring).
        std::vector<double> level(bits); std::array<std::vector<double>,3> classes; bool usable=true;
        const auto feature=[](const TextNode &n){ return n.cart ? 0 : n.dial ? 1 : 2; };
        for (size_t bit=0;bit<bits;++bit) {
            const auto &n=nodes[bit];
            const double v=n.cart ? gray_sample(gray,width,height,stride,h,n.x,n.y,kModuleSpread)
                         : n.dial ? polar_sample_slots(gray,width,height,stride,h,kCenter,8.55,n.slot,144.0,kRaySpread)
                                  : gray_sample(gray,width,height,stride,h,n.x,n.y,kDotSpread);
            if (v<0) { usable=false; break; }
            level[bit]=(v-empty_mean)/(mark_mean-empty_mean);
            classes[feature(n)].push_back(level[bit]);
        }
        if (!usable) continue;
        const std::array<double,3> cut={class_threshold(classes[0]),class_threshold(classes[1]),class_threshold(classes[2])};
        // printed[i]: estimated printed value of site i. Fill sites are known.
        std::vector<uint8_t> printed(nodes.size());
        for (size_t i=0;i<nodes.size();++i) printed[i]=i<bits ? (level[i]>cut[feature(nodes[i])]) : white[i];
        static const std::vector<int> ring_grid=[]{
            std::vector<int> grid(kTextRings*kTextSlots,-1); const auto &all=text_nodes();
            for (size_t i=0;i<all.size();++i) if (!all[i].cart && !all[i].dial) grid[static_cast<size_t>(all[i].ring)*kTextSlots+all[i].slot]=static_cast<int>(i);
            return grid; }();
        refine_ring_context(ring_grid,kTextRings,kTextSlots,bits,white,level,cut[2],printed);
        std::array<uint8_t,kTextCodewordBytes> cw{};
        for (size_t bit=0;bit<bits;++bit)
            if ((printed[bit]!=0) != (white[bit]!=0)) cw[bit/8]|=static_cast<uint8_t>(1u<<(7-(bit%8)));
        std::array<uint8_t,CKEY_TEXT_PAYLOAD_MAX_BYTES> candidate{}; size_t candidate_len=0; ckey_diagnostics local{};
        if (ckey_decode_text(cw.data(),cw.size(),candidate.data(),candidate.size(),&candidate_len,&local)!=CKEY_OK) continue;
        const std::vector<uint8_t> result(candidate.begin(),candidate.begin()+static_cast<std::ptrdiff_t>(candidate_len));
        if (!found) { accepted=result; accepted_corrections=local.corrected_symbols; found=true; }
        else if (result!=accepted) ambiguous=true;
    }
    if (!found || ambiguous) { set_status(diag,ambiguous?CKEY_INTEGRITY_FAILURE:CKEY_UNCORRECTABLE,ambiguous?"multiple Profile 2 orientations passed integrity":"no Profile 2 orientation passed calibration, Reed-Solomon, and CRC"); return ambiguous?CKEY_INTEGRITY_FAILURE:CKEY_UNCORRECTABLE; }
    if (out_capacity<accepted.size()) { set_status(diag,CKEY_INSUFFICIENT_BUFFER,"Profile 2 output buffer is too small"); return CKEY_INSUFFICIENT_BUFFER; }
    std::memcpy(payload,accepted.data(),accepted.size()); *out_payload_len=accepted.size();
    set_status(diag,CKEY_OK,"decoded rectified Profile 2 image"); if (diag) diag->corrected_symbols=accepted_corrections;
    return CKEY_OK;
}

ckey_status ckey_decode_extended_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                       const double quad_xy[8], uint8_t *payload, size_t out_capacity,
                                       size_t *out_payload_len, ckey_diagnostics *diag) {
    if (!payload || !out_payload_len || !valid_image_args(gray, width, height, stride, quad_xy)) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 3 image, quadrilateral, or output buffer"); return CKEY_INVALID_ARGUMENT;
    }
    constexpr double canonical[8]={0,0,116,0,116,116,0,116};
    const auto &nodes=extended_nodes(); const auto &white=extended_whitening();
    if (nodes.size()<kExtendedBits) { set_status(diag,CKEY_INTERNAL_ERROR,"Profile 3 sampling layout unavailable"); return CKEY_INTERNAL_ERROR; }
    bool found=false, ambiguous=false; std::vector<uint8_t> accepted; uint32_t accepted_corrections=0;
    for (int reverse=0;reverse<2;++reverse) for (int start=0;start<4;++start) {
        double dst[8],h[8];
        for (int i=0;i<4;++i) { const int q=(start+(reverse?-i:i)+8)%4; dst[2*i]=quad_xy[2*q]; dst[2*i+1]=quad_xy[2*q+1]; }
        if (!solve_homography(canonical,dst,h)) continue;
        { const KeyFit fit=extended_key_fit(gray,width,height,stride,h); if (!fit.valid || fit.evidence<kDenseKeyGate) continue; }
        std::vector<double> marks, empties;
        for (int r=0;r<2;++r) for (int c=0;c<2;++c) marks.push_back(gray_sample(gray,width,height,stride,h,8.25+c*.5,45.25+r*.5,kModuleSpread));
        empties.push_back(gray_sample(gray,width,height,stride,h,kExtendedGridClearX,kExtendedGridClearY,kModuleSpread));
        empties.push_back(gray_sample(gray,width,height,stride,h,58.0,4.5,kModuleSpread));
        if (std::any_of(marks.begin(),marks.end(),[](double x){return x<0;}) || std::any_of(empties.begin(),empties.end(),[](double x){return x<0;})) continue;
        const double mark_mean=std::accumulate(marks.begin(),marks.end(),0.0)/marks.size(), empty_mean=std::accumulate(empties.begin(),empties.end(),0.0)/empties.size();
        if (std::abs(mark_mean-empty_mean)<14.0) continue;
        const bool dark=mark_mean<empty_mean; const double threshold=(mark_mean+empty_mean)*.5;
        const auto marked=[&](double x){return dark?x<threshold:x>threshold;};
        if (std::count_if(marks.begin(),marks.end(),marked)<3) continue;
        (void)marked;
        const auto normalized=[&](double v){ return (v-empty_mean)/(mark_mean-empty_mean); };
        // Pass 1: normalized samples for Cartesian and ring sites; per-ray
        // length probes and integrated width coverage.
        std::vector<double> level(kExtendedBits, 0.0); std::vector<double> cart_values, ring_values, probe_values, width_values;
        std::array<std::array<double,4>,kExtendedRaySlots> probes{}; std::array<double,kExtendedRaySlots> coverage{};
        std::array<bool,kExtendedRaySlots> ray_ready{}; bool usable=true;
        for (size_t bit=0;bit<kExtendedBits && usable;++bit) {
            const auto &n=nodes[bit];
            if (n.kind!=ExtendedKind::ray) {
                const double v=gray_sample(gray,width,height,stride,h,n.x,n.y,n.kind==ExtendedKind::cart?kModuleSpread:kDotSpread);
                if (v<0) { usable=false; break; }
                level[bit]=normalized(v);
                (n.kind==ExtendedKind::cart?cart_values:ring_values).push_back(level[bit]);
                continue;
            }
            if (ray_ready[n.slot]) continue;
            for (int l=0;l<4;++l) {
                const double v=polar_sample_slots(gray,width,height,stride,h,58.0,26.30+.80+.85*l-.16,n.slot,576.0,kRaySpread);
                if (v<0) { usable=false; break; }
                probes[n.slot][l]=normalized(v); probe_values.push_back(probes[n.slot][l]);
            }
            // Width is integrated ink coverage across the ray, which blur
            // conserves: thin (0.16) covers ~0.6 of the +/-0.12 window and
            // wide (0.28) covers nearly all of it.
            const double a=kTau*n.slot/576.0, r=26.75, ux=std::sin(a), uy=-std::cos(a);
            double sum=0;
            for (int k=-3;k<=3 && usable;++k) {
                const double t=.04*k;
                const double v=gray_sample(gray,width,height,stride,h,58.0+r*ux-t*uy,58.0+r*uy+t*ux,0.0);
                if (v<0) { usable=false; break; }
                sum+=std::clamp(normalized(v),0.0,1.0);
            }
            coverage[n.slot]=sum/7.0; width_values.push_back(coverage[n.slot]); ray_ready[n.slot]=true;
        }
        if (!usable) continue;
        const double cart_cut=class_threshold(cart_values), ring_cut=class_threshold(ring_values);
        const double probe_cut=class_threshold(probe_values), width_cut=class_threshold(width_values);
        std::vector<uint8_t> ring_printed(nodes.size());
        for (size_t i=0;i<nodes.size();++i)
            ring_printed[i]=i<kExtendedBits ? (nodes[i].kind==ExtendedKind::ring && level[i]>ring_cut) : white[i];
        static const std::vector<int> ring_grid=[]{
            std::vector<int> grid(kExtendedRings*kExtendedRingSlots,-1); const auto &all=extended_nodes();
            for (size_t i=0;i<all.size();++i) if (all[i].kind==ExtendedKind::ring) grid[static_cast<size_t>(all[i].ring)*kExtendedRingSlots+all[i].slot]=static_cast<int>(i);
            return grid; }();
        refine_ring_context(ring_grid,kExtendedRings,kExtendedRingSlots,kExtendedBits,white,level,ring_cut,ring_printed);
        std::array<uint8_t,kExtendedCodewordBytes> cw{};
        for (size_t bit=0;bit<kExtendedBits;++bit) {
            const auto &n=nodes[bit];
            bool printed;
            if (n.kind==ExtendedKind::cart) printed=level[bit]>cart_cut;
            else if (n.kind==ExtendedKind::ring) printed=ring_printed[bit]!=0;
            else if (n.component==2) printed=coverage[n.slot]>width_cut;
            else {
                int length=0;
                for (int l=0;l<4;++l) if (probes[n.slot][l]>probe_cut) length=l;
                const int gray_code=length^(length>>1);
                printed=n.component==0 ? ((gray_code>>1)&1)!=0 : (gray_code&1)!=0;
            }
            if (printed != (white[bit]!=0)) cw[bit/8]|=static_cast<uint8_t>(1u<<(7-(bit%8)));
        }
        std::array<uint8_t,CKEY_EXTENDED_PAYLOAD_MAX_BYTES> candidate{}; size_t candidate_len=0; ckey_diagnostics local{};
        if (ckey_decode_extended(cw.data(),cw.size(),candidate.data(),candidate.size(),&candidate_len,&local)!=CKEY_OK) continue;
        const std::vector<uint8_t> result(candidate.begin(),candidate.begin()+static_cast<std::ptrdiff_t>(candidate_len));
        if (!found) { accepted=result; accepted_corrections=local.corrected_symbols; found=true; } else if (result!=accepted) ambiguous=true;
    }
    if (!found || ambiguous) { set_status(diag,ambiguous?CKEY_INTEGRITY_FAILURE:CKEY_UNCORRECTABLE,ambiguous?"multiple Profile 3 orientations passed integrity":"no Profile 3 orientation passed calibration, Reed-Solomon, and CRC"); return ambiguous?CKEY_INTEGRITY_FAILURE:CKEY_UNCORRECTABLE; }
    if (out_capacity<accepted.size()) { set_status(diag,CKEY_INSUFFICIENT_BUFFER,"Profile 3 output buffer is too small"); return CKEY_INSUFFICIENT_BUFFER; }
    std::memcpy(payload,accepted.data(),accepted.size()); *out_payload_len=accepted.size();
    set_status(diag,CKEY_OK,"decoded rectified Profile 3 image"); if (diag) diag->corrected_symbols=accepted_corrections;
    return CKEY_OK;
}

ckey_status ckey_render_svg(const ckey_symbol *s, uint32_t scale, char *out,
                            size_t cap, size_t *required) {
    if (!s || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE || s->abi_version != CKEY_ABI_VERSION || s->side != kSide) return CKEY_INVALID_ARGUMENT;
    const auto &nodes = data_nodes();
    if (nodes.size() < kBits) return CKEY_INTERNAL_ERROR;
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    std::ostringstream c; c << std::fixed << std::setprecision(4);  // Cartesian payload layer
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 31 * scale << "\" height=\"" << 31 * scale << "\" viewBox=\"-1 -1 31 31\" role=\"img\" aria-label=\"CKey locator symbol\">"
      << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" fill=\"#050505\"/><g fill=\"#f4f1e8\" stroke=\"#f4f1e8\">"
      << "<g class=\"ckey-frame\"><rect x=\"0.5\" y=\"0.5\" width=\"28\" height=\"28\" rx=\"3.5\" fill=\"none\" stroke-width=\"0.65\"/></g>"
      << "<g class=\"ckey-finders\">";
    svg_corner_finders(o, 29.0, 4.0, {2.30, 0.0, 0.0}, .80, .95);
    // Grid Key: fixed 2x2 Cartesian reference block at the left matrix edge.
    o << "</g><g class=\"ckey-grid-key\">";
    for (int r : {11, 12}) for (int c : {2, 3}) o << "<rect x=\"" << c + .13 << "\" y=\"" << r + .13 << "\" width=\"0.74\" height=\"0.74\" rx=\"0.16\" stroke=\"none\"/>";
    o << "</g><g class=\"ckey-polar-keys\">";
    // Permanent Ray Key is contained in the outer dial band (r=7.6..9.5).
    for (const auto &[slot, fraction] : {std::pair{0, 1.0}, {54, .5}, {55, .75}}) {
        const double a = kTau * slot / 72.0, r0 = 7.6, r1 = r0 + 1.9 * fraction;
        o << "<path d=\"M " << kCenter + r0 * std::sin(a) << ' ' << kCenter - r0 * std::cos(a) << " L " << kCenter + r1 * std::sin(a) << ' ' << kCenter - r1 * std::cos(a)
          << "\" fill=\"none\" stroke=\"#f4f1e8\" stroke-width=\"0.22\"/>";
    }
    // Permanent Ring Key on the outermost inner data ring.
    const double rr = 6.0;
    for (int slot : {18, 19, 21}) { const double a = kTau * slot / 72.0; svg_circle(o, kCenter + rr * std::sin(a), kCenter - rr * std::cos(a), polar_dot_radius()); }
    const double ring_key_dot_diameter = 2.0 * polar_dot_radius();
    // Round caps add one dot diameter to the visible length. Shorten the arc
    // centerline accordingly so its total visible length remains four slots.
    const double cap_angle = polar_dot_radius() / rr;
    const double a0 = kTau * 21.5 / 72.0 + cap_angle;
    const double a1 = kTau * 25.5 / 72.0 - cap_angle;
    o << "<path d=\"M " << kCenter + rr * std::sin(a0) << ' ' << kCenter - rr * std::cos(a0) << " A " << rr << ' ' << rr << " 0 0 1 "
      << kCenter + rr * std::sin(a1) << ' ' << kCenter - rr * std::cos(a1) << "\" fill=\"none\" stroke=\"#f4f1e8\" stroke-width=\"" << ring_key_dot_diameter << "\" stroke-linecap=\"round\"/>";
    o << "</g><g class=\"ckey-data-polar\">";
    for (size_t i = 0; i < kBits; ++i) {
        const Node &n = nodes[i];
        if (n.dial) {
            const double a = kTau * n.slot / 72.0;
            // Every data-bearing dial slot is marked. A short ray encodes 0;
            // a long ray encodes 1. Both remain distinct from reserved keys.
            const double length = code_bit(s, i) ? 1.35 : 0.65;
            o << "<path d=\"M " << kCenter + 7.6 * std::sin(a) << ' ' << kCenter - 7.6 * std::cos(a) << " L " << kCenter + (7.6 + length) * std::sin(a) << ' ' << kCenter - (7.6 + length) * std::cos(a)
              << "\" fill=\"none\" stroke=\"#f4f1e8\" stroke-width=\"0.15\" stroke-linecap=\"round\"/>";
            continue;
        }
        if (!code_bit(s, i)) continue;
        if (n.polar) svg_circle(o, n.x, n.y, polar_dot_radius());
        else c << "<rect x=\"" << n.col + .13 << "\" y=\"" << n.row + .13 << "\" width=\"0.74\" height=\"0.74\" rx=\"0.16\" stroke=\"none\"/>";
    }
    o << "</g><g class=\"ckey-data-cartesian\">" << c.str() << "</g></g></svg>";
    const std::string text = o.str(); *required = text.size() + 1;
    if (!out || cap < *required) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out, text.c_str(), *required);
    return CKEY_OK;
}

ckey_status ckey_render_text_svg(const ckey_text_symbol *s, uint32_t scale, char *out,
                                  size_t cap, size_t *required) {
    if (!s || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE ||
        s->abi_version != CKEY_ABI_VERSION || s->side != kSide || s->payload_length > CKEY_TEXT_PAYLOAD_MAX_BYTES) return CKEY_INVALID_ARGUMENT;
    const auto &nodes = text_nodes();
    if (nodes.size() < kTextCodewordBytes * 8) return CKEY_INTERNAL_ERROR;
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    std::ostringstream c; c << std::fixed << std::setprecision(4);  // Cartesian payload layer
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 31 * scale << "\" height=\"" << 31 * scale << "\" viewBox=\"-1 -1 31 31\" role=\"img\" aria-label=\"CKey text symbol profile 2\">"
      << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" fill=\"#050505\"/><g fill=\"#f4f1e8\" stroke=\"#f4f1e8\">"
      << "<g class=\"ckey-frame\"><rect x=\"0.5\" y=\"0.5\" width=\"28\" height=\"28\" rx=\"3.5\" fill=\"none\" stroke-width=\"0.65\"/></g>"
      << "<g class=\"ckey-finders\">";
    svg_corner_finders(o, 29.0, 4.0, {2.30, 0.0, 0.0}, .80, .95);
    o << "</g><g class=\"ckey-grid-key\">";
    for (int r : {11,12}) for (int c : {2,3}) o << "<rect x=\"" << c+.13 << "\" y=\"" << r+.13 << "\" width=\"0.74\" height=\"0.74\" rx=\"0.16\" stroke=\"none\"/>";
    o << "</g><g class=\"ckey-polar-keys\">";
    for (const auto &[slot, fraction] : {std::pair{0,1.0}, std::pair{108,.5}, std::pair{110,.75}}) {
        const double a=kTau*slot/144.0, r0=7.6, r1=r0+1.9*fraction;
        o << "<path d=\"M " << kCenter+r0*std::sin(a) << ' ' << kCenter-r0*std::cos(a) << " L " << kCenter+r1*std::sin(a) << ' ' << kCenter-r1*std::cos(a) << "\" fill=\"none\" stroke-width=\"0.22\" stroke-linecap=\"round\"/>";
    }
    constexpr double key_radius=6.41, dot_radius=.10;
    for (int slot : {36,38,42}) { const double a=kTau*slot/144.0; svg_circle(o,kCenter+key_radius*std::sin(a),kCenter-key_radius*std::cos(a),dot_radius); }
    svg_ring_run(o,kCenter,key_radius,43.5,51.5,144.0,dot_radius,"0.2000");
    o << "</g><g class=\"ckey-data-polar\">";
    std::array<std::array<bool,kTextSlots>,kTextRings> ring_bits{};
    for (size_t i=0;i<nodes.size();++i) {
        const auto &n=nodes[i]; const bool bit=text_site_value(s,i);
        if (n.cart) { if (bit) c << "<rect x=\"" << n.x-.17 << "\" y=\"" << n.y-.17 << "\" width=\"0.34\" height=\"0.34\" rx=\"0.08\" stroke=\"none\"/>"; }
        else if (n.dial) { const double a=kTau*n.slot/144.0, length=bit?1.20:.52; o << "<path d=\"M " << kCenter+7.6*std::sin(a) << ' ' << kCenter-7.6*std::cos(a) << " L " << kCenter+(7.6+length)*std::sin(a) << ' ' << kCenter-(7.6+length)*std::cos(a) << "\" fill=\"none\" stroke-width=\"0.1000\" stroke-linecap=\"round\"/>"; }
        else ring_bits[n.ring][n.slot]=bit;
    }
    for (int ring=0;ring<kTextRings;++ring) {
        const double radius=3.4+.43*ring;
        for (int slot=0;slot<kTextSlots;) {
            if (!ring_bits[ring][slot]) { ++slot; continue; }
            int end=slot+1; while (end<kTextSlots && ring_bits[ring][end]) ++end;
            if (end-slot==1) { const double a=kTau*slot/kTextSlots; svg_circle(o,kCenter+radius*std::sin(a),kCenter-radius*std::cos(a),dot_radius); }
            else svg_ring_run(o,kCenter,radius,slot-.5,end-.5,kTextSlots,dot_radius,"0.2000");
            slot=end;
        }
    }
    o << "</g><g class=\"ckey-data-cartesian\">" << c.str() << "</g></g></svg>";
    const std::string text=o.str(); *required=text.size()+1;
    if (!out || cap<*required) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out,text.c_str(),*required); return CKEY_OK;
}

ckey_status ckey_render_extended_svg(const ckey_extended_symbol *s, uint32_t scale, char *out,
                                      size_t cap, size_t *required) {
    if (!s || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE || s->abi_version != CKEY_ABI_VERSION || s->side != kExtendedSide || s->payload_length == 0 || s->payload_length > CKEY_EXTENDED_PAYLOAD_MAX_BYTES) return CKEY_INVALID_ARGUMENT;
    const auto &nodes=extended_nodes(); if (nodes.size()<kExtendedBits) return CKEY_INTERNAL_ERROR;
    constexpr double center=58.0, dot_radius=.075;
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    std::ostringstream c; c << std::fixed << std::setprecision(4);  // Cartesian payload layer
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 124*scale << "\" height=\"" << 124*scale << "\" viewBox=\"-4 -4 124 124\" role=\"img\" aria-label=\"CKey extended symbol profile 3\">"
      << "<rect x=\"-4\" y=\"-4\" width=\"124\" height=\"124\" fill=\"#050505\"/><g fill=\"#f4f1e8\" stroke=\"#f4f1e8\">"
      << "<g class=\"ckey-frame\"><rect x=\"2\" y=\"2\" width=\"112\" height=\"112\" rx=\"14\" fill=\"none\" stroke-width=\"2.6\"/></g>"
      << "<g class=\"ckey-finders\">";
    svg_corner_finders(o, 116.0, 16.0, {9.20, 0.0, 0.0}, 3.2, 3.8);
    // Fixed Grid Key: four ordinary 0.34-unit Cartesian modules on the same
    // 0.5-unit pitch as payload, so it calibrates payload pitch and contrast.
    o << "</g><g class=\"ckey-grid-key\">";
    for (int r=0;r<2;++r) for (int c=0;c<2;++c) o << "<rect x=\"" << 8.08+c*.5 << "\" y=\"" << 45.08+r*.5 << "\" width=\".34\" height=\".34\" rx=\".08\" stroke=\"none\"/>";
    // Fixed Ray Key: 12:00 full; 9:00 half; 9:05 three-quarter length.
    o << "</g><g class=\"ckey-polar-keys\">";
    for (const auto &[slot,fraction] : {std::pair{0,1.0},std::pair{432,.5},std::pair{440,.75}}) {
        const double a=kTau*slot/576.0,r0=26.30,r1=r0+3.6*fraction;
        o << "<path d=\"M " << center+r0*std::sin(a) << ' ' << center-r0*std::cos(a) << " L " << center+r1*std::sin(a) << ' ' << center-r1*std::cos(a) << "\" fill=\"none\" stroke-width=\".28\" stroke-linecap=\"round\"/>";
    }
    // Fixed Ring Key: two adjacent dots, one empty slot, one dot, then a
    // continuous four-slot arc of equal angular occupancy.
    constexpr double key_r=kExtendedRingKeyRadius;
    for (int slot : {72,73,75}) { const double a=kTau*slot/kExtendedRingSlots; svg_circle(o,center+key_r*std::sin(a),center-key_r*std::cos(a),dot_radius); }
    svg_ring_run(o,center,key_r,75.5,79.5,kExtendedRingSlots,dot_radius,".1500");
    o << "</g><g class=\"ckey-data-polar\">";
    std::array<std::array<bool,kExtendedRingSlots>,kExtendedRings> ring_bits{}; std::array<std::array<bool,3>,kExtendedRaySlots> ray_bits{};
    for (size_t i=0;i<nodes.size();++i) {
        const auto &n=nodes[i]; const bool bit=extended_site_value(s,i);
        if (n.kind==ExtendedKind::cart) { if (bit) c << "<rect x=\"" << n.x-.17 << "\" y=\"" << n.y-.17 << "\" width=\".34\" height=\".34\" rx=\".08\" stroke=\"none\"/>"; }
        else if (n.kind==ExtendedKind::ring) ring_bits[n.ring][n.slot]=bit;
        else ray_bits[n.slot][n.component]=bit;
    }
    // The ray alphabet is (Gray length, width): 4 calibrated lengths x 2
    // calibrated weights, three site values per ray. No state is an absent ray.
    for (int slot=0;slot<kExtendedRaySlots;++slot) {
        if (extended_reserved_ray(slot)) continue;
        const int gray=(ray_bits[slot][0]?2:0)|(ray_bits[slot][1]?1:0), level=gray^(gray>>1);
        const double a=kTau*slot/576.0,r0=26.30,length=.80+.85*level,width=ray_bits[slot][2]?.28:.16;
        o << "<path d=\"M " << center+r0*std::sin(a) << ' ' << center-r0*std::cos(a) << " L " << center+(r0+length)*std::sin(a) << ' ' << center-(r0+length)*std::cos(a) << "\" fill=\"none\" stroke-width=\"" << width << "\" stroke-linecap=\"round\"/>";
    }
    for (int ring=0;ring<kExtendedRings;++ring) {
        const double radius=kExtendedRingInner+kExtendedRingPitch*ring;
        for (int slot=0;slot<kExtendedRingSlots;) {
            if (!ring_bits[ring][slot]) { ++slot; continue; }
            int end=slot+1; while(end<kExtendedRingSlots && ring_bits[ring][end]) ++end;
            if (end-slot==1) { const double a=kTau*slot/kExtendedRingSlots; svg_circle(o,center+radius*std::sin(a),center-radius*std::cos(a),dot_radius); }
            else svg_ring_run(o,center,radius,slot-.5,end-.5,kExtendedRingSlots,dot_radius,".1500");
            slot=end;
        }
    }
    o << "</g><g class=\"ckey-data-cartesian\">" << c.str() << "</g></g></svg>"; const std::string text=o.str(); *required=text.size()+1;
    if (!out || cap<*required) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out,text.c_str(),*required);
    return CKEY_OK;
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
constexpr std::array<int, 4> kScalableParityPercent = {14, 28, 44, 60};  // L, M, Q, H
constexpr double kScalableRingInner = 3.4, kScalableDialInner = 7.6;
constexpr size_t kScalableHeaderBytes = 8, kScalableOverheadBytes = 12;  // header + CRC-32C

struct ScalableNode { uint8_t kind; double x, y; int ring, slot; uint32_t id; };  // kind: 0 Cartesian, 1 dial, 2 ring

struct ScalableLayout {
    int version = 0, grid = 0, slots = 0, rings = 0, col0 = 0, row0 = 0;
    double pitch = 0;
    DenseGeometry geom{};
    std::vector<ScalableNode> nodes;
    std::vector<uint8_t> white;
    std::vector<int> ring_grid;  // ring * slots + slot -> site index, -1 reserved
    size_t codeword_bytes = 0;
    double ring_radius(int k) const { return kScalableRingInner + 0.86 * pitch * k; }
    int ray_key_half() const { return 3 * slots / 4; }       // 9:00, half length
    int ray_key_three() const { return 3 * slots / 4 - 2; }  // two slots before, 3/4 length
    int ring_key_slot() const { return slots / 4; }          // 3:00, on the outermost ring
    bool reserved_ray(int slot) const { return slot == 0 || slot == ray_key_half() || slot == ray_key_three(); }
    // Dial and key dimensions, all proportional to the pitch.
    double long_ray() const { return 2.2 * pitch; }
    double short_ray() const { return 1.0 * pitch; }
    double key_ray() const { return 3.4 * pitch; }
};

DenseGeometry scalable_geometry(int col0, int row0, double p, int slots) {
    const double a = kTau * (3.0 * slots / 4.0 - 2.0) / slots;
    const double clear = std::max(0.375, p);
    return {14.5, 14.0, 3.5, 0.325, clear,
            4.0, 2.3, 0.4, clear, 29.0,
            col0 * p + 0.16 * p, row0 * p + 0.16 * p, (col0 + 2) * p - 0.16 * p, (row0 + 2) * p - 0.16 * p, p,
            kCenter, kScalableDialInner + 2.2 * p + 0.1 * p, p,
            kScalableDialInner, 3.4 * p, 0.22 * p,
            {{0.0, -1.0}, {-1.0, 0.0}, {std::sin(a), -std::cos(a)}},
            {1.0, 0.5, 0.75},
            0.34 * p};
}

std::unique_ptr<ScalableLayout> build_scalable_layout(int v) {
    auto L = std::make_unique<ScalableLayout>();
    const size_t i = static_cast<size_t>(v - 1);
    L->version = v; L->grid = kScalableGrid[i]; L->slots = kScalableSlots[i]; L->rings = kScalableRings[i];
    L->col0 = kScalableGridCol[i]; L->row0 = kScalableGridRow[i]; L->pitch = 29.0 / L->grid;
    L->geom = scalable_geometry(L->col0, L->row0, L->pitch, L->slots);
    const double p = L->pitch;
    for (int row = 0; row < L->grid; ++row) for (int col = 0; col < L->grid; ++col) {
        const double x = (col + .5) * p, y = (row + .5) * p;
        if (dense_site_margin(L->geom, x, y) <= 0.0) continue;
        L->nodes.push_back({0, x, y, -1, -1, site_identity(0, static_cast<uint32_t>(row), static_cast<uint32_t>(col))});
    }
    for (int slot = 0; slot < L->slots; ++slot)
        if (!L->reserved_ray(slot)) L->nodes.push_back({1, 0, 0, -1, slot, site_identity(1, 0, static_cast<uint32_t>(slot))});
    const int key = L->ring_key_slot();
    for (int ring = 0; ring < L->rings; ++ring) for (int slot = 0; slot < L->slots; ++slot) {
        if (L->reserved_ray(slot) || (ring == L->rings - 1 && slot >= key && slot <= key + 15)) continue;
        const double r = L->ring_radius(ring), a = kTau * slot / L->slots;
        L->nodes.push_back({2, kCenter + r * std::sin(a), kCenter - r * std::cos(a), ring, slot,
                            site_identity(2, static_cast<uint32_t>(ring), static_cast<uint32_t>(slot))});
    }
    order_sites(L->nodes, site_hash(0x434b5030u ^ static_cast<uint32_t>(v)));  // "CKP0" keyed by version
    L->white = whitening_bits(0x434b5730u + static_cast<uint32_t>(v), L->nodes.size());
    L->codeword_bytes = L->nodes.size() / 8;
    L->ring_grid.assign(static_cast<size_t>(L->rings) * L->slots, -1);
    for (size_t n = 0; n < L->nodes.size(); ++n)
        if (L->nodes[n].kind == 2) L->ring_grid[static_cast<size_t>(L->nodes[n].ring) * L->slots + L->nodes[n].slot] = static_cast<int>(n);
    return L;
}

const ScalableLayout &scalable_layout(int v) {
    static std::array<std::once_flag, kScalableVersions> once;
    static std::array<std::unique_ptr<ScalableLayout>, kScalableVersions> cache;
    const size_t i = static_cast<size_t>(v - 1);
    std::call_once(once[i], [&] { cache[i] = build_scalable_layout(v); });
    return *cache[i];
}

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

size_t scalable_capacity(int v, int level) {
    if (v < 1 || v > kScalableVersions || level < 0 || level > 3) return 0;
    const size_t k = scalable_data_bytes(scalable_layout(v).codeword_bytes, level);
    return k > kScalableOverheadBytes ? std::min<size_t>(k - kScalableOverheadBytes, 65535) : 0;
}

bool scalable_site_value(const ScalableLayout &L, const ckey_scalable_symbol *s, size_t i) {
    const bool data = i < L.codeword_bytes * 8 && ((s->codeword[i / 8] >> (7 - i % 8)) & 1u) != 0;
    return data != (L.white[i] != 0);
}

// Contrast/agreement fit of fixed samples (marks expected printed, empties
// expected clear); invalid if any sample leaves the image.
KeyFit sample_fit(const std::vector<double> &marks, const std::vector<double> &empty) {
    if (marks.empty() || empty.empty() || std::any_of(marks.begin(), marks.end(), [](double v) { return v < 0; }) ||
        std::any_of(empty.begin(), empty.end(), [](double v) { return v < 0; })) return {};
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
    const double p = L.pitch, rk = L.ring_radius(L.rings - 1), S = L.slots;
    std::vector<double> marks, empty;
    for (int r = 0; r < 2; ++r) for (int c = 0; c < 2; ++c)
        marks.push_back(gray_sample(gray, width, height, stride, h, (L.col0 + c + .5) * p, (L.row0 + r + .5) * p, 0.12 * p));
    const int k = L.ring_key_slot();
    for (double slot : {k + 0.0, k + 2.0, k + 6.0, k + 11.5}) marks.push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, rk, slot, S, 0.05 * p));
    marks.push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 3.0 * p, 0, S, 0.04 * p));
    marks.push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 1.3 * p, L.ray_key_half(), S, 0.04 * p));
    marks.push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 2.3 * p, L.ray_key_three(), S, 0.04 * p));
    empty.push_back(gray_sample(gray, width, height, stride, h, (L.col0 - 1 + .5) * p, (L.row0 + .5) * p, 0.12 * p));
    empty.push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, rk, k + 4.0, S, 0.05 * p));
    empty.push_back(polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 2.6 * p, L.ray_key_half(), S, 0.04 * p));
    return sample_fit(marks, empty);
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

int best_scalable_version(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8], double &score) {
    int best_v = 0; score = 0;
    for (int v = 1; v <= kScalableVersions; ++v) {
        const double s = scalable_version_score(scalable_layout(v), gray, width, height, stride, quad);
        if (s > score) { score = s; best_v = v; }
    }
    return best_v;
}

std::string fixed4(double v) { char b[32]; std::snprintf(b, sizeof(b), "%.4f", v); return b; }

}  // namespace

extern "C" {

uint32_t ckey_scalable_modules_across(uint32_t version) {
    return version >= 1 && version <= CKEY_SCALABLE_VERSION_COUNT ? static_cast<uint32_t>(kScalableGrid[version - 1]) : 0u;
}

size_t ckey_scalable_capacity(uint32_t version, ckey_ecc_level level) {
    return scalable_capacity(static_cast<int>(version), static_cast<int>(level));
}

ckey_status ckey_encode_scalable(const uint8_t *payload, size_t payload_len, uint32_t version, ckey_ecc_level level,
                                 ckey_scalable_symbol *out, ckey_diagnostics *diag) {
    if ((!payload && payload_len) || !out || static_cast<int>(level) < 0 || static_cast<int>(level) > 3 || version > CKEY_SCALABLE_VERSION_COUNT) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 0 payload, version, level, or output"); return CKEY_INVALID_ARGUMENT;
    }
    int v = static_cast<int>(version);
    if (v == 0) { for (v = 1; v <= kScalableVersions && scalable_capacity(v, level) < payload_len; ++v) {} }
    if (v > kScalableVersions || scalable_capacity(v, level) < payload_len) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "payload exceeds the selected Profile 0 version and level"); return CKEY_INVALID_ARGUMENT;
    }
    const ScalableLayout &L = scalable_layout(v);
    if (L.codeword_bytes > CKEY_SCALABLE_MAX_CODEWORD_BYTES) { set_status(diag, CKEY_INTERNAL_ERROR, "Profile 0 layout exceeds codeword storage"); return CKEY_INTERNAL_ERROR; }
    const auto blocks = scalable_blocks(L.codeword_bytes, level);
    std::vector<uint8_t> framed(scalable_data_bytes(L.codeword_bytes, level), 0);
    framed[0] = 0xc7; framed[1] = 0x4b; framed[2] = CKEY_SCALABLE_FORMAT_VERSION; framed[3] = 0;
    framed[4] = static_cast<uint8_t>(v); framed[5] = static_cast<uint8_t>(level);
    framed[6] = static_cast<uint8_t>(payload_len >> 8); framed[7] = static_cast<uint8_t>(payload_len);
    if (payload_len) std::memcpy(framed.data() + kScalableHeaderBytes, payload, payload_len);
    const size_t crc_index = kScalableHeaderBytes + payload_len; const uint32_t crc = crc32c(framed.data(), crc_index);
    for (int b = 0; b < 4; ++b) framed[crc_index + b] = static_cast<uint8_t>(crc >> (24 - 8 * b));
    fill_padding(framed.data(), crc_index + 4, framed.size(), crc_index + 4);
    std::vector<std::vector<uint8_t>> coded; size_t offset = 0;
    for (const auto &b : blocks) {
        std::vector<uint8_t> work(b.n, 0);
        std::copy_n(framed.begin() + static_cast<std::ptrdiff_t>(offset), b.k, work.begin()); offset += b.k;
        rs_encode_generic(work, b.k, b.n - b.k);
        coded.push_back(std::move(work));
    }
    std::memset(out, 0, sizeof(*out));
    out->abi_version = CKEY_ABI_VERSION; out->version = static_cast<uint32_t>(v); out->ecc_level = static_cast<uint32_t>(level);
    out->payload_length = static_cast<uint32_t>(payload_len); out->codeword_length = static_cast<uint32_t>(L.codeword_bytes);
    // Byte-interleave the blocks so local damage spreads across all of them.
    size_t pos = 0;
    for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) out->codeword[pos++] = coded[b][j];
    char message[96]; std::snprintf(message, sizeof(message), "encoded Profile 0 v%d level %c; %zu RS blocks", v, "LMQH"[level], blocks.size());
    set_status(diag, CKEY_OK, message);
    return CKEY_OK;
}

ckey_status ckey_decode_scalable(const uint8_t *codeword, size_t codeword_len, uint32_t version, uint8_t *out_payload,
                                 size_t out_capacity, size_t *out_payload_len, ckey_diagnostics *diag) {
    if (!codeword || !out_payload_len || version < 1 || version > CKEY_SCALABLE_VERSION_COUNT) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 0 codeword or version"); return CKEY_INVALID_ARGUMENT;
    }
    const ScalableLayout &L = scalable_layout(static_cast<int>(version));
    if (codeword_len != L.codeword_bytes) { set_status(diag, CKEY_INVALID_ARGUMENT, "codeword length does not match the Profile 0 version"); return CKEY_INVALID_ARGUMENT; }
    for (int level = 0; level < 4; ++level) {
        const auto blocks = scalable_blocks(L.codeword_bytes, level);
        std::vector<std::vector<uint8_t>> work(blocks.size());
        for (size_t b = 0; b < blocks.size(); ++b) work[b].resize(blocks[b].n);
        size_t pos = 0;
        for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) work[b][j] = codeword[pos++];
        std::vector<uint8_t> framed; uint32_t corrections = 0; bool ok = true;
        for (size_t b = 0; b < blocks.size() && ok; ++b) {
            uint32_t fixed = 0;
            ok = rs_correct_generic(work[b], blocks[b].n - blocks[b].k, fixed);
            corrections += fixed;
            framed.insert(framed.end(), work[b].begin(), work[b].begin() + static_cast<std::ptrdiff_t>(blocks[b].k));
        }
        if (!ok || framed.size() < kScalableOverheadBytes) continue;
        if (framed[0] != 0xc7 || framed[1] != 0x4b || framed[2] != CKEY_SCALABLE_FORMAT_VERSION || framed[3] != 0 ||
            framed[4] != version || framed[5] != level) continue;
        const size_t length = (static_cast<size_t>(framed[6]) << 8) | framed[7];
        if (kScalableOverheadBytes + length > framed.size()) continue;
        const size_t crc_index = kScalableHeaderBytes + length;
        uint32_t expected = 0; for (int b = 0; b < 4; ++b) expected = (expected << 8) | framed[crc_index + b];
        if (crc32c(framed.data(), crc_index) != expected || !padding_matches(framed.data(), crc_index + 4, framed.size(), crc_index + 4)) continue;
        if (out_capacity < length || (!out_payload && length)) { set_status(diag, CKEY_INSUFFICIENT_BUFFER, "Profile 0 payload exceeds output capacity"); return CKEY_INSUFFICIENT_BUFFER; }
        if (length) std::memcpy(out_payload, framed.data() + kScalableHeaderBytes, length);
        *out_payload_len = length;
        char message[96]; std::snprintf(message, sizeof(message), "decoded Profile 0 v%u level %c", version, "LMQH"[level]);
        set_status(diag, CKEY_OK, message); if (diag) diag->corrected_symbols = corrections;
        return CKEY_OK;
    }
    set_status(diag, CKEY_UNCORRECTABLE, "no Profile 0 level passed Reed-Solomon, frame, and CRC checks"); return CKEY_UNCORRECTABLE;
}

double ckey_calibration_score_scalable_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                             const double quad_xy[8], uint32_t *out_version) {
    if (!valid_image_args(gray, width, height, stride, quad_xy)) return -1.0;
    double score = 0; const int v = best_scalable_version(gray, width, height, stride, quad_xy, score);
    if (out_version) *out_version = static_cast<uint32_t>(v);
    return score;
}

ckey_status ckey_decode_scalable_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad_xy[8],
                                       uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len, uint32_t *out_version,
                                       ckey_diagnostics *diag) {
    if (!out_payload || !out_payload_len || !valid_image_args(gray, width, height, stride, quad_xy)) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 0 image, quadrilateral, or output buffer"); return CKEY_INVALID_ARGUMENT;
    }
    // 1. The skeleton gates orientation. 2. Each version's keys are tested at
    //    that orientation; only agreeing versions are fully sampled, best
    //    first. 3. RS, the frame's version byte, and CRC-32C decide.
    struct Attempt { double score; int version; double h[8]; };
    std::vector<Attempt> attempts;
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        double h[8]; bool ok; orientation_homography(quad_xy, reverse, start, h, ok); if (!ok) continue;
        const KeyFit skeleton = scalable_skeleton_fit(gray, width, height, stride, h);
        if (!skeleton.valid || skeleton.evidence < kDenseKeyGate || skeleton.contrast < 14.0) continue;
        for (int v = 1; v <= kScalableVersions; ++v) {
            const KeyFit keys = scalable_version_fit(scalable_layout(v), gray, width, height, stride, h);
            if (!keys.valid || keys.evidence < 0.85) continue;
            Attempt a{keys.contrast * keys.evidence * keys.evidence, v, {}}; std::copy(h, h + 8, a.h);
            attempts.push_back(a);
        }
    }
    std::sort(attempts.begin(), attempts.end(), [](const Attempt &a, const Attempt &b) { return a.score > b.score; });
    if (attempts.size() > 6) attempts.resize(6);
    std::vector<uint8_t> candidate(65535);
    for (const auto &attempt : attempts) {
        const ScalableLayout &L = scalable_layout(attempt.version); const double *h = attempt.h; const double p = L.pitch;
        // References: the Grid Key (printed) and two never-printed points.
        std::vector<double> marks, empties;
        for (int r = 0; r < 2; ++r) for (int c = 0; c < 2; ++c)
            marks.push_back(gray_sample(gray, width, height, stride, h, (L.col0 + c + .5) * p, (L.row0 + r + .5) * p, 0.12 * p));
        empties.push_back(gray_sample(gray, width, height, stride, h, (L.col0 - 1 + .5) * p, (L.row0 + .5) * p, 0.12 * p));
        empties.push_back(gray_sample(gray, width, height, stride, h, 14.5, 1.0, 0.05));
        if (std::any_of(marks.begin(), marks.end(), [](double x) { return x < 0; }) || std::any_of(empties.begin(), empties.end(), [](double x) { return x < 0; })) continue;
        const double mark_mean = std::accumulate(marks.begin(), marks.end(), 0.0) / marks.size();
        const double empty_mean = std::accumulate(empties.begin(), empties.end(), 0.0) / empties.size();
        if (std::abs(mark_mean - empty_mean) < 14.0) continue;
        const size_t bits = L.codeword_bytes * 8;
        std::vector<double> level(bits); std::array<std::vector<double>, 3> classes; bool usable = true;
        for (size_t i = 0; i < bits && usable; ++i) {
            const auto &n = L.nodes[i];
            const double v = n.kind == 0 ? gray_sample(gray, width, height, stride, h, n.x, n.y, 0.12 * p)
                           : n.kind == 1 ? polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + 1.6 * p, n.slot, L.slots, 0.04 * p)
                                         : gray_sample(gray, width, height, stride, h, n.x, n.y, 0.05 * p);
            if (v < 0) { usable = false; break; }
            level[i] = (v - empty_mean) / (mark_mean - empty_mean);
            classes[n.kind].push_back(level[i]);
        }
        if (!usable) continue;
        const std::array<double, 3> cut = {class_threshold(classes[0]), class_threshold(classes[1]), class_threshold(classes[2])};
        std::vector<uint8_t> printed(L.nodes.size());
        for (size_t i = 0; i < L.nodes.size(); ++i) printed[i] = i < bits ? (level[i] > cut[L.nodes[i].kind]) : L.white[i];
        refine_ring_context(L.ring_grid, L.rings, L.slots, bits, L.white, level, cut[2], printed);
        std::vector<uint8_t> cw(L.codeword_bytes, 0);
        for (size_t i = 0; i < bits; ++i) if ((printed[i] != 0) != (L.white[i] != 0)) cw[i / 8] |= static_cast<uint8_t>(1u << (7 - i % 8));
        size_t length = 0; ckey_diagnostics local{};
        if (ckey_decode_scalable(cw.data(), cw.size(), static_cast<uint32_t>(attempt.version), candidate.data(), candidate.size(), &length, &local) != CKEY_OK) continue;
        if (out_capacity < length) { set_status(diag, CKEY_INSUFFICIENT_BUFFER, "Profile 0 output buffer is too small"); return CKEY_INSUFFICIENT_BUFFER; }
        std::memcpy(out_payload, candidate.data(), length); *out_payload_len = length;
        if (out_version) *out_version = static_cast<uint32_t>(attempt.version);
        set_status(diag, CKEY_OK, local.message); if (diag) diag->corrected_symbols = local.corrected_symbols;
        return CKEY_OK;
    }
    set_status(diag, CKEY_UNCORRECTABLE, attempts.empty() ? "no Profile 0 version's keys matched" : "no Profile 0 version passed Reed-Solomon and CRC");
    return CKEY_UNCORRECTABLE;
}

}  // extern "C"

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
double logo_quiet_radius(const ScalableLayout &L) { return kScalableRingInner - 0.2 * L.pitch - 0.15; }
// Keep the outermost ring (which carries the Ring Key) fully visible.
double logo_max_radius(const ScalableLayout &L) { return L.ring_radius(L.rings - 1) - 0.2 * L.pitch - 0.25; }

double logo_load(const ScalableLayout &L, int level, double radius) {
    if (radius <= logo_quiet_radius(L)) return 0.0;
    const auto blocks = scalable_blocks(L.codeword_bytes, level);
    std::vector<size_t> block_of(L.codeword_bytes); size_t pos = 0;
    for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) block_of[pos++] = b;
    std::vector<uint8_t> hidden(L.codeword_bytes, 0);
    for (size_t i = 0; i < L.codeword_bytes * 8; ++i) {
        const auto &n = L.nodes[i];
        if (n.kind != 2) continue;  // only ring marks can lie under an allowed logo
        if (std::hypot(n.x - kCenter, n.y - kCenter) - 0.2 * L.pitch <= radius + 0.05) hidden[i / 8] = 1;
    }
    std::vector<size_t> per_block(blocks.size(), 0);
    for (size_t byte = 0; byte < hidden.size(); ++byte) per_block[block_of[byte]] += hidden[byte];
    double worst = 0;
    for (size_t b = 0; b < blocks.size(); ++b) worst = std::max(worst, per_block[b] / std::max(1.0, (blocks[b].n - blocks[b].k) / 2.0));
    return worst;
}

std::string check_style(const ScalableLayout &L, const ckey_scalable_symbol &sym, const ckey_style &st, double &min_contrast, double &load) {
    min_contrast = 0; load = 0;
    const auto finite01 = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
    if (st.modules < CKEY_MODULE_ROUNDED || st.modules > CKEY_MODULE_DIAMOND) return "unknown module shape";
    if (st.rings < CKEY_RING_JOINED || st.rings > CKEY_RING_DOTS) return "unknown ring style";
    if (st.rays < CKEY_RAY_ROUND || st.rays > CKEY_RAY_FLAT) return "unknown ray cap";
    if (st.accent_count > 4 || st.accent_percent > 100) return "accents: at most 4 colours and 100%";
    if (!finite01(st.bevel) || !finite01(st.shadow) || !finite01(st.texture)) return "bevel, shadow, and texture must be 0..1";
    std::vector<ckey_rgb> inks = {st.ink, st.ink_shade};
    if (st.accent_percent > 0) for (uint32_t i = 0; i < st.accent_count; ++i) inks.push_back(st.accents[i]);
    const ckey_rgb backgrounds[2] = {st.background, st.background_edge};
    min_contrast = 1e9; int polarity = 0;
    for (const auto &bg : backgrounds) for (const auto &ink : inks) {
        const double ratio = contrast_ratio(ink, bg);
        min_contrast = std::min(min_contrast, ratio);
        char why[96];
        if (ratio < 3.0) { std::snprintf(why, sizeof(why), "ink %s on background %s is %.1f:1; need 3:1", hex_colour(ink).c_str(), hex_colour(bg).c_str(), ratio); return why; }
        const int side = relative_luminance(ink) > relative_luminance(bg) ? 1 : -1;
        if (polarity && side != polarity) return "all inks must be lighter than the background, or all darker";
        polarity = side;
    }
    // Dark ink on a light ground: shadows darken the gaps and highlights
    // lighten the marks, both eating into contrast.
    if (polarity < 0 && st.shadow > 0.4) return "dark-on-light symbols allow shadow up to 0.4";
    if (polarity < 0 && st.bevel > 0.5) return "dark-on-light symbols allow bevel up to 0.5";
    const bool has_logo = st.logo_href && *st.logo_href;
    if (!std::isfinite(st.logo_radius) || st.logo_radius < 0) return "logo radius must be 0 or positive";
    if (has_logo) {
        const double radius = st.logo_radius > 0 ? st.logo_radius : logo_quiet_radius(L);
        char why[96];
        if (radius > logo_max_radius(L)) { std::snprintf(why, sizeof(why), "logo radius %.2f exceeds %.2f (it would hide the Ring Key)", radius, logo_max_radius(L)); return why; }
        if (radius > logo_quiet_radius(L) && !st.logo_plate) return "a logo larger than the empty centre needs a plate";
        load = logo_load(L, static_cast<int>(sym.ecc_level), radius);
        if (load > 0.5) { std::snprintf(why, sizeof(why), "logo hides %.0f%% of a block's correction (max 50%%); raise the level or shrink it", 100 * load); return why; }
    }
    return {};
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

extern "C" {

void ckey_style_default(ckey_style *st) {
    if (!st) return;
    std::memset(st, 0, sizeof(*st));
    st->background = st->background_edge = {5, 5, 5};
    st->ink = st->ink_shade = {0xf4, 0xf1, 0xe8};
    st->modules = CKEY_MODULE_ROUNDED; st->rings = CKEY_RING_JOINED; st->rays = CKEY_RAY_ROUND;
}

double ckey_scalable_logo_max_radius(uint32_t version) {
    return version >= 1 && version <= CKEY_SCALABLE_VERSION_COUNT ? logo_max_radius(scalable_layout(static_cast<int>(version))) : -1.0;
}

double ckey_scalable_logo_load(const ckey_scalable_symbol *s, double radius) {
    if (!s || s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT || s->ecc_level > 3 || !std::isfinite(radius) || radius < 0) return -1.0;
    return logo_load(scalable_layout(static_cast<int>(s->version)), static_cast<int>(s->ecc_level), radius);
}

ckey_status ckey_check_scalable_style(const ckey_scalable_symbol *s, const ckey_style *st, ckey_diagnostics *diag) {
    if (!s || !st || s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT || s->ecc_level > 3) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "style check needs a Profile 0 symbol and a style"); return CKEY_INVALID_ARGUMENT;
    }
    double contrast = 0, load = 0;
    const std::string problem = check_style(scalable_layout(static_cast<int>(s->version)), *s, *st, contrast, load);
    if (!problem.empty()) { set_status(diag, CKEY_INVALID_ARGUMENT, problem.c_str()); return CKEY_INVALID_ARGUMENT; }
    char message[96]; std::snprintf(message, sizeof(message), "style OK: contrast >= %.1f:1; logo uses %.0f%% of block correction", contrast, 100 * load);
    set_status(diag, CKEY_OK, message);
    return CKEY_OK;
}

ckey_status ckey_render_scalable_svg(const ckey_scalable_symbol *s, uint32_t scale, char *out, size_t cap, size_t *required) {
    ckey_style st; ckey_style_default(&st);
    return ckey_render_scalable_styled_svg(s, &st, scale, out, cap, required);
}

ckey_status ckey_render_scalable_styled_svg(const ckey_scalable_symbol *s, const ckey_style *st, uint32_t scale,
                                            char *out, size_t cap, size_t *required) {
    if (!s || !st || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE || s->abi_version != CKEY_ABI_VERSION ||
        s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT) return CKEY_INVALID_ARGUMENT;
    const ScalableLayout &L = scalable_layout(static_cast<int>(s->version));
    if (s->codeword_length != L.codeword_bytes) return CKEY_INVALID_ARGUMENT;
    double contrast = 0, load = 0;
    if (!check_style(L, *s, *st, contrast, load).empty()) return CKEY_INVALID_ARGUMENT;
    const double p = L.pitch, dot = 0.2 * p;
    const std::string dot_stroke = fixed4(2 * dot), cap_style = st->rays == CKEY_RAY_FLAT ? "butt" : "round";
    const bool sheen = !same_colour(st->ink, st->ink_shade), vignette = !same_colour(st->background, st->background_edge);
    const bool depth = st->bevel > 0 || st->shadow > 0;
    const std::string ink = sheen ? "url(#ckey-ink)" : hex_colour(st->ink);
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    std::ostringstream c; c << std::fixed << std::setprecision(4);
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 31 * scale << "\" height=\"" << 31 * scale << "\" viewBox=\"-1 -1 31 31\" role=\"img\" aria-label=\"CKey scalable symbol profile 0 version " << s->version << "\">";
    // ---- Materials ----
    if (sheen || vignette || depth || st->texture > 0 || (st->logo_href && *st->logo_href)) {
        o << "<defs>";
        if (sheen) o << "<linearGradient id=\"ckey-ink\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" y1=\"0\" x2=\"29\" y2=\"29\"><stop offset=\"0\" stop-color=\"" << hex_colour(st->ink)
                     << "\"/><stop offset=\"0.55\" stop-color=\"" << hex_colour(st->ink_shade) << "\"/><stop offset=\"1\" stop-color=\"" << hex_colour(st->ink) << "\"/></linearGradient>";
        if (vignette) o << "<radialGradient id=\"ckey-bg\" gradientUnits=\"userSpaceOnUse\" cx=\"14.5\" cy=\"14.5\" r=\"21\"><stop offset=\"0\" stop-color=\"" << hex_colour(st->background)
                        << "\"/><stop offset=\"1\" stop-color=\"" << hex_colour(st->background_edge) << "\"/></radialGradient>";
        if (st->texture > 0) o << "<filter id=\"ckey-grain\" x=\"0\" y=\"0\" width=\"100%\" height=\"100%\"><feTurbulence type=\"fractalNoise\" baseFrequency=\"1.7\" numOctaves=\"2\" seed=\"7\"/><feColorMatrix type=\"saturate\" values=\"0\"/></filter>";
        if (depth) {
            o << "<filter id=\"ckey-depth\" x=\"-2%\" y=\"-2%\" width=\"104%\" height=\"104%\" color-interpolation-filters=\"sRGB\">";
            std::string base = "SourceGraphic";
            if (st->shadow > 0) { o << "<feDropShadow dx=\"0.05\" dy=\"0.09\" stdDeviation=\"0.07\" flood-color=\"#000000\" flood-opacity=\"" << 0.75 * st->shadow << "\" result=\"shadowed\"/>"; base = "shadowed"; }
            if (st->bevel > 0)
                o << "<feGaussianBlur in=\"SourceAlpha\" stdDeviation=\"0.045\" result=\"bump\"/><feSpecularLighting in=\"bump\" surfaceScale=\"3\" specularConstant=\"" << 1.1 * st->bevel
                  << "\" specularExponent=\"16\" lighting-color=\"#ffffff\" result=\"spec\"><feDistantLight azimuth=\"225\" elevation=\"40\"/></feSpecularLighting>"
                  << "<feComposite in=\"spec\" in2=\"SourceAlpha\" operator=\"in\" result=\"shine\"/><feComposite in=\"shine\" in2=\"" << base << "\" operator=\"arithmetic\" k1=\"0\" k2=\"1\" k3=\"1\" k4=\"0\"/>";
            o << "</filter>";
        }
        if (st->logo_href && *st->logo_href) {
            const double r = st->logo_radius > 0 ? st->logo_radius : logo_quiet_radius(L);
            o << "<clipPath id=\"ckey-logo-clip\"><circle cx=\"14.5\" cy=\"14.5\" r=\"" << r << "\"/></clipPath>";
        }
        o << "</defs>";
    }
    o << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" fill=\"" << (vignette ? "url(#ckey-bg)" : hex_colour(st->background)) << "\"/>";
    if (st->texture > 0) o << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" filter=\"url(#ckey-grain)\" opacity=\"" << 0.18 * st->texture << "\"/>";
    o << "<g fill=\"" << ink << "\" stroke=\"" << ink << "\"" << (depth ? " filter=\"url(#ckey-depth)\"" : "") << ">"
      << "<g class=\"ckey-frame\"><rect x=\"0.5\" y=\"0.5\" width=\"28\" height=\"28\" rx=\"3.5\" fill=\"none\" stroke-width=\"0.65\"/></g>"
      << "<g class=\"ckey-finders\">";
    svg_corner_finders(o, 29.0, 4.0, {2.30, 0.0, 0.0}, .80, .95);
    o << "</g><g class=\"ckey-grid-key\">";
    for (int r = 0; r < 2; ++r) for (int cc = 0; cc < 2; ++cc) emit_module(o, (L.col0 + cc + .5) * p, (L.row0 + r + .5) * p, p, st->modules, "");
    o << "</g><g class=\"ckey-polar-keys\">";
    for (const auto &[slot, fraction] : {std::pair{0, 1.0}, std::pair{L.ray_key_half(), 0.5}, std::pair{L.ray_key_three(), 0.75}}) {
        const double a = kTau * slot / L.slots, r0 = kScalableDialInner, r1 = r0 + L.key_ray() * fraction;
        o << "<path d=\"M " << kCenter + r0 * std::sin(a) << ' ' << kCenter - r0 * std::cos(a) << " L " << kCenter + r1 * std::sin(a) << ' ' << kCenter - r1 * std::cos(a)
          << "\" fill=\"none\" stroke-width=\"" << 0.44 * p << "\" stroke-linecap=\"" << cap_style << "\"/>";
    }
    // The Ring Key keeps its dot/gap/run pattern in every style: it is a key.
    const double rk = L.ring_radius(L.rings - 1); const int k = L.ring_key_slot();
    for (int slot : {k, k + 2, k + 6}) { const double a = kTau * slot / L.slots; svg_circle(o, kCenter + rk * std::sin(a), kCenter - rk * std::cos(a), dot); }
    svg_ring_run(o, kCenter, rk, k + 7.5, k + 15.5, L.slots, dot, dot_stroke.c_str());
    o << "</g><g class=\"ckey-data-polar\">";
    std::vector<uint8_t> ring_bits(static_cast<size_t>(L.rings) * L.slots, 0);
    std::vector<int> ring_accent(static_cast<size_t>(L.rings) * L.slots, -1);
    for (size_t i = 0; i < L.nodes.size(); ++i) {
        const auto &n = L.nodes[i]; const bool bit = scalable_site_value(L, s, i);
        const int accent = accent_of(n.id, *st);
        if (n.kind == 0) { if (bit) emit_module(c, n.x, n.y, p, st->modules, accent >= 0 ? hex_colour(st->accents[accent]) : ""); }
        else if (n.kind == 1) {
            const double a = kTau * n.slot / L.slots, r0 = kScalableDialInner, r1 = r0 + (bit ? L.long_ray() : L.short_ray());
            o << "<path d=\"M " << kCenter + r0 * std::sin(a) << ' ' << kCenter - r0 * std::cos(a) << " L " << kCenter + r1 * std::sin(a) << ' ' << kCenter - r1 * std::cos(a)
              << "\" fill=\"none\" stroke-width=\"" << 0.2 * p << "\" stroke-linecap=\"" << cap_style << "\"/>";
        } else { ring_bits[static_cast<size_t>(n.ring) * L.slots + n.slot] = bit; ring_accent[static_cast<size_t>(n.ring) * L.slots + n.slot] = accent; }
    }
    for (int ring = 0; ring < L.rings; ++ring) {
        const double radius = L.ring_radius(ring);
        for (int slot = 0; slot < L.slots;) {
            const size_t at = static_cast<size_t>(ring) * L.slots + slot;
            if (!ring_bits[at]) { ++slot; continue; }
            int end = slot + 1;
            if (st->rings == CKEY_RING_JOINED) while (end < L.slots && ring_bits[static_cast<size_t>(ring) * L.slots + end]) ++end;
            const int accent = ring_accent[at];
            const std::string colour = accent >= 0 ? hex_colour(st->accents[accent]) : "";
            if (end - slot == 1) {
                const double a = kTau * slot / L.slots;
                o << "<circle cx=\"" << kCenter + radius * std::sin(a) << "\" cy=\"" << kCenter - radius * std::cos(a) << "\" r=\"" << dot << "\" stroke=\"none\""
                  << (colour.empty() ? "" : " fill=\"" + colour + "\"") << "/>";
            } else if (colour.empty()) svg_ring_run(o, kCenter, radius, slot - .5, end - .5, L.slots, dot, dot_stroke.c_str());
            else { o << "<g stroke=\"" << colour << "\">"; svg_ring_run(o, kCenter, radius, slot - .5, end - .5, L.slots, dot, dot_stroke.c_str()); o << "</g>"; }
            slot = end;
        }
    }
    o << "</g><g class=\"ckey-data-cartesian\">" << c.str() << "</g></g>";
    if (st->logo_href && *st->logo_href) {
        const double r = st->logo_radius > 0 ? st->logo_radius : logo_quiet_radius(L);
        if (st->logo_plate) o << "<g class=\"ckey-logo-plate\"><circle cx=\"14.5\" cy=\"14.5\" r=\"" << r + 0.1 << "\" fill=\"" << hex_colour(st->background) << "\"/></g>";
        o << "<image href=\"" << xml_escape(st->logo_href) << "\" x=\"" << 14.5 - r << "\" y=\"" << 14.5 - r << "\" width=\"" << 2 * r << "\" height=\"" << 2 * r
          << "\" preserveAspectRatio=\"xMidYMid meet\" clip-path=\"url(#ckey-logo-clip)\"/>";
    }
    o << "</svg>";
    const std::string text = o.str(); *required = text.size() + 1;
    if (!out || cap < *required) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out, text.c_str(), *required);
    return CKEY_OK;
}

}  // extern "C"

// ---------------------------------------------------------------------------
// Full-frame localization (moved from the iPhone reader so it is shared and
// testable). Border candidates are connected components of the continuous
// frame stroke; each is fitted with a perspective quadrilateral, then refined
// against a profile's fixed keys. Neither step can accept a payload.
// ---------------------------------------------------------------------------
namespace {

struct Point2 { double x, y; };

uint8_t frame_threshold(const uint8_t *gray, size_t width, size_t height, size_t stride, bool dark_marks) {
    std::array<size_t, 256> histogram{}; size_t samples = 0;
    for (size_t y = 0; y < height; y += 8) for (size_t x = 0; x < width; x += 8) { ++histogram[gray[y * stride + x]]; ++samples; }
    if (!samples) return dark_marks ? 92 : 164;
    const auto percentile = [&](double fraction) {
        const size_t target = static_cast<size_t>(static_cast<double>(samples - 1) * fraction); size_t total = 0;
        for (int v = 0; v < 256; ++v) { total += histogram[v]; if (total > target) return v; }
        return 255;
    };
    const int low = percentile(0.10), high = percentile(0.90);
    if (high - low < 32) return dark_marks ? 92 : 164;
    const double contrast = high - low;
    const double value = dark_marks ? low + contrast * 0.32 : high - contrast * 0.32;
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

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

double field_score(int field, const uint8_t *gray, size_t width, size_t height, size_t stride, const double q[8]) {
    if (field == CKEY_FIELD_SCALABLE) return frame_edge_score(gray, width, height, stride, q, 4.0);
    if (field == CKEY_FIELD_LOCATOR) return ckey_calibration_score_gray8(gray, width, height, stride, q);
    if (field == CKEY_FIELD_TEXT) return ckey_calibration_score_text_gray8(gray, width, height, stride, q);
    return ckey_calibration_score_extended_gray8(gray, width, height, stride, q);
}

}  // namespace

extern "C" {

size_t ckey_find_borders_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                               double *out_quads, size_t max_quads) {
    if (!gray || !out_quads || max_quads == 0 || width < 16 || height < 16 || stride < width ||
        height > std::numeric_limits<size_t>::max() / stride) return 0;
    // About 960 x 540 samples at any capture size (step 2 at 1080p, 4 at 4K).
    const size_t step = std::max<size_t>(2, 2 * std::max(width, height) / 1920);
    const size_t sw = (width + step - 1) / step, sh = (height + step - 1) / step;
    const size_t min_side = std::min(width, height);
    struct Candidate { std::array<double, 8> quad; double rank; };
    std::vector<Candidate> candidates;
    std::vector<uint8_t> visited(sw * sh);
    std::vector<size_t> queue;
    for (int polarity = 0; polarity < 2; ++polarity) {
        const bool dark = polarity == 1;
        const uint8_t t = frame_threshold(gray, width, height, stride, dark);
        const auto is_mark = [&](size_t sx, size_t sy) {
            const uint8_t v = gray[std::min(sy * step, height - 1) * stride + std::min(sx * step, width - 1)];
            return dark ? v <= t : v >= t;
        };
        std::fill(visited.begin(), visited.end(), 0);
        for (size_t sy = 0; sy < sh; ++sy) for (size_t sx = 0; sx < sw; ++sx) {
            const size_t start = sy * sw + sx;
            if (visited[start] || !is_mark(sx, sy)) continue;
            visited[start] = 1; queue.assign(1, start);
            size_t min_x = sx, max_x = sx, min_y = sy, max_y = sy;
            for (size_t head = 0; head < queue.size(); ++head) {
                const size_t x = queue[head] % sw, y = queue[head] / sw;
                min_x = std::min(min_x, x); max_x = std::max(max_x, x); min_y = std::min(min_y, y); max_y = std::max(max_y, y);
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const long nx = static_cast<long>(x) + dx, ny = static_cast<long>(y) + dy;
                    if (nx < 0 || ny < 0 || nx >= static_cast<long>(sw) || ny >= static_cast<long>(sh)) continue;
                    const size_t n = static_cast<size_t>(ny) * sw + static_cast<size_t>(nx);
                    if (!visited[n] && is_mark(static_cast<size_t>(nx), static_cast<size_t>(ny))) { visited[n] = 1; queue.push_back(n); }
                }
            }
            const size_t box_w = (max_x - min_x + 1) * step, box_h = (max_y - min_y + 1) * step, side = std::min(box_w, box_h);
            // The whole border must be in view: a component touching the frame
            // edge is clipped (or is a display bezel). This replaces a fixed
            // fraction-of-frame cap that rejected large rotated symbols.
            if (queue.size() < 24 || side < 20 * step || min_x == 0 || min_y == 0 || max_x + 1 >= sw || max_y + 1 >= sh) continue;
            const double aspect = static_cast<double>(box_w) / box_h;
            // A rotated square's axis-aligned box stays square; a filled
            // panel or blob fails the fill test.
            const double fill = static_cast<double>(queue.size()) / static_cast<double>((max_x - min_x + 1) * (max_y - min_y + 1));
            if (aspect <= 0.72 || aspect >= 1.38 || fill >= 0.38) continue;
            std::vector<Point2> points; points.reserve(queue.size());
            for (size_t index : queue) points.push_back({static_cast<double>((index % sw) * step), static_cast<double>((index / sw) * step)});
            Candidate c{};
            if (!fit_border_quad(points, static_cast<double>(step), c.quad)) continue;
            c.rank = border_rank(std::abs(std::log(aspect)), fill, static_cast<double>(side) / min_side);
            candidates.push_back(c);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.rank > b.rank; });
    // Every profile shares one skeleton in normalized coordinates (frame
    // stroke, finder arcs, orientation split; Profile 3 is the same shape at
    // 4x). Re-rank the leading shape candidates by how well that skeleton
    // fits, so a symbol's own interior components (dense payload clusters,
    // rings) cannot crowd its true border out of the returned set.
    if (candidates.size() > 24) candidates.resize(24);
    for (auto &c : candidates) {
        double q[8]; double cx = 0, cy = 0;
        for (int i = 0; i < 4; ++i) { cx += c.quad[2 * i] / 4; cy += c.quad[2 * i + 1] / 4; }
        for (int i = 0; i < 4; ++i) { q[2 * i] = cx + (c.quad[2 * i] - cx) * kBorderToField; q[2 * i + 1] = cy + (c.quad[2 * i + 1] - cy) * kBorderToField; }
        c.rank = valid_quad(q) ? scalable_skeleton_score(gray, width, height, stride, q) + 1e-3 * c.rank : -1;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.rank > b.rank; });
    const size_t count = std::min(max_quads, candidates.size());
    for (size_t i = 0; i < count; ++i) std::copy(candidates[i].quad.begin(), candidates[i].quad.end(), out_quads + 8 * i);
    return count;
}

double ckey_refine_quad_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                              const double border_quad[8], ckey_field field, double out_quad[8]) {
    if (!out_quad || (field != CKEY_FIELD_SCALABLE && field != CKEY_FIELD_LOCATOR && field != CKEY_FIELD_TEXT && field != CKEY_FIELD_EXTENDED) ||
        !valid_image_args(gray, width, height, stride, border_quad)) return -1.0;
    double cx = 0, cy = 0; for (int i = 0; i < 4; ++i) { cx += border_quad[2 * i] / 4; cy += border_quad[2 * i + 1] / 4; }
    const double side = (std::hypot(border_quad[2] - border_quad[0], border_quad[3] - border_quad[1]) +
                         std::hypot(border_quad[6] - border_quad[0], border_quad[7] - border_quad[1])) / 2;
    const auto similar = [&](double scale, double angle, double ox, double oy, double q[8]) {
        const double c = std::cos(angle), s = std::sin(angle);
        for (int i = 0; i < 4; ++i) {
            const double dx = border_quad[2 * i] - cx, dy = border_quad[2 * i + 1] - cy;
            q[2 * i] = cx + ox + scale * (c * dx - s * dy); q[2 * i + 1] = cy + oy + scale * (s * dx + c * dy);
        }
    };
    // Coordinate descent over (scale, rotation, offset), then per-corner
    // projective moves. Steps halve each round, so it converges at any pixel
    // scale without a symbol-size table.
    // Objective at a given search step (pixels). Profile 0 aligns on the
    // frame's edges with a sample separation that follows the step.
    const auto objective = [&](const double q[8], double step_px) {
        return field == CKEY_FIELD_SCALABLE ? frame_edge_score(gray, width, height, stride, q, std::clamp(step_px, 0.5, 6.0))
                                            : field_score(field, gray, width, height, stride, q);
    };
    double scale = kBorderToField, angle = 0, ox = 0, oy = 0;
    double best[8]; similar(scale, angle, ox, oy, best);
    double scale_step = 0.03, angle_step = 2.0 * kTau / 360.0, offset_step = side * 0.02;
    double best_score = objective(best, offset_step);
    for (int round = 0; round < 6; ++round) {
        best_score = objective(best, offset_step);  // re-evaluate at this step's resolution
        bool improved = true;
        while (improved) {
            improved = false;
            const double moves[8][4] = {{scale_step, 0, 0, 0}, {-scale_step, 0, 0, 0}, {0, angle_step, 0, 0}, {0, -angle_step, 0, 0},
                                        {0, 0, offset_step, 0}, {0, 0, -offset_step, 0}, {0, 0, 0, offset_step}, {0, 0, 0, -offset_step}};
            for (const auto &m : moves) {
                double q[8]; similar(scale + m[0], angle + m[1], ox + m[2], oy + m[3], q);
                const double score = objective(q, offset_step);
                if (score > best_score) { best_score = score; scale += m[0]; angle += m[1]; ox += m[2]; oy += m[3]; std::copy(q, q + 8, best); improved = true; }
            }
        }
        scale_step /= 2; angle_step /= 2; offset_step /= 2;
    }
    if (field == CKEY_FIELD_SCALABLE) {
        // Profile 0: stop here unless the CKey skeleton (frame, finders,
        // orientation split) agrees at some orientation, so a non-symbol
        // candidate costs only the similarity alignment above.
        bool skeleton_agrees = false;
        for (int reverse = 0; reverse < 2 && !skeleton_agrees; ++reverse) for (int start = 0; start < 4 && !skeleton_agrees; ++start) {
            double h[8]; bool ok; orientation_homography(best, reverse, start, h, ok); if (!ok) continue;
            const KeyFit fit = scalable_skeleton_fit(gray, width, height, stride, h);
            skeleton_agrees = fit.valid && fit.evidence >= kDenseKeyGate && fit.contrast >= 14.0;
        }
        if (!skeleton_agrees) { std::copy(best, best + 8, out_quad); return 0.0; }
    }
    // Per-corner projective moves: repeat at each step while they improve
    // (bounded), down to sub-pixel steps, so keystone displacements of a few
    // percent of the side are fully recovered.
    for (double corner_step = side * 0.02; corner_step > 0.3; corner_step /= 2) {
        best_score = objective(best, corner_step);
        for (int pass = 0; pass < 8; ++pass) {
            bool improved = false;
            for (int corner = 0; corner < 4; ++corner) for (const auto &[dx, dy] : {std::pair{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}}) {
                double q[8]; std::copy(best, best + 8, q);
                q[2 * corner] += dx * corner_step; q[2 * corner + 1] += dy * corner_step;
                const double score = objective(q, corner_step);
                if (score > best_score) { best_score = score; std::copy(q, q + 8, best); improved = true; }
            }
            if (!improved) break;
        }
    }
    if (field == CKEY_FIELD_SCALABLE) {
        // Profile 0: the skeleton aligned the field; now pick the version whose
        // keys fit best and fine-tune against them. They sit at the version's
        // own module and slot pitch, so they pin the scale far more tightly
        // than the frame and finders can at high densities.
        double version_score = 0;
        const int v = best_scalable_version(gray, width, height, stride, best, version_score);
        if (v > 0) {
            const ScalableLayout &L = scalable_layout(v);
            const double pitch_px = side / L.grid;
            double score = scalable_version_score(L, gray, width, height, stride, best);
            for (double step = pitch_px * 0.5; step > pitch_px * 0.02; step /= 2) {
                bool improved = true;
                while (improved) {
                    improved = false;
                    for (int corner = 0; corner < 4; ++corner) for (const auto &[dx, dy] : {std::pair{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}}) {
                        double q[8]; std::copy(best, best + 8, q);
                        q[2 * corner] += dx * step; q[2 * corner + 1] += dy * step;
                        const double candidate = scalable_version_score(L, gray, width, height, stride, q);
                        if (candidate > score) { score = candidate; std::copy(q, q + 8, best); improved = true; }
                    }
                }
            }
            best_score = score;
        }
    }
    std::copy(best, best + 8, out_quad);
    return best_score;
}

}  // extern "C"
