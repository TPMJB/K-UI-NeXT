#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact ordinary General read-ahead bodies from clean3a6 plus low flags.

The independent ordinary A1/A2 reference readers use only the reviewed New
reader compile/link flags: -fno-guess-branch-probability, -mbranch-cost=1 and
-fno-caller-saves. Their three complete bodies normalize identically to the
current readers after checking these17 exact typed absolute literal roles.
Historical748-byte loan sections and the separate session ABI keep their
existing maps; no digest is learned from the candidate under inspection.
"""
import hashlib
import struct

from check_vmu_native_worker_gateway import symbol_bytes
from check_native_session_preclear_profile import symbol_records,role_value
from native_runtime_layout import GENERAL,BACKGROUND_COMPACT,layout_for

ONE=(1,0,0,0xfff1)
REFERENCE_COMMIT='3a6ae064bb3f456da8d71587a8db0ce974f139cf'
REFERENCE_ELF_SHA256='7eaaa3132a59399db913d2b03ab90ac5428bcf6a911f32ae70c5ac1ccdda672a'
FUNCTIONS={
    '_deliver':(60,'242125a284829b39524e140dbe2fff926b08cd336d95689ae83ce224828e0dd0',[
        (52,('symbol','_kui_retail_async_region_storage',0,1,8,3,4,0))]),
    '_deliver_work':(456,'090909945931633ed701f63a1efc05c6ff7039e8a624a551a62d6721c2c10f88',[
        (384,('symbol','_kui_retail_async_region_storage',0,1,8,3,4,0)),
        (388,('symbol','_kui_sci_stream_busy@',0,2,1,6,24,0)),
        (396,('symbol','_kui_sci_stream_poll@',0,2,1,6,80,0)),
        (408,('symbol','_kui_retail_cursor_feed@',0,2,1,6,538,0))]),
    '_kui_vmu_observe_sd_lease':(756,'7616ec4d3ea885679486c8f49d937cc3172989c7c85c48dc75ce1981c6275655',[
        (156,('symbol','_native_resident_terminal@',0,2,1,6,96,0)),
        (160,('symbol','_kui_vmu_observe_boot_control',0,1,8,3,56,0x20000000)),
        (172,('symbol','_kui_retail_async_region_storage',0,1,8,3,4,0)),
        (176,('symbol','_kui_retail_hook_active',0,1,8,3,4,0)),
        (576,('symbol','_kui_sci_stream_busy@',0,2,1,6,24,0)),
        (580,('symbol','_unhook@',0,2,1,6,68,0)),
        (584,('symbol','_memcpy',0,2,1,6,64,0)),
        (588,('symbol','_kui_retail_native_async_loan_end@',0,2,1,6,252,0)),
        (740,('symbol','_kui_sci_stream_stop@',0,2,1,6,152,0)),
        (744,('symbol','_kui_retail_hook_fault',0,1,8,3,4,0)),
        (748,('symbol','_kui_sci_sd_release',0,2,1,6,364,0)),
        (752,('symbol','_kui_sci_sd_healthy@',0,2,1,6,32,0))]),
}


def profile(image):
    marker=image[4].get('__kui_native_general_lean_resident_layout')
    if marker is None or '_kui_retail_native_session_control' in image[4]:return False
    if layout_for(image[4]) is BACKGROUND_COMPACT:return False
    if marker!=ONE or any(image[4].get(name)!=ONE for name in
            ('__kui_native_readahead_resident','__kui_vmu_native_background_resident',
             '__kui_native_sdk_sci_structural_resident','__kui_native_sdk_sci_route_resident')):
        raise ValueError('Ordinary General low read-ahead markers differ')
    if layout_for(image[4]) is not GENERAL:
        raise ValueError('Ordinary General low read-ahead requires its complete112KiB geometry')
    row=image[4].get('_kui_vmu_observe_sd_lease')
    if row is None:return False
    address,count,kind,owner=row
    if owner>=len(image[3]):raise ValueError('Ordinary General loan section is unowned')
    section=image[3][owner]
    return (kind==2 and count==772 and section[1]==1 and section[2]&7==6 and
            section[5]==756 and address==section[3] and
            0x8c004000<=address<address+section[5]<=0x8c007ba0)


def body(image,name):
    if name=='_kui_vmu_observe_sd_lease':
        address,count,kind,owner=image[4][name];section=image[3][owner]
        if kind!=2 or section[1]!=1 or section[2]&7!=6 or address!=section[3] or count!=section[5]+16:
            raise ValueError('Ordinary General lease dedicated section shape changed')
        return address,image[0][section[4]:section[4]+section[5]],section
    return symbol_bytes(image,name,2)


def pinned(image,names=tuple(FUNCTIONS)):
    if not profile(image):raise ValueError('Ordinary General low map requires its exact non-session756-byte profile')
    records=symbol_records(image);result={}
    for name in names:
        count,expected,roles=FUNCTIONS[name];address,raw,section=body(image,name)
        if len(raw)!=count or section[1]!=1 or section[2]&7!=6 or not 0x8c004000<=address<address+count<=0x8c007ba0:
            raise ValueError('Ordinary General low read-ahead body owner/type/size changed: '+name)
        normalized=bytearray(raw);seen=set()
        for offset,role in roles:
            if offset&3 or not 0<=offset<=count-4 or offset in seen:
                raise ValueError('Ordinary General low authored literal crosses/overlaps its body')
            seen.add(offset)
            role_value(image,records,role,struct.unpack_from('<I',raw,offset)[0])
            normalized[offset:offset+4]=bytes(4)
        if hashlib.sha256(normalized).hexdigest()!=expected:
            raise ValueError('Ordinary General low read-ahead instructions/constants changed: '+name)
        result[name]=(count,hashlib.sha256(raw).hexdigest())
    return result


def reference(image):
    if hashlib.sha256(image[0]).hexdigest()!=REFERENCE_ELF_SHA256:
        raise ValueError('Ordinary General low reference differs from the pristine3a6 three-flag rebuild')
    pinned(image)
    return dict(status='PASS',reference_commit=REFERENCE_COMMIT,functions=len(FUNCTIONS),
        exact_typed_literal_words=sum(len(row[2]) for row in FUNCTIONS.values()),
        ordinary_non_session_low_profile=True,complete_pristine_function_instruction_bytes=True)
