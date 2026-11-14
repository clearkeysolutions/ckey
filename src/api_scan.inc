// Public C ABI: scan functions. Included after core_internal.inc.
#include "scan_internal.inc"
#include "ml_locator.inc"
#include "ml_sites.inc"

extern "C" {

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

double ckey_calibration_score_scalable_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                             const double quad_xy[8], uint32_t *out_version) {
    if (!valid_image_args(gray, width, height, stride, quad_xy)) return -1.0;
    double score = 0; const int v = best_scalable_version(gray, width, height, stride, quad_xy, score);
    if (out_version) *out_version = static_cast<uint32_t>(v);
    return score;
}

}  // extern "C"
namespace {
ckey_status decode_scalable_at(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad_xy[8],
                               uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len, uint32_t *out_version,
                               uint32_t *out_layout, ckey_diagnostics *diag);
}  // namespace
extern "C" {

// CKEY_SCAN_OPTION_FAST for the current thread's scan: no ML retries.
inline bool &scan_fast() { static thread_local bool fast = false; return fast; }
struct FastScanScope {
    bool previous;
    explicit FastScanScope(bool on) : previous(scan_fast()) { scan_fast() = on; }
    ~FastScanScope() { scan_fast() = previous; }
};

// With a corner refiner loaded, a failed decode is retried once from the
// refined corners (the refiner only moves corners; the decoder decides).
ckey_status ckey_decode_scalable_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad_xy[8],
                                       uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len, uint32_t *out_version,
                                       uint32_t *out_layout, ckey_diagnostics *diag) {
    const ckey_status st = decode_scalable_at(gray, width, height, stride, quad_xy, out_payload, out_capacity, out_payload_len, out_version, out_layout, diag);
    if (st == CKEY_OK || st == CKEY_INVALID_ARGUMENT || st == CKEY_INSUFFICIENT_BUFFER) return st;
    if (scan_fast()) return st;
    double refined[8];
    {
        const WorkScope work(kFindWorkBudget);
        if (!ml_refine_quad(gray, width, height, stride, quad_xy, refined)) return st;
    }
    const ckey_status again = decode_scalable_at(gray, width, height, stride, refined, out_payload, out_capacity, out_payload_len, out_version, out_layout, diag);
    if (again == CKEY_OK && diag) { char m[96]; std::snprintf(m, sizeof m, "%.66s (ML corners)", diag->message); set_status(diag, CKEY_OK, m); }
    return again == CKEY_OK || again == CKEY_INSUFFICIENT_BUFFER ? again : st;
}

}  // extern "C"
namespace {
ckey_status decode_scalable_at(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad_xy[8],
                               uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len, uint32_t *out_version,
                               uint32_t *out_layout, ckey_diagnostics *diag) {
    if (!out_payload || !out_payload_len || !valid_image_args(gray, width, height, stride, quad_xy)) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 0 image, quadrilateral, or output buffer"); return CKEY_INVALID_ARGUMENT;
    }
    const WorkScope work(kDecodeWorkBudget);
    // The lens is measured from the bow of the frame's sides; if one is
    // found, decoding is tried through it first and then without it.
    LensModel measured{};
    {
        const LensScope none(LensModel{});
        std::array<std::vector<FrameEdgePoint>, 4> sides;
        if (frame_edge_points(gray, width, height, stride, quad_xy, 0.7, sides)) measured = estimate_lens(sides, width, height);
    }
    // A second, more sensitive estimate comes from the rings themselves (see
    // lens_from_rings): find the best-fitting version and orientation with no
    // lens, then ask which lens makes its marks sharpest.
    LensModel from_rings{};
    {
        const LensScope none(LensModel{});
        double best_score = 0; int best_v = 0, best_f = 0, best_reverse = 0, best_start = 0;
        for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
            double h[8]; bool ok; orientation_homography(quad_xy, reverse, start, h, ok); if (!ok) continue;
            const KeyFit skeleton = scalable_skeleton_fit(gray, width, height, stride, h);
            if (!skeleton.valid || skeleton.evidence < kDenseKeyGate || skeleton.contrast < kMinKeyContrast) continue;
            for (int f = 0; f < kScalableFamilies; ++f) for (int v = 1; v <= kScalableVersions; ++v) {
                const KeyFit keys = scalable_version_fit(scalable_family_layout(v, f), gray, width, height, stride, h);
                if (!keys.valid || keys.evidence < 0.85) continue;
                const double score = keys.contrast * keys.evidence * keys.evidence;
                if (score > best_score) { best_score = score; best_v = v; best_f = f; best_reverse = reverse; best_start = start; }
            }
        }
        if (best_v > 0) {
            double oriented[8];
            for (int i = 0; i < 4; ++i) { const int q = (best_start + (best_reverse ? -i : i) + 8) % 4; oriented[2 * i] = quad_xy[2 * q]; oriented[2 * i + 1] = quad_xy[2 * q + 1]; }
            from_rings = lens_from_rings(scalable_family_layout(best_v, best_f), gray, width, height, stride, oriented);
        }
    }
    std::vector<LensModel> lenses;
    // The ring estimate supersedes the frame-bow one when it found a lens.
    for (const LensModel &candidate_lens : {from_rings, from_rings.k1 != 0 ? from_rings : measured, LensModel{}}) {
        bool seen = false; for (const auto &l : lenses) seen = seen || std::abs(l.k1 - candidate_lens.k1) < 0.004;
        if (!seen) lenses.push_back(candidate_lens);
    }
    // Geometry hypotheses, tried in turn: the lens candidates on a flat
    // surface, then (only if none of them decodes) a bent surface. On a bent
    // symbol the flat map misplaces the keys, so the version comes from the
    // module lattice's period and the bend from the modules themselves (see
    // versions_from_lattice and surface_from_marks).
    struct Geometry { LensModel lens; SurfaceModel surface; int version; std::array<double, 8> quad; };
    const double *const given_quad = quad_xy;
    std::vector<Geometry> geometries;
    for (const LensModel &l : lenses) geometries.push_back({l, SurfaceModel{}, 0, {}});
    bool surfaces_tried = false; std::array<double, 8> bent_quad{};
    for (size_t geometry_index = 0; ; ++geometry_index) {
    if (geometry_index == geometries.size()) {
        if (surfaces_tried || work_exhausted()) break;
        surfaces_tried = true;
        // The search for a surface gets a bounded share of the decode's work.
        const uint64_t surface_work_start = work_budget().left;
        const auto surface_work_spent = [&] { return surface_work_start - work_budget().left > kSurfaceWorkBudget; };
        const LensScope none(LensModel{});
        // A bend also curves the frame's sides, which puts the straight-line
        // corner fit off; refit the corners from curved sides first.
        // If no surface fits those corners, the given ones are tried.
        std::copy(quad_xy, quad_xy + 8, bent_quad.begin());
        // (A strong bend seen at a tilt bows a side by more than a unit, hence the wide search.)
        std::array<std::vector<FrameEdgePoint>, 4> frame_points;
        double bow = 0;
        // A bend that matters shows in the frame: its sides curve. Frames
        // whose sides are straight (most unreadable frames are flat and
        // blurred) skip the search. The dearer search over every low version
        // needs a clearer curve.
        constexpr double kBowToSearch = 0.06, kBowToSearchAllLow = 0.15;
        const bool refitted = fit_frame_curves(gray, width, height, stride, bent_quad.data(), 1.8, &frame_points, &bow) && bow >= kBowToSearch;
        // No frame stroke on all four sides: not a symbol's own border (the
        // candidates include outlines inside a symbol), so nothing to bend.
        for (int given = 0; refitted && given < 2 && geometry_index == geometries.size(); ++given) {
            if (given) std::copy(given_quad, given_quad + 8, bent_quad.begin());
            int candidates[kScalableVersions]; size_t found = versions_from_lattice(gray, width, height, stride, bent_quad.data(), candidates, 2);
            // The low versions have few modules, in clumps beside the finders,
            // and no scan line crosses enough of them to show a period. Their
            // surface search is cheap (large modules need only a coarse one),
            // so each is tried; a wrong version seldom fits any surface, and the
            // keys, Reed-Solomon and the CRC reject it when it does.
            if (found == 0 && given == 0 && bow >= kBowToSearchAllLow) for (int v = 1; v <= kScalableVersions && scalable_layout(v, 0).pitch >= 0.45; ++v) candidates[found++] = v;
            for (size_t i = 0; i < found && !surface_work_spent(); ++i) {
                const ScalableLayout &layout = scalable_layout(candidates[i], 0);
                SurfaceModel surface = surface_from_marks(layout, gray, width, height, stride, bent_quad.data());
                if (surface.kappa == 0) continue;
                // The surface says what shape the frame's sides have: fit the
                // corners to the measured frame through it, then the surface
                // again from those corners, twice.
                std::array<double, 8> fitted = bent_quad;
                for (int round = 0; round < 2; ++round) {
                    corners_from_bent_frame(frame_points, surface, width, height, fitted.data());
                    const SurfaceModel again = surface_from_marks(layout, gray, width, height, stride, fitted.data(), &surface);
                    if (again.kappa == 0) break;
                    surface = again;
                }
                // Last, settle the corners on the module lattice as seen
                // through the bend, and place the camera for the result.
                Geometry bent{LensModel{}, surface, candidates[i], fitted};
                { SurfaceModel placed = surface; if (surface_pose(placed, fitted.data(), width, height, surface.focal)) surface = placed; }
                { const SurfaceScope scope(surface); refine_on_lattice(layout, gray, width, height, stride, bent.quad.data(), 0.4); }
                SurfaceModel settled = surface;
                if (surface_pose(settled, bent.quad.data(), width, height, surface.focal)) bent.surface = settled;
                geometries.push_back(bent);  // a wrong version can fit some surface too: keep looking
                if (work_exhausted()) break;
            }
        }
        if (geometry_index == geometries.size()) break;
    }
    const Geometry geometry = geometries[geometry_index];
    quad_xy = geometry.surface.kappa != 0 ? geometry.quad.data() : given_quad;
    const LensScope lens_scope(geometry.lens);
    double edge_blur = 0; { const SurfaceScope bent(geometry.surface); edge_blur = measure_edge_blur(gray, width, height, stride, quad_xy); }
    // 1. The skeleton gates orientation. 2. Each version's keys are tested at
    //    that orientation; only agreeing versions are fully sampled, best
    //    first. 3. RS, the frame's version byte, and CRC-32C decide.
    struct Attempt { double score; int version, family; double h[8]; SurfaceModel surface; };
    std::vector<Attempt> attempts;
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        // The surface is described in the given corner order; turn it to
        // this orientation's.
        SurfaceModel surface{};
        if (geometry.surface.kappa != 0) {
            constexpr double corner[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            double oriented[8]; int q[4];
            for (int i = 0; i < 4; ++i) { q[i] = (start + (reverse ? -i : i) + 8) % 4; oriented[2 * i] = quad_xy[2 * q[i]]; oriented[2 * i + 1] = quad_xy[2 * q[i] + 1]; }
            // Canonical corner i of this orientation is corner q[i] of the
            // given order. The given order's x axis runs from its corner 0 to
            // its corner 1; find those two among q.
            int at[4]; for (int i = 0; i < 4; ++i) at[q[i]] = i;
            const double dx = corner[at[1]][0] - corner[at[0]][0], dy = corner[at[1]][1] - corner[at[0]][1];
            const double ex = corner[at[3]][0] - corner[at[0]][0], ey = corner[at[3]][1] - corner[at[0]][1];
            if (!surface_reoriented(geometry.surface, oriented, width, height, dx, dy, ex, ey, surface)) continue;
        }
        const SurfaceScope surface_scope(surface);
        double h[8]; bool ok; orientation_homography(quad_xy, reverse, start, h, ok); if (!ok) continue;
        const KeyFit skeleton = scalable_skeleton_fit(gray, width, height, stride, h);
        if (!skeleton.valid || skeleton.evidence < kDenseKeyGate || skeleton.contrast < kMinKeyContrast) continue;
        for (int f = 0; f < kScalableFamilies; ++f) for (int v = 1; v <= kScalableVersions; ++v) {
            if (geometry.version != 0 && v != geometry.version) continue;
            const KeyFit keys = scalable_version_fit(scalable_family_layout(v, f), gray, width, height, stride, h);
            const int hint = version_hint().version;
            const bool hinted = hint && std::abs(v - hint) <= version_hint().window;
            if (!keys.valid || keys.evidence < (hinted ? kHintedKeyGate : 0.85)) continue;
            Attempt a{keys.contrast * keys.evidence * keys.evidence * (hinted ? kHintBoost : 1.0), v, f, {}, surface}; std::copy(h, h + 8, a.h);
            attempts.push_back(a);
        }
    }
    std::sort(attempts.begin(), attempts.end(), [](const Attempt &a, const Attempt &b) { return a.score > b.score; });
    // Fast (live) scans try the two best version hypotheses; the next frame
    // gets another chance in tens of milliseconds.
    const size_t max_attempts = scan_fast() ? 2 : kMaxDecodeAttempts;
    // Standard and Standard arc share every key but the Ring Key, which is
    // faint in a small or blurred code; their hypotheses for one version can
    // score within noise of each other. When the best is one of them, the
    // next try is the same version and orientation in the other, so a fast
    // scan never spends both of its tries on one family.
    if (!attempts.empty() && scalable_fine(attempts[0].family)) {
        const Attempt &best = attempts[0];
        const int sibling = best.family == 0 ? 3 : 0;
        for (size_t i = 1; i < attempts.size(); ++i)
            if (attempts[i].family == sibling && attempts[i].version == best.version && std::equal(best.h, best.h + 8, attempts[i].h)) {
                const Attempt moved = attempts[i];
                attempts.erase(attempts.begin() + static_cast<std::ptrdiff_t>(i));
                attempts.insert(attempts.begin() + 1, moved);
                break;
            }
    }
    if (attempts.size() > max_attempts) attempts.resize(max_attempts);
    std::vector<uint8_t> candidate(65535), codeword, reliability;
    SiteRead read; SiteSamples samples;
    for (const auto &attempt : attempts) {
        if (work_exhausted()) break;
        const ScalableLayout &L = scalable_family_layout(attempt.version, attempt.family);
        const SurfaceScope surface_scope(attempt.surface);
        if (!scalable_sample_sites(L, gray, width, height, stride, attempt.h, samples)) continue;
        samples.edge_blur = edge_blur;
      bool hopeless = false;
      const bool leading = &attempt - attempts.data() < 2;  // sequence detection is costly: only for the two best hypotheses
      // With a site model loaded, its reading is tried right after the plain
      // one (it is built for the frames the plain reading loses).
      const bool ml_sites = !scan_fast() && ml_sites_current() != nullptr;
      std::vector<float> ml_logits, ml_doubt; bool ml_done = false;
      int modes_tried = 0;
      for (const unsigned mode : kReadModesWithMl) {
        if (work_exhausted()) break;
        // Fast scans: the plain reading and the local-levels ring-sequence one.
        if (scan_fast() && mode != kReadMl && ++modes_tried > 2) break;
        if (mode == kReadMl) {
            if (!ml_sites || !leading) continue;
            if (!ml_done) { ml_logits = ml_site_logits(L, gray, width, height, stride, attempt.h, &ml_doubt); ml_done = true; }
            if (ml_logits.size() != L.nodes.size()) continue;
            ml_site_read(ml_logits, read, &ml_doubt);
        } else {
        if ((mode & kReadRingSequence) && !leading) continue;
        if (hopeless && !(mode & kReadLocalLevels)) continue;  // only a change of levels can rescue a reading this far off
        if (!scalable_decide_sites(L, samples, mode, read)) continue;
        }
        // Both placements share the sites. Try the one whose raw header reads
        // better first; the other gets the full soft decode only if its own
        // header is plausible.
        // A family without a byte-grouped placement (Pixel) tries only its own.
        const int grouped_layout = scalable_layout_of(attempt.family, true);
        const ScalableLayout &G = grouped_layout >= 0 ? scalable_layout(attempt.version, grouped_layout) : L;
        std::vector<uint8_t> cw[2], rel[2];
        scalable_assemble(L, read, cw[0], rel[0]);
        if (grouped_layout >= 0) scalable_assemble(G, read, cw[1], rel[1]);
        const int match[2] = {scalable_header_matches(L, cw[0]), grouped_layout >= 0 ? scalable_header_matches(G, cw[1]) : -1};
        capture_evidence(L, read, std::max(match[0], match[1]), attempt.h);
        const int first = match[1] > match[0] ? 1 : 0;
        // The header reads before any correction (the code is systematic).
        // If almost none of it is right under either placement, this reading
        // is far from the codeword: skip it rather than spend soft decoding
        // on it. The remaining strategies get the same test.
        if (std::max(match[0], match[1]) < 2) { if (mode == 0) hopeless = true; continue; }
        for (int turn = 0; turn < 2; ++turn) {
            const int which = turn == 0 ? first : 1 - first;
            const ScalableLayout &layout = which ? G : L;
            if (turn == 1 && match[which] < 2) continue;
            const bool soft = match[which] >= 3;
            size_t length = 0; ckey_diagnostics local{};
            if (ckey_decode_scalable_soft(cw[which].data(), soft ? rel[which].data() : nullptr, cw[which].size(), static_cast<uint32_t>(attempt.version),
                                          static_cast<ckey_scalable_layout>(scalable_layout_of(attempt.family, which == 1)), candidate.data(), candidate.size(), &length, &local) != CKEY_OK) continue;
            (void)layout;
            if (out_capacity < length) { set_status(diag, CKEY_INSUFFICIENT_BUFFER, "Profile 0 output buffer is too small"); return CKEY_INSUFFICIENT_BUFFER; }
            std::memcpy(out_payload, candidate.data(), length); *out_payload_len = length;
            if (out_version) *out_version = static_cast<uint32_t>(attempt.version);
            if (out_layout) *out_layout = static_cast<uint32_t>(scalable_layout_of(attempt.family, which == 1));
            if (mode == kReadMl) { char m[96]; std::snprintf(m, sizeof m, "%.70s (ML site reading)", local.message); set_status(diag, CKEY_OK, m); }
            else set_status(diag, CKEY_OK, local.message);
            if (diag) { diag->corrected_symbols = local.corrected_symbols; diag->erasures_used = local.erasures_used; }
            return CKEY_OK;
        }
      }
    }
    }
    set_status(diag, CKEY_UNCORRECTABLE, work_exhausted() ? "Profile 0 decode stopped at its work budget" : "no Profile 0 version passed its keys, Reed-Solomon and CRC");
    return CKEY_UNCORRECTABLE;
}

}  // namespace (decode_scalable_at)

namespace {
// Border search shared by ckey_find_borders_gray8 and the frame scan. `cut`
// (may be null) reports a border-like outline that runs off the image.
size_t find_borders(const uint8_t *gray, size_t width, size_t height, size_t stride, double *out_quads, size_t max_quads, bool *cut) {
    if (cut) *cut = false;
    const WorkScope work(kFindWorkBudget);
    // About 960 x 540 area-averaged samples at any capture size (step 2 at 1080p, 4 at 4K).
    const size_t step = std::max<size_t>(1, 2 * std::max(width, height) / 1920);
    const ReducedImage r = reduce_area(gray, width, height, stride, step);
    const size_t sw = r.w, sh = r.h, min_side = std::min(width, height);
    std::vector<BorderCandidate> candidates;
    std::vector<uint8_t> visited(sw * sh), mask(sw * sh), mask2(sw * sh);
    std::vector<uint32_t> queue;
    // Pass 1: one level for the whole frame (both polarities).
    for (int polarity = 0; polarity < 2; ++polarity) {
        const bool dark = polarity == 1;
        const uint8_t t = reduced_threshold(r, dark);
        for (size_t i = 0; i < mask.size(); ++i) mask[i] = dark ? r.px[i] <= t : r.px[i] >= t;
        collect_border_candidates(mask, r, min_side, visited, queue, candidates, cut);
    }
    // A clean, high-contrast symbol is already found by pass 1: if one of its
    // candidates fits the CKey skeleton strongly at every key sample, the
    // local passes (which cost several times as much) are skipped.
    bool strong = false;
    for (const auto &c : candidates) {
        double q[8]; double cx = 0, cy = 0;
        for (int i = 0; i < 4; ++i) { cx += c.quad[2 * i] / 4; cy += c.quad[2 * i + 1] / 4; }
        for (int i = 0; i < 4; ++i) { q[2 * i] = cx + (c.quad[2 * i] - cx) * kBorderToField; q[2 * i + 1] = cy + (c.quad[2 * i + 1] - cy) * kBorderToField; }
        if (valid_quad(q) && scalable_skeleton_score(gray, width, height, stride, q) >= 60.0) { strong = true; break; }
    }
    // Passes 2 and 3: local levels. Each pixel is compared with the mean of
    // its own neighbourhood, so a symbol separates whatever the rest of the
    // frame looks like (a bright screen in a dark room, a shaded page, a
    // low-contrast print). Two window sizes cover thin and thick strokes.
    for (const long radius : {std::max<long>(7, static_cast<long>(std::min(sw, sh)) / 36), std::max<long>(20, static_cast<long>(std::min(sw, sh)) / 9)}) {
        if (strong) break;
        for (size_t y = 0; y < sh; ++y) for (size_t x = 0; x < sw; ++x) {
            double mean, sd; r.window(static_cast<long>(x) - radius, static_cast<long>(y) - radius, static_cast<long>(x) + radius + 1, static_cast<long>(y) + radius + 1, mean, sd);
            const double margin = std::max(5.0, 0.45 * sd), v = r.px[y * sw + x];
            mask[y * sw + x] = v < mean - margin; mask2[y * sw + x] = v > mean + margin;
        }
        collect_border_candidates(mask, r, min_side, visited, queue, candidates, cut);
        collect_border_candidates(mask2, r, min_side, visited, queue, candidates, cut);
    }
    // The passes find the same border more than once: keep the first of each.
    std::vector<BorderCandidate> unique;
    for (const auto &c : candidates) {
        const double tolerance = 0.04 * quad_mean_side(c.quad.data()) + 2.0;
        bool duplicate = false;
        for (const auto &u : unique) if (quad_corner_distance(c.quad.data(), u.quad.data()) < tolerance) { duplicate = true; break; }
        if (!duplicate) unique.push_back(c);
    }
    candidates.swap(unique);
    std::stable_sort(candidates.begin(), candidates.end(), [](const BorderCandidate &a, const BorderCandidate &b) { return a.rank > b.rank; });
    // Every profile shares one skeleton in normalized coordinates (frame
    // stroke, finder arcs, orientation split; Profile 3 is the same shape at
    // 4x). Re-rank the leading shape candidates by how well that skeleton
    // fits, so a symbol's own interior components (dense payload clusters,
    // rings) cannot crowd its true border out of the returned set.
    if (candidates.size() > kMaxSkeletonRanked) candidates.resize(kMaxSkeletonRanked);
    for (auto &c : candidates) {
        double q[8]; double cx = 0, cy = 0;
        for (int i = 0; i < 4; ++i) { cx += c.quad[2 * i] / 4; cy += c.quad[2 * i + 1] / 4; }
        for (int i = 0; i < 4; ++i) { q[2 * i] = cx + (c.quad[2 * i] - cx) * kBorderToField; q[2 * i + 1] = cy + (c.quad[2 * i + 1] - cy) * kBorderToField; }
        c.rank = valid_quad(q) ? scalable_skeleton_score(gray, width, height, stride, q) + 1e-3 * c.rank : -1;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const BorderCandidate &a, const BorderCandidate &b) { return a.rank > b.rank; });
    const size_t count = std::min(max_quads, candidates.size());
    for (size_t i = 0; i < count; ++i) std::copy(candidates[i].quad.begin(), candidates[i].quad.end(), out_quads + 8 * i);
    return count;
}
}  // namespace

extern "C" {

size_t ckey_find_borders_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                               double *out_quads, size_t max_quads) {
    if (!gray || !out_quads || max_quads == 0 || width < 16 || height < 16 || stride < width ||
        height > std::numeric_limits<size_t>::max() / stride) return 0;
    return find_borders(gray, width, height, stride, out_quads, max_quads, nullptr);
}

namespace {
// Scores a refined quadrilateral by the field it claims to be. It calls the
// scan library's calibration scores, so it lives here rather than in the
// shared internals, which the codec and render libraries also compile.
double field_score(int field, const uint8_t *gray, size_t width, size_t height, size_t stride, const double q[8]) {
    if (field == CKEY_FIELD_SCALABLE) return frame_edge_score(gray, width, height, stride, q, 4.0);
    if (field == CKEY_FIELD_LOCATOR) return ckey_calibration_score_gray8(gray, width, height, stride, q);
    if (field == CKEY_FIELD_TEXT) return ckey_calibration_score_text_gray8(gray, width, height, stride, q);
    return ckey_calibration_score_extended_gray8(gray, width, height, stride, q);
}
}  // namespace

}  // extern "C"
namespace { inline bool &refine_legacy_start() { static thread_local bool flag = false; return flag; } }
extern "C" {

double ckey_refine_quad_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                              const double border_quad[8], ckey_field field, double out_quad[8]) {
    if (!out_quad || (field != CKEY_FIELD_SCALABLE && field != CKEY_FIELD_LOCATOR && field != CKEY_FIELD_TEXT && field != CKEY_FIELD_EXTENDED) ||
        !valid_image_args(gray, width, height, stride, border_quad)) return -1.0;
    const WorkScope work(kRefineWorkBudget);
    const LensScope lens_scope(LensModel{});  // no lens until the frame fit measures one
    // Start from the frame itself. Every profile has the same frame stroke in
    // normalised coordinates, so its edges are located directly first; the
    // searches below then begin within a pixel or so of the field instead of
    // wherever the border search's coarse outline put it.
    // The border search sometimes returns the stroke's inner outline (the
    // moat's rim) or an outer rim instead of the stroke's outer edge, so
    // three sizes are tried and the best-fitting frame wins.
    double start_quad[8]; double start_scale = kBorderToField;
    double alternative_score = -1, alternative_quad[8] = {};
    bool start_has_skeleton = false;
    {
        double sx = 0, sy = 0; for (int i = 0; i < 4; ++i) { sx += border_quad[2 * i] / 4; sy += border_quad[2 * i + 1] / 4; }
        double best_fit = 0; bool fitted = false;
        for (const double grow : {1.0, 1.0 + 1.3 / 29.0, 1.0 - 1.3 / 29.0}) {
            double field[8]; for (int i = 0; i < 4; ++i) { field[2 * i] = sx + (border_quad[2 * i] - sx) * kBorderToField * grow; field[2 * i + 1] = sy + (border_quad[2 * i + 1] - sy) * kBorderToField * grow; }
            if (!valid_quad(field) || !fit_frame_edges(gray, width, height, stride, field, 0.6)) continue;
            fit_frame_edges(gray, width, height, stride, field, 0.25);
            const double fit = frame_edge_score(gray, width, height, stride, field, 1.5) * (0.25 + scalable_skeleton_score(gray, width, height, stride, field));
            if (fit > best_fit) { best_fit = fit; std::copy(field, field + 8, start_quad); fitted = true; }
            // The outline as found already fits a full skeleton: no need to try the other sizes.
            if (work_exhausted() || (grow == 1.0 && scalable_skeleton_score(gray, width, height, stride, field) >= 60.0)) break;
        }
        if (fitted && !refine_legacy_start()) start_scale = 1.0; else { std::copy(border_quad, border_quad + 8, start_quad); start_scale = kBorderToField; }
        // A clean frame with no CKey skeleton inside it (finder arms, moat,
        // quiet margin) is some other rectangle: stop before the searches.
        // Every profile shares that skeleton.
        if (fitted && !refine_legacy_start()) {
            for (int reverse = 0; reverse < 2 && !start_has_skeleton; ++reverse) for (int start = 0; start < 4 && !start_has_skeleton; ++start) {
                double h[8]; bool ok; orientation_homography(start_quad, reverse, start, h, ok); if (!ok) continue;
                const KeyFit fit = scalable_skeleton_fit(gray, width, height, stride, h);
                start_has_skeleton = fit.valid && fit.evidence >= kDenseKeyGate && fit.contrast >= kMinKeyContrast;
            }
            if (!start_has_skeleton) { std::copy(start_quad, start_quad + 8, out_quad); return 0.0; }
        }
        // Profile 3 registers on a handful of fine key samples and is
        // sensitive to where the search starts, so it is refined from both
        // starts and the better key fit is kept.
        if (fitted && !refine_legacy_start() && field == CKEY_FIELD_EXTENDED) {
            double legacy[8]; refine_legacy_start() = true;
            const double legacy_score = ckey_refine_quad_gray8(gray, width, height, stride, border_quad, field, legacy);
            refine_legacy_start() = false;
            alternative_score = legacy_score; std::copy(legacy, legacy + 8, alternative_quad);
        }
    }
    const double *const origin = start_quad;
    double cx = 0, cy = 0; for (int i = 0; i < 4; ++i) { cx += origin[2 * i] / 4; cy += origin[2 * i + 1] / 4; }
    const double side = (std::hypot(origin[2] - origin[0], origin[3] - origin[1]) +
                         std::hypot(origin[6] - origin[0], origin[7] - origin[1])) / 2;
    const auto similar = [&](double scale, double angle, double ox, double oy, double q[8]) {
        const double c = std::cos(angle), s = std::sin(angle);
        for (int i = 0; i < 4; ++i) {
            const double dx = origin[2 * i] - cx, dy = origin[2 * i + 1] - cy;
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
    double scale = start_scale, angle = 0, ox = 0, oy = 0;
    double best[8]; similar(scale, angle, ox, oy, best);
    double scale_step = 0.03, angle_step = 2.0 * kTau / 360.0, offset_step = side * 0.02;
    double best_score = objective(best, offset_step);
    // Profile 0 with the frame already fitted needs no similarity or corner search.
    const bool skip_search = field == CKEY_FIELD_SCALABLE && start_has_skeleton;
    for (int round = 0; round < 6 && !skip_search; ++round) {
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
    for (double corner_step = side * 0.02; corner_step > 0.3 && !skip_search; corner_step /= 2) {
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
        // Profile 0: place the frame by its edges directly (wide search, then narrow).
        // The first pass also measures the lens from the sides' bow; the rest
        // of this call then works through that lens.
        if (fit_frame_edges(gray, width, height, stride, best, 0.7, true)) fit_frame_edges(gray, width, height, stride, best, 0.25);
        best_score = objective(best, 1.0);
    }
    if (field == CKEY_FIELD_SCALABLE) {
        // Profile 0: the skeleton aligned the field; now pick the version whose
        // keys fit best and fine-tune against them. They sit at the version's
        // own module and slot pitch, so they pin the scale far more tightly
        // than the frame and finders can at high densities.
        double version_score = 0; int family = 0;
        const int v = best_scalable_version(gray, width, height, stride, best, version_score, &family);
        if (v > 0) {
            // The keys named the version; register on its module lattice,
            // within half a module of the frame fit.
            const ScalableLayout &L = scalable_family_layout(v, family);
            // Fine-tune against the version's keys (they sit at its own module
            // and slot pitch), each corner held within two modules of the
            // frame fit and the passes capped, so this can neither wander
            // nor run on.
            {
                const double pitch_px = side / L.grid; double anchor[8]; std::copy(best, best + 8, anchor);
                double score = scalable_version_score(L, gray, width, height, stride, best);
                for (double step = pitch_px * 0.5; step > pitch_px * 0.02; step /= 2) {
                    for (int pass = 0; pass < 6; ++pass) {
                        bool improved = false;
                        for (int corner = 0; corner < 4; ++corner) for (const auto &[dx, dy] : {std::pair{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}}) {
                            double q[8]; std::copy(best, best + 8, q);
                            q[2 * corner] += dx * step; q[2 * corner + 1] += dy * step;
                            if (std::hypot(q[2 * corner] - anchor[2 * corner], q[2 * corner + 1] - anchor[2 * corner + 1]) > 2.0 * pitch_px) continue;
                            const double candidate = scalable_version_score(L, gray, width, height, stride, q);
                            if (candidate > score) { score = candidate; std::copy(q, q + 8, best); improved = true; }
                        }
                        if (!improved) break;
                    }
                }
            }
            refine_on_lattice(L, gray, width, height, stride, best, 0.6);
            best_score = scalable_version_score(L, gray, width, height, stride, best);
        }
    }
    if (alternative_score > best_score) { std::copy(alternative_quad, alternative_quad + 8, out_quad); return alternative_score; }
    std::copy(best, best + 8, out_quad);
    return best_score;
}

}  // extern "C"
