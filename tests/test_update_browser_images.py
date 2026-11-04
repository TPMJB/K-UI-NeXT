#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the read-only updater browser on real FAT32 and exFAT images."""
import argparse
import os
from pathlib import Path
import shlex
import tempfile
from test_games_library_images import fatfs_source, private_formatter
from test_update_images import run

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, help="Use an explicitly prebuilt update-browser harness")
    args = parser.parse_args()
    if args.binary is not None and not args.binary.is_file():
        parser.error("Prebuilt browser harness does not exist: " + str(args.binary))
    source = fatfs_source()
    if not (source / "ff.c").is_file():
        raise SystemExit("Restore the pinned FatFs sources before running browser filesystem tests")
    with tempfile.TemporaryDirectory(prefix="kui-update-browser-") as temporary:
        base = Path(temporary)
        binary = args.binary.resolve() if args.binary else base / "update-browser-image"
        if args.binary is None:
            flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
                "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                "-Iinclude", "-I" + str(source), "-Isrc/dreamcast"]
            run(*flags, "tests/update_browser_image.c", "src/core/update_browser.c", "src/core/diskio.c",
                "src/core/data.c", str(source / "ff.c"), str(source / "ffunicode.c"), "-o", str(binary))
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            print(kind + ": " + run(str(binary), str(image)).strip(), flush=True)
    print("PASS updater browser FAT32 and exFAT read-only integration under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
