#include "ckey/ckey.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
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

struct Node { bool polar; bool dial; double x, y; int row, col; int ring, slot; };

bool in_finder(int r, int c) {
    const bool tl = r < 6 && c < 6, tr = r < 6 && c >= kSide - 6;
    const bool bl = r >= kSide - 6 && c < 6, br = r >= kSide - 6 && c >= kSide - 6;
    return tl || tr || bl || br;
}
bool grid_key(int r, int c) { return (c == 2 || c == 3) && (r == 11 || r == 12); }
bool ray_key(int slot) { return slot == 0 || slot == 54 || slot == 55; }
bool ring_key(int ring, int slot) { return ring == 3 && slot >= 18 && slot <= 25; }

std::vector<Node> data_nodes() {
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

bool code_bit(const ckey_symbol *s, size_t i) { return ((s->codeword[i / 8] >> (7 - (i % 8))) & 1u) != 0; }

struct TextNode { bool cart; bool dial; double x, y; int row, col, ring, slot; };

std::vector<TextNode> text_nodes() {
    constexpr int grid = 58, slots = 144, rings = 8;
    constexpr double pitch = 29.0 / grid;
    std::vector<TextNode> cart, polar, out;
    for (int row = 1; row < grid - 1; ++row) for (int col = 1; col < grid - 1; ++col) {
        const double x = (col + .5) * pitch, y = (row + .5) * pitch;
        const bool finder = (x < 6.1 && y < 6.1) || (x > 22.9 && y < 6.1) ||
                            (x < 6.1 && y > 22.9) || (x > 22.9 && y > 22.9);
        const bool grid_reference = x > 1.8 && x < 4.2 && y > 10.8 && y < 13.2;
        const double dx = x - kCenter, dy = y - kCenter;
        if (finder || grid_reference || dx * dx + dy * dy <= 10.5 * 10.5) continue;
        cart.push_back({true, false, x, y, row, col, -1, -1});
    }
    // The dial is narrow and radial. The eight rings share equal angular
    // pitch; each pair of adjacent occupied slots may become one arc.
    for (int slot = 0; slot < slots; ++slot) {
        if (slot != 0 && slot != 108 && slot != 110) polar.push_back({false, true, 0, 0, -1, -1, -1, slot});
    }
    for (int ring = 0; ring < rings; ++ring) for (int slot = 0; slot < slots; ++slot) {
        if (slot == 0 || slot == 108 || slot == 110 || (ring == rings - 1 && slot >= 36 && slot <= 51)) continue;
        const double radius = 3.4 + .43 * ring;
        const double a = kTau * slot / slots;
        polar.push_back({false, false, kCenter + radius * std::sin(a), kCenter - radius * std::cos(a), -1, -1, ring, slot});
    }
    // Deterministic coprime permutation plus Cartesian/polar interleaving
    // spreads adjacent codeword bits across both coordinate systems.
    size_t ci = 0, pi = 0;
    while (ci < cart.size() || pi < polar.size()) {
        if (ci < cart.size()) out.push_back(cart[ci++]);
        if (pi < polar.size()) out.push_back(polar[pi++]);
    }
    return out;
}

bool text_code_bit(const ckey_text_symbol *s, size_t i) { return ((s->codeword[i / 8] >> (7 - (i % 8))) & 1u) != 0; }

enum class ExtendedKind : uint8_t { cart, ring, ray };
struct ExtendedNode { ExtendedKind kind; double x, y; int ring, slot, component; };

std::vector<ExtendedNode> extended_nodes() {
    constexpr int grid = 232, slots = 288, rings = 24;
    constexpr double pitch = .5, center = 58.0;
    std::vector<ExtendedNode> cart, polar, out;
    // Profile 3 keeps the same visual finder proportion as Profile 1/2 but
    // grows the field.  Grid density, rather than finder shrinkage, supplies
    // its large Cartesian capacity.
    for (int row = 1; row < grid - 1; ++row) for (int col = 1; col < grid - 1; ++col) {
        const double x = (col + .5) * pitch, y = (row + .5) * pitch;
        // Structural boundary plus its anti-alias/blur guard is never payload.
        if (x < 4.0 || x > 112.0 || y < 4.0 || y > 112.0) continue;
        // Reserve only the actual finder strokes plus a measurable guard,
        // not the entire square corner envelope. This keeps data in the
        // useful regions beside and between the curves.
        const auto finder_clearance = [&](double cx, double cy, bool right, bool bottom) {
            const double vx=x-cx, vy=y-cy, distance=std::sqrt(vx*vx+vy*vy);
            const bool inward_x=right ? vx<=0 : vx>=0, inward_y=bottom ? vy<=0 : vy>=0;
            // The inside of a finder remains blank; only the exterior around
            // its true quarter-circle may carry Cartesian payload.
            return inward_x && inward_y && distance<=21.3;
        };
        const bool finder = finder_clearance(3.2,3.2,false,false) || finder_clearance(112.8,3.2,true,false) ||
                            finder_clearance(3.2,112.8,false,true) || finder_clearance(112.8,112.8,true,true);
        // The Grid Key occupies the identical module lattice as payload:
        // 0.34-unit marks on a 0.5-unit pitch, with one clear module around it.
        const bool grid_reference = x > 7.5 && x < 9.5 && y > 44.5 && y < 46.5;
        const double dx = x - center, dy = y - center;
        if (finder || grid_reference || dx * dx + dy * dy <= 31.3 * 31.3) continue;
        cart.push_back({ExtendedKind::cart, x, y, -1, -1, -1});
    }
    // Twenty-four rings are laterally denser than Profile 2, but their 0.65-unit
    // centre spacing remains more than four dot diameters. The dot is not
    // made smaller to fabricate capacity.
    // A deterministic two-dimensional permutation distributes the finite
    // codeword through the full Cartesian annulus. Row-major and pure radial
    // traversals both leave large, misleading empty regions at this profile.
    const auto spatial_key=[](const ExtendedNode &n) {
        uint32_t x=static_cast<uint32_t>(std::lround(n.x*2.0)), y=static_cast<uint32_t>(std::lround(n.y*2.0));
        uint32_t h=x*0x9e3779b9u ^ y*0x85ebca6bu; h^=h>>16; h*=0x7feb352du; h^=h>>15; h*=0x846ca68bu; return h^(h>>16);
    };
    std::sort(cart.begin(),cart.end(),[&](const ExtendedNode &a,const ExtendedNode &b){ return spatial_key(a)<spatial_key(b); });
    for (int ring = 0; ring < rings; ++ring) for (int slot = 0; slot < slots; ++slot) {
        if (slot == 0 || slot == 216 || slot == 224 ||
            (ring == rings - 1 && slot >= 72 && slot <= 79)) continue;
        const double radius = 9.50 + .65 * ring;
        const double a = kTau * slot / slots;
        polar.push_back({ExtendedKind::ring, center + radius * std::sin(a), center - radius * std::cos(a), ring, slot, -1});
    }
    // Every ray is always visible. Its Gray-coded two-bit length plus one
    // width bit gives an eight-state calibrated ray alphabet; no data state
    // relies on an absent ray.
    for (int slot = 0; slot < 576; ++slot) {
        if (slot == 0 || slot == 432 || slot == 440) continue;
        for (int component = 0; component < 3; ++component)
            polar.push_back({ExtendedKind::ray, 0, 0, -1, slot, component});
    }
    size_t ci = 0, pi = 0;
    while (ci < cart.size() || pi < polar.size()) {
        if (ci < cart.size()) out.push_back(cart[ci++]);
        if (pi < polar.size()) out.push_back(polar[pi++]);
    }
    return out;
}

bool extended_code_bit(const ckey_extended_symbol *s, size_t i) {
    return ((s->codeword[i / 8] >> (7 - (i % 8))) & 1u) != 0;
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

double gray_sample(const uint8_t *gray, size_t width, size_t height, size_t stride,
                   const double h[8], double x, double y) {
    // A small center-weighted 3x3 module sample tolerates rasterization and mild blur.
    constexpr double offsets[3]={-0.12,0.0,0.12};
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
                    const double h[8], double radius, double slot) {
    const double a=kTau*slot/72.0;
    return gray_sample(gray,width,height,stride,h,kCenter+radius*std::sin(a),kCenter-radius*std::cos(a));
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
    set_status(diag, CKEY_OK, "encoded locator profile v1");
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
    if (cw[0] != 0xc7 || cw[1] != 0x4b || cw[2] != CKEY_FORMAT_VERSION || cw[3] != 1 || cw[4] != 0 || cw[5] != 16) {
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
    // Frame: magic, format 9, profile 2, u16be payload length, payload,
    // CRC-32C over all preceding frame bytes, then deterministic zero padding.
    std::vector<uint8_t> codeword(kTextCodewordBytes, 0);
    codeword[0] = 0xc7; codeword[1] = 0x4b; codeword[2] = CKEY_TEXT_FORMAT_VERSION; codeword[3] = 2;
    codeword[4] = static_cast<uint8_t>(payload_len >> 8); codeword[5] = static_cast<uint8_t>(payload_len);
    std::memcpy(codeword.data() + 6, payload, payload_len);
    const uint32_t crc = crc32c(codeword.data(), 6 + payload_len);
    const size_t crc_index = 6 + payload_len;
    codeword[crc_index] = static_cast<uint8_t>(crc >> 24); codeword[crc_index + 1] = static_cast<uint8_t>(crc >> 16);
    codeword[crc_index + 2] = static_cast<uint8_t>(crc >> 8); codeword[crc_index + 3] = static_cast<uint8_t>(crc);
    rs_encode_generic(codeword, kTextDataBytes, kTextParityBytes);
    std::memcpy(out->codeword, codeword.data(), codeword.size());
    set_status(diag, CKEY_OK, "encoded text profile v2");
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
    if (corrected[0] != 0xc7 || corrected[1] != 0x4b || corrected[2] != CKEY_TEXT_FORMAT_VERSION || corrected[3] != 2) {
        set_status(diag, CKEY_INTEGRITY_FAILURE, "text frame has invalid magic, format, or profile"); return CKEY_INTEGRITY_FAILURE;
    }
    const size_t length = (static_cast<size_t>(corrected[4]) << 8) | corrected[5];
    if (length > CKEY_TEXT_PAYLOAD_MAX_BYTES || out_capacity < length || 6 + length + 4 > kTextDataBytes) {
        set_status(diag, CKEY_INSUFFICIENT_BUFFER, "text payload length exceeds profile or output capacity"); return CKEY_INSUFFICIENT_BUFFER;
    }
    const size_t crc_index = 6 + length;
    const uint32_t expected = (static_cast<uint32_t>(corrected[crc_index]) << 24) |
                              (static_cast<uint32_t>(corrected[crc_index + 1]) << 16) |
                              (static_cast<uint32_t>(corrected[crc_index + 2]) << 8) | corrected[crc_index + 3];
    if (crc32c(corrected.data(), crc_index) != expected) { set_status(diag, CKEY_INTEGRITY_FAILURE, "text CRC-32C validation failed"); return CKEY_INTEGRITY_FAILURE; }
    std::memcpy(payload, corrected.data() + 6, length); *out_payload_len = length;
    set_status(diag, CKEY_OK, corrections ? "decoded text profile; Reed-Solomon symbols corrected" : "decoded text profile; no correction needed");
    if (diag) diag->corrected_symbols = corrections;
    return CKEY_OK;
}

ckey_status ckey_encode_extended(const uint8_t *payload, size_t payload_len,
                                 ckey_extended_symbol *out, ckey_diagnostics *diag) {
    if (!payload || !out) { set_status(diag, CKEY_INVALID_ARGUMENT, "extended payload and output are required"); return CKEY_INVALID_ARGUMENT; }
    if (payload_len == 0 || payload_len > CKEY_EXTENDED_PAYLOAD_MAX_BYTES) { set_status(diag, CKEY_INVALID_ARGUMENT, "extended profile payload must contain 1 to 2048 bytes"); return CKEY_INVALID_ARGUMENT; }
    std::array<uint8_t, kExtendedBlocks * kExtendedDataBytes> framed{};
    framed[0]=0xc7; framed[1]=0x4b; framed[2]=CKEY_EXTENDED_FORMAT_VERSION; framed[3]=3;
    framed[4]=static_cast<uint8_t>(payload_len >> 8); framed[5]=static_cast<uint8_t>(payload_len);
    std::memcpy(framed.data()+6,payload,payload_len);
    const size_t crc_index=6+payload_len; const uint32_t crc=crc32c(framed.data(),crc_index);
    framed[crc_index]=static_cast<uint8_t>(crc>>24); framed[crc_index+1]=static_cast<uint8_t>(crc>>16);
    framed[crc_index+2]=static_cast<uint8_t>(crc>>8); framed[crc_index+3]=static_cast<uint8_t>(crc);
    std::array<std::array<uint8_t,kExtendedBlockBytes>,kExtendedBlocks> blocks{};
    for (size_t block=0;block<kExtendedBlocks;++block) {
        std::copy_n(framed.data()+block*kExtendedDataBytes,kExtendedDataBytes,blocks[block].begin());
        std::vector<uint8_t> work(blocks[block].begin(),blocks[block].end());
        rs_encode_generic(work,kExtendedDataBytes,kExtendedParityBytes);
        std::copy(work.begin(),work.end(),blocks[block].begin());
    }
    std::memset(out,0,sizeof(*out)); out->abi_version=CKEY_ABI_VERSION; out->side=kExtendedSide; out->payload_length=static_cast<uint32_t>(payload_len);
    // Byte interleaving maps local optical damage across all independent blocks.
    for (size_t byte=0;byte<kExtendedBlockBytes;++byte) for (size_t block=0;block<kExtendedBlocks;++block)
        out->codeword[byte*kExtendedBlocks+block]=blocks[block][byte];
    set_status(diag,CKEY_OK,"encoded extended profile v3; 10 interleaved RS blocks"); return CKEY_OK;
}

ckey_status ckey_decode_extended(const uint8_t *codeword, size_t codeword_len,
                                 uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len,
                                 ckey_diagnostics *diag) {
    if (!codeword || !out_payload || !out_payload_len || codeword_len != kExtendedCodewordBytes) { set_status(diag,CKEY_INVALID_ARGUMENT,"extended decode requires a 2550-byte codeword and output length"); return CKEY_INVALID_ARGUMENT; }
    std::array<std::array<uint8_t,kExtendedBlockBytes>,kExtendedBlocks> blocks{};
    for (size_t byte=0;byte<kExtendedBlockBytes;++byte) for (size_t block=0;block<kExtendedBlocks;++block) blocks[block][byte]=codeword[byte*kExtendedBlocks+block];
    std::array<uint8_t,kExtendedBlocks*kExtendedDataBytes> framed{}; uint32_t corrections=0;
    for (size_t block=0;block<kExtendedBlocks;++block) {
        std::vector<uint8_t> work(blocks[block].begin(),blocks[block].end()); uint32_t fixed=0;
        if (!rs_correct_generic(work,kExtendedParityBytes,fixed)) { set_status(diag,CKEY_UNCORRECTABLE,"extended Reed-Solomon correction failed"); return CKEY_UNCORRECTABLE; }
        corrections+=fixed; std::copy_n(work.begin(),kExtendedDataBytes,framed.data()+block*kExtendedDataBytes);
    }
    if (framed[0]!=0xc7 || framed[1]!=0x4b || framed[2]!=CKEY_EXTENDED_FORMAT_VERSION || framed[3]!=3) { set_status(diag,CKEY_INTEGRITY_FAILURE,"extended frame has invalid magic, format, or profile"); return CKEY_INTEGRITY_FAILURE; }
    const size_t length=(static_cast<size_t>(framed[4])<<8)|framed[5], crc_index=6+length;
    if (length==0 || length>CKEY_EXTENDED_PAYLOAD_MAX_BYTES || crc_index+4>framed.size() || out_capacity<length) { set_status(diag,CKEY_INSUFFICIENT_BUFFER,"extended payload length exceeds profile or output capacity"); return CKEY_INSUFFICIENT_BUFFER; }
    const uint32_t expected=(static_cast<uint32_t>(framed[crc_index])<<24)|(static_cast<uint32_t>(framed[crc_index+1])<<16)|(static_cast<uint32_t>(framed[crc_index+2])<<8)|framed[crc_index+3];
    if (crc32c(framed.data(),crc_index)!=expected) { set_status(diag,CKEY_INTEGRITY_FAILURE,"extended CRC-32C validation failed"); return CKEY_INTEGRITY_FAILURE; }
    std::memcpy(out_payload,framed.data()+6,length); *out_payload_len=length;
    set_status(diag,CKEY_OK,corrections ? "decoded extended profile; Reed-Solomon symbols corrected" : "decoded extended profile; no correction needed"); if (diag) diag->corrected_symbols=corrections;
    return CKEY_OK;
}

double ckey_calibration_score_gray8(const uint8_t *gray, size_t width, size_t height,
                                    size_t stride, const double quad_xy[8]) {
    if (!gray || !quad_xy || width < 2 || height < 2 || stride < width ||
        height > std::numeric_limits<size_t>::max() / stride) return -1.0;
    for (int i = 0; i < 8; ++i) if (!std::isfinite(quad_xy[i])) return -1.0;
    if (!valid_quad(quad_xy)) return -1.0;

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
        std::vector<double> grid, ring, ray, empty;
        for (int r : {11,12}) for (int c : {2,3}) grid.push_back(gray_sample(gray,width,height,stride,h,c+.5,r+.5));
        for (int slot : {18,19,21}) ring.push_back(polar_sample(gray,width,height,stride,h,6.0,slot));
        ring.push_back(polar_sample(gray,width,height,stride,h,6.0,23.5));
        ray.push_back(polar_sample(gray,width,height,stride,h,8.8,0));
        ray.push_back(polar_sample(gray,width,height,stride,h,8.4,54));
        ray.push_back(polar_sample(gray,width,height,stride,h,8.8,55));
        empty.push_back(gray_sample(gray,width,height,stride,h,1.5,11.5));
        empty.push_back(polar_sample(gray,width,height,stride,h,6.0,20));
        empty.push_back(polar_sample(gray,width,height,stride,h,8.8,54));
        std::vector<double> marks = grid;
        marks.insert(marks.end(), ring.begin(), ring.end());
        marks.insert(marks.end(), ray.begin(), ray.end());
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
        best = std::max(best, contrast * static_cast<double>(evidence) / 13.0);
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
        empties.push_back(gray_sample(gray,width,height,stride,h,1.5,11.5));
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

        std::array<uint8_t,kCodewordBytes> cw{};
        bool samples_ok=true;
        const auto nodes=data_nodes();
        for (size_t bit=0;bit<kBits;++bit) {
            const Node &n=nodes[bit]; double v;
            if (n.dial) v=polar_sample(gray,width,height,stride,h,8.8,n.slot); // outer band separates short from long ray
            else if (n.polar) v=gray_sample(gray,width,height,stride,h,n.x,n.y);
            else v=gray_sample(gray,width,height,stride,h,n.x,n.y);
            if (v<0) { samples_ok=false; break; }
            const bool one=marked(v);
            if (one) cw[bit/8]|=static_cast<uint8_t>(1u<<(7-(bit%8)));
        }
        if(!samples_ok) continue;
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

ckey_status ckey_render_svg(const ckey_symbol *s, uint32_t scale, char *out,
                            size_t cap, size_t *required) {
    if (!s || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE || s->abi_version != CKEY_ABI_VERSION || s->side != kSide) return CKEY_INVALID_ARGUMENT;
    const auto nodes = data_nodes();
    if (nodes.size() < kBits) return CKEY_INTERNAL_ERROR;
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 31 * scale << "\" height=\"" << 31 * scale << "\" viewBox=\"-1 -1 31 31\" role=\"img\" aria-label=\"CKey locator symbol\">"
      << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" fill=\"#050505\"/><g fill=\"#f4f1e8\" stroke=\"#f4f1e8\">"
      << "<rect x=\"0.5\" y=\"0.5\" width=\"28\" height=\"28\" rx=\"3.5\" fill=\"none\" stroke-width=\"0.65\"/>";
    // One thick quarter-arc marks each corner. The top-left arc is split
    // into two fragments to encode orientation with a one-module clear gap.
    for (int corner = 0; corner < 4; ++corner) {
        const char *transform = corner == 0 ? "" : corner == 1 ? "translate(29 0) scale(-1 1)" : corner == 2 ? "translate(29 29) scale(-1 -1)" : "translate(0 29) scale(1 -1)";
        o << "<g transform=\"" << transform << "\" fill=\"none\" stroke-linecap=\"round\">";
        constexpr double cx = .8, cy = .8, radius = 4.5, thickness = 1.0, visible_gap = 1.0;
        if (corner == 0) {
            const double half_gap = (visible_gap + thickness) / (2.0 * radius);
            const double middle = kTau * 45.0 / 360.0;
            const double a1 = middle + half_gap;
            const double a2 = middle - half_gap;
            o << "<path d=\"M " << cx << ' ' << cy + radius << " A " << radius << ' ' << radius << " 0 0 0 " << cx + radius * std::cos(a1) << ' ' << cy + radius * std::sin(a1)
              << " M " << cx + radius * std::cos(a2) << ' ' << cy + radius * std::sin(a2) << " A " << radius << ' ' << radius << " 0 0 0 " << cx + radius << ' ' << cy
              << "\" stroke-width=\"" << thickness << "\"/>";
        } else {
            o << "<path d=\"M " << cx << ' ' << cy + radius << " A " << radius << ' ' << radius << " 0 0 0 " << cx + radius << ' ' << cy
              << "\" stroke-width=\"" << thickness << "\"/>";
        }
        o << "</g>";
    }
    // Grid Key: fixed 2x2 Cartesian reference block at the left matrix edge.
    for (int r : {11, 12}) for (int c : {2, 3}) o << "<rect x=\"" << c + .13 << "\" y=\"" << r + .13 << "\" width=\"0.74\" height=\"0.74\" rx=\"0.16\" stroke=\"none\"/>";
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
        else o << "<rect x=\"" << n.col + .13 << "\" y=\"" << n.row + .13 << "\" width=\"0.74\" height=\"0.74\" rx=\"0.16\" stroke=\"none\"/>";
    }
    o << "</g></svg>";
    const std::string text = o.str(); *required = text.size() + 1;
    if (!out || cap < *required) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out, text.c_str(), *required);
    return CKEY_OK;
}

ckey_status ckey_render_text_svg(const ckey_text_symbol *s, uint32_t scale, char *out,
                                  size_t cap, size_t *required) {
    if (!s || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE ||
        s->abi_version != CKEY_ABI_VERSION || s->side != kSide || s->payload_length > CKEY_TEXT_PAYLOAD_MAX_BYTES) return CKEY_INVALID_ARGUMENT;
    const auto nodes = text_nodes();
    if (nodes.size() < kTextCodewordBytes * 8) return CKEY_INTERNAL_ERROR;
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 31 * scale << "\" height=\"" << 31 * scale << "\" viewBox=\"-1 -1 31 31\" role=\"img\" aria-label=\"CKey text symbol profile 2\">"
      << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" fill=\"#050505\"/><g fill=\"#f4f1e8\" stroke=\"#f4f1e8\">"
      << "<rect x=\"0.5\" y=\"0.5\" width=\"28\" height=\"28\" rx=\"3.5\" fill=\"none\" stroke-width=\"0.65\"/>";
    for (int corner = 0; corner < 4; ++corner) {
        const char *transform = corner == 0 ? "" : corner == 1 ? "translate(29 0) scale(-1 1)" : corner == 2 ? "translate(29 29) scale(-1 -1)" : "translate(0 29) scale(1 -1)";
        o << "<g transform=\"" << transform << "\" fill=\"none\" stroke-linecap=\"round\">";
        constexpr double cx=.8, cy=.8, radius=4.5, thickness=1.0;
        if (corner == 0) {
            const double gap = (1.0 + thickness) / (2.0 * radius), middle = kTau / 8.0;
            o << "<path d=\"M " << cx << ' ' << cy + radius << " A 4.5000 4.5000 0 0 0 " << cx + radius * std::cos(middle + gap) << ' ' << cy + radius * std::sin(middle + gap)
              << " M " << cx + radius * std::cos(middle - gap) << ' ' << cy + radius * std::sin(middle - gap) << " A 4.5000 4.5000 0 0 0 " << cx + radius << ' ' << cy << "\" stroke-width=\"1.0000\"/>";
        } else o << "<path d=\"M " << cx << ' ' << cy + radius << " A 4.5000 4.5000 0 0 0 " << cx + radius << ' ' << cy << "\" stroke-width=\"1.0000\"/>";
        o << "</g>";
    }
    for (int r : {11,12}) for (int c : {2,3}) o << "<rect x=\"" << c+.13 << "\" y=\"" << r+.13 << "\" width=\"0.74\" height=\"0.74\" rx=\"0.16\" stroke=\"none\"/>";
    for (const auto &[slot, fraction] : {std::pair{0,1.0}, std::pair{108,.5}, std::pair{110,.75}}) {
        const double a=kTau*slot/144.0, r0=7.6, r1=r0+1.9*fraction;
        o << "<path d=\"M " << kCenter+r0*std::sin(a) << ' ' << kCenter-r0*std::cos(a) << " L " << kCenter+r1*std::sin(a) << ' ' << kCenter-r1*std::cos(a) << "\" fill=\"none\" stroke-width=\"0.22\" stroke-linecap=\"round\"/>";
    }
    constexpr double key_radius=6.41, dot_radius=.10;
    for (int slot : {36,38,42}) { const double a=kTau*slot/144.0; svg_circle(o,kCenter+key_radius*std::sin(a),kCenter-key_radius*std::cos(a),dot_radius); }
    const double key_cap=dot_radius/key_radius, key_a0=kTau*43.5/144.0+key_cap, key_a1=kTau*51.5/144.0-key_cap;
    o << "<path d=\"M " << kCenter+key_radius*std::sin(key_a0) << ' ' << kCenter-key_radius*std::cos(key_a0) << " A " << key_radius << ' ' << key_radius << " 0 0 1 " << kCenter+key_radius*std::sin(key_a1) << ' ' << kCenter-key_radius*std::cos(key_a1) << "\" fill=\"none\" stroke-width=\"0.2000\" stroke-linecap=\"round\"/>";
    std::array<std::array<bool,144>,8> ring_bits{};
    for (size_t i=0;i<kTextCodewordBytes*8;++i) {
        const auto &n=nodes[i]; const bool bit=text_code_bit(s,i);
        if (n.cart) { if (bit) o << "<rect x=\"" << n.x-.17 << "\" y=\"" << n.y-.17 << "\" width=\"0.34\" height=\"0.34\" rx=\"0.08\" stroke=\"none\"/>"; }
        else if (n.dial) { const double a=kTau*n.slot/144.0, length=bit?1.20:.52; o << "<path d=\"M " << kCenter+7.6*std::sin(a) << ' ' << kCenter-7.6*std::cos(a) << " L " << kCenter+(7.6+length)*std::sin(a) << ' ' << kCenter-(7.6+length)*std::cos(a) << "\" fill=\"none\" stroke-width=\"0.1000\" stroke-linecap=\"round\"/>"; }
        else ring_bits[n.ring][n.slot]=bit;
    }
    for (int ring=0;ring<8;++ring) {
        const double radius=3.4+.43*ring;
        for (int slot=0;slot<144;) {
            if (!ring_bits[ring][slot]) { ++slot; continue; }
            int end=slot+1; while (end<144 && ring_bits[ring][end]) ++end;
            if (end-slot==1) { const double a=kTau*slot/144.0; svg_circle(o,kCenter+radius*std::sin(a),kCenter-radius*std::cos(a),dot_radius); }
            else {
                const double cap_angle=dot_radius/radius;
                const double a0=kTau*(slot-.5)/144.0+cap_angle, a1=kTau*(end-.5)/144.0-cap_angle;
                o << "<path d=\"M " << kCenter+radius*std::sin(a0) << ' ' << kCenter-radius*std::cos(a0) << " A " << radius << ' ' << radius << " 0 0 1 " << kCenter+radius*std::sin(a1) << ' ' << kCenter-radius*std::cos(a1) << "\" fill=\"none\" stroke-width=\"0.2000\" stroke-linecap=\"round\"/>";
            }
            slot=end;
        }
    }
    o << "</g></svg>";
    const std::string text=o.str(); *required=text.size()+1;
    if (!out || cap<*required) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out,text.c_str(),*required); return CKEY_OK;
}

ckey_status ckey_render_extended_svg(const ckey_extended_symbol *s, uint32_t scale, char *out,
                                      size_t cap, size_t *required) {
    if (!s || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE || s->abi_version != CKEY_ABI_VERSION || s->side != kExtendedSide || s->payload_length == 0 || s->payload_length > CKEY_EXTENDED_PAYLOAD_MAX_BYTES) return CKEY_INVALID_ARGUMENT;
    const auto nodes=extended_nodes(); if (nodes.size()<kExtendedBits) return CKEY_INTERNAL_ERROR;
    constexpr int slots=288, rings=24; constexpr double center=58.0, dot_radius=.075;
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 124*scale << "\" height=\"" << 124*scale << "\" viewBox=\"-4 -4 124 124\" role=\"img\" aria-label=\"CKey extended symbol profile 3\">"
      << "<rect x=\"-4\" y=\"-4\" width=\"124\" height=\"124\" fill=\"#050505\"/><g fill=\"#f4f1e8\" stroke=\"#f4f1e8\">"
      << "<rect x=\"2\" y=\"2\" width=\"112\" height=\"112\" rx=\"14\" fill=\"none\" stroke-width=\"2.6\"/>";
    for (int corner=0;corner<4;++corner) {
        const char *t=corner==0?"":corner==1?"translate(116 0) scale(-1 1)":corner==2?"translate(116 116) scale(-1 -1)":"translate(0 116) scale(1 -1)";
        o << "<g transform=\"" << t << "\" fill=\"none\" stroke-linecap=\"round\">";
        constexpr double cx=3.2,cy=3.2,r=18.0,thickness=4.0;
        if (corner==0) {
            const double gap=(4.0+thickness)/(2.0*r), middle=kTau/8.0;
            o << "<path d=\"M " << cx << ' ' << cy+r << " A 18 18 0 0 0 " << cx+r*std::cos(middle+gap) << ' ' << cy+r*std::sin(middle+gap)
              << " M " << cx+r*std::cos(middle-gap) << ' ' << cy+r*std::sin(middle-gap) << " A 18 18 0 0 0 " << cx+r << ' ' << cy << "\" stroke-width=\"4\"/>";
        } else o << "<path d=\"M " << cx << ' ' << cy+r << " A 18 18 0 0 0 " << cx+r << ' ' << cy << "\" stroke-width=\"4\"/>";
        o << "</g>";
    }
    // Fixed Grid Key: four ordinary 0.34-unit Cartesian modules on the same
    // 0.5-unit pitch as payload. It calibrates payload pitch and dead space;
    // it is deliberately not a larger visual marker.
    for (int r=0;r<2;++r) for (int c=0;c<2;++c) o << "<rect x=\"" << 8.08+c*.5 << "\" y=\"" << 45.08+r*.5 << "\" width=\".34\" height=\".34\" rx=\".08\" stroke=\"none\"/>";
    // Fixed Ray Key: 12:00 full; 9:00 half; 9:05 three-quarter length.
    for (const auto &[slot,fraction] : {std::pair{0,1.0},std::pair{432,.5},std::pair{440,.75}}) {
        const double a=kTau*slot/576.0,r0=26.30,r1=r0+3.6*fraction;
        o << "<path d=\"M " << center+r0*std::sin(a) << ' ' << center-r0*std::cos(a) << " L " << center+r1*std::sin(a) << ' ' << center-r1*std::cos(a) << "\" fill=\"none\" stroke-width=\".28\" stroke-linecap=\"round\"/>";
    }
    // Fixed Ring Key: two adjacent dots, one empty slot, one dot, then a
    // continuous four-slot arc of precisely equal angular occupancy.
    constexpr double key_r=24.45;
    for (int slot : {72,73,75}) { const double a=kTau*slot/slots; svg_circle(o,center+key_r*std::sin(a),center-key_r*std::cos(a),dot_radius); }
    const double key_cap=dot_radius/key_r,ka0=kTau*(75.5)/slots+key_cap,ka1=kTau*(79.5)/slots-key_cap;
    o << "<path d=\"M " << center+key_r*std::sin(ka0) << ' ' << center-key_r*std::cos(ka0) << " A " << key_r << ' ' << key_r << " 0 0 1 " << center+key_r*std::sin(ka1) << ' ' << center-key_r*std::cos(ka1) << "\" fill=\"none\" stroke-width=\".1500\" stroke-linecap=\"round\"/>";
    std::array<std::array<bool,slots>,rings> ring_bits{}; std::array<std::array<bool,3>,576> ray_bits{};
    for (size_t i=0;i<kExtendedBits;++i) {
        const auto &n=nodes[i]; const bool bit=extended_code_bit(s,i);
        if (n.kind==ExtendedKind::cart) { if (bit) o << "<rect x=\"" << n.x-.17 << "\" y=\"" << n.y-.17 << "\" width=\".34\" height=\".34\" rx=\".08\" stroke=\"none\"/>"; }
        else if (n.kind==ExtendedKind::ring) ring_bits[n.ring][n.slot]=bit;
        else ray_bits[n.slot][n.component]=bit;
    }
    // The ray alphabet is (Gray length, width): 4 calibrated lengths x 2
    // calibrated weights.  It contributes three payload bits/ray while every
    // orientation remains visible under blur and never aliases to absence.
    for (int slot=0;slot<576;++slot) {
        if (slot==0 || slot==432 || slot==440) continue;
        const int gray=(ray_bits[slot][0]?2:0)|(ray_bits[slot][1]?1:0), level=gray^(gray>>1);
        const double a=kTau*slot/576.0,r0=26.30,length=.80+.85*level,width=ray_bits[slot][2]?.28:.16;
        o << "<path d=\"M " << center+r0*std::sin(a) << ' ' << center-r0*std::cos(a) << " L " << center+(r0+length)*std::sin(a) << ' ' << center-(r0+length)*std::cos(a) << "\" fill=\"none\" stroke-width=\"" << width << "\" stroke-linecap=\"round\"/>";
    }
    for (int ring=0;ring<rings;++ring) {
        const double radius=9.50+.65*ring;
        for (int slot=0;slot<slots;) {
            if (!ring_bits[ring][slot]) { ++slot; continue; }
            int end=slot+1; while(end<slots && ring_bits[ring][end]) ++end;
            if (end-slot==1) { const double a=kTau*slot/slots; svg_circle(o,center+radius*std::sin(a),center-radius*std::cos(a),dot_radius); }
            else { const double cap_angle=dot_radius/radius,a0=kTau*(slot-.5)/slots+cap_angle,a1=kTau*(end-.5)/slots-cap_angle;
                o << "<path d=\"M " << center+radius*std::sin(a0) << ' ' << center-radius*std::cos(a0) << " A " << radius << ' ' << radius << " 0 0 1 " << center+radius*std::sin(a1) << ' ' << center-radius*std::cos(a1) << "\" fill=\"none\" stroke-width=\".1500\" stroke-linecap=\"round\"/>"; }
            slot=end;
        }
    }
    o << "</g></svg>"; const std::string text=o.str(); *required=text.size()+1;
    if (!out || cap<*required) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out,text.c_str(),*required);
    return CKEY_OK;
}
}
