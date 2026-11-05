# CKey interchange file (`.ckey`), version 1

**Status:** draft, implemented in build 65 (`ckey_write_interchange`, `ckey_read_interchange`).

A `.ckey` file stores a Profile 0 or Profile 0.1 symbol and, optionally, its style. It exists so tools can hand a symbol to one another without re-encoding it or losing the version, level and style that were chosen.

It is **optional**. No renderer, reader or SDK needs it to draw or display a symbol, and in-memory rendering never touches it (specification §2.5).

**The payload is in clear.** The codeword contains the payload, protected by Reed–Solomon but not encrypted. A `.ckey` file is exactly as sensitive as its payload. There is no separate payload record, and no way to leave the payload out.

## Layout

All integers are little-endian. A file has a header, then records, then a CRC.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `CKEY` (43 4B 45 59) |
| 4 | 1 | interchange version, `1` |
| 5 | 3 | reserved, must be 0 |
| 8 | 1 | symbol format byte: `0x1B` (Format 0.27, standard layout) or `0x1C` (Format 0.28, print layout) |
| 9 | 1 | Profile 0 version, 1–31 |
| 10 | 1 | ECC level: 0 = L, 1 = M, 2 = Q, 3 = H |
| 11 | 1 | layout: 0 = standard, 1 = print. It must agree with the format byte. |
| 12 | … | records |
| end − 4 | 4 | CRC-32C (Castagnoli, reflected, initial value and final XOR `0xFFFFFFFF`) of every preceding byte |

Each record is a 16-bit type, a 32-bit length, then that many value bytes. **Bit 15 of the type marks an optional record:**

- A reader skips an optional record it does not know.
- A reader rejects the file if it meets a required record it does not know.

| Type | Name | Required | Value |
|---|---|---|---|
| `0x0001` | codeword | yes, exactly once | the symbol's codeword, exactly as long as that version and layout specify |
| `0x8002` | style | no, at most once | the 64-byte style record (below) |
| `0x8003` | logo | no; only after a style record | the logo reference (URL or `data:` URI) as UTF-8: 1–32,768 bytes, no control characters |

### Style record (64 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 24 | eight RGB triples: background, background edge, ink, ink shade, accents 0–3 |
| 24 | 1 | accent count, 0–4 |
| 25 | 1 | accent percent, 0–100 |
| 26 | 1 | module shape, 0–3 |
| 27 | 1 | ring style, 0–1 |
| 28 | 1 | ray cap, 0–1 |
| 29 | 1 | policy: 0 = strict, 1 = adaptive, 2 = override |
| 30 | 1 | logo plate, 0–1 |
| 31 | 2 | bevel, × 65535 |
| 33 | 2 | shadow, × 65535 |
| 35 | 2 | texture, × 65535 |
| 37 | 4 | logo radius in 1/1000 canonical unit, at most 14,500 |
| 41 | 23 | reserved, must be 0 |

## Reader rules

A conforming reader rejects a file in any of these cases:

1. **Size:** it is shorter than 22 bytes or longer than **65,536 bytes**.
2. **Header:** the magic, the version or the reserved bytes are wrong.
3. **Integrity:** the CRC-32C does not match.
4. **Symbol header:** a symbol header field is out of range, or the format byte does not match the layout.
5. **Records:**
   - a record overruns the file;
   - the codeword is missing or duplicated, or has the wrong length;
   - a style record is invalid, or its reserved bytes are not zero;
   - there is a logo record without a style record;
   - there is an unknown required record.
6. **Codeword:**
   - it fails Reed–Solomon or the CRC under the stated version, level and layout;
   - or it is not the canonical encoding of its own payload. This means re-encoding the decoded payload must give the same bytes.

Rule 6 means a `.ckey` file can never carry a damaged or non-canonical symbol that would render differently from a fresh encode.

The reference reader is bounds-checked and fuzzed in CI (`ckey_interchange`, with 20,000 mutated files under AddressSanitizer and UBSan).

## Not in version 1

- **Profiles 1–3:** they are legacy, and Profile 0 covers their densities.
- **Raster or vector artwork:** a `.ckey` file holds the symbol, not a picture of it.
- **Logo image bytes:** only a reference to the logo is stored.
- **Signatures:** a future optional record type can add them without breaking version 1 readers.
