#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact root repair and old-bank archival on real FAT32/exFAT images."""
import argparse
import os
from pathlib import Path
import shlex
import tempfile

from test_games_library_images import fatfs_source, private_formatter
from test_update_images import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path, help="Existing root KUI directory")
    parser.add_argument("--old-package", required=True, type=Path, help="Actual old26 GUI package")
    parser.add_argument("--runtime", required=True, type=Path, help="Exact delivered replacement runtime.kui")
    parser.add_argument("--package", required=True, type=Path, help="Matching new34 GUI package for future installation")
    args = parser.parse_args()
    for path in (args.baseline / "runtime.kui", args.old_package, args.runtime, args.package):
        if not path.is_file():
            parser.error("Required fixture does not exist: " + str(path))
    source = fatfs_source()
    with tempfile.TemporaryDirectory(prefix="kui-root-migration-") as temporary:
        base = Path(temporary)
        binary = base / "update-root-migration"
        flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-Iinclude", "-I" + str(source), "-Isrc/dreamcast"]
        run(*flags, "tests/update_root_migration_image.c", "src/core/update.c", "src/core/update_file.c",
            "src/core/update_boot.c", "src/core/update_handoff.c", "src/core/runtime_image.c",
            "src/core/runtime_file.c", "src/core/storage_policy.c", "src/core/hash.c", "src/core/data.c",
            "src/core/boot_image.c", "src/core/boot_volume.c", "src/core/storage_probe.c",
            "src/core/diskio.c", str(source / "ff.c"), str(source / "ffunicode.c"), "-o", str(binary))
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            print(kind + ": " + run(str(binary), str(image), str(args.baseline.resolve()),
                str(args.old_package.resolve()), str(args.runtime.resolve()), str(args.package.resolve())).strip(), flush=True)
    print("PASS root-only manual repair and future updates on both filesystems under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
