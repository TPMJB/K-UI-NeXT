#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Project an admitted KUIVMUP2 base map into a read-only KUIVMUR1 profile.

This preserves the prepared base image's identity, sectors and hashes. It does
not relocate files, recover a journal or admit a live card. The mode-2 loader
must still verify the card, game-map disjointness and all 256 image sectors.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import zlib

from vmu_persist_profile import decode_profile as decode_persistent
from vmu_persist_profile import encode_profile as encode_persistent
from vmu_sd_profile import decode_profile as decode_readonly


def project_profile(persist_wire, image=None):
    """Return canonical R1 bytes without changing either input.

    P2's decoder requires its recorded zero journal marker and sequence. Those
    are preparation facts, not a new check of the current journal on storage.
    An optional complete raw image must match both SHA256 and every block CRC.
    """
    if not isinstance(persist_wire, (bytes, bytearray)):
        raise ValueError("Requires binary KUIVMUP2 preparation bytes")
    source = bytes(persist_wire)
    profile = decode_persistent(source)
    if encode_persistent(profile) != source:
        raise ValueError("Persistent preparation profile is not canonical")
    if image is not None:
        if not isinstance(image, (bytes, bytearray)) or len(image) != 131072:
            raise ValueError("Raw image must contain exactly131072 bytes")
        image = bytes(image)
        if hashlib.sha256(image).hexdigest() != profile["base_sha256"]:
            raise ValueError("Raw image SHA256 differs from the prepared base")
        for block, expected in enumerate(profile["block_crc"]):
            if zlib.crc32(image[block * 512:(block + 1) * 512]) != expected:
                raise ValueError(f"Raw image block{block} CRC differs from preparation")

    wire = bytearray(1536)
    wire[:8] = b"KUIVMUR1"
    for at, value in ((8, 1), (12, 1536), (20, profile["transport"]),
                      (24, 131072), (28, profile["high_capacity"])):
        struct.pack_into("<I", wire, at, value)
    struct.pack_into("<QQQ", wire, 32, profile["card_sectors"],
                     profile["partition_first"], profile["partition_end"])
    wire[56:72] = bytes.fromhex(profile["cid"])
    wire[72:88] = bytes.fromhex(profile["image_id"])
    wire[88:120] = bytes.fromhex(profile["base_sha256"])
    extents = profile["files"][0]["useful"]
    struct.pack_into("<I", wire, 120, len(extents))
    for index, extent in enumerate(extents):
        struct.pack_into("<III", wire, 128 + index * 12,
                         extent["logical_block"], extent["lba"], extent["blocks"])
    for block, crc in enumerate(profile["block_crc"]):
        struct.pack_into("<I", wire, 320 + block * 4, crc)
    struct.pack_into("<I", wire, 16, zlib.crc32(wire))
    result = bytes(wire)
    decode_readonly(result)
    return result


def write_new(path, data):
    """Create one new ordinary output, sync it and verify exact readback."""
    created = False
    try:
        with path.open("xb") as output:
            created = True
            if output.write(data) != len(data):
                raise OSError("Short read-only profile output write")
            output.flush()
            os.fsync(output.fileno())
        if path.read_bytes() != data:
            raise OSError("Read-only profile output readback differs")
    except Exception:
        if created:
            path.unlink()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, help="Canonical4096-byte KUIVMUP2 input")
    parser.add_argument("output", type=Path, help="Fresh1536-byte KUIVMUR1 output path")
    parser.add_argument("--image", type=Path, help="Also verify the exact131072-byte raw base image")
    args = parser.parse_args()
    try:
        source = args.profile.read_bytes()
        image = args.image.read_bytes() if args.image is not None else None
        readonly = project_profile(source, image)
        write_new(args.output, readonly)
        summary = decode_readonly(readonly)
        summary.update(source_format="KUIVMUP2", output_format="KUIVMUR1",
                       source_profile_sha256=hashlib.sha256(source).hexdigest(),
                       image_verified=image is not None, live_verification_required=True)
        print(json.dumps(summary, indent=2))
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(2, f"Read-only profile projection refused: {error}\n")


if __name__ == "__main__":
    main()
