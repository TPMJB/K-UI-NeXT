#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Box art scan, page covers, saved view and image-details covers on real
FAT32/exFAT images. Original synthetic discs and textures only."""
from pathlib import Path
import shutil
import struct
import tempfile
import zlib
from test_images import run
from games_fixture import make_fixture

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-covers-image")
CASES = ("scan", "cancel", "write-fail", "missing-root")


def twiddled(x, y):
    """Morton order, y in the even bits (documented in KallistiOS pvrtex)."""
    out = 0
    for bit in range(10):
        out |= ((y >> bit) & 1) << (2 * bit) | ((x >> bit) & 1) << (2 * bit + 1)
    return out


def pvr(pixel_format, layout, width, height, payload, gbix=False):
    chunk = b"PVRT" + struct.pack("<IBBHHH", 8 + len(payload), pixel_format, layout, 0, width, height) + payload
    return (b"GBIX" + struct.pack("<III", 8, 7, 0) if gbix else b"") + chunk


def twiddled_square(edge, colour):
    texels = [0] * (edge * edge)
    for y in range(edge):
        for x in range(edge):
            texels[twiddled(x, y)] = colour(x, y)
    return struct.pack(f"<{edge * edge}H", *texels)


def png(width, height, rgb, note=b""):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + bytes(rgb) * width for _ in range(height))
    text = chunk(b"tEXt", b"Comment\0" + note) if note else b""
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            text + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def make_tree(base):
    games = base / "Games"
    # Top half red, bottom half blue: shows the texture is not transposed.
    split = pvr(1, 1, 128, 128, twiddled_square(128, lambda x, y: 0xF800 if y < 64 else 0x001F))
    make_fixture(games / "Twiddled Game", b"TWIDDLED GAME", split)
    # VQ: every block uses codebook entry 0, four green texels.
    vq = struct.pack("<4H", *[0x07E0] * 4) + bytes(2040) + bytes(128 * 128)
    make_fixture(games / "VQ Game", b"VQ GAME", pvr(1, 3, 256, 256, vq))
    make_fixture(games / "Plain Game", b"PLAIN GAME")
    make_fixture(games / "User Art", b"USER ART GAME")
    # ARGB1555 with mipmaps and a GBIX chunk: the largest level ends the chunk.
    mips = bytes(2 * 1366) + struct.pack("<4096H", *[0xFFE0] * 4096)
    make_fixture(games / "Fighting" / "Category Game", b"CATEGORY GAME", pvr(0, 2, 64, 64, mips, True))
    make_fixture(games / "Fighting" / "Plain Game", b"SAME NAME GAME")
    # A loose GDI beside its tracks, with a wide stride texture.
    make_fixture(games, b"LOOSE GAME", pvr(1, 9, 64, 32, struct.pack("<2048H", *[0xF81F] * 2048)), "Loose.gdi")
    # Two GDIs sharing one folder are two games named after themselves.
    make_fixture(games / "Pair", b"PAIR GAME", None, "a.gdi")
    shutil.copyfile(games / "Pair" / "a.gdi", games / "Pair" / "b.gdi")
    covers = base / "KUI" / "covers"
    covers.mkdir(parents=True)
    (covers / "User Art.png").write_bytes(png(40, 60, (30, 60, 200)))
    extra = base / "extra"
    extra.mkdir()
    replacement = png(50, 50, (200, 60, 30), b"a replacement image of a different size")
    assert len(replacement) != (covers / "User Art.png").stat().st_size
    (extra / "User Art.png").write_bytes(replacement)
    # Name order (as C strcmp sorts) fixes the card's directory order.
    lines = []

    def walk(folder, relative):
        for path in sorted(folder.iterdir(), key=lambda p: p.name):
            name = f"{relative}/{path.name}" if relative else path.name
            lines.append(f"{'D' if path.is_dir() else 'F'} {name}")
            if path.is_dir():
                walk(path, name)
    for top in ("KUI", "Games"):
        lines.append(f"D {top}")
        walk(base / top, top)
    (base / "manifest.txt").write_text("\n".join(lines) + "\n")


def main():
    with tempfile.TemporaryDirectory(prefix="kui-covers-") as temp:
        base = Path(temp)
        fixture = base / "fixture"
        make_tree(fixture)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(clean)) if kind == "fat32" else run("mkfs.exfat", str(clean))
            for case in CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(clean, image)
                run(BINARY, str(image), str(fixture), "seed", case)
                output = run(BINARY, str(image), str(fixture), "check", case)
                assert f"PASS Games covers check {case}" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} Games covers: {case}", flush=True)


if __name__ == "__main__":
    main()
