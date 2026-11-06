#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Closed read-ahead installer map from the pristine 3a6 session reference.

Only the authored R_SH_DIR32 words below are relocatable. Every instruction,
branch, constant, typed target/interior and complete progress string remains
checked. The baseline --emit-relocs relink has identical PT_LOAD bytes. This
map is never generated from the candidate being admitted.
"""
import hashlib
import struct

from check_vmu_native_worker_gateway import symbol_bytes
from check_native_session_preclear_profile import (stage_profile, symbol_records,
                                                  role_value)

REFERENCE_COMMIT='3a6ae064bb3f456da8d71587a8db0ce974f139cf'
REFERENCE_ELF_SHA256='b5b94ce07d24d75af7874dbc716d00a88a926c98a4a3928fcb63424d39209e58'
REFERENCE_RELOCS_SHA256='8fa3a0239ac5312e0f8944eddabf9dbf3ec57aa7bbcc8480253f301e7bd19f55'
ONE=(1,0,0,0xfff1)
FUNCTIONS={
 '_native_readahead_bind':(1244,'85b898e80bffd8d9b14babff3e19e6613ffd8538d2f3712c80e7394424e9ada9',[
  (564,('symbol','_retail_display_line',0,2,1,6,400,0)),
  (568,('string','COLD 42 READ AHEAD BIND BEGIN\0')),
  (572,('symbol','_resident_bytes@vmu_native_heap_probe_stage.c',0,1,8,3,4,0)),
  (576,('symbol','_resident_blob@vmu_native_heap_probe_stage.c',0,1,8,3,4,0)),
  (580,('symbol','_memcpy',0,2,1,6,72,0)),
  (592,('symbol','_native_report@vmu_native_heap_probe_stage.c',0,2,1,6,260,0)),
  (604,('symbol','_resident_limit@vmu_native_heap_probe_stage.c',0,1,8,3,4,0)),
  (616,('symbol','_memcmp',0,2,1,6,34,0)),
  (632,('symbol','_kui_vmu_native_background_map_address@vmu_native_heap_probe_stage.c',0,2,1,6,92,0)),
  (644,('symbol','_control@vmu_native_heap_probe_stage.c',0,2,1,6,28,0)),
  (656,('label','_native_ra_aux_blob_start')),
  (660,('label','_native_ra_aux_blob_end')),
  (664,('label','_native_ra_aux_reloc_start')),
  (668,('label','_native_ra_aux_reloc_end')),
  (908,('symbol','_kui_vmu_native_relocate',0,2,1,6,880,0)),
  (912,('label','_native_ra_aux_blob_end')),
  (916,('label','_native_ra_aux_blob_start')),
  (920,('symbol','_memset',0,2,1,6,26,0)),
  (1196,('assembly_label','_kui_native_passive_publish')),
  (1200,('symbol','_retail_display_line',0,2,1,6,400,0)),
  (1208,('string','COLD 43 AUXILIARY INITIALIZE BEGIN\0')),
  (1216,('symbol','_kui_vmu_native_worker_call',0,2,1,6,160,0)),
  (1220,('string','COLD 44 AUXILIARY INITIALIZED\0')),
  (1236,('string','BOUNDED READ AHEAD READY / 192 KIB CACHE\0')),
  (1240,('string','COLD 45 READ AHEAD BOUND\0'))]),
 '_kui_vmu_native_background_map_address':(92,'431865c1e385488b2c6be9c59219f826be4803aa55d5a8b3e06748a6c9fb3536',[]),
 '_control':(28,'94085d18a5047313fe91f26eb35ad97e2e07fc96c320e58203716d10f426e162',[
  (20,('symbol','_native_low_bindings@vmu_native_heap_probe_stage.c',0,2,1,6,64,0))]),
 '_native_low_bindings':(64,'24f3327ef9c6334692b25d551b56a0b6385f70c829898a3d926839ce91127c7b',[
  (48,('symbol','_manifest@vmu_native_heap_probe_stage.c',0,1,8,3,2172,0)),
  (52,('symbol','_native_low_bindings_table@vmu_native_heap_probe_stage.c',0,1,1,2,48,0)),
  (56,('symbol','_stopped@vmu_native_heap_probe_stage.c',0,2,1,6,68,0)),
  (60,('symbol','_native_low_bindings_refused@vmu_native_heap_probe_stage.c',0,1,1,2,33,0))]),
 '_stopped':(68,'817f99db27c53b784a24bf7b4309d995ff628fd5b198d356e890d8bd3eae9322',[
  (44,('symbol','_retail_display_line',0,2,1,6,400,0)),
  (48,('symbol','_retail_display_hex',0,2,1,6,100,0)),
  (52,('string','DETAIL\0')),
  (56,('string','LAUNCH STOPPED - PHOTOGRAPH THIS SCREEN\0')),
  (60,('string','POWER OFF AND ON TO RETURN\0')),
  (64,('string','STORAGE WAS READ ONLY\0'))])}
OBJECTS={
 '_native_low_bindings_refused':(33,'455a71b81479a8d876eaeb352364b6075b9e9a2adf3efed43594e24a2c4dee85')}


def require(ok,message):
    if not ok:raise ValueError(message)


def profile(stage):
    if not stage_profile(stage):return False
    require(all(stage[4].get(name)==ONE for name in (
        '__kui_native_readahead_stage','__kui_native_sdk_sci_multi_owner_stage',
        '__kui_native_sdk_sci_structural_stage','__kui_native_dual_readers_stage',
        '__kui_native_cold_progress_stage','__kui_native_dual_low_callbacks_stage')),
        'Session read-ahead requires its complete paired cold/callback architecture')
    return True


def pinned_functions(stage):
    """Pin-only tail contract; full admission also requires callback_table."""
    require(profile(stage),'Session read-ahead pins require the exact compiled profile')
    return _bodies(stage)


def callback_table(stage,resident,legacy):
    from check_native_dual_callbacks import expected_entries,linked_table
    from check_vmu_native_readahead import session_low_profile
    require(session_low_profile(resident) and not session_low_profile(legacy),
            'Session callback table requires its actual New-session/ordinary-Legacy readers')
    for image in (resident,legacy):
        require(image[4].get('__kui_native_general_lean_resident_layout')==ONE and
                image[4].get('__kui_native_owner_stack_ip_hook')==ONE,
                'Session callback table requires both complete General low profiles')
    address=linked_table(stage,(expected_entries(resident),expected_entries(legacy)))
    return dict(address=address,bytes=48,exact_paired_low_roles=12,
        resident_sha256=hashlib.sha256(resident[0]).hexdigest(),
        legacy_sha256=hashlib.sha256(legacy[0]).hexdigest())


def pinned(stage,resident,legacy):
    result=pinned_functions(stage);callback_table(stage,resident,legacy)
    return result


def _bodies(stage):
    typed=symbol_records(stage);result={}
    for name,(count,expected,roles) in FUNCTIONS.items():
        address,raw,section=symbol_bytes(stage,name,2)
        require(len(raw)==count and section[1]==1 and section[2]&7==6 and
                0x8ce00000<=address<address+count<=0x8cfe0000,
                'Session read-ahead function owner/type/size changed: '+name)
        normalized=bytearray(raw);seen=set()
        for offset,role in roles:
            require(not(address+offset)&3 and 0<=offset<=count-4 and offset not in seen,
                    'Authored session read-ahead literal escapes/overlaps its body')
            seen.add(offset);value=struct.unpack_from('<I',raw,offset)[0]
            if role[0]=='label':
                target,size,kind,owner=stage[4][role[1]]
                require(owner<len(stage[3]),'Session auxiliary label lost its allocated section')
                storage=stage[3][owner]
                require(size==kind==0 and storage[1]==1 and storage[2]&7==2 and
                        storage[3]<=target<=storage[3]+storage[5] and value==target,
                        'Session auxiliary input literal lost its exact immutable label')
            else:role_value(stage,typed,role,value)
            normalized[offset:offset+4]=bytes(4)
        require(hashlib.sha256(normalized).hexdigest()==expected,
                'Session read-ahead instructions/constants changed: '+name)
        result[name]=(address,count,hashlib.sha256(raw).hexdigest())
    address,table,section=symbol_bytes(stage,'_native_low_bindings_table',1)
    require(len(table)==48 and not address&3 and section[1]==1 and section[2]&7==2,
            'Session callback table lost its exact immutable twelve-word shape')
    for name,(count,expected) in OBJECTS.items():
        address,raw,section=symbol_bytes(stage,name,1)
        require(len(raw)==count and section[1]==1 and section[2]&7==2 and
                hashlib.sha256(raw).hexdigest()==expected,
                'Session read-ahead complete low table/message changed: '+name)
    return result


def reference(stage,relocations):
    """Check the independently preserved baseline and every normalized word."""
    require(hashlib.sha256(stage[0]).hexdigest()==REFERENCE_ELF_SHA256 and
            hashlib.sha256(relocations[0]).hexdigest()==REFERENCE_RELOCS_SHA256,
            'Session reference differs from the independently built pristine3a6 ELFs')
    def loads(image):
        return [(p[2:8],image[0][p[1]:p[1]+p[4]]) for p in image[2] if p[0]==1]
    require(loads(stage)==loads(relocations),'Reference --emit-relocs changed PT_LOAD bytes/geometry')
    _bodies(stage);_bodies(relocations);actual=set()
    for section in relocations[3]:
        if section[1]!=4:continue
        for pos in range(section[4],section[4]+section[5],section[9]):
            address,info,_=struct.unpack_from('<IIi',relocations[0],pos)
            for name,(count,_,roles) in FUNCTIONS.items():
                begin=relocations[4][name][0]
                if begin<=address<begin+count:
                    require(info&255==1,'Unexpected relocation type in session read-ahead closure')
                    actual.add((name,address-begin))
    expected={(name,offset) for name,(_,_,roles) in FUNCTIONS.items() for offset,_ in roles}
    require(actual==expected,'Authored session literals differ from the complete baseline relocation set')
    return dict(status='PASS',reference_commit=REFERENCE_COMMIT,
        reference_elf_sha256=REFERENCE_ELF_SHA256,reference_relocations_sha256=REFERENCE_RELOCS_SHA256,
        identical_loads=True,functions=len(FUNCTIONS),objects=2,exact_named_literals=len(actual))


def object_mutations(stage,resident,legacy):
    rejected=0;pinned(stage,resident,legacy)
    for name in (*OBJECTS,'_native_low_bindings_table'):
        address,raw,section=symbol_bytes(stage,name,1)
        start=section[4]+address-section[3]
        for offset in range(len(raw)):
            changed=bytearray(stage[0]);changed[start+offset]^=1
            try:pinned((bytes(changed),*stage[1:]),resident,legacy)
            except ValueError:rejected+=1
            else:raise ValueError('Session read-ahead pins accepted a changed immutable object')
    return rejected
