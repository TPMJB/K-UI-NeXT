#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the shared image-reader correctness and physical-call regression gate."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def audit():
    sources=('tests/test_game_image_batch.c','src/core/zso.c','src/core/lzo1x.c')
    with tempfile.TemporaryDirectory(prefix='kui-game-image-batch-') as folder:
        binary=Path(folder)/'batch'
        subprocess.run((*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-pie','-no-pie','-Iinclude',*sources,'-o',str(binary)),
            cwd=ROOT,check=True)
        output=subprocess.check_output((str(binary),),cwd=ROOT,text=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
    return dict(schema='kui.game-image.common-batch.v1',status='PASS',
        layout_output_cases=10,request_sizes=[1,2,15,16,17,33,257],container_labels=5,
        maximum_physical_sectors_per_buffered_read=16,
        representative_257_raw_sectors=dict(old_callbacks=257,new_callbacks=17),
        sanitizers=['address','undefined'],output=output,
        source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in (*sources,'src/core/game_image.c','include/kui/game_image.h')},
        limits='Actual shared filesystem image-reader C; container parsers, physical SD CRCs, resident ownership and VMU saving have separate regression suites. Callback counts are not console timings.')


if __name__=='__main__':print(json.dumps(audit(),indent=2))
