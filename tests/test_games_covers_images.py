#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Box art scan, selected-cover caches, saved view and image-details covers on real
FAT32/exFAT images. Original synthetic discs and textures only."""
from pathlib import Path
import shutil
import struct
import tempfile
import zlib
from test_images import run
from games_fixture import make_fixture, raw_sector, dual32

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-covers-image")
CASES = ("scan", "selected-cache", "cancel", "write-fail", "missing-root", "format-iso", "format-cue",
         "format-cdi", "format-compressed", "format-legacy-empty", "format-payloads")


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
    write_manifest(base)


def write_manifest(base):
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


def make_format_tree(base, case):
    source = base / "source"
    art = pvr(1, 1, 128, 128, twiddled_square(128, lambda x, y: 0xF800 if y < 64 else 0x001F))
    make_fixture(source, b"GENERIC BOX ART", art)
    raw = (source / "track03.bin").read_bytes()
    sectors = [bytearray(raw[n + 16:n + 2064]) for n in range(0, len(raw), 2352)]
    session = 11700 if case == "format-cdi" else 0
    sectors[0][37:43] = b"CD-ROM"
    dual32(sectors[16], 80, len(sectors))
    dual32(sectors[16], 158, session + 20)
    directory = sectors[20]
    offset = 0
    while directory[offset]:
        extent = struct.unpack_from("<I", directory, offset + 2)[0]
        dual32(directory, offset + 2, extent - 45000 + session)
        offset += directory[offset]
    games = base / "Games"
    folder = games / ("Ambiguous" if case == "format-payloads" else "Format Game")
    folder.mkdir(parents=True)
    (base / "KUI" / "covers").mkdir(parents=True)
    if case == "format-compressed":
        (folder / "selected.cso").write_bytes(b"CISO")
    elif case in ("format-iso", "format-legacy-empty"):
        (folder / "selected.iso").write_bytes(b"".join(sectors))
    else:
        data = []
        for n, sector in enumerate(sectors):
            framed = raw_sector(session + n, sector)
            framed[15] = 2
            framed[16:24] = bytes((1, 2, 8, 0, 1, 2, 8, 0))
            framed[24:2072] = sector
            data.append(bytes(framed))
        if case in ("format-cue", "format-payloads"):
            (folder / "payload.bin").write_bytes(b"".join(data))
            (folder / "selected.cue").write_text(
                'FILE "payload.bin" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n', encoding="ascii")
            if case == "format-payloads":
                (folder / "independent.iso").write_bytes(b"".join(sectors))
        else:
            assert case == "format-cdi"
            stride, pregap = 2336, 3
            payload = bytes([0xD3]) * (pregap * stride) + b"".join(s[16:] for s in data)
            fields = bytearray(87)
            struct.pack_into("<II", fields, 0, pregap, len(sectors))
            struct.pack_into("<I", fields, 14, 2)
            struct.pack_into("<II", fields, 30, session + 150 - pregap, pregap + len(sectors))
            struct.pack_into("<I", fields, 54, 1)
            marker = bytes((0, 0, 1, 0, 0, 0, 255, 255, 255, 255)) * 2
            track = bytes(4) + marker + bytes(4) + bytes(1) + bytes(19) + bytes(4) + bytes(2) + fields + bytes(9)
            footer = struct.pack("<HH", 1, 1) + track + bytes(13)
            (folder / "selected.cdi").write_bytes(payload + footer + struct.pack("<II", 0x80000006, len(footer) + 8))
    write_manifest(base)


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
                selected_fixture = fixture
                if case.startswith("format-"):
                    selected_fixture = base / f"fixture-{kind}-{case}"
                    make_format_tree(selected_fixture, case)
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(clean, image)
                run(BINARY, str(image), str(selected_fixture), "seed", case)
                output = run(BINARY, str(image), str(selected_fixture), "check", case)
                assert f"PASS Games covers check {case}" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} Games covers: {case}", flush=True)


if __name__ == "__main__":
    main()
