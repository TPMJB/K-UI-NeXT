#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual A1 owner queries and the retained A2/fence regression."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    cc=os.environ.get('HOST_CC','cc')
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fno-pie','-no-pie','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all','-Iinclude','-DKUI_ARMADA_CE_MAPLE_A2=1']
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'}
    with tempfile.TemporaryDirectory(prefix='swirl-target-owner-') as tmp:
        for name,source,extra in (
            ('swirl-a1','tests/test_swirl_ce_vmu_rx_slice.c',
                ['-DKUI_SMASH_SWIRL_VMU=1','-DKUI_VC2_CE_VMU=1']),
            ('default-a2','tests/test_armada_ce_maple_rx_slice.c',[]),
            ('swirl-fence','tests/test_armada_ce_maple_fence.c',['-DKUI_SMASH_SWIRL_VMU=1'])):
            output=str(Path(tmp)/name)
            subprocess.run([cc,*flags,*extra,source,'-o',output],cwd=ROOT,check=True)
            subprocess.run([output],cwd=ROOT,env=env,check=True)

if __name__=='__main__':main()
