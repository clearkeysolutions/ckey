// ML locator: a small convolutional network that finds CKey symbols in a whole
// frame and proposes their four field corners (in symbol corner order), the
// version, the layout and a readability estimate. It only proposes: every
// proposal still goes through ckey_refine_quad_gray8 and the decoder, and a
// payload is accepted only after Reed-Solomon and CRC-32C, exactly as for a
// classical border candidate. Included by the scan library (api_camera.inc).
//
// Model file (CKML version 2, little-endian), produced by key-test ml/locator.py:
//   "CKML", u32 version, u32 buffers, u32 ops, u32 input_side, u32 out_stride, u32 out_channels
//   op 1 CONV  : u32 src, dst, cin, cout, k, stride, groups, relu; i8 w[cout][cin/groups][k][k];
//                f32 scale[cout]; f32 bias[cout]          (w = i8 * scale)
//   op 2 ADD   : u32 a, b, dst                              (dst = a + b)
//   op 3 UP2ADD: u32 a, b, dst                              (dst = nearest 2x up(a) + b)
// Buffer 0 is the standardised input; the last op's destination is the output.
// A model can arrive as a data asset, so the parser treats it as untrusted:
// every size, index and shape is checked before anything runs.

namespace {

struct MlConv {
    uint32_t src = 0, dst = 0, cin = 0, cout = 0, k = 0, stride = 1, groups = 1, relu = 0;
    std::vector<float> w, bias;
};
struct MlOp { uint32_t type = 0; uint32_t a = 0, b = 0, dst = 0; size_t conv = 0; };
struct MlShape { uint32_t c = 0, h = 0, w = 0; };

struct MlModel {
    uint32_t side = 0, stride = 0, channels = 0, buffers = 0;
    std::vector<MlConv> convs;
    std::vector<MlOp> ops;
    std::vector<MlShape> shapes;
    std::vector<size_t> last_use;
    uint64_t id = 0;
};

constexpr uint32_t kMlChannels = 24;  // the detection head this decoder understands

uint64_t fnv1a64(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

struct MlReader {
    const uint8_t *p; size_t n, at = 0; bool ok = true;
    uint32_t u32() {
        if (at > n || n - at < 4) { ok = false; return 0; }
        const uint32_t v = static_cast<uint32_t>(p[at]) | static_cast<uint32_t>(p[at + 1]) << 8 | static_cast<uint32_t>(p[at + 2]) << 16 | static_cast<uint32_t>(p[at + 3]) << 24;
        at += 4; return v;
    }
    float f32() { const uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
    bool take(size_t count) { if (at > n || n - at < count) { ok = false; return false; } return true; }
};

std::shared_ptr<const MlModel> ml_parse(const uint8_t *data, size_t len, std::string &err) {
    if (!data || len < 32 || std::memcmp(data, "CKML", 4) != 0) { err = "not a CKML model"; return nullptr; }
    MlReader r{data, len, 4};
    auto m = std::make_shared<MlModel>();
    const uint32_t version = r.u32();
    m->buffers = r.u32(); const uint32_t nops = r.u32();
    m->side = r.u32(); m->stride = r.u32(); m->channels = r.u32();
    if (!r.ok || version != 2) { err = "unsupported CKML version"; return nullptr; }
    if (m->buffers < 2 || m->buffers > 512 || nops < 1 || nops > 1024 || m->side < 64 || m->side > 1024 || m->side % 32 ||
        (m->stride != 4) || m->channels != kMlChannels) { err = "CKML header out of range"; return nullptr; }
    m->shapes.assign(m->buffers, MlShape{});
    m->shapes[0] = {1, m->side, m->side};
    std::vector<bool> defined(m->buffers, false); defined[0] = true;
    m->last_use.assign(m->buffers, 0);
    size_t weights_total = 0;
    for (uint32_t i = 0; i < nops; ++i) {
        MlOp op; op.type = r.u32();
        if (!r.ok) { err = "truncated CKML"; return nullptr; }
        if (op.type == 1) {
            MlConv c; c.src = r.u32(); c.dst = r.u32(); c.cin = r.u32(); c.cout = r.u32(); c.k = r.u32(); c.stride = r.u32(); c.groups = r.u32(); c.relu = r.u32();
            if (!r.ok || c.src >= m->buffers || c.dst >= m->buffers || !defined[c.src] || defined[c.dst] || c.cin == 0 || c.cout == 0 ||
                c.cin > 1024 || c.cout > 1024 || (c.k != 1 && c.k != 3) || (c.stride != 1 && c.stride != 2) || c.groups == 0 ||
                c.cin % c.groups || c.cout % c.groups || (c.groups != 1 && c.groups != c.cin) || c.relu > 1) { err = "bad CKML convolution"; return nullptr; }
            const MlShape in = m->shapes[c.src];
            if (in.c != c.cin || in.h < c.stride || in.w < c.stride) { err = "CKML shape mismatch"; return nullptr; }
            const size_t count = static_cast<size_t>(c.cout) * (c.cin / c.groups) * c.k * c.k;
            weights_total += count;
            if (weights_total > 8u * 1000 * 1000 || !r.take(count + 8 * static_cast<size_t>(c.cout))) { err = "CKML weights out of range"; return nullptr; }
            c.w.resize(count); c.bias.resize(c.cout);
            const int8_t *q = reinterpret_cast<const int8_t *>(data + r.at); r.at += count;
            std::vector<float> scale(c.cout);
            for (auto &s : scale) s = r.f32();
            for (auto &b : c.bias) b = r.f32();
            const size_t per = count / c.cout;
            for (size_t o = 0; o < c.cout; ++o) {
                if (!std::isfinite(scale[o]) || !std::isfinite(c.bias[o]) || std::abs(scale[o]) > 1e3f || std::abs(c.bias[o]) > 1e6f) { err = "CKML value out of range"; return nullptr; }
                for (size_t j = 0; j < per; ++j) c.w[o * per + j] = static_cast<float>(q[o * per + j]) * scale[o];
            }
            m->shapes[c.dst] = {c.cout, (in.h + c.stride - 1) / c.stride, (in.w + c.stride - 1) / c.stride};
            defined[c.dst] = true;
            m->last_use[c.src] = i;
            op.a = c.src; op.dst = c.dst; op.conv = m->convs.size();
            m->convs.push_back(std::move(c));
        } else if (op.type == 2 || op.type == 3) {
            op.a = r.u32(); op.b = r.u32(); op.dst = r.u32();
            if (!r.ok || op.a >= m->buffers || op.b >= m->buffers || op.dst >= m->buffers || !defined[op.a] || !defined[op.b] || defined[op.dst]) { err = "bad CKML add"; return nullptr; }
            const MlShape a = m->shapes[op.a], b = m->shapes[op.b];
            const bool fits = op.type == 2 ? (a.c == b.c && a.h == b.h && a.w == b.w) : (a.c == b.c && a.h * 2 == b.h && a.w * 2 == b.w);
            if (!fits) { err = "CKML add shape mismatch"; return nullptr; }
            m->shapes[op.dst] = b; defined[op.dst] = true;
            m->last_use[op.a] = i; m->last_use[op.b] = i;
        } else { err = "unknown CKML op"; return nullptr; }
        m->ops.push_back(op);
    }
    if (r.at != len) { err = "trailing bytes in CKML"; return nullptr; }
    const MlShape out = m->shapes[m->ops.back().dst];
    if (out.c != m->channels || out.h != m->side / m->stride || out.w != m->side / m->stride) { err = "CKML output shape"; return nullptr; }
    m->last_use[m->ops.back().dst] = nops;
    m->id = fnv1a64(data, len);
    return m;
}

// ---- kernels (planar CHW float) -------------------------------------------------

void ml_conv(const MlConv &c, const MlShape &in, const float *x, const MlShape &out, float *y) {
    const size_t ow = out.w, oh = out.h, iw = in.w, ih = in.h, plane = ow * oh;
    if (c.k == 1 && c.groups == 1) {
        // Pointwise: y[o] = b[o] + sum_i w[o][i] * x[i] (strided gather when stride 2).
        std::vector<float> gathered;
        const float *src = x;
        if (c.stride != 1) {
            gathered.resize(static_cast<size_t>(c.cin) * plane);
            for (size_t i = 0; i < c.cin; ++i) for (size_t yy = 0; yy < oh; ++yy) for (size_t xx = 0; xx < ow; ++xx)
                gathered[i * plane + yy * ow + xx] = x[i * iw * ih + yy * c.stride * iw + xx * c.stride];
            src = gathered.data();
        }
        // Four output channels per pass, so each input row is read once per four
        // outputs; pixels in tiles that stay in cache.
        constexpr size_t kTile = 1024;
        for (size_t o = 0; o < c.cout; ++o) { float *dst = y + o * plane; const float b = c.bias[o]; for (size_t p = 0; p < plane; ++p) dst[p] = b; }
        for (size_t t0 = 0; t0 < plane; t0 += kTile) {
            const size_t t1 = std::min(plane, t0 + kTile);
            size_t o = 0;
            for (; o + 4 <= c.cout; o += 4) {
                float *d0 = y + o * plane, *d1 = d0 + plane, *d2 = d1 + plane, *d3 = d2 + plane;
                const float *w0 = c.w.data() + o * c.cin, *w1 = w0 + c.cin, *w2 = w1 + c.cin, *w3 = w2 + c.cin;
                for (size_t i = 0; i < c.cin; ++i) {
                    const float a0 = w0[i], a1 = w1[i], a2 = w2[i], a3 = w3[i]; const float *s = src + i * plane;
                    for (size_t p = t0; p < t1; ++p) { const float v = s[p]; d0[p] += a0 * v; d1[p] += a1 * v; d2[p] += a2 * v; d3[p] += a3 * v; }
                }
            }
            for (; o < c.cout; ++o) {
                float *dst = y + o * plane; const float *w = c.w.data() + o * c.cin;
                for (size_t i = 0; i < c.cin; ++i) { const float wi = w[i]; const float *s = src + i * plane; for (size_t p = t0; p < t1; ++p) dst[p] += wi * s[p]; }
            }
        }
    } else {
        // 3x3 (depthwise, or full with groups 1), padding 1.
        const size_t cin_g = c.cin / c.groups, cout_g = c.cout / c.groups;
        for (size_t o = 0; o < c.cout; ++o) {
            const size_t g = o / cout_g;
            float *dst = y + o * plane;
            for (size_t p = 0; p < plane; ++p) dst[p] = c.bias[o];
            for (size_t ii = 0; ii < cin_g; ++ii) {
                const size_t i = g * cin_g + ii;
                const float *s = x + i * iw * ih;
                const float *w = c.w.data() + (o * cin_g + ii) * 9;
                for (size_t yy = 0; yy < oh; ++yy) {
                    const long sy = static_cast<long>(yy * c.stride) - 1;
                    for (int ky = 0; ky < 3; ++ky) {
                        const long ry = sy + ky;
                        if (ry < 0 || ry >= static_cast<long>(ih)) continue;
                        const float *row = s + static_cast<size_t>(ry) * iw;
                        float *drow = dst + yy * ow;
                        for (int kx = 0; kx < 3; ++kx) {
                            const float wk = w[ky * 3 + kx];
                            if (c.stride == 1) {
                                const long off = kx - 1;
                                const size_t lo = off < 0 ? 1 : 0, hi = off > 0 ? ow - 1 : ow;
                                for (size_t xx = lo; xx < hi; ++xx) drow[xx] += wk * row[static_cast<long>(xx) + off];
                            } else {
                                for (size_t xx = 0; xx < ow; ++xx) {
                                    const long rx = static_cast<long>(xx * 2) - 1 + kx;
                                    if (rx >= 0 && rx < static_cast<long>(iw)) drow[xx] += wk * row[rx];
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (c.relu) for (size_t p = 0, n = static_cast<size_t>(c.cout) * plane; p < n; ++p) y[p] = std::max(0.0f, y[p]);
}

std::vector<float> ml_forward(const MlModel &m, std::vector<float> input) {
    std::vector<std::vector<float>> buf(m.buffers);
    buf[0] = std::move(input);
    for (size_t i = 0; i < m.ops.size(); ++i) {
        const MlOp &op = m.ops[i];
        const MlShape &out = m.shapes[op.dst];
        std::vector<float> y(static_cast<size_t>(out.c) * out.h * out.w);
        if (op.type == 1) {
            const MlConv &c = m.convs[op.conv];
            ml_conv(c, m.shapes[c.src], buf[c.src].data(), out, y.data());
        } else if (op.type == 2) {
            const auto &a = buf[op.a], &b = buf[op.b];
            for (size_t p = 0; p < y.size(); ++p) y[p] = a[p] + b[p];
        } else {
            const MlShape &sa = m.shapes[op.a];
            const auto &a = buf[op.a], &b = buf[op.b];
            for (size_t ch = 0; ch < out.c; ++ch) for (size_t yy = 0; yy < out.h; ++yy) for (size_t xx = 0; xx < out.w; ++xx)
                y[(ch * out.h + yy) * out.w + xx] = a[(ch * sa.h + yy / 2) * sa.w + xx / 2] + b[(ch * out.h + yy) * out.w + xx];
        }
        buf[op.dst] = std::move(y);
        for (uint32_t b = 0; b < m.buffers; ++b) if (b != op.dst && !buf[b].empty() && m.last_use[b] <= i) { std::vector<float>().swap(buf[b]); }
    }
    return std::move(buf[m.ops.back().dst]);
}

// ---- model registry ---------------------------------------------------------------

std::mutex g_ml_mutex;
std::shared_ptr<const MlModel> g_ml_model;   // loaded model, or null
bool g_ml_builtin_tried = false;

#if defined(CKEY_ML_BUILTIN)
#include "ml_locator_model.inc"   // kMlLocatorModel[], kMlLocatorModelSize
#endif

std::shared_ptr<const MlModel> ml_current() {
    std::lock_guard<std::mutex> lock(g_ml_mutex);
#if defined(CKEY_ML_BUILTIN)
    if (!g_ml_model && !g_ml_builtin_tried) {
        g_ml_builtin_tried = true;
        std::string err;
        g_ml_model = ml_parse(kMlLocatorModel, kMlLocatorModelSize, err);
    }
#endif
    return g_ml_model;
}

// ---- preprocessing and decoding -------------------------------------------------------

struct MlLetterbox { double scale = 1, ox = 0, oy = 0; };

std::vector<float> ml_prepare(const uint8_t *gray, size_t width, size_t height, size_t stride, uint32_t side, MlLetterbox &lb) {
    // Area-average downscale so the longer side is `side`, letterboxed with 128,
    // then per-image standardisation: the same as the training export and
    // ml/locator.py normalise().
    std::vector<float> img(static_cast<size_t>(side) * side, 128.0f);
    const double sc = static_cast<double>(side) / static_cast<double>(std::max(width, height));
    const size_t ow = std::max<size_t>(1, static_cast<size_t>(std::lround(static_cast<double>(width) * sc)));
    const size_t oh = std::max<size_t>(1, static_cast<size_t>(std::lround(static_cast<double>(height) * sc)));
    const size_t ox = (side - std::min<size_t>(ow, side)) / 2, oy = (side - std::min<size_t>(oh, side)) / 2;
    for (size_t y = 0; y < oh && y < side; ++y) {
        const size_t y0 = static_cast<size_t>(static_cast<double>(y) / sc);
        const size_t y1 = std::min(height, std::max(y0 + 1, static_cast<size_t>(static_cast<double>(y + 1) / sc)));
        for (size_t x = 0; x < ow && x < side; ++x) {
            const size_t x0 = static_cast<size_t>(static_cast<double>(x) / sc);
            const size_t x1 = std::min(width, std::max(x0 + 1, static_cast<size_t>(static_cast<double>(x + 1) / sc)));
            uint32_t acc = 0, n = 0;
            for (size_t yy = y0; yy < y1; ++yy) for (size_t xx = x0; xx < x1; ++xx) { acc += gray[yy * stride + xx]; ++n; }
            img[(oy + y) * side + ox + x] = static_cast<float>(acc / std::max<uint32_t>(1, n));
        }
    }
    double mean = 0; for (float v : img) mean += v; mean /= static_cast<double>(img.size());
    double var = 0; for (float v : img) var += (v - mean) * (v - mean); var /= static_cast<double>(img.size());
    const double sd = std::max(4.0, std::sqrt(var));
    for (float &v : img) v = static_cast<float>((v - mean) / sd);
    lb.scale = sc; lb.ox = static_cast<double>(ox); lb.oy = static_cast<double>(oy);
    return img;
}

inline float ml_sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

struct MlDetection { double quad[8]; double score, quality, version, print; };

// Mirrors ml/locator.py decode(): centre peaks, coarse corners from the centre's
// offsets, then each corner's own heatmap peak near the coarse corner with its
// sub-cell offset. Coordinates are mapped back to full-frame pixels.
size_t ml_decode(const MlModel &m, const std::vector<float> &out, const MlLetterbox &lb, double threshold, MlDetection *det, size_t max) {
    const long O = static_cast<long>(m.side / m.stride), R = static_cast<long>(m.stride);
    const size_t plane = static_cast<size_t>(O * O);
    auto at = [&](size_t ch, long y, long x) { return out[ch * plane + static_cast<size_t>(y * O + x)]; };
    struct Peak { float s; long x, y; };
    std::vector<Peak> peaks;
    for (long y = 0; y < O; ++y) for (long x = 0; x < O; ++x) {
        const float v = at(0, y, x);
        if (ml_sigmoid(v) <= threshold) continue;
        bool top = true;
        for (int d = 0; d < 4 && top; ++d) {
            const long nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
            if (nx >= 0 && ny >= 0 && nx < O && ny < O && at(0, ny, nx) > v) top = false;
        }
        if (top) peaks.push_back({v, x, y});
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) { return a.s > b.s; });
    size_t n = 0;
    for (const Peak &pk : peaks) {
        if (n >= max) break;
        double coarse[8], quad[8];
        for (int k = 0; k < 4; ++k) {
            coarse[2 * k] = static_cast<double>(pk.x * R) + at(5 + 2 * k, pk.y, pk.x) * 16.0;
            coarse[2 * k + 1] = static_cast<double>(pk.y * R) + at(6 + 2 * k, pk.y, pk.x) * 16.0;
        }
        double side = 0; for (int k = 0; k < 4; ++k) side += std::hypot(coarse[2 * ((k + 1) % 4)] - coarse[2 * k], coarse[2 * ((k + 1) % 4) + 1] - coarse[2 * k + 1]) / 4;
        const long rad = std::max<long>(2, static_cast<long>(0.2 * side / static_cast<double>(R)));
        std::copy(coarse, coarse + 8, quad);
        for (int k = 0; k < 4; ++k) {
            const double px = coarse[2 * k] / static_cast<double>(R), py = coarse[2 * k + 1] / static_cast<double>(R);
            const long x0 = std::max<long>(0, static_cast<long>(px) - rad), x1 = std::min<long>(O, static_cast<long>(px) + rad + 1);
            const long y0 = std::max<long>(0, static_cast<long>(py) - rad), y1 = std::min<long>(O, static_cast<long>(py) + rad + 1);
            if (x0 >= x1 || y0 >= y1) continue;
            float best = -1e30f; long bx = x0, by = y0;
            for (long y = y0; y < y1; ++y) for (long x = x0; x < x1; ++x) { const float v = at(1 + k, y, x); if (v > best) { best = v; bx = x; by = y; } }
            if (ml_sigmoid(best) < 0.1f) continue;
            quad[2 * k] = (static_cast<double>(bx) + ml_sigmoid(at(13 + 2 * k, by, bx))) * static_cast<double>(R);
            quad[2 * k + 1] = (static_cast<double>(by) + ml_sigmoid(at(14 + 2 * k, by, bx))) * static_cast<double>(R);
        }
        MlDetection &d = det[n++];
        for (int k = 0; k < 4; ++k) { d.quad[2 * k] = (quad[2 * k] - lb.ox) / lb.scale; d.quad[2 * k + 1] = (quad[2 * k + 1] - lb.oy) / lb.scale; }
        d.score = ml_sigmoid(pk.s); d.quality = ml_sigmoid(at(21, pk.y, pk.x));
        d.version = at(22, pk.y, pk.x) * 8.0 + 16.0; d.print = ml_sigmoid(at(23, pk.y, pk.x));
    }
    return n;
}

size_t ml_locate(const uint8_t *gray, size_t width, size_t height, size_t stride, double threshold, MlDetection *det, size_t max) {
    const auto model = ml_current();
    if (!model || !det || !max) return 0;
    MlLetterbox lb;
    std::vector<float> in = ml_prepare(gray, width, height, stride, model->side, lb);
    const std::vector<float> out = ml_forward(*model, std::move(in));
    return ml_decode(*model, out, lb, threshold, det, max);
}

}  // namespace
