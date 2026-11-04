#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute both actual compact workers and refuse mixed/corrupt slot evidence.

Usage: test_native_controller_slot_audit.py <A2-build> <A1-build>
These are linked loader artifacts; no game, save or firmware bytes are used.
"""
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_native_controller_failure_witness import (Failure,complete_cases,console_layout,
    pinned,slot_instructions,virtual_slot)
from check_native_dual_readers import controller_slot_binding
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

checks=0


def check(value):
    global checks
    checks+=1
    if not value:raise AssertionError('Slot audit check failed')


def refused(function,*args):
    global checks
    checks+=1
    try:function(*args)
    except (ValueError,AssertionError,Failure,KeyError,struct.error):return
    raise AssertionError('Slot audit accepted mixed or corrupted evidence')


def change_function(image,name,offset,word):
    address,_,section=symbol_bytes(image,name,2)
    data=bytearray(image[0]);struct.pack_into('<H',data,section[4]+address-section[3]+offset,word)
    return (bytes(data),*image[1:])


def run(directory,expected,layout):
    worker=read_elf(directory/'native-worker.elf');stage=read_elf(directory/'stage.elf')
    resident=read_elf(directory/'resident-sci.elf');module=(directory/'retail-maple-observe.kui').read_bytes()
    check(virtual_slot(worker)==expected);pins=pinned(worker)
    check('_kui_vmu_native_passive_complete' in pins and '_kui_vmu_native_passive_submit' in pins)
    words=slot_instructions(worker)
    check(words['virtual_slot']==expected and words['physical_slot']==3-expected)
    check(words['occupied_attachment_bit']==expected<<16 and words['presence_or_bit']==expected)
    report=complete_cases(resident,worker,layout)
    check(report['successes']==72 and report['failures']==116)
    check(report['occupied_virtual_slot_refused'] and report['physical_attachment_preserved'])
    check(report['success_only_parent_presence_byte'] and report['failure_no_owner_writes'])
    binding=controller_slot_binding(stage,module,worker)
    check(binding['virtual_slot']==expected and binding['normal_mode']==0 and binding['off_mode']==7)
    check(binding['exactly_one_mode_descriptor'] and binding['paired_on_off_captions'])
    refused(complete_cases,resident,worker,layout,3-expected)
    for offset,word in ((0x724,0xc800|(3-expected)),(0x838,0xcb00|(3-expected))):
        mutant=change_function(worker,'_kui_vmu_native_passive_complete',offset,word)
        refused(complete_cases,resident,mutant,layout,expected)
        refused(pinned,mutant)
    refused(pinned,change_function(worker,'_kui_vmu_native_passive_submit',0x43a,0xe100|(3-expected)))
    mode_address,mode,section=symbol_bytes(stage,'__native_runtime_mode_start',1)
    refused(controller_slot_binding,stage,module+mode,worker)
    for value in (1,7,0xffffffff):
        changed=bytearray(stage[0]);at=section[4]+mode_address-section[3]
        struct.pack_into('<I',changed,at+16,value)
        altered=bytearray(mode);struct.pack_into('<I',altered,16,value)
        refused(controller_slot_binding,(bytes(changed),*stage[1:]),module.replace(mode,altered),worker)
    for record in ((mode_address,31,1,stage[4]['__native_runtime_mode_start'][3]),
                   (mode_address,32,2,stage[4]['__native_runtime_mode_start'][3])):
        symbols=dict(stage[4]);symbols['__native_runtime_mode_start']=record
        refused(controller_slot_binding,(*stage[:4],symbols),module,worker)
    changed=module.replace(('VIRTUAL A%d ON'%expected).encode(),('VIRTUAL A%d ON'%(3-expected)).encode())
    refused(controller_slot_binding,stage,changed,worker)
    return worker,stage,module


def main():
    if len(sys.argv)!=3:raise SystemExit(__doc__)
    layout=console_layout()
    a2=run(Path(sys.argv[1]),2,layout);a1=run(Path(sys.argv[2]),1,layout)
    refused(controller_slot_binding,a2[1],a2[2],a1[0])
    refused(controller_slot_binding,a1[1],a1[2],a2[0])
    print(f'PASS compact controller slot audit: {checks} checks; actual A1/A2 conflict and presence execution, exact paired pins, mode descriptor and mixed-slot refusals')


if __name__=='__main__':main()
