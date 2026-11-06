#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual core command38 and disabled-profile tests; mapping/progress are authored."""
import argparse,hashlib,json,os,shlex,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCES=('tests/test_native_dma_stream.c','src/core/retail_gd.c')
LEGACY_SOURCES=('tests/test_native_legacy_stream.c','src/core/retail_gd.c')
FLAGS=('-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic','-fno-pie','-no-pie',
       '-fno-omit-frame-pointer','-fno-sanitize-recover=all','-Iinclude','-DKUI_ON_CONSOLE=1',
       '-DKUI_RETAIL_GD_ASYNC=1','-DKUI_NATIVE_OCRAM_BOOTSTRAP=1','-DKUI_NATIVE_GD_LEGACY_STREAM=1',
       '-DKUI_NATIVE_GD_MMU_BUFFERS=1','-DKUI_RETAIL_NATIVE_DRIVE_STATE=1')
PROFILES=(('disabled',0,0),('dma38',1,0),('vram-dma38',1,1))
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(command,env=None):return subprocess.run(command,cwd=ROOT,text=True,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',default='cc');a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);cc=Path(shutil.which(a.cc) or a.cc).resolve()
    paths={ROOT/x for x in SOURCES+LEGACY_SOURCES}|{Path(__file__).resolve()}
    for _,enabled,vram in PROFILES:
        dep=run([str(cc),*FLAGS,'-DKUI_NATIVE_GD_DMA_STREAM='+str(enabled),'-DKUI_NATIVE_GD_VRAM_DMA='+str(vram),'-MM',*SOURCES]);assert dep.returncode==0,dep.stdout
        for line in dep.stdout.replace('\\\n',' ').splitlines():paths.update((ROOT/n).resolve() for n in shlex.split(line.partition(':')[2]))
    hashes={str(x):sha(x) for x in sorted(paths)};logs=[]
    report=dict(status='FAIL',schema='kui.native.dma-stream-core.v1',source_sha256=hashes,
        compiler=dict(path=str(cc),sha256=sha(cc)),runs={},hardware_executed=False,
        limits=['The actual native GD core executes with authored virtual CPU, main-RAM and bounded VRAM DMA mappings.',
                'Byte delivery and IRQ draining are explicit model boundaries; no payload is copied by this test.',
                'No target instruction, SCI peripheral, CE event delivery or complete game executes.'])
    try:
        for profile,enabled,vram in PROFILES:
            report['runs'][profile]={}
            for mode,san in [('ubsan','undefined'),('asan-ubsan','address,undefined')]:
                exe=out/(profile+'-'+mode);cmd=[str(cc),*FLAGS,'-DKUI_NATIVE_GD_DMA_STREAM='+str(enabled),'-DKUI_NATIVE_GD_VRAM_DMA='+str(vram),'-fsanitize='+san,*SOURCES,'-o',str(exe)]
                built=run(cmd);logs.append(' '.join(cmd)+'\n'+built.stdout);assert built.returncode==0,built.stdout
                done=run([str(exe)],dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));logs.append(done.stdout);assert done.returncode==0,done.stdout
                row=json.loads(done.stdout);assert row['enabled']==bool(enabled) and row['vram_enabled']==bool(vram) and row['checks']>0 and row['cases']>0 and not row['io_reads'] and not row['hardware_executed']
                report['runs'][profile][mode]=dict(command=cmd,executable_sha256=sha(exe),result=row)
        report['legacy_under_dma38']={}
        for mode,san in [('ubsan','undefined'),('asan-ubsan','address,undefined')]:
            exe=out/('legacy-under-dma38-'+mode);cmd=[str(cc),*FLAGS,'-DKUI_NATIVE_GD_DMA_STREAM=1','-fsanitize='+san,*LEGACY_SOURCES,'-o',str(exe)]
            built=run(cmd);logs.append(' '.join(cmd)+'\n'+built.stdout);assert built.returncode==0,built.stdout
            done=run([str(exe)],dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));logs.append(done.stdout);assert done.returncode==0,done.stdout
            row=json.loads(done.stdout);assert row['enabled'] and row['checks']>=210 and not row['io_reads'] and not row['hardware_executed']
            report['legacy_under_dma38'][mode]=dict(command=cmd,executable_sha256=sha(exe),result=row)
        sizes={x['result']['service_bytes'] for v in report['runs'].values() for x in v.values()};assert len(sizes)==1
        report['no_service_struct_growth']=True
        assert {x['result']['service_bytes'] for x in report['legacy_under_dma38'].values()}==sizes
        assert hashes=={k:sha(k) for k in hashes};report['source_hashes_unchanged_after_tests']=True;report['status']='PASS'
    except Exception as e:report['error']=str(e);raise
    finally:
        (out/'native-dma-stream.log').write_text('\n'.join(logs));(out/'native-dma-stream.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS native command38 core:',{k:{m:x['result']['checks'] for m,x in v.items()} for k,v in report['runs'].items()})
if __name__=='__main__':main()
