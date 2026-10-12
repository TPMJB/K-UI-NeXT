#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay actual GD/adapter for DATA mode0, one-sector mode1 and control3.

Compare complete payloads/terminal protocol, while explicitly expecting
MODE1's different per-visit credit. Hardware pacing/IRQ timing is not modeled.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--no-sanitizers',action='store_true')
    args=parser.parse_args()
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
           '-fno-pie','-no-pie','-ffunction-sections','-fdata-sections',
           '-Iinclude','-Isrc/loader','-DKUI_TOY_PILOT_PRIVATE_P2=1',
           '-DKUI_TOY_PILOT_GD_FIXED_STEP=2','-DKUI_TOY_LOADER_SCHEDULE_HOST_TEST=1']
    if not args.no_sanitizers:
        flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    cc=shlex.split(args.cc)
    fixture=ROOT/'tests/test_toy_loader_schedule.c'
    adapter=ROOT/'src/loader/toy_pilot_gd.c'
    def execute(binary,mode,*,source=adapter,extra=()):
        subprocess.run(cc+list(extra)+flags+['-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE='+str(mode),
            str(fixture),'src/core/retail_gd.c',str(source),'-Wl,--gc-sections','-o',str(binary)],
            cwd=ROOT,check=True)
        return subprocess.run([str(binary)],cwd=ROOT,env=env,text=True,capture_output=True)
    with tempfile.TemporaryDirectory(prefix='kui-toy-data-schedule-') as temp:
        signatures=[]
        for mode in (0,1,3):
            result=execute(Path(temp)/('schedule-'+str(mode)),mode)
            if result.returncode:
                raise RuntimeError('Schedule protocol fixture failed:\n'+result.stdout+result.stderr)
            print(result.stdout,end='')
            signature=re.findall(r'^RESULT completed=40 cancelled=3 boundaries=2 digest=[0-9a-f]{16}$',
                                 result.stdout,re.MULTILINE)
            if len(signature)!=1:
                raise RuntimeError('Missing complete production result: '+result.stdout)
            signatures.append(signature[0])
        if len(set(signatures))!=1:
            raise RuntimeError('DATA schedule complete protocol or payload differs: '+repr(signatures))
        schedule_header=(ROOT/'include/kui/toy_loader_schedule.h').read_text()
        adapter_source=adapter.read_text()
        mutations=(
            ('lost-startup-bound',schedule_header,'return accepted_play ? 2u : 1u;',
             'return accepted_play ? 2u : 2u;',True,'kui_toy_loader_schedule_step(0u)==1u'),
            ('lost-audio-handoff',schedule_header,'return accepted_play ? 2u : 1u;',
             'return accepted_play ? 1u : 1u;',True,'kui_toy_loader_schedule_step(1u)==2u'),
            ('missing-accepted-play',adapter_source,'kui_toy_loader_schedule_accepted_play();',
             '(void)0;',False,'accepted_callbacks==1u && low_play_accepted'),
        )
        for name,original,before,after,header,diagnostic in mutations:
            if original.count(before)!=1:
                raise RuntimeError('Schedule mutation target missing/ambiguous: '+name)
            extra=[];source=adapter
            if header:
                directory=Path(temp)/name/'kui';directory.mkdir(parents=True)
                (directory/'toy_loader_schedule.h').write_text(original.replace(before,after,1))
                extra=['-I'+str(directory.parent)]
            else:
                source=Path(temp)/(name+'.c');source.write_text(original.replace(before,after,1))
            result=execute(Path(temp)/name/'mutated' if header else Path(temp)/(name+'-run'),
                           1,source=source,extra=extra)
            if not result.returncode or diagnostic not in result.stderr:
                raise RuntimeError('Mutation did not fail intended production assertion: '+name+'\n'+
                                   result.stdout+result.stderr)
        for definitions,diagnostic in (
            ({'KUI_TOY_PILOT_DATA_SCHEDULE_MODE':'-1'},'schedule mode must be 0, 1, 2 or 3'),
            ({'KUI_TOY_PILOT_DATA_SCHEDULE_MODE':'4'},'schedule mode must be 0, 1, 2 or 3'),
            ({'KUI_TOY_PILOT_PRIVATE_P2':'0'},'schedule requires retained private P2'),
            ({'KUI_TOY_PILOT_NATIVE_CACHE':'1'},'schedule requires retained private P2'),
            ({'KUI_TOY_PILOT_SHARED_SCI':'1'},'schedule requires retained private P2'),
            ({'KUI_TOY_PILOT_ASYNC_CDDA':'1'},'schedule requires retained private P2'),
            ({'KUI_TOY_PILOT_SYNTHETIC_SOURCE':'1'},'schedule requires retained private P2'),
            ({'KUI_TOY_PILOT_GD_FIXED_STEP':'3'},'schedule requires retained private P2'),
            ({'KUI_SCI_DMA_REUSE_TDRE':'1'},'schedule requires retained private P2'),
            ({'KUI_TOY_PILOT_DATA_PAYLOAD_MODE':'1'},'schedule requires retained private P2'),
        ):
            settings={'KUI_TOY_PILOT_DATA_SCHEDULE_MODE':'1','KUI_TOY_PILOT_PRIVATE_P2':'1',
                      'KUI_TOY_PILOT_GD_FIXED_STEP':'2'}
            settings.update(definitions)
            result=subprocess.run(cc+['-std=c11','-Iinclude']+
                ['-D'+name+'='+value for name,value in settings.items()]+
                ['-x','c','-c','-o',os.devnull,'-'],
                input='#include "kui/toy_loader_schedule.h"\n',cwd=ROOT,text=True,capture_output=True)
            if not result.returncode or diagnostic not in result.stderr:
                raise RuntimeError('Invalid schedule profile admitted: '+repr(definitions)+'\n'+result.stderr)
    print('DATA schedule production differential: 40 completions, 3 cancellations/failures, '
          '2 accepted/rejected PLAY boundaries per mode; 3 behavioral mutations and 10 unsafe configs rejected')


if __name__=='__main__':
    main()
