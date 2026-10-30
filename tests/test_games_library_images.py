#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Saved Games catalogue behavior and failure recovery on FAT32 and exFAT."""
from pathlib import Path
import argparse
import shutil
import sys
import tempfile
from games_fixture import make_fixture
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-library-image")
CASES = ("cold", "groups", "stale", "cancel", "cancel-before", "cancel-readback",
         "first-write-fail", "load-read-fail", "corrupt-newer", "pending-newer", "epoch", "corrupt-epoch",
         "volume-identity", "generation-read-fail", "source-read-fail", "failed-invalidation-readonly")


def fixture(base):
    games = base / "Games"
    make_fixture(games / "Skies of Arcadia Disc 1", b"SYNTHETIC DISC ONE")
    make_fixture(games / "Skies of Arcadia Disc 2", b"SYNTHETIC DISC TWO")
    run(sys.executable, str(ROOT / "tools/gdi_optimize.py"),
        str(games / "Skies of Arcadia Disc 2" / "disc.gdi"),
        str(games / "Skies of Arcadia Disc 2-2048"))
    simple = ["Standalone", "Duplicate Disc 1", "Duplicate Disc 01", "Duplicate Disc 2",
              "Skies of Arcadia (Japan) Disc 1", "Skies of Arcadia (Japan) Disc 2"]
    simple += [f"Filler {number:02}" for number in range(10)]
    for name in simple:
        folder = games / name
        folder.mkdir(parents=True)
        (folder / "selected.iso").write_bytes(b"SYNTHETIC IMAGE; LISTING ONLY")
    (base / "KUI").mkdir()
    entries = []

    def walk(folder, relative):
        for path in sorted(folder.iterdir(), key=lambda p: p.name):
            name = relative / path.name
            entries.append(f"{'D' if path.is_dir() else 'F'} {name.as_posix()}")
            if path.is_dir():
                walk(path, name)
    for top in ("KUI", "Games"):
        entries.append(f"D {top}")
        walk(base / top, Path(top))
    (base / "manifest.txt").write_text("\n".join(entries) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", choices=CASES)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kui-games-library-") as temp:
        base = Path(temp)
        source = base / "fixture"
        fixture(source)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(clean)) if kind == "fat32" else run("mkfs.exfat", str(clean))
            for case in args.case or CASES:
                card = base / f"{kind}-{case}.img"
                shutil.copyfile(clean, card)
                run(BINARY, str(card), str(source), "seed", case)
                output = run(BINARY, str(card), str(source), "check", case)
                assert f"PASS Games catalogue {case}; production writes stayed under KUI" in output
                for line in output.splitlines():
                    if line.startswith("EVIDENCE "):
                        print(f"{kind}/{case}: {line}")
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(card))
                card.unlink()
                print(f"PASS {kind} saved Games catalogue: {case}")
    print("PASS saved Games catalogue images")


if __name__ == "__main__":
    main()
