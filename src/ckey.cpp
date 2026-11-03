#include "ckey/ckey.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
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
            const double radius = 2.8 + 1.2 * ring;
            const double a = kTau * slot / 72.0;
            polar_layers[layer].push_back({true, false, kCenter + radius * std::sin(a), kCenter - radius * std::cos(a), -1, -1, ring, slot});
        }
    }
    for (size_t depth = 0; ; ++depth) {
        bool any = false;
        for (auto &layer : polar_layers) if (depth < layer.size()) { polar.push_back(layer[depth]); any = true; }
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

void set_status(ckey_diagnostics *d, ckey_status s, const char *m) {
    if (!d) return;
    d->abi_version = CKEY_ABI_VERSION; d->status = s;
    d->corrected_symbols = 0; d->erasures_used = 0;
    std::snprintf(d->message, sizeof(d->message), "%s", m);
}

void svg_circle(std::ostringstream &o, double x, double y, double r) {
    o << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << r << "\"/>";
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

ckey_status ckey_render_svg(const ckey_symbol *s, uint32_t scale, char *out,
                            size_t cap, size_t *required) {
    if (!s || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE || s->abi_version != CKEY_ABI_VERSION || s->side != kSide) return CKEY_INVALID_ARGUMENT;
    const auto nodes = data_nodes();
    if (nodes.size() < kBits) return CKEY_INTERNAL_ERROR;
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 31 * scale << "\" height=\"" << 31 * scale << "\" viewBox=\"-1 -1 31 31\" role=\"img\" aria-label=\"CKey locator symbol\">"
      << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" fill=\"#050505\"/><g fill=\"#f4f1e8\" stroke=\"#f4f1e8\">"
      << "<rect x=\"0.5\" y=\"0.5\" width=\"28\" height=\"28\" rx=\"3.5\" fill=\"none\" stroke-width=\"0.65\"/>";
    // One thick quarter arc marks three corners; the top-left arc is split
    // into two equal-thickness segments with a one-module clear orientation gap.
    for (int corner = 0; corner < 4; ++corner) {
        const char *transform = corner == 0 ? "" : corner == 1 ? "translate(29 0) scale(-1 1)" : corner == 2 ? "translate(29 29) scale(-1 -1)" : "translate(0 29) scale(1 -1)";
        o << "<g transform=\"" << transform << "\" fill=\"none\" stroke-linecap=\"round\">";
        constexpr double radius = 4.5, thickness = 1.0, cx = .8, cy = .8;
        if (corner == 0) {
            const double half_gap = 1.0 / radius; // 2-unit centerline gap leaves 1 unit clear after round caps.
            const double a1 = (45.0 + half_gap * 180.0 / 3.14159265358979323846) * kTau / 360.0;
            const double a2 = (45.0 - half_gap * 180.0 / 3.14159265358979323846) * kTau / 360.0;
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
    const double rr = 6.4;
    for (int slot : {18, 19, 21}) { const double a = kTau * slot / 72.0; svg_circle(o, kCenter + rr * std::sin(a), kCenter - rr * std::cos(a), .06); }
    const double a0 = kTau * 21.5 / 72.0, a1 = kTau * 25.5 / 72.0;
    o << "<path d=\"M " << kCenter + rr * std::sin(a0) << ' ' << kCenter - rr * std::cos(a0) << " A " << rr << ' ' << rr << " 0 0 1 "
      << kCenter + rr * std::sin(a1) << ' ' << kCenter - rr * std::cos(a1) << "\" fill=\"none\" stroke=\"#f4f1e8\" stroke-width=\"0.12\" stroke-linecap=\"butt\"/>";
    for (size_t i = 0; i < kBits; ++i) if (code_bit(s, i)) {
        const Node &n = nodes[i];
        if (n.dial) {
            const double a = kTau * n.slot / 72.0;
            o << "<path d=\"M " << kCenter + 7.6 * std::sin(a) << ' ' << kCenter - 7.6 * std::cos(a) << " L " << kCenter + 9.5 * std::sin(a) << ' ' << kCenter - 9.5 * std::cos(a)
              << "\" fill=\"none\" stroke=\"#f4f1e8\" stroke-width=\"0.15\"/>";
        }
        else if (n.polar) svg_circle(o, n.x, n.y, .06);
        else o << "<rect x=\"" << n.col + .13 << "\" y=\"" << n.row + .13 << "\" width=\"0.74\" height=\"0.74\" rx=\"0.16\" stroke=\"none\"/>";
    }
    o << "</g></svg>";
    const std::string text = o.str(); *required = text.size() + 1;
    if (!out || cap < *required) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out, text.c_str(), *required);
    return CKEY_OK;
}
}
