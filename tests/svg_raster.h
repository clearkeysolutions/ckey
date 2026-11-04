// Minimal rasterizer for the SVG subset emitted by CKey's renderers: <g>
// with translate/scale transforms and inherited fill/stroke/linecap/class,
// <rect> (filled or stroked, rounded), <circle>, <polygon>, and stroked
// <path> made of M/L/A commands. Styled output: <defs> are skipped, gradient
// fills count as ink, full-canvas rects are background, <image> is not
// drawn, and the "ckey-logo-plate" layer erases (painter's order). It consumes the rendered SVG text, not the encoder's
// internal geometry, so round-trip tests check what is actually printed.
#ifndef CKEY_TESTS_SVG_RASTER_H
#define CKEY_TESTS_SVG_RASTER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace svgtest {

constexpr double kPi = 3.14159265358979323846;

struct Affine { double sx = 1, sy = 1, tx = 0, ty = 0; };  // parent = s * local + t

struct Segment {
    bool arc = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // endpoints
    double cx = 0, cy = 0, r = 0, theta = 0, sweep = 0;  // arc parameterization
};

struct Prim {
    enum Kind { FillRect, StrokeRect, Circle, Stroke, Polygon } kind = FillRect;
    std::string layer;
    bool erase = false;
    std::vector<std::pair<double, double>> points;  // polygon vertices
    Affine xf;
    double x = 0, y = 0, w = 0, h = 0, rx = 0;  // rects; circle uses x,y,rx
    double half_width = 0; bool round_cap = false;
    std::vector<Segment> segments;
    double bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;  // bounding box in user space
};

struct Svg { double vx = 0, vy = 0, vw = 1, vh = 1; std::vector<Prim> prims; };

inline std::map<std::string, std::string> attributes(const std::string &tag) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while ((i = tag.find('=', i)) != std::string::npos) {
        size_t k = i; while (k > 0 && tag[k - 1] != ' ') --k;
        const std::string key = tag.substr(k, i - k);
        const size_t q0 = tag.find('"', i), q1 = tag.find('"', q0 + 1);
        out[key] = tag.substr(q0 + 1, q1 - q0 - 1);
        i = q1 + 1;
    }
    return out;
}

inline double sdf_round_rect(double px, double py, double x, double y, double w, double h, double r) {
    const double cx = x + w / 2, cy = y + h / 2;
    const double qx = std::abs(px - cx) - (w / 2 - r), qy = std::abs(py - cy) - (h / 2 - r);
    return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r;
}

inline double angle_offset(double a, double from, double sweep) {
    double d = a - from;
    if (sweep >= 0) { while (d < 0) d += 2 * kPi; while (d >= 2 * kPi) d -= 2 * kPi; }
    else { while (d > 0) d -= 2 * kPi; while (d <= -2 * kPi) d += 2 * kPi; }
    return d;
}

// Ink test for a stroked segment: distance <= half_width, honoring caps.
inline bool stroke_hit(const Segment &s, double px, double py, double hw, bool round_cap) {
    if (!s.arc) {
        const double vx = s.x1 - s.x0, vy = s.y1 - s.y0, len2 = vx * vx + vy * vy;
        const double t = len2 > 0 ? ((px - s.x0) * vx + (py - s.y0) * vy) / len2 : 0;
        if (!round_cap && (t < 0 || t > 1)) return false;
        const double c = std::clamp(t, 0.0, 1.0);
        return std::hypot(s.x0 + c * vx - px, s.y0 + c * vy - py) <= hw;
    }
    const double d = angle_offset(std::atan2(py - s.cy, px - s.cx), s.theta, s.sweep);
    if (std::abs(d) <= std::abs(s.sweep)) return std::abs(std::hypot(px - s.cx, py - s.cy) - s.r) <= hw;
    if (!round_cap) return false;
    return std::hypot(px - s.x0, py - s.y0) <= hw || std::hypot(px - s.x1, py - s.y1) <= hw;
}

inline bool prim_hit(const Prim &p, double ux, double uy) {
    const double x = (ux - p.xf.tx) / p.xf.sx, y = (uy - p.xf.ty) / p.xf.sy;
    switch (p.kind) {
    case Prim::FillRect: return sdf_round_rect(x, y, p.x, p.y, p.w, p.h, std::min(p.rx, std::min(p.w, p.h) / 2)) <= 0;
    case Prim::StrokeRect: return std::abs(sdf_round_rect(x, y, p.x, p.y, p.w, p.h, p.rx)) <= p.half_width;
    case Prim::Circle: return std::hypot(x - p.x, y - p.y) <= p.rx;
    case Prim::Stroke:
        for (const auto &s : p.segments) if (stroke_hit(s, x, y, p.half_width, p.round_cap)) return true;
        return false;
    case Prim::Polygon: {
        bool inside = false;
        for (size_t i = 0, j = p.points.size() - 1; i < p.points.size(); j = i++) {
            const auto &a = p.points[i], &b = p.points[j];
            if ((a.second > y) != (b.second > y) && x < (b.first - a.first) * (y - a.second) / (b.second - a.second) + a.first) inside = !inside;
        }
        return inside;
    }
    }
    return false;
}

inline void finish_bbox(Prim &p) {
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    const auto add = [&](double x, double y, double pad) { x0 = std::min(x0, x - pad); y0 = std::min(y0, y - pad); x1 = std::max(x1, x + pad); y1 = std::max(y1, y + pad); };
    if (p.kind == Prim::FillRect || p.kind == Prim::StrokeRect) { add(p.x, p.y, p.half_width); add(p.x + p.w, p.y + p.h, p.half_width); }
    else if (p.kind == Prim::Circle) add(p.x, p.y, p.rx);
    else if (p.kind == Prim::Polygon) for (const auto &[x, y] : p.points) add(x, y, 0);
    else for (const auto &s : p.segments) {
        if (s.arc) {
            // Tight bound: the sweep's endpoints plus every axis extreme it crosses.
            add(s.x0, s.y0, p.half_width); add(s.x1, s.y1, p.half_width);
            for (int q = -8; q <= 8; ++q) {
                const double a = q * kPi / 2;
                if (std::abs(angle_offset(a, s.theta, s.sweep)) <= std::abs(s.sweep))
                    add(s.cx + s.r * std::cos(a), s.cy + s.r * std::sin(a), p.half_width);
            }
        }
        else { add(s.x0, s.y0, p.half_width); add(s.x1, s.y1, p.half_width); }
    }
    const double ax = p.xf.sx * x0 + p.xf.tx, bx = p.xf.sx * x1 + p.xf.tx;
    const double ay = p.xf.sy * y0 + p.xf.ty, by = p.xf.sy * y1 + p.xf.ty;
    p.bx0 = std::min(ax, bx); p.bx1 = std::max(ax, bx); p.by0 = std::min(ay, by); p.by1 = std::max(ay, by);
}

// SVG 1.1 F.6.5 endpoint-to-center conversion for a circular arc (rx = ry, no rotation).
inline Segment make_arc(double x0, double y0, double r, bool large, bool sweep, double x1, double y1) {
    Segment s; s.arc = true; s.x0 = x0; s.y0 = y0; s.x1 = x1; s.y1 = y1;
    const double hx = (x0 - x1) / 2, hy = (y0 - y1) / 2, d2 = hx * hx + hy * hy;
    if (d2 > r * r) r = std::sqrt(d2);
    const double coef = (large != sweep ? 1.0 : -1.0) * std::sqrt(std::max(0.0, (r * r - d2) / d2));
    const double cxp = coef * hy, cyp = -coef * hx;
    s.cx = cxp + (x0 + x1) / 2; s.cy = cyp + (y0 + y1) / 2; s.r = r;
    s.theta = std::atan2(hy - cyp, hx - cxp);
    const double end = std::atan2(-hy - cyp, -hx - cxp);
    double delta = end - s.theta;
    if (sweep && delta < 0) delta += 2 * kPi;
    if (!sweep && delta > 0) delta -= 2 * kPi;
    s.sweep = delta;
    return s;
}

inline Svg parse(const std::string &text) {
    Svg svg;
    struct Ctx { Affine xf; std::string fill, stroke, cap, layer; };
    std::vector<Ctx> stack{{}};
    size_t i = 0;
    while ((i = text.find('<', i)) != std::string::npos) {
        const size_t j = text.find('>', i);
        std::string tag = text.substr(i + 1, j - i - 1);
        i = j + 1;
        if (tag.rfind("defs", 0) == 0) { const size_t end = text.find("</defs>", i); i = end == std::string::npos ? text.size() : end + 7; continue; }
        if (tag.rfind("/g", 0) == 0) { stack.pop_back(); continue; }
        if (tag[0] == '/') continue;
        const bool self_close = !tag.empty() && tag.back() == '/';
        const std::string name = tag.substr(0, tag.find_first_of(" /"));
        auto a = attributes(tag);
        Ctx ctx = stack.back();
        if (a.count("fill")) ctx.fill = a["fill"];
        if (a.count("stroke")) ctx.stroke = a["stroke"];
        if (a.count("stroke-linecap")) ctx.cap = a["stroke-linecap"];
        if (a.count("class")) ctx.layer = a["class"];
        if (name == "svg") {
            std::istringstream v(a["viewBox"]); v >> svg.vx >> svg.vy >> svg.vw >> svg.vh;
            continue;
        }
        if (name == "g") {
            const std::string t = a["transform"];
            if (!t.empty()) {
                double tx = 0, ty = 0, sx = 1, sy = 1;
                const size_t tp = t.find("translate("), sp = t.find("scale(");
                if (tp != std::string::npos) std::sscanf(t.c_str() + tp, "translate(%lf %lf)", &tx, &ty);
                if (sp != std::string::npos) std::sscanf(t.c_str() + sp, "scale(%lf %lf)", &sx, &sy);
                // child = parent_xf( translate( scale(p) ) )
                const Affine &q = stack.back().xf;
                ctx.xf = {q.sx * sx, q.sy * sy, q.sx * tx + q.tx, q.sy * ty + q.ty};
            }
            if (!self_close) stack.push_back(ctx);
            continue;
        }
        if (ctx.fill == "#050505") continue;  // background, not ink
        Prim p; p.layer = ctx.layer; p.xf = ctx.xf; p.erase = ctx.layer == "ckey-logo-plate";
        if (name == "rect" && std::stod(a["x"]) == svg.vx && std::stod(a["width"]) == svg.vw) continue;  // background / texture
        if (name == "polygon") {
            p.kind = Prim::Polygon;
            std::string pts = a["points"]; for (char &ch : pts) if (ch == ',') ch = ' ';
            std::istringstream in(pts); double px, py; while (in >> px >> py) p.points.push_back({px, py});
        } else if (name == "rect") {
            p.x = std::stod(a["x"]); p.y = std::stod(a["y"]); p.w = std::stod(a["width"]); p.h = std::stod(a["height"]);
            p.rx = a.count("rx") ? std::stod(a["rx"]) : 0.0;
            if (ctx.fill == "none") { p.kind = Prim::StrokeRect; p.half_width = std::stod(a["stroke-width"]) / 2; }
            else p.kind = Prim::FillRect;
        } else if (name == "circle") {
            p.kind = Prim::Circle; p.x = std::stod(a["cx"]); p.y = std::stod(a["cy"]); p.rx = std::stod(a["r"]);
        } else if (name == "path") {
            p.kind = Prim::Stroke; p.half_width = std::stod(a["stroke-width"]) / 2; p.round_cap = ctx.cap == "round";
            std::istringstream d(a["d"]); std::string cmd; double cx = 0, cy = 0;
            while (d >> cmd) {
                if (cmd == "M") d >> cx >> cy;
                else if (cmd == "L") { Segment s; s.x0 = cx; s.y0 = cy; d >> s.x1 >> s.y1; cx = s.x1; cy = s.y1; p.segments.push_back(s); }
                else if (cmd == "A") {
                    double r, ry, rot, x1, y1; int large, sweep; d >> r >> ry >> rot >> large >> sweep >> x1 >> y1;
                    p.segments.push_back(make_arc(cx, cy, r, large != 0, sweep != 0, x1, y1)); cx = x1; cy = y1;
                }
            }
        } else continue;
        finish_bbox(p);
        svg.prims.push_back(p);
    }
    return svg;
}

// Antialiased grayscale raster (ss x ss supersampling). Pixels map the
// viewBox onto width x height. layer_prefix filters primitives by class.
inline std::vector<uint8_t> rasterize(const Svg &svg, int width, int height, int ss = 4,
                                      uint8_t background = 5, uint8_t ink = 241,
                                      const std::string &layer_prefix = "") {
    const int W = width * ss, H = height * ss;
    std::vector<uint8_t> hit(static_cast<size_t>(W) * H, 0);
    const double kx = svg.vw / W, ky = svg.vh / H;
    for (const auto &p : svg.prims) {
        if (!layer_prefix.empty() && p.layer.rfind(layer_prefix, 0) != 0) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor((p.bx0 - svg.vx) / kx)) - 1);
        const int x1 = std::min(W - 1, static_cast<int>(std::ceil((p.bx1 - svg.vx) / kx)) + 1);
        const int y0 = std::max(0, static_cast<int>(std::floor((p.by0 - svg.vy) / ky)) - 1);
        const int y1 = std::min(H - 1, static_cast<int>(std::ceil((p.by1 - svg.vy) / ky)) + 1);
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            uint8_t &cell = hit[static_cast<size_t>(y) * W + x];
            if (p.erase) { if (cell && prim_hit(p, svg.vx + (x + .5) * kx, svg.vy + (y + .5) * ky)) cell = 0; }
            else if (!cell && prim_hit(p, svg.vx + (x + .5) * kx, svg.vy + (y + .5) * ky)) cell = 1;
        }
    }
    std::vector<uint8_t> out(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        int count = 0;
        for (int sy = 0; sy < ss; ++sy) for (int sx = 0; sx < ss; ++sx) count += hit[static_cast<size_t>(y * ss + sy) * W + x * ss + sx];
        out[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(background + (ink - background) * count / (ss * ss));
    }
    return out;
}

// Exact squared Euclidean distance transform (Felzenszwalb-Huttenlocher) of
// a binary mask: distance in pixels from each pixel to the nearest set pixel.
inline std::vector<double> distance_to_set(const std::vector<uint8_t> &mask, int width, int height) {
    const double inf = 1e20;
    std::vector<double> f(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < f.size(); ++i) f[i] = mask[i] ? 0.0 : inf;
    const auto pass = [&](std::vector<double> &line) {
        const int n = static_cast<int>(line.size());
        std::vector<double> d(n), z(n + 1); std::vector<int> v(n);
        int k = 0; v[0] = 0; z[0] = -inf; z[1] = inf;
        for (int q = 1; q < n; ++q) {
            double s;
            while (true) {
                s = ((line[q] + q * 1.0 * q) - (line[v[k]] + v[k] * 1.0 * v[k])) / (2.0 * q - 2.0 * v[k]);
                if (s <= z[k] && k > 0) --k; else break;
            }
            if (s <= z[k]) { v[0] = q; z[0] = -inf; z[1] = inf; k = 0; continue; }
            ++k; v[k] = q; z[k] = s; z[k + 1] = inf;
        }
        k = 0;
        for (int q = 0; q < n; ++q) { while (z[k + 1] < q) ++k; d[q] = (q - v[k]) * 1.0 * (q - v[k]) + line[v[k]]; }
        line = d;
    };
    std::vector<double> line;
    for (int x = 0; x < width; ++x) {
        line.resize(height); for (int y = 0; y < height; ++y) line[y] = f[static_cast<size_t>(y) * width + x];
        pass(line); for (int y = 0; y < height; ++y) f[static_cast<size_t>(y) * width + x] = line[y];
    }
    for (int y = 0; y < height; ++y) {
        line.assign(f.begin() + static_cast<std::ptrdiff_t>(y) * width, f.begin() + static_cast<std::ptrdiff_t>(y + 1) * width);
        pass(line); std::copy(line.begin(), line.end(), f.begin() + static_cast<std::ptrdiff_t>(y) * width);
    }
    for (double &value : f) value = std::sqrt(value);
    return f;
}

}  // namespace svgtest
#endif
