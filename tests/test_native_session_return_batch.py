#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check bounded return-runtime reads through production session preparation.

The existing session fixture supplies valid map/descriptor contracts. FatFs
access is a read-only real-file stream seam; CRC, rolling marker recognition,
plan encoding and atomic failure execute production code. The frozen original
implementation provides the counted 4 KiB control and byte-for-byte plan oracle.
"""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from runtime_package import envelope

ORIGINAL='3d9d015b95f9d2aa7f0d724a7e546a385a3a754a'
PAYLOAD_BYTES=3554304
MARKER=bytes.fromhex('4b554953424f4f540100000003000000fcffffff')
SOURCES=('src/core/native_session.c','src/core/native_return.c','src/core/storage_policy.c',
    'src/core/runtime_image.c','src/core/retail_image.c',
    'src/core/game_launch_settings.c','src/core/game_ram_patches.c','src/core/data.c')

def run(command,**kwargs):
    p=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,**kwargs)
    if p.returncode:raise AssertionError(shlex.join(map(str,command))+'\n'+p.stdout+p.stderr)
    return p.stdout

def main():
    with tempfile.TemporaryDirectory(prefix='kui-return-batch-') as temporary:
        tmp=Path(temporary)
        old=tmp/'original-session-prepare.c'
        old.write_text(run(['git','show',ORIGINAL+':src/apps/native_session_prepare.c']))
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
        flags=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-DKUI_GUI_FAST_CRC32=1','-Iinclude','-I.deps/fatfs/source','-Itests']
        binaries={}
        for label,source in [('old',old),('current',ROOT/'src/apps/native_session_prepare.c')]:
            binary=tmp/label
            run([*flags,'tests/test_native_session_return_batch.c',str(source),*SOURCES,
                '-Wl,--wrap=malloc,--wrap=free','-o',str(binary)])
            binaries[label]=binary
        outcomes=[]
        def fixture(name,positions,*,damage=False,truncated=False):
            body=bytearray(PAYLOAD_BYTES)
            for at in positions:body[at:at+len(MARKER)]=MARKER
            wire=bytearray(envelope(bytes(body),len(body),'0123456789ab'))
            if damage:wire[-4]^=1
            if truncated:del wire[-4:]
            path=tmp/(name+'.kui');path.write_bytes(wire);return path
        def check(label,path,*,fallback=False,fault=0,ok=True):
            output=tmp/(label+'-'+path.stem+'-'+str(fallback)+'-'+str(fault)+'.wire')
            result=json.loads(run([str(binaries[label]),str(path),str(int(fallback)),
                str(fault),str(int(ok)),str(output)],env=env))
            outcomes.append(dict(implementation=label,fixture=path.name,fallback=fallback,fault=fault,**result))
            assert result['opens']==1 and result['closes']==(0 if fault==6 else 1)
            return result,output.read_bytes() if ok else None
        for at in (0,4096-8,32768-8,PAYLOAD_BYTES-20):
            path=fixture('valid-'+str(at),[at])
            old_result,old_wire=check('old',path)
            result,wire=check('current',path)
            fallback_result,fallback_wire=check('current',path,fallback=True)
            assert old_wire==wire==fallback_wire
            assert old_result['payload_reads']==fallback_result['payload_reads']==868
            assert result['payload_reads']==109 and result['largest']==32768
            assert fallback_result['largest']==4096 and fallback_result['batch_allocations']==1
        path=fixture('valid-faults',[32768-8])
        for fault in range(1,8):
            for fallback in (False,True):check('current',path,fallback=fallback,fault=fault,ok=False)
        for name,path in [('absent',fixture('absent',[])),
                          ('duplicate',fixture('duplicate',[4096-8,32768-8])),
                          ('changed',fixture('changed',[32768-8],damage=True)),
                          ('truncated',fixture('truncated',[32768-8],truncated=True))]:
            for label in ('old','current'):check(label,path,ok=False)
        report=dict(status='PASS',schema='kui.native.return-runtime-batching.v1',
            payload_bytes=PAYLOAD_BYTES,old_payload_reads=868,current_payload_reads=109,
            fallback_payload_reads=868,identical_complete_sealed_plans=True,
            original_source_commit=ORIGINAL,original_source_sha256=hashlib.sha256(old.read_bytes()).hexdigest(),
            source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                for name in ('src/apps/native_session_prepare.c','tests/test_native_session_return_batch.c',
                             'tests/test_native_session_return_batch.py','tests/test_native_session_prepare.c',*SOURCES)},
            checks=outcomes,console_timing_measured=False,
            limits='Production preparation and actual-file stream reads; hardware/FatFs mapping is an explicit existing-fixture seam.')
        if len(sys.argv)>1:Path(sys.argv[1]).write_text(json.dumps(report,indent=2)+'\n')
        print('PASS return-runtime batching: 868 -> 109 payload reads; identical full checked plans; 4 KiB allocation-failure fallback and malformed/changed/cancel/error cleanup controls')

if __name__=='__main__':main()
