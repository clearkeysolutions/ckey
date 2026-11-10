// Public C ABI: scan functions. Included after core_internal.inc.

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

ckey_status ckey_decode_scalable_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride, const double quad_xy[8],
                                       uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len, uint32_t *out_version,
                                       uint32_t *out_layout, ckey_diagnostics *diag) {
    if (!out_payload || !out_payload_len || !valid_image_args(gray, width, height, stride, quad_xy)) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 0 image, quadrilateral, or output buffer"); return CKEY_INVALID_ARGUMENT;
    }
    // 1. The skeleton gates orientation. 2. Each version's keys are tested at
    //    that orientation; only agreeing versions are fully sampled, best
    //    first. 3. RS, the frame's version byte, and CRC-32C decide.
    struct Attempt { double score; int version, family; double h[8]; };
    std::vector<Attempt> attempts;
    for (int reverse = 0; reverse < 2; ++reverse) for (int start = 0; start < 4; ++start) {
        double h[8]; bool ok; orientation_homography(quad_xy, reverse, start, h, ok); if (!ok) continue;
        const KeyFit skeleton = scalable_skeleton_fit(gray, width, height, stride, h);
        if (!skeleton.valid || skeleton.evidence < kDenseKeyGate || skeleton.contrast < 14.0) continue;
        for (int f = 0; f < kScalableFamilies; ++f) for (int v = 1; v <= kScalableVersions; ++v) {
            const KeyFit keys = scalable_version_fit(scalable_layout(v, f), gray, width, height, stride, h);
            if (!keys.valid || keys.evidence < 0.85) continue;
            Attempt a{keys.contrast * keys.evidence * keys.evidence, v, f, {}}; std::copy(h, h + 8, a.h);
            attempts.push_back(a);
        }
    }
    std::sort(attempts.begin(), attempts.end(), [](const Attempt &a, const Attempt &b) { return a.score > b.score; });
    if (attempts.size() > 8) attempts.resize(8);
    std::vector<uint8_t> candidate(65535);
    for (const auto &attempt : attempts) {
        const ScalableLayout &L = scalable_layout(attempt.version, attempt.family); const double *h = attempt.h; const double p = L.pitch;
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
                           : n.kind == 1 ? polar_sample_slots(gray, width, height, stride, h, kCenter, kScalableDialInner + L.dial_sample, n.slot, L.dial_slots, 0.04 * p)
                                         : gray_sample(gray, width, height, stride, h, n.x, n.y, 0.25 * L.dot);
            if (v < 0) { usable = false; break; }
            level[i] = (v - empty_mean) / (mark_mean - empty_mean);
            classes[n.kind].push_back(level[i]);
        }
        if (!usable) continue;
        const std::array<double, 3> cut = {class_threshold(classes[0]), class_threshold(classes[1]), class_threshold(classes[2])};
        std::vector<uint8_t> printed(L.nodes.size());
        for (size_t i = 0; i < L.nodes.size(); ++i) printed[i] = i < bits ? (level[i] > cut[L.nodes[i].kind]) : L.white[i];
        refine_ring_context_rings(L.ring_grid, L.ring_slots, L.ring_stride, bits, L.white, level, cut[2], printed);
        std::vector<uint8_t> cw(L.codeword_bytes, 0);
        for (size_t i = 0; i < bits; ++i) if ((printed[i] != 0) != (L.white[i] != 0)) cw[i / 8] |= static_cast<uint8_t>(1u << (7 - i % 8));
        size_t length = 0; ckey_diagnostics local{};
        if (ckey_decode_scalable(cw.data(), cw.size(), static_cast<uint32_t>(attempt.version), static_cast<ckey_scalable_layout>(attempt.family),
                                 candidate.data(), candidate.size(), &length, &local) != CKEY_OK) continue;
        if (out_capacity < length) { set_status(diag, CKEY_INSUFFICIENT_BUFFER, "Profile 0 output buffer is too small"); return CKEY_INSUFFICIENT_BUFFER; }
        std::memcpy(out_payload, candidate.data(), length); *out_payload_len = length;
        if (out_version) *out_version = static_cast<uint32_t>(attempt.version);
        if (out_layout) *out_layout = static_cast<uint32_t>(attempt.family);
        set_status(diag, CKEY_OK, local.message); if (diag) diag->corrected_symbols = local.corrected_symbols;
        return CKEY_OK;
    }
    set_status(diag, CKEY_UNCORRECTABLE, attempts.empty() ? "no Profile 0 version's keys matched" : "no Profile 0 version passed Reed-Solomon and CRC");
    return CKEY_UNCORRECTABLE;
}

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
        double version_score = 0; int family = 0;
        const int v = best_scalable_version(gray, width, height, stride, best, version_score, &family);
        if (v > 0) {
            const ScalableLayout &L = scalable_layout(v, family);
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
