#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only console backups and publication failures on real FAT32/exFAT."""
from pathlib import Path
import hashlib
import shutil
import tempfile
from test_images import run
ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/maintenance-image")
def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()
def main():
    cases = ("inspect", "good", "bios", "bad-layout", "identity-fail", "identity-change",
             "source-fail", "source-change", "verify-source-change", "write-fail", "sync-fail",
             "readback-fail", "readback-corrupt", "cancel-before", "cancel-writing", "cancel-verifying")
    with tempfile.TemporaryDirectory(prefix="kui-maintenance-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            for case in cases:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                before = digest(image) if case in ("inspect", "bad-layout", "identity-fail", "cancel-before") else None
                output = run(BINARY, str(image), case)
                assert f"PASS maintenance {case}" in output
                if before is not None:
                    assert digest(image) == before, "inspection or early refusal wrote the card"
                if case not in ("write-fail", "sync-fail"):
                    run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                print(f"PASS {kind} maintenance: {case}", flush=True)
if __name__ == "__main__":
    main()
