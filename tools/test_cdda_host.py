#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused portable PCM, deadline and AICA-register checks; no console claim."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import os

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror']
    if args.sanitize: flags+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    cases=[('pcm',['-Iinclude'],['src/core/cdda_pcm.c','tests/test_cdda_pcm.c']),
           ('ring',['-Iinclude'],['src/core/cdda_ring.c','tests/test_cdda_ring.c']),
           ('aica',['-Isrc/loader','-DKUI_CDDA_AICA_TEST'],['src/loader/cdda_aica.c','tests/test_cdda_aica.c'])]
    env=os.environ.copy()
    # This runner performs no allocation leak testing; ptrace prevents LSAN
    # from reading /proc in the managed build container. ASan/UBSan stay on.
    if args.sanitize: env['ASAN_OPTIONS']='detect_leaks=0'
    with tempfile.TemporaryDirectory(prefix='kui-cdda-host-') as tmp:
        for name,includes,sources in cases:
            out=Path(tmp)/name
            subprocess.run([args.cc,*flags,*includes,*sources,'-o',str(out)],cwd=ROOT,check=True)
            subprocess.run([str(out)],cwd=ROOT,env=env,check=True)
if __name__=='__main__': main()
