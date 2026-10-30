#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Close the linked low GD refusal -> guarded worker-terminal route."""
import hashlib
import json
from pathlib import Path
from native_runtime_layout import LEASE_BYTES, layout_for
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
# Separately reviewed Background equivalents. The map guard retains the same
# lease/destination predicate, then calls its typed async map_guest. Failure
# retains the identical terminal guard and uses the owned region's display.
BACKGROUND_PINS={
    '_native_background_map':('4d2cb3fee34f3558e81ea762fce0f6d163e2c37827855d97f4e2f00b1980c1d9',188),
    '_native_resident_terminal':PINS['_native_resident_terminal'],
    '_kui_vmu_observe_low_failure':('4733a395aa815e4a3227afe93d42acaf69d9a076a84a4fe138e68874d6c6f22d',60),
}
# The opt-in 384 KiB lease changes only the admitted payload ceiling and the
# excluded destination end in these two complete instruction bodies. Worker
# terminal code stays in its original 128 KiB reservation; the low guard is
# unchanged. All previous profile pins above remain independently required.
READAHEAD_PINS={
    '_native_background_map':('4f2ee5b6686ffee94253d4a92a02b2d4769369c906598ea0949d6410567352f2',188),
    '_native_resident_terminal':('3c480406466530bec6748de2f40a8857772a3d159046db48d77c2b1ab3b7da7d',96),
    '_kui_vmu_observe_low_failure':BACKGROUND_PINS['_kui_vmu_observe_low_failure'],
}

def wide_guard_pins(image):
    from check_native_general_lean_profile import low_pins
    return low_pins(image,'map_refusal',READAHEAD_PINS)

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
    background='_kui_vmu_native_background_manifest_bind' in image[4]
    read_ahead=image[4].get('__kui_native_readahead_resident')
    if read_ahead is not None:
        require(background and read_ahead==(1,0,0,0xfff1) and
                image[4].get('__kui_native_sdk_sci_route_resident')==(1,0,0,0xfff1),
                'read-ahead refusal requires the exact native SDK background profile')
    read_ahead=read_ahead is not None
    if background:
        require(image[4]['_kui_vmu_native_background_manifest_bind'][2]==2 and
                image[4].get('__kui_vmu_native_background_resident',(0,))[0]==1,
                'Background refusal requires its typed binder and exact profile marker')
    low_limit=0x8c007ba0 if background else 0x8c007800
    layout=inspect_elf(image[0],LOW,low_limit)
    bss_object(image[0],layout,'_kui_vmu_observe_boot_control',56,64,low_limit=low_limit)
    terminal=signature(image,disassembly,'_native_resident_terminal')[:2]
    wide=read_ahead or '__kui_native_general_lean_resident_layout' in image[4]
    wide_pins=wide_guard_pins(image)
    require(terminal==wide_pins['_native_resident_terminal'] if wide else
            terminal in (PINS['_native_resident_terminal'],UNCACHED_TERMINAL),
        'GD refusal terminal code alias/guard changed')
    cached=wide or terminal==PINS['_native_resident_terminal']
    selected=dict(wide_pins if wide else BACKGROUND_PINS if background else PINS)
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
        'no_new_callback_or_control_abi':not background,'reader_profile':'background' if background else 'standard',
        'read_ahead_lease_variant':wide,'guarded_lease_bytes':layout_for(image[4]).LEASE_BYTES if wide else 0x30000,
        'diagnostic_detail':'00000100',
        'report_provenance':'differential only; existing late-heap/exit terminal uses same report'}

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf',type=Path)
    parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    print(json.dumps(check(args.elf,self_test=args.self_test),indent=2))
