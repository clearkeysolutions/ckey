#!/usr/bin/env python3
"""Independent RS parity reference for the current versioned CKey locator test vector.

This is a test oracle only. Product SDKs must call the portable C++ core.
It uses polynomial long division with shift/XOR field multiplication.
"""
import json
from pathlib import Path


def gf_mul(a: int, b: int) -> int:
    result = 0
    while b:
        if b & 1:
            result ^= a
        a <<= 1
        if a & 0x100:
            a ^= 0x11D
        b >>= 1
    return result


def gf_pow(a: int, n: int) -> int:
    result = 1
    for _ in range(n):
        result = gf_mul(result, a)
    return result


def generator(parity_bytes: int) -> list[int]:
    coefficients = [1]
    for i in range(parity_bytes):
        root = gf_pow(2, i)
        product = [0] * (len(coefficients) + 1)
        for j, coefficient in enumerate(coefficients):
            product[j] ^= coefficient
            product[j + 1] ^= gf_mul(coefficient, root)
        coefficients = product
    return coefficients


def encode(data: bytes, parity_bytes: int) -> bytes:
    gen = generator(parity_bytes)
    remainder = list(data) + [0] * parity_bytes
    for i in range(len(data)):
        factor = remainder[i]
        for j, coefficient in enumerate(gen):
            remainder[i + j] ^= gf_mul(factor, coefficient)
    return bytes(remainder[-parity_bytes:])


if __name__ == "__main__":
    fixture = json.loads(Path("tests/vectors/locator-v11.json").read_text())
    actual = encode(bytes.fromhex(fixture["frame_hex"]), fixture["rs_parameters"]["n"] - fixture["rs_parameters"]["k"]).hex()
    expected = fixture["rs_parameters"]["parity_hex"]
    if actual != expected:
        raise SystemExit(f"RS fixture mismatch: {actual} != {expected}")
    print(f"Independent RS fixture verified: {actual}")
