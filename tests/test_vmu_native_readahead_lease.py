#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual allocator/low guards/placement for both isolated geometries."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/"src/loader/vmu_native_background_resident.c").read_text()
guard=re.search(r"/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/",source,re.S)
assert guard and "native_resident_transaction" in guard.group(1)
with tempfile.TemporaryDirectory(prefix="kui-native-readahead-lease-") as tmp:
    directory=Path(tmp)
    (directory/"vmu_native_background_guards.h").write_text(guard.group(1))
    for enabled in (0,1):
        flags=[f"-DKUI_NATIVE_READAHEAD={enabled}","-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1",
               "-DKUI_VMU_NATIVE_CACHED_CODE=1"]
        common=[os.environ.get("HOST_CC","cc"),"-std=c11","-O1","-g","-Wall","-Wextra",
                "-Werror","-Wpedantic","-fsanitize=address,undefined","-fno-omit-frame-pointer",
                "-fno-pie","-no-pie","-I",str(ROOT/"include"),"-I",str(directory),*flags]
        for name,inputs in (
            ("lease",[ROOT/"tests/test_vmu_native_readahead_lease.c",
                      ROOT/"src/core/vmu_native_stage_placement.c"]),
            ("map",[ROOT/"tests/test_vmu_native_background_map.c"]),
        ):
            binary=directory/f"{name}-{enabled}"
            subprocess.run([*common,*map(str,inputs),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
