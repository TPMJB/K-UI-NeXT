#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Seal the exact persistent CE helper owner for the unified Smash package."""
import argparse
from pathlib import Path
import struct
import zlib


def profile(helper, kind, *, embedded=False, prepared_image=None):
    if kind not in (1, 2):
        raise ValueError("CE profile must be VC2(1) or Swirl(2)")
    if not helper or len(helper) > 49152 or len(helper) & 3:
        raise ValueError("helper must be nonempty, aligned and at most48KiB")
    swirl = kind == 2
    image=helper+bytes(49152-len(helper)) if prepared_image is None else prepared_image
    if len(image)!=49152 or image[:len(helper)]!=helper:raise ValueError("prepared transport changes immutable helper bytes")
    helper_crc = zlib.crc32(image)
    if not helper_crc:
        raise ValueError("zero helper CRC is not an admitted owner")
    words = [0x5349554b, 0x31504543, 2 if embedded else 1, 80, kind, 1 if swirl else 2,
             7 if swirl else 3, 493127 if swirl else 529805,
             893 if swirl else 899, 0x1be800 if swirl else 0x1c1800,
             0x1be000 if swirl else 0x1c1000,
             0x14a27ac2 if swirl else 0x6d9c21a3,
             0x7e29bc00 if swirl else 0xa253bfef,
             0x8cfb8000 if swirl else 0x8cfc0000,
             49152, helper_crc, 0x8cfc4000, 49152, 0, 0]
    blob = struct.pack("<20I", *words)
    return blob[:-4] + struct.pack("<I", zlib.crc32(blob))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("helper", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--kind", type=int, required=True, choices=(1, 2))
    parser.add_argument("--embedded-helper", action="store_true")
    parser.add_argument("--prepared-image", type=Path)
    args = parser.parse_args()
    try:
        args.output.write_bytes(profile(args.helper.read_bytes(), args.kind, embedded=args.embedded_helper,
            prepared_image=args.prepared_image.read_bytes() if args.prepared_image else None))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
