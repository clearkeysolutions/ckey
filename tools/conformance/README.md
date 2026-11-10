# Conformance vectors

`tests/vectors/conformance-v1.json` fixes inputs and expected outputs that every CKey implementation and SDK must reproduce exactly (specification §6, acceptance criterion 1). CTest runs `ckey-conformance --check` on every build.

Each vector gives an input: `profile` ("0" covers both Profile 0 layouts; "1", "2" and "3" are the legacy profiles), `version` (0 = automatic), `level` (0–5 = L/M/Q/H/U/X), `layout` (0 standard, 1 print, 2 standard-grouped, 3 print-grouped) and `payload_hex`.

It then lists the expected outputs:

| Key | Meaning |
|---|---|
| `version` | the version the encoder chose (Profile 0) |
| `codeword_hex` | the full codeword, for codewords up to 160 bytes |
| `codeword_fnv1a64` | FNV-1a 64 over the codeword bytes |
| `shape_count`, `geometry_fnv1a64` | the canonical geometry from `ckey_scalable_geometry` with the default style. For each shape, in order, the hash covers `kind`, `layer`, `paint` and `round_cap` as little-endian int32, then `p[0..4]` and `width` each as a little-endian int64 of `round(value × 10⁶)` |
| `ckey_file_fnv1a64` | FNV-1a 64 over the `.ckey` file written without a style |
| `raster_decodes` | `true` if a binary-policy GRAY8 raster at 8 px/module decodes back to the payload, version and layout |

FNV-1a 64 uses offset basis `0xcbf29ce484222325` and prime `0x100000001b3`.

The geometry hash rounds coordinates to 10⁻⁶ units, so it does not depend on the last bits of a platform's `sin` and `cos`. Pixel output is checked by behaviour (`raster_decodes`), not by hash, because antialiased edge pixels may legitimately differ by one level between math libraries.

To regenerate after an intentional, versioned change:

```sh
./build/ckey-conformance --write tests/vectors/conformance-v1.json
```

That is a format or geometry change: bump the format or interchange version and record it in the changelog.
