#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production spindle policy and pin its actual worker wiring."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/dreamcast/main.c').read_text()
assert 'if(kui_disc_idle_cleanup(action)) {' in source
assert 'inspection_passed && !kui_cancelled(),inspection_read,timer_ms_gettime64(),' in source
assert 'if(cleanup==KUI_DISC_IDLE_STOP && !kui_disc_stop())' in source
assert 'bool can_stop=!busy && !pending && !video_preview;' in source
assert 'bool no_media=disc_identity.state==KUI_DISC_IDENTITY_OPEN ||' in source
assert 'bool must_reset=drive_reset_required;' in source
assert 'kui_disc_idle_take_stop(&disc_idle,timer_ms_gettime64(),can_stop,' in source
assert 'if(action>=49u && action<=53u) kui_disc_idle_cancel(&disc_idle);' in source
normal=source[source.index('if(action==12) {',source.index('if(action == 8 || action == 9)')):]
normal=normal[:normal.index('if(action==13 || action==14)')]
assert 'inspection_passed=inspection_read=disc_identity.state==KUI_DISC_IDENTITY_READY;' in normal
info=source[source.index('if(action==105) {\n                struct kui_app_status result;'):]
info=info[:info.index('if(action==56 || action==57')]
assert 'inspection_passed=result.complete && result.passed && !result.stopped;' in info
assert 'inspection_read=inspection_passed && info.enabled;' in info
handoff=source[source.index('if(action==56 || action==57 || action==58 || action==104 || action==121 || action==122) {',
    source.index('if(action==105) {\n                struct kui_app_status result;')):]
assert handoff.index('for(;;) {')<handoff.index('if(kui_disc_idle_cleanup(action)) {')
with tempfile.TemporaryDirectory(prefix='disc-idle-') as d:
    binary=Path(d)/'idle'
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g',
        '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-fno-pie','-no-pie','-Iinclude',
        'tests/test_disc_idle.c','-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('PASS actual worker spindle wiring: ready identity, successful card review, pending-job priority, cancellation and parked handoff')
