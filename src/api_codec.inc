// Public C ABI: codec functions. Included after core_internal.inc.

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

uint32_t ckey_scalable_modules_across(uint32_t version) {
    return version >= 1 && version <= CKEY_SCALABLE_VERSION_COUNT ? static_cast<uint32_t>(kScalableGrid[version - 1]) : 0u;
}

// CKEY_LAYOUT_AUTO: Standard for small versions, Standard arc (grouped) from v8.
int scalable_auto_layout(int v) { return v < static_cast<int>(CKEY_AUTO_LAYOUT_ARC_FROM_VERSION) ? CKEY_LAYOUT_STANDARD : CKEY_LAYOUT_STANDARD_ARC_GROUPED; }

size_t ckey_scalable_capacity(uint32_t version, ckey_ecc_level level, ckey_scalable_layout layout) {
    const int l = layout == CKEY_LAYOUT_AUTO ? scalable_auto_layout(static_cast<int>(version)) : static_cast<int>(layout);
    return scalable_capacity(static_cast<int>(version), static_cast<int>(level), l);
}

ckey_status ckey_encode_scalable(const uint8_t *payload, size_t payload_len, uint32_t version, ckey_ecc_level level,
                                 ckey_scalable_layout layout, ckey_scalable_symbol *out, ckey_diagnostics *diag) {
    const bool automatic = layout == CKEY_LAYOUT_AUTO;
    int family = automatic ? scalable_auto_layout(static_cast<int>(version)) : static_cast<int>(layout);
    if ((!payload && payload_len) || !out || static_cast<int>(level) < 0 || static_cast<int>(level) >= kScalableLevels || version > CKEY_SCALABLE_VERSION_COUNT ||
        family < 0 || family >= kScalableLayouts) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 0 payload, version, level, or output"); return CKEY_INVALID_ARGUMENT;
    }
    int v = static_cast<int>(version);
    if (v == 0) {
        for (v = 1; v <= kScalableVersions; ++v) {
            if (automatic) family = scalable_auto_layout(v);
            if (scalable_capacity(v, level, family) >= payload_len) break;
        }
    }
    if (v > kScalableVersions || scalable_capacity(v, level, family) < payload_len) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "payload exceeds the selected Profile 0 version and level"); return CKEY_INVALID_ARGUMENT;
    }
    const ScalableLayout &L = scalable_layout(v, family);
    if (L.codeword_bytes > CKEY_SCALABLE_MAX_CODEWORD_BYTES) { set_status(diag, CKEY_INTERNAL_ERROR, "Profile 0 layout exceeds codeword storage"); return CKEY_INTERNAL_ERROR; }
    const auto blocks = scalable_blocks(L.codeword_bytes, level);
    std::vector<uint8_t> framed(scalable_data_bytes(L.codeword_bytes, level), 0);
    framed[0] = 0xc7; framed[1] = 0x4b; framed[2] = L.format; framed[3] = 0;
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
    out->layout = static_cast<uint32_t>(family);
    out->payload_length = static_cast<uint32_t>(payload_len); out->codeword_length = static_cast<uint32_t>(L.codeword_bytes);
    // Byte-interleave the blocks so local damage spreads across all of them.
    size_t pos = 0;
    for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) out->codeword[pos++] = coded[b][j];
    char message[96]; std::snprintf(message, sizeof(message), "encoded Profile 0%s v%d level %c; %zu RS blocks%s", scalable_family_label(L.family), v, kScalableLevelNames[level], blocks.size(), L.grouped ? ", grouped" : "");
    set_status(diag, CKEY_OK, message);
    return CKEY_OK;
}

namespace {
// Frame check shared by every decode path: header, length, CRC-32C, padding.
bool scalable_frame_ok(const ScalableLayout &L, const std::vector<uint8_t> &framed, uint32_t version, int level, size_t &length) {
    if (framed.size() < kScalableOverheadBytes) return false;
    if (framed[0] != 0xc7 || framed[1] != 0x4b || framed[2] != L.format || framed[3] != 0 || framed[4] != version || framed[5] != level) return false;
    length = (static_cast<size_t>(framed[6]) << 8) | framed[7];
    if (kScalableOverheadBytes + length > framed.size()) return false;
    const size_t crc_index = kScalableHeaderBytes + length;
    uint32_t expected = 0; for (int b = 0; b < 4; ++b) expected = (expected << 8) | framed[crc_index + b];
    return crc32c(framed.data(), crc_index) == expected && padding_matches(framed.data(), crc_index + 4, framed.size(), crc_index + 4);
}

// Erasure steps tried per block after plain correction fails, as fractions
// of the block's parity (generalised minimum distance, capped).
constexpr std::array<double, 5> kErasureSteps = {0.25, 0.5, 0.7, 0.85, 1.0};
}  // namespace

ckey_status ckey_decode_scalable_soft(const uint8_t *codeword, const uint8_t *reliability, size_t codeword_len, uint32_t version,
                                      ckey_scalable_layout layout, uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len,
                                      ckey_diagnostics *diag) {
    const int family = static_cast<int>(layout);
    if (!codeword || !out_payload_len || version < 1 || version > CKEY_SCALABLE_VERSION_COUNT || family < 0 || family >= kScalableLayouts) {
        set_status(diag, CKEY_INVALID_ARGUMENT, "invalid Profile 0 codeword, version, or layout"); return CKEY_INVALID_ARGUMENT;
    }
    const ScalableLayout &L = scalable_layout(static_cast<int>(version), family);
    if (codeword_len != L.codeword_bytes) { set_status(diag, CKEY_INVALID_ARGUMENT, "codeword length does not match the Profile 0 version"); return CKEY_INVALID_ARGUMENT; }
    const size_t block_count = (L.codeword_bytes + 254) / 255;
    // The header is systematic: byte 5 of block 0 is the level. If the raw
    // header looks intact, try its level first.
    std::array<int, kScalableLevels> order{}; std::iota(order.begin(), order.end(), 0);
    if (L.codeword_bytes >= 6 * block_count) {
        const int raw = codeword[5 * block_count];
        if (raw < kScalableLevels && codeword[0] == 0xc7 && codeword[block_count] == 0x4b) std::rotate(order.begin(), order.begin() + raw, order.begin() + raw + 1);
    }
    const auto finish = [&](const std::vector<uint8_t> &framed, int level, uint32_t corrections, uint32_t erased, size_t length) {
        if (out_capacity < length || (!out_payload && length)) { set_status(diag, CKEY_INSUFFICIENT_BUFFER, "Profile 0 payload exceeds output capacity"); return CKEY_INSUFFICIENT_BUFFER; }
        if (length) std::memcpy(out_payload, framed.data() + kScalableHeaderBytes, length);
        *out_payload_len = length;
        char message[96]; std::snprintf(message, sizeof(message), "decoded Profile 0%s v%u level %c%s", scalable_family_label(L.family), version, kScalableLevelNames[level], L.grouped ? ", grouped" : "");
        set_status(diag, CKEY_OK, message); if (diag) { diag->corrected_symbols = corrections; diag->erasures_used = erased; }
        return CKEY_OK;
    };
    for (int pass = 0; pass < (reliability ? 2 : 1); ++pass) for (int level : order) {
        const auto blocks = scalable_blocks(L.codeword_bytes, level);
        std::vector<std::vector<uint8_t>> work(blocks.size()); std::vector<std::vector<size_t>> where(blocks.size());
        for (size_t b = 0; b < blocks.size(); ++b) { work[b].resize(blocks[b].n); where[b].resize(blocks[b].n); }
        size_t pos = 0;
        for (size_t j = 0; j < blocks.front().n; ++j) for (size_t b = 0; b < blocks.size(); ++b) if (j < blocks[b].n) { where[b][j] = pos; work[b][j] = codeword[pos++]; }
        std::vector<uint8_t> framed; uint32_t corrections = 0, erased = 0; bool ok = true;
        for (size_t b = 0; b < blocks.size() && ok; ++b) {
            const size_t parity = blocks[b].n - blocks[b].k;
            uint32_t fixed = 0;
            if (pass == 0) ok = rs_correct_generic(work[b], parity, fixed);
            else {
                // Least reliable bytes first; ties keep codeword order.
                std::vector<size_t> rank(blocks[b].n); std::iota(rank.begin(), rank.end(), size_t{0});
                std::stable_sort(rank.begin(), rank.end(), [&](size_t l, size_t r) { return reliability[where[b][l]] < reliability[where[b][r]]; });
                const size_t margin = parity >= 8 ? 2 : 0;
                const std::vector<uint8_t> original = work[b];
                ok = rs_correct_generic(work[b], parity, fixed);
                size_t previous = 0;
                for (size_t s = 0; s < kErasureSteps.size() && !ok; ++s) {
                    const size_t count = std::min(parity - margin, 2 * static_cast<size_t>(kErasureSteps[s] * static_cast<double>(parity) / 2.0));
                    if (count <= previous) continue;
                    previous = count;
                    work[b] = original;
                    const std::vector<size_t> erasures(rank.begin(), rank.begin() + static_cast<std::ptrdiff_t>(count));
                    ok = rs_correct_erasures(work[b], parity, erasures, margin, fixed);
                    if (ok) erased += static_cast<uint32_t>(count);
                }
                // Block 0 carries the header: a wrong level stops here.
                if (ok && b == 0 && blocks[0].k >= 6 && (work[0][0] != 0xc7 || work[0][1] != 0x4b || work[0][2] != L.format || work[0][4] != version || work[0][5] != level)) ok = false;
            }
            corrections += fixed;
            framed.insert(framed.end(), work[b].begin(), work[b].begin() + static_cast<std::ptrdiff_t>(blocks[b].k));
        }
        size_t length = 0;
        if (ok && scalable_frame_ok(L, framed, version, level, length)) return finish(framed, level, corrections, erased, length);
    }
    set_status(diag, CKEY_UNCORRECTABLE, "no Profile 0 level passed Reed-Solomon, frame, and CRC checks"); return CKEY_UNCORRECTABLE;
}

ckey_status ckey_decode_scalable(const uint8_t *codeword, size_t codeword_len, uint32_t version, ckey_scalable_layout layout,
                                 uint8_t *out_payload, size_t out_capacity, size_t *out_payload_len, ckey_diagnostics *diag) {
    return ckey_decode_scalable_soft(codeword, nullptr, codeword_len, version, layout, out_payload, out_capacity, out_payload_len, diag);
}

}  // extern "C"
