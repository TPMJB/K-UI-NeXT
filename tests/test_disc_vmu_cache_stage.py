#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run actual disc cache-admission/orchestration bodies with guarded leaves."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def function(source,name):
    # Find the definition declaration (attributes may sit before the name).
    start=source.rfind('static ',0,source.index(name))
    brace=source.index('{',source.index(name));end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

# The replay is only an immutable mapping shortcut. The actual relay must
# verify the complete restored file before mapping, and retain every later
# live admission before publishing its allocator hook. Linked ELF tests
# independently pin the owner-frame ABI and controlling calls.
stage=(ROOT/'src/loader/disc_vmu_stage.c').read_text()
relay=stage[stage.index('void kui_disc_vmu_stage_relay('):stage.index('void kui_disc_vmu_stage_returned(')]
assert 'exec_bytes=plan.exec_bytes;' in stage[:stage.index('void kui_disc_vmu_stage_relay(')]
assert 'KUI_DISC_LAUNCH_EXEC_ADDRESS,exec_bytes)!=plan.exec_crc32)' in relay
assert 'kui_sha256_update(&hash,(const void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,exec_bytes);' in relay
ordered=(
    'if(kui_stage_crc32(',
    'kui_sha256_update(&hash,',
    'if(memcmp(digest,plan.exec_sha256,sizeof(digest))) stopped(',
    'kui_vmu_native_program_extent(',
    'native_disc_mapping_prepare();',
    'if(!native_crt_bootstrap_prearm())',
    'if(!native_disc_entry_return_bind(',
    'code_sha(native_heap_code_sha);',
    'if(((volatile uint32_t *)c)[i]) native_report(',
    'if(original[i]) native_report(',
    'if(native_word(native_heap.initializer_slot)!=native_heap.initializer_target)',
    'c->status=KUI_VMU_OBS_BOOT_ARMED;',
    '*(volatile uint32_t *)(uintptr_t)native_heap.initializer_slot=native_heap_hook;',
)
positions=[relay.index(item) for item in ordered]
assert positions==sorted(positions), 'Disc proof replay moved outside full-image/live handoff guards'
with tempfile.TemporaryDirectory(prefix='kui-disc-cache-stage-') as directory:
    work=Path(directory)
    (work/'disc-vmu-cache-stage-fixture.h').write_text(
        function((ROOT/'src/loader/vmu_native_passive_stage.inc').read_text(),'native_resolution_replay(void)')+'\n'+
        function(stage,'native_disc_mapping_prepare(void)')+'\n')
    out=work/'cache-stage'
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-Iinclude','-I'+directory,
        'tests/test_disc_vmu_cache_stage.c','-o',str(out)],cwd=ROOT,check=True)
    subprocess.run([str(out)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
