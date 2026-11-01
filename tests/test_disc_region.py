#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Region preference corruption/write cuts and independently owned SD lifetime."""
from pathlib import Path
import os
import shlex
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="disc-region-") as work:
        output = Path(work) / "test-region"
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-Iinclude", "-Isrc/dreamcast", "-I.deps/fatfs/source", "-DKUI_DISC_REGION_TEST=1",
            "tests/test_disc_region.c", "src/apps/disc_region.c", "src/core/data.c", "-o", str(output),
        ], cwd=ROOT, check=True)
        subprocess.run([str(output)], check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
        if not (shutil.which("mkfs.fat") and shutil.which("mkfs.exfat")):
            print("SKIP disc region filesystem boundaries: mkfs.fat/mkfs.exfat unavailable")
            return
        image_test = Path(work) / "test-region-image"
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-Iinclude", "-Isrc/dreamcast", "-I.deps/fatfs/source",
            "tests/disc_region_image.c", "src/apps/disc_region.c", "src/core/data.c",
            "src/core/diskio.c", "src/core/command.c", "src/core/clock.c",
            ".deps/fatfs/source/ff.c", ".deps/fatfs/source/ffunicode.c", "-o", str(image_test),
        ], cwd=ROOT, check=True)
        for kind, cluster in (("fat32", None), ("exfat", "4K"), ("exfat", "128K")):
            volume = Path(work) / "volume.fs"
            with volume.open("wb") as out:
                out.truncate(64 * 1024 * 1024)
            command = ["mkfs.fat", "-F", "32", "-s", "1", "-h", "2048"] if kind == "fat32" else [
                "mkfs.exfat", "-c", cluster]
            subprocess.run([*command, str(volume)], capture_output=True, check=True)
            payload = volume.read_bytes();mbr = bytearray(512)
            struct.pack_into("<B3xB3xII", mbr, 446, 0, 0x0c if kind == "fat32" else 7, 2048, len(payload)//512)
            mbr[510:512] = b"\x55\xaa"
            image = Path(work) / "disk.img"
            image.write_bytes(mbr + bytes(2048*512-512) + payload + bytes(1024*1024))
            subprocess.run([str(image_test), str(image)], check=True,
                           env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})


if __name__ == "__main__":
    main()
