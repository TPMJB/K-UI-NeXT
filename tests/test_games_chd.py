#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT CHD preparation, source preservation and write faults."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
import tempfile
from games_fixture import make_fixture
from game_format_fixture import make_format_fixture
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-chd-image")
CASES = ("inspect", "prepare", "reuse", "changed-source", "existing-incomplete",
         "cancel-before", "cancel-read", "cancel-write", "connect-fail",
         "source-open-fail", "source-read-fail", "source-short-read", "source-seek-fail",
         "source-close-fail", "output-open-fail", "write-fail", "short-write",
         "verify-corrupt", "sync-fail", "close-fail", "rename-fail", "mkdir-fail",
         "getfree-fail", "no-space", "decode-read-fail", "cancel-verify", "cancel-publish",
         "stamp-write-fail", "stamp-sync-fail", "descriptor-write-fail", "descriptor-corrupt",
         "descriptor-close-fail")
CD_CASES = ("cd-inspect", "cd-prepare", "cd-reuse", "cd-cue-write-fail", "cd-cue-sync-fail",
            "cd-cue-corrupt", "cd-cue-close-fail", "cd-cancel-cue-publish")
CASES += CD_CASES


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chdman", default=os.environ.get("CHDMAN", "chdman"))
    parser.add_argument("--case", choices=CASES, action="append")
    parser.add_argument("--filesystem", choices=("fat32", "exfat"), action="append")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kui-chd-games-") as temp:
        base = Path(temp)
        fixture = base / "original"
        make_fixture(fixture)
        run(args.chdman, "createcd", "-i", str(fixture / "disc.gdi"),
            "-o", str(fixture / "selected.chd"), "-c", "cdzl")
        run(args.chdman, "verify", "-i", str(fixture / "selected.chd"))
        cd_fixture = base / "cd-original"
        make_format_fixture(cd_fixture, "format-iso-cd0")
        (cd_fixture / "disc.cue").write_text(
            'REM KUI SCRAMBLED 0\nFILE "selected.iso" BINARY\n TRACK 01 MODE1/2048\n  INDEX 01 00:00:00\n',
            encoding="ascii")
        run(args.chdman, "createcd", "-i", str(cd_fixture / "disc.cue"),
            "-o", str(cd_fixture / "selected.chd"), "-c", "cdzl")
        run(args.chdman, "verify", "-i", str(cd_fixture / "selected.chd"))
        for kind in args.filesystem or ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(clean)) if kind == "fat32" else run("mkfs.exfat", str(clean))
            for case in args.case or CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(clean, image)
                selected_fixture = cd_fixture if case in CD_CASES else fixture
                run(BINARY, str(image), str(selected_fixture), "seed", case)
                before = digest(image)
                output = run(BINARY, str(image), str(selected_fixture), "check", case)
                assert f"PASS CHD Games check {case}" in output
                if case in ("inspect", "cancel-before", "connect-fail", "source-open-fail",
                            "source-read-fail", "source-short-read", "source-seek-fail",
                            "getfree-fail", "no-space", "cd-inspect"):
                    assert digest(image) == before, f"CHD {case} changed {kind} card"
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} CHD Games: {case}; source preserved, handles closed, fsck clean", flush=True)


if __name__ == "__main__":
    main()
