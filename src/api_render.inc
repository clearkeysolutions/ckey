// Public C ABI: render functions. Included after core_internal.inc.

extern "C" {

ckey_status ckey_render_svg(const ckey_symbol *s, uint32_t scale, char *out,
                            size_t cap, size_t *required) {
    if (!s || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE || s->abi_version != CKEY_ABI_VERSION || s->side != kSide) return CKEY_INVALID_ARGUMENT;
    const auto &nodes = data_nodes();
    if (nodes.size() < kBits) return CKEY_INTERNAL_ERROR;
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    std::ostringstream c; c << std::fixed << std::setprecision(4);  // Cartesian payload layer
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 31 * scale << "\" height=\"" << 31 * scale << "\" viewBox=\"-1 -1 31 31\" role=\"img\" aria-label=\"CKey locator symbol\">"
      << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" fill=\"#ffffff\"/><g fill=\"#000000\" stroke=\"#000000\">"
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
          << "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"0.22\"/>";
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
      << kCenter + rr * std::sin(a1) << ' ' << kCenter - rr * std::cos(a1) << "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"" << ring_key_dot_diameter << "\" stroke-linecap=\"round\"/>";
    o << "</g><g class=\"ckey-data-polar\">";
    for (size_t i = 0; i < kBits; ++i) {
        const Node &n = nodes[i];
        if (n.dial) {
            const double a = kTau * n.slot / 72.0;
            // Every data-bearing dial slot is marked. A short ray encodes 0;
            // a long ray encodes 1. Both remain distinct from reserved keys.
            const double length = code_bit(s, i) ? 1.35 : 0.65;
            o << "<path d=\"M " << kCenter + 7.6 * std::sin(a) << ' ' << kCenter - 7.6 * std::cos(a) << " L " << kCenter + (7.6 + length) * std::sin(a) << ' ' << kCenter - (7.6 + length) * std::cos(a)
              << "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"0.15\" stroke-linecap=\"round\"/>";
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
      << "<rect x=\"-1\" y=\"-1\" width=\"31\" height=\"31\" fill=\"#ffffff\"/><g fill=\"#000000\" stroke=\"#000000\">"
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
      << "<rect x=\"-4\" y=\"-4\" width=\"124\" height=\"124\" fill=\"#ffffff\"/><g fill=\"#000000\" stroke=\"#000000\">"
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

void ckey_style_default(ckey_style *st) { if (st) fill_default_style(*st); }

ckey_status ckey_style_preset_fill(ckey_style *st, ckey_style_preset preset) {
    if (!st || (preset != CKEY_PRESET_PLAIN && preset != CKEY_PRESET_CONCEPT)) return CKEY_INVALID_ARGUMENT;
    ckey_style_default(st);
    if (preset == CKEY_PRESET_CONCEPT) {
        st->background = {0x14, 0x16, 0x1e}; st->background_edge = {0x07, 0x08, 0x0c};
        st->ink = {0xf6, 0xee, 0xe0}; st->ink_shade = {0xc8, 0xb6, 0x9a};
        st->accents[0] = {0xa9, 0xb8, 0x96}; st->accents[1] = {0x9e, 0xa1, 0xa6}; st->accent_count = 2; st->accent_percent = 18;
        st->bevel = 0.6; st->shadow = 0.5; st->texture = 0.4;
    }
    return CKEY_OK;
}

double ckey_scalable_logo_max_radius(uint32_t version, ckey_scalable_layout layout) {
    return version >= 1 && version <= CKEY_SCALABLE_VERSION_COUNT && static_cast<int>(layout) >= 0 && static_cast<int>(layout) < kScalableLayouts
        ? logo_max_radius(scalable_layout(static_cast<int>(version), static_cast<int>(layout))) : -1.0;
}

double ckey_scalable_logo_load(const ckey_scalable_symbol *s, double radius) {
    if (!s || s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT || s->ecc_level >= kScalableLevels || s->layout >= kScalableLayouts || !std::isfinite(radius) || radius < 0) return -1.0;
    return logo_load(scalable_layout(static_cast<int>(s->version), static_cast<int>(s->layout)), static_cast<int>(s->ecc_level), radius);
}

ckey_status ckey_check_scalable_style(const ckey_scalable_symbol *s, const ckey_style *st, ckey_diagnostics *diag) {
    if (!s || !st || s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT || s->ecc_level >= kScalableLevels || s->layout >= kScalableLayouts) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "style check needs a Profile 0 symbol and a style"); return CKEY_INVALID_ARGUMENT;
    }
    double contrast = 0, load = 0;
    const ScalableLayout &L = scalable_layout(static_cast<int>(s->version), static_cast<int>(s->layout));
    const std::string structural = structural_problem(L, *st);
    if (!structural.empty()) { set_status(diag, CKEY_INVALID_ARGUMENT, structural.c_str()); return CKEY_INVALID_ARGUMENT; }
    ckey_style eff = *st; uint32_t adjusted = 0;
    if (st->policy == CKEY_POLICY_ADAPTIVE && !safety_problem(L, *s, eff, {}, contrast, load).empty()) adjusted = adapt_style(L, *s, eff, {});
    std::string advice;
    const std::string problem = safety_problem(L, *s, eff, {}, contrast, load, &advice);
    char message[96];
    if (!problem.empty()) {
        if (st->policy != CKEY_POLICY_OVERRIDE) { set_status(diag, CKEY_INVALID_ARGUMENT, problem.c_str()); return CKEY_INVALID_ARGUMENT; }
        std::snprintf(message, sizeof(message), "override, NOT scan-safe: %s", problem.c_str());
        set_status(diag, CKEY_OK, message);
        return CKEY_OK;
    }
    if (!advice.empty()) std::snprintf(message, sizeof(message), "style OK%s, but %s; certify it to check that it scans", adjusted ? " after adaptive changes" : "", advice.c_str());
    else std::snprintf(message, sizeof(message), "style OK%s: contrast >= %.1f:1; logo uses %.0f%% of block correction",
                  adjusted ? " after adaptive changes" : "", contrast, 100 * load);
    set_status(diag, CKEY_OK, message);
    return CKEY_OK;
}

ckey_status ckey_render_scalable_svg(const ckey_scalable_symbol *s, uint32_t scale, char *out, size_t cap, size_t *required) {
    ckey_style st; ckey_style_default(&st);
    return ckey_render_scalable_styled_svg(s, &st, scale, out, cap, required);
}

ckey_status ckey_render_scalable_styled_svg(const ckey_scalable_symbol *s, const ckey_style *style_in, uint32_t scale,
                                            char *out, size_t cap, size_t *required) {
    if (!s || !style_in || !required || scale == 0 || scale > CKEY_MAX_RENDER_SCALE || s->abi_version != CKEY_ABI_VERSION ||
        s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT || s->layout >= kScalableLayouts) return CKEY_INVALID_ARGUMENT;
    const ScalableLayout &L = scalable_layout(static_cast<int>(s->version), static_cast<int>(s->layout));
    if (s->codeword_length != L.codeword_bytes) return CKEY_INVALID_ARGUMENT;
    const ckey_style eff = effective_style(L, *s, *style_in);
    const ckey_style *st = &eff;
    double contrast = 0, load = 0;
    if (!check_style(L, *s, eff, contrast, load).empty()) return CKEY_INVALID_ARGUMENT;
    const double p = L.pitch, dot = L.dot;
    const std::string dot_stroke = fixed4(2 * dot), cap_style = st->rays == CKEY_RAY_FLAT ? "butt" : "round";
    const bool sheen = !same_colour(st->ink, st->ink_shade), vignette = !same_colour(st->background, st->background_edge);
    const bool depth = st->bevel > 0 || st->shadow > 0;
    const std::string ink = sheen ? "url(#ckey-ink)" : hex_colour(st->ink);
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    std::ostringstream c; c << std::fixed << std::setprecision(4);
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << 31 * scale << "\" height=\"" << 31 * scale << "\" viewBox=\"-1 -1 31 31\" role=\"img\" aria-label=\"CKey scalable symbol profile 0" << (L.family == 1 ? ".1" : L.family == 2 ? ".2" : "") << " version " << s->version << "\">";
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
        const double a = kTau * slot / L.dial_slots, r0 = kScalableDialInner, r1 = r0 + L.key_ray() * fraction;
        o << "<path d=\"M " << kCenter + r0 * std::sin(a) << ' ' << kCenter - r0 * std::cos(a) << " L " << kCenter + r1 * std::sin(a) << ' ' << kCenter - r1 * std::cos(a)
          << "\" fill=\"none\" stroke-width=\"" << 0.44 * p << "\" stroke-linecap=\"" << cap_style << "\"/>";
    }
    // The Ring Key keeps its dot/gap/run pattern in every style: it is a key.
    const double rk = L.ring_radius(L.rings - 1); const int k = L.ring_key_slot(); const double SK = L.ring_slots.back();
    for (int slot : L.ring_key_dots()) { const double a = kTau * (k + slot) / SK; svg_circle(o, kCenter + rk * std::sin(a), kCenter - rk * std::cos(a), dot); }
    svg_ring_run(o, kCenter, rk, k + L.ring_key_run().first, k + L.ring_key_run().second, SK, dot, dot_stroke.c_str());
    o << "</g><g class=\"ckey-data-polar\">";
    const size_t stride = static_cast<size_t>(L.ring_stride);
    std::vector<uint8_t> ring_bits(static_cast<size_t>(L.rings) * stride, 0);
    std::vector<int> ring_accent(static_cast<size_t>(L.rings) * stride, -1);
    for (size_t i = 0; i < L.nodes.size(); ++i) {
        const auto &n = L.nodes[i]; const bool bit = scalable_site_value(L, s, i);
        const int accent = accent_of(n.id, *st);
        if (n.kind == 0) { if (bit) emit_module(c, n.x, n.y, p, st->modules, accent >= 0 ? hex_colour(st->accents[accent]) : ""); }
        else if (n.kind == 1) {
            const double a = kTau * n.slot / L.dial_slots, r0 = kScalableDialInner, r1 = r0 + (bit ? L.long_ray() : L.short_ray());
            o << "<path d=\"M " << kCenter + r0 * std::sin(a) << ' ' << kCenter - r0 * std::cos(a) << " L " << kCenter + r1 * std::sin(a) << ' ' << kCenter - r1 * std::cos(a)
              << "\" fill=\"none\" stroke-width=\"" << L.ray_width << "\" stroke-linecap=\"" << cap_style << "\"/>";
        } else { ring_bits[static_cast<size_t>(n.ring) * stride + n.slot] = bit; ring_accent[static_cast<size_t>(n.ring) * stride + n.slot] = accent; }
    }
    for (int ring = 0; ring < L.rings; ++ring) {
        const double radius = L.ring_radius(ring); const int S = L.ring_slots[static_cast<size_t>(ring)];
        for (int slot = 0; slot < S;) {
            const size_t at = static_cast<size_t>(ring) * stride + slot;
            if (!ring_bits[at]) { ++slot; continue; }
            int end = slot + 1;
            if (st->rings == CKEY_RING_JOINED) while (end < S && ring_bits[static_cast<size_t>(ring) * stride + end]) ++end;
            const int accent = ring_accent[at];
            const std::string colour = accent >= 0 ? hex_colour(st->accents[accent]) : "";
            if (end - slot == 1) {
                const double a = kTau * slot / S;
                o << "<circle cx=\"" << kCenter + radius * std::sin(a) << "\" cy=\"" << kCenter - radius * std::cos(a) << "\" r=\"" << dot << "\" stroke=\"none\""
                  << (colour.empty() ? "" : " fill=\"" + colour + "\"") << "/>";
            } else if (colour.empty()) svg_ring_run(o, kCenter, radius, slot - .5, end - .5, S, dot, dot_stroke.c_str());
            else { o << "<g stroke=\"" << colour << "\">"; svg_ring_run(o, kCenter, radius, slot - .5, end - .5, S, dot, dot_stroke.c_str()); o << "</g>"; }
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

ckey_status ckey_render_scalable_print(const ckey_scalable_symbol *s, const ckey_style *style, double width_in, double dpi,
                                       uint8_t *out, size_t cap, size_t *out_side) {
    if (!s || !out_side || !std::isfinite(width_in) || !std::isfinite(dpi) || width_in <= 0 || dpi < 72 || dpi > 4800 || width_in * dpi > 20000)
        return CKEY_INVALID_ARGUMENT;
    ckey_style st; ckey_style_default(&st);
    if (style) { st.modules = style->modules; st.rings = style->rings; st.rays = style->rays; }  // shapes only
    const int side = std::max(16, static_cast<int>(std::lround(width_in * dpi)));
    if (side > 16384) return CKEY_INVALID_ARGUMENT;
    // Direct geometry -> printer dots: black where at least half of a 3x3
    // sample grid is ink (no SVG in this path; specification §2.2).
    ckey_raster_options o; ckey_raster_options_default(&o);
    o.size_px = static_cast<uint32_t>(side); o.policy = CKEY_RASTER_BINARY; o.format = CKEY_PIXELS_GRAY8;
    o.supersample = 3; o.materials = 0;
    size_t needed = 0;
    const ckey_status probe = ckey_render_scalable_raster(s, &st, &o, nullptr, 0, &needed);
    if (probe != CKEY_INSUFFICIENT_BUFFER) return probe == CKEY_OK ? CKEY_INTERNAL_ERROR : probe;
    *out_side = static_cast<size_t>(side);
    if (!out || cap < needed) return CKEY_INSUFFICIENT_BUFFER;
    return ckey_render_scalable_raster(s, &st, &o, out, cap, &needed);
}

}  // extern "C"

#include "scalable_render.inc"
