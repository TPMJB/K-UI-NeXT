#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production GD CPU-buffer/DMA policy with an explicitly authored mapper.

No actual resident mapper, MMIO, TLB handler, target instructions or payload
engine runs here. Existing finite-stream tests compare flag0/flag1 at cpu_mmu0.
"""
import argparse,hashlib,json,os,shlex,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CORE='src/core/retail_gd.c'
FLAGS=('-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic','-fno-pie','-no-pie',
       '-fno-omit-frame-pointer','-fno-sanitize-recover=all','-Iinclude','-DKUI_ON_CONSOLE=1',
       '-DKUI_RETAIL_GD_ASYNC=1','-DKUI_NATIVE_OCRAM_BOOTSTRAP=1','-DKUI_RETAIL_NATIVE_DRIVE_STATE=1',
       '-DKUI_NATIVE_GD_LEGACY_STREAM=1')
PROFILES=(('mmu-buffers','tests/test_native_mmu_buffers.c',1),
          ('at0-baseline','tests/test_native_legacy_stream.c',0),
          ('at0-candidate','tests/test_native_legacy_stream.c',1))
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(cmd,env=None):return subprocess.run(cmd,cwd=ROOT,text=True,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);p.add_argument('--cc',default='cc');a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);cc=Path(shutil.which(a.cc)or a.cc).resolve();paths={Path(__file__).resolve()};logs=[]
    for _,source,enabled in PROFILES:
        dep=run([str(cc),*FLAGS,'-DKUI_NATIVE_GD_MMU_BUFFERS='+str(enabled),'-MM',source,CORE]);assert dep.returncode==0,dep.stdout
        for line in dep.stdout.replace('\\\n',' ').splitlines():paths.update((ROOT/n).resolve()for n in shlex.split(line.partition(':')[2]))
    hashes={str(x):sha(x)for x in sorted(paths)}
    report=dict(status='FAIL',schema='kui.native.mmu-buffers-core.v1',source_sha256=hashes,compiler=dict(path=str(cc),sha256=sha(cc)),runs={},hardware_executed=False,
        limits=['Production GD dispatch runs with separate authored U0,0x0c process-slot,P3,OCRAM-numbered virtual and main-RAM banks.',
                'The mapping callback models live-MMU acceptance and refuses virtual payload validation; the actual resident mapper is not executed.',
                'Payload completion is authored through the production progress API; no payload bytes, SCI, MMIO, TLB handler, target gateway or console startup execute.',
                'Host structure size uses host pointer alignment; target structure geometry requires a separate cross-compiled check.'])
    try:
        for name,source,enabled in PROFILES:
            report['runs'][name]={}
            for mode,san in [('ubsan','undefined'),('asan-ubsan','address,undefined')]:
                exe=out/(name+'-'+mode);cmd=[str(cc),*FLAGS,'-DKUI_NATIVE_GD_MMU_BUFFERS='+str(enabled),'-fsanitize='+san,source,CORE,'-o',str(exe)]
                built=run(cmd);logs.append(' '.join(cmd)+'\n'+built.stdout);assert built.returncode==0,built.stdout
                done=run([str(exe)],dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));logs.append(done.stdout);assert done.returncode==0,done.stdout
                row=json.loads(done.stdout);assert row['checks']>0 and row['cases']>0 and row['io_reads']==0 and row['hardware_executed']is False
                report['runs'][name][mode]=dict(command=cmd,executable_sha256=sha(exe),result=row)
        for mode in ('ubsan','asan-ubsan'):
            old=report['runs']['at0-baseline'][mode]['result'];new=report['runs']['at0-candidate'][mode]['result']
            assert {k:v for k,v in old.items()if k!='service_bytes'}=={k:v for k,v in new.items()if k!='service_bytes'}
        report['at0_existing_stream_assertions_equal']=True
        assert hashes=={k:sha(k)for k in hashes};report['source_hashes_unchanged_after_tests']=True;report['status']='PASS'
    except Exception as e:report['error']=str(e);raise
    finally:
        (out/'native-mmu-buffers.log').write_text('\n'.join(logs));(out/'native-mmu-buffers.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS native MMU CPU buffers:',{k:{m:x['result']['checks']for m,x in v.items()}for k,v in report['runs'].items()})
if __name__=='__main__':main()
