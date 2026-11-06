#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Closed full General dual selector/binder map from pristine3a6 references.

Ordinary A1/A2 and session A2 relocation-enabled references independently
establish these identical370 instruction bytes and24 exact literal roles.
Only those authored typed/string/blob-label words may move. Compact and old
General profiles keep their separate historical dispatch and pin maps.
"""
import hashlib
import struct

from check_vmu_native_worker_gateway import symbol_bytes
from check_native_session_preclear_profile import symbol_records,role_value

REFERENCE_COMMIT='3a6ae064bb3f456da8d71587a8db0ce974f139cf'
REFERENCES=(
 ('ordinary-a1','63b5dbe9153b7b430675bc9ae5a3ce5c0b9310a00ea50ac26fcf15c8d1b18682',
  'a7201954b68b42b480cdc227a51adfb9f2ebb1839e1e734e2274bb4caf315eec'),
 ('ordinary-a2','9a121c88dc60cf6bd0635bdfef6348f3b29a5b30f0cd350d9d17612a22d8cbea',
  '2fefa6f8ccd2abec4fcf3ca57c3c84605a52c0b2bb1bd7cfa9028db594bf6b56'),
 ('session-a2','b5b94ce07d24d75af7874dbc716d00a88a926c98a4a3928fcb63424d39209e58',
  '8fa3a0239ac5312e0f8944eddabf9dbf3ec57aa7bbcc8480253f301e7bd19f55'),
)
CAPABILITY=struct.pack('<8I',0x3152444b,0x4c415544,1,32,1,2,0x51000,0)
FUNCTIONS={
 '_kui_native_dual_u32':(30,'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c',[]),
 '_native_dual_reader_bind':(68,'9a0d41fcafc70f534fa9a97c9754825f6c14d4d059fafec70630e5cff6135bd9',[
  (52,('symbol','_manifest@vmu_native_heap_probe_stage.c',0,1,8,3,2172,0)),
  (56,('symbol','_native_loader_timeline_bind@vmu_native_heap_probe_stage.c',0,2,1,6,180,0)),
  (60,('symbol','_native_sdk_sci_route_bind@vmu_native_heap_probe_stage.c',0,2,1,6,944,0)),
  (64,('symbol','_native_readahead_bind@vmu_native_heap_probe_stage.c',0,2,1,6,1244,0))]),
 '_select_resident':(272,'e92625f5cb45ae4463cc1bd346a0528c45489e0755d4cedf6881431a0fc2a9fc',[
  (172,('symbol','_kui_native_dual_u32@vmu_native_heap_probe_stage.c',0,2,1,6,30,0)),
  (180,('symbol','_resident_limit@vmu_native_heap_probe_stage.c',0,1,8,3,4,0)),
  (184,('symbol','_kui_native_dual_reader_capability',0,1,1,2,32,0)),
  (192,('symbol','_kui_native_dual_reader_capability',4,1,1,2,32,0)),
  (200,('symbol','_kui_native_dual_reader_capability',8,1,1,2,32,0)),
  (204,('symbol','_kui_native_dual_reader_capability',12,1,1,2,32,0)),
  (208,('symbol','_kui_native_dual_reader_capability',16,1,1,2,32,0)),
  (212,('symbol','_kui_native_dual_reader_capability',20,1,1,2,32,0)),
  (216,('symbol','_kui_native_dual_reader_capability',24,1,1,2,32,0)),
  (224,('symbol','_kui_native_dual_reader_capability',28,1,1,2,32,0)),
  (228,('symbol','_manifest@vmu_native_heap_probe_stage.c',0,1,8,3,2172,0)),
  (232,('label','__retail_resident_sci_blob_start')),
  (236,('label','__retail_resident_sci_blob_end')),
  (240,('symbol','_resident_blob@vmu_native_heap_probe_stage.c',0,1,8,3,4,0)),
  (244,('symbol','_resident_bytes@vmu_native_heap_probe_stage.c',0,1,8,3,4,0)),
  (252,('string','DUAL READER CAPABILITY INVALID\0')),
  (256,('symbol','_stopped@vmu_native_heap_probe_stage.c',0,2,1,6,68,0)),
  (260,('string','DUAL NATIVE READER NEEDS SCI X OR Y\0')),
  (264,('label','__retail_resident_legacy_blob_start')),
  (268,('label','__retail_resident_legacy_blob_end'))]),
}


def require(ok,message):
    if not ok:raise ValueError(message)


def profile(stage):
    from check_native_general_readahead_profile import profile as ordinary
    from check_native_session_readahead_profile import profile as session
    return ordinary(stage) or session(stage)


def pinned(stage):
    require(profile(stage),'Full General dual pins require the exact ordinary/session cold architecture')
    return _bodies(stage)


def _bodies(stage):
    typed=symbol_records(stage);result={}
    for name,(count,expected,roles) in FUNCTIONS.items():
        address,raw,section=symbol_bytes(stage,name,2)
        require(len(raw)==count and section[1]==1 and section[2]&7==6 and
                0x8ce00000<=address<address+count<=0x8cfe0000,
                'Full General dual function owner/type/size changed: '+name)
        normalized=bytearray(raw);seen=set()
        for offset,role in roles:
            require(not(address+offset)&3 and 0<=offset<=count-4 and offset not in seen,
                    'Full General dual authored literal escapes/overlaps its body')
            seen.add(offset);value=struct.unpack_from('<I',raw,offset)[0]
            if role[0]=='label':
                target,size,kind,owner=stage[4][role[1]]
                require(owner<len(stage[3]),'Selected low blob label is unallocated')
                storage=stage[3][owner]
                require(size==kind==0 and storage[1]==1 and storage[2]&7==2 and
                        storage[3]<=target<=storage[3]+storage[5] and value==target,
                        'Selected low blob literal lost its exact immutable boundary')
            else:role_value(stage,typed,role,value)
            normalized[offset:offset+4]=bytes(4)
        require(hashlib.sha256(normalized).hexdigest()==expected,
                'Full General dual instructions/constants changed: '+name)
        result[name]=(address,count,hashlib.sha256(raw).hexdigest())
    _,wire,section=symbol_bytes(stage,'_kui_native_dual_reader_capability',1)
    require(section[1]==1 and section[2]&7==2 and wire==CAPABILITY,
            'Full General dual capability is not its complete112KiB/324KiB descriptor')
    return result


def raw_pins(stage):
    """Adapt only already-proved complete bodies to the historical pin API."""
    return {name:(count,digest) for name,(_,count,digest) in pinned(stage).items()}


def reference(stage,relocations):
    identities=(hashlib.sha256(stage[0]).hexdigest(),hashlib.sha256(relocations[0]).hexdigest())
    names=[name for name,*expected in REFERENCES if tuple(expected)==identities]
    require(len(names)==1,'Full General dual reference differs from its pristine3a6 ELFs')
    loads=lambda image:[(p[2:8],image[0][p[1]:p[1]+p[4]]) for p in image[2] if p[0]==1]
    require(loads(stage)==loads(relocations),'Dual reference --emit-relocs changed PT_LOAD bytes/geometry')
    # Pristine3a6 predates the new explicit high-stage virtual-slot marker.
    # Its exact independently authored ELF identity binds that old reference;
    # current candidates still pass the complete strict profile above.
    _bodies(stage);_bodies(relocations);actual=set()
    for section in relocations[3]:
        if section[1]!=4:continue
        require(section[9]==12,'Dual reference relocation entry width differs')
        for pos in range(section[4],section[4]+section[5],section[9]):
            address,info,_=struct.unpack_from('<IIi',relocations[0],pos)
            for name,(count,_,roles) in FUNCTIONS.items():
                begin=relocations[4][name][0]
                if begin<=address<begin+count:
                    require(info&255==1,'Unexpected relocation type in the full General dual closure')
                    actual.add((name,address-begin))
    expected={(name,offset) for name,(_,_,roles) in FUNCTIONS.items() for offset,_ in roles}
    require(actual==expected,'Full General dual literals differ from the complete reference relocation set')
    return dict(status='PASS',reference_commit=REFERENCE_COMMIT,reference_profile=names[0],
        identical_loads=True,functions=len(FUNCTIONS),exact_named_literals=len(actual))


def byte_mutations(stage):
    pinned(stage);rejected=0
    for name,kind in [(name,2) for name in FUNCTIONS]+[('_kui_native_dual_reader_capability',1)]:
        address,raw,section=symbol_bytes(stage,name,kind);start=section[4]+address-section[3]
        for offset in range(len(raw)):
            changed=bytearray(stage[0]);changed[start+offset]^=1
            try:pinned((bytes(changed),*stage[1:]))
            except ValueError:rejected+=1
            else:raise ValueError('Full General dual accepted a changed instruction/literal/capability byte')
    return rejected
