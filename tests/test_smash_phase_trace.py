#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production phase-trace wire/order checks under sanitizers."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def audit():
    sources=('tests/test_smash_phase_trace.c','src/core/smash_phase_trace.c')
    with tempfile.TemporaryDirectory(prefix='kui-smash-phase-trace-') as directory:
        binary=Path(directory)/'phase-trace'
        command=(*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-pie','-no-pie','-Iinclude',*sources,'-o',str(binary))
        subprocess.run(command,cwd=ROOT,check=True)
        output=subprocess.check_output((str(binary),),cwd=ROOT,text=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
    return dict(schema='kui.smash-phase-trace.host.v1',status='PASS',
        sanitizers=['address','undefined'],ordered_defined_checkpoints=21,output=output,
        source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                       for name in (*sources,'include/kui/smash_phase_trace.h')},
        limits='Production trace/order code executes; the clock is an ABI fixture that reports unavailable.')


if __name__=='__main__':print(json.dumps(audit(),indent=2))
