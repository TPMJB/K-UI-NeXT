#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Close the linked low GD refusal -> guarded worker-terminal route."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from check_loader_layout import inspect_elf
from check_vmu_native_heap_probe import bss_object
from check_vmu_native_worker_gateway import literal_aware_instructions,read_elf,symbol_bytes

ROOT=Path(__file__).resolve().parents[1]
LOW=0x8c004000
PINS={
    '_observe_map':('c4447da9ae1a88a17cfc59cfcfc045769818ee205bb3b440417cbbceb7ccc0e4',184),
    '_native_resident_terminal':('13f02f0a82bfffe09bcda0f1da99b957cbd5a4fc67be1da2cc7c6530d29ac5db',96),
    '_kui_vmu_observe_low_failure':('94257ebf984b9228fceb2d8095d87c71d33541227385dc5f6734f3d69fdd1d3b',36),
}
UNCACHED_TERMINAL=('bcde286f5c278522335115fa1fbb58bb635256fee2b08366de40735442d1519c',96)

def require(condition,message):
    if not condition:raise ValueError(message)

def signature(image,disassembly,name):
    address,raw,section=symbol_bytes(image,name,2)
    require(section[2]&7==6 and not address&1,"GD refusal function is not typed executable code")
    symbols=image[4]
    # SH --relax leaves ST_SIZE at its pre-relaxation extent. The next exact
    # typed function in the same executable section bounds the actual body;
    # no neighbouring function bytes become part of this proof.
    following=[v[0] for key,v in symbols.items() if key!=name and v[2]==2 and
               v[3]==symbols[name][3] and address<v[0]<address+len(raw)]
    if following:raw=raw[:min(following)-address]
    body=bytearray(raw);references=[];seen=set()
    for pc,op,mnemonic,_ in literal_aware_instructions(disassembly):
        if not address<=pc<address+len(body):continue
        op=struct.unpack_from('<H',raw,pc-address)[0]
        if op>>12==0xd:
            at=((pc+4)&~3)+4*(op&255)
            require(address<=at<=address+len(body)-4,"GD refusal literal escapes typed function")
            value=struct.unpack_from('<I',body,at-address)[0]
            canonical=(value&0x1fffffff)|0x80000000
            candidates=[(key,v)for key,v in symbols.items() if v[2] in (1,2) and v[1] and
                        v[0]<=canonical<v[0]+v[1]]
            if not candidates:continue
            target,record=min(candidates,key=lambda row:(row[1][0]!=canonical,row[1][1],row[0]))
            offset=at-address
            if offset not in seen:
                references.append((offset,target,canonical-record[0],value-canonical,4));seen.add(offset)
                body[offset:offset+4]=bytes(4)
        elif op>>12 in (0xa,0xb):
            delta=op&4095;delta-=4096 if delta&2048 else 0
            target=pc+4+2*delta
            if address<=target<address+len(body):continue
            candidates=[key for key,v in symbols.items() if v[2]==2 and v[0]==target]
            require(len(candidates)==1,"GD refusal branch does not enter one exact typed function")
            offset=pc-address;references.append((offset,candidates[0],0,0,2))
            body[offset:offset+2]=struct.pack('<H',op&0xf000)
    digest=hashlib.sha256(json.dumps({'body':body.hex(),'references':references},
        sort_keys=True,separators=(',',':')).encode()).hexdigest()
    return digest,len(body),references

def check(path,*,self_test=False,objdump=None):
    path=Path(path);image=read_elf(path)
    objdump=objdump or shutil.which('sh-elf-objdump') or str(ROOT/'.deps/sh-elf/bin/sh-elf-objdump')
    disassembly=subprocess.check_output([objdump,'-d',str(path)],text=True)
    layout=inspect_elf(image[0],LOW,0x8c007800)
    bss_object(image[0],layout,'_kui_vmu_observe_boot_control',56,64)
    terminal=signature(image,disassembly,'_native_resident_terminal')[:2]
    require(terminal in (PINS['_native_resident_terminal'],UNCACHED_TERMINAL),
        'GD refusal terminal code alias/guard changed')
    cached=terminal==PINS['_native_resident_terminal']
    selected=dict(PINS)
    if not cached:selected['_native_resident_terminal']=UNCACHED_TERMINAL
    functions={}
    for name,expected in selected.items():
        value=signature(image,disassembly,name)
        require(value[:2]==expected,'GD refusal linked instructions/targets changed: '+name)
        functions[name]={'normalized_sha256':value[0],'bytes':value[1],'typed_bindings':value[2]}
    require(PINS,'missing reviewed GD refusal route pins')
    rejected=0
    if self_test:
        for name in selected:
            address,body,section=symbol_bytes(image,name,2)
            for offset in range(0,selected[name][1],2):
                for mask in (1,0x1000):
                    changed=bytearray(image[0]);at=section[4]+address-section[3]+offset
                    struct.pack_into('<H',changed,at,struct.unpack_from('<H',changed,at)[0]^mask)
                    mutant=(bytes(changed),)+image[1:]
                    try:different=signature(mutant,disassembly,name)[:2]!=selected[name]
                    except (ValueError,struct.error):different=True
                    require(different,'GD refusal route accepted changed instruction/literal');rejected+=1
    return {'schema':'kui.native.gd-map-refusal.v1','functions':functions,
        'cached_worker_terminal':cached,
        'resident_memory_end':hex(layout['memory_end']),'mutation_rejections':rejected,
        'no_new_callback_or_control_abi':True,'diagnostic_detail':'00000100',
        'report_provenance':'differential only; existing late-heap/exit terminal uses same report'}

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf',type=Path)
    parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    print(json.dumps(check(args.elf,self_test=args.self_test),indent=2))
