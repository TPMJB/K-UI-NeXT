#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check real GUI CRC entry points with the actual build flag and no flag."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='kui-gui-crc-') as temporary:
    for fast in (0,1):
        output=Path(temporary)/('test-'+str(fast))
        subprocess.run(['cc','-std=c11','-O2','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
            '-ffunction-sections','-fdata-sections','-Iinclude','-DKUI_GUI_FAST_CRC32='+str(fast),
            'tests/test_gui_fast_crc.c','src/core/data.c','src/core/retail_image.c',
            '-Wl,--gc-sections','-o',str(output)],cwd=ROOT,check=True)
        subprocess.run([str(output)],cwd=ROOT,check=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
