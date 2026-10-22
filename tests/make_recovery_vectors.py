#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate synthetic recovery test vectors; no game data or production imports.

Mode 1 parity is encoded by polynomial division using two remainder registers
for g(x)=x^2+3*x+2 over GF(256), p(x)=0x11d. The production validator uses a
different syndrome recurrence/inverse table. EDC uses a bit-at-a-time oracle;
CRC32 uses Python's standard zlib. Long-suffix CRC replacement vectors use
normal (unreflected) polynomial arithmetic, not production's reflected matrix.
Output is a C include on stdout; Make writes it atomically into build/.
"""
import hashlib
import zlib


def gf_mul(a, b):
    out = 0
    while b:
        if b & 1:
            out ^= a
        b >>= 1
        a <<= 1
        if a & 0x100:
            a ^= 0x11d
    return out


def parity(data, major, minor, multiplier, increment, offset):
    for word in range(major):
        at = (word // 2) * multiplier + (word & 1)
        left = right = 0
        for _ in range(minor):
            factor = data[12 + at] ^ left
            left, right = right ^ gf_mul(factor, 3), gf_mul(factor, 2)
            at = (at + increment) % (major * minor)
        data[offset + word] = left
        data[offset + word + major] = right


def edc(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xd8018001 if value & 1 else 0)
    return value


def bcd(value):
    return (value // 10) * 16 + value % 10


def sector(fad, seed, reserved=0):
    data = bytearray(2352)
    data[1:11] = b"\xff" * 10
    data[12:16] = bytes((bcd(fad // 4500), bcd((fad // 75) % 60), bcd(fad % 75), 1))
    data[16:2064] = bytes((i * 37 + seed) & 255 for i in range(2048))
    data[2064:2068] = edc(data[:2064]).to_bytes(4, "little")
    data[2068] = reserved
    parity(data, 86, 24, 2, 86, 2076)
    parity(data, 52, 43, 86, 88, 2248)
    return data


def poly_mod(value):
    generator = 0x104c11db7
    while value.bit_length() > 32:
        value ^= generator << (value.bit_length() - 33)
    return value


def poly_mul(a, b):
    product = 0
    while b:
        if b & 1:
            product ^= a
        a <<= 1
        b >>= 1
    return poly_mod(product)


def reverse32(value):
    return int(f"{value:032b}"[::-1], 2)


def shifted_crc_delta(delta, suffix):
    power = 2  # x
    factor = 1
    exponent = 8 * suffix
    while exponent:
        if exponent & 1:
            factor = poly_mul(factor, power)
        power = poly_mul(power, power)
        exponent >>= 1
    return reverse32(poly_mul(reverse32(delta), factor))


def byte_rows(values, indent="        "):
    return "\n".join(indent + ",".join(f"0x{x:02x}" for x in values[i:i+16]) + ","
                     for i in range(0, len(values), 16))


def main():
    print("/* Generated synthetic vectors: tests/make_recovery_vectors.py. */")
    print("static const struct { uint32_t fad; uint8_t seed,header[4],trailer[288]; uint32_t crc; unsigned expected; } sector_vectors[] = {")
    for fad, seed, reserved in ((150, 19, 0), (45150, 93, 0),
                                (549150, 177, 0), (719999, 41, 0), (45150, 93, 1)):
        data = sector(fad, seed, reserved)
        print(f"    /* Synthetic SHA-256: {hashlib.sha256(data).hexdigest()} */")
        print(f"    {{ {fad}u, {seed}u, {{"+",".join(f"0x{x:02x}" for x in data[12:16])+"}, {")
        print(byte_rows(data[2064:]))
        print(f"    }}, UINT32_C(0x{zlib.crc32(data):08x}), {64 if reserved else 0}u }},")
    print("};")
    print("static const struct { uint64_t suffix; uint32_t expected; } crc_vectors[] = {")
    whole, before, after = 0x12345678, 0xcbf43926, 0xe3069283
    for suffix in (0, 1, 2352, 1048576, (1 << 32) + 17, (1 << 40) + 2352, 1 << 63, (1 << 64) - 1):
        result = whole ^ shifted_crc_delta(before ^ after, suffix)
        print(f"    {{ UINT64_C({suffix}), UINT32_C(0x{result:08x}) }},")
    print("};")


if __name__ == "__main__":
    main()
