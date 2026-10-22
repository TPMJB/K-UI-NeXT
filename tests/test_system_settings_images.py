#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise independent system preferences recovery on real FAT32/exFAT images."""
from pathlib import Path
import hashlib
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/system-settings-image")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def main():
    with tempfile.TemporaryDirectory(prefix="kui-system-settings-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            before = digest(seed)
            run(BINARY, str(seed), "missing")
            assert digest(seed) == before, "loading missing settings wrote to the card"
            run(BINARY, str(seed), "seed")
            checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"
            run(checker, "-n", str(seed))
            for case in ("alternate", "v1-migrate", "v1-fallback", "v2-migrate", "corrupt", "truncated", "oversize", "version", "flags", "startup-app", "safe-area", "reserved",
                         "both-invalid", "invalid-save", "parent-is-file", "overflow", "read-fail", "save-read-fail", "write-fail", "sync-fail",
                         "readback-fail", "readback-corrupt", "readback-substitute"):
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                before = digest(image) if case in ("read-fail", "save-read-fail") else None
                output = run(BINARY, str(image), case)
                assert f"PASS system settings {case}" in output
                if before is not None:
                    assert digest(image) == before, "loading failed settings wrote to the card"
                if case not in ("write-fail", "sync-fail", "readback-fail"):
                    run(checker, "-n", str(image))
                print(f"PASS {kind} system settings: {case}, ripper settings preserved", flush=True)


if __name__ == "__main__":
    main()
