#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check CE completion ownership against the real reader/core and old source.

Only the established byte-level SCI/card and CPU/interrupt fixtures are
modeled. The test does not execute a CE kernel or establish console acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
BASELINE='91fa7cc6922b1e5274ac0e622fc9e9980763499c'
BASELINE_SHA256='475de755ce2c8540dc271c6a5d1edaa2fcff67a7099795966c767a6cc2e50839'
SOURCES=('src/loader/retail_async.c','src/loader/sci_stream.c',
         'src/core/retail_cursor.c','src/core/retail_image.c')
TEST='tests/test_ce_gd_completion_lifecycle.c'
FLAGS=('-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
       '-Wno-unused-function','-fsanitize=address,undefined',
       '-fno-omit-frame-pointer','-fno-sanitize-recover=all','-fno-pie','-no-pie',
       '-DKUI_RETAIL_ASYNC_TEST=1','-DKUI_SCI_STREAM_TEST=1',
       '-DKUI_RETAIL_ASYNC=1','-DKUI_RETAIL_GD_ASYNC=1','-DKUI_RETAIL_CE=1',
       '-DKUI_RETAIL_TRANSPORT=1','-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=1',
       '-Iinclude','-Isrc/loader','-Itests')
CASES=('callback-clear','dma-failure-active-piece','dma-failure-no-piece')
DMA_FIX='''    /* A failed active piece releases the DMA thread as well as the request's
     * interrupt thread. No further piece can produce its DMA-end wake. */
    if(s->error && s->xfer_left) s->interrupts |= KUI_RETAIL_GD_IRQ_DMA_END;
'''
CALLBACK_FIX='''#endif
#if defined(KUI_RETAIL_CE) || KUI_NATIVE_GD_PIO_STREAM
        /* Removing a registration also cancels its undelivered callback.
         * Keeping a due flag with no callback can never be drained by EXEC
         * and would permanently prevent an otherwise idle VMU loan. */
'''
NATIVE_PROFILES={
    'native-sync':(),
    'native-background':('KUI_RETAIL_GD_ASYNC=1',),
    'native-session':('KUI_RETAIL_GD_ASYNC=1','KUI_NATIVE_SESSION_CONTROLS=1'),
    'native-mmu-stream':('KUI_RETAIL_GD_ASYNC=1','KUI_NATIVE_OCRAM_BOOTSTRAP=1',
        'KUI_NATIVE_GD_LEGACY_STREAM=1','KUI_NATIVE_GD_MMU_BUFFERS=1',
        'KUI_NATIVE_GD_DMA_STREAM=1','KUI_NATIVE_GD_PIO_STREAM=1','KUI_NATIVE_GD_VRAM_DMA=1')}
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(command):
    return subprocess.run(command,cwd=ROOT,text=True,stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',
        UBSAN_OPTIONS='halt_on_error=1'))
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default='cc');parser.add_argument('--report',type=Path)
    parser.add_argument('--baseline-ref',help='Optional historical Git ref; its full core hash must match the fixed reference')
    args=parser.parse_args();compiler=shutil.which(args.cc)
    if not compiler:raise RuntimeError('C compiler unavailable: '+args.cc)
    paths=[*SOURCES,'src/core/retail_gd.c',TEST,
           'tests/test_retail_async_ce.c','tests/sci_stream_model.h']
    before={name:digest(ROOT/name) for name in paths}
    report=dict(schema='kui.ce.gd-completion-lifecycle.v1',status='FAIL',
        baseline_ref=BASELINE,source_sha256=before,runs={},hardware_executed=False,
        limits=['SCI timing, CPU mappings, interrupt handoff and source card bytes use the existing authored fixture.',
                'Production GD dispatch, read_part, stream error handling and VMU loan execute; no CE kernel or complete game executes.'])
    try:
        if args.baseline_ref:
            old=subprocess.check_output(['git','show',args.baseline_ref+':src/core/retail_gd.c'],cwd=ROOT)
        else:
            # The local delivery commit can have a different commit identity
            # from its published, identical source tree. Reconstruct only the
            # two exact old hunks, then require the complete historical hash;
            # a fresh clone needs neither an unpublished object nor a network.
            text=(ROOT/'src/core/retail_gd.c').read_text()
            if text.count(DMA_FIX)!=1 or text.count(CALLBACK_FIX)!=1:
                raise AssertionError('Exact lifecycle fix hunks changed; supply --baseline-ref')
            old=text.replace(DMA_FIX,'').replace(CALLBACK_FIX,'').encode()
        if hashlib.sha256(old).hexdigest()!=BASELINE_SHA256:
            raise AssertionError('Historical GD core differs from the pinned full-source hash')
        report['baseline_core_sha256']=hashlib.sha256(old).hexdigest()
        with tempfile.TemporaryDirectory(prefix='ce-gd-completion-') as temporary:
            directory=Path(temporary);reference=directory/'old-retail-gd.c';reference.write_bytes(old)
            report['native_preservation']={}
            for name,defines in NATIVE_PROFILES.items():
                command=[compiler,'-E','-P','-Iinclude','-DKUI_ON_CONSOLE=1',*['-D'+define for define in defines]]
                a=run([*command,str(reference)]);b=run([*command,'src/core/retail_gd.c'])
                if a.returncode or b.returncode or a.stdout!=b.stdout:
                    raise AssertionError('Native preprocessed core changed: '+name+'\n'+a.stderr+b.stderr)
                report['native_preservation'][name]=dict(preprocessed_identical=True,
                    sha256=hashlib.sha256(b.stdout.encode()).hexdigest(),defines=defines)
            for variant,core in [('current',ROOT/'src/core/retail_gd.c'),('baseline',reference)]:
                binary=directory/variant
                command=[compiler,*FLAGS,*SOURCES,str(core),TEST,'-o',str(binary)]
                built=run(command)
                if built.returncode:raise AssertionError(built.stdout+built.stderr)
                report['runs'][variant]={}
                for case in CASES:
                    result=run([str(binary),case])
                    expected=1 if variant=='baseline' and case!='dma-failure-no-piece' else 0
                    if result.returncode!=expected or result.stderr:
                        raise AssertionError(f'{variant}/{case}: expected exit{expected}, got{result.returncode}\n{result.stdout}{result.stderr}')
                    row=json.loads(result.stdout)
                    report['runs'][variant][case]=dict(exit_code=result.returncode,result=row)
        if before!={name:digest(ROOT/name) for name in paths}:raise AssertionError('Input source changed during test')
        report['status']='PASS'
    finally:
        if args.report:
            args.report.parent.mkdir(parents=True,exist_ok=True)
            args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS CE callback removal releases VMU loan; DMA failure wakes active piece; old source fails both controls')
if __name__=='__main__':main()
