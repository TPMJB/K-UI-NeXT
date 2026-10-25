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
DETECTOR_EPOCH=3

def require(condition,message):
    if not condition:raise ValueError(message)

def _word(data,offset):return struct.unpack_from('<I',data,offset)[0]

def template_seed():
    return b'KUINVS01'+struct.pack('<4I',1,SEED_BYTES,0,RECORD_BYTES)+bytes(SEED_BYTES-24)

def record_metadata(wire,expected_digest=None):
    wire=bytes(wire)
    require(len(wire)==RECORD_BYTES,'Native cache record must be exactly 1024 bytes')
    require(wire[:8]==b'KUINVR01' and tuple(_word(wire,n) for n in (8,12,16,20))==
            (1,RECORD_BYTES,DETECTOR_EPOCH,1),'Native cache record version/epoch/schema differs')
    require(not any(wire[748:992]),'Native cache record reserved bytes are not zero')
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
    require(tuple(_word(wire,n) for n in (64,68,72,76,80,84))==
            (1,104,1,image_bytes,load,7) and _word(wire,132) in (12,13) and
            wire[136:168]==wire[32:64],'Native cache driver schema differs')
    require(_word(wire,168) in (1,2) and _word(wire,172)==440 and
            (_word(wire,192),_word(wire,404)) in ((1,12),(0,1)),
            'Native cache layout schema differs')
    require(tuple(_word(wire,n) for n in (608,612,616,620,624,628))==
            (1,72,1,1,image_bytes,load),'Native cache allocator schema differs')
    family,count=_word(wire,708),_word(wire,720)
    require(tuple(_word(wire,n) for n in (700,704,712,716))==(1,48,load,image_bytes) and
            ((family in (1,2,3) and count==3) or
             (family==4 and count==2 and not _word(wire,732) and not _word(wire,740))),
            'Native cache exit schema differs')
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
