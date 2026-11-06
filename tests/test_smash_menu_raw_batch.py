#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the real bounded capsule run reader against the existing SCI/card model."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from run_native_standalone_foreground import FLAGS,DEFINITIONS

def audit():
    report={'schema':'kui.cold-raw-dma.host.v1','status':'PASS','profiles':{},
        'hardware_executed':False,'limits':'Production C with authored SCI/card/DMA registers; no hardware timing or SH cache claim.'}
    with tempfile.TemporaryDirectory(prefix="kui-smash-raw-batch-") as folder:
        temporary=Path(folder)
        fixture=(ROOT/'tests/test_retail_async_foreground.c').read_text()
        if fixture.count('int main(')!=1:
            raise RuntimeError('Foreground fixture must have one reviewed entry')
        fixture=fixture.replace('int main(','int original_foreground_main(')
        (temporary/'foreground-fixture.inc').write_text(fixture)
        (temporary/"smash_menu_handoff_symbols.h").write_text(
            '#include <stdbool.h>\n#include <stdint.h>\n'
            'bool kui_smash_menu_raw_test_redirect(uint32_t,uint32_t);\n'
            '#define KUI_SMASH_MENU_LOADER_REDIRECT_P2 ((uintptr_t)kui_smash_menu_raw_test_redirect)\n')
        binary=temporary/"batch"
        definitions=(*DEFINITIONS,'KUI_NATIVE_OCRAM_BOOTSTRAP=1',
            'KUI_NATIVE_GD_LEGACY_STREAM=1','KUI_NATIVE_GD_MMU_BUFFERS=1',
            'KUI_NATIVE_GD_DMA_STREAM=1','KUI_NATIVE_GD_PIO_STREAM=1',
            'KUI_NATIVE_GD_VRAM_DMA=1','KUI_VC2_VMU_LATE=1','KUI_SMASH_MENU_LOADER=1')
        sources=('tests/test_smash_menu_raw_batch.c','src/loader/retail_async.c',
            'src/loader/sci_stream.c','src/core/retail_cursor.c',
            'src/core/retail_image.c','src/core/retail_gd.c')
        command=(*shlex.split(os.environ.get('HOST_CC','cc')),*FLAGS,
            '-fsanitize=address,undefined','-I'+str(temporary),
            '-DKUI_SMASH_FOREGROUND_FIXTURE="'+str(temporary/'foreground-fixture.inc')+'"',
            *('-D'+x for x in definitions),*sources,
            '-Wl,--wrap=kui_sci_stream_stop','-o',str(binary))
        subprocess.run(command,cwd=ROOT,check=True)
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
        outputs=[]
        for args in ((),('--stop',),('--dma-fault',)):
            run=subprocess.run((str(binary),*args),cwd=ROOT,env=env,check=True,text=True,
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            outputs.append(run.stdout)
        def packet(prefix):
            rows=[json.loads(line[len(prefix):])for output in outputs for line in output.splitlines()if line.startswith(prefix)]
            if len(rows)!=1:raise RuntimeError('Missing unique raw DMA test record: '+prefix)
            return rows[0]
        exact=packet('SMASH_RAW_DMA_JSON ');faults=packet('SMASH_RAW_DMA_FAULT_JSON ')
        if (exact['exact_runs'],exact['dma_blocks'],exact['crc_refusals'])!=(4,490,3) or \
           (faults['post_start_refusals'],faults['pre_start_pio_cases'],faults['pre_start_pio_blocks'])!=(5,2,8):
            raise RuntimeError('Raw DMA qualification omitted required cases')
        report['profiles']['smash_counted']={'exact':exact,'faults':faults,
            'ordinary_foreground':packet('FOREGROUND_READER_JSON '),'stop_quarantine':True}
        # Compile the original single-sector late-helper ABI as well. It has
        # no Smash count argument and must retain all prior refusal/CRC cases.
        single=temporary/'single-helper'
        single_definitions=tuple(d for d in definitions if d!='KUI_SMASH_MENU_LOADER=1')
        single_sources=('tests/test_retail_async_foreground.c',*sources[1:])
        single_command=(*shlex.split(os.environ.get('HOST_CC','cc')),*FLAGS,
            '-fsanitize=address,undefined',*('-D'+x for x in single_definitions),*single_sources,
            '-Wl,--wrap=kui_sci_stream_stop','-o',str(single))
        subprocess.run(single_command,cwd=ROOT,check=True)
        single_outputs=[]
        for args in ((),('--vc2-late-stop',)):
            run=subprocess.run((str(single),*args),cwd=ROOT,env=env,check=True,text=True,
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT);single_outputs.append(run.stdout)
        rows=[json.loads(line[len('VC2_LATE_CARD_JSON '):]) for output in single_outputs
            for line in output.splitlines()if line.startswith('VC2_LATE_CARD_JSON ')]
        reads=[row for row in rows if 'raw_card_reads' in row]
        stops=[row for row in rows if 'raw_stop_failure_cases' in row]
        if len(reads)!=1 or len(stops)!=1 or reads[0]['raw_card_reads']<1 or reads[0]['read_error_cases']<1 or \
           reads[0]['forced_PIO_reads']!=7 or \
           stops[0]['raw_stop_failure_cases']!=1 or stops[0]['sticky_quarantine_cases']!=1:
            raise RuntimeError('Single-helper ABI omitted its raw card cases')
        report['profiles']['vc2_single']={'late_card':reads[0],'stop_quarantine':stops[0]}
        paths=set(sources)|set(single_sources)|{'tests/test_smash_menu_raw_batch.py',
            'tests/test_retail_async.c','tests/sci_stream_model.h','src/loader/native_standalone_foreground.inc'}
        report['source_sha256']={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in sorted(paths)}
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path)
    args=parser.parse_args();result=audit();rendered=json.dumps(result,indent=2)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(rendered)
    print(rendered,end='')

if __name__=='__main__':main()
