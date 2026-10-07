#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused portable PCM, deadline and AICA-register checks; no console claim."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import os
import sys

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror']
    if args.sanitize: flags+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    cases=[('pcm',['-Iinclude'],['src/core/cdda_pcm.c','tests/test_cdda_pcm.c']),
           ('clock',['-Iinclude'],['src/core/cdda_clock.c','tests/test_cdda_clock.c']),
           ('job',['-Iinclude'],['src/core/cdda_job.c','tests/test_cdda_job.c']),
           ('handoff',['-Iinclude'],['src/core/cdda_handoff.c','tests/test_cdda_handoff.c']),
           ('timing',['-Iinclude'],['src/core/cdda_timing.c','tests/test_cdda_timing.c']),
           ('control',['-Iinclude'],['src/core/cdda_control.c','tests/test_cdda_control.c']),
           ('bios',['-Iinclude'],['src/core/cdda_bios.c','src/core/cdda_control.c','tests/test_cdda_bios.c']),
           ('batch',['-Iinclude'],['src/core/cdda_bios_batch.c','src/core/cdda_control.c','tests/test_cdda_bios_batch.c']),
           ('stream',['-Iinclude'],['src/core/cdda_stream.c','tests/test_cdda_stream.c']),
           ('ring',['-Iinclude'],['src/core/cdda_ring.c','src/core/cdda_clock.c','tests/test_cdda_ring.c']),
           ('aica',['-Iinclude','-Isrc/loader','-DKUI_CDDA_AICA_TEST'],['src/loader/cdda_aica.c','tests/test_cdda_aica.c'])]
    env=os.environ.copy()
    env['CC']=args.cc
    # This runner performs no allocation leak testing; ptrace prevents LSAN
    # from reading /proc in the managed build container. ASan/UBSan stay on.
    if args.sanitize: env['ASAN_OPTIONS']='detect_leaks=0'
    with tempfile.TemporaryDirectory(prefix='kui-cdda-host-') as tmp:
        for name,includes,sources in cases:
            out=Path(tmp)/name
            subprocess.run([args.cc,*flags,*includes,*sources,'-o',str(out)],cwd=ROOT,check=True)
            subprocess.run([str(out)],cwd=ROOT,env=env,check=True)
    integration=[sys.executable,str(ROOT/'tools/test_cdda_harness.py')]
    if args.sanitize: integration.append('--sanitize')
    subprocess.run(integration,cwd=ROOT,env=env,check=True)
    service=[sys.executable,str(ROOT/'tools/test_cdda_service.py')]
    if args.sanitize: service.append('--sanitize')
    subprocess.run(service,cwd=ROOT,env=env,check=True)
    bios=[sys.executable,str(ROOT/'tools/test_cdda_bios_integration.py')]
    if args.sanitize: bios.append('--sanitize')
    subprocess.run(bios,cwd=ROOT,env=env,check=True)
    batch=[sys.executable,str(ROOT/'tools/test_cdda_batch_integration.py')]
    if args.sanitize: batch.append('--sanitize')
    subprocess.run(batch,cwd=ROOT,env=env,check=True)
    fault=[sys.executable,str(ROOT/'tools/test_cdda_batch_integration.py'),'--profile','10']
    if args.sanitize: fault.append('--sanitize')
    subprocess.run(fault,cwd=ROOT,env=env,check=True)
if __name__=='__main__': main()
