#!/usr/bin/env python3
"""Run the real native worker fixture with capture off, split and full submit."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = """src/core/vmu_native_heap.c src/core/vmu_native_runtime.c
src/core/vmu_native_layout.c src/core/vmu_native_live.c src/core/vmu_native_lifetime.c
src/core/vmu_native_profile.c src/core/vmu_native_discovery.c src/core/vmu_native_patch.c
src/core/hash.c src/core/data.c src/core/vmu_durable.c src/core/vmu_store.c
src/core/vmu_emulation.c src/core/retail_image.c src/loader/vmu_readonly_overlay.c
src/loader/vmu_volatile_chain.c src/loader/sd_reader.c src/loader/vmu_backing.c
src/dreamcast/sci_sd_storage.c""".split()
FLAGS = """-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
-ffunction-sections -fdata-sections -fsanitize=address,undefined
-fno-omit-frame-pointer -fno-pie -no-pie -Iinclude -Isrc/loader
-DKUI_VMU_EXPERIMENTAL=1 -DKUI_VMU_NATIVE_PERSISTENT_PROBE=1
-DKUI_VMU_NATIVE_CACHED_CODE=1 -DKUI_NATIVE_DUAL_READERS=1
-DKUI_NATIVE_RA_TICK_OFF_ISOLATION=1 -DKUI_NATIVE_SPLIT_CONTROLLER_ISOLATION=1
-DKUI_NATIVE_SPLIT_CONTROLLER_GENERAL=1 -DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1
-DKUI_NATIVE_READAHEAD=1""".split()

def main():
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    with tempfile.TemporaryDirectory(prefix="native-perf-") as temporary:
        for capture, full, reader in ((0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 0, 1)):
            output = str(Path(temporary) / f"capture{capture}-full{full}-reader{reader}")
            source = ("tests/test_vmu_native_perf_capture.c" if capture else
                      "tests/test_vmu_native_split_controller.c")
            extra = (["-DKUI_RETAIL_ASYNC_TEST=1", "-DKUI_VMU_NATIVE_BACKGROUND=1",
                      "-DKUI_NATIVE_READER_DIAGNOSTIC=1", "-DKUI_NATIVE_REQUEST_TIMING=1",
                      "-DKUI_NATIVE_IRQ_REARM=1", "-DKUI_NATIVE_COOP_PUMP=1"] if reader else [])
            subprocess.run([os.environ.get("CC", "cc"), *FLAGS, *extra,
                            f"-DKUI_NATIVE_PERF_FULL_SUBMIT={full}", source,
                            *SOURCES, "-Wl,--gc-sections", "-o", output],
                           cwd=ROOT, check=True)
            subprocess.run([output], cwd=ROOT, env=env, check=True)
    print("Native capture default/split/full checks passed")

if __name__ == "__main__":
    main()
