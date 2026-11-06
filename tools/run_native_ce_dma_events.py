#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production event admission/queue helper against the external VC2 kernel.

Host address translation is an authored test boundary. The original body is
not distributed, modified on disk, or executed as a CE kernel in this check.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--cc',default='cc')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    kernel=a.kernel.resolve()
    assert sha(kernel)=='b728e8eaef03bd7cc5acbe3dc3363ec9fafcaca90916cf21781d2e72ff99b3bc'
    cc=Path(shutil.which(a.cc)or a.cc).resolve()
    paths=[Path(__file__).resolve(),ROOT/'include/kui/native_ce_dma_events.h',ROOT/'tests/test_native_ce_dma_events.c']
    hashes={str(x):sha(x)for x in paths};logs=[]
    report=dict(status='FAIL',schema='kui.native.ce-dma-event-admission.v1',
        source_sha256=hashes,input_sha256={str(kernel):sha(kernel)},
        compiler=dict(path=str(cc),sha256=sha(cc)),runs={},hardware_executed=False,
        limits=['Production C admission and pending/ring/reschedule queue helper executes on original supplied VC2 body bytes.',
                'Memory-address translation to host backing is an authored test boundary.',
                'A rejected literal may leave earlier checked private binding words; terminal production refusal prevents their use and no kernel byte is written.',
                'CE scheduler, real MMU/TLB, kernel dispatch, storage and device interrupts do not execute here.',
                'The three expected pointers are checked for this input; production admission resolves them from its live VBR instruction literals.'])
    try:
        for mode,sanitizer in [('ubsan','undefined'),('asan-ubsan','address,undefined')]:
            exe=out/('native-ce-events-'+mode)
            cmd=[str(cc),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic','-fno-pie','-no-pie',
                 '-fno-omit-frame-pointer','-fno-sanitize-recover=all','-fsanitize='+sanitizer,
                 '-Iinclude','tests/test_native_ce_dma_events.c','-o',str(exe)]
            built=subprocess.run(cmd,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            logs.append(' '.join(cmd)+'\n'+built.stdout);assert built.returncode==0,built.stdout
            done=subprocess.run([str(exe),str(kernel)],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
            logs.append(done.stdout);assert done.returncode==0,done.stdout
            row=json.loads(done.stdout);assert row['checks']>0 and row['cases']>0
            report['runs'][mode]=dict(command=cmd,executable_sha256=sha(exe),result=row)
        assert hashes=={str(x):sha(x)for x in paths}
        report['source_hashes_unchanged_after_tests']=True;report['status']='PASS'
    finally:
        (out/'native-ce-dma-events.json').write_text(json.dumps(report,indent=2)+'\n')
        (out/'native-ce-dma-events.log').write_text('\n'.join(logs))
    print('PASS CE DMA event helper:',{m:r['result']for m,r in report['runs'].items()})
if __name__=='__main__':main()
