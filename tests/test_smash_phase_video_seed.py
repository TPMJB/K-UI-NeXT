#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sanitized production VID1 shape/CRC and normal/diagnostic pair checks."""
import argparse,json,os
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def audit(outer=None,child=None):
    with tempfile.TemporaryDirectory(prefix='smash-video-seed-') as folder:
        binary=Path(folder)/'test'
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Iinclude',
            'tests/test_smash_phase_video_seed.c','src/core/smash_phase_video.c',
            'src/core/smash_phase_trace.c','src/core/retail_image.c','-o',str(binary)],cwd=ROOT,check=True,capture_output=True)
        args=[str(binary)]
        if outer is not None or child is not None:
            assert outer is not None and child is not None
            args += [str(Path(outer).resolve()),str(Path(child).resolve())]
        result=subprocess.run(args,cwd=ROOT,check=True,capture_output=True,text=True,
                              env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
        return json.loads(result.stdout)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--report',type=Path)
    p.add_argument('--outer',type=Path);p.add_argument('--child',type=Path);a=p.parse_args()
    text=json.dumps(audit(a.outer,a.child),indent=2)+'\n'
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
