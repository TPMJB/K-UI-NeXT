#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Enlarge the exact 192-byte top-left/MSB-first VMU bitmap without smoothing."""
import argparse
from pathlib import Path
import struct
import zlib


def png(bitmap, scale=12):
    if len(bitmap) != 192:
        raise ValueError("VMU LCD bitmap must be 192 bytes (48x32, 1bpp)")
    rows = []
    for y in range(32):
        line = bytearray(b"\0")
        for x in range(48):
            on = bitmap[y * 6 + x // 8] & (0x80 >> (x % 8))
            line.extend((b"\x1c\x32\x22" if on else b"\xc7\xd4\xaf") * scale)
        rows.extend([bytes(line)] * scale)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48 * scale, 32 * scale, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bitmap", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(png(args.bitmap.read_bytes()))


if __name__ == "__main__":
    main()
