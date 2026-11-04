#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install actual release packages on FAT32/exFAT; never execute SH payloads."""
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
    parser.add_argument("--baseline", required=True, type=Path, help="Baseline KUI fixture with runtime.kui and any existing root readers; absent readers stay absent")
    parser.add_argument("--package", required=True, type=Path, help="Candidate .kup from a different clean build")
    parser.add_argument("--binary", type=Path, help="Prebuilt update-release-image harness")
    args = parser.parse_args()
    if not (args.baseline / "runtime.kui").is_file() or not args.package.is_file():
        parser.error("The baseline runtime.kui and candidate package must exist")
    if args.binary is not None and not args.binary.is_file():
        parser.error("Prebuilt harness does not exist: " + str(args.binary))
    source = fatfs_source()
    with tempfile.TemporaryDirectory(prefix="kui-real-update-") as temporary:
        base = Path(temporary)
        binary = args.binary or base / "update-release-image"
        if not binary.is_file():
            flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
                "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                "-Iinclude", "-I" + str(source), "-Isrc/dreamcast"]
            run(*flags, "tests/update_release_image.c", "src/core/update.c", "src/core/update_file.c",
                "src/core/update_boot.c", "src/core/update_handoff.c", "src/core/runtime_image.c",
                "src/core/runtime_file.c", "src/core/storage_policy.c", "src/core/hash.c", "src/core/data.c",
                "src/core/native_vmu_standard.c", "src/core/vmu_game_card.c",
                "src/core/vmu_persist_profile.c", "src/core/retail_image.c",
                "src/core/diskio.c", str(source / "ff.c"), str(source / "ffunicode.c"), "-o", str(binary))
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            print(kind + ": " + run(str(binary), str(image), str(args.baseline.resolve()),
                str(args.package.resolve())).strip(), flush=True)
    print("PASS actual release installation, inherited root module recognition/absence and rollback on both filesystems under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
