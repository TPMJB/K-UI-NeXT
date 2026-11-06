#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the reviewed bounded pointer-pool DAG to linked cold SH instructions.

Only authored relocation slots are normalized. Every slot must still point to
its exact typed callee/object start; every other byte remains fixed. This is a
cold guard closure proof, not a proof of arbitrary indirect game control flow.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_vmu_native_worker_gateway import read_elf
from check_native_bottom_heap import records

SCHEMA='kui.native.patch-pool.compiled.v1'
# Authored offline from the exact production SH flags, then independently
# reviewed against source, disassembly and the complete typed DIR32 records.
FUNCTIONS={
 '_ram@vmu_native_patch.c': (80,'1327b2f301be78737ee539a4af7e5def8f100d6f05d998534324da10e1755f03',{}),
 '_pointer@vmu_native_patch.c': (30,'6a27271642802c2053d895ffeb2e2a93c5a3bf48ef2c4b7f42a9ced75da48334',{}),
 '_pool_node@vmu_native_patch.c': (632,'d610eb505b55e9b543646a2b271c8f718b38468890601166933ffdce57ce7cb6',{
  0xd0:'_pointer@vmu_native_patch.c',0xd4:'_ram@vmu_native_patch.c',
  0x264:'_pointer@vmu_native_patch.c',0x268:'_ram@vmu_native_patch.c',
  0x270:'_code.0@vmu_native_patch.c'}),
 '_kui_vmu_native_patch_no_interiors': (1324,'f28728b85c74305b5f2493737e9595cd6dc52ac0374ba5eb57f95e541c6b8dc3',{
  0x190:'_ram@vmu_native_patch.c',0x30c:'_pool_node@vmu_native_patch.c',
  0x31c:'_ram@vmu_native_patch.c',0x504:'_pointer@vmu_native_patch.c',
  0x508:'_ram@vmu_native_patch.c',0x520:'_pool_node@vmu_native_patch.c'}),
 '_kui_vmu_native_patch_no_interior': (36,'8af71471d5cd8f7bb857bf1fd09387436c5083126e97ea3eda8e01d2e5f1180a',{
  0x20:'_kui_vmu_native_patch_no_interiors'})}
OBJECT='_code.0@vmu_native_patch.c'
DISPATCHER=struct.pack('<22H',0x6063,0x8801,0x8905,0x8802,0x8906,0x8803,
 0x8907,0xa009,9,0xd306,0x432b,9,0xd305,0x432b,9,0xd305,0x432b,9,0xd304,0x432b,9,0)


def require(value,message):
    if not value:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def exact_closure(stage):
    available=records(stage);result={}
    require(OBJECT in available,'Pointer-pool dispatcher object is missing')
    address,body,kind,section=available[OBJECT]
    require(kind==1 and section[1]==1 and section[2]&7==2 and not address&1 and body==DISPATCHER,
        'Pointer-pool dispatcher is not the exact typed immutable44-byte object')
    result[OBJECT]=dict(address=address,bytes=len(body),sha256=sha(body))
    for name,(size,digest,slots) in FUNCTIONS.items():
        require(name in available,'Pointer-pool cold function is missing: '+name)
        address,body,kind,section=available[name]
        require(kind==2 and section[1]==1 and section[2]&7==6 and not address&1 and len(body)==size,
            'Pointer-pool cold function type/span differs: '+name)
        normalized=bytearray(body);roles=[]
        for offset,target in slots.items():
            require(target in available,'Pointer-pool typed target is missing: '+target)
            target_address,target_body,target_kind,target_section=available[target]
            expected_kind=1 if target==OBJECT else 2
            require(target_kind==expected_kind and target_section[1]==1 and
                target_section[2]&7==(2 if expected_kind==1 else 6),
                'Pointer-pool target has the wrong immutable type: '+target)
            require(offset+4<=size and not (address+offset)&3 and
                struct.unpack_from('<I',body,offset)[0]==target_address,
                'Pointer-pool pointer does not bind the exact typed start: '+name+' -> '+target)
            normalized[offset:offset+4]=bytes(4)
            roles.append(dict(offset=offset,target=target,address=target_address))
        require(sha(normalized)==digest,'Pointer-pool exact instructions/constants differ: '+name)
        result[name]=dict(address=address,bytes=len(body),sha256=sha(body),
            normalized_sha256=digest,typed_pointer_roles=roles)
    return result


def changed_byte(stage,offset,value):
    data=bytearray(stage[0]);data[offset]=value
    return (bytes(data),*stage[1:])


def self_tests(stage):
    available=records(stage);code=objects=roles=types=0
    def refused(changed,message):
        try:exact_closure(changed)
        except ValueError:return
        raise ValueError('Pointer-pool proof accepted '+message)
    for name in (*FUNCTIONS,OBJECT):
        address,body,kind,section=available[name];start=section[4]+address-section[3]
        for byte in range(len(body)):
            for bit in (range(8) if kind==1 else (0,)):
                refused(changed_byte(stage,start+byte,body[byte]^(1<<bit)),name+' byte mutation')
                if kind==1:objects+=1
                else:code+=1
    for name,(_,_,slots) in FUNCTIONS.items():
        address,body,_,section=available[name];start=section[4]+address-section[3]
        for offset,target in slots.items():
            data=bytearray(stage[0]);struct.pack_into('<I',data,start+offset,available[target][0]+2)
            refused((bytes(data),*stage[1:]),'interior typed target: '+name);roles+=1
    # Check symbol metadata independently from byte mutations. A mutable section
    # or function-typed dispatcher cannot silently satisfy a data pin.
    for table in stage[3]:
        if table[1]!=2:continue
        strings=stage[3][table[6]];names=stage[0][strings[4]:strings[4]+strings[5]];source=''
        for offset in range(table[4],table[4]+table[5],16):
            label_at,_,_,info,_,_=struct.unpack_from('<IIIBBH',stage[0],offset)
            label=names[label_at:].split(b'\0',1)[0].decode('ascii')
            if info&15==4:source=label
            qualified=label+('@'+source if not info>>4 else '')
            if qualified in (*FUNCTIONS,OBJECT):
                value=(info&0xf0)|(2 if qualified==OBJECT else 1)
                refused(changed_byte(stage,offset+12,value),'changed symbol type: '+qualified);types+=1
    for name in (*FUNCTIONS,OBJECT):
        section=available[name][3];sections=list(stage[3]);index=sections.index(section)
        mutable=list(section);mutable[2]|=1;sections[index]=tuple(mutable)
        refused((stage[0],stage[1],stage[2],sections,stage[4]),'mutable owner section: '+name);types+=1
    require(types==12,'Pointer-pool type mutation coverage differs')
    return dict(instruction_bytes_rejected=code,dispatcher_bits_rejected=objects,
        interior_pointer_roles_rejected=roles,typed_symbol_section_refusals=types)


def check(stage,*,self_test=False):
    result=dict(schema=SCHEMA,artifact_sha256=sha(stage[0]),exact_linked_closure=exact_closure(stage),
        limits=dict(nodes=8,dependency_path_nodes=4,pool_bytes=128),
        worker_geometry_checked=False,arbitrary_indirect_control_flow_proven=False)
    if self_test:result['self_tests']=self_tests(stage)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',type=Path)
    parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    try:print(json.dumps(check(read_elf(args.stage),self_test=args.self_test),indent=2))
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as error:
        parser.exit(2,'Pointer-pool linked proof refused: '+str(error)+'\n')


if __name__=='__main__':main()
