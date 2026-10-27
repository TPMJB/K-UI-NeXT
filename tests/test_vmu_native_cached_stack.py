#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual private-stack C guards in cached and legacy configurations."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="kui-native-stack-") as temporary:
        for cached in (0, 1):
            binary = Path(temporary) / f"stack-{cached}"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra",
                "-ffunction-sections", "-fdata-sections", "-Iinclude",
                f"-DKUI_VMU_NATIVE_CACHED_CODE={cached}",
                "tests/test_vmu_native_cached_stack.c", "-Wl,--gc-sections", "-o", str(binary)],
                cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
