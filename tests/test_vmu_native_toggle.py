#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run actual General worker On/Off, physical Maple and retained-cache tests."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCES=("src/core/vmu_native_heap.c", "src/core/vmu_native_runtime.c",
    "src/core/vmu_native_layout.c", "src/core/vmu_native_live.c",
    "src/core/vmu_native_lifetime.c", "src/core/vmu_native_profile.c",
    "src/core/vmu_native_discovery.c", "src/core/vmu_native_patch.c",
    "src/core/hash.c", "src/core/data.c", "src/core/vmu_durable.c",
    "src/core/vmu_store.c", "src/core/vmu_emulation.c", "src/core/retail_image.c",
    "src/loader/vmu_readonly_overlay.c", "src/loader/vmu_volatile_chain.c",
    "src/loader/sd_reader.c", "src/loader/vmu_backing.c", "src/dreamcast/sci_sd_storage.c")
DEFINES=("KUI_VMU_EXPERIMENTAL", "KUI_VMU_NATIVE_PERSISTENT_PROBE",
    "KUI_VMU_NATIVE_CACHED_CODE", "KUI_NATIVE_DUAL_READERS",
    "KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION", "KUI_NATIVE_SPLIT_CONTROLLER_GENERAL",
    "KUI_NATIVE_SPLIT_CONTROLLER_PREFETCH", "KUI_NATIVE_SPLIT_CONTROLLER_STREAM_GUARD",
    "KUI_NATIVE_GENERAL_LEAN_LAYOUT")

class NativeVmuToggle(unittest.TestCase):
    def test_worker_on_off(self):
        with tempfile.TemporaryDirectory(prefix="kui-vmu-toggle-") as temporary:
            binary=Path(temporary)/"worker"
            # The real TICK entry is linked at its owned SH4 address. ASan's
            # shadow cannot share that address; UBSan covers this fixture.
            cmd=[*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
                "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-ffunction-sections",
                "-fdata-sections", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                "-mcmodel=large", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
                "-fsanitize=undefined", "-fno-sanitize-recover=all", "-Iinclude", "-Isrc/loader",
                *("-D"+name+"=1" for name in DEFINES), "tests/test_vmu_native_toggle.c",
                *SOURCES, "-Wl,--gc-sections", "-Wl,--section-start=.isolation_tick=0x8c71e000",
                "-o", str(binary)]
            subprocess.run(cmd,cwd=ROOT,check=True)
            result=subprocess.run([str(binary)],check=True,text=True,capture_output=True)
            self.assertIn("VMU toggle General worker:",result.stdout)
            self.assertIn("zero virtual-card SD I/O",result.stdout)
            print(result.stdout,end="")

if __name__=="__main__":
    unittest.main()
