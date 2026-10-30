#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual GPIO backend, low loan, and bus-independent writer."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/loader/vmu_maple_observe_resident.c').read_text()
guard=re.search(r'/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/',source,re.S)
assert guard
with tempfile.TemporaryDirectory(prefix='kui-vmu-only-scif-') as tmp:
    out=Path(tmp)
    (out/'vmu_native_resident_guards.h').write_text(guard.group(1))
    binary=out/'transport'
    subprocess.run([
        os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-DKUI_NATIVE_VMU_ONLY=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
        '-DKUI_NATIVE_SPLIT_CONTROLLER_ISOLATION=1','-DKUI_NATIVE_SPLIT_CONTROLLER_GENERAL=1',
        '-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1',
        '-DKUI_VMU_NATIVE_CACHED_CODE=1','-DKUI_RETAIL_SD_TEST=1','-DKUI_RETAIL_TRANSPORT=0',
        '-DKUI_VMU_SD_LEASE_NATIVE_HOST_TEST=1',
        '-I',str(ROOT/'include'),'-I',str(ROOT/'src/loader'),'-I',str(out),
        str(ROOT/'tests/test_vmu_only_scif_transport.c'),
        str(ROOT/'src/loader/sd_reader.c'),str(ROOT/'src/dreamcast/sci_sd_storage.c'),
        '-o',str(binary),
    ],check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
    port=out/'port'
    subprocess.run([
        os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
        '-DKUI_NATIVE_VMU_ONLY=1','-DKUI_RETAIL_SD_TEST=1',
        '-I',str(ROOT/'include'),'-I',str(ROOT/'src/loader'),
        str(ROOT/'tests/test_vmu_only_scif_port.c'),str(ROOT/'src/loader/retail_sd.c'),
        '-o',str(port),
    ],check=True)
    subprocess.run([str(port)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
