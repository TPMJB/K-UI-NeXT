#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise report publication/cancellation/failures using real FAT32/exFAT."""
from pathlib import Path
import hashlib
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/report-image")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    with tempfile.TemporaryDirectory(prefix="kui-reports-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            run(BINARY, str(seed), "seed")
            for case in ("save", "cancel-before", "cancel-during", "cancel-publish", "write-fail", "sync-fail", "full"):
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                before = digest(image) if case == "cancel-before" else None
                output = run(BINARY, str(image), case)
                assert "PASS report" in output
                assert "SAVED DATA VERIFIED" not in output
                if case == "cancel-before":
                    assert digest(image) == before
                if case not in ("write-fail", "sync-fail"):
                    run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                print(f"PASS {kind} reports: {case}, preserved old reports/capture files", flush=True)


if __name__ == "__main__":
    main()
