#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact low read-ahead bodies from the pristine3a6 session reference.

Only the explicit typed absolute literal words below move. Complete original
instruction bodies and numeric guards remain exact, including the dedicated
lease section whose LTO symbol extends16bytes past its executable section.
"""
import hashlib
import struct
from check_vmu_native_worker_gateway import symbol_bytes
from check_native_session_preclear_profile import symbol_records,role_value
REFERENCE_COMMIT='3a6ae064bb3f456da8d71587a8db0ce974f139cf'
FUNCTIONS={'_deliver': (60,
              'c3e3791524e1007e60403d5ec9e81e5b54a0e53ffaae0e75fc29c1e12aea5ae2',
              [(52, ('symbol', '_kui_retail_async_region_storage', 0, 1, 8, 3, 4, 0))]),
 '_deliver_work': (416,
                   '02b364aa15cdbadd2549a48538f21e7de7a75e71d43049ff3b61d5e823301f28',
                   [(364, ('symbol', '_kui_retail_async_region_storage', 0, 1, 8, 3, 4, 0)),
                    (368, ('symbol', '_kui_sci_stream_busy@', 0, 2, 1, 6, 24, 0)),
                    (372, ('symbol', '_kui_sci_stream_poll@', 0, 2, 1, 6, 80, 0))]),
 '_kui_retail_native_session_control': (20,
                                        '2770b1418215baaff625213f5e24669dd17787bef04ae0bb9b5c1746c102e63b',
                                        [(16, ('symbol', '_native_session_binding@', 0, 1, 1, 2, 24, 0))]),
 '_kui_vmu_observe_sd_lease': (748,
                               '5e15e1593f31316ee3c70d41bd4770909d8603c15f2465a96510824940db8145',
                               [(156, ('symbol', '_native_resident_terminal@', 0, 2, 1, 6, 96, 0)),
                                (160, ('symbol', '_kui_vmu_observe_boot_control', 0, 1, 8, 3, 56, 536870912)),
                                (172, ('symbol', '_kui_retail_async_region_storage', 0, 1, 8, 3, 4, 0)),
                                (176, ('symbol', '_kui_retail_hook_active', 0, 1, 8, 3, 4, 0)),
                                (568, ('symbol', '_kui_sci_stream_busy@', 0, 2, 1, 6, 24, 0)),
                                (572, ('symbol', '_unhook@', 0, 2, 1, 6, 68, 0)),
                                (576, ('symbol', '_memcpy', 0, 2, 1, 6, 64, 0)),
                                (580, ('symbol', '_kui_retail_native_async_loan_end@', 0, 2, 1, 6, 252, 0)),
                                (732, ('symbol', '_kui_sci_stream_stop@', 0, 2, 1, 6, 148, 0)),
                                (736, ('symbol', '_kui_retail_hook_fault', 0, 1, 8, 3, 4, 0)),
                                (740, ('symbol', '_kui_sci_sd_release', 0, 2, 1, 6, 364, 0)),
                                (744, ('symbol', '_kui_sci_sd_healthy@', 0, 2, 1, 6, 32, 0))])}

def body(image,name):
    if name=='_kui_vmu_observe_sd_lease':
        address,count,kind,owner=image[4][name];section=image[3][owner]
        if kind!=2 or section[1]!=1 or section[2]&7!=6 or address!=section[3] or count!=section[5]+16:
            raise ValueError('Session lease dedicated section shape changed')
        return address,image[0][section[4]:section[4]+section[5]],section
    return symbol_bytes(image,name,2)

def pinned(image,names=tuple(FUNCTIONS)):
    records=symbol_records(image);result={}
    for name in names:
        count,expected,roles=FUNCTIONS[name];address,raw,section=body(image,name)
        if len(raw)!=count or section[1]!=1 or section[2]&7!=6 or not 0x8c004000<=address<address+count<=0x8c007ba0:
            raise ValueError('Session low read-ahead function owner/type/size changed: '+name)
        normalized=bytearray(raw);seen=set()
        for offset,role in roles:
            if offset&3 or not 0<=offset<=count-4 or offset in seen:
                raise ValueError('Session low authored literal crosses/overlaps its body')
            seen.add(offset);role_value(image,records,role,struct.unpack_from('<I',raw,offset)[0])
            normalized[offset:offset+4]=bytes(4)
        if hashlib.sha256(normalized).hexdigest()!=expected:
            raise ValueError('Session low read-ahead instructions/constants changed: '+name)
        result[name]=(count,hashlib.sha256(raw).hexdigest())
    return result

def reference(image):
    if hashlib.sha256(image[0]).hexdigest()!='808c950b2a5fb66230c7462f550490fbaa48688c141670749261792eff0eaac2':
        raise ValueError('Low read-ahead reference differs from the pristine3a6 rebuild')
    pinned(image)
    return dict(status='PASS',reference_commit=REFERENCE_COMMIT,functions=len(FUNCTIONS),
        exact_typed_literal_words=sum(len(row[2]) for row in FUNCTIONS.values()),
        complete_pristine_function_instruction_bytes=True)
