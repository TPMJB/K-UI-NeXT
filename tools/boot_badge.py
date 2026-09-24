#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the original K-UI boot badge and its placement in a packaged CDI.

No artwork conversion or bootstrap patching is performed here. mkdcdisc's -i
option embeds the already encoded MR asset in its own bootstrap. Source and
format references are recorded in resources/branding/boot-disc-badge.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BADGE = ROOT / "resources/branding/boot-disc-badge.mr"
BADGE_SHA256 = "4f95ef3caac9d449914baa6266b154582363edb7e5b821d5bf18121951fc5fec"
MR_OFFSET = 0x3820
MR_LIMIT = 8192
BOOT_BYTES = 32768
DATA_BYTES = 2048
BOOT_SIGNATURE = b"SEGA SEGAKATANA SEGA ENTERPRISES "


def inspect_badge(data):
    """Require the unchanged original asset and its bounded MR geometry."""
    digest = hashlib.sha256(data).hexdigest()
    if digest != BADGE_SHA256:
        raise ValueError("Boot badge differs from the approved original asset")
    if len(data) < 30 or data[:2] != b"MR":
        raise ValueError("Missing MR badge header")
    size, reserved, offset, width, height, reserved2, colors = struct.unpack_from("<7I", data, 2)
    if not 30 < size == len(data) <= MR_LIMIT or reserved or reserved2:
        raise ValueError("Invalid MR badge size or reserved fields")
    if (width, height, colors) != (320, 90, 32) or offset != 30 + 4 * colors:
        raise ValueError("Original MR badge geometry changed")
    # MR uses indexed literals and runs. Decoding counts here catches malformed
    # or truncated data before any disc is produced; no pixel buffer is needed.
    cursor, pixels = offset, 0
    while cursor < size:
        token = data[cursor]
        cursor += 1
        count, color = 1, token
        if token >= 128:
            if cursor >= size:
                raise ValueError("Truncated MR run")
            count = token & 127
            if token == 0x81 or (token == 0x82 and data[cursor] >= 128):
                count = data[cursor] if token == 0x81 else 256 + (data[cursor] & 127)
                cursor += 1
                if cursor >= size:
                    raise ValueError("Truncated MR run color")
            color = data[cursor]
            cursor += 1
        if not count or color >= colors or count > width * height - pixels:
            raise ValueError("MR run exceeds image or palette bounds")
        pixels += count
    if pixels != width * height:
        raise ValueError("Incomplete MR badge pixels")
    return {"sha256": digest, "bytes": size, "width": width, "height": height,
            "colors": colors, "bootstrap_offset": f"0x{MR_OFFSET:04x}"}


def verify_cdi_badge(data, badge):
    """Read the logo back from the generated disc's ISO9660 bootstrap.

    The pinned mkdcdisc uses 2336-byte Mode2 sectors. Recognize the raw 2352-
    and data-only 2048-byte representations too, requiring the ISO descriptor
    and its sector size to identify a unique bootstrap rather than searching
    for an incidental copy of the artwork in arbitrary file bytes.
    """
    info = inspect_badge(badge)
    candidates = []
    search = 0
    while True:
        start = data.find(BOOT_SIGNATURE, search)
        if start < 0:
            break
        search = start + 1
        for stride in (2336, 2352, 2048):
            descriptor = data[start + 16 * stride:start + 16 * stride + DATA_BYTES]
            if (len(descriptor) != DATA_BYTES or descriptor[:7] != b"\x01CD001\x01" or
                    descriptor[128:132] != b"\x00\x08\x08\x00"):
                continue
            bootstrap = b"".join(data[start + i * stride:start + i * stride + DATA_BYTES]
                                 for i in range(BOOT_BYTES // DATA_BYTES))
            if (len(bootstrap) != BOOT_BYTES or
                    bootstrap[0x60:0x70].rstrip(b" ") != b"1ST_READ.BIN"):
                raise ValueError("Invalid CDI bootstrap or boot filename")
            candidates.append((start, stride, bootstrap))
    if len(candidates) != 1:
        raise ValueError("CDI must contain one recognizable ISO9660 bootstrap")
    start, stride, bootstrap = candidates[0]
    if bootstrap[MR_OFFSET:MR_OFFSET + len(badge)] != badge:
        raise ValueError("CDI does not contain the original K-UI boot badge")
    return {**info, "cdi_bootstrap_offset": start, "cdi_sector_bytes": stride}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cdi", type=Path, help="also verify the badge in this generated CDI")
    args = parser.parse_args()
    try:
        badge = BADGE.read_bytes()
        info = verify_cdi_badge(args.cdi.read_bytes(), badge) if args.cdi else inspect_badge(badge)
        print(json.dumps(info, sort_keys=True))
    except (OSError, ValueError, struct.error) as error:
        raise SystemExit(f"Boot badge validation failed: {error}") from error


if __name__ == "__main__":
    main()
