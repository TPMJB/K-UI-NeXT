#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production native session switching with the real SCI/card model."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
FLAGS=("KUI_RETAIL_ASYNC_TEST=1","KUI_SCI_STREAM_TEST=1","KUI_RETAIL_ASYNC=1",
       "KUI_RETAIL_GD_ASYNC=1","KUI_RETAIL_TRANSPORT=1","KUI_SCI_SD_NO_BLOCK=1",
       "KUI_GENERAL_SCI_RESUME_FIX=1","KUI_RETAIL_NATIVE_HYBRID=1",
       "KUI_NATIVE_REQUEST_TIMING=0","KUI_NATIVE_COOP_PUMP=1","KUI_NATIVE_IRQ_REARM=1",
       "KUI_NATIVE_LOADER_TIMELINE=1","KUI_NATIVE_READER_DIAGNOSTIC=1",
       "KUI_VMU_NATIVE_BACKGROUND=1","KUI_NATIVE_SDK_SCI_ROUTE=1",
       "KUI_NATIVE_SDK_SCI_STRUCTURAL=1","KUI_RETAIL_MANIFEST_SLOTS=64",
       "KUI_NATIVE_READAHEAD=1","KUI_NATIVE_SESSION_CONTROLS=1",
       "KUI_VMU_NATIVE_PERSISTENT_PROBE=1","KUI_VMU_NATIVE_CACHED_CODE=1",
       "KUI_NATIVE_GENERAL_LEAN_LAYOUT=1")
SOURCES=("src/loader/retail_async.c","src/loader/sci_stream.c","src/core/retail_cursor.c",
         "src/core/retail_image.c","src/core/retail_gd.c","tests/test_retail_async_session.c")
with tempfile.TemporaryDirectory(prefix="kui-async-session-") as folder:
    # Compile the actual cold getter without privileged-instruction or MMIO
    # substitutions. Active operations now belong to the worker gateway and
    # are exercised separately by test_native_session_switch.py.
    source=(ROOT/"src/loader/vmu_native_background_resident.c").read_text()
    binding=re.search(r"static const struct kui_native_session_binding native_session_binding=\{(.*?)\};",source,re.S)
    assert binding
    assert re.sub(r"\s+","",binding.group(1))==(
        "KUI_NATIVE_SESSION_BINDING_MAGIC,KUI_NATIVE_SESSION_BINDING_VERSION,"
        "sizeof(structkui_native_session_binding),"
        "(uint32_t)(uintptr_t)&kui_retail_async_region_storage,"
        "(uint32_t)(uintptr_t)&native_background_manifest,"
        "(uint32_t)(uintptr_t)native_session_firmware")
    resident=(ROOT/"src/loader/retail_resident.c").read_text()
    assert re.search(r"static\s+uint32_t\s+native_session_firmware\[9\]\s*;",resident)
    start=source.index("int __attribute__((noipa)) kui_retail_native_session_control(")
    opening=source.index("{",start);end=opening+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    header=Path(folder)/"session_binding.h"
    header.write_text(source[start:end]+"\n")
    binary=str(Path(folder)/"session")
    command=[*shlex.split(os.environ.get("HOST_CC","cc")),"-std=c11","-O2","-g",
             "-Wall","-Wextra","-Werror","-Wpedantic","-fsanitize=address,undefined",
             "-fno-omit-frame-pointer","-fno-pie","-no-pie","-Iinclude","-Isrc/loader",
             '-DKUI_SESSION_BINDING_TEST_HEADER="'+str(header)+'"']
    subprocess.run(command+["-D"+flag for flag in FLAGS]+list(SOURCES)+["-o",binary],cwd=ROOT,check=True)
    subprocess.run([binary],cwd=ROOT,check=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
