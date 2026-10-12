#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production SCHED v2 request attribution and exact frozen budget telemetry."""
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
           '-DKUI_TOY_PILOT_GD_FIXED_STEP=2','-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE=2',
           '-DKUI_TOY_PILOT_LOADER_TRACE=1','-DKUI_TOY_PILOT_DATA_PROBE=1',
           '-DKUI_TOY_LOADER_SCHEDULE_REPORT_HOST_TEST=1']
    if not args.no_sanitizers:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    cc=shlex.split(args.cc);env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    original=ROOT/'src/loader/toy_loader_schedule_report.c'
    def execute(path,profile,production=original):
        subprocess.run(cc+flags+['-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE='+str(profile),
            '-DKUI_SCHEDULE_REPORT_SOURCE="'+str(production)+'"',
            'tests/test_toy_loader_budget_report.c','src/core/retail_gd.c',
            '-Wl,--gc-sections','-o',str(path)],cwd=ROOT,check=True)
        return subprocess.run([str(path)],cwd=ROOT,env=env,text=True,capture_output=True)
    with tempfile.TemporaryDirectory(prefix='kui-toy-budget-report-') as temp:
        signatures=[]
        for profile in (1,2,3,4):
            result=execute(Path(temp)/('budget-'+str(profile)),profile)
            if result.returncode:
                raise RuntimeError('Budget SCHED report failed:\n'+result.stdout+result.stderr)
            print(result.stdout,end='')
            signature=re.findall(r'^BUDGET SCHED protocol/payload digest=[0-9a-f]{16}$',result.stdout,re.MULTILINE)
            if len(signature)!=1:raise RuntimeError('Missing budget SCHED protocol signature')
            signatures.append(signature[0])
        if len(set(signatures))!=1:raise RuntimeError('Budget SCHED protocol differs: '+repr(signatures))
        text=original.read_text()
        mutations=(
            ('initial-pump-excluded','schedule_visit.request_irq=schedule_visit.call_irq;',
             'schedule_visit.request_irq=kui_toy_pilot_sci_snapshot()->irq_blocks;',
             'r[16]==2u && r[17]==1u'),
            ('wrapped-counter-lost','r[16]=engine->irq_blocks-schedule_visit.request_irq;',
             'r[16]=engine->irq_blocks<schedule_visit.request_irq?0u:engine->irq_blocks-schedule_visit.request_irq;',
             'r[16]==5u && r[17]==3u'),
            ('stale-active-freeze','if(active)schedule_counter_delta(active);','(void)active;',
             'r[16]==11u && r[17]==3u'),
            ('budget-after-cleanup','memcpy(schedule_report+36u,budget,48u);',
             'kui_toy_pilot_sci_quiesce();memcpy(schedule_report+36u,budget,48u);',
             '!memcmp(schedule_report+36u,&expected_budget,48u)'),
            ('budget-saturation-lost','if(schedule_report[i]==UINT32_MAX)schedule_report[10]=1u;',
             'if(schedule_report[i]==UINT32_MAX)(void)0;',
             'schedule_report[10]'),
        )
        for name,before,after,diagnostic in mutations:
            if text.count(before)!=1:raise RuntimeError('Missing/ambiguous budget report mutation: '+name)
            source=Path(temp)/(name+'.c');source.write_text(text.replace(before,after,1))
            result=execute(Path(temp)/(name+'-run'),1,source)
            if not result.returncode or diagnostic not in result.stderr:
                raise RuntimeError('Budget report mutation escaped its intended assertion: '+name+'\n'+result.stdout+result.stderr)
        for profile,mode,diagnostic in ((-1,2,'profile must be'),(5,2,'profile must be'),
                                      (1,1,'profiles require startup'),(1,3,'profiles require startup')):
            result=subprocess.run(cc+['-std=c11','-Iinclude',
                '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE='+str(profile),
                '-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE='+str(mode),
                '-x','c','-c','-o',os.devnull,'-'],
                input='#include "kui/toy_loader_schedule_report.h"\n',cwd=ROOT,text=True,capture_output=True)
            if not result.returncode or diagnostic not in result.stderr:
                raise RuntimeError('Invalid budget report configuration admitted:\n'+result.stderr)
    print('SCHED budget report profiles1/2/3/4 passed (192 words and per-request IRQ/retry attribution)')


if __name__=='__main__':main()
