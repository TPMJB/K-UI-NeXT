#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Closed ordinary General read-ahead installer map from clean3a6.

Both independently rebuilt ordinary A1/A2 references have identical complete
instruction bodies after these36 authored R_SH_DIR32 roles are checked. The
relocation-enabled references preserve every PT_LOAD byte. Low table entries
are bound to the actual paired reader ELFs, not a candidate-derived hash.
Historical compact and session maps remain separate and unchanged.
"""
import hashlib
import struct

from check_vmu_native_worker_gateway import symbol_bytes
from check_native_general_lean_profile import stage_profile as general_stage_profile
from check_native_session_preclear_profile import symbol_records,role_value
from native_runtime_layout import GENERAL,layout_for

REFERENCE_COMMIT='3a6ae064bb3f456da8d71587a8db0ce974f139cf'
REFERENCES={
    1:('63b5dbe9153b7b430675bc9ae5a3ce5c0b9310a00ea50ac26fcf15c8d1b18682',
       'a7201954b68b42b480cdc227a51adfb9f2ebb1839e1e734e2274bb4caf315eec'),
    2:('9a121c88dc60cf6bd0635bdfef6348f3b29a5b30f0cd350d9d17612a22d8cbea',
       '2fefa6f8ccd2abec4fcf3ca57c3c84605a52c0b2bb1bd7cfa9028db594bf6b56'),
}
ONE=(1,0,0,0xfff1)
PAIRED_CALLBACK_TABLE_COMPANION_REQUIRED=True
FUNCTIONS={
 '_native_readahead_bind':(1244,'5e7470691d7d2d6d6d9af674e67727ac09dcd0235bed5149f501cf49492eb314',[
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
  (64,('string','STORAGE WAS READ ONLY\0'))]),
}
MESSAGE=b'DUAL LOW CALLBACK READER REFUSED\0'


def require(ok,message):
    if not ok:raise ValueError(message)


def profile(stage):
    if layout_for(stage[4]) is not GENERAL:return False
    if '__native_session_descriptor' in stage[4] or '_native_session_cold_bind' in stage[4]:return False
    # Older General cohorts retain their historical maps. Only the complete
    # reviewed private-pool/dispatch/selected-callback capability set selects
    # this new ordinary closure; malformed present capabilities still refuse.
    selectors=tuple(stage[4].get(name) for name in (
        '__kui_native_private_pool_budget_stage','__kui_native_dispatch_read_gate_stage',
        '__kui_native_dual_low_callbacks_stage'))
    require(all(value is None or value==ONE for value in selectors),
            'Ordinary read-ahead selector capability is malformed')
    if any(value is None for value in selectors):return False
    if not general_stage_profile(stage):return False
    require(layout_for(stage[4]) is GENERAL and
            stage[4].get('__kui_native_isolation_lease_bytes')==(0x51000,0,0,0xfff1),
            'Ordinary read-ahead requires its complete112KiB/324KiB General geometry')
    require(all(stage[4].get(name)==ONE for name in (
        '__kui_native_readahead_stage','__kui_native_sdk_sci_multi_owner_stage',
        '__kui_native_sdk_sci_structural_stage','__kui_native_sdk_sci_route_stage',
        '__kui_native_dual_readers_stage','__kui_native_cold_progress_stage',
        '__kui_native_dual_low_callbacks_stage','__kui_native_preheap_clear_stage',
        '__kui_native_preheap_clear_tail','__kui_native_private_pool_budget_stage',
        '__kui_native_dispatch_read_gate_stage')),
        'Ordinary read-ahead lacks its complete cold/callback/preclear architecture')
    slot=stage[4].get('__kui_native_virtual_slot_stage')
    require(slot is None or slot in ((1,0,0,0xfff1),(2,0,0,0xfff1)),
            'Ordinary read-ahead virtual slot marker differs')
    return True


def _bodies(stage):
    typed=symbol_records(stage);result={}
    for name,(count,expected,roles) in FUNCTIONS.items():
        address,raw,section=symbol_bytes(stage,name,2)
        require(len(raw)==count and section[1]==1 and section[2]&7==6 and
                0x8ce00000<=address<address+count<=0x8cfe0000,
                'Ordinary read-ahead function owner/type/size changed: '+name)
        normalized=bytearray(raw);seen=set()
        for offset,role in roles:
            require(not(address+offset)&3 and 0<=offset<=count-4 and offset not in seen,
                    'Authored ordinary read-ahead literal escapes/overlaps its body')
            seen.add(offset);value=struct.unpack_from('<I',raw,offset)[0]
            if role[0]=='label':
                target,size,kind,owner=stage[4][role[1]]
                require(owner<len(stage[3]),'Ordinary auxiliary label lost its allocated section')
                storage=stage[3][owner]
                require(size==kind==0 and storage[1]==1 and storage[2]&7==2 and
                        storage[3]<=target<=storage[3]+storage[5] and value==target,
                        'Ordinary auxiliary input literal lost its exact immutable label')
            else:role_value(stage,typed,role,value)
            normalized[offset:offset+4]=bytes(4)
        require(hashlib.sha256(normalized).hexdigest()==expected,
                'Ordinary read-ahead instructions/constants changed: '+name)
        result[name]=(address,count,hashlib.sha256(raw).hexdigest())
    address,table,section=symbol_bytes(stage,'_native_low_bindings_table',1)
    require(len(table)==48 and not address&3 and section[1]==1 and section[2]&7==2,
            'Ordinary low table must be two complete immutable six-word rows')
    _,message,section=symbol_bytes(stage,'_native_low_bindings_refused',1)
    require(message==MESSAGE and section[1]==1 and section[2]&7==2,
            'Ordinary read-ahead refusal message changed')
    return result


def pinned_functions(stage):
    """Pin five functions, table shape and refusal; require the table companion.

    This is the pin-only API used by existing tail-contract validators. Its
    result does not certify the12 low table words. Complete admission must
    also consume the mandatory dual-callback proof, which binds all words to
    both actual low ELFs, or call pinned(stage,resident,legacy) below.
    """
    require(profile(stage),'Ordinary read-ahead pins require their exact compiled profile')
    return _bodies(stage)


def callback_table(stage,resident,legacy):
    from check_native_dual_callbacks import expected_entries,linked_table
    for image in (resident,legacy):
        require(image[4].get('__kui_native_owner_stack_ip_hook')==ONE and
                image[4].get('__kui_native_general_lean_resident_layout')==ONE and
                '_kui_retail_native_session_control' not in image[4],
                'Ordinary callback companion lacks its exact owned-timer non-session General reader')
    expected=(expected_entries(resident),expected_entries(legacy))
    address=linked_table(stage,expected)
    return dict(address=address,bytes=48,exact_paired_low_roles=12,
        resident_sha256=hashlib.sha256(resident[0]).hexdigest(),
        legacy_sha256=hashlib.sha256(legacy[0]).hexdigest())


def pinned(stage,resident,legacy):
    result=pinned_functions(stage)
    callback_table(stage,resident,legacy)
    return result


def reference(stage,relocations,resident,legacy):
    identities=(hashlib.sha256(stage[0]).hexdigest(),hashlib.sha256(relocations[0]).hexdigest())
    slots=[slot for slot,expected in REFERENCES.items() if expected==identities]
    require(len(slots)==1,'Ordinary reference differs from the independently built pristine3a6 ELFs')
    loads=lambda image:[(p[2:8],image[0][p[1]:p[1]+p[4]]) for p in image[2] if p[0]==1]
    require(loads(stage)==loads(relocations),'Reference --emit-relocs changed PT_LOAD bytes/geometry')
    pinned(stage,resident,legacy);pinned(relocations,resident,legacy);actual=set()
    for section in relocations[3]:
        if section[1]!=4:continue
        require(section[9]==12,'Ordinary reference relocation entry width differs')
        for pos in range(section[4],section[4]+section[5],section[9]):
            address,info,_=struct.unpack_from('<IIi',relocations[0],pos)
            for name,(count,_,roles) in FUNCTIONS.items():
                begin=relocations[4][name][0]
                if begin<=address<begin+count:
                    require(info&255==1,'Unexpected relocation type in ordinary read-ahead closure')
                    actual.add((name,address-begin))
    expected={(name,offset) for name,(_,_,roles) in FUNCTIONS.items() for offset,_ in roles}
    require(actual==expected,'Ordinary authored literals differ from the complete baseline relocation set')
    return dict(status='PASS',reference_commit=REFERENCE_COMMIT,virtual_slot=slots[0],
        reference_elf_sha256=identities[0],reference_relocations_sha256=identities[1],
        identical_loads=True,functions=len(FUNCTIONS),objects=2,exact_named_literals=len(actual),
        paired_low_table=callback_table(stage,resident,legacy))


def function_mutations(stage,resident,legacy):
    pinned(stage,resident,legacy);rejected=0
    for name in FUNCTIONS:
        address,raw,section=symbol_bytes(stage,name,2);start=section[4]+address-section[3]
        for offset in range(len(raw)):
            changed=bytearray(stage[0]);changed[start+offset]^=1
            try:pinned((bytes(changed),*stage[1:]),resident,legacy)
            except ValueError:rejected+=1
            else:raise ValueError('Ordinary read-ahead accepted a changed function byte: '+name)
    return rejected


def object_mutations(stage,resident,legacy):
    pinned(stage,resident,legacy);rejected=0
    for name in ('_native_low_bindings_table','_native_low_bindings_refused'):
        address,raw,section=symbol_bytes(stage,name,1);start=section[4]+address-section[3]
        for offset in range(len(raw)):
            changed=bytearray(stage[0]);changed[start+offset]^=1
            try:pinned((bytes(changed),*stage[1:]),resident,legacy)
            except ValueError:rejected+=1
            else:raise ValueError('Ordinary read-ahead accepted a changed paired table/message byte')
    return rejected
