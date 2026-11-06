#!/usr/bin/env python3
"""Production same-job recovery reuse with production receipt and disk epoch."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='kui-launch-history-') as tmp:
    binary=Path(tmp)/'launch-history'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-Iinclude','-Isrc/dreamcast','-I.deps/fatfs/source',
        'tests/test_vmu_launch_history.c','src/apps/vmu_launch_history.c','src/core/diskio.c',
        'src/core/vmu_game_card.c','src/core/data.c','-Wl,--gc-sections','-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,check=True,
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
