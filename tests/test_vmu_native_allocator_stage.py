#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute actual installer digest and private-pool routing on public RAM."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def function(source,name):
    start=source.rfind('static ',0,source.index(name))
    brace=source.index('{',source.index(name));end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

with tempfile.TemporaryDirectory(prefix='kui-allocator-stage-') as directory:
    work=Path(directory)
    image=(ROOT/'src/loader/vmu_native_heap_probe_stage.c').read_text()
    disc=(ROOT/'src/loader/disc_vmu_stage.c').read_text()
    (work/'allocator-stage-fixture.h').write_text(
        function(image,'code_sha(uint8_t').replace('code_sha(','image_code_sha(',1)+'\n'+
        function(disc,'code_sha(uint8_t').replace('code_sha(','disc_code_sha(',1)+'\n'+
        function(image,'native_budget_prepare(')+'\n')
    output=work/'allocator-stage'
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g',
        '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=undefined','-fno-sanitize-recover=all',
        '-fno-omit-frame-pointer','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections',
        '-Wl,--gc-sections','-Iinclude','-I'+directory,'tests/test_vmu_native_allocator_stage.c',
        'src/core/vmu_native_heap.c','src/core/hash.c','src/core/data.c','-o',str(output)],cwd=ROOT,check=True)
    subprocess.run([str(output)],cwd=ROOT,check=True)
