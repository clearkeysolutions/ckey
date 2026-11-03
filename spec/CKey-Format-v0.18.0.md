# CKey Format v0.18.0 — Profile 2 Deterministic Visual Fill

Status: experimental profile. This revision emits `format=18` (`12`),
`profile=2`; decoders continue to accept legacy Profile 2 Formats 0.9 and 0.15.
The frame, geometry, and RS parameters otherwise remain those of Format 0.15.

Profile 2 carries 1–200 bytes in a 210-byte frame protected by systematic
RS(255,210), with 45 parity bytes. The frame is `C7 4B 12 02 LLhi LLlo
payload CRC32C padding`. CRC-32C covers magic through payload. Remaining data
bytes through offset 209 use the deterministic visual-fill algorithm specified
in Format 0.17.0, seeded by CRC-32C over the frame through its CRC. This is
visual decorrelation only: the complete 210-byte data block is still protected
by RS, and CRC remains the payload integrity check. Format 0.15 zero-padding
and Format 0.9 remain readable.

The 29-unit field, one inward-facing rounded arc per corner, Grid/Ray/Ring
calibration keys, Cartesian and polar placement, and connected round-capped
ring runs are unchanged. Profile 2 image decoding remains experimental and
requires physical-capture validation.
