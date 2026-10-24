#!/usr/bin/env python3
"""Run the real native SCI reader/card model with the gated VMU loan path."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["src/loader/retail_async.c", "src/loader/sci_stream.c",
           "src/core/retail_cursor.c", "src/core/retail_image.c",
           "src/core/retail_gd.c", "tests/test_vmu_native_async_loan.c"]
DEFS = ["KUI_RETAIL_ASYNC_TEST=1", "KUI_SCI_STREAM_TEST=1",
        "KUI_RETAIL_ASYNC=1", "KUI_RETAIL_GD_ASYNC=1",
        "KUI_RETAIL_TRANSPORT=1", "KUI_SCI_SD_NO_BLOCK=1",
        "KUI_GENERAL_SCI_RESUME_FIX=1", "KUI_RETAIL_NATIVE_DISC_CHECK=1",
        "KUI_RETAIL_NATIVE_DRIVE_STATE=1", "KUI_VMU_NATIVE_BACKGROUND=1"]

with tempfile.TemporaryDirectory(prefix="kui-native-async-loan-") as tmp:
    output = str(Path(tmp) / "test-native-async-loan")
    compiler = os.environ.get("HOST_CC", "cc")
    command = [compiler, "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wpedantic", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               "-fno-pie", "-no-pie", "-Iinclude", "-Isrc/loader"]
    subprocess.run(command + ["-D" + flag for flag in DEFS] + SOURCES + ["-o", output],
                   cwd=ROOT, check=True)
    subprocess.run([output], cwd=ROOT, check=True,
                   env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
    # A misplaced gate cannot silently select Standard, CE, or unrepaired SCI.
    for refused in ("KUI_RETAIL_ASYNC=1", "KUI_RETAIL_GD_ASYNC=1",
                    "KUI_SCI_SD_NO_BLOCK=1", "KUI_GENERAL_SCI_RESUME_FIX=1"):
        flags = ["-D" + flag for flag in DEFS if flag != refused]
        result = subprocess.run([compiler, "-E", "-Iinclude", "-Isrc/loader"] + flags +
                                ["-include", "kui/vmu_native_async_loan.h", "-x", "c", "/dev/null"],
                                cwd=ROOT, text=True, capture_output=True)
        assert result.returncode and "requires the repaired native SCI" in result.stderr
    result = subprocess.run([compiler, "-E", "-Iinclude", "-Isrc/loader"] +
                            ["-D" + flag for flag in DEFS] + ["-DKUI_RETAIL_CE=1",
                            "-include", "kui/vmu_native_async_loan.h", "-x", "c", "/dev/null"],
                            cwd=ROOT, text=True, capture_output=True)
    assert result.returncode and "requires the repaired native SCI" in result.stderr
    # Use the real SCI bus descriptor/MMIO model and VMU identity parser.
    # Ordinary NO_BLOCK builds must retain their original NULL clock.
    for enabled in (0, 1):
        bus_output = str(Path(tmp) / ("test-native-async-bus-" + str(enabled)))
        flags = [flag for flag in DEFS if not flag.startswith("KUI_VMU_NATIVE_BACKGROUND=")]
        flags += ["KUI_VMU_NATIVE_BACKGROUND=" + str(enabled),
                  "KUI_SCI_SD_TEST=1", "KUI_VMU_NATIVE_PERSISTENT_PROBE=1"]
        subprocess.run(command + ["-ffunction-sections", "-fdata-sections"] +
                       ["-D" + flag for flag in flags] + ["src/loader/sci_sd_bus.c",
                       "src/loader/vmu_backing.c", "tests/test_vmu_native_async_bus.c",
                       "-Wl,--gc-sections", "-o", bus_output], cwd=ROOT, check=True)
        subprocess.run([bus_output], cwd=ROOT, check=True,
                       env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
print("native async VMU loan build gates passed")
