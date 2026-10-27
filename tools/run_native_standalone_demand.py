#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run actual standalone reader C against the modeled card/SCI/DMA transport."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SOURCES=['tests/test_retail_async_standalone.c','src/loader/retail_async.c',
         'src/loader/sci_stream.c','src/core/retail_cursor.c',
         'src/core/retail_image.c','src/core/retail_gd.c']
DEFINITIONS=['KUI_ON_CONSOLE=1','KUI_NATIVE_READER_DIAGNOSTIC=1',
    'KUI_RETAIL_NATIVE_HYBRID=1','KUI_SCI_SD_NO_BLOCK=1','KUI_GENERAL_SCI_RESUME_FIX=1',
    'KUI_RETAIL_NATIVE_DISC_CHECK=1','KUI_RETAIL_NATIVE_DRIVE_STATE=1',
    'KUI_RETAIL_ASYNC_TEST=1','KUI_SCI_STREAM_TEST=1','KUI_RETAIL_ASYNC=1',
    'KUI_RETAIL_GD_ASYNC=1','KUI_RETAIL_TRANSPORT=1','KUI_RETAIL_LOW_RESIDENT=1',
    'KUI_NATIVE_REQUEST_TIMING=1','KUI_NATIVE_COOP_PUMP=1','KUI_NATIVE_IRQ_REARM=1',
    'KUI_NATIVE_LOADER_TIMELINE=1','KUI_NATIVE_SDK_SCI_ROUTE=1',
    'KUI_NATIVE_SDK_SCI_STRUCTURAL=1','KUI_NATIVE_STANDALONE_READER=1']


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/standalone-demand')
    parser.add_argument('--cc',default='cc')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fno-omit-frame-pointer',
           '-fno-pie','-no-pie','-fno-sanitize-recover=all',
           '-ffunction-sections','-fdata-sections','-Iinclude','-Isrc/loader','-Itests']
    results={};logs=[]
    for mode in ('ubsan','asan-ubsan'):
        exe=out/('test-standalone-demand-'+mode)
        sanitize='undefined' if mode=='ubsan' else 'address,undefined'
        command=[args.cc,*flags,'-fsanitize='+sanitize,
                 *['-D'+d for d in DEFINITIONS],
                 *(['-DKUI_STANDALONE_DEMAND_ASAN=1'] if mode=='asan-ubsan' else []),
                 *SOURCES,'-Wl,--gc-sections','-o',str(exe)]
        built=subprocess.run(command,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        logs.append(mode+' compilation\n'+built.stdout)
        if built.returncode:raise RuntimeError(built.stdout)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
        ran=subprocess.run([str(exe)],cwd=ROOT,env=env,text=True,
                           stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        logs.append(mode+' execution\n'+ran.stdout)
        (out/'standalone-demand.log').write_text('\n'.join(logs))
        if ran.returncode:raise RuntimeError(ran.stdout)
        rows=[line[len('STANDALONE_DEMAND_JSON '):] for line in ran.stdout.splitlines()
              if line.startswith('STANDALONE_DEMAND_JSON ')]
        if len(rows)!=1:raise ValueError('Missing unique demand test result')
        result=json.loads(rows[0]);assert result['byte_exact_requests']>=10
        assert result['route_loss_cases']==5 and result['cancel_cases']==3
        assert result['pio_bounded_entries']>100 and not result['hardware_executed']
        results[mode]=dict(result=result,command=command,executable=str(exe),sha256=sha(exe),
                           actual_c_first_binding=mode=='ubsan',
                           route_preinstalled_by_fixture=mode=='asan-ubsan')
    bound=[*SOURCES,'tests/test_retail_async.c','tests/sci_stream_model.h',
           'src/loader/native_standalone_reader.inc','src/loader/retail_async.h',
           'include/kui/native_standalone_reader.h','include/kui/native_sdk_sci_runtime.h',
           'tools/run_native_standalone_demand.py']
    receipt=dict(schema='kui.native.standalone-reader-demand-model.v1',result='PASS',
        transport='Explicit byte/card/SCI/DMA register model; no SD hardware',
        modes=results,source_sha256={name:sha(ROOT/name) for name in bound},
        production_sources_execute=['standalone binder','retail_async demand/IRQ delivery',
            'SCI stream CMD18/CRC/PIO/DMA logic','GD request/progress/result protocol',
            'image cursor/sector extraction'],
        substitutes=['Hardware register/card timing and CPU VBR/SR/cache operations are modeled.',
            'SDK callback assembly entry is an address-only symbol in this host fixture.',
            'The source/copied vector fixture is synthetic prior-stage context, not retail instructions.',
            'ASAN reserves SH P1 addresses as shadow, so only UBSAN performs exact-address fresh C binding.',
            'ASAN demand cases start with a preinstalled route; the actual binder returns at its first safe gate.'],
        console_compatibility_proved=False,hardware_executed=False,
        cache_ordering_proved=False,sh4_callback_execution_proved_here=False)
    (out/'standalone-demand.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS standalone demand models:',{name:value['result'] for name,value in results.items()})


if __name__=='__main__':main()
