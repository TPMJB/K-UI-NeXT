#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production native PIO39 core; mapping/progress are authored boundaries."""
import argparse,hashlib,json,os,shlex,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCES=('tests/test_native_pio_stream.c','src/core/retail_gd.c')
REGRESSIONS=(('dma38','tests/test_native_dma_stream.c'),('legacy28','tests/test_native_legacy_stream.c'))
FLAGS=('-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic','-fno-pie','-no-pie',
 '-fno-omit-frame-pointer','-fno-sanitize-recover=all','-Iinclude','-DKUI_ON_CONSOLE=1',
 '-DKUI_RETAIL_GD_ASYNC=1','-DKUI_NATIVE_OCRAM_BOOTSTRAP=1','-DKUI_NATIVE_GD_LEGACY_STREAM=1',
 '-DKUI_NATIVE_GD_MMU_BUFFERS=1','-DKUI_NATIVE_GD_DMA_STREAM=1','-DKUI_RETAIL_NATIVE_DRIVE_STATE=1')
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(args,env=None):return subprocess.run(args,cwd=ROOT,text=True,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);p.add_argument('--cc',default='cc');a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);cc=Path(shutil.which(a.cc)or a.cc).resolve();paths={ROOT/x for x in SOURCES}|{ROOT/v for _,v in REGRESSIONS}|{Path(__file__).resolve()};logs=[]
 for enabled in (0,1):
  dep=run([str(cc),*FLAGS,'-DKUI_NATIVE_GD_PIO_STREAM='+str(enabled),'-MM',*SOURCES]);assert dep.returncode==0,dep.stdout
  for line in dep.stdout.replace('\\\n',' ').splitlines():paths.update((ROOT/n).resolve()for n in shlex.split(line.partition(':')[2]))
 hashes={str(x):sha(x)for x in sorted(paths)}
 report=dict(status='FAIL',schema='kui.native.pio-stream-core.v1',source_sha256=hashes,compiler=dict(path=str(cc),sha256=sha(cc)),runs={},hardware_executed=False,
  limits=['Actual native GD dispatch executes with authored virtual CPU and physical DMA banks.',
   'The mapper represents virtual PIO payload admission; actual resident mapper, SCI reader and target instructions do not execute.',
   'Data delivery is reported through the production progress API; callbacks are never invoked by this core test.',
   'Deferred callback execution, kernel scheduling, hardware memory translation and complete VC2 remain outside this test.'])
 try:
  old_commit='40034a198042ca7d682830920adee7b8c99aa835'
  old_source=subprocess.check_output(['git','show',old_commit+':src/core/retail_gd.c'],cwd=ROOT)
  with tempfile.TemporaryDirectory(prefix='pio-disabled-',dir=out)as directory:
   reference=Path(directory)/'core.c';reference.write_bytes(old_source)
   # FN6 now captures rejected packets without modifying GD service state.
   # Its intentional source delta invalidates the old token-for-token
   # preprocessing claim. Run the historical DMA/legacy contract fixtures
   # against both cores instead, under identical current headers/flags.
   control=dict(status='PASS',old_commit=old_commit,
    old_source_sha256=hashlib.sha256(old_source).hexdigest(),current_source_sha256=sha(ROOT/'src/core/retail_gd.c'),runs={},
    scope='Historical DMA38/legacy28 host contract fixtures pass on reference and current cores with PIOflag0. Result rows match; this is bounded behavioral regression evidence, not source or whole-program equivalence.')
   for name,test_path in REGRESSIONS:
    test_source=subprocess.check_output(['git','show',old_commit+':'+test_path],cwd=ROOT)
    test=Path(directory)/(name+'-control.c');test.write_bytes(test_source)
    control['runs'][name]=dict(test_sha256=hashlib.sha256(test_source).hexdigest(),sanitizers={})
    for mode,san in [('ubsan','undefined'),('asan-ubsan','address,undefined')]:
     results={}
     for variant,source in [('reference',reference),('current',ROOT/'src/core/retail_gd.c')]:
      exe=out/('pio-disabled-'+name+'-'+variant+'-'+mode)
      cmd=[str(cc),*FLAGS,'-DKUI_NATIVE_GD_PIO_STREAM=0','-fsanitize='+san,str(test),str(source),'-o',str(exe)]
      built=run(cmd);logs.append(' '.join(cmd)+'\n'+built.stdout);assert built.returncode==0,built.stdout
      done=run([str(exe)],dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));logs.append(done.stdout);assert done.returncode==0,done.stdout
      row=json.loads(done.stdout);assert row['checks']and not row['io_reads']and not row['hardware_executed']
      results[variant]=dict(command=cmd,executable_sha256=sha(exe),result=row)
     assert results['reference']['result']==results['current']['result'],'Historical PIO-disabled contract result changed: '+name
     control['runs'][name]['sanitizers'][mode]=results
   report['disabled_profile_contract_regressions']=control
   (out/'pio-disabled-core-contract-regressions.json').write_text(json.dumps(control,indent=2)+'\n')
  for enabled in (0,1):
   profile='pio39'if enabled else'disabled';report['runs'][profile]={}
   for mode,san in [('ubsan','undefined'),('asan-ubsan','address,undefined')]:
    exe=out/(profile+'-'+mode);cmd=[str(cc),*FLAGS,'-DKUI_NATIVE_GD_PIO_STREAM='+str(enabled),'-DKUI_NATIVE_GD_VRAM_DMA='+str(enabled),'-fsanitize='+san,*SOURCES,'-o',str(exe)]
    built=run(cmd);logs.append(' '.join(cmd)+'\n'+built.stdout);assert built.returncode==0,built.stdout
    done=run([str(exe)],dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));logs.append(done.stdout);assert done.returncode==0,done.stdout
    row=json.loads(done.stdout);assert row['enabled']==bool(enabled)and row['checks']and row['cases']and not row['io_reads']and not row['hardware_executed']
    report['runs'][profile][mode]=dict(command=cmd,executable_sha256=sha(exe),result=row)
  report['pio_enabled_regressions']={}
  for name,source in REGRESSIONS:
   report['pio_enabled_regressions'][name]={}
   for mode,san in [('ubsan','undefined'),('asan-ubsan','address,undefined')]:
    exe=out/(name+'-'+mode);cmd=[str(cc),*FLAGS,'-DKUI_NATIVE_GD_PIO_STREAM=1','-DKUI_NATIVE_GD_VRAM_DMA=1','-fsanitize='+san,source,'src/core/retail_gd.c','-o',str(exe)]
    built=run(cmd);logs.append(' '.join(cmd)+'\n'+built.stdout);assert built.returncode==0,built.stdout
    done=run([str(exe)],dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));logs.append(done.stdout);assert done.returncode==0,done.stdout
    row=json.loads(done.stdout);assert row['checks']>190 and not row['io_reads']and not row['hardware_executed']
    report['pio_enabled_regressions'][name][mode]=dict(command=cmd,executable_sha256=sha(exe),result=row)
  report['persistent_callback_words']=3
  assert hashes=={k:sha(k)for k in hashes};report['source_hashes_unchanged_after_tests']=True;report['status']='PASS'
 except Exception as e:report['error']=str(e);raise
 finally:
  (out/'native-pio-stream.log').write_text('\n'.join(logs));(out/'native-pio-stream.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS native command39 core:',{k:{m:v['result']['checks']for m,v in row.items()}for k,row in report['runs'].items()})
if __name__=='__main__':main()
