#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate DATA tuning profiles against literal caps and independently timed DMA."""
import argparse, hashlib, json, os, re, shlex, subprocess
from pathlib import Path
import test_toy_data_budget as baseline
ROOT=Path(__file__).resolve().parents[1]
FIXTURE='tests/test_toy_data_tuning.c'
WIRE_FIXTURE='tests/test_toy_startup_sci_wire.c'
HEADER='src/loader/toy_pilot_sci.h'
TUNING_HEADER='include/kui/toy_loader_data_tuning.h'
DESCRIPTORS={0:(6,24,12000,1024),1:(9,24,12000,2048),2:(10,24,9000,1024),3:(11,32,12000,2048),4:(12,32,18000,2048)}
MUTATIONS=[
 ('2048-step allowance reverted to AF1024',1,TUNING_HEADER,'#define KUI_TOY_TUNING_STEPS 2048u','#define KUI_TOY_TUNING_STEPS 1024u',1),
 ('AJ retained1024-step allowance doubled',2,TUNING_HEADER,'#define KUI_TOY_TUNING_STEPS 1024u','#define KUI_TOY_TUNING_STEPS 2048u',1),
 ('AK/AL32-block allowance reverted to24',3,TUNING_HEADER,'#define KUI_TOY_TUNING_BLOCKS 32u','#define KUI_TOY_TUNING_BLOCKS 24u',1),
 ('AJ9000-tick allowance reverted to12000',2,TUNING_HEADER,'#define KUI_TOY_TUNING_TICKS 9000u','#define KUI_TOY_TUNING_TICKS 12000u',1),
 ('AL18000-tick allowance reverted to12000',4,TUNING_HEADER,'#define KUI_TOY_TUNING_TICKS 18000u','#define KUI_TOY_TUNING_TICKS 12000u',1),
 ('tuning report ID shifted by one',1,TUNING_HEADER,'(8u + KUI_TOY_PILOT_DATA_TUNING_PROFILE)','(7u + KUI_TOY_PILOT_DATA_TUNING_PROFILE)',1),
 *[(label,1,baseline.ENGINE[0],needle,replacement,1) for label,needle,replacement in baseline.MUTATIONS],
 ('intercall IRQ blocks excluded from foreground quota',1,baseline.ENGINE[0],
  'if(e.verified_since<KUI_TOY_SCI_SERVICE_BLOCKS)++e.verified_since;',
  'if(!e.in_irq && e.verified_since<KUI_TOY_SCI_SERVICE_BLOCKS)++e.verified_since;',2),
]
UNSAFE=[{'KUI_TOY_PILOT_DATA_TUNING_PROFILE':-1},{'KUI_TOY_PILOT_DATA_TUNING_PROFILE':5},
 *[{'KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE':v} for v in [0,1,3,4]],
 *[{'KUI_TOY_PILOT_DATA_BUDGET_PROFILE':v} for v in [0,1,2,3]],
 *[{'KUI_TOY_PILOT_DATA_SCHEDULE_MODE':v} for v in [0,1,3]],
 {'KUI_TOY_PILOT_LOADER_TRACE':0},{'KUI_TOY_PILOT_DATA_PROBE':0},
 {'KUI_TOY_PILOT_PRIVATE_P2':0},{'KUI_TOY_PILOT_NATIVE_CACHE':1},
 {'KUI_TOY_PILOT_SHARED_SCI':1},{'KUI_TOY_PILOT_ASYNC_CDDA':1},
 {'KUI_TOY_PILOT_SYNTHETIC_SOURCE':1},{'KUI_TOY_PILOT_GD_FIXED_STEP':1},
 {'KUI_SCI_DMA_REUSE_TDRE':1},{'KUI_TOY_PILOT_DATA_PAYLOAD_MODE':1}]

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--cc',default=os.environ.get('CC','cc'))
 parser.add_argument('--build-dir',type=Path,default=ROOT/'build/tuning-tests')
 args=parser.parse_args();directory=args.build_dir.resolve();directory.mkdir(parents=True,exist_ok=True)
 cc=shlex.split(args.cc);flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
 results=[];observations={};mutations=[];guards=[]
 def defines_for(profile):
  return dict(baseline.DEFINES,KUI_TOY_PILOT_DATA_BUDGET_PROFILE=4,KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=2,KUI_TOY_PILOT_DATA_TUNING_PROFILE=profile)
 def compile_case(name,profile,fixture,mutation_dir=None):
  executable=directory/name;defines=defines_for(profile)
  if fixture==WIRE_FIXTURE:defines['KUI_SCI_STREAM_TEST']=1
  includes=([f'-I{mutation_dir}'] if mutation_dir else [])+['-Iinclude','-Isrc/loader']
  engine=str(mutation_dir/'toy_pilot_sci.c') if mutation_dir else baseline.ENGINE[0]
  sources=[fixture,engine,*baseline.ENGINE[1:]]
  command=[*cc,*flags,*includes,*[f'-D{k}={v}' for k,v in defines.items()],*sources,'-o',str(executable)]
  built=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=60)
  if built.returncode:raise RuntimeError(name+' compilation failed:\n'+built.stdout+built.stderr)
  return executable,command,sources
 def run_case(name,profile,fixture):
  executable,command,sources=compile_case(name,profile,fixture)
  run=subprocess.run([str(executable)],cwd=ROOT,env=env,capture_output=True,text=True,timeout=30)
  if run.returncode:raise RuntimeError(name+' failed:\n'+run.stdout+run.stderr)
  print(run.stdout.rstrip(),flush=True)
  results.append({'name':name,'tuning_profile':profile,'command':command,'stdout':run.stdout,'stderr':run.stderr,'exit_code':run.returncode,'source_sha256':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources}})
  if fixture!=FIXTURE:return
  match=re.search(r'report=(\d+) blocks=(\d+) ticks=(\d+) steps=(\d+); fast-cap=(\d+) time-cap=(\d+) timed-calls=(\d+); paced-blocks=(\d+) paced-polls=(\d+) paced-ticks=(\d+) paced-exits=(\d+)/(\d+)/(\d+); DATA_TRANSCRIPT=([0-9a-f]{8})',run.stdout)
  if not match:raise RuntimeError('Missing independently observed fixture output: '+name)
  v=tuple(map(int,match.groups()[:13]))
  if v[:4]!=DESCRIPTORS[profile] or v[4]!=DESCRIPTORS[profile][1]:raise RuntimeError('Literal descriptor mismatch: '+name)
  if not 0<v[5]<v[4]:raise RuntimeError('Clock cap did not independently bind: '+name)
  observations[profile]={'report_profile':v[0],'block_limit':v[1],'tick_limit':v[2],'step_limit':v[3],
   'fast_receiver_verified_blocks':v[4],'clock_limited_verified_blocks':v[5],'timed_calls':v[6],
   'independent_deadline_receiver':{'verified_blocks':v[7],'polls':v[8],'elapsed_ticks':v[9],'block_exits':v[10],'time_exits':v[11],'step_exits':v[12],'receiver_delay_ticks':400,'clock_tick_step':8,'per_poll_tick_cost':0,'forced_arrival_in_case':False},'DATA_transcript':match.group(14)}
 for profile in range(5):
  run_case(f'tuning-{profile}',profile,FIXTURE);run_case(f'tuning-wire-{profile}',profile,WIRE_FIXTURE)
 if observations[0]['independent_deadline_receiver']['step_exits']!=1:raise RuntimeError('AF step constraint not reproduced')
 if any(observations[p]['independent_deadline_receiver']['step_exits'] for p in [1,3,4]):raise RuntimeError('2048step guard unexpectedly binding')
 sources_text={p:(ROOT/p).read_text() for p in [baseline.ENGINE[0],HEADER,TUNING_HEADER]}
 for index,(label,profile,source,needle,replacement,count) in enumerate(MUTATIONS):
  text=sources_text[source]
  if text.count(needle)!=count:raise RuntimeError('Mutation anchor count changed: '+label)
  mutated=text.replace(needle,replacement);case_dir=directory/f'mutation-{index}';(case_dir/'kui').mkdir(parents=True,exist_ok=True)
  for path,name in [(baseline.ENGINE[0],'toy_pilot_sci.c'),(HEADER,'toy_pilot_sci.h'),(TUNING_HEADER,'kui/toy_loader_data_tuning.h')]:
   (case_dir/name).write_text(mutated if path==source else sources_text[path])
  executable,command,_=compile_case(f'mutant-{index}',profile,FIXTURE,case_dir)
  run=subprocess.run([str(executable)],cwd=ROOT,env=env,capture_output=True,text=True,timeout=30)
  if run.returncode==0 or 'Assertion' not in run.stderr:raise RuntimeError('Mutation was not rejected by assertion: '+label+'\n'+run.stderr)
  mutations.append({'name':label,'tuning_profile':profile,'source':source,'command':command,'exit_code':run.returncode,'assertion':run.stderr.strip().splitlines()[-1]})
 for changes in UNSAFE:
  defines=defines_for(1);defines.update(changes)
  command=[*cc,'-std=c11','-Iinclude','-Isrc/loader',*[f'-D{k}={v}' for k,v in defines.items()],'-x','c','-fsyntax-only','-']
  built=subprocess.run(command,cwd=ROOT,input='#include "kui/toy_loader_data_tuning.h"\n',capture_output=True,text=True,timeout=30)
  if built.returncode==0 or '#error' not in built.stderr:raise RuntimeError('Unsafe tuning accepted: '+repr(changes))
  guards.append({'changed_defines':changes,'command':command,'exit_code':built.returncode})
 pin=[FIXTURE,WIRE_FIXTURE,'tests/test_toy_pilot_sci.c','tests/test_toy_pilot_sci_async_integration.c','tests/sci_stream_model.h',HEADER,TUNING_HEADER,'include/kui/toy_loader_ad_followup.h','src/loader/sci_stream.c']
 report={'schema':1,'sanitizers':['address','undefined'],'scope':'Actual production DATA engine, GD service and image cursor. Independent DMA deadlines and real SCI/card framing. Hardware playback and audible quality unproven.','retained_AF_step_constraint_reproduced':True,'matrix_observations':observations,'results':results,'mutation_results':mutations,'unsafe_configurations':guards,'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in pin}}
 (directory/'tuning-data-tests.json').write_text(json.dumps(report,indent=2)+'\n')
 print(f'DATA tuning profiles 0..4 and 5 real-wire suites passed; {len(mutations)} behavioral mutations and {len(guards)} unsafe configurations rejected')
if __name__=='__main__':main()
