#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Console Wi-Fi updates from real FAT32/exFAT, including protocol faults."""
from pathlib import Path
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="kui-wifi-update-") as temp:
        for kind in ("fat32", "exfat"):
            card = Path(temp) / f"{kind}.img"
            with card.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            if kind == "fat32":
                run("mkfs.fat", "-F", "32", str(card))
            else:
                run("mkfs.exfat", str(card))
            result = run(str(ROOT / "build/test-wifi-update"), str(card))
            assert "PASS Wi-Fi updater: real card reads" in result
            run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(card))
            print(f"PASS {kind} Wi-Fi firmware updater: all integrity, cancel, fault and reboot cases", flush=True)


if __name__ == "__main__":
    main()
