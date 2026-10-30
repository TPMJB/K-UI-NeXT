#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real delivered module recognition; no expected hash is learned at runtime."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
paths=sys.argv[1:] or ['build/skies-read-paced/retail-native-vmu-lite-paced.kui',
                      'build/background72-standalone/retail-native-vmu-lite-paced.kui']
if len(paths)!=2:raise SystemExit('Provide exactly the accepted d6 and173 module paths')
with tempfile.TemporaryDirectory(prefix='kui-standard-vmu-recognition-') as temporary:
    binary=Path(temporary)/'recognize'
    sources=('tests/test_native_vmu_standard.c','src/core/native_vmu_standard.c',
             'src/core/vmu_game_card.c','src/core/vmu_persist_profile.c',
             'src/core/runtime_image.c','src/core/retail_image.c','src/core/hash.c','src/core/data.c')
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g',
        '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-fno-pie','-no-pie','-Iinclude','-I.deps/fatfs/source',*sources,'-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary),*(str((ROOT/path).resolve()) for path in paths)],cwd=ROOT,check=True,
                   env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
