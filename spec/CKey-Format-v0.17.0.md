# CKey Format v0.17.0 — Profile 3 Capacity and ECC Tiers

Status: experimental profile. This revision emits `format=17` (`11`),
`profile=3`; decoders continue to accept Profile 3 Formats 0.10 and 0.16.
The optical geometry and fixed 2,550-byte interleaved codeword do not change.

## Frame and tiers

The frame is `C7 4B 11 03 TT LLhi LLlo payload CRC32C padding`. `TT` is the
one-based tier number (1–7). The CRC-32C covers magic through payload. The
frame is followed by deterministic visual fill through ten equal data blocks.
The fill is not encryption and is not relied on for integrity; RS protects it
and CRC validates the payload frame.

| Tier | Payload capacity | Data bytes per RS block | Parity bytes | Unknown byte errors correctable per block |
|---:|---:|---:|---:|---:|
| 1 | 256 | 27 | 228 | 114 |
| 2 | 384 | 40 | 215 | 107 |
| 3 | 512 | 53 | 202 | 101 |
| 4 | 768 | 78 | 177 | 88 |
| 5 | 1,024 | 104 | 151 | 75 |
| 6 | 1,536 | 155 | 100 | 50 |
| 7 | 2,048 | 206 | 49 | 24 |

Each block is a systematic RS(255,k) code over GF(256), primitive polynomial
`0x11D`, primitive element 2, roots `alpha^0` through `alpha^(254-k)`,
high-degree coefficient first, parity appended. The ten 255-byte blocks are
byte-interleaved in the public codeword. Higher tiers trade correction
strength for payload capacity while all tiers fill the same visual lattice.
The default encoder selects the smallest tier that fits; callers may request a
larger tier explicitly. The maximum remains 2,048 payload bytes.

## Deterministic visual fill

Let `frame_end` be the byte immediately after CRC-32C. Initialize a 32-bit
state to CRC-32C of the complete frame through that CRC. For each padding byte,
update `state += 0x9E3779B9` modulo 2^32, then apply:

1. `z = (state xor (state >> 16)) * 0x85EBCA6B` modulo 2^32
2. `z = (z xor (z >> 13)) * 0xC2B2AE35` modulo 2^32
3. `z = z xor (z >> 16)`
4. emit the low byte of `z`

The state is seeded from the frame, making fill deterministic. It decorrelates
unused capacity from the black/white appearance of zero-filled padding; it
provides no secrecy or additional error correction.

## Geometry and decoding

The v0.16 116-unit geometry, 232×232 Cartesian grid, 24 rings, 576 calibrated
ray bearings, and one inward-facing rounded arc per corner are unchanged.
Decoders first use the Grid Key to establish sampling scale, read the fixed
codeword lattice, try the declared tier's RS parameters, then validate tier,
length, deterministic fill, and CRC. Legacy v0.10/v0.16 frames use ten
RS(255,223) blocks. Camera image decoding and full-frame localization remain
experimental and require physical-capture validation.
