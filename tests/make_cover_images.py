#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regenerate original box art test images: PNG written here, JPEG by ffmpeg.

Run manually after a deliberate fixture update. Tests use the checked-in bytes
and need no ffmpeg. Encoder versions may alter the JPEG bytes.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def png(width, height, channels, pixel):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))
    colour = 2 if channels == 3 else 6
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, colour, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def rgb(x, y):
    return (x * 4, y * 5, (x + y) * 2)


def rgba(x, y):
    return (x * 16, 255 - y * 16, 128, (x + y) * 8)


fixtures = [("png_rgb", png(64, 48, 3, rgb)), ("png_rgba", png(16, 16, 4, rgba))]
with tempfile.TemporaryDirectory(prefix="kui-cover-") as temp:
    source = Path(temp) / "source.png"
    source.write_bytes(fixtures[0][1])
    jpeg = Path(temp) / "cover.jpg"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(source), "-q:v", "2", "-pix_fmt", "yuvj444p",
                    str(jpeg)], check=True)
    fixtures.append(("jpeg", jpeg.read_bytes()))
header = ("/* SPDX-License-Identifier: GPL-3.0-only */\n"
          "/* Original synthetic gradients from tests/make_cover_images.py; the JPEG is\n"
          " * FFmpeg's encoding of the RGB PNG. No third-party artwork. */\n")
for name, data in fixtures:
    header += f"static const unsigned char cover_{name}[] = {{\n"
    for at in range(0, len(data), 16):
        header += "    " + ",".join(f"0x{byte:02x}" for byte in data[at:at + 16]) + ",\n"
    header += "};\n"
(ROOT / "tests/fixtures/cover_images.h").write_text(header)
