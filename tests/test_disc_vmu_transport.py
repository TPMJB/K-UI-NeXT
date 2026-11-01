#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the same independent disc adapter for both fixed serial backends."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="disc-vmu-transport-") as work:
        for transport in (0, 1):
            output = Path(work) / f"adapter-{transport}"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g",
                "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-fno-pie", "-no-pie", "-Iinclude", "-Isrc/loader",
                "-DKUI_DISC_VMU_TRANSPORT_TEST=1", "-DKUI_NATIVE_VMU_ONLY=1",
                "-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1", f"-DKUI_RETAIL_TRANSPORT={transport}",
                "tests/test_disc_vmu_transport.c", "src/loader/disc_vmu_transport.c",
                "src/loader/disc_vmu_storage.c", "-o", str(output),
            ], cwd=ROOT, check=True)
            subprocess.run([str(output)], check=True,
                           env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})


if __name__ == "__main__":
    main()
