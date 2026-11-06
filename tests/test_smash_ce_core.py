#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the production unified collection and captured CE identity cores."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="smash-ce-core-") as temporary:
        executable = Path(temporary) / "test-ce-core"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wpedantic", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-pie", "-no-pie",
                        "-Iinclude", "tests/test_smash_ce_core.c", "src/core/smash_ce_child.c",
                        "src/core/smash_menu_collection.c", "src/core/smash_menu_loader.c",
                        "src/core/retail_image.c", "-o", str(executable)], cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
