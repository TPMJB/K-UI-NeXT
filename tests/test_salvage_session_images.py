#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RAM-only salvage receipts on actual FAT32/exFAT storage and fault paths."""
import argparse
from pathlib import Path
import shutil
import tempfile

from make_recovery_vectors import sector
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
CASES = ("warm-clean", "warm-large", "warm-one-pass", "warm-reboot", "warm-epoch", "warm-card", "warm-volume", "warm-wrong-disc", "warm-invalidate", "warm-disabled", "warm-null-card",
         "warm-guard", "warm-header", "warm-index", "warm-index-length", "warm-journal-tail", "warm-journal-length",
         "warm-track-length", "warm-patch-length", "warm-track-data", "warm-patch-data", "warm-index-read-fail",
         "warm-index-short-read", "warm-cancel-preflight", "warm-close-fail", "warm-sync-fail", "warm-unmount-fail",
         "warm-pending-candidate", "warm-new-job", "warm-incomplete")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=CASES, action="append", dest="cases")
    parser.add_argument("--filesystem", choices=("fat32", "exfat"), action="append", dest="filesystems")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kui-salvage-session-") as temporary:
        base = Path(temporary)
        fixture = base/"fixture"
        fixture.mkdir()
        (fixture/"track01.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(150, 166)))
        (fixture/"track02.raw").write_bytes(bytes((i*31+5) & 255 for i in range(4*2352)))
        (fixture/"track03.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(45150, 45214)))
        (fixture/"alternate-anchor.raw").write_bytes(sector(45150, 999))
        large = base/"large-fixture"
        large.mkdir()
        for name in ("track01.bin", "track02.raw"):
            shutil.copyfile(fixture/name, large/name)
        (large/"track03.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(45150, 45150+2049+16)))
        for kind in args.filesystems or ("fat32", "exfat"):
            seed = base/(kind+"-seed.img")
            with seed.open("wb") as out:
                out.truncate(96*1024*1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", "-c", "131072", str(seed))
            for case in args.cases or CASES:
                image = base/(kind+"-"+case+".img")
                shutil.copyfile(seed, image)
                output = run(str(ROOT/"build/salvage-session-image"), str(image), str(large if case == "warm-large" else fixture), case)
                assert f"PASS salvage session {case}" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} salvage session: {case}", flush=True)


if __name__ == "__main__":
    main()
