#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The optional existing GUI module provider cannot shadow pinned updates."""
import os
from pathlib import Path
import shlex
import tempfile

from test_games_library_images import fatfs_source, private_formatter
from test_update_images import run


def main():
    source = fatfs_source()
    with tempfile.TemporaryDirectory(prefix="kui-runtime-provider-") as temporary:
        base = Path(temporary)
        binaries = []
        flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-Iinclude", "-I" + str(source), "-Isrc/dreamcast"]
        for provider in (1, 0):
            binary = base / ("provider-present" if provider else "provider-absent")
            run(*flags, "-DKUI_TEST_MODULE_PROVIDER=" + str(provider),
                "tests/test_runtime_provider_image.c", "src/core/update.c", "src/core/update_file.c",
                "src/core/update_boot.c", "src/core/update_handoff.c", "src/core/runtime_image.c",
                "src/core/runtime_file.c", "src/core/storage_policy.c", "src/core/hash.c",
                "src/core/data.c", "src/core/diskio.c", str(source / "ff.c"),
                str(source / "ffunicode.c"), "-Wl,--wrap=f_open", "-o", str(binary))
            binaries.append(binary)
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            for binary in binaries:
                print(kind + ": " + run(str(binary), str(image)).strip(), flush=True)
    print("PASS optional runtime-provider routing on both filesystems under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
