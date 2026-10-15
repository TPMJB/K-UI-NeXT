#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Rasterize the original Home SVGs and pin their source/rendered hashes.

Requires Inkscape and Pillow only when editing artwork. Ordinary builds use
the committed PNGs and shell_art.inc. The small SVGs are separately drawn.
"""
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ICONS = ("games", "vmu-manager", "file-manager", "music-player", "gd-play", "memory-test", "network")


def main():
    hashes = {}
    for name in ICONS:
        for suffix, side in (("", 128), ("-small", 24)):
            svg = ROOT / "resources/icons" / (name + suffix + ".svg")
            png = svg.with_suffix(".png")
            subprocess.run(["inkscape", str(svg), "--export-type=png", "--export-filename=" + str(png),
                            "--export-width=" + str(side), "--export-height=" + str(side)],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            # Strip renderer metadata; retain genuine alpha, including corners.
            with Image.open(png) as image:
                rgba = image.convert("RGBA")
            assert rgba.size == (side, side)
            rgba.save(png, optimize=True)
            for path in (svg, png):
                hashes[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = ROOT / "resources/icons/home-icons.json"
    manifest.write_text(json.dumps(hashes, indent=2, sort_keys=True) + "\n")
    print("Rendered seven large/small icon pairs and updated their SHA-256 manifest.")


if __name__ == "__main__":
    main()
