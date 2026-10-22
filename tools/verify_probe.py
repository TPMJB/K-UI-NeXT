#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently check a console storage probe directory (Python 3, no packages)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def pattern(offset, size):
    out = bytearray()
    for word in range(offset // 4, (offset + size + 3) // 4):
        x = (word ^ 0x4B554931) & 0xFFFFFFFF
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out.extend(struct.pack("<I", x))
    begin = offset % 4
    return bytes(out[begin:begin + size])


def verify(directory):
    manifest = json.loads((directory / "storage.json").read_text())
    if (manifest.get("schema") != 1 or manifest.get("complete") is not True or
            manifest.get("pattern") != "kui-xorshift32-v1"):
        raise ValueError("Unsupported or incomplete manifest")
    expected_size = manifest.get("bytes")
    if type(expected_size) is not int or not 0 < expected_size <= 64 * 1024 * 1024 + 173:
        raise ValueError("Invalid expected byte count")
    path = directory / "storage.bin"
    if path.stat().st_size != expected_size:
        raise ValueError("File length differs from the manifest")
    offset = crc = 0
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(65536):
            expected = pattern(offset, len(chunk))
            if chunk != expected:
                index = next(i for i, pair in enumerate(zip(chunk, expected)) if pair[0] != pair[1])
                raise ValueError(f"Pattern mismatch at byte {offset + index}")
            offset += len(chunk)
            crc = zlib.crc32(chunk, crc)
            sha.update(chunk)
    if offset != expected_size or f"{crc:08x}" != manifest.get("crc32"):
        raise ValueError("Saved length or CRC32 differs from the console result")
    return {"bytes": offset, "crc32": f"{crc:08x}", "sha256": sha.hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="KUI/probes/pNNNN containing storage.bin and storage.json")
    args = parser.parse_args()
    try:
        result = verify(args.directory)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"FAIL: {error}\n")
    print("PASS: length, byte pattern and console CRC32 match.")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
