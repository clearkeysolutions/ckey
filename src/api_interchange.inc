// Public C ABI: .ckey interchange (spec/CKey-Interchange-v1.md). Part of
// ckey_codec: it needs only framing and the codeword decoder.

namespace {

// Record types. Bit 15 set = optional (a reader may skip it if unknown).
constexpr uint16_t kRecordCodeword = 0x0001, kRecordStyle = 0x8002, kRecordLogo = 0x8003;
constexpr size_t kInterchangeHeader = 12, kStyleRecordBytes = 64;

void put16(std::vector<uint8_t> &o, uint16_t v) { o.push_back(static_cast<uint8_t>(v)); o.push_back(static_cast<uint8_t>(v >> 8)); }
void put32(std::vector<uint8_t> &o, uint32_t v) { for (int s = 0; s < 32; s += 8) o.push_back(static_cast<uint8_t>(v >> s)); }
void put_unit(std::vector<uint8_t> &o, double v) { put16(o, static_cast<uint16_t>(std::lround(std::clamp(v, 0.0, 1.0) * 65535))); }
uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }

// Fixed 64-byte style record: colours, shapes, effects (16-bit fixed point),
// logo radius (1/1000 unit) and flags. Logos travel as their own record.
std::vector<uint8_t> encode_style(const ckey_style &st) {
    std::vector<uint8_t> o;
    for (const ckey_rgb &c : {st.background, st.background_edge, st.ink, st.ink_shade, st.accents[0], st.accents[1], st.accents[2], st.accents[3]}) { o.push_back(c.r); o.push_back(c.g); o.push_back(c.b); }
    o.push_back(static_cast<uint8_t>(st.accent_count)); o.push_back(static_cast<uint8_t>(st.accent_percent));
    o.push_back(static_cast<uint8_t>(st.modules)); o.push_back(static_cast<uint8_t>(st.rings)); o.push_back(static_cast<uint8_t>(st.rays));
    o.push_back(static_cast<uint8_t>(st.policy)); o.push_back(st.logo_plate ? 1 : 0);
    put_unit(o, st.bevel); put_unit(o, st.shadow); put_unit(o, st.texture);
    put32(o, static_cast<uint32_t>(std::lround(std::clamp(st.logo_radius, 0.0, 1e6) * 1000)));
    while (o.size() < kStyleRecordBytes) o.push_back(0);
    return o;
}

bool decode_style(const uint8_t *p, ckey_style &st) {
    fill_default_style(st);
    ckey_rgb *colours[8] = {&st.background, &st.background_edge, &st.ink, &st.ink_shade, &st.accents[0], &st.accents[1], &st.accents[2], &st.accents[3]};
    for (int i = 0; i < 8; ++i) *colours[i] = {p[3 * i], p[3 * i + 1], p[3 * i + 2]};
    const uint8_t *q = p + 24;
    st.accent_count = q[0]; st.accent_percent = q[1];
    st.modules = static_cast<ckey_module_shape>(q[2]); st.rings = static_cast<ckey_ring_style>(q[3]); st.rays = static_cast<ckey_ray_cap>(q[4]);
    st.policy = q[5]; st.logo_plate = q[6];
    st.bevel = get16(q + 7) / 65535.0; st.shadow = get16(q + 9) / 65535.0; st.texture = get16(q + 11) / 65535.0;
    st.logo_radius = get32(q + 13) / 1000.0;
    for (size_t i = 24 + 17; i < kStyleRecordBytes; ++i) if (p[i]) return false;  // reserved bytes must be zero
    return st.accent_count <= 4 && st.accent_percent <= 100 && q[2] <= CKEY_MODULE_DIAMOND && q[3] <= CKEY_RING_DOTS &&
           q[4] <= CKEY_RAY_FLAT && q[5] <= CKEY_POLICY_OVERRIDE && q[6] <= 1 && st.logo_radius <= 14.5;
}

}  // namespace

extern "C" {

ckey_status ckey_write_interchange(const ckey_scalable_symbol *s, const ckey_style *style, uint8_t *out, size_t cap, size_t *required) {
    if (!s || !required || s->abi_version != CKEY_ABI_VERSION || s->version < 1 || s->version > CKEY_SCALABLE_VERSION_COUNT ||
        s->ecc_level >= static_cast<uint32_t>(kScalableLevels) || s->layout >= static_cast<uint32_t>(kScalableLayouts)) return CKEY_INVALID_ARGUMENT;
    const ScalableLayout &L = scalable_layout(static_cast<int>(s->version), static_cast<int>(s->layout));
    if (s->codeword_length != L.codeword_bytes) return CKEY_INVALID_ARGUMENT;
    std::vector<uint8_t> o = {'C', 'K', 'E', 'Y', static_cast<uint8_t>(CKEY_INTERCHANGE_VERSION), 0, 0, 0,
                              L.format, static_cast<uint8_t>(s->version), static_cast<uint8_t>(s->ecc_level), static_cast<uint8_t>(s->layout)};
    put16(o, kRecordCodeword); put32(o, static_cast<uint32_t>(s->codeword_length));
    o.insert(o.end(), s->codeword, s->codeword + s->codeword_length);
    if (style) {
        if (!structural_problem(L, *style).empty()) return CKEY_INVALID_ARGUMENT;
        const auto rec = encode_style(*style);
        put16(o, kRecordStyle); put32(o, static_cast<uint32_t>(rec.size())); o.insert(o.end(), rec.begin(), rec.end());
        if (style->logo_href && *style->logo_href) {
            const size_t n = std::strlen(style->logo_href);
            if (n > 32768) return CKEY_INVALID_ARGUMENT;
            put16(o, kRecordLogo); put32(o, static_cast<uint32_t>(n)); o.insert(o.end(), style->logo_href, style->logo_href + n);
        }
    }
    put32(o, crc32c(o.data(), o.size()));
    if (o.size() > CKEY_INTERCHANGE_MAX_BYTES) return CKEY_INVALID_ARGUMENT;
    *required = o.size();
    if (!out || cap < o.size()) return CKEY_INSUFFICIENT_BUFFER;
    std::memcpy(out, o.data(), o.size());
    return CKEY_OK;
}

ckey_status ckey_read_interchange(const uint8_t *data, size_t len, ckey_scalable_symbol *sym, ckey_style *out_style,
                                  char *logo_href, size_t logo_cap, ckey_diagnostics *diag) {
    const auto fail = [&](const char *why) { set_status(diag, CKEY_INVALID_ARGUMENT, why); return CKEY_INVALID_ARGUMENT; };
    if (!data || !sym) return fail(".ckey reader needs data and a symbol");
    if (len < kInterchangeHeader + 6 + 4 || len > CKEY_INTERCHANGE_MAX_BYTES) return fail(".ckey file is too short or too large");
    if (std::memcmp(data, "CKEY", 4) != 0) return fail("not a .ckey file");
    if (data[4] != CKEY_INTERCHANGE_VERSION) return fail("unsupported .ckey version");
    if (data[5] || data[6] || data[7]) return fail(".ckey reserved header bytes must be zero");
    if (crc32c(data, len - 4) != get32(data + len - 4)) return fail(".ckey CRC-32C mismatch");
    const uint8_t format = data[8], version = data[9], level = data[10], layout = data[11];
    if (version < 1 || version > CKEY_SCALABLE_VERSION_COUNT || level >= kScalableLevels || layout >= kScalableLayouts) return fail(".ckey symbol header out of range");
    const ScalableLayout &L = scalable_layout(version, layout);
    if (format != L.format) return fail(".ckey format byte does not match its layout");
    bool have_codeword = false, have_style = false;
    ckey_scalable_symbol s{}; s.abi_version = CKEY_ABI_VERSION; s.version = version; s.ecc_level = level; s.layout = layout;
    ckey_style st{}; std::string logo;
    size_t at = kInterchangeHeader; const size_t end = len - 4;
    while (at < end) {
        if (end - at < 6) return fail(".ckey record header truncated");
        const uint16_t type = get16(data + at); const uint32_t n = get32(data + at + 2); at += 6;
        if (n > end - at) return fail(".ckey record overruns the file");
        const uint8_t *v = data + at;
        if (type == kRecordCodeword) {
            if (have_codeword || n != L.codeword_bytes) return fail(".ckey codeword record is duplicated or has the wrong length");
            std::memcpy(s.codeword, v, n); s.codeword_length = n; have_codeword = true;
        } else if (type == kRecordStyle) {
            if (have_style || n != kStyleRecordBytes || !decode_style(v, st)) return fail(".ckey style record is invalid");
            have_style = true;
        } else if (type == kRecordLogo) {
            if (!have_style || !logo.empty() || n == 0 || n > 32768) return fail(".ckey logo record is invalid");
            for (uint32_t i = 0; i < n; ++i) if (v[i] < 0x20 || v[i] == 0x7f) return fail(".ckey logo reference has control characters");
            logo.assign(reinterpret_cast<const char *>(v), n);
        } else if (!(type & 0x8000u)) return fail(".ckey has an unknown required record");
        at += n;
    }
    if (!have_codeword) return fail(".ckey has no codeword");
    // The codeword must decode: a .ckey never carries a symbol a reader would reject.
    std::vector<uint8_t> payload(65535); size_t payload_len = 0; ckey_diagnostics d{};
    if (ckey_decode_scalable(s.codeword, s.codeword_length, version, static_cast<ckey_scalable_layout>(layout), payload.data(), payload.size(), &payload_len, &d) != CKEY_OK)
        return fail(".ckey codeword does not decode");
    // Same check as the encoder: the stored codeword must be the canonical,
    // error-free encoding (a corrected codeword would render differently).
    ckey_scalable_symbol again{}; ckey_diagnostics e{};
    if (ckey_encode_scalable(payload.data(), payload_len, version, static_cast<ckey_ecc_level>(level), static_cast<ckey_scalable_layout>(layout), &again, &e) != CKEY_OK ||
        again.codeword_length != s.codeword_length || std::memcmp(again.codeword, s.codeword, s.codeword_length) != 0)
        return fail(".ckey codeword is not a canonical encoding");
    *sym = s;
    if (out_style) {
        if (have_style) {
            st.logo_href = nullptr;
            if (!logo.empty() && logo_href && logo_cap > logo.size()) { std::memcpy(logo_href, logo.c_str(), logo.size() + 1); st.logo_href = logo_href; }
            *out_style = st;
        } else fill_default_style(*out_style);
    }
    set_status(diag, CKEY_OK, have_style ? "read .ckey symbol and style" : "read .ckey symbol");
    return CKEY_OK;
}

}  // extern "C"
