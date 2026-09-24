#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT Games reads, injected faults and whole-card preservation."""
from pathlib import Path
import hashlib
import shutil
import tempfile
from test_images import run
from games_fixture import make_fixture

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-image")
CASES = ("valid", "valid-session-size", "listing", "missing-root", "missing-track", "truncated-audio",
         "truncated-boot", "unsafe-gdi", "bad-ip", "unsupported-format",
         "connect-fail", "mount-fail", "open-fail", "read-fail", "short-read",
         "seek-fail", "close-fail", "dir-read-fail", "dir-close-fail",
         "cancel-before", "cancel-read", "cancel-list", "invalid-root",
         "invalid-path", "offset-limit", "long-root")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def main():
    with tempfile.TemporaryDirectory(prefix="kui-games-") as temp:
        base = Path(temp)
        fixture = base / "original-gdi"
        make_fixture(fixture)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            if kind == "fat32":
                run("mkfs.fat", "-F", "32", str(clean))
            else:
                run("mkfs.exfat", str(clean))
            for case in CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(clean, image)
                run(BINARY, str(image), str(fixture), "seed", case)
                before = digest(image)
                output = run(BINARY, str(image), str(fixture), "check", case)
                assert f"PASS Games check {case}; no active-operation writes" in output
                assert digest(image) == before, f"Games changed {kind} card image in {case}"
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} Games: {case}; whole-card SHA256 unchanged", flush=True)


if __name__ == "__main__":
    main()
