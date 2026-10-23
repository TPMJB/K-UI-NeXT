#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Canonical native runtime selection; never grants memory or storage ownership."""
import struct

MODE_BYTES=32
NORMAL=0
CONTROL=1
PROFILE=2
LEASE_ONLY=3
HOOKS_ONLY=4
RAW_CHAIN=5
MODE_NAMES={NORMAL:'normal',CONTROL:'control',PROFILE:'profile',LEASE_ONLY:'lease-only',HOOKS_ONLY:'hooks-only',RAW_CHAIN:'raw-chain'}

def require(condition,message):
    if not condition:raise ValueError(message)

def template_mode():
    return b'KUINVM01'+struct.pack('<3I',1,MODE_BYTES,NORMAL)+bytes(12)

def decode_mode(wire):
    wire=bytes(wire)
    require(len(wire)==MODE_BYTES and wire[:8]==b'KUINVM01' and
            struct.unpack_from('<2I',wire,8)==(1,MODE_BYTES) and not any(wire[20:]),
            'Native runtime mode header/reserved bytes differ')
    mode=struct.unpack_from('<I',wire,16)[0]
    require(mode in MODE_NAMES,'Native runtime mode is unsupported')
    return mode

def find_mode(module):
    module=bytes(module);found=None;start=0
    while True:
        at=module.find(b'KUINVM01',start)
        if at<0:break
        start=at+1
        try:decode_mode(module[at:at+MODE_BYTES])
        except (ValueError,struct.error):continue
        require(found is None,'Native runtime mode is duplicated or ambiguous')
        found=at
    return found

def fill_mode(wire,mode):
    decode_mode(wire)
    require(type(mode) is int and mode in MODE_NAMES,'Native runtime mode is unsupported')
    result=bytearray(template_mode());struct.pack_into('<I',result,16,mode)
    require(decode_mode(result)==mode,'Native runtime mode round trip differs')
    return bytes(result)
