#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check persisted creation/modification dates with the real FAT32/exFAT code."""
from pathlib import Path
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="kui-clock-") as temp:
        for kind in ("fat32", "exfat"):
            image = Path(temp) / f"{kind}.img"
            with image.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(image)) if kind == "fat32" else run("mkfs.exfat", str(image))
            output = run(str(ROOT / "build/clock-image"), str(image))
            assert "PASS timestamps:" in output
            run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
            print(f"PASS {kind} created/modified timestamps and invalid RTC fallback", flush=True)


if __name__ == "__main__":
    main()
