#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real native GD core with/without the OCRAM CPU-buffer bridge.

The authored mapper models only checked CPU OCRAM aliases and native RAM.
It does not execute resident map_guest, MMIO, SCI, SH startup or a CE kernel.
"""
import argparse,hashlib,json,os,shlex,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCES=('tests/test_native_ocram_bootstrap.c','src/core/retail_gd.c')
FLAGS=('-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
       '-fno-pie','-no-pie','-fno-omit-frame-pointer','-fno-sanitize-recover=all',
       '-DKUI_ON_CONSOLE=1','-DKUI_RETAIL_GD_ASYNC=1',
       '-DKUI_RETAIL_NATIVE_DRIVE_STATE=1','-Iinclude')
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def run(command,env=None):return subprocess.run(command,cwd=ROOT,text=True,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
def integration():
    text=(ROOT/'src/loader/retail_resident.c').read_text()
    begin=text.index('static uint8_t *map_guest(')
    function=text[begin:text.index('\n#ifndef KUI_RETAIL_ASYNC',begin)]
    # Select the actual OCRAM if-scope, not an earlier feature-guarded
    # declaration. CPU virtual admission precedes it in the opt-in MMU build.
    first=function.index('if(kui_retail_ocram_address(address)) {')
    opening=function.index('{',first);depth=1;end=opening+1
    while depth:
        depth+=(function[end]=='{')-(function[end]=='}');end+=1
    policy=function[first:end]
    assert end<function.index('if(address < KUI_RETAIL_IP_ADDRESS')
    assert function.index('if(service.cpu_mmu')<first
    assert function.index('uint32_t why;')<first
    assert function.index('uint8_t *result=NULL;')<first
    guard='if(writing==KUI_RETAIL_MAP_VALIDATE) goto ocram_mapping_done;'
    mmu='if(mmucr&1u) goto ocram_mapping_done;'
    cache='if((ccr&0xa0u)!=0x20u) goto ocram_mapping_done;'
    span='if(!kui_retail_ocram_span(address,bytes,mmucr,ccr)) goto ocram_mapping_done;'
    accepted='result=(uint8_t *)(uintptr_t)address;'
    assert all(part in policy for part in (guard,mmu,cache,span,accepted))
    assert policy.index(guard)<policy.index('0xff000010u')<policy.index(mmu)<policy.index(cache)<policy.index(span)<policy.index(accepted)
    assert 'address|=' not in policy and 'address=' not in policy
    # Reason writes are observable diagnostics; they do not grant admission.
    # Bind each reason to the same refusal/acceptance sequence as the live mapper.
    for number,after in ((2,guard),(3,mmu),(4,cache),(5,span),(1,accepted)):
        reason='why='+str(number)+'u;'
        assert reason in policy and policy.index(reason)<policy.index(after)
    assert 'kui_native_ocram_trace.entry[14]=(uint32_t)(uintptr_t)result;' in policy
    assert 'kui_native_ocram_trace.entry[15]=why;' in policy
    assert policy.index('ocram_mapping_done:')<policy.index('return result;')
    tail=function[end:]
    assert tail.index('why=7u;')<tail.index('if(address < KUI_RETAIL_IP_ADDRESS')<tail.index('why=6u;')
    assert '#define RETURN_BAD_MAP() goto ocram_mapping_done' in tail
    converted='result=(uint8_t *)(uintptr_t)((address&0x1fffffffu)|0xa0000000u);'
    assert tail.index(converted)<tail.index('why=6u;')<tail.index('goto ocram_mapping_done;',tail.index('why=6u;'))
    assert text.index('Every native address')<begin
    return dict(real_policy_helper=True,raw_CPU_pointer_preserved=True,
                READ_payloads_refused_before_MMIO=True,reason_sequence_bound=True,
                resident_map_guest_executed=False)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',default='cc');a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    compiler=Path(shutil.which(a.cc) or a.cc).resolve();compiler_hash=sha(compiler)
    source_paths={ROOT/x for x in SOURCES}|{Path(__file__).resolve(),ROOT/'src/loader/retail_resident.c',ROOT/'include/kui/retail_ocram.h'}
    profiles={'baseline':('-DKUI_NATIVE_OCRAM_BOOTSTRAP=0',),'ocram':('-DKUI_NATIVE_OCRAM_BOOTSTRAP=1',)}
    for defines in profiles.values():
        dep=run([str(compiler),*FLAGS,*defines,'-MM',*SOURCES]);assert dep.returncode==0,dep.stdout
        for line in dep.stdout.replace('\\\n',' ').splitlines():
            source_paths.update((ROOT/name).resolve() for name in shlex.split(line.partition(':')[2]))
    hashes={str(x):sha(x) for x in sorted(source_paths)}
    report=dict(schema='kui.native.ocram-bootstrap.host.v1',status='FAIL',source_root=str(ROOT),source_sha256=hashes,
                compiler=dict(path=str(compiler),sha256=compiler_hash),source_integration=integration(),runs={},
                limits=['The production native GD core executes with the actual async-protocol macro and native drive-state macro.',
                        'The authored mapper applies the same pure policy and no-payload guard; it is not resident map_guest execution.',
                        'The host array models OIX=0 OCRAM bank aliases, not CPU caches or MMU behavior.',
                        'No target entry, hardware MMIO, SCI transfer, whole startup, CE kernel or gameplay is executed.'],hardware_executed=False)
    logs=[]
    try:
        for profile,defines in profiles.items():
            report['runs'][profile]={}
            for mode,san in [('ubsan','undefined'),('asan-ubsan','address,undefined')]:
                exe=out/('test-'+profile+'-'+mode)
                command=[str(compiler),*FLAGS,*defines,'-fsanitize='+san,*SOURCES,'-o',str(exe)]
                built=run(command);logs.append(profile+' '+mode+' compile\n'+built.stdout);assert built.returncode==0,built.stdout
                got=run([str(exe)],dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
                logs.append(profile+' '+mode+' execute\n'+got.stdout);assert got.returncode==0,got.stdout
                row=json.loads(got.stdout);assert row['enabled']==(profile=='ocram') and row['hardware_executed'] is False
                assert row['checks']>0 and row['policy_cases']>0 and row['bootstrap_cycles']>0 and row['negative_handle_cases']==9
                assert row['idle_zero_case_count']==19 and len(row['idle_zero_cases'])==19 and len(set(row['idle_zero_cases']))==19
                if profile=='baseline':assert row['raw_mapper_visits']==0 and row['payload_denials']==0
                else:assert row['raw_mapper_visits']>0 and row['payload_denials']==2
                assert row['first_check_result']==(2 if profile=='ocram' else -1)
                assert row['first_drive_result']==(0 if profile=='ocram' else -1)
                report['runs'][profile][mode]=dict(command=command,executable_sha256=sha(exe),result=row)
        traces={json.dumps(x['result']['ordinary_trace']) for v in report['runs'].values() for x in v.values()};assert len(traces)==1
        idle_names={json.dumps(x['result']['idle_zero_cases']) for v in report['runs'].values() for x in v.values()};assert len(idle_names)==1
        report['ordinary_native_observations_identical']=True
        report['idle_zero_case_count']=19
        assert hashes=={path:sha(path) for path in hashes} and sha(compiler)==compiler_hash
        report['source_hashes_unchanged_after_tests']=True;report['status']='PASS'
    except Exception as error:report['error']=str(error);raise
    finally:
        (out/'native-ocram-bootstrap.log').write_text('\n'.join(logs))
        (out/'native-ocram-bootstrap.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS native OCRAM bootstrap:',{k:{m:x['result']['checks'] for m,x in v.items()} for k,v in report['runs'].items()})
if __name__=='__main__':main()
