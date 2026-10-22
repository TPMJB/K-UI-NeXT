#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise destination records and directory browsing on FAT32/exFAT images."""
from pathlib import Path
import hashlib
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/destination-image")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def main():
    with tempfile.TemporaryDirectory(prefix="kui-destination-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            before = digest(seed)
            run(BINARY, str(seed), "missing")
            assert digest(seed) == before, "loading missing destination wrote to the card"
            run(BINARY, str(seed), "seed")
            checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"
            run(checker, "-n", str(seed))
            for case in ("alternate", "corrupt", "truncated", "oversize", "version", "bad-path", "bad-padding",
                         "both-invalid", "directories", "invalid-save", "overflow", "read-fail", "save-read-fail",
                         "write-fail", "sync-fail", "readback-fail", "readback-corrupt"):
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                before = digest(image) if case in ("read-fail", "save-read-fail", "invalid-save") else None
                output = run(BINARY, str(image), case)
                assert f"PASS destination {case}" in output
                if before is not None:
                    assert digest(image) == before, "refused destination operation wrote to the card"
                if case not in ("write-fail", "sync-fail", "readback-fail"):
                    run(checker, "-n", str(image))
                print(f"PASS {kind} destination: {case}, settings/capture files preserved", flush=True)


if __name__ == "__main__":
    main()
