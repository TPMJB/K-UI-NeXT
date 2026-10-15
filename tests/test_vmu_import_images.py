#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""External save previews/staging on real FAT32/exFAT, with original inputs
unchanged and staged bundles accepted by the existing physical restore preview.
The C harness asserts every physical VMU write; import itself cannot use Maple.
"""
from pathlib import Path
import hashlib
import shutil
import tempfile
from test_images import run
ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/vmu-import-image")
def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()
def main():
    cases = ("vms", "padded", "vmi", "vmi-game", "dci", "dci-game", "dci-ignored-first", "uppercase", "metadata-highbit",
             "qualified", "deep", "preview-only", "bad-crc", "long-name", "missing-companion",
             "source-change", "vmi-change", "path-change", "no-preview", "unsafe", "other-drive",
             "media-fail", "cancel-before", "cancel-during")
    readonly = ("preview-only", "bad-crc", "long-name", "missing-companion", "path-change", "no-preview",
                "unsafe", "other-drive", "media-fail", "cancel-before", "cancel-during")
    with tempfile.TemporaryDirectory(prefix="kui-vmu-import-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            for case in cases:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                run(BINARY, str(image), case, "prepare")
                before = digest(image) if case in readonly else None
                output = run(BINARY, str(image), case, "run")
                assert f"PASS VMU import {case}" in output
                if before is not None:
                    assert digest(image) == before, "read-only preview/refusal changed SD storage"
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} VMU import: {case}", flush=True)
if __name__ == "__main__":
    main()
