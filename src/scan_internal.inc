// Scan-library internals: full-frame border search. Included by api_scan.inc
// after core_internal.inc. Everything here has internal linkage.

namespace {

// ---- Area-averaged working image -------------------------------------------
// The border search works on a box-filtered reduction of the frame (about
// 960 x 540 at any capture size). Averaging each step x step block, instead of
// reading one pixel per block, is what keeps a display's pixel grid from
// aliasing into moire bands; it also halves sensor noise per step.
struct ReducedImage {
    size_t w = 0, h = 0, step = 1;
    std::vector<uint8_t> px;
    std::vector<uint32_t> sum;  // (w + 1) x (h + 1) integral image
    std::vector<uint64_t> sq;   // integral of squares
    // Mean and standard deviation of the window [x0, x1) x [y0, y1), clipped.
    void window(long x0, long y0, long x1, long y1, double &mean, double &sd) const {
        x0 = std::max(x0, 0L); y0 = std::max(y0, 0L);
        x1 = std::min(x1, static_cast<long>(w)); y1 = std::min(y1, static_cast<long>(h));
        const size_t W = w + 1;
        const size_t a = static_cast<size_t>(y0) * W + static_cast<size_t>(x0), b = static_cast<size_t>(y0) * W + static_cast<size_t>(x1);
        const size_t c = static_cast<size_t>(y1) * W + static_cast<size_t>(x0), d = static_cast<size_t>(y1) * W + static_cast<size_t>(x1);
        const double n = static_cast<double>((x1 - x0) * (y1 - y0));
        const double s = static_cast<double>(sum[d] - sum[b] - sum[c] + sum[a]);
        const double q = static_cast<double>(sq[d] - sq[b] - sq[c] + sq[a]);
        mean = s / n;
        sd = std::sqrt(std::max(0.0, q / n - mean * mean));
    }
};

ReducedImage reduce_area(const uint8_t *gray, size_t width, size_t height, size_t stride, size_t step) {
    ReducedImage r;
    r.step = step; r.w = (width + step - 1) / step; r.h = (height + step - 1) / step;
    r.px.resize(r.w * r.h);
    std::vector<uint32_t> row(r.w);
    for (size_t sy = 0; sy < r.h; ++sy) {
        std::fill(row.begin(), row.end(), 0u);
        const size_t y0 = sy * step, y1 = std::min(y0 + step, height);
        for (size_t y = y0; y < y1; ++y) {
            const uint8_t *line = gray + y * stride;
            for (size_t sx = 0; sx < r.w; ++sx) {
                const size_t x0 = sx * step, x1 = std::min(x0 + step, width);
                uint32_t s = 0; for (size_t x = x0; x < x1; ++x) s += line[x];
                row[sx] += s;
            }
        }
        for (size_t sx = 0; sx < r.w; ++sx) {
            const size_t n = (y1 - y0) * (std::min(sx * step + step, width) - sx * step);
            r.px[sy * r.w + sx] = static_cast<uint8_t>((row[sx] + n / 2) / n);
        }
    }
    r.sum.assign((r.w + 1) * (r.h + 1), 0); r.sq.assign((r.w + 1) * (r.h + 1), 0);
    for (size_t y = 0; y < r.h; ++y) {
        uint32_t s = 0; uint64_t q = 0;
        for (size_t x = 0; x < r.w; ++x) {
            const uint32_t v = r.px[y * r.w + x]; s += v; q += v * v;
            r.sum[(y + 1) * (r.w + 1) + x + 1] = r.sum[y * (r.w + 1) + x + 1] + s;
            r.sq[(y + 1) * (r.w + 1) + x + 1] = r.sq[y * (r.w + 1) + x + 1] + q;
        }
    }
    return r;
}

// The former whole-frame rule (10th/90th percentile), on the reduced image.
// Kept as one of several passes: it is the cheapest and still the best when a
// symbol fills the frame.
uint8_t reduced_threshold(const ReducedImage &r, bool dark_marks) {
    std::array<size_t, 256> histogram{}; size_t samples = 0;
    for (size_t y = 0; y < r.h; y += 4) for (size_t x = 0; x < r.w; x += 4) { ++histogram[r.px[y * r.w + x]]; ++samples; }
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

// ---- Border quadrilateral from a component ---------------------------------

bool line_intersection(const Point2 &a0, const Point2 &a1, const Point2 &b0, const Point2 &b1, Point2 &out) {
    const double dax = a1.x - a0.x, day = a1.y - a0.y, dbx = b1.x - b0.x, dby = b1.y - b0.y;
    const double det = dax * dby - day * dbx;
    if (std::abs(det) < 1e-9) return false;
    const double t = ((b0.x - a0.x) * dby - (b0.y - a0.y) * dbx) / det;
    out = {a0.x + t * dax, a0.y + t * day};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

// Reduces a convex hull to its four dominant sides: repeatedly drop the edge
// whose removal (extending its two neighbours until they meet) adds the least
// area. The short edges of a rounded corner go first, so the result is the
// enclosing quadrilateral of the stroke's outer edge. Unlike a minimum-area
// rectangle, it follows a keystoned (perspective) outline at any tilt.
bool hull_to_quad(std::vector<Point2> hull, std::array<Point2, 4> &quad) {
    if (hull.size() < 4) return false;
    if (hull.size() > 120) {
        // Keep the hull's extreme vertex in each of 120 directions: the same
        // outline to within a fraction of a pixel, at a fixed cost.
        std::vector<size_t> keep;
        for (int k = 0; k < 120; ++k) {
            const double a = kTau * k / 120.0, dx = std::cos(a), dy = std::sin(a);
            size_t best = 0; double best_v = -1e300;
            for (size_t i = 0; i < hull.size(); ++i) { const double v = hull[i].x * dx + hull[i].y * dy; if (v > best_v) { best_v = v; best = i; } }
            keep.push_back(best);
        }
        std::sort(keep.begin(), keep.end()); keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
        std::vector<Point2> reduced; for (size_t i : keep) reduced.push_back(hull[i]);
        hull.swap(reduced);
        if (hull.size() < 4) return false;
    }
    while (hull.size() > 4) {
        const size_t n = hull.size();
        double best = std::numeric_limits<double>::infinity(); size_t best_i = n; Point2 best_p{};
        for (size_t i = 0; i < n; ++i) {
            // Edge i runs hull[i] -> hull[i + 1]; neighbours are (i - 1 -> i) and (i + 1 -> i + 2).
            const Point2 &p0 = hull[(i + n - 1) % n], &p1 = hull[i], &p2 = hull[(i + 1) % n], &p3 = hull[(i + 2) % n];
            Point2 x{};
            if (!line_intersection(p0, p1, p2, p3, x)) continue;
            // The meeting point must lie beyond the edge (outside the hull), ahead of p1 along p0 -> p1.
            if ((x.x - p1.x) * (p1.x - p0.x) + (x.y - p1.y) * (p1.y - p0.y) < 0) continue;
            if ((x.x - p2.x) * (p2.x - p3.x) + (x.y - p2.y) * (p2.y - p3.y) < 0) continue;
            const double area = std::abs(cross(p1, x, p2)) / 2;
            if (area < best) { best = area; best_i = i; best_p = x; }
        }
        if (best_i == n) return false;
        const size_t j = (best_i + 1) % n;
        hull[best_i] = best_p;
        hull.erase(hull.begin() + static_cast<std::ptrdiff_t>(j));
    }
    for (size_t i = 0; i < 4; ++i) quad[i] = hull[i];
    return true;
}

// Fits the outer edge of a border component as a perspective quadrilateral.
// 1. hull_to_quad gives four side directions at any tilt. 2. Each side is
// re-fitted as a least-squares line through the component's points that lie
// in a band along the middle of that side (rounded corners excluded), then
// pushed out to the stroke's outer edge.
bool fit_border_quad_perspective(const std::vector<Point2> &points, const std::vector<Point2> &hull, double step, std::array<double, 8> &out) {
    std::array<Point2, 4> q{};
    if (!hull_to_quad(hull, q)) return false;
    double area = 0; for (int i = 0; i < 4; ++i) area += q[i].x * q[(i + 1) % 4].y - q[(i + 1) % 4].x * q[i].y;
    if (std::abs(area) < 1.0) return false;
    Point2 centre{0, 0}; for (const auto &p : q) { centre.x += p.x / 4; centre.y += p.y / 4; }
    struct Line { double nx, ny, d; };
    std::array<Line, 4> lines{};
    for (int s = 0; s < 4; ++s) {
        const Point2 &a = q[s], &b = q[(s + 1) % 4];
        const double len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 4 * step) return false;
        const double tx = (b.x - a.x) / len, ty = (b.y - a.y) / len;
        double nx = ty, ny = -tx;
        if ((centre.x - a.x) * nx + (centre.y - a.y) * ny > 0) { nx = -nx; ny = -ny; }  // outward
        const double depth = std::abs((centre.x - a.x) * nx + (centre.y - a.y) * ny);  // side to centre
        const double band = std::max(3.0 * step, 0.10 * depth);
        std::vector<Point2> side;
        for (const auto &p : points) {
            const double along = (p.x - a.x) * tx + (p.y - a.y) * ty, across = (p.x - a.x) * nx + (p.y - a.y) * ny;
            if (across > -band && along > 0.19 * len && along < 0.81 * len) side.push_back(p);
        }
        Line line{nx, ny, a.x * nx + a.y * ny};
        if (side.size() >= 8) {
            double mx = 0, my = 0; for (const auto &p : side) { mx += p.x; my += p.y; } mx /= side.size(); my /= side.size();
            double sxx = 0, syy = 0, sxy = 0;
            for (const auto &p : side) { sxx += (p.x - mx) * (p.x - mx); syy += (p.y - my) * (p.y - my); sxy += (p.x - mx) * (p.y - my); }
            const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);
            double lx = -std::sin(theta), ly = std::cos(theta);
            if (lx * nx + ly * ny < 0) { lx = -lx; ly = -ly; }
            if (lx * nx + ly * ny > 0.966) { line.nx = lx; line.ny = ly; }  // within 15 degrees of the hull side
            std::vector<double> outward; outward.reserve(side.size());
            for (const auto &p : side) outward.push_back(p.x * line.nx + p.y * line.ny);
            std::sort(outward.begin(), outward.end());
            line.d = outward[static_cast<size_t>(0.95 * (outward.size() - 1))] + 0.5 * step;
        } else {
            line.d += 0.5 * step;
        }
        lines[s] = line;
    }
    for (int c = 0; c < 4; ++c) {
        const Line &a = lines[(c + 3) % 4], &b = lines[c];
        const double det = a.nx * b.ny - a.ny * b.nx;
        if (std::abs(det) < 1e-9) return false;
        out[2 * c] = (a.d * b.ny - a.ny * b.d) / det;
        out[2 * c + 1] = (a.nx * b.d - a.d * b.nx) / det;
    }
    return std::all_of(out.begin(), out.end(), [](double v) { return std::isfinite(v); }) && valid_quad(out.data());
}

struct BorderCandidate { std::array<double, 8> quad; double rank; };
double quad_corner_distance(const double a[8], const double b[8]);

// Hard limits on the border search, so no frame can make it run on (a noisy
// frame can hold tens of thousands of components).
constexpr size_t kMaxBorderFits = 160;        // quadrilateral fits per mask
constexpr size_t kMaxSkeletonRanked = 32;     // candidates scored against the skeleton

// Connected components of a mark mask that look like a border outline. The
// mask is consumed: it is copied into a frame padded by one empty pixel so the
// fill needs no bounds checks, and each pixel is cleared as it is queued.
void collect_border_candidates(const std::vector<uint8_t> &mask, const ReducedImage &r, size_t min_side_px,
                               std::vector<uint8_t> &visited, std::vector<uint32_t> &queue, std::vector<BorderCandidate> &out, bool *cut) {
    const size_t sw = r.w, sh = r.h, step = r.step, pw = sw + 2;
    visited.assign(pw * (sh + 2), 0);  // padded working copy of the mask
    for (size_t y = 0; y < sh; ++y) std::copy_n(mask.begin() + static_cast<std::ptrdiff_t>(y * sw), sw, visited.begin() + static_cast<std::ptrdiff_t>((y + 1) * pw + 1));
    const std::ptrdiff_t around[8] = {-static_cast<std::ptrdiff_t>(pw) - 1, -static_cast<std::ptrdiff_t>(pw), -static_cast<std::ptrdiff_t>(pw) + 1, -1, 1,
                                      static_cast<std::ptrdiff_t>(pw) - 1, static_cast<std::ptrdiff_t>(pw), static_cast<std::ptrdiff_t>(pw) + 1};
    size_t fits = 0;
    for (size_t sy = 0; sy < sh; ++sy) for (size_t sx = 0; sx < sw; ++sx) {
        const size_t start = (sy + 1) * pw + sx + 1;
        if (!visited[start]) continue;
        visited[start] = 0; queue.assign(1, static_cast<uint32_t>(start));
        size_t min_x = sx, max_x = sx, min_y = sy, max_y = sy;
        for (size_t head = 0; head < queue.size(); ++head) {
            const size_t at = queue[head], x = at % pw - 1, y = at / pw - 1;
            min_x = std::min(min_x, x); max_x = std::max(max_x, x); max_y = std::max(max_y, y);
            for (const std::ptrdiff_t offset : around) {
                const size_t n = static_cast<size_t>(static_cast<std::ptrdiff_t>(at) + offset);
                if (visited[n]) { visited[n] = 0; queue.push_back(static_cast<uint32_t>(n)); }
            }
        }
        // Back to unpadded indices for the fit.
        for (auto &index : queue) index = static_cast<uint32_t>((index / pw - 1) * sw + (index % pw - 1));
        const size_t bw = max_x - min_x + 1, bh = max_y - min_y + 1, side = std::min(bw, bh) * step;
        // The whole border must be in view: a component touching the frame
        // edge is clipped (or is a display bezel).
        if (queue.size() < 24 || side < 20 * step) continue;
        if (min_x == 0 || min_y == 0 || max_x + 1 >= sw || max_y + 1 >= sh) {
            // A large, thin, open outline against the edge looks like a
            // symbol that is partly out of frame: tell the caller to back off.
            if (cut && side * 10 >= min_side_px && static_cast<double>(queue.size()) < 0.2 * static_cast<double>(bw * bh) &&
                !(min_x == 0 && min_y == 0 && max_x + 1 >= sw && max_y + 1 >= sh)) *cut = true;
            continue;
        }
        const double aspect = static_cast<double>(bw) / bh;
        // A rotated square's box stays square, and a tilted one narrows to
        // the cosine of the tilt; a filled panel or blob fails the fill test.
        const double fill = static_cast<double>(queue.size()) / static_cast<double>(bw * bh);
        // An outline has at least about one pixel per unit of its box's perimeter.
        if (aspect <= 0.30 || aspect >= 3.3 || fill >= 0.38) continue;
        if (fits >= kMaxBorderFits) return;
        ++fits;
        std::vector<Point2> points; points.reserve(queue.size());
        for (uint32_t index : queue) points.push_back({static_cast<double>((index % sw) * step) + 0.5 * (step - 1), static_cast<double>((index / sw) * step) + 0.5 * (step - 1)});
        // Two fits of the same outline: the perspective fit follows any tilt;
        // the rectangle-based fit is steadier on near-frontal symbols. Both
        // go forward (near-duplicates are merged later).
        BorderCandidate c{}, alternative{}; bool has_alternative = false;
        if (fit_border_quad_perspective(points, convex_hull(points), static_cast<double>(step), c.quad)) has_alternative = fit_border_quad(points, static_cast<double>(step), alternative.quad) && valid_quad(alternative.quad.data());
        else if (!fit_border_quad(points, static_cast<double>(step), c.quad)) continue;
        // Shape rank from the fitted quadrilateral's own side lengths, so a
        // tilted border is not penalised for its bounding box.
        double sides[4]; for (int i = 0; i < 4; ++i) sides[i] = std::hypot(c.quad[2 * ((i + 1) % 4)] - c.quad[2 * i], c.quad[2 * ((i + 1) % 4) + 1] - c.quad[2 * i + 1]);
        const double longest = *std::max_element(sides, sides + 4), shortest = *std::min_element(sides, sides + 4);
        if (shortest < 16.0 || longest > 6.0 * shortest) continue;
        const double quad_aspect = std::min(std::abs(std::log(aspect)), std::abs(std::log((sides[0] + sides[2]) / (sides[1] + sides[3]))));
        c.rank = border_rank(quad_aspect, fill, static_cast<double>(side) / static_cast<double>(min_side_px));
        out.push_back(c);
        if (has_alternative) {
            // When the two fits nearly agree the view is close to frontal:
            // the rectangle-based fit goes first there.
            const double apart = quad_corner_distance(c.quad.data(), alternative.quad.data());
            if (apart <= 0.015 * longest) { out.back().quad = alternative.quad; }
            else { alternative.rank = c.rank - 1e-6; out.push_back(alternative); }
        }
    }
}

double quad_corner_distance(const double a[8], const double b[8]) {
    double best = std::numeric_limits<double>::infinity();
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        double worst = 0;
        for (int i = 0; i < 4; ++i) { const int j = (start + (reverse ? -i : i) + 8) % 4; worst = std::max(worst, std::hypot(a[2 * i] - b[2 * j], a[2 * i + 1] - b[2 * j + 1])); }
        best = std::min(best, worst);
    }
    return best;
}

double quad_mean_side(const double q[8]) {
    double s = 0; for (int i = 0; i < 4; ++i) s += std::hypot(q[2 * ((i + 1) % 4)] - q[2 * i], q[2 * ((i + 1) % 4) + 1] - q[2 * i + 1]);
    return s / 4;
}

}  // namespace

namespace {

// ---- Profile 0 site reading --------------------------------------------------
// Neighbour tables for intersymbol-interference cancellation. For each site of
// a family's scattered layout: up to 8 neighbours (-1 none, -2 a fixed printed
// mark). Cartesian slots 0,1 east/west, 2,3 north/south, 4,5 and 6,7 the two
// diagonals; ring and dial slots 0,1 the previous and next slot.
struct SiteNeighbours { std::vector<std::array<int32_t, 8>> of; };

const SiteNeighbours &site_neighbours(int v, int family) {
    static std::array<std::once_flag, kScalableVersions * kScalableFamilies> once;
    static std::array<std::unique_ptr<SiteNeighbours>, kScalableVersions * kScalableFamilies> cache;
    const size_t slot = static_cast<size_t>(family * kScalableVersions + v - 1);
    std::call_once(once[slot], [&] {
        const ScalableLayout &L = scalable_family_layout(v, family);
        auto n = std::make_unique<SiteNeighbours>();
        std::array<int32_t, 8> none; none.fill(-1);
        n->of.assign(L.nodes.size(), none);
        const int G = L.grid;
        std::vector<int32_t> lattice(static_cast<size_t>(G) * G, -1), dial(static_cast<size_t>(L.dial_slots), -1);
        for (size_t i = 0; i < L.nodes.size(); ++i) {
            const auto &node = L.nodes[i];
            if (node.kind == 0) lattice[static_cast<size_t>((node.id >> 12) & 0xfffu) * G + (node.id & 0xfffu)] = static_cast<int32_t>(i);
            else if (node.kind == 1) dial[static_cast<size_t>(node.slot)] = static_cast<int32_t>(i);
        }
        for (int s = 0; s < L.dial_slots; ++s) if (L.reserved_ray(s)) dial[static_cast<size_t>(s)] = -2;  // Ray Keys reach the sample radius
        constexpr int offsets[8][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}};
        for (size_t i = 0; i < L.nodes.size(); ++i) {
            const auto &node = L.nodes[i];
            if (node.kind == 0) {
                const int row = static_cast<int>((node.id >> 12) & 0xfffu), col = static_cast<int>(node.id & 0xfffu);
                for (int k = 0; k < 8; ++k) {
                    const int r = row + offsets[k][0], c = col + offsets[k][1];
                    if (r >= 0 && c >= 0 && r < G && c < G) n->of[i][static_cast<size_t>(k)] = lattice[static_cast<size_t>(r) * G + c];
                }
            } else if (node.kind == 1) {
                n->of[i][0] = dial[static_cast<size_t>((node.slot + L.dial_slots - 1) % L.dial_slots)];
                n->of[i][1] = dial[static_cast<size_t>((node.slot + 1) % L.dial_slots)];
            } else {
                const int S = L.ring_slots[static_cast<size_t>(node.ring)];
                n->of[i][0] = L.ring_grid[static_cast<size_t>(node.ring) * L.ring_stride + static_cast<size_t>((node.slot + S - 1) % S)];
                n->of[i][1] = L.ring_grid[static_cast<size_t>(node.ring) * L.ring_stride + static_cast<size_t>((node.slot + 1) % S)];
            }
        }
        cache[slot] = std::move(n);
    });
    return *cache[slot];
}

// Solves the d x d system a * x = b in place (d <= 6); false if singular.
bool solve_small(double a[6][6], double b[6], int d) {
    for (int col = 0; col < d; ++col) {
        int pivot = col;
        for (int row = col + 1; row < d; ++row) if (std::abs(a[row][col]) > std::abs(a[pivot][col])) pivot = row;
        if (std::abs(a[pivot][col]) < 1e-9) return false;
        if (pivot != col) { for (int j = 0; j < d; ++j) std::swap(a[pivot][j], a[col][j]); std::swap(b[pivot], b[col]); }
        for (int row = 0; row < d; ++row) if (row != col) {
            const double f = a[row][col] / a[col][col];
            for (int j = col; j < d; ++j) a[row][j] -= f * a[col][j];
            b[row] -= f * b[col];
        }
    }
    for (int i = 0; i < d; ++i) b[i] /= a[i][i];
    return true;
}

// One reading of every site of a family's scattered layout.
struct SiteRead {
    std::vector<uint8_t> printed;  // estimated printed value per site
    std::vector<float> confidence; // 0 = unknown (erased) .. large = certain, in noise standard deviations
    size_t unknown = 0;            // sites outside the image or in a washed-out region
};

// Mean and deviation of the known values within +/- radius of index i on a
// circular sequence (prefix sums over three laps).
struct CircularStats {
    std::vector<double> sum, sq; std::vector<int> count; int n = 0;
    void build(const std::vector<double> &value, const std::vector<uint8_t> &valid) {
        n = static_cast<int>(value.size());
        sum.assign(static_cast<size_t>(3 * n + 1), 0); sq = sum; count.assign(static_cast<size_t>(3 * n + 1), 0);
        for (int i = 0; i < 3 * n; ++i) {
            const size_t k = static_cast<size_t>(i % n), o = static_cast<size_t>(i);
            sum[o + 1] = sum[o] + (valid[k] ? value[k] : 0); sq[o + 1] = sq[o] + (valid[k] ? value[k] * value[k] : 0); count[o + 1] = count[o] + (valid[k] ? 1 : 0);
        }
    }
    void window(int i, int radius, double &s, double &q, int &c) const {
        radius = std::min(radius, (n - 1) / 2);
        const size_t lo = static_cast<size_t>(n + i - radius), hi = static_cast<size_t>(n + i + radius + 1);
        s = sum[hi] - sum[lo]; q = sq[hi] - sq[lo]; c = count[hi] - count[lo];
    }
};

// Sequence detection along the rings, with a physical model. Ring slots sit
// as close as 0.3 module apart, so blur makes each sample a mix of several
// neighbouring dots and a per-site threshold fails long before the modules do.
//
// What is printed on a ring is known exactly as a function of the bits: a dot
// of the layout's diameter at each printed slot, and, where two neighbours are
// both printed, ink filling the gap between them (dots join into arcs). Blur
// spreads that ink along the ring. So the sample at any slot is
//     base + gain * [ sum_j bit_j D(distance to dot j) + join * sum_j bit_j bit_j+1 J(distance to gap j) ]
// where D and J are a dot and a gap smeared by a Gaussian of the blur's width.
// Nothing about the kernels is fitted from uncertain decisions:
//   * the blur comes from the module lattice, where the reader has already
//     measured how far each module leaks east-west, north-south and along
//     both diagonals (a 2x2 covariance, so motion blur has its direction).
//     Ring geometry is a fixed multiple of the module pitch, which converts
//     that measurement into ring slots. Each part of a ring uses the blur
//     along its own direction of travel;
//   * base and gain are the levels of long gaps and long arcs, which stay
//     readable under blur that hides single dots.
// The most likely bit sequence under the model is found with the Viterbi
// algorithm. An isolated dot is then read by how much it lifts the known
// tail of its neighbours, not by an absolute threshold.
constexpr int kRingReach = 3;     // neighbours on each side that the detector models
constexpr int kRingSectors = 8;   // directions of travel around a ring

struct RingModel {
    double base = 0, gain = 1, join = 1;   // join: 1 joined arcs, 0 separate dots
    double cov_xx = 0, cov_xy = 0, cov_yy = 0;  // blur covariance, canonical units squared
    double half = 0.1;                     // the dot's radius, canonical units
    double noise = 0.1; bool valid = false;
};

struct RingLine {
    int slots = 0; double spacing = 0;       // slot pitch in canonical units
    std::vector<int32_t> site;               // site index per slot, -1 none
    std::vector<int8_t> fixed;               // known printed value (0/1), -1 for data
    std::vector<double> y; std::vector<uint8_t> has;  // normalised sample per slot
    std::vector<uint8_t> bit;                // current decision per slot
    // Kernels per sector: dot[t + reach] for the dot t slots away, gap[t + reach + 1]
    // for the gap between slots t and t + 1.
    std::array<std::array<double, 2 * kRingReach + 1>, kRingSectors> dot{};
    std::array<std::array<double, 2 * kRingReach + 2>, kRingSectors> gap{};
    int sector(int s) const { return std::min(kRingSectors - 1, s * kRingSectors / std::max(1, slots)); }
    // Registration of this ring: how far along the ring (canonical units) the
    // dots really sit from where the samples were taken, and the ring's own
    // levels (a radial offset shows up as a lower gain).
    double shift = 0, base = 0, gain = 1, noise = 1;
    // Weight of the gap kernel between two printed neighbours: +join where
    // dots are further apart than their diameter (the gap is filled when
    // dots join into arcs), -1 where they are closer (the dots overlap, and
    // the overlap must not be counted twice).
    double pair = 0;
};

double normal_cdf(double x) { return 0.5 * std::erfc(-x * 0.70710678118654752440); }

// Fraction of full ink seen at distance x from the centre of a strip of half-width `half`, blur sigma.
double smeared_strip(double x, double half, double sigma) { return normal_cdf((x + half) / sigma) - normal_cdf((x - half) / sigma); }

void prepare_ring(RingLine &line, const RingModel &m, double pitch, double shift = 0) {
    line.shift = shift;
    for (int k = 0; k < kRingSectors; ++k) {
        // Direction of travel in the middle of the sector (slot angle a is measured from the top, clockwise).
        const double a = kTau * (k + 0.5) / kRingSectors, tx = std::cos(a), ty = std::sin(a);
        const double variance = m.cov_xx * tx * tx + 2 * m.cov_xy * tx * ty + m.cov_yy * ty * ty;
        const double sigma = std::sqrt(std::max(variance, (0.05 * pitch) * (0.05 * pitch)));
        const double half_gap = line.spacing / 2 - m.half;
        line.pair = half_gap > 0 ? m.join : -1.0;
        for (int t = -kRingReach; t <= kRingReach; ++t) line.dot[static_cast<size_t>(k)][static_cast<size_t>(t + kRingReach)] = smeared_strip(t * line.spacing + shift, m.half, sigma);
        for (int t = -kRingReach - 1; t <= kRingReach; ++t) line.gap[static_cast<size_t>(k)][static_cast<size_t>(t + kRingReach + 1)] = half_gap != 0 ? smeared_strip((t + 0.5) * line.spacing + shift, std::abs(half_gap), sigma) : 0.0;
    }
}

// Ink seen at slot s under the current decisions (1 = a long arc).
double ring_ink(const RingLine &line, const RingModel &, int s) {
    const int S = line.slots; const auto &dot = line.dot[static_cast<size_t>(line.sector(s))]; const auto &gap = line.gap[static_cast<size_t>(line.sector(s))];
    double ink = 0;
    for (int t = -kRingReach - 1; t <= kRingReach; ++t) {
        const int j = ((s + t) % S + S) % S;
        if (!line.bit[static_cast<size_t>(j)]) continue;
        if (t >= -kRingReach) ink += dot[static_cast<size_t>(t + kRingReach)];
        if (line.pair != 0 && line.bit[static_cast<size_t>((j + 1) % S)]) ink += line.pair * gap[static_cast<size_t>(t + kRingReach + 1)];
    }
    return ink;
}

// Base and gain of one ring from its own samples and current decisions.
bool fit_line_levels(RingLine &line, const RingModel &m) {
    double n = 0, sf = 0, sy = 0, sff = 0, sfy = 0, syy = 0;
    for (int s = 0; s < line.slots; ++s) {
        if (!line.has[static_cast<size_t>(s)]) continue;
        const double f = ring_ink(line, m, s), y = line.y[static_cast<size_t>(s)];
        n += 1; sf += f; sy += y; sff += f * f; sfy += f * y; syy += y * y;
    }
    const double det = n * sff - sf * sf;
    if (n < 24 || det < 1e-9 * n * n) return false;
    const double gain = (n * sfy - sf * sy) / det, base = (sy - gain * sf) / n;
    if (gain <= 0.03) return false;
    line.base = base; line.gain = gain;
    line.noise = std::sqrt(std::max(0.0, (syy - 2 * base * sy - 2 * gain * sfy + n * base * base + 2 * base * gain * sf + gain * gain * sff) / n));
    return true;
}

// Most likely bits of one ring under the model (Viterbi over a window of
// 2 * reach + 1 slots; the ring is unrolled with an overlap so it closes).
void detect_ring(RingLine &line, const RingModel &) {
    const int S = line.slots;
    int reach = 1;
    for (int k = 0; k < kRingSectors; ++k) while (reach < kRingReach && std::max(line.dot[static_cast<size_t>(k)][static_cast<size_t>(kRingReach + reach + 1)], line.dot[static_cast<size_t>(k)][static_cast<size_t>(kRingReach - reach - 1)]) > 0.03) ++reach;
    if (S < 4 * reach + 4) return;
    const int width = 2 * reach + 1, states = 1 << (width - 1), windows = 1 << width;
    // Prediction for every window, per sector; bit (width - 1 - k) is the slot at offset k - reach.
    std::vector<double> prediction(static_cast<size_t>(kRingSectors) * windows);
    for (int sector = 0; sector < kRingSectors; ++sector) for (int w = 0; w < windows; ++w) {
        double ink = 0;
        for (int k = 0; k < width; ++k) {
            if (!((w >> (width - 1 - k)) & 1)) continue;
            ink += line.dot[static_cast<size_t>(sector)][static_cast<size_t>(k - reach + kRingReach)];
            if (k + 1 < width && ((w >> (width - 2 - k)) & 1)) ink += line.pair * line.gap[static_cast<size_t>(sector)][static_cast<size_t>(k - reach + kRingReach + 1)];
        }
        prediction[static_cast<size_t>(sector) * windows + w] = line.base + line.gain * ink;
    }
    const int overlap = 4 * reach + 6, length = S + 2 * overlap;
    constexpr double kInfinity = 1e300;
    std::vector<double> cost(static_cast<size_t>(states), 0.0), next(static_cast<size_t>(states));
    std::vector<uint8_t> back(static_cast<size_t>(length) * states);
    // Step i decides the slot at unrolled position i + reach and scores the sample at position i.
    for (int i = 0; i < length; ++i) {
        const int centre = ((i - overlap) % S + S) % S, incoming = ((i - overlap + reach) % S + S) % S;
        const int8_t fixed = line.fixed[static_cast<size_t>(incoming)];
        const bool scored = line.has[static_cast<size_t>(centre)] != 0; const double y = line.y[static_cast<size_t>(centre)];
        const double *table = prediction.data() + static_cast<size_t>(line.sector(centre)) * windows;
        std::fill(next.begin(), next.end(), kInfinity);
        for (int state = 0; state < states; ++state) {
            if (cost[static_cast<size_t>(state)] >= kInfinity) continue;
            for (int bit = 0; bit < 2; ++bit) {
                if (fixed >= 0 && bit != fixed) continue;
                const int window = (state << 1) | bit, to = window & (states - 1);
                double c = cost[static_cast<size_t>(state)];
                if (scored && i >= reach) { const double d = y - table[window]; c += d * d; }
                if (c < next[static_cast<size_t>(to)]) { next[static_cast<size_t>(to)] = c; back[static_cast<size_t>(i) * states + to] = static_cast<uint8_t>(state >> (width - 2)); }
            }
        }
        cost.swap(next);
    }
    int state = static_cast<int>(std::min_element(cost.begin(), cost.end()) - cost.begin());
    for (int i = length - 1; i >= 0; --i) {
        const int position = i - overlap + reach;  // slot decided at step i
        if (position >= 0 && position < S && i >= overlap && i < overlap + S + reach) line.bit[static_cast<size_t>(position)] = static_cast<uint8_t>(state & 1);
        const int dropped = back[static_cast<size_t>(i) * states + state];
        state = (state >> 1) | (dropped << (width - 2));
    }
    for (int s = 0; s < S; ++s) if (line.fixed[static_cast<size_t>(s)] >= 0) line.bit[static_cast<size_t>(s)] = static_cast<uint8_t>(line.fixed[static_cast<size_t>(s)]);
}

// Refits one ring's kernels freely from its own decisions: a level for every
// dot offset and every gap offset in reach (least squares, lightly
// regularised towards the current kernels). The physical model starts the
// detector; this lets the ring then learn what the camera really did to it,
// including what the model leaves out: sharpening, the tone curve, ink that
// saturates, a residual shift (an off-centre kernel). The kernel's centre of
// mass must stay within 0.6 slot of its sample, so the refit can never
// re-label the ring one slot round. Returns false (kernels unchanged) if the
// ring is too short or the fit is unsound.
bool fit_line_taps(RingLine &line, const RingModel &m) {
    constexpr int D = 2 * kRingReach + 1, G = 2 * kRingReach + 2, N = 1 + D + G;
    const int S = line.slots; if (S < 6 * N) return false;
    const bool joins = line.pair != 0; const int n = joins ? N : 1 + D;
    std::vector<double> a(static_cast<size_t>(n) * n, 0.0), b(static_cast<size_t>(n), 0.0), f(static_cast<size_t>(n));
    int used = 0;
    for (int s = 0; s < S; ++s) {
        if (!line.has[static_cast<size_t>(s)]) continue;
        std::fill(f.begin(), f.end(), 0.0); f[0] = 1;
        for (int t = -kRingReach - 1; t <= kRingReach; ++t) {
            const int j = ((s + t) % S + S) % S;
            if (!line.bit[static_cast<size_t>(j)]) continue;
            if (t >= -kRingReach) f[static_cast<size_t>(1 + t + kRingReach)] = 1;
            if (joins && line.bit[static_cast<size_t>((j + 1) % S)]) f[static_cast<size_t>(1 + D + t + kRingReach + 1)] = 1;
        }
        const double y = line.y[static_cast<size_t>(s)];
        for (int r = 0; r < n; ++r) { if (f[static_cast<size_t>(r)] == 0) continue; b[static_cast<size_t>(r)] += y; for (int q = 0; q < n; ++q) a[static_cast<size_t>(r) * n + q] += f[static_cast<size_t>(q)]; }
        ++used;
    }
    if (used < 6 * n) return false;
    // Ridge towards the current kernels (sector 0 holds the ring's working kernel after the first refit).
    const double ridge = 0.005 * used;
    std::vector<double> prior(static_cast<size_t>(n), 0.0); prior[0] = line.base;
    for (int t = 0; t < D; ++t) prior[static_cast<size_t>(1 + t)] = line.gain * line.dot[0][static_cast<size_t>(t)];
    if (joins) for (int t = 0; t < G; ++t) prior[static_cast<size_t>(1 + D + t)] = line.gain * line.pair * line.gap[0][static_cast<size_t>(t)];
    for (int r = 1; r < n; ++r) { a[static_cast<size_t>(r) * n + r] += ridge; b[static_cast<size_t>(r)] += ridge * prior[static_cast<size_t>(r)]; }
    // Gaussian elimination with partial pivoting.
    for (int col = 0; col < n; ++col) {
        int pivot = col; for (int r = col + 1; r < n; ++r) if (std::abs(a[static_cast<size_t>(r) * n + col]) > std::abs(a[static_cast<size_t>(pivot) * n + col])) pivot = r;
        if (std::abs(a[static_cast<size_t>(pivot) * n + col]) < 1e-9) return false;
        if (pivot != col) { for (int q = 0; q < n; ++q) std::swap(a[static_cast<size_t>(pivot) * n + q], a[static_cast<size_t>(col) * n + q]); std::swap(b[static_cast<size_t>(pivot)], b[static_cast<size_t>(col)]); }
        for (int r = 0; r < n; ++r) if (r != col) {
            const double k = a[static_cast<size_t>(r) * n + col] / a[static_cast<size_t>(col) * n + col];
            if (k == 0) continue;
            for (int q = col; q < n; ++q) a[static_cast<size_t>(r) * n + q] -= k * a[static_cast<size_t>(col) * n + q];
            b[static_cast<size_t>(r)] -= k * b[static_cast<size_t>(col)];
        }
    }
    std::vector<double> w(static_cast<size_t>(n)); for (int r = 0; r < n; ++r) w[static_cast<size_t>(r)] = b[static_cast<size_t>(r)] / a[static_cast<size_t>(r) * n + r];
    // Sanity: a positive kernel centred on its own slot.
    double mass = 0, moment = 0;
    for (int t = 0; t < D; ++t) { const double v = std::max(0.0, w[static_cast<size_t>(1 + t)]); mass += v; moment += v * (t - kRingReach); }
    if (mass <= 0.02 || std::abs(moment / mass) > 0.6 || w[static_cast<size_t>(1 + kRingReach)] <= 0) return false;
    line.base = w[0]; line.gain = 1;
    for (int k = 0; k < kRingSectors; ++k) {
        for (int t = 0; t < D; ++t) line.dot[static_cast<size_t>(k)][static_cast<size_t>(t)] = w[static_cast<size_t>(1 + t)];
        for (int t = 0; t < G; ++t) line.gap[static_cast<size_t>(k)][static_cast<size_t>(t)] = joins ? w[static_cast<size_t>(1 + D + t)] / line.pair : 0.0;
    }
    double sum_sq = 0;
    for (int s = 0; s < S; ++s) if (line.has[static_cast<size_t>(s)]) { const double r = line.y[static_cast<size_t>(s)] - line.base - ring_ink(line, m, s); sum_sq += r * r; }
    line.noise = std::sqrt(sum_sq / used);
    return true;
}

// Detects one ring at a given shift: levels from its own long gaps and long
// arcs, then detection and a level refit, twice. Returns the residual.
double detect_ring_at(RingLine &line, const RingModel &m, double pitch, double shift) {
    prepare_ring(line, m, pitch, shift);
    std::vector<double> values;
    for (int s = 0; s < line.slots; ++s) if (line.has[static_cast<size_t>(s)]) values.push_back(line.y[static_cast<size_t>(s)]);
    if (values.size() < 24) return std::numeric_limits<double>::infinity();
    std::sort(values.begin(), values.end());
    line.base = values[values.size() / 10]; line.gain = std::max(0.05, values[values.size() * 9 / 10] - line.base);
    for (int round = 0; round < 2; ++round) { detect_ring(line, m); if (!fit_line_levels(line, m)) return std::numeric_limits<double>::infinity(); }
    return line.noise;
}

// Hard expectation-maximisation on one ring from its current kernels:
// detect, let the ring relearn its kernels from the decisions, repeat.
void refine_ring(RingLine &line, const RingModel &m) {
    for (int round = 0; round < 4; ++round) {
        const std::vector<uint8_t> before = line.bit;
        if (!fit_line_taps(line, m)) break;
        detect_ring(line, m);
        if (line.bit == before || work_exhausted()) break;
    }
}

// Copies a converged ring's learned kernels to another ring, resampled for
// the other ring's slot spacing (the kernels are functions of distance
// along the ring, and neighbouring rings see the same blur).
void adopt_kernels(RingLine &line, const RingLine &from) {
    const auto at = [](const double *taps, int count, double x) {  // linear interpolation, zero outside
        if (x <= -1 || x >= count) return 0.0;
        const int i = static_cast<int>(std::floor(x)); const double f = x - i;
        const double lo = i >= 0 && i < count ? taps[i] : 0.0, hi = i + 1 >= 0 && i + 1 < count ? taps[i + 1] : 0.0;
        return lo * (1 - f) + hi * f;
    };
    const double ratio = line.spacing / from.spacing;
    const double pair = line.pair != 0 ? line.pair : 1.0;
    for (int k = 0; k < kRingSectors; ++k) {
        for (int t = -kRingReach; t <= kRingReach; ++t)
            line.dot[static_cast<size_t>(k)][static_cast<size_t>(t + kRingReach)] = from.gain * at(from.dot[static_cast<size_t>(k)].data(), 2 * kRingReach + 1, t * ratio + kRingReach);
        for (int t = -kRingReach - 1; t <= kRingReach; ++t)
            line.gap[static_cast<size_t>(k)][static_cast<size_t>(t + kRingReach + 1)] = from.gain * from.pair * at(from.gap[static_cast<size_t>(k)].data(), 2 * kRingReach + 2, (t + 0.5) * ratio + kRingReach + 0.5) / pair;
    }
    line.base = from.base; line.gain = 1;
}

// Detects every ring. Each starts from the physical model and refines its
// own kernels. The refinement can settle on a wrong reading when its start
// is poor; such a ring shows a residual well above its neighbours'. It is
// then restarted from the kernels a well-converged neighbour learned, and
// keeps whichever reading explains its samples better.
void register_and_detect_rings(std::vector<RingLine> &rings, const RingModel &m, double pitch) {
    for (auto &line : rings) { detect_ring_at(line, m, pitch, 0.0); refine_ring(line, m); if (work_exhausted()) return; }
    const int R = static_cast<int>(rings.size());
    if (R < 2) return;
    for (int sweep = 0; sweep < 3; ++sweep) {
        std::vector<double> noise; for (const auto &line : rings) noise.push_back(line.noise);
        std::vector<double> sorted = noise; std::sort(sorted.begin(), sorted.end());
        const double good = sorted[static_cast<size_t>(R / 3)];  // a well-converged ring's residual
        bool changed = false;
        for (int k = 0; k < R; ++k) {
            if (noise[static_cast<size_t>(k)] <= 1.25 * good) continue;
            // Nearest well-converged ring.
            int source = -1;
            for (int d = 1; d < R && source < 0; ++d) for (const int j : {k - d, k + d}) if (j >= 0 && j < R && rings[static_cast<size_t>(j)].noise <= 1.15 * good) { source = j; break; }
            if (source < 0) continue;
            RingLine trial = rings[static_cast<size_t>(k)];
            adopt_kernels(trial, rings[static_cast<size_t>(source)]);
            detect_ring(trial, m);
            if (!fit_line_levels(trial, m)) continue;
            detect_ring(trial, m);
            refine_ring(trial, m);
            if (trial.noise < 0.9 * rings[static_cast<size_t>(k)].noise) { rings[static_cast<size_t>(k)] = trial; changed = true; }
            if (work_exhausted()) return;
        }
        if (!changed) break;
    }
}

// How firmly each ring decision stands: the extra squared error of flipping
// that one bit, as a distance in noise deviations.
void ring_confidence(const RingLine &line, const RingModel &m, std::vector<float> &confidence) {
    const int S = line.slots; RingLine flipped = line;
    for (int s = 0; s < S; ++s) {
        const int32_t site = line.site[static_cast<size_t>(s)];
        if (site < 0 || line.fixed[static_cast<size_t>(s)] >= 0) continue;
        flipped.bit[static_cast<size_t>(s)] ^= 1;
        double delta = 0, signal = 0;
        for (int t = -kRingReach - 1; t <= kRingReach + 1; ++t) {
            const int u = ((s + t) % S + S) % S;
            if (!line.has[static_cast<size_t>(u)]) continue;
            const double p0 = line.base + line.gain * ring_ink(line, m, u), p1 = line.base + line.gain * ring_ink(flipped, m, u), y = line.y[static_cast<size_t>(u)];
            delta += (y - p1) * (y - p1) - (y - p0) * (y - p0); signal += (p1 - p0) * (p1 - p0);
        }
        flipped.bit[static_cast<size_t>(s)] ^= 1;
        confidence[static_cast<size_t>(site)] = signal > 1e-12 ? static_cast<float>(std::max(0.0, delta) / (2 * std::sqrt(signal) * std::max(line.noise, 0.01))) : 0.0f;
    }
}

// Blur width (standard deviation, canonical units) from how much a module
// leaks into the neighbour `distance` away, as a fraction of its own level.
// Modules are squares of side `side`; the leak ratio of a Gaussian blur is
// monotonic in its width, so it is inverted by bisection.
double blur_from_leak(double ratio, double distance, double side) {
    if (ratio <= 1e-4) return 0;
    double lo = 0.02 * distance, hi = 2.0 * distance;
    for (int it = 0; it < 40; ++it) {
        const double mid = (lo + hi) / 2;
        const double r = smeared_strip(distance, side / 2, mid) / std::max(1e-9, smeared_strip(0, side / 2, mid));
        if (r < ratio) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2;
}

// Reads every site under homography h.
//   1. Sample each site. A site whose sample leaves the image is unknown,
//      not a failure, so a symbol partly out of frame can still decode.
//   2. Normalise between the symbol's own dark and light levels (two clusters
//      of the Cartesian samples; whitening makes about half of them printed).
//   3. Local levels. Each sample is re-referenced to the mean and spread of
//      its own neighbourhood (a window of the lattice for modules, a sector
//      of the ring or dial for polar marks), so uneven light, a shadow edge
//      or a highlight does not move the threshold for the whole field. A
//      neighbourhood whose samples barely vary (glare, a covering object) is
//      washed out: its sites are unknown. Clipped is unknown, not white.
//   4. Cancel intersymbol interference. For modules and dial rays, a
//      least-squares fit of
//      sample = base + own * bit + sum(leak_c * printed neighbours in class c)
//      measures the blur leak in this image, and each site is re-decided
//      after subtracting what its neighbours contribute (separate classes
//      for east-west, north-south and the two diagonals follow motion blur).
//      Ring dots go through sequence detection (see RingModel).
//   5. Confidence is the distance from the decision level in noise units.
// The samples are taken once (SiteSamples) and can then be decided under
// several strategies (ReadMode): each strategy wins on different images, and
// Reed-Solomon plus the CRC say which reading is right.
constexpr int kDialRadii = 3;
struct SiteSamples {
    std::vector<double> raw; std::vector<uint8_t> known; size_t unknown = 0;
    std::vector<std::array<double, kDialRadii>> dial_raw;
    std::vector<double> cart_values;
    double lo = 0, hi = 0; bool dark = true;
    double edge_blur = 0;  // measured blur width in canonical units (0 = unknown)
};
constexpr unsigned kReadLocalLevels = 1u;   // re-reference each sample to its neighbourhood
constexpr unsigned kReadRingSequence = 2u;  // sequence detection along the rings
// Strategies in the order tried: the plain reading first (cheapest, right for
// clean images), then the ones built for uneven light and for blur.
constexpr std::array<unsigned, 4> kReadModes = {0u, kReadLocalLevels | kReadRingSequence, kReadRingSequence, kReadLocalLevels};
// The ML site reading (ml_sites.inc), used only when a site model is loaded.
constexpr unsigned kReadMl = 0x100u;
constexpr std::array<unsigned, 5> kReadModesWithMl = {0u, kReadMl, kReadLocalLevels | kReadRingSequence, kReadRingSequence, kReadLocalLevels};

bool scalable_sample_sites(const ScalableLayout &L, const uint8_t *gray, size_t width, size_t height, size_t stride,
                           const double h[8], SiteSamples &S) {
    const size_t N = L.nodes.size(); const double p = L.pitch;
    std::vector<double> &raw = S.raw; std::vector<uint8_t> &known = S.known;
    raw.assign(N, 0); known.assign(N, 1); S.unknown = 0;
    struct { size_t unknown = 0; } out;
    // Dial rays are read at three radii; ink spread moves the best one.
    const double dial_offset[kDialRadii] = {-0.3 * p, 0.0, 0.3 * p};
    std::vector<std::array<double, kDialRadii>> &dial_raw = S.dial_raw; dial_raw.assign(static_cast<size_t>(L.dial_slots), {});
    std::vector<double> &cart_values = S.cart_values; cart_values.clear();
    for (size_t i = 0; i < N; ++i) {
        const auto &n = L.nodes[i];
        double v;
        if (n.kind == 1) {
            v = 0;
            for (int k = 0; k < kDialRadii; ++k) {
                const double s = polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + L.dial_sample + dial_offset[k], n.slot, L.dial_slots, 0.04 * p);
                dial_raw[static_cast<size_t>(n.slot)][static_cast<size_t>(k)] = s;
                if (s < 0) v = -1;
            }
        } else v = gray_sample(gray, width, height, stride, h, n.x, n.y, n.kind == 0 ? 0.12 * p : 0.25 * L.dot);
        if (v < 0) { known[i] = 0; ++out.unknown; raw[i] = 0; if (work_exhausted()) return false; continue; }
        raw[i] = v; if (n.kind == 0) cart_values.push_back(v);
    }
    if (out.unknown * 2 > N || cart_values.size() < 32) return false;
    // Global dark and light levels from the Cartesian samples.
    double lo = 0, hi = 0;
    {
        const auto [min_it, max_it] = std::minmax_element(cart_values.begin(), cart_values.end());
        double cut = (*min_it + *max_it) / 2;
        for (int it = 0; it < 24; ++it) {
            double sl = 0, sh = 0; size_t nl = 0, nh = 0;
            for (double v : cart_values) { if (v > cut) { sh += v; ++nh; } else { sl += v; ++nl; } }
            if (!nl || !nh) return false;
            lo = sl / nl; hi = sh / nh;
            const double next = (lo + hi) / 2; if (std::abs(next - cut) < 1e-4) break; cut = next;
        }
        if (hi - lo < 4.0) return false;
    }
    // Polarity: the frame stroke and the Grid Key are solid ink, so they sit
    // on the printed side of the modules' mean level. (The stroke is wide; it
    // stays that side of the mean under blur that already merges the moat.)
    double mark = 0; int marks = 0;
    for (int r = 0; r < 2; ++r) for (int c = 0; c < 2; ++c) {
        const double v = gray_sample(gray, width, height, stride, h, (L.col0 + c + .5) * p, (L.row0 + r + .5) * p, 0.12 * p);
        if (v >= 0) { mark += v; ++marks; }
    }
    for (const double t : {7.0, 11.0, 14.5, 18.0, 22.0}) for (const auto &[x, y] : {std::pair{t, 0.5}, {28.5, t}, {t, 28.5}, {0.5, t}}) {
        const double v = gray_sample(gray, width, height, stride, h, x, y, 0.05);
        if (v >= 0) { mark += v; ++marks; }
    }
    if (marks < 6) return false;
    S.dark = mark / marks < std::accumulate(cart_values.begin(), cart_values.end(), 0.0) / static_cast<double>(cart_values.size());
    S.lo = lo; S.hi = hi; S.unknown = out.unknown;
    return true;
}

bool scalable_decide_sites(const ScalableLayout &L, const SiteSamples &S, unsigned mode, SiteRead &out) {
    const size_t N = L.nodes.size(); const double p = L.pitch;
    std::vector<double> raw = S.raw; std::vector<uint8_t> known = S.known;
    const auto &dial_raw = S.dial_raw; const double lo = S.lo, hi = S.hi; const bool dark = S.dark;
    out.unknown = S.unknown;
    const double unprinted = dark ? hi : lo, printed_level = dark ? lo : hi;
    const auto normalise = [&](double v) { return (v - unprinted) / (printed_level - unprinted); };
    std::vector<double> level(N, 0.0);
    // Dial: choose the radius whose samples split most evenly and widely.
    {
        int best = 1; double best_score = -1;
        for (int k = 0; k < kDialRadii; ++k) {
            std::vector<double> values;
            for (size_t i = 0; i < N; ++i) if (known[i] && L.nodes[i].kind == 1) values.push_back(normalise(dial_raw[static_cast<size_t>(L.nodes[i].slot)][static_cast<size_t>(k)]));
            if (values.size() < 16) break;
            double separation = 0; const double cut = two_means_cut(values, separation);
            const double above = static_cast<double>(std::count_if(values.begin(), values.end(), [&](double v) { return v > cut; })) / static_cast<double>(values.size());
            const double score = separation * std::min(1.0, std::min(above, 1 - above) / 0.35) * (k == 1 ? 1.08 : 1.0);
            if (score > best_score) { best_score = score; best = k; }
        }
        for (size_t i = 0; i < N; ++i) if (known[i] && L.nodes[i].kind == 1) raw[i] = dial_raw[static_cast<size_t>(L.nodes[i].slot)][static_cast<size_t>(best)];
    }
    for (size_t i = 0; i < N; ++i) if (known[i]) level[i] = normalise(raw[i]);
    const SiteNeighbours &nb = site_neighbours(L.version, L.family);
    // ---- Local levels: Cartesian lattice windows ----
    if (mode & kReadLocalLevels) {
        const int G = L.grid; const size_t W = static_cast<size_t>(G) + 1;
        std::vector<double> sum(W * W, 0), sq(W * W, 0); std::vector<int> cnt(W * W, 0);
        std::vector<int32_t> at(static_cast<size_t>(G) * G, -1);
        for (size_t i = 0; i < N; ++i) if (L.nodes[i].kind == 0) at[static_cast<size_t>((L.nodes[i].id >> 12) & 0xfffu) * G + (L.nodes[i].id & 0xfffu)] = static_cast<int32_t>(i);
        double gsum = 0, gsq = 0; int gn = 0;
        for (int r = 0; r < G; ++r) for (int c = 0; c < G; ++c) {
            const int32_t i = at[static_cast<size_t>(r) * G + c]; const bool ok = i >= 0 && known[static_cast<size_t>(i)];
            const double v = ok ? level[static_cast<size_t>(i)] : 0;
            if (ok) { gsum += v; gsq += v * v; ++gn; }
            const size_t o = static_cast<size_t>(r + 1) * W + static_cast<size_t>(c + 1);
            sum[o] = sum[o - 1] + sum[o - W] - sum[o - W - 1] + v; sq[o] = sq[o - 1] + sq[o - W] - sq[o - W - 1] + v * v;
            cnt[o] = cnt[o - 1] + cnt[o - W] - cnt[o - W - 1] + (ok ? 1 : 0);
        }
        const double gmean = gsum / gn, gsd = std::sqrt(std::max(1e-9, gsq / gn - gmean * gmean));
        const auto window = [&](int r, int c, int radius, double &mean, double &sd, int &n) {
            const int r0 = std::max(0, r - radius), c0 = std::max(0, c - radius), r1 = std::min(G, r + radius + 1), c1 = std::min(G, c + radius + 1);
            const size_t a = static_cast<size_t>(r0) * W + static_cast<size_t>(c0), b = static_cast<size_t>(r0) * W + static_cast<size_t>(c1), d = static_cast<size_t>(r1) * W + static_cast<size_t>(c0), e = static_cast<size_t>(r1) * W + static_cast<size_t>(c1);
            n = cnt[e] - cnt[b] - cnt[d] + cnt[a];
            if (n < 2) { mean = gmean; sd = gsd; return; }
            mean = (sum[e] - sum[b] - sum[d] + sum[a]) / n; sd = std::sqrt(std::max(0.0, (sq[e] - sq[b] - sq[d] + sq[a]) / n - mean * mean));
        };
        const int radius = std::clamp(static_cast<int>(std::lround(1.7 / p)), 3, 10);
        std::vector<double> adjusted(N, 0.0); std::vector<uint8_t> washed(N, 0);
        for (size_t i = 0; i < N; ++i) {
            if (!known[i] || L.nodes[i].kind != 0) continue;
            const int r = static_cast<int>((L.nodes[i].id >> 12) & 0xfffu), c = static_cast<int>(L.nodes[i].id & 0xfffu);
            double mean, sd; int n;
            window(r, c, 2, mean, sd, n);
            if (n >= 12 && sd < 0.3 * gsd) { washed[i] = 1; continue; }
            window(r, c, radius, mean, sd, n);
            if (n < 20) window(r, c, 2 * radius, mean, sd, n);
            // Shrink towards the global levels when the window is small.
            const double weight = static_cast<double>(n) / (static_cast<double>(n) + 12.0);
            const double m = weight * mean + (1 - weight) * gmean, s = std::max(0.35 * gsd, weight * sd + (1 - weight) * gsd);
            adjusted[i] = gmean + (level[i] - m) * gsd / s;
        }
        for (size_t i = 0; i < N; ++i) {
            if (!known[i] || L.nodes[i].kind != 0) continue;
            if (washed[i]) { known[i] = 0; ++out.unknown; } else level[i] = adjusted[i];
        }
    }
    // ---- Rings: slot tables, local levels along each ring ----
    std::vector<RingLine> rings(static_cast<size_t>(L.rings));
    {
        const int key = L.ring_key_slot(); const auto key_dots = L.ring_key_dots(); const auto key_run = L.ring_key_run();
        double gsum = 0, gsq = 0; int gn = 0;
        for (size_t i = 0; i < N; ++i) if (known[i] && L.nodes[i].kind == 2) { gsum += level[i]; gsq += level[i] * level[i]; ++gn; }
        const double gmean = gn ? gsum / gn : 0, gsd = gn ? std::sqrt(std::max(1e-9, gsq / gn - gmean * gmean)) : 1;
        for (int k = 0; k < L.rings; ++k) {
            RingLine &line = rings[static_cast<size_t>(k)]; const int S = L.ring_slots[static_cast<size_t>(k)];
            line.slots = S; line.spacing = kTau * L.ring_radius(k) / S;
            line.site.assign(static_cast<size_t>(S), -1); line.fixed.assign(static_cast<size_t>(S), 0); line.y.assign(static_cast<size_t>(S), 0); line.has.assign(static_cast<size_t>(S), 0); line.bit.assign(static_cast<size_t>(S), 0);
            for (int s = 0; s < S; ++s) {
                const int index = L.ring_grid[static_cast<size_t>(k) * L.ring_stride + static_cast<size_t>(s)];
                if (index >= 0) { line.site[static_cast<size_t>(s)] = index; line.fixed[static_cast<size_t>(s)] = -1; }
            }
            if (k == L.rings - 1) {
                for (int d : key_dots) line.fixed[static_cast<size_t>((key + d) % S)] = 1;
                for (int s = static_cast<int>(std::ceil(key_run.first)); s <= static_cast<int>(std::floor(key_run.second)); ++s) line.fixed[static_cast<size_t>((key + s) % S)] = 1;
            }
        }
        // Sector statistics pooled over up to three neighbouring rings.
        std::vector<CircularStats> stats(static_cast<size_t>(L.rings));
        for (int k = 0; k < L.rings; ++k) {
            const RingLine &line = rings[static_cast<size_t>(k)];
            std::vector<double> value(static_cast<size_t>(line.slots), 0); std::vector<uint8_t> valid(static_cast<size_t>(line.slots), 0);
            for (int s = 0; s < line.slots; ++s) { const int32_t i = line.site[static_cast<size_t>(s)]; if (i >= 0 && known[static_cast<size_t>(i)]) { value[static_cast<size_t>(s)] = level[static_cast<size_t>(i)]; valid[static_cast<size_t>(s)] = 1; } }
            stats[static_cast<size_t>(k)].build(value, valid);
        }
        std::vector<double> adjusted(N, 0.0); std::vector<uint8_t> washed(N, 0);
        if (mode & kReadLocalLevels) for (int k = 0; k < L.rings; ++k) {
            const RingLine &line = rings[static_cast<size_t>(k)];
            for (int s = 0; s < line.slots; ++s) {
                const int32_t i = line.site[static_cast<size_t>(s)];
                if (i < 0 || !known[static_cast<size_t>(i)]) continue;
                double sum = 0, sq = 0; int n = 0;
                // A sixth of a turn each way, over every ring. The window must
                // be wide: ring samples depend on their neighbours' bits, so a
                // narrow window would normalise away the data itself.
                for (int j = 0; j < L.rings; ++j) {
                    const int Sj = rings[static_cast<size_t>(j)].slots; double a, b; int c;
                    stats[static_cast<size_t>(j)].window(static_cast<int>(std::lround(static_cast<double>(s) * Sj / line.slots)) % Sj, std::max(8, Sj / 6), a, b, c);
                    sum += a; sq += b; n += c;
                }
                if (n < 48) { adjusted[static_cast<size_t>(i)] = level[static_cast<size_t>(i)]; continue; }
                const double mean = sum / n, sd = std::sqrt(std::max(0.0, sq / n - mean * mean));
                if (sd < 0.25 * gsd) { washed[static_cast<size_t>(i)] = 1; continue; }
                const double weight = static_cast<double>(n) / (static_cast<double>(n) + 64.0);
                const double m = weight * mean + (1 - weight) * gmean, sdev = std::max(0.4 * gsd, weight * sd + (1 - weight) * gsd);
                adjusted[static_cast<size_t>(i)] = gmean + (level[static_cast<size_t>(i)] - m) * gsd / sdev;
            }
        }
        if (mode & kReadLocalLevels) for (size_t i = 0; i < N; ++i) {
            if (!known[i] || L.nodes[i].kind != 2) continue;
            if (washed[i]) { known[i] = 0; ++out.unknown; } else level[i] = adjusted[i];
        }
    }
    // ---- Dial: local levels along the dial ----
    if (mode & kReadLocalLevels) {
        std::vector<double> value(static_cast<size_t>(L.dial_slots), 0); std::vector<uint8_t> valid(static_cast<size_t>(L.dial_slots), 0); std::vector<int32_t> site(static_cast<size_t>(L.dial_slots), -1);
        double gsum = 0, gsq = 0; int gn = 0;
        for (size_t i = 0; i < N; ++i) if (L.nodes[i].kind == 1) { site[static_cast<size_t>(L.nodes[i].slot)] = static_cast<int32_t>(i); if (known[i]) { value[static_cast<size_t>(L.nodes[i].slot)] = level[i]; valid[static_cast<size_t>(L.nodes[i].slot)] = 1; gsum += level[i]; gsq += level[i] * level[i]; ++gn; } }
        if (gn >= 24) {
            const double gmean = gsum / gn, gsd = std::sqrt(std::max(1e-9, gsq / gn - gmean * gmean));
            CircularStats stats; stats.build(value, valid);
            for (int s = 0; s < L.dial_slots; ++s) {
                const int32_t i = site[static_cast<size_t>(s)];
                if (i < 0 || !known[static_cast<size_t>(i)]) continue;
                double a, b; int n; stats.window(s, std::max(8, L.dial_slots / 10), a, b, n);
                if (n < 12) continue;
                const double mean = a / n, sd = std::sqrt(std::max(0.0, b / n - mean * mean));
                if (sd < 0.25 * gsd) { known[static_cast<size_t>(i)] = 0; ++out.unknown; continue; }
                const double weight = static_cast<double>(n) / (static_cast<double>(n) + 12.0);
                const double m = weight * mean + (1 - weight) * gmean, sdev = std::max(0.4 * gsd, weight * sd + (1 - weight) * gsd);
                level[static_cast<size_t>(i)] = gmean + (level[static_cast<size_t>(i)] - m) * gsd / sdev;
            }
        }
    }
    if (out.unknown * 2 > N) return false;
    // ---- Decisions ----
    out.printed.assign(N, 0); out.confidence.assign(N, 0.0f);
    double lattice_blur[3] = {0, 0, 0}; bool lattice_blur_valid = false;  // blur covariance (xx, xy, yy) measured on the modules
    for (int kind = 0; kind < 3; ++kind) {
        std::vector<uint32_t> sites; std::vector<double> values;
        for (size_t i = 0; i < N; ++i) if (known[i] && L.nodes[i].kind == kind) { sites.push_back(static_cast<uint32_t>(i)); values.push_back(level[i]); }
        if (sites.empty()) continue;
        const double cut = class_threshold(values);
        for (uint32_t i : sites) out.printed[i] = level[i] > cut;
        if (kind == 2 && (mode & kReadRingSequence)) {
            // Ring dots: sequence detection under the physical model.
            std::vector<double> ring_values;
            for (auto &line : rings) for (int s = 0; s < line.slots; ++s) {
                const int32_t i = line.site[static_cast<size_t>(s)];
                if (i >= 0 && known[static_cast<size_t>(i)]) { line.y[static_cast<size_t>(s)] = level[static_cast<size_t>(i)]; line.has[static_cast<size_t>(s)] = 1; ring_values.push_back(level[static_cast<size_t>(i)]); }
                line.bit[static_cast<size_t>(s)] = line.fixed[static_cast<size_t>(s)] >= 0 ? static_cast<uint8_t>(line.fixed[static_cast<size_t>(s)]) : (i >= 0 ? out.printed[static_cast<size_t>(i)] : 0);
            }
            RingModel model;
            if (ring_values.size() >= 48) {
                // Long gaps and long arcs give the first base and gain.
                std::sort(ring_values.begin(), ring_values.end());
                const double low = ring_values[ring_values.size() / 10], high = ring_values[ring_values.size() * 9 / 10];
                // Candidate blurs: the lattice measurement and two scalings of
                // it, the frame-edge measurement, and "sharp". Each is tried on
                // a few rings; the one that leaves the least unexplained
                // signal after detection is used for all rings.
                struct Blur { double xx, xy, yy; };
                std::vector<Blur> candidates;
                const double floor_variance = (0.08 * p) * (0.08 * p);
                if (lattice_blur_valid) for (const double scale : {1.0, 0.6, 1.6}) candidates.push_back({lattice_blur[0] * scale + floor_variance, lattice_blur[1] * scale, lattice_blur[2] * scale + floor_variance});
                if (S.edge_blur > 0) candidates.push_back({S.edge_blur * S.edge_blur + floor_variance, 0, S.edge_blur * S.edge_blur + floor_variance});
                for (const double sigma : {0.12 * p, 0.25 * p, 0.4 * p}) candidates.push_back({sigma * sigma, 0, sigma * sigma});
                std::vector<RingLine> probe;
                for (int k : {0, L.rings / 2, L.rings - 1}) { bool seen = false; for (const auto &q : probe) seen = seen || q.spacing == rings[static_cast<size_t>(k)].spacing; if (!seen) probe.push_back(rings[static_cast<size_t>(k)]); }
                double best_noise = std::numeric_limits<double>::infinity(); RingModel best;
                for (size_t c = 0; c <= candidates.size(); ++c) {
                    // Last pass: the best blur again, as separate dots (the "dots" ring style).
                    if (c == candidates.size() && !best.valid) break;
                    const Blur blur = c < candidates.size() ? candidates[c] : Blur{best.cov_xx, best.cov_xy, best.cov_yy};
                    const double join = c < candidates.size() ? 1.0 : 0.0;
                    RingModel m; m.cov_xx = blur.xx; m.cov_xy = blur.xy; m.cov_yy = blur.yy; m.half = L.dot; m.join = join;
                    m.base = low; m.gain = std::max(0.1, high - low); m.valid = true;
                    std::vector<RingLine> trial = probe; double total = 0;
                    for (auto &line : trial) { const double noise = detect_ring_at(line, m, p, 0.0); total += noise * noise; }
                    m.noise = std::sqrt(total / static_cast<double>(trial.size()));
                    if (std::isfinite(m.noise) && m.noise < best_noise) { best_noise = m.noise; best = m; }
                    if (work_exhausted()) break;
                }
                if (best.valid) { model = best; register_and_detect_rings(rings, model, p); }
            }
            if (model.valid) {
                for (const auto &line : rings) for (int s = 0; s < line.slots; ++s) { const int32_t i = line.site[static_cast<size_t>(s)]; if (i >= 0) out.printed[static_cast<size_t>(i)] = line.bit[static_cast<size_t>(s)]; }
                for (const auto &line : rings) ring_confidence(line, model, out.confidence);
                for (uint32_t i : sites) if (!known[i]) out.confidence[i] = 0;
                continue;
            }
            double sum_sq = 0; for (uint32_t i : sites) { const double d = level[i] - cut; sum_sq += d * d; }
            const double spread = std::sqrt(sum_sq / static_cast<double>(sites.size()));
            for (uint32_t i : sites) out.confidence[i] = static_cast<float>(std::abs(level[i] - cut) / std::max(0.25 * spread, 0.02));
            continue;
        }
        const int classes = kind == 0 ? 4 : 1, d = 2 + classes;
        double w[6] = {0, 1, 0, 0, 0, 0}; double centre = cut, sigma = 0.25; bool fitted = false;
        const auto counts = [&](uint32_t i, double c[4]) {
            c[0] = c[1] = c[2] = c[3] = 0;
            for (int k = 0; k < (kind == 0 ? 8 : 2); ++k) {
                const int32_t j = nb.of[i][static_cast<size_t>(k)];
                if (j == -2) c[kind == 0 ? k / 2 : 0] += 1;
                else if (j >= 0 && known[static_cast<size_t>(j)]) c[kind == 0 ? k / 2 : 0] += out.printed[static_cast<size_t>(j)];
            }
        };
        std::vector<uint8_t> next(N);
        for (int iteration = 0; iteration < 4 && sites.size() >= 48; ++iteration) {
            double a[6][6] = {}, b[6] = {};
            for (uint32_t i : sites) {
                double c[4]; counts(i, c);
                double f[6] = {1.0, static_cast<double>(out.printed[i]), c[0], c[1], c[2], c[3]};
                for (int r = 0; r < d; ++r) { b[r] += f[r] * level[i]; for (int q = 0; q < d; ++q) a[r][q] += f[r] * f[q]; }
            }
            for (int r = 0; r < d; ++r) a[r][r] += 1e-6;
            if (!solve_small(a, b, d) || b[1] < 0.12) break;
            // Leak terms are physical: non-negative and smaller than the mark itself.
            for (int r = 2; r < d; ++r) b[r] = std::clamp(b[r], 0.0, 0.6 * b[1]);
            std::copy(b, b + 6, w); fitted = true;
            double sum_sq = 0; size_t changed = 0;
            for (uint32_t i : sites) {
                double c[4]; counts(i, c);
                double rest = level[i] - w[0]; for (int r = 0; r < classes; ++r) rest -= w[2 + r] * c[r];
                next[i] = rest > w[1] / 2;
                const double residual = rest - (next[i] ? w[1] : 0.0); sum_sq += residual * residual;
            }
            for (uint32_t i : sites) { if (out.printed[i] != next[i]) ++changed; out.printed[i] = next[i]; }
            sigma = std::sqrt(sum_sq / static_cast<double>(sites.size()));
            if (!changed) break;
        }
        if (fitted) centre = w[1] / 2;
        if (fitted && kind == 0) {
            // The measured leaks give the blur's covariance in canonical
            // units: widths along x, y and both diagonals, then
            // var(direction u) = u' C u. The module sampling footprint
            // (about 0.1 pitch) is part of what was measured; remove it.
            const double side = 0.68 * p, footprint = (0.1 * p) * (0.1 * p);
            const double sx = blur_from_leak(w[2] / w[1], p, side), sy = blur_from_leak(w[3] / w[1], p, side);
            const double d1 = blur_from_leak(w[4] / w[1], 1.41421356 * p, side), d2 = blur_from_leak(w[5] / w[1], 1.41421356 * p, side);
            lattice_blur[0] = std::max(0.0, sx * sx - footprint); lattice_blur[2] = std::max(0.0, sy * sy - footprint);
            // Class 2 neighbours are (row - 1, col - 1) and (row + 1, col + 1): the direction (1, 1); class 3 is (1, -1).
            lattice_blur[1] = std::clamp((d1 * d1 - d2 * d2) / 2, -0.9 * std::sqrt(lattice_blur[0] * lattice_blur[2]), 0.9 * std::sqrt(lattice_blur[0] * lattice_blur[2]));
            lattice_blur_valid = true;
        }
        sigma = std::max(sigma, fitted ? 0.03 * w[1] : 0.03);
        for (uint32_t i : sites) {
            double distance;
            if (fitted) {
                double c[4]; counts(i, c);
                double rest = level[i] - w[0]; for (int r = 0; r < classes; ++r) rest -= w[2 + r] * c[r];
                distance = std::abs(rest - centre);
            } else distance = std::abs(level[i] - centre);
            out.confidence[i] = static_cast<float>(distance / sigma);
        }
    }
    return true;
}

// Assembles the codeword of `layout` (a placement of the family that `read`
// was sampled with) and each byte's reliability (0..255; its weakest bit).
void scalable_assemble(const ScalableLayout &layout, const SiteRead &read, std::vector<uint8_t> &codeword, std::vector<uint8_t> &reliability) {
    const size_t bytes = layout.codeword_bytes;
    codeword.assign(bytes, 0); reliability.assign(bytes, 255);
    for (size_t n = 0; n < bytes * 8; ++n) {
        const size_t site = layout.grouped ? layout.base_index[n] : n;
        if ((read.printed[site] != 0) != (layout.white[n] != 0)) codeword[n / 8] |= static_cast<uint8_t>(1u << (7 - n % 8));
        const uint8_t r = static_cast<uint8_t>(std::min(255.0f, 40.0f * read.confidence[site]));
        reliability[n / 8] = std::min(reliability[n / 8], r);
    }
}

// How many of the frame's fixed header bytes read correctly before any
// correction (the code is systematic, so block 0 starts with the header).
int scalable_header_matches(const ScalableLayout &layout, const std::vector<uint8_t> &codeword) {
    const size_t blocks = (layout.codeword_bytes + 254) / 255;
    if (layout.codeword_bytes < 5 * blocks + 1) return 0;
    const uint8_t expected[5] = {0xc7, 0x4b, layout.format, 0, static_cast<uint8_t>(layout.version)};
    int matches = 0; for (size_t i = 0; i < 5; ++i) matches += codeword[i * blocks] == expected[i];
    return matches;
}

}  // namespace

namespace {

// ---- Precise frame fit --------------------------------------------------------
// Locates the frame stroke's outer edge directly. Along the straight middle
// of each side, a brightness profile is taken across the expected edge and
// the edge is placed at its steepest step (sub-pixel, by a parabola through
// the gradient peak).
struct FrameEdgePoint { double x, y, strength, t = 0, d = 0; };  // image position, step strength; canonical position along the side and offset outward from the straight edge
constexpr double kFrameOuter = 0.175;  // stroke outer edge inset, canonical units

// `bow` (optional) gives, per side, where to expect the edge when the side is
// curved: outward offset c0 + c1 u + c2 u^2 in canonical units at u = t - 14.5.
bool frame_edge_points(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8], double reach,
                       std::array<std::vector<FrameEdgePoint>, 4> &found, const double (*bow)[3] = nullptr) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    double h[8]; if (!solve_homography(canonical, quad, h)) return false;
    const double unit_px = quad_mean_side(quad) / 29.0;
    if (unit_px < 1.0) return false;
    // Half-pixel steps, or coarser if the reach would need more than 64 of them.
    const double step = std::max(0.5 / unit_px, reach / 64.0);
    const int steps = std::max(3, static_cast<int>(std::ceil(reach / step)));
    // The stroke is 0.65 units wide: its inner edge is a step of the opposite
    // sign that far inside. Scoring the pair keeps a lone edge nearby (the
    // rim of a label, a screen bezel, the first row of modules) from being
    // taken for the frame.
    const int stroke = std::max(1, static_cast<int>(std::lround(0.65 / step)));
    const int inner = steps + stroke + 2, outer = steps + 2, length = inner + outer + 1;  // index `inner` is the expected edge
    constexpr int kPoints = 28;
    const auto expected = [&](int side, int i) { if (!bow) return 0.0; const double u = 5.0 + 19.0 * i / (kPoints - 1) - 14.5; return bow[side][0] + bow[side][1] * u + bow[side][2] * u * u; };
    const auto base_point = [](int side, int i, double &x, double &y, double &nx, double &ny) {
        const double t = 5.0 + 19.0 * i / (kPoints - 1);
        if (side == 0) { x = t; y = kFrameOuter; nx = 0; ny = -1; }
        else if (side == 1) { x = 29 - kFrameOuter; y = t; nx = 1; ny = 0; }
        else if (side == 2) { x = 29 - t; y = 29 - kFrameOuter; nx = 0; ny = 1; }
        else { x = kFrameOuter; y = 29 - t; nx = -1; ny = 0; }
    };
    std::array<std::vector<std::vector<double>>, 4> profiles; double signed_sum = 0;
    for (int side = 0; side < 4; ++side) for (int i = 0; i < kPoints; ++i) {
        double x, y, nx, ny; base_point(side, i, x, y, nx, ny);
        const double e = expected(side, i); x += nx * e; y += ny * e;
        std::vector<double> profile(static_cast<size_t>(length));
        bool ok = true;
        for (int k = 0; k < length && ok; ++k) {
            profile[static_cast<size_t>(k)] = gray_sample(gray, width, height, stride, h, x + nx * (k - inner) * step, y + ny * (k - inner) * step, 0.0);
            ok = profile[static_cast<size_t>(k)] >= 0;
        }
        if (!ok) profile.clear();
        else signed_sum += profile[static_cast<size_t>(std::min(length - 1, inner + stroke / 2 + 1))] - profile[static_cast<size_t>(inner - stroke / 2)];  // just outside minus mid-stroke
        profiles[static_cast<size_t>(side)].push_back(std::move(profile));
    }
    const double polarity = signed_sum >= 0 ? 1.0 : -1.0;  // +1: dark stroke on a light surround
    for (int side = 0; side < 4; ++side) {
        found[static_cast<size_t>(side)].clear();
        for (int i = 0; i < kPoints; ++i) {
            const auto &profile = profiles[static_cast<size_t>(side)][static_cast<size_t>(i)];
            if (profile.empty()) continue;
            const auto grad = [&](int k) { return polarity * (profile[static_cast<size_t>(k + 1)] - profile[static_cast<size_t>(k - 1)]); };
            int best = -1; double best_score = 0;
            for (int k = inner - steps; k <= inner + steps; ++k) {
                const double g = grad(k), paired = -grad(k - stroke);
                if (g <= 0 || paired <= 0) continue;
                const double score = g + std::min(g, paired);
                if (score > best_score) { best_score = score; best = k; }
            }
            if (best < 0 || grad(best) < 4.0) continue;
            const double best_g = grad(best);
            const double a = grad(best - 1), b = grad(best), c = grad(best + 1), denom = a - 2 * b + c;
            const double sub = std::abs(denom) > 1e-9 ? std::clamp(0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
            double x, y, nx, ny; base_point(side, i, x, y, nx, ny);
            const double d = (best + sub - inner) * step + expected(side, i);
            ImagePoint p{}; if (!map_point(h, x + nx * d, y + ny * d, p)) continue;
            found[static_cast<size_t>(side)].push_back({p.x, p.y, best_g, 5.0 + 19.0 * i / (kPoints - 1), d});
        }
        // Weak steps are usually not the frame: keep those at least 40% of the median strength.
        auto &points = found[static_cast<size_t>(side)];
        if (points.size() < 10) return false;
        std::vector<double> strengths; for (const auto &e : points) strengths.push_back(e.strength);
        std::nth_element(strengths.begin(), strengths.begin() + static_cast<std::ptrdiff_t>(strengths.size() / 2), strengths.end());
        const double median = strengths[strengths.size() / 2];
        points.erase(std::remove_if(points.begin(), points.end(), [&](const FrameEdgePoint &e) { return e.strength < 0.4 * median; }), points.end());
        if (points.size() < 8) return false;
    }
    return true;
}

struct EdgeLine { double nx, ny, d; };

// Least-squares line through one side's points after undistorting them with
// `lens`, dropping outliers; returns the sum of squared residuals of the
// points kept (or a negative value if too few remain).
double fit_edge_line(const std::vector<FrameEdgePoint> &side, const LensModel &lens, EdgeLine &line, size_t *kept_count = nullptr) {
    std::vector<Point2> points; points.reserve(side.size());
    for (const auto &e : side) { double x = e.x, y = e.y; lens_undistort(lens, x, y); points.push_back({x, y}); }
    double error = 0;
    for (int pass = 0; pass < 3; ++pass) {
        if (points.size() < 8) return -1;
        double mx = 0, my = 0; for (const auto &e : points) { mx += e.x; my += e.y; } mx /= points.size(); my /= points.size();
        double sxx = 0, syy = 0, sxy = 0;
        for (const auto &e : points) { sxx += (e.x - mx) * (e.x - mx); syy += (e.y - my) * (e.y - my); sxy += (e.x - mx) * (e.y - my); }
        const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);
        line = {-std::sin(theta), std::cos(theta), 0}; line.d = mx * line.nx + my * line.ny;
        std::vector<double> residual; error = 0;
        for (const auto &e : points) { residual.push_back(std::abs(e.x * line.nx + e.y * line.ny - line.d)); error += residual.back() * residual.back(); }
        std::vector<double> sorted = residual; std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
        const double limit = std::max(0.6, 2.5 * sorted[sorted.size() / 2]);
        std::vector<Point2> kept; for (size_t k = 0; k < points.size(); ++k) if (residual[k] <= limit) kept.push_back(points[k]);
        if (kept.size() == points.size()) break;
        points.swap(kept);
    }
    if (kept_count) *kept_count = points.size();
    return error / static_cast<double>(points.size());
}

// Radial distortion from the frame itself: the k1 that makes all four sides
// straightest. Returns a lens with k1 = 0 unless bending the sides straight
// clearly beats leaving them (so noise is not mistaken for a lens).
LensModel estimate_lens(const std::array<std::vector<FrameEdgePoint>, 4> &sides, size_t width, size_t height) {
    LensModel lens{0, 0.5 * static_cast<double>(width), 0.5 * static_cast<double>(height), static_cast<double>(std::max(width, height))};
    const auto cost = [&](double k1) {
        LensModel trial = lens; trial.k1 = k1; double total = 0;
        for (const auto &side : sides) { EdgeLine line{}; const double e = fit_edge_line(side, trial, line); if (e < 0) return 1e300; total += e; }
        return total;
    };
    const double flat = cost(0.0);
    if (flat >= 1e300) return lens;
    double best_k = 0, best = flat;
    for (double k = -0.6; k <= 0.4001; k += 0.02) { const double c = cost(k); if (c < best) { best = c; best_k = k; } }
    for (double span = 0.01; span > 0.0004; span /= 2) for (const double k : {best_k - span, best_k + span}) { const double c = cost(k); if (c < best) { best = c; best_k = k; } }
    // Mean squared residual per point, in pixels squared.
    if (best_k != 0 && flat - best > 0.02 && best < 0.6 * flat) lens.k1 = best_k;
    return lens;
}

// Fits the four frame sides as straight lines (in undistorted space under the
// active lens) and rewrites `quad` as the field corners they imply. A
// straight line stays straight under perspective, so this is exact at any
// tilt and does not depend on a hill climb converging. `reach` is the search
// distance either side of the expected edge, in canonical units. With
// `fit_lens`, the radial distortion is estimated first and installed as the
// active lens for the caller's scope.
bool fit_frame_edges(const uint8_t *gray, size_t width, size_t height, size_t stride, double quad[8], double reach, bool fit_lens = false) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    std::array<std::vector<FrameEdgePoint>, 4> sides;
    if (!frame_edge_points(gray, width, height, stride, quad, reach, sides)) return false;
    if (fit_lens) active_lens() = estimate_lens(sides, width, height);
    const LensModel lens = active_lens();
    std::array<EdgeLine, 4> lines{};
    for (int side = 0; side < 4; ++side) if (fit_edge_line(sides[static_cast<size_t>(side)], lens, lines[static_cast<size_t>(side)]) < 0) return false;
    double outer[8];
    for (int c = 0; c < 4; ++c) {  // corner c joins side c - 1 and side c
        const EdgeLine &a = lines[static_cast<size_t>((c + 3) % 4)], &b = lines[static_cast<size_t>(c)];
        const double det = a.nx * b.ny - a.ny * b.nx;
        if (std::abs(det) < 1e-9) return false;
        outer[2 * c] = (a.d * b.ny - a.ny * b.d) / det; outer[2 * c + 1] = (a.nx * b.d - a.d * b.nx) / det;
    }
    // From the stroke's outer-edge square to the full field (undistorted space), then through the lens.
    const double src[8] = {kFrameOuter, kFrameOuter, 29 - kFrameOuter, kFrameOuter, 29 - kFrameOuter, 29 - kFrameOuter, kFrameOuter, 29 - kFrameOuter};
    double g[8]; if (!solve_homography_raw(src, outer, g)) return false;
    double out[8];
    for (int c = 0; c < 4; ++c) {
        ImagePoint p{}; if (!map_point_raw(g, canonical[2 * c], canonical[2 * c + 1], p)) return false;
        lens_distort(lens, p.x, p.y); out[2 * c] = p.x; out[2 * c + 1] = p.y;
    }
    const double unit_px = quad_mean_side(quad) / 29.0;
    if (!valid_quad(out) || quad_corner_distance(out, quad) > 1.5 * reach * unit_px + 2.0) return false;
    std::copy(out, out + 8, quad);
    return true;
}

// Field corners of a symbol whose frame sides are curved (a bent surface).
// Each side's edge points are fitted with a parabola instead of a line, and
// the corners are where neighbouring parabolas cross. On a flat symbol this
// agrees with the line fit but is noisier, so it is used only by the bent-
// surface pass. `quad` is the starting estimate and receives the result.
// `bow_out` receives how far the most curved side departs from its chord
// over the fitted span, in canonical units.
bool fit_frame_curves(const uint8_t *gray, size_t width, size_t height, size_t stride, double quad[8], double reach,
                      std::array<std::vector<FrameEdgePoint>, 4> *edge_points = nullptr, double *bow_out = nullptr) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    std::array<std::vector<FrameEdgePoint>, 4> sides;
    if (!frame_edge_points(gray, width, height, stride, quad, reach, sides)) return false;
    // A wide search also catches rows of modules that look like a stroke. On
    // each side take the parabola (through three of the points) that the
    // most points agree with, then search again close to it.
    double bow[4][3];
    for (int side = 0; side < 4; ++side) {
        const auto &points = sides[static_cast<size_t>(side)]; const size_t n = points.size(), third = n / 3;
        size_t best_count = 0; double best[3] = {0, 0, 0};
        for (size_t i = 0; i < third; ++i) for (size_t j = third; j < 2 * third; ++j) for (size_t k = 2 * third; k < n; ++k) {
            const double u1 = points[i].t - 14.5, u2 = points[j].t - 14.5, u3 = points[k].t - 14.5, d1 = points[i].d, d2 = points[j].d, d3 = points[k].d;
            const double c2 = ((d3 - d1) / (u3 - u1) - (d2 - d1) / (u2 - u1)) / (u3 - u2), c1 = (d2 - d1) / (u2 - u1) - c2 * (u1 + u2), c0 = d1 - c1 * u1 - c2 * u1 * u1;
            size_t count = 0; for (const auto &e : points) { const double u = e.t - 14.5; count += std::abs(e.d - (c0 + c1 * u + c2 * u * u)) < 0.12; }
            if (count > best_count) { best_count = count; best[0] = c0; best[1] = c1; best[2] = c2; }
        }
        if (best_count < 12) return false;
        std::copy(best, best + 3, bow[side]);
    }
    if (!frame_edge_points(gray, width, height, stride, quad, 0.3, sides, bow)) return false;
    if (bow_out) { *bow_out = 0; for (int side = 0; side < 4; ++side) *bow_out = std::max(*bow_out, std::abs(bow[side][2]) * 9.5 * 9.5); }
    if (edge_points) *edge_points = sides;
    // A side as: mean point m, unit tangent (tx, ty), unit normal (nx, ny), and
    // offset along the normal = c0 + c1 t + c2 t^2 at distance t along the tangent.
    struct Curve { double mx, my, tx, ty, nx, ny, c[3]; };
    std::array<Curve, 4> curves{};
    for (int side = 0; side < 4; ++side) {
        const auto &points = sides[static_cast<size_t>(side)];
        Curve &k = curves[static_cast<size_t>(side)];
        double sxx = 0, syy = 0, sxy = 0; k.mx = k.my = 0;
        for (const auto &e : points) { k.mx += e.x; k.my += e.y; } k.mx /= points.size(); k.my /= points.size();
        for (const auto &e : points) { sxx += (e.x - k.mx) * (e.x - k.mx); syy += (e.y - k.my) * (e.y - k.my); sxy += (e.x - k.mx) * (e.y - k.my); }
        const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);
        k.tx = std::cos(theta); k.ty = std::sin(theta); k.nx = -k.ty; k.ny = k.tx;
        std::vector<std::pair<double, double>> tr;
        for (const auto &e : points) tr.push_back({(e.x - k.mx) * k.tx + (e.y - k.my) * k.ty, (e.x - k.mx) * k.nx + (e.y - k.my) * k.ny});
        for (int pass = 0; pass < 3; ++pass) {
            if (tr.size() < 8) return false;
            // Normal equations for a quadratic, with t scaled to keep them well conditioned.
            double scale = 1e-9; for (const auto &[t, r] : tr) scale = std::max(scale, std::abs(t));
            double m[3][4] = {};
            for (const auto &[t, r] : tr) { const double u = t / scale, b[3] = {1, u, u * u}; for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) m[i][j] += b[i] * b[j]; m[i][3] += b[i] * r; } }
            for (int col = 0; col < 3; ++col) {
                int pivot = col; for (int row = col + 1; row < 3; ++row) if (std::abs(m[row][col]) > std::abs(m[pivot][col])) pivot = row;
                if (std::abs(m[pivot][col]) < 1e-12) return false;
                if (pivot != col) for (int j = 0; j < 4; ++j) std::swap(m[pivot][j], m[col][j]);
                for (int row = 0; row < 3; ++row) if (row != col) { const double f = m[row][col] / m[col][col]; for (int j = col; j < 4; ++j) m[row][j] -= f * m[col][j]; }
            }
            k.c[0] = m[0][3] / m[0][0]; k.c[1] = m[1][3] / m[1][1] / scale; k.c[2] = m[2][3] / m[2][2] / (scale * scale);
            std::vector<double> residual; for (const auto &[t, r] : tr) residual.push_back(std::abs(r - (k.c[0] + k.c[1] * t + k.c[2] * t * t)));
            std::vector<double> sorted = residual; std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
            const double limit = std::max(0.6, 2.5 * sorted[sorted.size() / 2]);
            std::vector<std::pair<double, double>> kept; for (size_t i = 0; i < tr.size(); ++i) if (residual[i] <= limit) kept.push_back(tr[i]);
            if (kept.size() == tr.size()) break;
            tr.swap(kept);
        }
    }
    const auto miss = [](const Curve &k, double x, double y) {
        const double t = (x - k.mx) * k.tx + (y - k.my) * k.ty;
        return (x - k.mx) * k.nx + (y - k.my) * k.ny - (k.c[0] + k.c[1] * t + k.c[2] * t * t);
    };
    double outer[8];
    for (int c = 0; c < 4; ++c) {  // corner c joins side c - 1 and side c
        const Curve &a = curves[static_cast<size_t>((c + 3) % 4)], &b = curves[static_cast<size_t>(c)];
        // Start where the two chords cross, then Newton's method on the curves.
        const double det0 = a.nx * b.ny - a.ny * b.nx; if (std::abs(det0) < 1e-9) return false;
        const double da = a.mx * a.nx + a.my * a.ny, db = b.mx * b.nx + b.my * b.ny;
        double x = (da * b.ny - a.ny * db) / det0, y = (a.nx * db - da * b.nx) / det0;
        for (int iteration = 0; iteration < 12; ++iteration) {
            const double fa = miss(a, x, y), fb = miss(b, x, y), e = 0.25;
            const double ax = (miss(a, x + e, y) - fa) / e, ay = (miss(a, x, y + e) - fa) / e, bx = (miss(b, x + e, y) - fb) / e, by = (miss(b, x, y + e) - fb) / e;
            const double det = ax * by - ay * bx; if (std::abs(det) < 1e-9) return false;
            x -= (fa * by - ay * fb) / det; y -= (ax * fb - fa * bx) / det;
        }
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        outer[2 * c] = x; outer[2 * c + 1] = y;
    }
    // From the stroke's outer-edge square to the full field: a short step, for which a flat map is close enough.
    const double src[8] = {kFrameOuter, kFrameOuter, 29 - kFrameOuter, kFrameOuter, 29 - kFrameOuter, 29 - kFrameOuter, kFrameOuter, 29 - kFrameOuter};
    double g[8]; if (!solve_homography_raw(src, outer, g)) return false;
    double out[8];
    for (int c = 0; c < 4; ++c) { ImagePoint p{}; if (!map_point_raw(g, canonical[2 * c], canonical[2 * c + 1], p)) return false; out[2 * c] = p.x; out[2 * c + 1] = p.y; }
    const double unit_px = quad_mean_side(quad) / 29.0;
    if (!valid_quad(out) || quad_corner_distance(out, quad) > 2.5 * reach * unit_px + 2.0) return false;
    std::copy(out, out + 8, quad);
    return true;
}

// Lens from the rings. A corner-anchored perspective fit is exact at the
// corners, but radial lens distortion moves the middle of the symbol
// relative to them: by 2 to 3 pixels on a large symbol for a k1 of only
// -0.02, which bows the frame's sides by a fraction of a pixel (too little
// to measure there) yet is half a slot on the rings. The rings themselves
// show it: samples taken on dot centres are either ink or background, and
// off-centre they mix, so the right k1 is the one that makes the ring
// samples spread widest. `quad` holds the field corners in the image, in
// the attempt's orientation. Returns the lens (k1 = 0 if nothing beats it).
LensModel lens_from_rings(const ScalableLayout &L, const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8]) {
    LensModel lens{0, 0.5 * static_cast<double>(width), 0.5 * static_cast<double>(height), static_cast<double>(std::max(width, height))};
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    // A sample of each kind of site. Rings are the sensitive ones; modules
    // and rays keep a wrong lens from winning by landing ring samples on
    // some other, higher-contrast mark.
    std::array<std::vector<uint32_t>, 3> sites; std::array<size_t, 3> count{};
    for (const auto &n : L.nodes) ++count[n.kind];
    std::array<size_t, 3> seen{};
    for (size_t i = 0; i < L.nodes.size(); ++i) { const int k = L.nodes[i].kind; if (seen[static_cast<size_t>(k)]++ % std::max<size_t>(1, count[static_cast<size_t>(k)] / 600) == 0) sites[static_cast<size_t>(k)].push_back(static_cast<uint32_t>(i)); }
    if (sites[2].size() < 64 || sites[0].size() < 32) return lens;
    const auto spread = [&](double k1, std::array<double, 3> &variance) {
        LensModel trial = lens; trial.k1 = k1; const LensScope scope(trial);
        double h[8]; if (!solve_homography(canonical, quad, h)) return false;
        for (int kind = 0; kind < 3; ++kind) {
            double sum = 0, sq = 0; int n = 0;
            for (uint32_t i : sites[static_cast<size_t>(kind)]) {
                const auto &node = L.nodes[i];
                const double v = kind == 1 ? polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + L.dial_sample, node.slot, L.dial_slots, 0.0)
                                           : gray_sample(gray, width, height, stride, h, node.x, node.y, 0.0);
                if (v < 0) continue;
                sum += v; sq += v * v; ++n;
            }
            if (n < static_cast<int>(sites[static_cast<size_t>(kind)].size()) * 3 / 4) { if (kind != 1) return false; variance[static_cast<size_t>(kind)] = 0; continue; }
            const double mean = sum / n; variance[static_cast<size_t>(kind)] = sq / n - mean * mean;
        }
        return true;
    };
    std::array<double, 3> flat{}; if (!spread(0.0, flat) || flat[0] <= 0 || flat[2] <= 0) return lens;
    const auto gain = [&](double k1) {
        std::array<double, 3> v{}; if (!spread(k1, v)) return 0.0;
        // Each kind relative to no lens; the modules must not get worse.
        if (v[0] < 0.97 * flat[0]) return 0.0;
        return v[2] / flat[2] + v[0] / flat[0] + (flat[1] > 0 ? v[1] / flat[1] : 1.0);
    };
    double best = gain(0.0), best_k = 0; const double none = best;
    for (double k = -0.20; k <= 0.0801; k += 0.01) { const double v = gain(k); if (v > best) { best = v; best_k = k; } }
    for (double span = 0.005; span > 0.0006; span /= 2) for (const double k : {best_k - span, best_k + span}) { const double v = gain(k); if (v > best) { best = v; best_k = k; } }
    // An optimum at the end of the range is not a lens.
    if (best > 1.01 * none && best_k > -0.195 && best_k < 0.075) lens.k1 = best_k;
    return lens;
}

// Camera position for a surface hypothesis. Given the field corners in the
// image, a cylinder (axis and curvature set in `m`) and a focal length, finds
// the vector g that completes the surface model (see SurfaceModel). The four
// corners' positions on the bent surface are known in three dimensions, so
// the camera's rotation R and position t follow from where they appear
// (least squares on the reprojection, started from the flat symbol's pose).
// The plane's perspective map is then H = K [r1 r2 t] and g = H^-1 K r3.
bool surface_pose(SurfaceModel &m, const double quad[8], size_t width, size_t height, double focal) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29}; constexpr double c = 14.5;
    const double cx = 0.5 * static_cast<double>(width), cy = 0.5 * static_cast<double>(height);
    // Corners on the surface, relative to the field centre.
    double P[4][3];
    for (int i = 0; i < 4; ++i) {
        const double u = canonical[2 * i] - c, v = canonical[2 * i + 1] - c, across = -u * m.sa + v * m.ca, along = u * m.ca + v * m.sa;
        const double bent = m.kappa != 0 ? std::sin(m.kappa * across) / m.kappa : across, out = m.kappa != 0 ? (1 - std::cos(m.kappa * across)) / m.kappa : 0.0;
        P[i][0] = along * m.ca - bent * m.sa; P[i][1] = along * m.sa + bent * m.ca; P[i][2] = out;
    }
    // Start: the pose of a flat symbol with these corners.
    double R[3][3], t[3];
    {
        double h[8]; if (!solve_homography_raw(canonical, quad, h)) return false;
        const double H[3][3] = {{h[0], h[1], h[2]}, {h[3], h[4], h[5]}, {h[6], h[7], 1.0}};
        double M[3][3];
        for (int k = 0; k < 3; ++k) { M[0][k] = (H[0][k] - cx * H[2][k]) / focal; M[1][k] = (H[1][k] - cy * H[2][k]) / focal; M[2][k] = H[2][k]; }
        const double n1 = std::sqrt(M[0][0] * M[0][0] + M[1][0] * M[1][0] + M[2][0] * M[2][0]), n2 = std::sqrt(M[0][1] * M[0][1] + M[1][1] * M[1][1] + M[2][1] * M[2][1]);
        if (n1 < 1e-12 || n2 < 1e-12) return false;
        const double scale = (n1 + n2) / 2;
        double r1[3] = {M[0][0] / n1, M[1][0] / n1, M[2][0] / n1}, r2[3] = {M[0][1] / n2, M[1][1] / n2, M[2][1] / n2};
        const double dot = r1[0] * r2[0] + r1[1] * r2[1] + r1[2] * r2[2];
        for (int k = 0; k < 3; ++k) r2[k] -= dot * r1[k];
        const double n = std::sqrt(r2[0] * r2[0] + r2[1] * r2[1] + r2[2] * r2[2]); if (n < 1e-9) return false;
        for (int k = 0; k < 3; ++k) r2[k] /= n;
        const double r3[3] = {r1[1] * r2[2] - r1[2] * r2[1], r1[2] * r2[0] - r1[0] * r2[2], r1[0] * r2[1] - r1[1] * r2[0]};
        for (int k = 0; k < 3; ++k) { R[k][0] = r1[k]; R[k][1] = r2[k]; R[k][2] = r3[k]; }
        // H maps canonical (x, y); the centre is at t.
        for (int k = 0; k < 3; ++k) t[k] = (M[k][0] * c + M[k][1] * c + M[k][2]) / scale;
        if (t[2] < 0) { for (int k = 0; k < 3; ++k) { t[k] = -t[k]; R[k][0] = -R[k][0]; R[k][1] = -R[k][1]; } }
    }
    const auto residuals = [&](const double Rm[3][3], const double tv[3], double r[8]) {
        for (int i = 0; i < 4; ++i) {
            double pc[3]; for (int k = 0; k < 3; ++k) pc[k] = Rm[k][0] * P[i][0] + Rm[k][1] * P[i][1] + Rm[k][2] * P[i][2] + tv[k];
            if (pc[2] < 1e-6) return false;
            r[2 * i] = cx + focal * pc[0] / pc[2] - quad[2 * i]; r[2 * i + 1] = cy + focal * pc[1] / pc[2] - quad[2 * i + 1];
        }
        return true;
    };
    // A small rotation (wx, wy, wz) applied on the left of R, re-orthonormalised.
    const auto rotated = [](const double Rm[3][3], const double w[3], double out[3][3]) {
        const double W[3][3] = {{1, -w[2], w[1]}, {w[2], 1, -w[0]}, {-w[1], w[0], 1}};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) out[i][j] = W[i][0] * Rm[0][j] + W[i][1] * Rm[1][j] + W[i][2] * Rm[2][j];
        double a[3] = {out[0][0], out[1][0], out[2][0]}, b[3] = {out[0][1], out[1][1], out[2][1]};
        const double na = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); for (double &x : a) x /= na;
        const double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; for (int k = 0; k < 3; ++k) b[k] -= d * a[k];
        const double nb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]); for (double &x : b) x /= nb;
        const double n[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        for (int k = 0; k < 3; ++k) { out[k][0] = a[k]; out[k][1] = b[k]; out[k][2] = n[k]; }
    };
    double r[8]; if (!residuals(R, t, r)) return false;
    double error = 0; for (double x : r) error += x * x;
    double lambda = 1e-3;
    // A pose costs about as much as a few hundred image samples; charge it, so a search over surfaces stays within the caller's work budget.
    { WorkBudget &budget = work_budget(); budget.left -= std::min<size_t>(budget.left, 300); }
    for (int iteration = 0; iteration < 12 && error > 1e-6; ++iteration) {
        // Numeric Jacobian over (rotation, translation).
        double J[8][6];
        for (int k = 0; k < 6; ++k) {
            double Rk[3][3], tk[3] = {t[0], t[1], t[2]}, w[3] = {0, 0, 0}, rk[8];
            const double e = k < 3 ? 1e-6 : 1e-5 * std::max(1.0, std::abs(t[2]));
            if (k < 3) { w[k] = e; rotated(R, w, Rk); } else { std::copy(&R[0][0], &R[0][0] + 9, &Rk[0][0]); tk[k - 3] += e; }
            if (!residuals(Rk, tk, rk)) return false;
            for (int i = 0; i < 8; ++i) J[i][k] = (rk[i] - r[i]) / e;
        }
        double A[6][7] = {};
        for (int a = 0; a < 6; ++a) { for (int b = 0; b < 6; ++b) for (int i = 0; i < 8; ++i) A[a][b] += J[i][a] * J[i][b]; for (int i = 0; i < 8; ++i) A[a][6] -= J[i][a] * r[i]; }
        for (int a = 0; a < 6; ++a) A[a][a] *= 1 + lambda;
        bool singular = false;
        for (int col = 0; col < 6 && !singular; ++col) {
            int pivot = col; for (int row = col + 1; row < 6; ++row) if (std::abs(A[row][col]) > std::abs(A[pivot][col])) pivot = row;
            if (std::abs(A[pivot][col]) < 1e-18) { singular = true; break; }
            if (pivot != col) for (int k = 0; k < 7; ++k) std::swap(A[pivot][k], A[col][k]);
            for (int row = 0; row < 6; ++row) if (row != col) { const double f = A[row][col] / A[col][col]; for (int k = col; k < 7; ++k) A[row][k] -= f * A[col][k]; }
        }
        if (singular) { lambda *= 10; if (lambda > 1e6) break; continue; }
        double step[6]; for (int a = 0; a < 6; ++a) step[a] = A[a][6] / A[a][a];
        double Rn[3][3], tn[3] = {t[0] + step[3], t[1] + step[4], t[2] + step[5]}, rn[8];
        rotated(R, step, Rn);
        double next = 1e300; if (residuals(Rn, tn, rn)) { next = 0; for (double x : rn) next += x * x; }
        if (next < error) {
            const bool settled = error - next < 1e-4 * error;  // with a wrong focal length the corners cannot all be met: stop when it stops improving
            std::copy(&Rn[0][0], &Rn[0][0] + 9, &R[0][0]); std::copy(tn, tn + 3, t); std::copy(rn, rn + 8, r); error = next; lambda = std::max(1e-9, lambda / 3);
            if (settled) break;
        }
        else { lambda *= 10; if (lambda > 1e6) break; }
    }
    // g solves [r1 r2 t'] g = r3, with t' the image of the canonical origin:
    // r3 is perpendicular to r1 and r2, so g2 = 1 / (t' . r3) and the rest follow.
    double corner[3]; for (int k = 0; k < 3; ++k) corner[k] = t[k] - c * R[k][0] - c * R[k][1];
    double along1 = 0, along2 = 0, along3 = 0;
    for (int k = 0; k < 3; ++k) { along1 += corner[k] * R[k][0]; along2 += corner[k] * R[k][1]; along3 += corner[k] * R[k][2]; }
    if (std::abs(along3) < 1e-9) return false;
    m.g[2] = 1.0 / along3; m.g[0] = -m.g[2] * along1; m.g[1] = -m.g[2] * along2; m.focal = focal;
    return std::isfinite(m.g[0]) && std::isfinite(m.g[1]) && std::isfinite(m.g[2]);
}

// Surface from the marks: which cylinder (axis direction, signed curvature),
// seen by which camera (focal length), makes the module, ray and ring
// samples spread widest. A grid search, so it is not trapped by the lattice
// looking sharp again one whole module out, followed by refinement. Returns
// a flat surface unless bending clearly helps. `quad` holds the field
// corners in the image in the attempt's orientation. With `seed`, only the
// neighbourhood of that surface is searched (after the corners have moved).
SurfaceModel surface_from_marks(const ScalableLayout &L, const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8],
                                const SurfaceModel *seed = nullptr) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    // Module centres, and the points where four modules meet. A centre is ink
    // or background; a meeting point is always background (modules are 0.68
    // pitch wide). Under the right surface the centres spread wide and the
    // meeting points all read alike. Under a wrong one both land anywhere and
    // spread alike. That holds on a sharp image too, where "the centres
    // spread wide" alone is true of any hypothesis.
    const int G = L.grid; const double p = L.pitch;
    std::vector<uint8_t> is_site(static_cast<size_t>(G) * G, 0);
    for (const auto &n : L.nodes) if (n.kind == 0) is_site[static_cast<size_t>((n.id >> 12) & 0xfffu) * G + (n.id & 0xfffu)] = 1;
    std::vector<std::pair<double, double>> centres, gaps;
    {
        std::vector<std::pair<int, int>> cells;
        for (int r = 0; r + 1 < G; ++r) for (int c = 0; c + 1 < G; ++c)
            if (is_site[static_cast<size_t>(r) * G + c] && is_site[static_cast<size_t>(r) * G + c + 1] && is_site[static_cast<size_t>(r + 1) * G + c] && is_site[static_cast<size_t>(r + 1) * G + c + 1]) cells.push_back({r, c});
        const size_t skip = std::max<size_t>(1, cells.size() / 600);
        for (size_t i = 0; i < cells.size(); i += skip) {
            const auto [r, c] = cells[i];
            centres.push_back({(c + .5) * p, (r + .5) * p}); gaps.push_back({(c + 1) * p, (r + 1) * p});
        }
    }
    if (centres.size() < 48) return {};
    // `stride_points` > 1 scores a subset of the points (the search grid).
    const auto score = [&](const SurfaceModel &m, size_t stride_points) {
        const SurfaceScope scope(m);
        double h[8]; if (!solve_homography(canonical, quad, h)) return 0.0;
        double variance[2] = {0, 0};
        for (int which = 0; which < 2; ++which) {
            const auto &points = which ? gaps : centres;
            double sum = 0, sq = 0; int n = 0, tried = 0;
            for (size_t i = 0; i < points.size(); i += stride_points) { ++tried; const double v = gray_sample(gray, width, height, stride, h, points[i].first, points[i].second, stride_points > 1 ? 0.0 : 0.05 * p); if (v < 0) continue; sum += v; sq += v * v; ++n; }
            if (n < tried * 2 / 3) return 0.0;
            const double mean = sum / n; variance[which] = sq / n - mean * mean;
        }
        return variance[0] / (variance[1] + 0.04 * variance[0] + 1.0);
    };
    const double flat = score(SurfaceModel{}, 1);
    // A flat symbol read through the right map scores about 20; nothing to find.
    if (flat > 8.0) return {};
    const double longer = static_cast<double>(std::max(width, height));
    const double focal0 = 0.63 * longer;  // a phone's main camera, about a 65 degree field of view
    const auto trial = [&](double angle, double kappa, double f, size_t stride_points, SurfaceModel &m) {
        m = SurfaceModel{}; m.ca = std::cos(angle); m.sa = std::sin(angle); m.kappa = kappa;
        return surface_pose(m, quad, width, height, f) ? score(m, stride_points) : 0.0;
    };
    // The right surface has to put every sample within about a quarter of a
    // module, and a bend of half-angle s moves the middle of the symbol by
    // up to about s * G / 4 modules, so the grid's relative step in both
    // curvature and axis direction is 1 / (G s), kept between 8% and 30%:
    // fine for dense symbols and strong bends, coarse otherwise. It is scored
    // on about 150 of the points; the best few are then refined on all.
    struct Start { double score, angle, kappa; };
    std::vector<Start> starts;
    const size_t coarse = std::max<size_t>(1, centres.size() / 150);
    bool certain = false;
    if (seed && seed->kappa != 0) { starts.push_back({1e9, std::atan2(seed->sa, seed->ca), seed->kappa}); certain = true; }
    for (double half_span = 0.05; half_span < 0.7 && !certain; ) {
        const double relative = std::clamp(1.0 / (G * half_span), 0.08, 0.30);
        const int angles = std::clamp(static_cast<int>(std::ceil(0.5 * kTau / relative)), 8, 32);
        for (int a = 0; a < angles && !certain; ++a) for (const double sign : {1.0, -1.0}) {
            SurfaceModel m; const double angle = kTau * a / (2.0 * angles), g = trial(angle, sign * half_span / 14.5, focal0, coarse, m);
            if (g > 0) starts.push_back({g, angle, m.kappa});
            if (work_exhausted()) return {};
        }
        half_span *= 1.0 + relative;
    }
    std::sort(starts.begin(), starts.end(), [](const Start &a, const Start &b) { return a.score > b.score; });
    SurfaceModel best{}; double best_score = flat;
    size_t refined = 0;
    for (const Start &start : starts) {
        // A start that the grid scored like a wrong surface is not worth refining.
        if (refined >= 4 || best_score > 16.0 || start.score < 3.0) break;
        // Skip a start next to one already refined.
        bool near = false;
        for (size_t k = 0; k < static_cast<size_t>(&start - starts.data()) && !near; ++k) near = std::abs(starts[k].angle - start.angle) < kTau / 40.0 && std::abs(starts[k].kappa - start.kappa) < 0.1 * std::abs(start.kappa);
        if (near) continue;
        ++refined;
        SurfaceModel local; double angle = start.angle, focal = seed && seed->focal > 0 ? seed->focal : focal0, local_score = trial(angle, start.kappa, focal, 1, local);
        double step_k = 0.04 * std::abs(start.kappa), step_a = kTau / 128.0;
        for (int round = 0; round < 4; ++round) {
            for (int pass = 0; pass < 3; ++pass) {
                bool improved = false;
                for (int move = 0; move < 6; ++move) {
                    double a2 = angle, kappa = local.kappa, f = focal;
                    if (move == 0) kappa += step_k; else if (move == 1) kappa -= step_k;
                    else if (move == 2) a2 += step_a; else if (move == 3) a2 -= step_a;
                    else if (move == 4) f *= 1.0 + 0.15 / (round + 1); else f /= 1.0 + 0.15 / (round + 1);
                    if (std::abs(kappa) < 0.02 / 14.5 || std::abs(kappa) > 1.0 / 14.5 || f < 0.4 * longer || f > 2.5 * longer) continue;
                    SurfaceModel m; const double g = trial(a2, kappa, f, 1, m);
                    if (g > local_score) { local_score = g; local = m; angle = a2; focal = f; improved = true; }
                }
                if (!improved || work_exhausted()) break;
            }
            step_k /= 2; step_a /= 2;
        }
        if (local_score > best_score) { best_score = local_score; best = local; }
    }
    // Wrong surfaces score 1 to 5 and the right one about 20, so the test is
    // not delicate.
    return best_score > 7.0 && best_score > 3.0 * flat ? best : SurfaceModel{};
}

// The same surface seen from another of the eight orientations of the field
// corners: `quad` holds the corners in that orientation, and (dx, dy) is where
// the first orientation's x axis points in it (a unit vector along a side of
// the square, with (ex, ey) the same for its y axis).
bool surface_reoriented(const SurfaceModel &m, const double quad[8], size_t width, size_t height, double dx, double dy, double ex, double ey, SurfaceModel &out) {
    out = SurfaceModel{};
    if (m.kappa == 0) return true;
    out.kappa = m.kappa; out.ca = m.ca * dx + m.sa * ex; out.sa = m.ca * dy + m.sa * ey;
    return surface_pose(out, quad, width, height, m.focal);
}

// Field corners from the frame as seen through a bent surface. `edges` holds
// measured points on the frame stroke's outer edge (fit_frame_curves); the
// corners are moved until the edge the surface model predicts runs through
// them. The model's curve is the true shape of a bent side, where the
// parabolas of fit_frame_curves are an approximation that has to be
// extrapolated to the corners. Only the surface's curvature, axis and focal
// length are used; the camera is placed again for every trial.
void corners_from_bent_frame(const std::array<std::vector<FrameEdgePoint>, 4> &edges, const SurfaceModel &surface, size_t width, size_t height, double quad[8]) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29}; constexpr int kSamples = 33;
    const auto cost = [&](const double q[8]) {
        SurfaceModel m = surface; if (!surface_pose(m, q, width, height, surface.focal)) return 1e300;
        const SurfaceScope scope(m);
        double h[8]; if (!solve_homography(canonical, q, h)) return 1e300;
        double total = 0;
        for (int side = 0; side < 4; ++side) {
            ImagePoint line[kSamples];
            for (int i = 0; i < kSamples; ++i) {
                const double t = 2.5 + 24.0 * i / (kSamples - 1);
                const double x = side == 0 ? t : side == 1 ? 29 - kFrameOuter : side == 2 ? 29 - t : kFrameOuter, y = side == 0 ? kFrameOuter : side == 1 ? t : side == 2 ? 29 - kFrameOuter : 29 - t;
                if (!map_point(h, x, y, line[i])) return 1e300;
            }
            for (const auto &e : edges[static_cast<size_t>(side)]) {
                double nearest = 1e300;
                for (int i = 0; i + 1 < kSamples; ++i) {
                    const double ax = line[i].x, ay = line[i].y, bx = line[i + 1].x - ax, by = line[i + 1].y - ay, len2 = bx * bx + by * by;
                    const double u = len2 > 0 ? std::clamp(((e.x - ax) * bx + (e.y - ay) * by) / len2, 0.0, 1.0) : 0.0;
                    const double dx = e.x - (ax + u * bx), dy = e.y - (ay + u * by); nearest = std::min(nearest, dx * dx + dy * dy);
                }
                total += std::min(nearest, 9.0);  // a stray point counts for at most 3 pixels
            }
        }
        return total;
    };
    double best = cost(quad); if (best >= 1e300) return;
    for (double step = 2.0; step >= 0.05; step /= 2) for (int pass = 0; pass < 6; ++pass) {
        bool improved = false;
        for (int k = 0; k < 8; ++k) for (const double direction : {1.0, -1.0}) {
            double q[8]; std::copy(quad, quad + 8, q); q[k] += direction * step;
            const double c = cost(q);
            if (c < best) { best = c; std::copy(q, q + 8, quad); improved = true; }
        }
        if (!improved) break;
    }
}

// Version from the module lattice alone, without the keys. On a bent symbol
// the flat map puts the keys in the wrong place, but the lattice keeps its
// period: bending shifts modules by whole pitches while stretching them by
// only a few percent. The gaps between modules give every scan line across
// the module field a strong component at exactly one cycle per pitch, so the
// grid size shows as the frequency where that component is strongest. Scan
// lines run along the four bands between the frame and the dial, which hold
// only modules in every version. Fills `versions` with up to `capacity`
// candidates, best first, and returns how many.
size_t versions_from_lattice(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8], int *versions, size_t capacity) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    double h[8]; if (!solve_homography(canonical, quad, h)) return 0;
    const double unit_px = quad_mean_side(quad) / 29.0;
    const double step = std::max(0.02, 0.5 / std::max(unit_px, 1.0));  // half a pixel, or a sixth of the finest pitch
    constexpr double kFrom = 6.6, kTo = 22.4, kNear = 1.7, kFar = 4.2; constexpr int kLines = 6;
    const size_t count = static_cast<size_t>((kTo - kFrom) / step);
    if (count < 32) return 0;
    std::vector<std::vector<double>> lines;
    for (int side = 0; side < 4; ++side) for (int k = 0; k < kLines; ++k) {
        const double d = kNear + (kFar - kNear) * (k + 0.37) / kLines;
        std::vector<double> line(count); bool ok = true;
        for (size_t i = 0; i < count && ok; ++i) {
            const double t = kFrom + step * static_cast<double>(i);
            const double x = side == 0 ? t : side == 1 ? 29 - d : side == 2 ? 29 - t : d, y = side == 0 ? d : side == 1 ? t : side == 2 ? 29 - d : 29 - t;
            line[i] = gray_sample(gray, width, height, stride, h, x, y, 0.0); ok = line[i] >= 0;
        }
        if (ok) lines.push_back(std::move(line));
    }
    if (lines.size() < 6) return 0;
    std::array<std::pair<double, int>, kScalableVersions> ranked;
    for (int v = 1; v <= kScalableVersions; ++v) {
        const double pitch = 29.0 / kScalableGrid[static_cast<size_t>(v - 1)];
        // Windows of 16 pitches (the whole line if it is shorter), half overlapped.
        const size_t window = std::min(count, static_cast<size_t>(16.0 * pitch / step));
        // Power at the lattice frequency against the power 12% to either
        // side: data modules spread their energy smoothly over frequency,
        // the lattice puts a line on top of it.
        double power[3] = {0, 0, 0};
        if (pitch / step >= 2.5 && window >= 16) {
            for (int which = 0; which < 3; ++which) {
                const double omega = kTau * step / pitch * (which == 0 ? 1.0 : which == 1 ? 0.88 : 1.12);
                for (const auto &line : lines) for (size_t start = 0; start + window <= count; start += std::max<size_t>(1, window / 2)) {
                    double mean = 0; for (size_t i = 0; i < window; ++i) mean += line[start + i]; mean /= static_cast<double>(window);
                    double re = 0, im = 0;
                    for (size_t i = 0; i < window; ++i) {
                        const double w = 0.5 - 0.5 * std::cos(kTau * (static_cast<double>(i) + 0.5) / static_cast<double>(window)), value = w * (line[start + i] - mean);
                        re += value * std::cos(omega * static_cast<double>(i)); im += value * std::sin(omega * static_cast<double>(i));
                    }
                    power[which] += re * re + im * im;
                }
            }
        }
        ranked[static_cast<size_t>(v - 1)] = {power[0] > 0 ? power[0] / (0.5 * (power[1] + power[2]) + 1e-9) : 0.0, v};
    }
    const auto by_version = ranked;
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    size_t n = 0;
    const auto add = [&](int v) { for (size_t k = 0; k < n; ++k) if (versions[k] == v) return; if (n < capacity) versions[n++] = v; };
    for (const auto &[power, v] : ranked) {
        // A second candidate must be nearly as clear as the first.
        if (n >= capacity || power < 2.5 || power < 0.6 * ranked[0].first) break;
        // A lattice also has a line at twice its frequency. If the version
        // with half this grid shows a clear line of its own, it is the
        // fundamental and goes first.
        const int grid = kScalableGrid[static_cast<size_t>(v - 1)];
        for (int u = 1; u < v; ++u) {
            const int half = kScalableGrid[static_cast<size_t>(u - 1)];
            if (std::abs(2 * half - grid) <= std::max(1, grid / 40) && by_version[static_cast<size_t>(u - 1)].first >= std::max(2.5, 0.3 * power)) add(u);
        }
        add(v);
    }
    return n;
}

// Blur from a known edge. The frame stroke's outer edge is a sharp step in
// the design, so its profile in the image is the camera's edge response, and
// the width of that profile's gradient is the blur width (the standard
// deviation of the point spread function), measured directly rather than
// guessed from the data. Returned in canonical units; 0 if it cannot be
// measured.
double measure_edge_blur(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8]) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    double h[8]; if (!solve_homography(canonical, quad, h)) return 0;
    const double unit_px = quad_mean_side(quad) / 29.0;
    if (unit_px < 1.5) return 0;
    const double step = 0.5 / unit_px; const int half = std::clamp(static_cast<int>(std::ceil(0.3 / step)), 4, 24);
    std::vector<double> widths;
    for (int side = 0; side < 4; ++side) for (int i = 0; i < 12; ++i) {
        const double t = 6.0 + 17.0 * i / 11.0;
        double x, y, nx, ny;
        if (side == 0) { x = t; y = kFrameOuter; nx = 0; ny = -1; }
        else if (side == 1) { x = 29 - kFrameOuter; y = t; nx = 1; ny = 0; }
        else if (side == 2) { x = 29 - t; y = 29 - kFrameOuter; nx = 0; ny = 1; }
        else { x = kFrameOuter; y = 29 - t; nx = -1; ny = 0; }
        std::vector<double> profile(static_cast<size_t>(2 * half + 1)); bool ok = true;
        for (int k = -half; k <= half && ok; ++k) { profile[static_cast<size_t>(k + half)] = gray_sample(gray, width, height, stride, h, x + nx * k * step, y + ny * k * step, 0.0); ok = profile[static_cast<size_t>(k + half)] >= 0; }
        if (!ok) continue;
        const double sign = profile.back() >= profile.front() ? 1.0 : -1.0;
        double total = 0, first = 0, second = 0;
        for (int k = 0; k < 2 * half; ++k) {
            const double g = std::max(0.0, sign * (profile[static_cast<size_t>(k + 1)] - profile[static_cast<size_t>(k)])), position = (k + 0.5 - half) * step;
            total += g; first += g * position; second += g * position * position;
        }
        if (total < 8.0) continue;
        const double mean = first / total, variance = second / total - mean * mean;
        if (variance > 0) widths.push_back(std::sqrt(variance));
    }
    if (widths.size() < 12) return 0;
    std::nth_element(widths.begin(), widths.begin() + static_cast<std::ptrdiff_t>(widths.size() / 2), widths.end());
    return widths[widths.size() / 2];
}

// Sharpness of the module lattice under a quadrilateral: the spread of a
// sample of Cartesian sites. Samples taken on module centres are either ink
// or background; off-centre they mix, and the spread falls. It needs no
// polarity, threshold or key.
double lattice_sharpness(const ScalableLayout &L, const std::vector<uint32_t> &sites, const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad[8]) {
    constexpr double canonical[8] = {0, 0, 29, 0, 29, 29, 0, 29};
    double h[8]; if (!solve_homography(canonical, quad, h)) return 0;
    double sum = 0, sq = 0; int n = 0;
    for (uint32_t i : sites) {
        const double v = gray_sample(gray, width, height, stride, h, L.nodes[i].x, L.nodes[i].y, 0.1 * L.pitch);
        if (v < 0) continue;
        sum += v; sq += v * v; ++n;
    }
    if (n < static_cast<int>(sites.size()) / 2 || n < 16) return 0;
    const double mean = sum / n;
    return std::sqrt(std::max(0.0, sq / n - mean * mean));
}

// Final registration on the data itself: per-corner moves that sharpen the
// module lattice, limited to `limit_modules` from where the frame fit put
// each corner (so the lattice cannot slip by a whole module).
void refine_on_lattice(const ScalableLayout &L, const uint8_t *gray, size_t width, size_t height, size_t stride, double quad[8], double limit_modules) {
    std::vector<uint32_t> sites;
    const size_t total = L.nodes.size(), stride_sites = std::max<size_t>(1, total / 700);
    for (size_t i = 0; i < total; i += stride_sites) if (L.nodes[i].kind == 0) sites.push_back(static_cast<uint32_t>(i));
    if (sites.size() < 48) { sites.clear(); for (size_t i = 0; i < total; ++i) if (L.nodes[i].kind == 0) sites.push_back(static_cast<uint32_t>(i)); }
    if (sites.size() < 24) return;
    const double module_px = quad_mean_side(quad) / L.grid, limit = limit_modules * module_px;
    double start[8]; std::copy(quad, quad + 8, start);
    double best = lattice_sharpness(L, sites, gray, width, height, stride, quad);
    if (best <= 0) return;
    for (double step = 0.3 * module_px; step >= 0.03 * module_px && step >= 0.05; step /= 2) {
        for (int pass = 0; pass < 4; ++pass) {
            bool improved = false;
            for (int corner = 0; corner < 4; ++corner) for (const auto &[dx, dy] : {std::pair{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}}) {
                double q[8]; std::copy(quad, quad + 8, q);
                q[2 * corner] += dx * step; q[2 * corner + 1] += dy * step;
                if (std::hypot(q[2 * corner] - start[2 * corner], q[2 * corner + 1] - start[2 * corner + 1]) > limit) continue;
                const double score = lattice_sharpness(L, sites, gray, width, height, stride, q);
                if (score > best * 1.002) { best = score; std::copy(q, q + 8, quad); improved = true; }
            }
            if (!improved) break;
        }
    }
}

}  // namespace
