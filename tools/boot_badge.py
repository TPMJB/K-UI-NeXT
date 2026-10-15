#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the Tyrfing K-UI boot badge and its placement in a packaged CDI.

mkdcdisc's -i option embeds the already encoded MR asset in its own bootstrap.
Optional --rebuild-branding renders the editable vector badge and header with
Inkscape/Pillow. Ordinary validation needs only the Python standard library.
Source and format references are in resources/branding/boot-disc-badge.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BADGE = ROOT / "resources/branding/boot-disc-badge.mr"
BADGE_SHA256 = "42de4efd6e9401fbd1df17a42e1240a3e0a5b200ed5b7bc573ba29d0d1c77bac"
MR_OFFSET = 0x3820
MR_LIMIT = 8192
BOOT_BYTES = 32768
DATA_BYTES = 2048
BOOT_SIGNATURE = b"SEGA SEGAKATANA SEGA ENTERPRISES"


def inspect_badge(data):
    """Require the approved asset and its bounded MR geometry."""
    digest = hashlib.sha256(data).hexdigest()
    if digest != BADGE_SHA256:
        raise ValueError("Boot badge differs from the approved Tyrfing asset")
    if len(data) < 30 or data[:2] != b"MR":
        raise ValueError("Missing MR badge header")
    size, reserved, offset, width, height, reserved2, colors = struct.unpack_from("<7I", data, 2)
    if not 30 < size == len(data) <= MR_LIMIT or reserved or reserved2:
        raise ValueError("Invalid MR badge size or reserved fields")
    if (width, height, colors) != (320, 90, 32) or offset != 30 + 4 * colors:
        raise ValueError("Approved MR badge geometry changed")
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
        raise ValueError(f"CDI must contain one recognizable ISO9660 bootstrap; found {len(candidates)}")
    start, stride, bootstrap = candidates[0]
    if bootstrap[MR_OFFSET:MR_OFFSET + len(badge)] != badge:
        raise ValueError("CDI does not contain the approved K-UI boot badge")
    return {**info, "cdi_bootstrap_offset": start, "cdi_sector_bytes": stride}


def rebuild_branding():
    """Render the shared vector mark, then encode its 32-color MR preview.

    The independent encoder uses only literal indices and runs of 2..127;
    this small image does not need the extended-run forms the validator accepts.
    The SVG is the editable source for both deployed images.
    """
    import subprocess
    import tempfile
    import xml.etree.ElementTree as ET
    from PIL import Image

    branding = ROOT / "resources/branding"
    source = branding / "boot-disc-badge.svg"
    with tempfile.TemporaryDirectory(prefix="kui-branding-") as directory:
        temp = Path(directory)
        rendered = temp / "badge.png"
        subprocess.run(["inkscape", str(source), "--export-type=png",
                        "--export-filename=" + str(rendered)], check=True)
        image = Image.open(rendered).convert("RGB").quantize(
            colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        if image.size != (320, 90) or len(image.getcolors()) != 32:
            raise ValueError("Expected a 320x90 badge with 32 colors")
        image.save(branding / "boot-disc-badge.png", optimize=False)

        # Preserve the original K-UI wordmark paths at a readable header size.
        # Share the sword with the badge rather than maintaining a second icon.
        namespace = "{http://www.w3.org/2000/svg}"
        ET.register_namespace("", namespace[1:-1])
        header = ET.Element(namespace + "svg", {
            "width": "256", "height": "128", "viewBox": "0 0 256 128"})
        ET.SubElement(header, namespace + "title").text = "K-UI Tyrfing by TPMJB"
        ET.SubElement(header, namespace + "rect", {
            "width": "256", "height": "128", "fill": "#080f23"})
        for item in ET.parse(source).getroot():
            transform = {"tyrfing-mark": "translate(7 21) scale(.95)",
                         "kui-wordmark": "translate(12 29) scale(.79)"}.get(item.get("id"))
            if transform:
                group = ET.SubElement(header, namespace + "g", {"transform": transform})
                group.append(item)
        ET.SubElement(header, namespace + "text", {
            "x": "91", "y": "91", "font-family": "DejaVu Sans Mono, monospace",
            "font-size": "12", "font-weight": "bold", "letter-spacing": "2",
            "fill": "#ffc56a"}).text = "by TPMJB"
        header_source = temp / "launcher.svg"
        ET.ElementTree(header).write(header_source, encoding="unicode")
        header_png = temp / "launcher.png"
        subprocess.run(["inkscape", str(header_source), "--export-type=png",
                        "--export-filename=" + str(header_png)], check=True)
        Image.open(header_png).convert("RGB").save(branding / "launcher-brand.png")

    indices = list(image.tobytes())
    compressed = bytearray()
    at = 0
    while at < len(indices):
        length = 1
        while length < 127 and at + length < len(indices) and indices[at + length] == indices[at]:
            length += 1
        if length > 1:
            compressed.append(0x80 | length)
        compressed.append(indices[at])
        at += length
    palette = image.getpalette()
    palette_bytes = b"".join(bytes((palette[i + 2], palette[i + 1], palette[i], 0))
                             for i in range(0, 32 * 3, 3))
    offset = 30 + len(palette_bytes)
    badge = b"MR" + struct.pack("<7I", offset + len(compressed), 0, offset, 320, 90, 0, 32)
    badge += palette_bytes + compressed
    if len(badge) > MR_LIMIT:
        raise ValueError("Rebuilt badge exceeds the boot logo reservation")
    BADGE.write_bytes(badge)
    print("Rebuilt branding; update source pins after reviewing the PNG previews.")
    print("BADGE_SHA256 = " + repr(hashlib.sha256(badge).hexdigest()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cdi", type=Path, help="also verify the badge in this generated CDI")
    parser.add_argument("--rebuild-branding", action="store_true",
                        help="rebuild badge and launcher PNG/MR from SVG (requires Inkscape/Pillow)")
    args = parser.parse_args()
    try:
        if args.rebuild_branding:
            if args.cdi:
                parser.error("--rebuild-branding cannot be combined with --cdi")
            rebuild_branding()
            return
        badge = BADGE.read_bytes()
        info = verify_cdi_badge(args.cdi.read_bytes(), badge) if args.cdi else inspect_badge(badge)
        print(json.dumps(info, sort_keys=True))
    except (OSError, ValueError, struct.error) as error:
        raise SystemExit(f"Boot badge validation failed: {error}") from error


if __name__ == "__main__":
    main()
