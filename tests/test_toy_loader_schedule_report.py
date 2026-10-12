#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production SCHED chronology, payload/protocol differential and corruption checks."""
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
           '-DKUI_TOY_PILOT_GD_FIXED_STEP=2','-DKUI_TOY_LOADER_SCHEDULE_HOST_TEST=1',
           '-DKUI_TOY_LOADER_SCHEDULE_REPORT_HOST_TEST=1']
    if not args.no_sanitizers:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    cc=shlex.split(args.cc);env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    source=ROOT/'src/loader/toy_loader_schedule_report.c'
    def execute(path,mode,production=source):
        module=['src/loader/toy_pilot_gd.c'] if mode!=2 else []
        subprocess.run(cc+flags+['-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE='+str(mode),
            '-DKUI_SCHEDULE_REPORT_SOURCE="'+str(production)+'"',
            'tests/test_toy_loader_schedule_report.c','src/core/retail_gd.c']+
            module+['-Wl,--gc-sections','-o',str(path)],cwd=ROOT,check=True)
        return subprocess.run([str(path)],cwd=ROOT,env=env,text=True,capture_output=True)
    with tempfile.TemporaryDirectory(prefix='kui-toy-sched-report-') as temp:
        signatures=[]
        for mode in (1,3,2):
            result=execute(Path(temp)/('report-'+str(mode)),mode)
            if result.returncode:
                raise RuntimeError('Production SCHED report failed:\n'+result.stdout+result.stderr)
            print(result.stdout,end='')
            signature=re.findall(r'^SCHED protocol/payload digest=[0-9a-f]{16}$',result.stdout,re.MULTILINE)
            if len(signature)!=1:raise RuntimeError('Missing SCHED protocol signature')
            signatures.append(signature[0])
        if len(set(signatures))!=1:
            raise RuntimeError('SCHED instrumented/uninstrumented protocol differs: '+repr(signatures))
        production=source.read_text()
        mutations=(
            ('crc-corruption','0xbdbdf21cu','0x00000000u',1,'r[11]==UINT32_C(0x9f5edd58)'),
            ('guest-write-map','address,s->sector_bytes,0);','address,s->sector_bytes,1);',1,'access==0'),
            ('lost-consumed-ack','r[7]|=8u;','r[7]|=0u;',1,'r[7]&FLAGS_ACK'),
            ('clock-hole-age','if(!valid)schedule_visit.request_valid=0u;','if(!valid)(void)0;',1,'!(r[7]&(FLAGS_FIRST|FLAGS_COMPLETE))'),
            ('cleanup-before-snapshot','memcpy(schedule_report+16u,kui_toy_pilot_sci_snapshot(),64u);',
             'kui_toy_pilot_sci_quiesce();memcpy(schedule_report+16u,kui_toy_pilot_sci_snapshot(),64u);',
             2,'!memcmp(schedule_report+16u,&original,sizeof(original))'),
        )
        for name,before,after,mode,diagnostic in mutations:
            if production.count(before)!=1:raise RuntimeError('Missing/ambiguous SCHED mutation target: '+name)
            mutated=Path(temp)/(name+'.c');mutated.write_text(production.replace(before,after,1))
            result=execute(Path(temp)/(name+'-run'),mode,mutated)
            if not result.returncode or diagnostic not in result.stderr:
                raise RuntimeError('SCHED mutation escaped intended assertion: '+name+'\n'+result.stdout+result.stderr)
    print('SCHED production report modes1/2/3: protocol/payload match, chronology/cancellation/timing/'
          'rolling/CRC/freeze checks and five adverse mutations passed')


if __name__=='__main__':main()
