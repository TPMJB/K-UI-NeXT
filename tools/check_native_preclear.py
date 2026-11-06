#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Require reviewed stage code plus the original/linked preheap-clear proof."""
import argparse
import hashlib
import json
from pathlib import Path

from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_native_bottom_heap import records,pinned_records
from check_native_dual_callbacks import preclear_profile,pin_helpers,linked_table,expected_entries
from check_vmu_native_heap_probe import heap_resume_offset
from check_loader_layout import inspect_elf
import check_native_compact_background_profile as compact
import check_native_session_preclear_profile as session
import check_native_ordinary_preclear_profile as ordinary

ROOT=Path(__file__).resolve().parents[1]
SCHEMA='kui.native.preheap-clear.compiled.v1'
FLOW_SCHEMA='kui.native.preheap-clear.actual-execution.v1'
OWNER_SHA='a9fa66b6bcc482dfaa47683d552e42f24a8200c8efa5ed905d8184eedeba3ac0'
EXTRA_FUNCTIONS=frozenset(('_native_preclear_prepare@vmu_native_heap_probe_stage.c',
 '_native_preclear_arm@vmu_native_heap_probe_stage.c','_native_preclear_hook@vmu_native_heap_probe_stage.c',
 '_kui_native_preclear_finish','_kui_native_preclear_stage_install','_kui_vmu_native_heap_preclear_plan',
 '_native_arm_heap_hook@vmu_native_heap_probe_stage.c','_overlaps@vmu_native_heap.c'))
EXTRA_OBJECTS=frozenset(('_digest.2@vmu_native_heap.c','_helper.0@vmu_native_heap.c','_path.1@vmu_native_heap.c'))
HOOK_REFUSALS=('pr','begin','value','bytes','mask','control','tuple','binding','retired-literal')
FINISH_REFUSALS=('pr','BL','mask','binding','span','control','installer','stack',
 'tuple-target','tuple-begin','tuple-bytes','tuple-state','literal','restored-original-literal',
 *('plan-'+str(i) for i in range(5)))
TAIL_REFUSALS=('missing-low-PR-pop','JSR-instead-of-tail-JMP','missing-delay-slot-retirement')
FLOW_SOURCES=('tests/test_vmu_native_preclear_execution.py','tests/native_original_clear_sh.py',
 'tests/test_vmu_native_heap_execution.py','tools/check_native_bottom_heap.py',
 'tools/profile_vmu_native_release_sh.py','src/loader/vmu_native_preclear_stage.inc',
 'src/loader/vmu_native_preclear_tail.S','src/loader/vmu_maple_observe_boot.S',
 'src/core/vmu_native_heap.c','include/kui/vmu_native_heap.h',
 'tools/check_native_preclear.py','tools/check_native_session_preclear_profile.py',
 'tools/check_native_ordinary_preclear_profile.py',
 'tools/check_native_general_lean_profile.py','tools/check_native_compact_background_profile.py',
 'tools/native_runtime_layout.py')

def require(value,message):
    if not value:raise ValueError(message)
def sha(data):return hashlib.sha256(data).hexdigest()

def compiled_profile(stage):
    require(preclear_profile(stage),'Preheap proof needs both exact C/tail capability markers')
    if compact.stage_profile(stage):
        return dict(name='compact-background-72k',lease_bytes=0x47000,worker_bytes=0x12000)
    if session.stage_profile(stage):
        return dict(name=session.PROFILE,lease_bytes=session.LEASE_BYTES,worker_bytes=session.WORKER_BYTES)
    require(ordinary.stage_profile(stage),'Preheap proof needs a reviewed compact, General/session or ordinary General profile')
    return dict(name=ordinary.PROFILE,lease_bytes=ordinary.LEASE_BYTES,worker_bytes=ordinary.WORKER_BYTES)

def exact_closure(stage):
    selected=compiled_profile(stage)
    if selected['name']==session.PROFILE:
        # The session callback replaces the historical map-only binder. Its
        # complete helper bodies join the independently authored cold/preclear
        # closure; no missing helper is silently ignored by an old pin set.
        return session.closure(stage)
    if selected['name']==ordinary.PROFILE:
        return ordinary.closure(stage)
    require(set(compact.PRECLEAR_FUNCTIONS)==EXTRA_FUNCTIONS and set(compact.PRECLEAR_OBJECTS)==EXTRA_OBJECTS,
        'Preheap reviewed additional instruction/data closure differs')
    result=pinned_records(stage);available=records(stage)
    for pins,kind,flags in ((compact.PRECLEAR_FUNCTIONS,2,6),(compact.PRECLEAR_OBJECTS,1,2)):
        for name,expected in pins.items():
            require(name in available,'Preheap closure symbol missing: '+name)
            address,body,actual,section=available[name]
            require(actual==kind and section[1]==1 and section[2]&7==flags and
                (len(body),sha(body))==expected,'Preheap exact instructions/data differ: '+name)
            result[name]=dict(address=address,bytes=len(body),sha256=sha(body))
    pin_helpers(stage)
    return result

def low_resumes(stage,lows):
    require(preclear_profile(stage),'Preheap C/tail marker pair missing')
    linked_table(stage,tuple(expected_entries(low) for low in lows))
    result=[]
    for image in lows:
        symbol=image[4].get('_kui_vmu_observe_heap_hook')
        require(symbol is not None,'Preheap low hook missing')
        name='_kui_vmu_observe_heap_hook';address,body,_=symbol_bytes(image,name,2)
        offset=heap_resume_offset(image[0],inspect_elf(image[0],0x8c004000,0x8c007ba0),address,body)
        require(offset is not None and image[4].get('__kui_native_heap_resume_offset')==(offset,0,0,0xfff1),
            'Preheap return does not name its actual typed ABS low continuation')
        result.append(address+offset)
    return result

def require_companion(directory,stage,lows,proof):
    selected=compiled_profile(stage)
    require(isinstance(proof,dict) and proof.get('schema')==FLOW_SCHEMA and proof.get('source_sha256')==OWNER_SHA,
        'Preheap actual-original companion identity differs')
    names=('stage.elf','native-worker.elf','resident-sci.elf','resident-legacy.elf')
    require(proof.get('artifacts')=={name:sha((Path(directory)/name).read_bytes()) for name in names},
        'Preheap original/linked proof is detached from these exact artifacts')
    require(proof.get('proof_source_sha256')=={name:sha((ROOT/name).read_bytes()) for name in FLOW_SOURCES},
        'Preheap original/linked proof source changed')
    require(proof.get('original_clear_scalar_differential_cases')==21,
        'Preheap scalar decoder lacks its complete-state differentials')
    if selected['name'] in (session.PROFILE,ordinary.PROFILE):
        require(proof.get('compiled_profile')==selected,
                'General original companion names a different reviewed geometry')
    closure=exact_closure(stage)
    spans=[(row['address'],row['address']+row['bytes']) for name,row in closure.items()
        if name in compact.COLD_FUNCTIONS or name in compact.PRECLEAR_FUNCTIONS]
    executed=proof.get('executed_stage_addresses')
    require(isinstance(executed,list) and executed==sorted(set(executed)) and executed and
        all(isinstance(pc,int) and not pc&1 and any(first<=pc<last for first,last in spans) for pc in executed),
        'Preheap actual stage execution escaped its complete reviewed closure')
    require(all(closure[name]['address'] in executed for name in EXTRA_FUNCTIONS),
        'Preheap proof did not execute every additional stage helper')
    context=proof.get('inherited_context_source_review',{})
    require(context.get('imask_set_at')=='0x8c01021e' and context.get('intervening_entry')=='0x8c025be8' and
        context.get('mask_preserved_by')==['0x8c03ab34','0xac025710'] and
        context.get('mask_restored_at')==['0x8c03ac8c','0xac025796'] and context.get('BL_cleared_at')=='0x8c03acc2' and
        context.get('inherited_IMASK')==15 and context.get('inherited_BL')==0 and
        context.get('positive_entry_SR')=='0x600000f1','Preheap inherited SR is not bound to the reviewed original prelude')
    end=stage[4]['__retail_stage_bss_end'][0];resumes=low_resumes(stage,lows)
    rows=proof.get('cases');require(isinstance(rows,list) and len(rows)==2,'Preheap proof lacks both actual readers')
    require({row.get('reader') for row in rows}=={1,2},'Preheap reader matrix differs')
    for row in rows:
        reader=row['reader'];require(row.get('stage_end')==hex(end) and row.get('owner_sp')=='0x8c00f3c4' and
            row.get('entry_sr')=='0x600000f1' and
            row.get('low_resume')==hex(resumes[reader-1]),'Preheap actual stage/owner/low geometry differs')
        calls=row.get('clear_calls');expected=((0x8c3b0000,0,0x8ce00000-0x8c3b0000),
            (end,0,0x8d000000-end),(0x8ce00000,0,end-0x8ce00000))
        require(isinstance(calls,list) and len(calls)==3,'Preheap exact three clear calls missing')
        for call,args in zip(calls,expected):
            require(call.get('args')==list(args) and call.get('byte_writes')==args[2] and
                call.get('instructions')==5*args[2]+7 and call.get('first')==args[0] and
                call.get('last')==args[0]+args[2]-1,'Preheap original clear instructions/byte partition differ')
        lease=selected['lease_bytes']
        require(row.get('initializer_calls')==1 and row.get('actual_allocator_calls')==[
            ['allocate',0xc50000-lease-64],['allocate',lease],['release',0x8c3b0000+lease+64]],
            'Preheap proof changed the exact original bottom allocator calls')
        for field in ('complete_stage_erased_after_installer','no_high_fetch_read_write_after_erasure',
            'full_retained_header_and_payload_preserved','all_original_initializer_registers_and_SR_MAC_GBR_restored',
            'repeated_original_clear_reaches_low_terminal_before_any_store'):
            require(row.get(field) is True,'Preheap original/linked proof lacks '+field)
        require(row.get('worker_handoff_leaf_postconditions_modeled')==1,
            'Preheap proof changed its explicit separately validated worker-handoff boundary')
    failures=proof.get('refusals');expected={(reader,phase,name) for reader in (1,2)
        for phase,names in (('hook',HOOK_REFUSALS),('finish',FINISH_REFUSALS),('tail-code',TAIL_REFUSALS)) for name in names}
    require(isinstance(failures,list) and len(failures)==len(expected) and
        {(row.get('reader'),row.get('phase'),row.get('mutation')) for row in failures}==expected,
        'Preheap actual linked refusal/counterfactual matrix differs')
    for row in failures:
        if row['phase']=='tail-code':require(row.get('erasure_not_admitted') is True,'Unsafe tail erasure was not rejected')
        else:
            detail=0x51 if row['phase']=='hook' else (0x56 if row['mutation'] in
                ('pr','BL','mask','binding','span',*('plan-'+str(i) for i in range(5))) else 0x57)
            require(row.get('refusal')==[2,detail],'Preheap refusal occurred at the wrong ownership boundary')
    original=proof.get('original_unintercepted_clear_erases_stage',{})
    require(original==dict(instructions=5*0xc50000+7,byte_writes=0xc50000,first=0x8c3b0000,last=0x8cffffff),
        'Preheap proof omits the actual original stage-erasure regression')
    return True

def check(directory,*,proof_path=None,self_test=False):
    directory=Path(directory);stage=read_elf(directory/'stage.elf')
    if not preclear_profile(stage):return dict(schema=SCHEMA,enabled=False)
    closure=exact_closure(stage);lows=[read_elf(directory/name) for name in ('resident-sci.elf','resident-legacy.elf')]
    path=Path(proof_path) if proof_path else directory/'native-preclear-proof.json'
    require(path.is_file(),'Preheap capability requires its actual original/linked execution companion')
    proof=json.loads(path.read_text());require_companion(directory,stage,lows,proof)
    result=dict(schema=SCHEMA,enabled=True,compiled_profile=compiled_profile(stage),
        exact_linked_closure=closure,low_resume_addresses=low_resumes(stage,lows),
        actual_original_linked_companion_sha256=sha(path.read_bytes()),original_linked_readers=2,
        original_linked_refusals=len(proof['refusals']),full_original_corpus=False)
    if self_test:
        available=records(stage);mutants=0
        for name in (*compact.PRECLEAR_FUNCTIONS,*compact.PRECLEAR_OBJECTS):
            address,body,kind,section=available[name];start=section[4]+address-section[3]
            for byte in range(len(body)):
                for bit in (range(8) if kind==1 else (0,)):
                    changed=bytearray(stage[0]);changed[start+byte]^=1<<bit
                    try:exact_closure((bytes(changed),*stage[1:]))
                    except ValueError:mutants+=1
                    else:raise ValueError('Preheap pin accepted changed instructions/data: '+name)
        refusals=0
        for field,value in (('schema','bad'),('source_sha256','0'*64),('artifacts',{}),('proof_source_sha256',{}),
            ('original_clear_scalar_differential_cases',0),('cases',[]),('refusals',[]),
            ('executed_stage_addresses',[]),('inherited_context_source_review',{}),
            ('original_unintercepted_clear_erases_stage',{})):
            changed={**proof,field:value}
            try:require_companion(directory,stage,lows,changed)
            except ValueError:refusals+=1
            else:raise ValueError('Preheap companion accepted changed field: '+field)
        result['self_tests']=dict(exact_code_bytes_and_object_bits_rejected=mutants,companion_refusals=refusals)
        typed=({session.PROFILE:session,ordinary.PROFILE:ordinary}).get(compiled_profile(stage)['name'])
        if typed is not None:
            changed={**proof,'compiled_profile':{**proof['compiled_profile'],'lease_bytes':0x47000}}
            try:require_companion(directory,stage,lows,changed)
            except ValueError:refusals+=1
            else:raise ValueError('Preheap companion accepted a different General lease')
            result['self_tests']['companion_refusals']=refusals
            result['self_tests']['typed_session_closure' if typed is session else 'typed_ordinary_closure']=typed.mutation_tests(stage)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    parser.add_argument('--proof',type=Path);parser.add_argument('--self-test',action='store_true');a=parser.parse_args()
    try:print(json.dumps(check(a.directory,proof_path=a.proof,self_test=a.self_test),indent=2))
    except (OSError,ValueError,KeyError,AssertionError) as error:parser.exit(2,'Preheap linked proof refused: '+str(error)+'\n')
if __name__=='__main__':main()
