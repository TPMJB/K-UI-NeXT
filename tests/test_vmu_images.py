#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FatFs backups with read-only VMU transport and injected block faults."""
from pathlib import Path
import hashlib
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/test-vmu-app")
READ_ONLY = ("missing", "empty", "list", "slot-d2", "invalid-slot", "cycle", "range", "root-layout", "duplicate-chain",
             "short-chain", "bad-header", "vmu-read-fail", "short-block", "oversize-block",
             "device-change", "cancel-start", "cancel-read", "stale-list", "unlisted", "invalid-row", "invalid-page", "connect-fail")
BACKUPS = ("backup-selected", "selected-page", "backup-all", "unsafe-name", "game-save", "contents-change", "write-fail",
           "sync-fail", "readback-fail", "readback-corrupt", "cancel-write")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def main():
    with tempfile.TemporaryDirectory(prefix="kui-vmu-") as temporary:
        base = Path(temporary)
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            run(BINARY, str(seed), "seed")
            original = digest(seed)
            checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"
            for case in READ_ONLY + BACKUPS:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                assert f"PASS VMU {case}" in run(BINARY, str(image), case)
                if case in READ_ONLY:
                    assert digest(image) == original, f"{case} unexpectedly wrote to the SD card"
                if case not in ("write-fail", "sync-fail"):
                    run(checker, "-n", str(image))
                print(f"PASS {kind} VMU: {case}", flush=True)


if __name__ == "__main__":
    main()
