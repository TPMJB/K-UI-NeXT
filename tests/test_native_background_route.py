#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the shared GUI reader/capability route with malformed modules."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def audit():
    sources=('tests/test_native_background_route.c','src/core/native_session.c')
    with tempfile.TemporaryDirectory(prefix='kui-background-route-') as temporary:
        binary=Path(temporary)/'route'
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-pie','-no-pie','-Iinclude','-Iconfig','-I.deps/fatfs/source',
            *sources,'-o',str(binary)],cwd=ROOT,check=True)
        output=subprocess.check_output([str(binary)],cwd=ROOT,text=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
    return dict(schema='kui.native.background-route.v1',status='PASS',
        requested_reader_routes=dict(new='compact-dual',legacy='compact-dual'),
        optional_session_controls='explicit-dual-path-New-only',
        virtual_slots=[1,2],crossed_and_malformed_capabilities_refused=True,per_title_rules=False,
        sanitizers=['address','undefined'],output=output,
        source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
            (*sources,'include/kui/native_background_route.h','include/kui/games_retail.h',
             'include/kui/native_dual_reader.h','include/kui/native_session.h','src/apps/games_retail.c')})

if __name__=='__main__':print(json.dumps(audit(),indent=2))
