#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Canonical native resolution record/seed codec; never grants live ownership.

Integrity hashes are not authentication. A filled seed requires a record
digest independently pinned by its audited package catalog, or another
explicitly trusted configuration source.
"""
import hashlib
import struct

SEED_BYTES=1120
RECORD_BYTES=1024
DETECTOR_EPOCH=8
RECORD_PAYLOAD_BYTES=752
EXIT_VERSION=2
EXIT_BYTES=52

def require(condition,message):
    if not condition:raise ValueError(message)

def _word(data,offset):return struct.unpack_from('<I',data,offset)[0]

def exit_schema(wire,load,image_bytes):
    """Mirror the versioned C descriptor's family/count/unused-slot rules."""
    family,count=_word(wire,708),_word(wire,720)
    return (tuple(_word(wire,n) for n in (700,704,712,716))==
            (EXIT_VERSION,EXIT_BYTES,load,image_bytes) and
            ((family in (1,2,3) and count==3 and not _word(wire,736)) or
             (family==4 and count==2 and not _word(wire,732) and
              not _word(wire,736) and not _word(wire,744)) or
             (family==5 and count==3 and _word(wire,732) and
              not _word(wire,736) and _word(wire,744)) or
             (family==6 and count==4 and _word(wire,736)) or
             (family==7 and count==2 and _word(wire,724) and
              _word(wire,728)==((_word(wire,724)+8)&0xffffffff) and
              not _word(wire,732) and not _word(wire,736) and not _word(wire,744)) or
             (family==8 and count==3 and _word(wire,724) and
              _word(wire,728)==((_word(wire,724)+8)&0xffffffff) and
              _word(wire,732) and not _word(wire,736) and
              _word(wire,744)==((_word(wire,732)+76)&0xffffffff)) or
             (family in (9,10) and count==3 and _word(wire,732) and
              not _word(wire,736) and not _word(wire,744) and
              (family==9 or (_word(wire,724) and
               _word(wire,728)==((_word(wire,724)+8)&0xffffffff))))))

def _ram_span(address,size):
    physical=address&0x1fffffff
    return ((address&0xe0000000) in (0,0x80000000,0xa0000000) and
            0x0c000000<=physical<0x0d000000 and size<=0x0d000000-physical)

def heap_schema(wire,load,image_bytes):
    """Mirror C's canonical descriptors; this is not a discovery proof."""
    if tuple(_word(wire,n) for n in (608,612,624,628))!=(1,72,image_bytes,load):return False
    family,layout=_word(wire,616),_word(wire,620)
    slot,target=_word(wire,636),_word(wire,640)
    if family==2:
        absent=(620,632,640,644,648,652,656,660,664,668,672,676)
        return (not any(_word(wire,n) for n in absent) and slot and
                _word(wire,688)==((_word(wire,684)+4)&0xffffffff) and _word(wire,696))
    if family==1:
        return layout==1 and (slot or (not target and
            _word(wire,688)==((_word(wire,684)+4)&0xffffffff) and _word(wire,696)))
    if family!=3 or layout!=2 or not slot:return False
    initializer,allocate,release=(_word(wire,n) for n in (632,644,648))
    sentinel,cursor,begin,heap_bytes,copy=(_word(wire,n) for n in (660,664,668,672,676))
    physical=lambda address:address&0x1fffffff
    # DIRECT32 has no dispatch roles. Its complete code span and immutable
    # initializer-pointer role use the same address rules as heap_metadata_valid.
    return (not _word(wire,652) and not _word(wire,656) and
            not (load&3) and _ram_span(load,image_bytes) and
            not (sentinel&3) and _ram_span(sentinel,0x30) and
            not (initializer&3) and initializer>=load and
            initializer-load<=image_bytes and image_bytes-(initializer-load)>=0x268 and
            not (slot&3) and _ram_span(slot,4) and
            physical(slot)>=physical(load) and physical(slot)-physical(load)<=image_bytes-4 and
            _ram_span(target,2) and physical(target)==physical(initializer) and
            cursor==sentinel+0x20 and begin==sentinel+0x24 and
            heap_bytes==sentinel+0x28 and copy==sentinel+0x2c and
            not ((allocate|release)&1) and physical(allocate)==physical(initializer)+0xae and
            physical(release)==physical(initializer)+0x160 and
            _ram_span(initializer,0x268) and _ram_span(allocate,2) and _ram_span(release,2))

def template_seed():
    return b'KUINVS01'+struct.pack('<4I',1,SEED_BYTES,0,RECORD_BYTES)+bytes(SEED_BYTES-24)

def record_metadata(wire,expected_digest=None):
    wire=bytes(wire)
    require(len(wire)==RECORD_BYTES,'Native cache record must be exactly 1024 bytes')
    require(wire[:8]==b'KUINVR01' and tuple(_word(wire,n) for n in (8,12,16))==
            (1,RECORD_BYTES,DETECTOR_EPOCH) and _word(wire,20) in (1,2,3),
            'Native cache record version/epoch/schema differs')
    require(not any(wire[RECORD_PAYLOAD_BYTES:992]),'Native cache record reserved bytes are not zero')
    digest=hashlib.sha256(wire[:992]).digest()
    require(wire[992:]==digest,'Native cache record digest mismatch')
    if expected_digest is not None:
        if isinstance(expected_digest,str):
            require(len(expected_digest)==64 and all(c in '0123456789abcdef' for c in expected_digest),
                    'Native cache trusted digest must be canonical hexadecimal')
            expected_digest=bytes.fromhex(expected_digest)
        require(isinstance(expected_digest,(bytes,bytearray,memoryview)) and len(expected_digest)==32,
                'Native cache trusted digest must contain exactly 32 bytes')
        require(bytes(expected_digest)==digest,'Native cache record differs from its independently pinned digest')
    image_bytes,load=_word(wire,24),_word(wire,28)
    require(0x300<=image_bytes<=0xc00000 and load==0x8c010000,
            'Native cache executable size/load is unsupported')
    require(tuple(_word(wire,n) for n in (64,68,76,80,84))==
            (1,104,image_bytes,load,7) and _word(wire,72) in (1,2,3) and _word(wire,132) in (12,13) and
            wire[136:168]==wire[32:64],'Native cache driver schema differs')
    require(_word(wire,168) in (1,2) and _word(wire,172)==440 and
            (_word(wire,192),_word(wire,404)) in ((1,12),(0,1)),
            'Native cache layout schema differs')
    require(heap_schema(wire,load,image_bytes) and _word(wire,20)==
            (3 if _word(wire,616)==2 else 1 if _word(wire,636) else 2),
            'Native cache allocator schema differs')
    # The P2-lock early family is admitted only through the separately proved
    # public heap-then-Maple call sequence. Scalar shape is not source proof:
    # the C fresh/replay path still checks the complete original call body.
    call=_word(wire,684)
    require(_word(wire,72)!=3 or (_word(wire,636) and call>=load+82 and
            _word(wire,680)==call-12 and call-load<=image_bytes-30 and _word(wire,688)==call+4 and
            call+26==_word(wire,184)),
            'Native cache early P2 call ordering differs')
    require(exit_schema(wire,load,image_bytes),'Native cache exit schema differs')
    return dict(detector_epoch=DETECTOR_EPOCH,record_bytes=RECORD_BYTES,
        record_digest=digest.hex(),record_sha256=hashlib.sha256(wire).hexdigest(),
        image_sha256=wire[32:64].hex(),image_bytes=image_bytes,load_address=f'{load:08x}')

def inspect_seed(seed):
    seed=bytes(seed)
    require(len(seed)==SEED_BYTES and seed[:8]==b'KUINVS01' and
            tuple(_word(seed,n) for n in (8,12,20))==(1,SEED_BYTES,RECORD_BYTES) and
            _word(seed,16) in (0,1) and not any(seed[56:64]) and not any(seed[1088:]),
            'Native cache seed header/reserved bytes differ')
    if not _word(seed,16):
        require(not any(seed[24:1088]),'Empty native cache seed contains data')
        return None
    return record_metadata(seed[64:1088],seed[24:56])

def find_seed(module):
    module=bytes(module);found=None;start=0
    while True:
        at=module.find(b'KUINVS01',start)
        if at<0:break
        start=at+1
        try:inspect_seed(module[at:at+SEED_BYTES])
        except (ValueError,struct.error):continue
        require(found is None,'Native cache seed is duplicated or ambiguous')
        found=at
    return found

def fill_seed(seed,wire,trusted_digest):
    inspect_seed(seed)
    require(trusted_digest is not None,'An independently pinned native cache digest is required')
    metadata=record_metadata(wire,trusted_digest)
    result=bytearray(template_seed());struct.pack_into('<I',result,16,1)
    result[24:56]=bytes.fromhex(metadata['record_digest']);result[64:1088]=wire
    require(inspect_seed(result)==metadata,'Native cache seed round trip differs')
    return bytes(result)

# Stable descriptive aliases for the package builder and external checks.
decode_record=record_metadata
decode_seed=inspect_seed
