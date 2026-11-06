#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual held-report VMU page and existing read-ahead counters."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    "src/core/vmu_native_heap.c", "src/core/vmu_native_runtime.c",
    "src/core/vmu_native_layout.c", "src/core/vmu_native_live.c",
    "src/core/vmu_native_lifetime.c", "src/core/vmu_native_profile.c",
    "src/core/vmu_native_discovery.c", "src/core/vmu_native_patch.c",
    "src/core/hash.c", "src/core/data.c", "src/core/vmu_durable.c",
    "src/core/vmu_store.c", "src/core/vmu_emulation.c", "src/core/retail_image.c",
    "src/loader/vmu_readonly_overlay.c", "src/loader/vmu_volatile_chain.c",
    "src/loader/sd_reader.c", "src/loader/vmu_backing.c", "src/dreamcast/sci_sd_storage.c",
)
with tempfile.TemporaryDirectory(prefix="kui-held-reader-report-") as work:
    for a1 in (False, True):
        output = Path(work) / ("report-a1" if a1 else "report-a2")
        flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                 "-ffunction-sections", "-fdata-sections", "-fsanitize=undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                 "-mcmodel=large", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
                 "-Iinclude", "-Isrc/loader", "-DKUI_VMU_EXPERIMENTAL=1",
                 "-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1", "-DKUI_VMU_NATIVE_CACHED_CODE=1",
                 "-DKUI_NATIVE_DUAL_READERS=1", "-DKUI_NATIVE_SPLIT_CONTROLLER_ISOLATION=1",
                 "-DKUI_NATIVE_SPLIT_CONTROLLER_GENERAL=1", "-DKUI_NATIVE_SPLIT_CONTROLLER_PREFETCH=1",
                 "-DKUI_NATIVE_SPLIT_CONTROLLER_STREAM_GUARD=1", "-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1"]
        if a1:
            flags.append("-DKUI_VMU_VIRTUAL_A1=1")
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), *flags,
                        "tests/test_vmu_native_held_reader_report.c", *SOURCES,
                        "-Wl,--gc-sections", "-Wl,--section-start=.isolation_tick=0x8c71e000",
                        "-o", str(output)], cwd=ROOT, check=True)
        subprocess.run([str(output)], check=True)
