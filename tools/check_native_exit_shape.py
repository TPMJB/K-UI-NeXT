#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the ten-family descriptor policy table and its linked predicates.

The compound compact audit separately requires the complete heap metadata
closure before this check. This adds the table, inlined schema instructions,
and the worker's specialized descriptor/table bindings; it grants no image
or allocator authority by descriptor shape alone.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_vmu_native_worker_gateway import read_elf
from check_vmu_native_persistent import prepared_symbol_records
from check_native_bottom_heap import records
import check_native_compact_background_profile as compact

SCHEMA='kui.native.exit-shape.compiled.v1'
WORDS=struct.pack('<10H',0x123,0x123,0x123,0x1fa,0x13b,0x24,0x3fa,0x72b,0x1bb,0x3bb)


def require(value,message):
    if not value:raise ValueError(message)


def sha(body):return hashlib.sha256(body).hexdigest()


def exact_component(image,role):
    require(role in ('stage','worker'),'Unknown exit-shape component')
    require((compact.stage_profile if role=='stage' else compact.worker_profile)(image),
        'Exit-shape proof requires its exact compact component')
    available=records(image);result={}
    functions=compact.EXIT_SHAPE_FUNCTIONS[role];objects=compact.EXIT_SHAPE_OBJECTS[role]
    require(len(functions)==len(objects)==1 and len(compact.EXIT_SHAPE_BINDINGS[role])==2,
        'Exit-shape reviewed function/table/binding set differs')
    for pins,kind,flags in ((functions,2,6),(objects,1,2)):
        for name,expected in pins.items():
            require(name in available,'Exit-shape typed symbol missing: '+name)
            address,body,actual,section=available[name]
            require(actual==kind and section[1]==1 and section[2]&7==flags and
                (len(body),sha(body))==expected,'Exit-shape exact instructions/data differ: '+name)
            if kind==1:require(body==WORDS,'Exit-shape table differs from the authored ten rows')
            result[name]=dict(address=address,bytes=len(body),sha256=sha(body))
    targets={name:(address,len(body),kind,section) for name,(address,body,kind,section) in available.items()}
    if role=='worker':
        config=[row for row in prepared_symbol_records(image) if row[0]=='_config@vmu_native_passive.c']
        require(len(config)==1,'Exit-shape worker descriptor owner is not unique')
        name,address,size,kind,owner=config[0];section=image[3][owner]
        require(kind==1 and size==2304 and section[1]==8 and section[2]&7==3,
            'Exit-shape worker descriptor owner type/span differs')
        targets[name]=(address,size,kind,section)
    bindings=[]
    for function,offset,target,addend in compact.EXIT_SHAPE_BINDINGS[role]:
        require(function in functions and target in targets,'Exit-shape binding lacks its reviewed source/target')
        address,body,_,_=available[function];destination,size,kind,section=targets[target]
        if target in objects:require(kind==1 and section[2]&7==2 and not addend,'Exit-shape table binding is not its typed start')
        elif target=='_config@vmu_native_passive.c':require(addend==0x8bc and addend+12+52<=size,'Exit-shape descriptor binding differs')
        else:require(target=='_kui_vmu_native_heap_metadata_valid' and kind==2 and section[2]&7==6 and not addend,
            'Exit-shape schema does not name its separate typed heap metadata gate')
        require(offset+4<=len(body) and not (address+offset)&3 and
            struct.unpack_from('<I',body,offset)[0]==destination+addend,
            'Exit-shape literal binding differs: '+function+' -> '+target)
        bindings.append(dict(function=function,offset=offset,target=target,addend=addend,address=destination+addend))
    return dict(exact_linked_closure=result,typed_bindings=bindings)


def self_tests(image,role):
    available=records(image);code=bits=bindings=sections=0
    def refused(changed,message):
        try:exact_component(changed,role)
        except ValueError:return
        raise ValueError('Exit-shape proof accepted '+message)
    for pins,kind in ((compact.EXIT_SHAPE_FUNCTIONS[role],2),(compact.EXIT_SHAPE_OBJECTS[role],1)):
        for name in pins:
            address,body,_,section=available[name];start=section[4]+address-section[3]
            for byte in range(len(body)):
                for bit in (range(8) if kind==1 else (0,)):
                    data=bytearray(image[0]);data[start+byte]^=1<<bit
                    refused((bytes(data),*image[1:]),name+' changed byte')
                    if kind==1:bits+=1
                    else:code+=1
            mutated=list(image[3]);changed=list(section);changed[2]|=1;mutated[mutated.index(section)]=tuple(changed)
            refused((image[0],image[1],image[2],mutated,image[4]),name+' writable owner');sections+=1
    for function,offset,_,_ in compact.EXIT_SHAPE_BINDINGS[role]:
        address,body,_,section=available[function];start=section[4]+address-section[3]
        data=bytearray(image[0]);struct.pack_into('<I',data,start+offset,struct.unpack_from('<I',body,offset)[0]+2)
        refused((bytes(data),*image[1:]),'interior pointer target');bindings+=1
    return dict(instruction_bytes_rejected=code,table_bits_rejected=bits,
        interior_bindings_rejected=bindings,mutable_owner_refusals=sections)


def check(stage,worker,*,self_test=False):
    result=dict(schema=SCHEMA,components={})
    for role,image in (('stage',stage),('worker',worker)):
        row=exact_component(image,role);row['artifact_sha256']=sha(image[0])
        if self_test:row['self_tests']=self_tests(image,role)
        result['components'][role]=row
    result['separate_complete_heap_metadata_proof_required']=True
    result['shape_alone_authorizes_no_image_or_allocator']=True
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    try:print(json.dumps(check(read_elf(args.directory/'stage.elf'),read_elf(args.directory/'native-worker.elf'),self_test=args.self_test),indent=2))
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as error:
        parser.exit(2,'Exit-shape linked proof refused: '+str(error)+'\n')


if __name__=='__main__':main()
