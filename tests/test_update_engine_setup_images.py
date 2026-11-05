#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual old-bank to engine2-root setup and FULL KUP boot on FAT32/exFAT."""
import argparse
import os
from pathlib import Path
import shlex
import tempfile

from test_games_library_images import fatfs_source, private_formatter
from test_update_images import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path, help="Existing physical root KUI directory")
    parser.add_argument("--old-package", required=True, type=Path, help="Actual legacy26 package")
    parser.add_argument("--runtime", required=True, type=Path, help="Exact engine2 root GUI")
    parser.add_argument("--package", required=True, type=Path, help="Exact new FULL KUP")
    args = parser.parse_args()
    for path in (args.baseline / "runtime.kui", args.old_package, args.runtime, args.package):
        if not path.is_file():
            parser.error("Missing fixture: " + str(path))
    source = fatfs_source()
    with tempfile.TemporaryDirectory(prefix="kui-engine2-setup-") as temporary:
        base = Path(temporary)
        inventory = base / "root-inventory.txt"
        members = []
        for path in sorted(args.baseline.rglob("*")):
            if path.is_symlink():
                parser.error("Baseline must not contain symlinks: " + str(path))
            if path.is_file():
                members.append(path.relative_to(args.baseline).as_posix())
        inventory.write_text("\n".join(members) + "\n", encoding="ascii")
        binary = base / "engine2-setup"
        flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-Iinclude", "-I" + str(source), "-Isrc/dreamcast"]
        run(*flags, "tests/update_engine_setup_image.c", "src/core/update.c", "src/core/update_file.c",
            "src/core/update_boot.c", "src/core/update_software_file.c", "src/core/update_handoff.c",
            "src/core/runtime_image.c", "src/core/runtime_file.c", "src/core/storage_policy.c",
            "src/core/hash.c", "src/core/data.c", "src/core/boot_image.c", "src/core/boot_volume.c",
            "src/core/storage_probe.c", "src/core/diskio.c", str(source / "ff.c"), str(source / "ffunicode.c"),
            "-Wl,--wrap=f_open,--wrap=f_stat,--wrap=f_read", "-o", str(binary))
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            print(kind + ": " + run(str(binary), str(image), str(args.baseline.resolve()),
                str(args.old_package.resolve()), str(args.runtime.resolve()), str(args.package.resolve()), str(inventory)).strip(), flush=True)
    print("PASS actual engine2 setup and exact FULL update on both filesystems under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
