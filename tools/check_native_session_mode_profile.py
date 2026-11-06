#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Closed session mode map, including the reviewed shared FAT-cache guard.

The two mode/policy bodies normalize identically to the pristine3a6 session
reference. Storage initialization adds the existing Smash cache-configuration
branch for every native profile; its source delta is exactly removal of that
branch's two KUI_SMASH_NATIVE_MENU preprocessor guards. All776 emitted bytes
are checked, with only these explicitly authored typed literal words moved.
The actual SH route/mutation model remains the admission proof.
"""
import hashlib
import struct
from pathlib import Path
from functools import lru_cache
from check_vmu_native_worker_gateway import symbol_bytes
from check_native_session_preclear_profile import symbol_records,role_value
REFERENCE_COMMIT='3a6ae064bb3f456da8d71587a8db0ce974f139cf'
BASELINE_FUNCTIONS={'_native_persist_storage_init': (720,
                                  'a39704e920c48b66f0200f20d088718f43099ff2667c07ba6471de842b9533c2',
                                  [(168,
                                    ('symbol',
                                     '_native_runtime_mode@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     88,
                                     0)),
                                   (172, ('symbol', '_kui_retail_storage_init', 0, 2, 1, 6, 188, 0)),
                                   (176,
                                    ('symbol',
                                     '_native_persist_ready@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     1,
                                     0)),
                                   (180, ('profile_label', '__vmu_sd_profile_blob_start')),
                                   (184, ('profile_label', '__vmu_sd_profile_blob_end')),
                                   (188, ('string', 'NATIVE VMU PREPARED PROFILE / GAME MAP REFUSED\x00')),
                                   (192, ('symbol', '_stopped@vmu_native_heap_probe_stage.c', 0, 2, 1, 6, 68, 0)),
                                   (196, ('symbol', '_kui_vmu_persist_profile_decode', 0, 2, 1, 6, 960, 0)),
                                   (200,
                                    ('symbol',
                                     '_native_persist_profile@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     2728,
                                     0)),
                                   (204, ('symbol', '_kui_vmu_persist_stage_admit', 0, 2, 1, 6, 296, 0)),
                                   (208, ('symbol', '_manifest@vmu_native_heap_probe_stage.c', 0, 1, 8, 3, 2172, 0)),
                                   (212, ('string', 'NATIVE VMU REQUIRES STANDARD SCI STORAGE\x00')),
                                   (596, ('symbol', '_kui_retail_storage_acquire', 0, 2, 1, 6, 52, 0)),
                                   (600, ('symbol', '_retail_display_line', 0, 2, 1, 6, 400, 0)),
                                   (604, ('string', 'NATIVE VMU: LIVE OWNERSHIP / RECOVERY - KEEP POWER ON\x00')),
                                   (608,
                                    ('symbol',
                                     '_native_persist_identity@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     76,
                                     0)),
                                   (612, ('symbol', '_kui_vmu_persist_stage_prepare', 0, 2, 1, 6, 32, 0)),
                                   (616,
                                    ('symbol',
                                     '_native_persist_read@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     40,
                                     0)),
                                   (620,
                                    ('symbol',
                                     '_native_persist_handoff@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     1272,
                                     0)),
                                   (624,
                                    ('symbol',
                                     '_native_persist_write@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     40,
                                     0)),
                                   (628,
                                    ('symbol',
                                     '_native_persist_work@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     58964,
                                     0)),
                                   (632,
                                    ('symbol',
                                     '_native_persist_sync@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     32,
                                     0)),
                                   (636, ('symbol', '_manifest@vmu_native_heap_probe_stage.c', 0, 1, 8, 3, 2172, 0)),
                                   (640,
                                    ('symbol',
                                     '_native_persist_healthy@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     64,
                                     0)),
                                   (644,
                                    ('symbol',
                                     '_native_persist_profile@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     2728,
                                     0)),
                                   (648, ('symbol', '_kui_retail_storage_stop', 0, 2, 1, 6, 96, 0)),
                                   (652, ('symbol', '_kui_retail_storage_release', 0, 2, 1, 6, 32, 0)),
                                   (656, ('symbol', '_kui_sci_sd_faulted', 0, 2, 1, 6, 12, 0)),
                                   (660,
                                    ('symbol',
                                     '_native_persist_work@vmu_native_heap_probe_stage.c',
                                     3324,
                                     1,
                                     8,
                                     3,
                                     58964,
                                     0)),
                                   (664, ('symbol', '_memset', 0, 2, 1, 6, 26, 0)),
                                   (668,
                                    ('symbol',
                                     '_native_persist_work@vmu_native_heap_probe_stage.c',
                                     58940,
                                     1,
                                     8,
                                     3,
                                     58964,
                                     0)),
                                   (672, ('symbol', '_retail_display_values', 0, 2, 1, 6, 124, 0)),
                                   (676, ('symbol', '_kui_vmu_persist_session_poison', 0, 2, 1, 6, 40, 0)),
                                   (680, ('symbol', '_retail_display_restore', 0, 2, 1, 6, 136, 0)),
                                   (684, ('symbol', '_display@vmu_native_heap_probe_stage.c', 0, 1, 8, 3, 56, 0)),
                                   (688, ('string', 'NATIVE VMU PREPARATION FAILED\x00')),
                                   (692, ('string', 'PREPARE  SESSION  FS       DETAIL   PHASE\x00')),
                                   (696, ('string', 'STOP     HEALTH   REJECT   FLAGS    VALID\x00')),
                                   (700, ('string', 'ZERO     REV      SECTOR   CLUSTER  FATS\x00')),
                                   (704, ('string', 'PARTLO   PARTHI   EXPLO    EXPHI\x00')),
                                   (708, ('string', 'VOLUMELO VOLUMEHI EXPLO    EXPHI\x00')),
                                   (712, ('string', 'PHOTO THIS SCREEN THEN POWER OFF\x00')),
                                   (716, ('string', 'PREPARED VMU MAY REQUIRE JOURNAL RECOVERY\x00'))]),
 '_native_runtime_diagnostics': (44,
                                 'a10cd43769d510cb9ae0f952fbdbba53f6e42843fd34516485c003d8640ea057',
                                 [(36,
                                   ('symbol', '_native_runtime_mode@vmu_native_heap_probe_stage.c', 0, 2, 1, 6, 88, 0)),
                                  (40, ('symbol', '_CSWTCH.296@vmu_native_heap_probe_stage.c', 0, 1, 1, 2, 6, 0))]),
 '_native_runtime_mode': (88,
                          'c024b257b7db8639e919bf3220c13ca67e8c5d33512c83479d26d3cce9cb43c2',
                          [(60, ('symbol', '_kui_vmu_native_mode_decode', 0, 2, 1, 6, 124, 0)),
                           (64, ('symbol', '__native_runtime_mode_start', 0, 1, 1, 2, 32, 0)),
                           (68, ('string', 'NATIVE VMU RUNTIME MODE REFUSED\x00')),
                           (72, ('symbol', '_kui_vmu_native_toggle_capability', 0, 1, 1, 2, 32, 0)),
                           (76, ('string', 'VMU OFF CAPABILITY REFUSED\x00')),
                           (80, ('symbol', '_stopped@vmu_native_heap_probe_stage.c', 0, 2, 1, 6, 68, 0)),
                           (84, ('string', 'BACKGROUND VMU REQUIRES LIVE WORKER RESERVATION\x00'))])}

FUNCTIONS={'_native_persist_storage_init': (776,
                                  '53d6a8c67ba5fb703057d8575ee4f1757353a835a11acc99d3cc0a9f3d742c86',
                                  [(208,
                                    ('symbol',
                                     '_native_runtime_mode@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     88,
                                     0)),
                                   (212, ('symbol', '_kui_retail_storage_init', 0, 2, 1, 6, 188, 0)),
                                   (216,
                                    ('symbol',
                                     '_native_persist_ready@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     1,
                                     0)),
                                   (220, ('profile_label', '__vmu_sd_profile_blob_start')),
                                   (224, ('profile_label', '__vmu_sd_profile_blob_end')),
                                   (228, ('string', 'NATIVE VMU PREPARED PROFILE / GAME MAP REFUSED\x00')),
                                   (232, ('symbol', '_stopped@vmu_native_heap_probe_stage.c', 0, 2, 1, 6, 68, 0)),
                                   (236, ('symbol', '_kui_vmu_persist_profile_decode', 0, 2, 1, 6, 960, 0)),
                                   (240,
                                    ('symbol',
                                     '_native_persist_profile@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     2728,
                                     0)),
                                   (244, ('symbol', '_kui_vmu_persist_stage_admit', 0, 2, 1, 6, 296, 0)),
                                   (248, ('symbol', '_manifest@vmu_native_heap_probe_stage.c', 0, 1, 8, 3, 2172, 0)),
                                   (252, ('string', 'NATIVE VMU REQUIRES STANDARD SCI STORAGE\x00')),
                                   (256, ('symbol', '_kui_smash_fat_cache_configure', 0, 2, 1, 6, 176, 0)),
                                   (260,
                                    ('symbol',
                                     '_smash_fat_read_cache@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     16512,
                                     0)),
                                   (264, ('string', 'NATIVE VMU FRESH FAT CACHE BOUNDS REFUSED\x00')),
                                   (652, ('symbol', '_kui_retail_storage_acquire', 0, 2, 1, 6, 52, 0)),
                                   (656, ('symbol', '_retail_display_line', 0, 2, 1, 6, 400, 0)),
                                   (660, ('string', 'NATIVE VMU: LIVE OWNERSHIP / RECOVERY - KEEP POWER ON\x00')),
                                   (664,
                                    ('symbol',
                                     '_native_persist_identity@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     96,
                                     0)),
                                   (668,
                                    ('symbol',
                                     '_native_persist_handoff@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     1272,
                                     0)),
                                   (672,
                                    ('symbol',
                                     '_native_persist_read@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     56,
                                     0)),
                                   (676,
                                    ('symbol',
                                     '_native_persist_work@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     58964,
                                     0)),
                                   (680,
                                    ('symbol',
                                     '_native_persist_write@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     72,
                                     0)),
                                   (684, ('symbol', '_manifest@vmu_native_heap_probe_stage.c', 0, 1, 8, 3, 2172, 0)),
                                   (688,
                                    ('symbol',
                                     '_native_persist_sync@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     56,
                                     0)),
                                   (692,
                                    ('symbol',
                                     '_native_persist_profile@vmu_native_heap_probe_stage.c',
                                     0,
                                     1,
                                     8,
                                     3,
                                     2728,
                                     0)),
                                   (696,
                                    ('symbol',
                                     '_native_persist_healthy@vmu_native_heap_probe_stage.c',
                                     0,
                                     2,
                                     1,
                                     6,
                                     64,
                                     0)),
                                   (700, ('symbol', '_kui_vmu_persist_stage_prepare', 0, 2, 1, 6, 32, 0)),
                                   (704, ('symbol', '_kui_retail_storage_stop', 0, 2, 1, 6, 96, 0)),
                                   (708, ('symbol', '_kui_retail_storage_release', 0, 2, 1, 6, 32, 0)),
                                   (712, ('symbol', '_kui_sci_sd_faulted', 0, 2, 1, 6, 12, 0)),
                                   (716,
                                    ('symbol',
                                     '_native_persist_work@vmu_native_heap_probe_stage.c',
                                     3324,
                                     1,
                                     8,
                                     3,
                                     58964,
                                     0)),
                                   (720, ('symbol', '_memset', 0, 2, 1, 6, 26, 0)),
                                   (724,
                                    ('symbol',
                                     '_native_persist_work@vmu_native_heap_probe_stage.c',
                                     58940,
                                     1,
                                     8,
                                     3,
                                     58964,
                                     0)),
                                   (728, ('symbol', '_retail_display_values', 0, 2, 1, 6, 124, 0)),
                                   (732, ('symbol', '_kui_vmu_persist_session_poison', 0, 2, 1, 6, 40, 0)),
                                   (736, ('symbol', '_retail_display_restore', 0, 2, 1, 6, 136, 0)),
                                   (740, ('symbol', '_display@vmu_native_heap_probe_stage.c', 0, 1, 8, 3, 56, 0)),
                                   (744, ('string', 'NATIVE VMU PREPARATION FAILED\x00')),
                                   (748, ('string', 'PREPARE  SESSION  FS       DETAIL   PHASE\x00')),
                                   (752, ('string', 'STOP     HEALTH   REJECT   FLAGS    VALID\x00')),
                                   (756, ('string', 'ZERO     REV      SECTOR   CLUSTER  FATS\x00')),
                                   (760, ('string', 'PARTLO   PARTHI   EXPLO    EXPHI\x00')),
                                   (764, ('string', 'VOLUMELO VOLUMEHI EXPLO    EXPHI\x00')),
                                   (768, ('string', 'PHOTO THIS SCREEN THEN POWER OFF\x00')),
                                   (772, ('string', 'PREPARED VMU MAY REQUIRE JOURNAL RECOVERY\x00'))]),
 '_native_runtime_diagnostics': (44,
                                 'a10cd43769d510cb9ae0f952fbdbba53f6e42843fd34516485c003d8640ea057',
                                 [(36,
                                   ('symbol', '_native_runtime_mode@vmu_native_heap_probe_stage.c', 0, 2, 1, 6, 88, 0)),
                                  (40, ('policy_table',))]),
 '_native_runtime_mode': (88,
                          'c024b257b7db8639e919bf3220c13ca67e8c5d33512c83479d26d3cce9cb43c2',
                          [(60, ('symbol', '_kui_vmu_native_mode_decode', 0, 2, 1, 6, 124, 0)),
                           (64, ('symbol', '__native_runtime_mode_start', 0, 1, 1, 2, 32, 0)),
                           (68, ('string', 'NATIVE VMU RUNTIME MODE REFUSED\x00')),
                           (72, ('symbol', '_kui_vmu_native_toggle_capability', 0, 1, 1, 2, 32, 0)),
                           (76, ('string', 'VMU OFF CAPABILITY REFUSED\x00')),
                           (80, ('symbol', '_stopped@vmu_native_heap_probe_stage.c', 0, 2, 1, 6, 68, 0)),
                           (84, ('string', 'BACKGROUND VMU REQUIRES LIVE WORKER RESERVATION\x00'))])}

def policy_table(stage,records,value=None):
    expected=bytes.fromhex('010002030405');matches=[]
    for name,(address,count,kind,owner) in records.items():
        if not name.startswith('_CSWTCH.') or not name.endswith('@vmu_native_heap_probe_stage.c'):continue
        section=stage[3][owner]
        if count!=len(expected) or kind!=1 or section[1]!=1 or section[2]&7!=2:continue
        offset=section[4]+address-section[3];body=stage[0][offset:offset+count]
        if body==expected:matches.append((address,body,section))
    if len(matches)!=1 or value is not None and value!=matches[0][0]:
        raise ValueError('Native mode lost its unique complete source-owned six-mode policy table')
    return matches[0]

def profile(stage):
    marker=stage[4].get('__kui_native_fat_read_cache')
    if marker is None:return False
    from check_native_vmu_toggle_profile import stage_profile as toggle
    from check_native_general_lean_profile import stage_profile as general
    from native_runtime_layout import layout_for,GENERAL
    if not toggle(stage):return False
    if marker!=(1,0,0,0xfff1) or not general(stage) or layout_for(stage[4]) is not GENERAL:
        raise ValueError('Shared FAT mode map requires the exact full General toggle profile')
    if '__native_session_descriptor' in stage[4]:
        from check_native_session_preclear_profile import stage_profile as session
        if not session(stage):raise ValueError('Session FAT mode map lost its complete typed profile')
    return True

def pinned(stage):
    if not profile(stage):raise ValueError('Shared FAT mode map requires its complete typed profile')
    source_delta()
    return _bodies(stage,FUNCTIONS)

def _bodies(stage,functions):
    records=symbol_records(stage);result={}
    for name,(count,expected,roles) in functions.items():
        address,body,section=symbol_bytes(stage,name,2)
        if len(body)!=count or section[1]!=1 or section[2]&7!=6:
            raise ValueError('Session mode function type/size differs: '+name)
        normalized=bytearray(body);seen=set()
        for offset,role in roles:
            if offset&3 or not 0<=offset<=count-4 or offset in seen:
                raise ValueError('Session mode authored literal crosses/overlaps its body')
            seen.add(offset);value=struct.unpack_from('<I',body,offset)[0]
            if role[0]=='policy_table':
                policy_table(stage,records,value)
            elif role[0]=='profile_label':
                target,size,kind,owner=stage[4][role[1]];storage=stage[3][owner]
                if size or kind or storage[1]!=1 or storage[2]&7!=2 or value!=target or not storage[3]<=target<=storage[3]+storage[5]:
                    raise ValueError('Session mode profile literal lost its immutable boundary')
            else:role_value(stage,records,role,value)
            normalized[offset:offset+4]=bytes(4)
        if hashlib.sha256(normalized).hexdigest()!=expected:
            raise ValueError('Session mode instructions/constants changed: '+name)
        result[name]=(count,hashlib.sha256(body).hexdigest())
    return result

CACHE_BRANCH='    if(!kui_smash_fat_cache_configure(&smash_fat_read_cache,storage->device.sd.blocks,\n       native_persist_profile.partition_first,native_persist_profile.partition_end))\n        stopped("NATIVE VMU FRESH FAT CACHE BOUNDS REFUSED",0u);\n'
REFERENCE_STORAGE_SOURCE_SHA256='a47946d0555b2bfdf812879f731d2311fd18b881a5983de6e348301b178c95f8'

@lru_cache(maxsize=1)
def source_delta():
    from test_native_dual_readers import function_body
    source=(Path(__file__).resolve().parents[1]/'src/loader/vmu_native_persist_stage.inc').read_text()
    body=function_body(source,'native_persist_storage_init')
    if body.count(CACHE_BRANCH)!=1:
        raise ValueError('Shared FAT mode route lacks its exact reviewed configuration branch')
    restored=body.replace(CACHE_BRANCH,'#if KUI_SMASH_NATIVE_MENU\n'+CACHE_BRANCH+'#endif\n')
    if hashlib.sha256(restored.encode()).hexdigest()!=REFERENCE_STORAGE_SOURCE_SHA256:
        raise ValueError('Session storage mode source differs beyond the reviewed two guard removals')
    return {'reference_commit':REFERENCE_COMMIT,'existing_cache_branch_promoted':True,
            'only_storage_source_delta':'remove two title guards',
            'reference_function_source_sha256':REFERENCE_STORAGE_SOURCE_SHA256}

def byte_mutations(stage):
    pinned(stage);rejected=0
    for name in FUNCTIONS:
        address,body,section=symbol_bytes(stage,name,2);start=section[4]+address-section[3]
        for offset in range(len(body)):
            data=bytearray(stage[0]);data[start+offset]^=1
            try:pinned((bytes(data),*stage[1:]))
            except ValueError:rejected+=1
            else:raise ValueError('Session mode map accepted a changed instruction/literal byte')
    address,body,section=policy_table(stage,symbol_records(stage))
    start=section[4]+address-section[3]
    for offset in range(len(body)):
        data=bytearray(stage[0]);data[start+offset]^=1
        try:pinned((bytes(data),*stage[1:]))
        except ValueError:rejected+=1
        else:raise ValueError('Native mode map accepted a changed policy byte')
    return rejected

def reference(stage,relocations):
    expected_stage='b5b94ce07d24d75af7874dbc716d00a88a926c98a4a3928fcb63424d39209e58'
    expected_reloc='8fa3a0239ac5312e0f8944eddabf9dbf3ec57aa7bbcc8480253f301e7bd19f55'
    if hashlib.sha256(stage[0]).hexdigest()!=expected_stage or hashlib.sha256(relocations[0]).hexdigest()!=expected_reloc:
        raise ValueError('Session mode reference differs from the pristine3a6 stage')
    loads=lambda image:[(row[2:8],image[0][row[1]:row[1]+row[4]]) for row in image[2] if row[0]==1]
    if loads(stage)!=loads(relocations):raise ValueError('Mode reference emit-relocs changed actual loaded bytes')
    _bodies(stage,BASELINE_FUNCTIONS);_bodies(relocations,BASELINE_FUNCTIONS);actual=set()
    for section in relocations[3]:
        if section[1]!=4:continue
        for pos in range(section[4],section[4]+section[5],section[9]):
            address,info,_=struct.unpack_from('<IIi',relocations[0],pos)
            for name,(count,_,roles) in BASELINE_FUNCTIONS.items():
                start=relocations[4][name][0]
                if start<=address<start+count:
                    if info&255!=1:raise ValueError('Unexpected mode reference relocation type')
                    actual.add((name,address-start))
    expected={(name,offset) for name,(_,_,roles) in BASELINE_FUNCTIONS.items() for offset,_ in roles}
    if actual!=expected:raise ValueError('Mode authored baseline roles omit or add relocation words')
    for name in ('_native_runtime_mode','_native_runtime_diagnostics'):
        if FUNCTIONS[name][:2]!=BASELINE_FUNCTIONS[name][:2]:
            raise ValueError('Mode/policy changed beyond typed baseline literal relocation')
    return dict(status='PASS',reference_commit=REFERENCE_COMMIT,identical_reference_loads=True,
        reference_stage_sha256=expected_stage,reference_relocations_sha256=expected_reloc,
        baseline_exact_named_literals=len(actual),unchanged_mode_and_policy_bodies=True,
        storage_source_delta=source_delta())
