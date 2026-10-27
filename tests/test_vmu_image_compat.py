#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Existing VMU manager/import/restore accepts the new canonical images."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*arguments):
    environment = dict(os.environ)
    environment["ASAN_OPTIONS"] = "detect_leaks=0"
    result = subprocess.run([str(value) for value in arguments], env=environment,
                            text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{arguments}: exit {result.returncode}\n{result.stdout}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        default=ROOT / "build/test-vmu-image-compat")
    parser.add_argument("--harness-binary",type=Path,
                        default=ROOT / "build/vmu-harness/host-sequence")
    args = parser.parse_args()
    for tool in ("mkfs.fat", "fsck.fat", "mkfs.exfat", "fsck.exfat"):
        if not shutil.which(tool):
            raise SystemExit(f"Missing test prerequisite: {tool}")
    with tempfile.TemporaryDirectory(prefix="kui-vmu-compat-") as temporary:
        for filesystem in ("fat32", "exfat"):
            image = Path(temporary) / f"{filesystem}.img"
            with image.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            if filesystem == "fat32":
                run("mkfs.fat", "-F", "32", image)
            else:
                run("mkfs.exfat", image)
            output = run(args.harness_binary,image,"create")
            assert "PASS native harness host sequence" in output, output
            output = run(args.binary, image,"harness")
            assert "PASS VMU image compatibility" in output, output
            run("fsck.fat" if filesystem == "fat32" else "fsck.exfat", "-n", image)
            print(f"PASS {filesystem}: canonical blank/populated VMD, current manager, "
                  "actual harness save, VMS CRC parser, extract/import, verified restore, fsck", flush=True)


if __name__ == "__main__":
    main()
