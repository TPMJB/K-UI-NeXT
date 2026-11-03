#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""File Manager operations on real FAT32/exFAT images: listing and paging,
copy with read-back, Stop, write and read-back failures, a full card, move,
delete, rename, new folder, details and pictures. fsck checks every image."""
from pathlib import Path
import shutil
import tempfile
from test_images import run
from test_games_library_images import private_formatter

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/files-image")
CASES = ("list", "copy", "cancel", "write-fail", "verify-fail", "full", "move", "delete", "rename",
         "mkdir", "picture")


def main():
    with tempfile.TemporaryDirectory(prefix="kui-files-") as temp:
        base = Path(temp)
        formatter = None if shutil.which("mkfs.fat") and shutil.which("mkfs.exfat") else private_formatter(base)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            if formatter:
                run(str(formatter), kind, str(clean))
            else:
                with clean.open("wb") as stream:
                    stream.truncate(96 * 1024 * 1024)
                run("mkfs.fat", "-F", "32", str(clean)) if kind == "fat32" else run("mkfs.exfat", str(clean))
            for case in CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(clean, image)
                output = run(BINARY, str(image), case)
                assert f"PASS File Manager image {case}" in output, output[-2000:]
                checker = shutil.which("fsck.fat" if kind == "fat32" else "fsck.exfat")
                if checker:
                    run(checker, "-n", str(image))
                image.unlink()
                print(f"PASS {kind} File Manager: {case}", flush=True)

        if not shutil.which("fsck.fat") or not shutil.which("fsck.exfat"):
            print("Filesystem behavior passed on real FatFs images; external fsck unavailable and not claimed.", flush=True)


if __name__ == "__main__":
    main()
