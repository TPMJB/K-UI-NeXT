#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Successful CE adoption must not access PVR/VRAM or wait for video frames.

The real renderer executes on a host where all Dreamcast MMIO addresses are
unmapped. A missed quiet guard faults instead of being hidden by a video stub.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "retail_display.h"
int main(void) {
    uint32_t values[17]={0};
    retail_display_adoption_quiet(1u);
    retail_display_restore(0);
    retail_display_line("ORIGINAL GIN CONTINUATION");
    retail_display_hex("VALUE",0x12345678u);
    retail_display_values("VALUES",values,17u);
    retail_display_progress(1u,2u);
    retail_display_pause(2u);
    retail_display_vc2_ownership_begin();
    retail_display_vc2_ownership_status(0);
    retail_display_vc2_ownership_results(1u);
    retail_display_adoption_quiet(0u);
    return 0;
}
'''

def audit():
    with tempfile.TemporaryDirectory(prefix='smash-gin-video-') as temporary:
        folder=Path(temporary);source=folder/'quiet.c';source.write_text(SOURCE)
        binary=folder/'quiet'
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),
            '-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-fno-pie','-no-pie','-DKUI_SMASH_CE_GIN_ADOPT=1',
            '-DKUI_VC2_STAGE_OWNERSHIP_PROFILE=1','-DKUI_BUILD_ID="host-check"',
            '-Iinclude','-Isrc/loader',str(source),'src/loader/retail_display.c',
            '-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,check=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
    return 'PASS actual quiet renderer: no PVR/VRAM access, no VBL wait'

if __name__=='__main__':
    print(audit())
