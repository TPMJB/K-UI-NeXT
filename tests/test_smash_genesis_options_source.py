#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute original Genesis combo/options/save/return integer routing.

Controller lookup, menu choice, opaque SDK/rendering jobs and the save job are
explicit boundaries. Original integer instructions connect those boundaries;
no original executable bytes are shipped and no save durability is claimed.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from test_smash_native_gin_linked import Machine,State,RETURN,STACK

OWNER_BYTES=1848288
OWNER_SHA='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
LOAD,GETPAD,FLAG=0x8c010000,0x8c1a9648,0x8c48f514
PAD,PAD6=0x8c700000,0x8c700200
SIZE,DIRTY,MASK,SELECT=0x8c48f3e0,0x8c48f3e8,0x8c15ed50,0x8c15ed4c
OPTIONS,UI,SAVE,RESET,CONFIRM=0x8c01adb0,0x8c01a9d6,0x8c018362,0x8c0117f0,0x8c01b71e
DESCRIPTOR,LABELS=0x8c15ed3c,0x8c15ebbc
SDK_AFTER_LOOP=(0x8c1a2130,0x8c1a1c4c)


def require(ok,message):
    if not ok:raise AssertionError(message)


def sha(data):return hashlib.sha256(data).hexdigest()
def put(machine,address,value):machine.put(address,struct.pack('<I',value))
def get(machine,address):return struct.unpack('<I',machine.block(address,4))[0]
def authored_return(machine,value=0):machine.c.r[0]=value;machine.c.pc=machine.c.pr


def combo(run,source,hijack=False):
    machine=Machine(run);machine.put(LOAD,source)
    machine.c.r[15]=STACK;machine.c.pc=0x8c019894;machine.c.pr=RETURN;machine.c.sr=0x40000000
    for address in(PAD,PAD6):machine.put(address,bytes(256));put(machine,address,1)
    put(machine,PAD+8,0x606);put(machine,PAD+16,8)
    put(machine,FLAG,0);put(machine,0x8c48f40c,0)
    machine.c.watch_first[0]=0x0c008100;machine.c.watch_end[0]=0x0c00c000
    ports=[]
    for _ in range(4):
        status=machine.execute(stop=RETURN,break_pc=GETPAD,budget=1000000)
        if status==0:break
        port=machine.c.r[4];require(port in(0,6),'Original input requested an unknown controller port')
        ports.append(port);machine.c.r[0]=PAD if port==0 else PAD6;machine.c.pc=machine.c.pr
    else:raise AssertionError('Original controller-state boundary exceeded its bound')
    require(get(machine,FLAG)==1,'Original ABXY+Start did not set its menu request')
    input_steps=machine.c.steps
    # Source-checked callerAE64 saved frame: four locals, PR, R8..R14.
    sp=STACK-0x100
    frame=[0]*4+[0x8c01ae6c,SDK_AFTER_LOOP[1],SDK_AFTER_LOOP[0],SIZE,5,0x8c47ee1c,0,0x8c514000]
    machine.put(sp,struct.pack('<12I',*frame));machine.c.r[15]=sp
    machine.c.r[14]=FLAG;machine.c.r[12]=0;machine.c.pc=0x8c010488
    put(machine,0x8c514000,0x8c720000)
    require(machine.block(0x8c01048e,4)==struct.pack('<HH',0xa1f8,0x2ec2),
        'Original combo edge differs from its reviewed branch and delay slot')
    if hijack:machine.put(0x8c01048e,struct.pack('<H',0xa1f4))
    machine.execute(stop=RESET if hijack else SDK_AFTER_LOOP[0],
        break_pc=SDK_AFTER_LOOP[0]if hijack else RESET,budget=1000)
    require(machine.c.pc==(RESET if hijack else SDK_AFTER_LOOP[0])and get(machine,FLAG)==0,
        'Original combo route no longer reaches its exact post-loop boundary')
    frame_preserved=machine.c.r[15]==sp if hijack else machine.c.r[15]==sp+48
    require(frame_preserved,'Original combo changed its exact source-owned frame layout')
    require(machine.c.watch_writes[0]==0,'Original combo wrote the low return gate')
    row=dict(input_function='0x8c019894',held='0x606',start_edge='0x8',controller_ports=ports,
        input_instructions=input_steps,post_loop_instructions=machine.c.steps-input_steps,
        original_branch='0xa1f8',executed_branch='0xa1f4'if hijack else'0xa1f8',
        source_frame_preserved=frame_preserved,
        reached=hex(machine.c.pc),menu_reached_before_reset=not hijack,
        reset_before_options=hijack,low_return_gate_writes=0)
    if hijack:
        require(machine.c.pr==0x8c01087e,'Old hijack no longer calls the original reset directly')
        return machine,row
    authored_return(machine)
    machine.execute(stop=SDK_AFTER_LOOP[1],budget=1000);authored_return(machine)
    machine.execute(stop=OPTIONS,break_pc=RESET,budget=1000)
    require(machine.c.pc==OPTIONS,'Original combo reached reset before its options loop')
    row.update(reached=hex(machine.c.pc),original_emulator_epilogue='0x8c010882',
        original_emulator_caller='0x8c01ae6c',SDK_boundaries=[hex(address)for address in SDK_AFTER_LOOP])
    return machine,row


def options_fixture(run,source,size,dirty):
    machine,original_combo=combo(run,source)
    # Source-owned options/main saved frames. Rendering is not executed.
    # Saved PR3A0 returns to the original caller; its saved PR292 reaches reset.
    sp=machine.c.r[15]
    machine.put(sp,struct.pack('<12I',0,0x8c0103a0,0,0,0,0,0,0,SIZE,0x8c010292,0,0))
    put(machine,SIZE,size);put(machine,DIRTY,dirty);put(machine,0x8c48f510,1)
    start=machine.c.steps;machine.execute(stop=UI,break_pc=RESET,budget=1000)
    require(machine.c.pc==UI and list(machine.c.r[4:6])==[DESCRIPTOR,0]and get(machine,DESCRIPTOR+12)==6,
        'Original options UI did not receive its six-item descriptor')
    disabled=bool(get(machine,MASK)&8)
    require(disabled==(not bool(size and dirty)),'Original Save Game availability gate changed')
    labels=[machine.block(LABELS+64*index,64).split(b'\0')[0].decode()for index in range(6)]
    require(labels==[' Start Game',' Resume Game',' Controls',' Save Game',' Show Credits',' Return to Menu'],
        'Original options items or their indices changed')
    return machine,dict(combo=original_combo,SRAM_bytes=size,dirty=dirty,
        menu_entry=hex(OPTIONS),menu_UI=hex(UI),menu_descriptor=hex(DESCRIPTOR),
        gate_instructions=machine.c.steps-start,save_disabled=disabled,
        menu_before_any_reset=True)


def choose(machine,index):
    require(machine.c.pc==UI,'Selection bypassed the original options UI boundary')
    put(machine,SELECT,index);authored_return(machine,1)


def save_case(run,source,size,dirty):
    machine,row=options_fixture(run,source,size,dirty);choose(machine,3);start=machine.c.steps
    available=bool(size and dirty)
    if available:
        machine.execute(stop=SAVE,budget=1000)
        require(machine.c.pr==0x8c01ae9e,'Save Game did not call its original handler')
        # Authored handler return establishes routing only, not any BUP job.
        authored_return(machine);machine.execute(stop=OPTIONS,budget=1000)
    else:machine.execute(stop=UI,break_pc=OPTIONS,budget=1000)
    require(machine.c.pc==(OPTIONS if available or size else UI),
        'Unavailable Save Game escaped the menu or available Save Game did not return there')
    return dict(**row,selected_index=3,selected_label='Save Game',
        dispatch_instructions=machine.c.steps-start,original_save_handler_reached=available,
        save_handler='0x8c018362',save_return='0x8c01ae9e'if available else None,
        menu_reentered=True,reached=hex(machine.c.pc),reset_reached=False,
        save_handler_return_authored=available,save_backend_executed=False)


def return_case(run,source,dirty,answer=None):
    machine,row=options_fixture(run,source,8192,dirty);choose(machine,5);start=machine.c.steps
    machine.execute(stop=0x8c010a40,budget=1000)
    require(machine.c.pr==0x8c01aeb0,'Return to Menu changed its original render/setup call')
    authored_return(machine)
    for target in SDK_AFTER_LOOP:
        machine.execute(stop=target,budget=1000);authored_return(machine)
    machine.execute(stop=RESET,break_pc=CONFIRM,budget=1000)
    prompt=machine.c.pc==CONFIRM;require(prompt==bool(dirty),'Original dirty-save confirmation gate changed')
    save_seen=False;events=['options','Return_to_Menu']
    if prompt:
        require(machine.c.pr==0x8c018354 and answer in(0,1),
            'Dirty return bypassed the original save-confirm wrapper')
        events.append('original_save_confirmation')
        authored_return(machine,answer)
        machine.execute(stop=RESET,break_pc=SAVE,budget=1000)
        save_seen=machine.c.pc==SAVE
        require(save_seen==bool(answer),'Actual original confirmation wrapper selected the wrong edge')
        if save_seen:
            require(machine.c.pr==0x8c010292,'Confirmed save lost the original reset continuation')
            events.append('original_save_handler')
            authored_return(machine);machine.execute(stop=RESET,budget=1000)
    require(machine.c.pc==RESET,'Explicit Return to Menu failed to reach the original reset gate')
    events.append('original_reset_gate')
    return dict(**row,selected_index=5,selected_label='Return to Menu',
        dispatch_instructions=machine.c.steps-start,render_setup_boundary='0x8c010a40',
        options_caller_return='0x8c0103a0',reset_tail='0x8c010292',reset_entry=hex(RESET),
        dirty_save_confirmation_reached=prompt,confirmation_function=hex(CONFIRM)if prompt else None,
        confirmation_answer=answer,original_save_handler_reached=save_seen,
        save_return_continuation='0x8c010292'if save_seen else None,
        save_handler_return_authored=save_seen,save_backend_executed=False,
        actual_confirmation_wrapper_executed=prompt,original_reset_gate_reached=True,events=events)


def audit(original):
    original=Path(original);source=original.read_bytes()
    require(len(source)==OWNER_BYTES and sha(source)==OWNER_SHA,'Original MDE owner differs')
    sources={Path(__file__).resolve(),ROOT/'tests/sh4_integer_fast.c'}
    for module in tuple(sys.modules.values()):
        name=getattr(module,'__file__',None)
        if name:
            path=Path(name).resolve()
            if path.is_relative_to(ROOT)and path.suffix=='.py':sources.add(path)
    source_before={str(path.relative_to(ROOT)):sha(path.read_bytes())for path in sorted(sources)}
    with tempfile.TemporaryDirectory(prefix='smash-original-options-')as folder:
        lib=Path(folder)/'integer.so'
        subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O2','-shared','-fPIC',
            str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(lib)],check=True,capture_output=True)
        library=ctypes.CDLL(str(lib));run=library.sh4_fast_run
        run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
        saves=[save_case(run,source,size,dirty)for size in(0,8192)for dirty in(0,1)]
        returns=[return_case(run,source,0),return_case(run,source,1,0),return_case(run,source,1,1)]
        _,negative=combo(run,source,True)
    require(original.read_bytes()==source and source_before==
        {str(path.relative_to(ROOT)):sha(path.read_bytes())for path in sources},
        'Original input or execution sources changed during replay')
    windows=[(0x8c010480,18),(0x8c01086c,42),(OPTIONS,60),(0x8c01ae32,116),
        (0x8c01aeaa,44),(0x8c0103a0,32),(0x8c010292,12),(0x8c01834c,22)]
    return dict(schema='kui.smash.genesis-options-source.v1',status='PASS',
        original_mde_sha256=OWNER_SHA,inputs={str(original.resolve()):OWNER_SHA},
        inputs_immutable_during_execution=True,test_sha256=source_before['tests/test_smash_genesis_options_source.py'],
        source_sha256=source_before,menu_descriptor=hex(DESCRIPTOR),
        menu_items=['Start Game','Resume Game','Controls','Save Game','Show Credits','Return to Menu'],
        checks=dict(original_combo_reaches_options_before_reset=True,
            original_six_item_menu_includes_save_and_return=True,save_SRAM_and_dirty_gates_exact=True,
            save_selection_rechecked=True,save_handler_returns_to_options=True,
            explicit_return_reaches_original_reset=True,dirty_return_runs_original_confirmation_wrapper=True,
            confirmed_save_precedes_reset=True,old_hijack_skips_options=True,original_input_unchanged=True),
        save_cases=saves,return_cases=returns,old_hijack_negative=negative,
        source_windows=[dict(address=hex(address),bytes=count,
            sha256=sha(source[address-LOAD:address-LOAD+count]))for address,count in windows],
        limits=['Controller-state lookup, source-owned saved caller frames, UI choice, opaque SDK jobs, A40 rendering/setup, confirmation answer and save-handler return are explicit authored boundaries.',
            'Original combo, loop epilogue, SRAM enable/recheck, item dispatch, options epilogue, confirmation wrapper and reset-tail integer instructions execute unchanged.',
            'The old A1F4 hijack is executed only in private modeled RAM as a negative control; no original file is modified.',
            'No FPU UI renderer, save backend/filesystem/VMU job, save durability, worker retirement, outer loader or physical hardware timing is proved.'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-mde',type=Path,required=True);parser.add_argument('--report',type=Path)
    args=parser.parse_args();result=audit(args.original_mde);text=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(text)
    print(text,end='')
