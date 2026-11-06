#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise mounted startup-report writes through actual FAT32 and exFAT."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from test_games_library_images import private_formatter, fatfs_source

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True,
        env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"})


def main():
    source = fatfs_source()
    if not (source / "ff.c").is_file():
        raise SystemExit("Restore the pinned FatFs sources before running startup report tests")
    build = ROOT / "build/startup-report-tests"
    build.mkdir(parents=True, exist_ok=True)
    executable = build / "startup-report"
    flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
        "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        "-Iinclude", "-I" + str(source)]
    run(*flags, "tests/test_startup_report.c", "src/core/startup_report.c", "src/core/diskio.c",
        "src/core/data.c", str(source / "ff.c"), str(source / "ffunicode.c"),
        "-Wl,--wrap=f_open,--wrap=f_write,--wrap=f_sync,--wrap=f_close,--wrap=f_rename,--wrap=f_unlink,--wrap=f_mount",
        "-o", str(executable))
    with tempfile.TemporaryDirectory(prefix="kui-startup-report-") as temporary:
        base = Path(temporary)
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            run(str(executable), str(image))
    print("PASS startup report FAT32/exFAT under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
