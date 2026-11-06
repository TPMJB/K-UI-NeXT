#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Two exact timeline bodies from independently rebuilt pristine 3a6.

Only five authored DIR32 literals move; every other instruction/constant byte
remains exact. The low bridge keeps its existing actual execution model. The
cold binder remains a complete instruction pin, not an execution claim.
"""
import hashlib
import struct
from check_vmu_native_worker_gateway import symbol_bytes
from check_native_session_preclear_profile import symbol_records,role_value
REFERENCE_COMMIT='3a6ae064bb3f456da8d71587a8db0ce974f139cf'
BRIDGE_PREFIX=bytes.fromhex('06d1126709917c311c511821038904d222662b4109000b000900fc04')
BINDER_SHA256='7861b4fc572525d1488a3dc55ad0809d50ac3e23650b6cc007e9c26a92afbee8'
ROLES={
 'bridge':((28,('symbol','_kui_retail_async_region_storage',0,1,8,3,4,0)),
           (32,('symbol','_kui_retail_hook_sr',0,1,8,3,4,0))),
 'binder':((152,('symbol','_native_report@vmu_native_heap_probe_stage.c',0,2,1,6,260,0)),
           (160,('symbol','_passive_entry@vmu_native_heap_probe_stage.c',0,2,1,6,40,0)),
           (168,('symbol','_kui_vmu_native_background_map_address@vmu_native_heap_probe_stage.c',0,2,1,6,92,0)))}


def require(value,message):
    if not value:raise ValueError(message)


def profile(resident,worker,stage):
    from check_native_session_witness_profile import profile as session_worker
    from check_native_ordinary_general_profile import profile as ordinary_worker
    session=session_worker(worker);ordinary=ordinary_worker(worker)
    if session is None and ordinary is None:return None
    from check_native_general_lean_profile import low_profile
    require(low_profile(resident),'Modern timeline requires its complete General low profile')
    require(('_kui_retail_native_session_control' in resident[4])==(session is not None),
            'Modern timeline low/worker session capabilities differ')
    if session is not None:
        from check_native_session_preclear_profile import stage_profile
    else:
        from check_native_ordinary_preclear_profile import stage_profile
    require(stage_profile(stage),'Modern timeline stage/worker variant differs')
    slot=session if session is not None else ordinary
    marker=stage[4].get('__kui_native_virtual_slot_stage')
    require(marker is None or marker==(slot,0,0,0xfff1),'Modern timeline stage/worker virtual slots differ')
    return 'session' if session is not None else 'ordinary'


def pinned(resident,stage):
    result={}
    for part,image,name,qualified,size,first,last in (
        ('bridge',resident,'_kui_retail_async_trace','_kui_retail_async_trace',36,0x8c004000,0x8c007ba0),
        ('binder',stage,'_native_loader_timeline_bind','_native_loader_timeline_bind@vmu_native_heap_probe_stage.c',180,0x8ce00000,0x8cfe0000)):
        records=symbol_records(image)
        require(records.get(qualified)==image[4].get(name),'Modern timeline root differs from its actual FILE-owned symbol')
        address,body,section=symbol_bytes(image,name,2)
        require(section[1]==1 and section[2]&7==6 and len(body)==size and first<=address<address+size<=last,
                'Modern timeline complete code owner/size changed: '+part)
        normalized=bytearray(body)
        for offset,role in ROLES[part]:
            role_value(image,records,role,struct.unpack_from('<I',body,offset)[0])
            normalized[offset:offset+4]=bytes(4)
        expected=hashlib.sha256(BRIDGE_PREFIX+bytes(8)).hexdigest() if part=='bridge' else BINDER_SHA256
        require(hashlib.sha256(normalized).hexdigest()==expected,'Modern timeline instructions/constants changed: '+part)
        result[part]=dict(bytes=size,normalized_sha256=expected,typed_literal_words=len(ROLES[part]),reference_source=REFERENCE_COMMIT)
    return result


def mutation_checks(resident,stage):
    pinned(resident,stage);rejected=0
    for part,image,name in (('bridge',resident,'_kui_retail_async_trace'),('binder',stage,'_native_loader_timeline_bind')):
        address,body,section=symbol_bytes(image,name,2);start=section[4]+address-section[3]
        # Five wrong exact pointer roles and one actual opcode per body.
        for offset in (0,*(row[0] for row in ROLES[part])):
            changed=bytearray(image[0]);changed[start+offset]^=1
            mutant=(bytes(changed),*image[1:])
            try:pinned(mutant if part=='bridge' else resident,mutant if part=='binder' else stage)
            except (ValueError,KeyError):rejected+=1
            else:raise ValueError('Modern timeline accepted changed instruction/typed target')
        wanted={role[1] for _,role in ROLES[part]};found={}
        for table in image[3]:
            if table[1]!=2:continue
            strings=image[3][table[6]];names=image[0][strings[4]:strings[4]+strings[5]];source=''
            for at in range(table[4],table[4]+table[5],16):
                label,value,count,info,_,_=struct.unpack_from('<IIIBBH',image[0],at)
                label=names[label:].split(b'\0',1)[0].decode('ascii')
                if info&15==4:source=label
                key=label+('@'+source if not info>>4 else '')
                if key in wanted:found[key]=(at,value,count,info)
        require(set(found)==wanted,'Modern timeline role mutation coverage is incomplete')
        for at,value,count,info in found.values():
            for variant in ('type','address','size'):
                changed=bytearray(image[0])
                if variant=='type':changed[at+12]=(info&0xf0)|(2 if info&15==1 else 1)
                elif variant=='address':struct.pack_into('<I',changed,at+4,value+4)
                else:struct.pack_into('<I',changed,at+8,count+4)
                mutant=(bytes(changed),*image[1:])
                try:pinned(mutant if part=='bridge' else resident,mutant if part=='binder' else stage)
                except (ValueError,KeyError):rejected+=1
                else:raise ValueError('Modern timeline accepted changed raw symbol role metadata')
    return rejected
