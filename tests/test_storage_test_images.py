#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verified tests preserve user data and clean up their scratch on real volumes."""
from pathlib import Path
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/storage-test-image")
CASES = (
    "quick", "compare", "compare-five", "soak", "invalid", "cancel-before",
    "cancel-write", "cancel-read", "write", "short-write", "read", "short-read",
    "corrupt", "stale", "sync", "close-write", "close-read", "mount", "remount",
    "unmount", "unlink", "free-error", "full", "collision", "size",
)


def main():
    with tempfile.TemporaryDirectory(prefix="kui-storage-tests-") as directory:
        base = Path(directory)
        for kind in ("exfat", "fat32"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            if kind == "fat32":
                run("mkfs.fat", "-F", "32", str(seed))
            else:
                run("mkfs.exfat", str(seed))
            for case in CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                output = run(BINARY, str(image), case)
                assert f"PASS storage-test {case}" in output, output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                print(f"PASS {kind}: {case}", flush=True)
                image.unlink()


if __name__ == "__main__":
    main()
