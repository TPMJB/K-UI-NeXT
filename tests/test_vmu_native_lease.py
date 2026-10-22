#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual native low guards plus actual scoped SCI loan on the host."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/loader/vmu_maple_observe_resident.c").read_text()
guard = re.search(r"/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/", source, re.S)
assert guard and "native_resident_transaction" in guard.group(1)
with tempfile.TemporaryDirectory(prefix="kui-native-sci-lease-") as tmp:
    directory = Path(tmp)
    (directory / "vmu_native_resident_guards.h").write_text(guard.group(1))
    for mode, flags in (
        ("fixed2", ["-DKUI_VMU_READONLY_PROOF=2"]),
        ("fixed4", ["-DKUI_VMU_READONLY_PROOF=4"]),
        ("native", ["-DKUI_VMU_READONLY_PROOF=0", "-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1",
                    "-DKUI_VMU_SD_LEASE_NATIVE_HOST_TEST=1"]),
    ):
        binary = directory / mode
        subprocess.run([
            os.environ.get("HOST_CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wpedantic", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "src/loader"), "-I", str(directory),
            *flags, str(ROOT / "tests/test_vmu_sd_lease.c"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
