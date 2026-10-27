#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently admit a canonical per-card read-only VMU build input."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def decode_profile(data):
    if len(data) != 1536 or data[:8] != b"KUIVMUR1":
        raise ValueError("Requires exactly one1536-byte KUIVMUR1 profile")
    word = lambda at: struct.unpack_from("<I", data, at)[0]
    wide = lambda at: struct.unpack_from("<Q", data, at)[0]
    crc_data = bytearray(data)
    crc_data[16:20] = bytes(4)
    if zlib.crc32(crc_data) != word(16):
        raise ValueError("Profile checksum mismatch")
    count, sectors, first, end = word(120), wide(32), wide(40), wide(48)
    if (word(8) != 1 or word(12) != 1536 or word(20) != 1 or
            word(24) != 131072 or word(28) not in (0, 1) or
            not 1 <= sectors <= 0x100000000 or
            (not word(28) and sectors > 0x800000) or
            not 0 <= first < end <= sectors or not 1 <= count <= 16 or
            any(data[124:128]) or any(data[128 + 12 * count:320]) or
            any(data[1344:]) or data[56:72] in (bytes(16), bytes([255]) * 16) or
            not any(data[72:88]) or not any(data[88:120])):
        raise ValueError("Invalid identity, format or reserved profile fields")
    extents, logical = [], 0
    for index in range(count):
        block, lba, blocks = struct.unpack_from("<III", data, 128 + 12 * index)
        if (block != logical or not blocks or blocks > 256 - logical or
                not first <= lba < lba + blocks <= end or
                (extents and extents[-1]["lba"] + extents[-1]["blocks"] == lba) or
                any(lba < old["lba"] + old["blocks"] and old["lba"] < lba + blocks
                    for old in extents)):
            raise ValueError("Profile extent map is not canonical and bounded")
        extents.append(dict(logical_block=block, lba=lba, blocks=blocks))
        logical += blocks
    if logical != 256:
        raise ValueError("Profile must cover exactly256 image sectors")
    return dict(wire_sha256=hashlib.sha256(data).hexdigest(),
                image_sha256=data[88:120].hex(), image_bytes=word(24),
                card_sectors=sectors, high_capacity=word(28),
                partition_first=first, partition_end=end,
                extent_count=count, extents=extents)


def template_profile():
    """Valid wire template for compilation; never matches an admitted card.

    The derived console package replaces only this fixed-size ROdata span.
    Native compilation/audits need no owner's CID, allocation or image data.
    """
    data = bytearray(1536)
    data[:8] = b"KUIVMUR1"
    for at, value in ((8, 1), (12, 1536), (20, 1), (24, 131072), (28, 1), (120, 1)):
        struct.pack_into("<I", data, at, value)
    struct.pack_into("<QQQ", data, 32, 65536, 2048, 65536)
    data[56:88] = bytes(range(1, 33))
    data[88:120] = hashlib.sha256(b"KUI SD VMU build template; personalize before use").digest()
    struct.pack_into("<III", data, 128, 0, 8192, 256)
    struct.pack_into("<I", data, 16, zlib.crc32(data))
    decode_profile(data)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, nargs="?")
    parser.add_argument("--template", type=Path, help="Write a deterministic non-owner build template")
    args = parser.parse_args()
    try:
        if bool(args.profile) == bool(args.template):
            raise ValueError("Choose one input profile or --template output")
        if args.template:
            data = template_profile()
            args.template.parent.mkdir(parents=True, exist_ok=True)
            args.template.write_bytes(data)
        else:
            data = args.profile.read_bytes()
        print(json.dumps(decode_profile(data), indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(2, f"SD profile refused: {error}\n")


if __name__ == "__main__":
    main()
