#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay collection receipt, CE-helper and low-reader relaunch boundaries.

All executed instructions come from the supplied cohort's actual SH ELFs.
Old private state is an authored dirty pattern. These are separate boundary
checks, not execution of the original kernel, games or complete warm launch.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_native_gin_linked import Machine,State
from test_smash_menu_loader_runtime import card_receipt
from test_smash_ce_return_late import PINS

DATA=0x8c100000
HELPER=0xacfc4000
HELPER_BYTES=41152
OBJECTS=('_runtime','_loan','_a2_continuation','_fence_state','_vc2_saved_region',
         '_kui_armada_ce_maple_traffic_freeze')

def require(ok,message):
    if not ok:raise AssertionError(message)
def sha(data):return hashlib.sha256(data).hexdigest()


def decoder(directory):
    source=(ROOT/'tests/sh4_integer_fast.c').read_text()
    tag='    case 0x0002u:r[n]=c->sr;return;'
    require(source.count(tag)==1,'Integer decoder control-register seam is ambiguous')
    # The actual immutable helper entry uses this standard SH instruction.
    # No helper, CRC, copy, clear loop or memory callback is substituted.
    source=source.replace(tag,'    case 0x0012u:r[n]=c->gbr;return;\n'+tag)
    path=directory/'integer.c';path.write_text(source)
    binary=directory/'integer.so'
    subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O2','-shared','-fPIC',
        str(path),'-o',str(binary)],check=True,capture_output=True)
    library=ctypes.CDLL(str(binary));run=library.sh4_fast_run
    run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
    return run,dict(base_sha256=sha((ROOT/'tests/sh4_integer_fast.c').read_bytes()),
        adapted_source_sha256=sha(source.encode()),extension='STC GBR,Rn')


def receipt_lifecycle(outer,vc2,run):
    cases=[]
    for name,owner,sequence in (('cold_outer_to_vc2',1,1),('swirl_outer_to_vc2',4,2)):
        source=card_receipt(owner,sequence);cpu=Machine(run);cpu.load(outer);cpu.put(DATA,source);steps=0
        if owner==4:
            before=cpu.block(DATA,96)
            value,count,status=cpu.call(outer[4]['_kui_smash_card_receipt_transition_valid'][0],[DATA,3]);steps+=count
            require(status==0 and value==0 and cpu.block(DATA,96)==before,'Direct Swirl-to-VC2 transfer was accepted')
            value,count,status=cpu.call(outer[4]['_kui_smash_card_receipt_ce_return_valid'][0],[DATA]);steps+=count
            require(status==0 and value==1,'Source-owned CE return receipt was refused')
            value,count,status=cpu.call(outer[4]['_kui_smash_card_receipt_ce_return'][0],[DATA]);steps+=count
            require(status==0 and value==1 and cpu.block(DATA,96)==card_receipt(1,3),
                'CE return changed the receipt beyond its outer-owner/sequence/CRC transition')
        incoming=cpu.block(DATA,96);cpu.load(vc2);cpu.put(DATA,incoming)
        value,count,status=cpu.call(vc2[4]['_kui_smash_card_receipt_transition_valid'][0],[DATA,3]);steps+=count
        require(status==0 and value==1,'Fresh VC2 allocation transfer was refused')
        value,count,status=cpu.call(vc2[4]['_kui_smash_card_receipt_transfer'][0],[DATA,3]);steps+=count
        next_sequence=2 if owner==1 else 4
        require(status==0 and value==1 and cpu.block(DATA,96)==card_receipt(3,next_sequence),
            'VC2 owner/sequence/CRC transition differs')
        require(cpu.block(DATA+20,68)==source[20:88],'Generation, binding or admission changed across the epoch')
        cases.append(dict(case=name,initial_owner=owner,initial_sequence=sequence,final_owner=3,
            final_sequence=next_sequence,generation=7,binding_and_admission_unchanged=True,
            actual_linked_instructions=steps))
    return dict(cases=cases,direct_owner4_to3_refused=True,
        limits='Actual linked pure receipt+CRC operations; fresh CID, backend rebind and storage I/O are separate gates.')


def helper_reset(image,swirl,helper_binary,run):
    sy=image[4];lo,hi=sy['__kui_maple_workspace_start'][0],sy['__kui_maple_workspace_end'][0]
    tail=sy['__kui_maple_hook_binary_end'][0];top=sy['__kui_maple_stack_top'][0]
    require((lo,hi,tail,top)==(0xacfc0000,0xacfc3538,0xacfce0c0,0xacfcfe00),
        'Frozen helper private/immutable boundaries changed')
    require(len(helper_binary)==HELPER_BYTES and sha(helper_binary)==PINS[1],
        'VC2 helper differs from its independently frozen immutable component')
    require(all(sy[n][:2]==swirl[4][n][:2] for n in OBJECTS),
        'Old Swirl and new VC2 private-object layouts differ')
    cpu=Machine(run)
    # The source bridge's VMA is a CE virtual address; initialize its actual
    # physical LMA, exactly as the production transport lays out the helper.
    for part in image[2]:
        if part[0]==1:cpu.put(part[3]|0x20000000,
            image[0][part[1]:part[1]+part[4]]+bytes(part[5]-part[4]))
    require(cpu.block(HELPER,HELPER_BYTES)==helper_binary,'Helper physical image differs from its binary')
    cpu.put(lo,b'\x9d'*(hi-lo));cpu.put(tail,b'\x3c'*(0xacfd0000-tail))
    cpu.put(0xac004000,b'\x57'*0x3ea0)
    cpu.put(lo-32,b'\xa7'*32);cpu.put(0xacfd0000,b'\xa7'*32)
    code=cpu.block(HELPER,HELPER_BYTES)
    cpu.c.pc=sy['_kui_vc2_ce_vmu_prepare'][0];cpu.c.pr=0x8c003000;cpu.c.r[15]=0x8c007f80
    args=[0x8c007500,7,0x8c007b48];cpu.c.r[4:7]=args;cpu.c.ccr=0x909
    status=cpu.execute(break_pc=sy['_kui_vc2_ce_vmu_prepare_run'][0],budget=1000000)
    require(status==1 and list(cpu.c.r[4:7])==args,'Actual helper entry did not reach C with its original arguments')
    require(cpu.block(lo,hi-lo)==bytes(hi-lo),'New helper retained old lower workspace')
    require(cpu.block(tail,top-24-tail)==bytes(top-24-tail) and
        cpu.block(top,0xacfd0000-top)==bytes(0xacfd0000-top),
        'Mutable tail was not cleared before the exact24-byte saved native frame')
    require(cpu.block(HELPER,HELPER_BYTES)==code and cpu.block(0xac004000,0x3ea0)==b'\x57'*0x3ea0,
        'Helper entry changed immutable code or the low reader')
    require(cpu.block(lo-32,32)==cpu.block(0xacfd0000,32)==b'\xa7'*32,'Helper clear escaped its owned span')
    require(cpu.c.ccr_writes==1 and cpu.c.ccr_last_write&0x800 and cpu.c.r[15]==top-40,
        'ICI or wrapper frame/home-byte setup changed before cached C')
    for name in OBJECTS:
        at,size,_,_=sy[name]
        require(cpu.block(at,max(size,4))==bytes(max(size,4)),'Retained old helper object: '+name)
    return dict(helper_elf_sha256=sha(image[0]),previous_swirl_layout_equal=True,
        workspace=dict(begin=hex(lo),end=hex(hi),old_pattern_cleared=True),
        mutable_tail=dict(begin=hex(tail),end='0xacfd0000',cleared_before_wrapper_frame=True,frame_bytes=24),
        tracked_objects_zero=list(OBJECTS),immutable_helper_unchanged=True,low_reader_unchanged=True,
        ICI_before_cached_C_entry=True,constructor_args_preserved=True,actual_instructions=int(cpu.c.steps),
        limits='Actual accepted helper entry over authored old Swirl state to cached initializer C; coherent RAM/CCR observations only.')


def low_reader_reset(image,run):
    sy=image[4];begin,end=sy['__retail_resident_bss_begin'][0],sy['__retail_resident_bss_end'][0]
    require((begin,end,sy['_standalone_reader_region'][0],sy['_manifest'][0])==
        (0x8c007804,0x8c007e84,0x8c007500,0x8c007b48),'Frozen low-reader geometry changed')
    cpu=Machine(run);cpu.load(image);cpu.put(begin,b'\x9d'*(end-begin))
    before=cpu.block(0xac004000,begin-0x8c004000);after=cpu.block(end,32);raw=cpu.block(0xac008100,512)
    args=[0x8ce00000,0x8ce01000,0x8c000100,0x8ce02000]
    cpu.c.r[4:8]=args;cpu.c.r[15]=0x8cff0000;cpu.c.pc=sy['_start'][0];cpu.c.pr=0x8c003000
    status=cpu.execute(break_pc=sy['_kui_retail_resident_init'][0],budget=1000000)
    require(status==1 and cpu.block(begin,end-begin)==bytes(end-begin),
        'New reader entry retained old BSS before its constructor')
    require(list(cpu.c.r[4:8])==args,'Reader clear changed its prepared map/card/firmware/display arguments')
    require(cpu.block(0xac004000,begin-0x8c004000)==before and cpu.block(end,32)==after and
        cpu.block(0xac008100,512)==raw,'Reader clear changed initialized code, raw512 or its upper canary')
    return dict(resident_elf_sha256=sha(image[0]),
        bss=dict(begin=hex(begin),end=hex(end),bytes=end-begin,old_pattern_cleared=True),
        region=hex(sy['_standalone_reader_region'][0]),manifest=hex(sy['_manifest'][0]),
        raw512_unchanged=True,initialized_code_unchanged=True,constructor_args_preserved=True,
        actual_instructions=int(cpu.c.steps),
        limits='Actual reader entry clears authored inherited BSS before constructor; subsequent map/card adoption and physical SCI are separate gates.')


def audit(build):
    build=Path(build)
    files={name:build/relative for name,relative in (
        ('outer_stage','outer/stage.elf'),('vc2_stage','vc2/stage.elf'),
        ('vc2_helper','vc2/helper/helper.elf'),('swirl_helper','swirl/helper/helper.elf'),
        ('vc2_helper_binary','vc2/helper/helper.bin'),('vc2_resident','vc2/resident-sci.elf'))}
    inputs={str(path):sha(path.read_bytes())for path in files.values()}
    sources=('tests/test_smash_ce_relaunch_state.py','tests/sh4_integer_fast.c',
        'tests/test_smash_native_gin_linked.py','tests/test_smash_menu_loader_runtime.py',
        'tests/test_smash_ce_return_late.py','tools/check_vmu_native_worker_gateway.py',
        'src/core/smash_card_session.c','src/loader/retail_resident.S',
        'src/loader/vc2_ce_vmu_prepare_entry.S')
    source_before={name:sha((ROOT/name).read_bytes())for name in sources}
    images={name:read_elf(path)for name,path in files.items()if name!='vc2_helper_binary'}
    with tempfile.TemporaryDirectory(prefix='kui-ce-relaunch-state-')as folder:
        run,adaptation=decoder(Path(folder))
        receipt=receipt_lifecycle(images['outer_stage'],images['vc2_stage'],run)
        helper=helper_reset(images['vc2_helper'],images['swirl_helper'],files['vc2_helper_binary'].read_bytes(),run)
        reader=low_reader_reset(images['vc2_resident'],run)
    require(inputs=={str(path):sha(path.read_bytes())for path in files.values()},'Cohort changed during relaunch checks')
    require(source_before=={name:sha((ROOT/name).read_bytes())for name in sources},'Relaunch proof source changed during checks')
    return dict(schema='kui.smash-ce.relaunch-state.v1',status='PASS',inputs=inputs,
        test_sha256=source_before['tests/test_smash_ce_relaunch_state.py'],source_sha256=source_before,
        decoder=adaptation,receipt_lifecycle=receipt,helper_reset=helper,low_reader_reset=reader,
        limits=['These are independent actual-instruction boundary checks, not complete original Swirl→menu→VC2 execution.',
            'No original game bytes are embedded or required. Old private data is an authored pattern.',
            'No physical cache/MMU, IRQ scheduling, SD protocol, AICA/PVR state or console compatibility is certified.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--report',type=Path);args=parser.parse_args()
    result=audit(args.build);rendered=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(rendered)
    print(rendered,end='')
if __name__=='__main__':main()
