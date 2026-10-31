#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read strategies against real FAT32/exFAT images, codewords and durable jobs."""
import argparse
from pathlib import Path
import shutil
import tempfile

from make_recovery_vectors import sector
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
CASES = ("all-good", "windows", "partial", "spread", "single", "short-track", "audio-mixed", "audio-only", "all-bad",
         "wrong-fad", "bad-edc", "bad-pq", "bad-payload", "partial-invalid", "conflict",
         "anchor-bad", "anchor-different", "fatal", "fatal-anchor", "budget", "budget-after-approach",
         "stop-before-target", "stop-read", "stop-partial", "stop-before-approach", "stop-after-approach",
         "patch-write-fail", "patch-sync-fail", "track-write-fail", "guard", "unfinished",
         "invalid-new", "invalid-resume", "missing-clock", "mutually-exclusive", "defer-exclusive", "session")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=CASES, action="append", dest="cases")
    parser.add_argument("--filesystem", choices=("fat32", "exfat"), action="append", dest="filesystems")
    args = parser.parse_args()
    for binary in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat"):
        if not shutil.which(binary):
            raise SystemExit(f"Missing test prerequisite: {binary}")
    with tempfile.TemporaryDirectory(prefix="kui-read-trial-") as temporary:
        base = Path(temporary)
        fixture = base/"fixture"
        fixture.mkdir()
        for alternate in (False, True):
            prefix = "alternate-" if alternate else ""
            for number, start, end, extension in ((1, 150, 166, "bin"), (3, 45150, 45246, "bin")):
                (fixture/f"{prefix}track{number:02}.{extension}").write_bytes(
                    b"".join(sector(fad, fad+999 if alternate else fad) for fad in range(start, end)))
            (fixture/f"{prefix}track02.raw").write_bytes(bytes((i*31+5) & 255 for i in range(4*2352)))
        for kind in args.filesystems or ("fat32", "exfat"):
            seed = base/(kind+"-seed.img")
            with seed.open("wb") as out:
                out.truncate(96*1024*1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", "-c", "131072", str(seed))
            for case in args.cases or CASES:
                image = base/(kind+"-"+case+".img")
                shutil.copyfile(seed, image)
                output = run(str(ROOT/"build/salvage-read-trial-image"), str(image), str(fixture), case)
                assert f"PASS salvage read trial {case}" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} salvage read trial: {case}", flush=True)


if __name__ == "__main__":
    main()
