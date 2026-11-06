#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the production bound-owner guard with distinct Armada/VC2 KData.

The guard is the exact preprocessed prefix of production a2_run, ending at
the diagnostic-mode boundary. Only fixed platform loads are substituted.
Both source profiles run under sanitizers; a cross-profile load mutation
must fail the positive control. This does not model storage or CE timing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from test_armada_ce_maple_continuation import FLAGS, definition

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'src/loader/armada_ce_maple_traffic.c'

PREFIX=r'''
#include "kui/armada_ce_maple_rx_slice.h"
#include "kui/armada_ce_vmu_a2_publish.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
static struct {
    uint32_t ttb,process,ccr,records,index[2],needed[2],parent,target,target_bytes;
} a2_context;
static uint32_t platform[4],last_read;
static unsigned reads;
static unsigned slot(uint32_t at) {
    switch(at) {
    case 0x8c145894u:return 0;
    case 0x8c145890u:return 1;
    case 0x8c1d5894u:return 2;
    case 0x8c1d5890u:return 3;
    default:assert(!"unexpected platform address");return 0;
    }
}
static uint32_t test_register(uint32_t at) {++reads;last_read=at;return platform[slot(at)];}
'''

HARNESS=r'''
enum { THREAD=0x11220000u, PROCESS=0x8c1de8b4u, TTB=0x8c1d58a0u, CCR=0x105u };
static void fixture(void) {
    memset(&a2_continuation,0,sizeof(a2_continuation));
    memset(&kui_armada_ce_vmu_a2_log,0,sizeof(kui_armada_ce_vmu_a2_log));
    /* Populate both real source locations with deliberately different live
     * values. A wrong-profile read cannot accidentally pass a zero fixture. */
    platform[0]=0xaabbcc00u;platform[1]=0x8c14e8b4u;
    platform[2]=0xddeeff00u;platform[3]=0x8c1de8b4u;
    platform[slot(KUI_CE_MAPLE_CURRENT_THREAD)]=THREAD;
    a2_continuation.bound=1u;a2_continuation.thread=THREAD;
    a2_continuation.process=PROCESS;a2_continuation.ttb=TTB;a2_continuation.ccr=CCR;
    a2_continuation.checked=99u;a2_continuation.result=99u;reads=last_read=0u;
}
static void entered(void) {
    assert(a2_continuation.result==UINT32_MAX && !a2_continuation.checked);
}
static void fault(unsigned cause,uint32_t address,uint32_t actual,uint32_t expected) {
    const uint32_t wanted[5]={100u,address,actual,expected,cause};
    assert(!memcmp(kui_armada_ce_vmu_a2_log.fault,wanted,sizeof(wanted)));
    assert(kui_armada_ce_vmu_a2_log.status[0]==1u);
    entered();
}
int main(void) {
    fixture();a2_run(TTB,PROCESS,CCR);entered();
    assert(!kui_armada_ce_vmu_a2_log.fault[0] && reads==1u && last_read==KUI_CE_MAPLE_CURRENT_THREAD);
    fixture();platform[slot(KUI_CE_MAPLE_CURRENT_THREAD)]^=4u;
    /* Even making the other profile look correct must not admit this one. */
    platform[slot(KUI_CE_MAPLE_CURRENT_THREAD==0x8c145894u?0x8c1d5894u:0x8c145894u)]=THREAD;
    a2_run(TTB,PROCESS,CCR);
    fault(4u,KUI_CE_MAPLE_CURRENT_THREAD,THREAD^4u,THREAD);assert(reads==1u);
    fixture();a2_run(TTB^4u,PROCESS,CCR);
    fault(1u,0xff000008u,TTB^4u,TTB);assert(!reads);
    fixture();a2_run(TTB,PROCESS^4u,CCR);
    fault(2u,KUI_CE_MAPLE_CURRENT_PROCESS,PROCESS^4u,PROCESS);assert(!reads);
    fixture();a2_run(TTB,PROCESS,CCR^0x20u);
    fault(3u,0xff00001cu,CCR^0x20u,CCR);assert(!reads);
    fixture();a2_continuation.bound=0u;
    platform[slot(KUI_CE_MAPLE_CURRENT_THREAD)]^=4u;a2_run(TTB,PROCESS,CCR);
    entered();assert(!reads && !kui_armada_ce_vmu_a2_log.fault[0]);
    puts("PASS production a2_run owner guard: matching profile, wrong thread, TTB, process, CCR, unbound control");
    return 0;
}
'''


def extracted(preprocessed):
    state=re.search(r'(?ms)^static struct \{\n\s*uint32_t pending,rounds,raw,mode,sr,result,checked;.*?\} a2_continuation;',preprocessed)
    if not state:raise ValueError('Production continuation state missing')
    function=definition(preprocessed,'a2_run')
    boundary=function.find('    unsigned diagnostic=')
    if boundary<0:raise ValueError('Production diagnostic boundary missing')
    guard=function[:boundary]+'}\n'
    guard,loads=re.subn(r'\*\(volatile const uint32_t \*\)\(uintptr_t\)(0x[0-9a-f]+[uU]?)',
        r'test_register(\1)',guard)
    if loads!=1:raise ValueError(f'Expected exactly one production current-thread read, found {loads}')
    return state.group()+'\n'+definition(preprocessed,'a2_fault')+'\n'+guard,guard


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=os.environ.get('CC','cc'))
    parser.add_argument('--json',type=Path)
    args=parser.parse_args();compiler=shlex.split(args.cc)
    report=dict(status='PASS',production_bound_guard_executed=True,
        distinct_armada_and_vc2_kdata=True,profiles={},sanitizers=['address','undefined'],
        scope='Production initial bound-owner admission and precise first-cause diagnostics; no storage/timing claim')
    with tempfile.TemporaryDirectory(prefix='ce-owner-profile-') as temporary:
        folder=Path(temporary)
        for name,extra,selected,other in (
            ('armada',[],0x8c145894,0x8c1d5894),
            ('vc2',['-DKUI_VC2_CE_VMU=1','-DKUI_VC2_VMU_PERSIST=1'],0x8c1d5894,0x8c145894)):
            flags=[*FLAGS,*extra]
            preprocessed=subprocess.check_output(compiler+['-E','-P','-std=c11','-I'+str(ROOT/'include'),
                *flags,str(SOURCE)],text=True)
            body,guard=extracted(preprocessed)
            variants=(('control',body),('cross-profile-mutant',re.sub(
                r'test_register\(0x[0-9a-f]+[uU]?\)',f'test_register(0x{other:08x}u)',body)))
            for kind,source in variants:
                if kind!='control' and source==body:raise ValueError('Current-thread mutation was not applied')
                path=folder/(name+'-'+kind+'.c');binary=path.with_suffix('')
                path.write_text(PREFIX+source+HARNESS)
                subprocess.run(compiler+['-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
                    '-I'+str(ROOT/'include'),*flags,str(path),'-o',str(binary)],check=True)
                run=subprocess.run([str(binary)],text=True,capture_output=True,
                    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'))
                if kind=='control' and run.returncode:
                    raise RuntimeError(name+' control failed:\n'+run.stdout+run.stderr)
                if kind!='control' and not run.returncode:
                    raise RuntimeError(name+' admitted a cross-profile stale KData load')
            report['profiles'][name]=dict(status='PASS',selected_current_thread=f'{selected:08x}',
                mismatches_checked=['current_thread','ttb','process','ccr'],unbound_control=True,
                cross_profile_load_mutant_rejected=True,guard_sha256=hashlib.sha256(guard.encode()).hexdigest())
    report['source_sha256']=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    output=json.dumps(report,indent=2)+'\n'
    if args.json:args.json.write_text(output)
    print(output,end='')


if __name__=='__main__':main()
