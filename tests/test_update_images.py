#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline update codecs and real FAT32/exFAT installation fault boundaries."""
import os
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile
from test_games_library_images import private_formatter, fatfs_source

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True,
        env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"})
    if result.returncode:
        print(result.stdout, end="")
        print(result.stderr, end="")
        raise SystemExit(f"Updater check failed ({result.returncode}): {args}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, help="Use a prebuilt update-image harness")
    parser.add_argument("--codec", type=Path, help="Use a prebuilt update codec harness")
    parser.add_argument("--skip-codec", action="store_true")
    parser.add_argument("--boot-binary", type=Path, help="Also test a prebuilt update-boot harness")
    args = parser.parse_args()
    for supplied in (args.binary, args.codec, args.boot_binary):
        if supplied is not None and not supplied.is_file():
            parser.error("Prebuilt harness does not exist: " + str(supplied))
    source = fatfs_source()
    if not (source / "ff.c").is_file():
        raise SystemExit("Restore the pinned FatFs sources before running updater filesystem tests")
    flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
        "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        "-Iinclude", "-I" + str(source), "-Isrc/dreamcast"]
    pure = ["src/core/update.c", "src/core/hash.c", "src/core/data.c"]
    file_source = next((str(path.relative_to(ROOT)) for path in
        (ROOT / "src/apps/update_file.c", ROOT / "src/core/update_file.c") if path.is_file()), None)
    if not file_source:
        raise SystemExit("Updater file service source has not been written yet")
    with tempfile.TemporaryDirectory(prefix="kui-update-tests-") as temporary:
        base = Path(temporary)
        codec = args.codec or (ROOT / "build/test-update" if (ROOT / "build/test-update").is_file() else base / "update-codec")
        image = args.binary or (ROOT / "build/update-image" if (ROOT / "build/update-image").is_file() else base / "update-image")
        boot = args.boot_binary or (ROOT / "build/update-boot" if (ROOT / "build/update-boot").is_file() else None)
        if not args.skip_codec:
            if not codec.is_file():
                run(*flags, "tests/test_update.c", *pure, "-o", str(codec))
            print(run(str(codec)), end="", flush=True)
        if not image.is_file():
            run(*flags, "tests/update_image.c", *pure, file_source, "src/core/runtime_image.c",
                "src/core/diskio.c", str(source / "ff.c"), str(source / "ffunicode.c"),
                "-Wl,--wrap=f_open,--wrap=f_close,--wrap=f_write,--wrap=f_getfree", "-o", str(image))
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            disk = base / (kind + ".img")
            run(str(formatter), kind, str(disk))
            print(kind + ": " + run(str(image), str(disk)).strip(), flush=True)
            if boot:
                print(kind + " boot: " + run(str(boot), str(disk)).strip(), flush=True)
    print("PASS updater codec and both real filesystems under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
