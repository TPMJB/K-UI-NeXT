#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Whole-image library identities on real FatFs, using original test bytes."""
from pathlib import Path
import hashlib
import os
import shlex
import shutil
import struct
import sys
import tempfile
from games_fixture import make_fixture, dual32
from test_games_library_images import private_formatter
from test_images import run
from zso_fixture import ziso

ROOT = Path(__file__).resolve().parents[1]


def oracle(tracks, session):
    digest = hashlib.sha256(b"K-UI represented image identity v1\0")
    digest.update(struct.pack("<II", len(tracks), session))
    for number, start, control, mode, payload, stride in tracks:
        logical = b"".join(payload[at + 16:at + 16 + 2048] for at in range(0, len(payload), stride)) if control == 4 and stride == 2352 else payload
        sectors = len(payload) // stride
        digest.update(struct.pack("<IIIII", number, start, start + sectors, control, mode))
        digest.update(logical)
    return digest.hexdigest()


def fixture(base):
    games = base / "Games"
    original = games / "Original"
    make_fixture(original)
    run(sys.executable, str(ROOT / "tools/gdi_optimize.py"), str(original / "disc.gdi"), str(games / "Cooked"))
    renamed = games / "Renamed"
    shutil.copytree(original, renamed)
    descriptor = (renamed / "disc.gdi").read_text()
    for old, new in (("track01.bin", "data-one.bin"), ("track02.raw", "sound.raw"), ("track03.bin", "data-three.bin")):
        (renamed / old).rename(renamed / new)
        descriptor = descriptor.replace(old, new)
    (renamed / "selected.gdi").write_text(descriptor)
    (renamed / "disc.gdi").unlink()
    mixed = games / "Mixed"
    shutil.copytree(original, mixed)
    (mixed / "track03.bin").write_bytes((games / "Cooked/track03.iso").read_bytes())
    (mixed / "disc.gdi").write_text((original / "disc.gdi").read_text().replace("3 45000 4 2352", "3 45000 4 2048"))
    for name, track, offset, value in (
        ("Changed", "track03.bin", 21 * 2352 + 16 + 100, None),
        ("Unused", "track03.bin", 48 * 2352 + 16 + 100, None),
        ("Audio", "track02.raw", 2500, None),
        ("Revision", "track03.bin", 16 + 79, ord("1")),
        ("Disc2", "track03.bin", 16 + 43, ord("2")),
        ("Bad", "track03.bin", 16, ord("X")),
    ):
        destination = games / name
        shutil.copytree(original, destination)
        path = destination / track
        data = bytearray(path.read_bytes())
        data[offset] = data[offset] ^ 0x55 if value is None else value
        if name == "Disc2":
            data[16 + 45] = ord("2")
        path.write_bytes(data)
    truncated = games / "Truncated"
    shutil.copytree(original, truncated)
    path = truncated / "track02.raw"
    path.write_bytes(path.read_bytes()[:-1])
    raw = (original / "track03.bin").read_bytes()
    iso = bytearray(b"".join(raw[at + 16:at + 16 + 2048] for at in range(0, len(raw), 2352)))
    iso[32:48] = b"0000 CD-ROM1/1  "
    dual32(iso, 16 * 2048 + 80, 64)
    dual32(iso, 16 * 2048 + 156 + 2, 20)
    at = 20 * 2048
    while iso[at]:
        previous = struct.unpack_from("<I", iso, at + 2)[0]
        dual32(iso, at + 2, previous - 45000)
        at += iso[at]
    folder = games / "ISO"
    folder.mkdir()
    (folder / "disc.iso").write_bytes(iso)
    (folder / "disc.cue").write_text('FILE "disc.iso" BINARY\n TRACK 01 MODE1/2048\n INDEX 01 00:00:00\n')
    (folder / "disc.zso").write_bytes(ziso(bytes(iso), 0)[0])
    entries = []
    def walk(folder, relative):
        for path in sorted(folder.iterdir()):
            name = relative / path.name
            entries.append(f"{'D' if path.is_dir() else 'F'} {name.as_posix()}")
            if path.is_dir():
                walk(path, name)
    entries.append("D Games")
    walk(games, Path("Games"))
    (base / "manifest.txt").write_text("\n".join(entries) + "\n")
    tracks = [(1, 0, 4, 1, (original / "track01.bin").read_bytes(), 2352),
              (2, 4, 0, 0, (original / "track02.raw").read_bytes(), 2352),
              (3, 45000, 4, 1, raw, 2352)]
    return {"original": oracle(tracks, 45000), "iso": oracle([(1, 0, 4, 1, bytes(iso), 2048)], 0)}


def main():
    with tempfile.TemporaryDirectory(prefix="kui-identity-images-") as temporary:
        base = Path(temporary)
        sources = base / "fixture"
        wanted = fixture(sources)
        binary = base / "games-identity-image"
        paths = ["tests/games_identity_image.c", "src/apps/games_identity.c", "src/apps/games_storage.c",
                 "src/core/game_image.c", "src/core/game_metadata.c", "src/core/zso.c", "src/core/lzo1x.c",
                 "src/core/destination.c", "src/core/command.c", "src/core/data.c", "src/core/diskio.c",
                 "src/core/clock.c", "src/core/storage_probe.c", "src/core/hash.c",
                 ".deps/fatfs/source/ff.c", ".deps/fatfs/source/ffunicode.c"]
        run(*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-I" + str(ROOT / "include"), "-I" + str(ROOT / ".deps/fatfs/source"), "-I" + str(ROOT / "src/dreamcast"),
            *(str(ROOT / path) for path in paths), "-Wl,--wrap=f_open,--wrap=f_close,--wrap=f_read", "-lm", "-o", str(binary))
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            card = base / f"{kind}.img"
            run(str(formatter), kind, str(card))
            run(str(binary), str(card), str(sources), "seed")
            before = hashlib.file_digest(card.open("rb"), "sha256").digest()
            output = run(str(binary), str(card), str(sources), "check")
            after = hashlib.file_digest(card.open("rb"), "sha256").digest()
            assert before == after, f"Identity verification modified {kind} filesystem bytes"
            for key, digest in wanted.items():
                assert f"DIGEST {key} {digest}" in output, output
            assert "PASS represented identity:" in output
            print(f"PASS {kind}: whole represented image fingerprints match independent SHA256 oracle; faults/cancellation preserve outputs and close handles; whole-card SHA256 unchanged", flush=True)


if __name__ == "__main__":
    main()
