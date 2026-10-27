#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated Armada CE read-only synthetic blank A2 test.

The fixed bridge and private-stack shim are pinned separately from compiled C.
C instructions, retained static frames, callback closure and every allocated
range are audited; the complete ELF hash binds the resulting package.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from check_armada_ce_maple_hook import BRIDGE,EH,SH,PH,SYM,require
from check_retail_instructions import audit as audit_instructions
from check_retail_stack import load as load_graph,base as base_name,worst as low_stack_worst
from check_armada_ce_excerpt import ARRAY_READ,ARRAY_READ_BYTES
from check_retail_loader_layout import FORBIDDEN_PREFIXES,FORBIDDEN_SYMBOLS
from armada_ce_maple_write_owner_audit import CE_WAIT_ABI_METADATA
import armada_ce_cache_snapshot_contract as snapshot_contract

HOOK=0xACFCA000
P1_HOOK=HOOK-0x20000000
WRITE_HOOK=0xACFC4000
WRITE_LAYOUT='__kui_maple_write_layout'
WRITE_RUNTIME='__kui_maple_write_runtime'
RESERVE_END=0x8CFD0000
LOG=0xACFCFE00
STACK_BOTTOM=0xACFCF800
STACK_TOP=LOG
LOG_BYTES=124
FENCE=0xACFCFE80
FENCE_BYTES=120
A2=0xACFCFF00
A2_BYTES=120
AUTO_BYTES=12
PROOF_COUNTER_BYTES=8
DIAGNOSTIC_MODE='_kui_armada_ce_maple_diagnostic_mode'
DIAGNOSTIC_NAMES={0:'full',1:'gate-only',2:'proof-only'}
AUTO_LEAF='kui_armada_ce_maple_auto_stop'
# mov.l vector-literal,r0; mov.l @r0,r0; mov #1,r4; jmp @r0; nop;
# alignment NOP; literal0xac0000e0. The C caller must prove the installed
# vector's exact expected identity before entering this nonreturning leaf.
AUTO_LEAF_BYTES=bytes.fromhex('02d0026001e42b4009000900e00000ac')
# Filled from the independently authored, assembled and modeled wrapper.
BRIDGE_BYTES=bytes.fromhex(
    '862f962fa62f224f024f124f134f436801e4ddbd090003690dda00e593660b4a8364'
    '9364d4bd09009eb9836401e4cfbd090001e503660b4a83649364c8bd0900174f164f'
    '064f264ff66af6690b00f66800a0fcac0900090009000900')
BRIDGE_HELPER_LITERAL_OFFSET=80
# Every authored instruction and the stack literal are fixed. The final
# four-byte call literal must equal the separately bounded C entry symbol.
SHIM_PREFIX=bytes.fromhex(
    '02033360f0cb0e4008d0f620862022403620036f06d00b400900f660264ff668f261'
    '136f0e400b000900090000fefcac')
SHIM_BYTES=52
RUNTIME_SHIM_BYTES=184
RUNTIME_HANDOFF=0xACFCE200
RUNTIME_HANDOFF_BYTES=1272
RUNTIME_SERVICES=0xACFCE700
RUNTIME_SERVICES_BYTES=32
RUNTIME_STACK_BOTTOM=0xACFCE800
RUNTIME_STACK_TOP=0xACFCF800
# Complete authored service/ordinary-stack-wait/remask/terminal stream. Only
# the bounded C entries and retained bridge-return cell are relocated.
RUNTIME_SHIM=bytes.fromhex(
    '02033360f0cb0e4024d0f62086202240362046206620036ff35126d2122233671fd0'
    '0b40090008202e8901882a8bf2501fd11820268bf3681adf0e4081541ad00b400900'
    '036702033360f0cb0e40836f78270f8b855f82500e40815413d00b40090003670203'
    '3360f0cb0e40836f78270089fee7f15402e5f26609d00b400900d1af0900feaf0900'
    'f250f3512a41f458f551136f0e400b000900090000fefcacdc65fc8c00f8fcac8c8d'
    'fc8c8865fc8cf0000010a0befcac')
RUNTIME_SHIM_RELOCATIONS=((160,'_kui_armada_ce_maple_traffic_run'),
    (168,'_kui_armada_ce_vmu_runtime_work'),(172,'_kui_armada_ce_maple_runtime_wait'),
    (180,'_kui_armada_ce_maple_bridge_return'))
ROOT='kui_armada_ce_maple_traffic_run'
READ_CALLBACKS=('traffic_read_p1','traffic_read_cache','traffic_read_p2')
MMIO_CALLBACKS=('fence_read_mmio','fence_write_mmio')
TABLE_CALLBACKS=('traffic_read_p1','kui_armada_ce_maple_traffic_table','kui_armada_ce_maple_rx_slice_table')
CACHE_CALLBACKS=('kui_armada_ce_maple_traffic_cache','kui_armada_ce_maple_rx_slice_cache')
BACKEND_CALLBACKS=('read_blank','commit_refused')
A2_CALLBACKS=('a2_store32','a2_backend_call','a2_recheck')
CALLBACKS=tuple(dict.fromkeys(READ_CALLBACKS+MMIO_CALLBACKS+TABLE_CALLBACKS+
    CACHE_CALLBACKS+BACKEND_CALLBACKS+A2_CALLBACKS))
ASM_FRAMES={name:0 for name in ('kui_armada_ce_excerpt_array_read',
    AUTO_LEAF,
    '__movmem_i4_even','__movstr_i4_even','__movmem_i4_odd','__movstr_i4_odd',
    '__movmemSI12_i4','__movstrSI12_i4')}
ASM_FRAMES[snapshot_contract.LEAF_SYMBOL[1:]]=snapshot_contract.STACK_BYTES
# This fixed CE-ABI boundary is reachable only on the original CE stack.
# Its24 bytes are PR/GBR saves plus the Microsoft16-byte argument-home area;
# the external native callee's stack is deliberately not inferred from it.
RUNTIME_NATIVE_WAIT='_kui_armada_ce_maple_native_wait'
RUNTIME_NATIVE_WAIT_BYTES=bytes.fromhex('224f134f436053646365f07f0b400900107f174f264f0b000900')
ASM_FRAMES[RUNTIME_NATIVE_WAIT[1:]]=24
# A single compiler copy member is retained for the 12-byte protocol IO
# descriptor copy. The called leaf and every unused co-linked alias are
# integer-only, never touch SP, and stay inside these complete pinned bytes.
COPY_MEMBER_BYTES=bytes.fromhex(
    '04140b001514090056600ca056615661fc745662566311142214331410465660ee8d'
    '5661107456625663022410461114f38f22140b003314090009000900090052605151'
    '5252022411140b002214090009000900090009000900090009000900')
COPY_SPANS={'___movmem_i4_even':(8,48),'___movstr_i4_even':(8,48),
    '___movmem_i4_odd':(14,42),'___movstr_i4_odd':(14,42),
    '___movmemSI12_i4':(64,14),'___movstrSI12_i4':(64,14)}
MMIO_BODIES={
    '_fence_read_mmio':bytes.fromhex('68260d8d00e007d2efe159212031048df7e159210c722031028b526101e012260b000900046c5fa0'),
    '_fence_write_mmio':bytes.fromhex('05d22035058f01e11636028962220b0001e00b0000e00900146c5fa0'),
}

# Complete independently reviewed source-specific callbacks, including
# physical bounds and private witness/state/callee literals.
A2_STORE_BYTES=bytes.fromhex(
    '18d119d259212031288f00e117d318d25c332633228917d322925c3326331d8b15d2'
    '2a500820068d03671a93fc773637148d03c8128b285150310b8d08200d8d00e12952'
    '56320989fc70536128310631048d00e162255261603129010b001360ff5ff803030000'
    'e0000000a00000ff53fcfffe0000600353c4e3fcac')
# The expanded guard has a 0xbfff span, which the compiler loads with MOV.L
# instead of the default profile's signed-word-sized 0x5fff constant. Pin the
# complete independent body, including that instruction and its new literal;
# only its final context address is relocated against the bounded symbol.
A2_WRITE_STORE_BYTES=bytes.fromhex(
    '18d119d259212031288f00e117d318d25c332633228917d317d25c3326331d8b16d2'
    '2a500820068d03671993fc773637148d03c8128b285150310b8d08200d8d00e12952'
    '56320989fc70536128310631048d00e162255261603129010b001360f8030900030000'
    'e0000000a00000ff53fcfffe0000c00353ffbf00008489fcac')
A2_BACKEND_BYTES=bytes.fromhex(
    '09d1126118210d8908d112641034098b07d11e533823058b1f511821028b05d02b40'
    '09000b00ffe0c0e3fcace4e8fcace0eafcac88cdfcac')
# These complete function templates contain four-byte absolute addresses.
# Resolve only the authored literal slots, retaining every instruction and
# non-address constant. In particular, +508 is the native32 ABI location of
# the optional reader in the bounded CE adapter object; the guard must keep
# rejecting that field before entering the backend.
CALLBACK_RELOCATIONS={
    '_a2_store32':(A2_STORE_BYTES,((120,'_a2_context',0),)),
    '_a2_backend_call':(A2_BACKEND_BYTES,((40,'_a2_ready',0),
        (44,'_a2_backend',0),(48,'_a2_backend',508),
        (52,'_kui_armada_ce_vmu_readonly_handle',0))),
}
RUNTIME_BACKEND_BYTES=bytes.fromhex(
    '862f962fa62fb62f224f2bd8f47f522f8751611f721f1821f85a348ff95b27d00b40'
    '00e408203c8d20e625d125d583641261181824d111522918126212512a181b1822d1'
    '0b413874885118212a8dffe01fd120d2126920d199212031228f83651ed026940b40'
    '307508201a898c51182117891bd00b4082540820128b1a911c3901e19d181718f257'
    '00e4f156f26515d0b91fa81f0c7f264ff66bf66af6692b40f668ffe00c7f264ff66b'
    'f66af6690b00f66878014efb0900a4befcac9c61fc8c9458148c94cbfcac98c2fcac'
    '5c47fc8ca0befcacc667dd01ffffff814c54fc8cac8bfc8cd08cfc8c')
RUNTIME_BACKEND_RELOCATIONS=((184,'_a2_continuation',0),(188,'_a2_recheck',0),
    (196,'_traffic_scratch',0),(200,'_a2_context',0),(204,'_a2_copy_words',0),
    (208,'_kui_armada_ce_maple_bridge_return',0),(220,'_a2_extension_word',0),
    (224,'_kui_armada_ce_vmu_runtime_bind',0),(228,'_kui_armada_ce_vmu_runtime_handle',0))
RUNTIME_WAIT_BYTES=bytes.fromhex(
    '862f224f02080fd10fd2892120310389ffe0264f0b00f66812908b200e400bd00b40'
    '09000e480820f28d01e608d009d11c550b401d54049110300a30e9af0760f0000201'
    'f000005000000040000000000000000000000000')
RUNTIME_WAIT_RELOCATIONS=((76,'_kui_armada_ce_maple_runtime_owner',0),
    (80,RUNTIME_NATIVE_WAIT,0),(84,'_a2_continuation',0))
# Complete parked-DMA witnesses. Only bounded private/function/table addresses
# relocate; all state offsets, equality branches and live MMIO reads are fixed.
RUNTIME_HELD_TABLE='_at.6'
RUNTIME_HELD_TABLE_BYTES=bytes.fromhex('146c5fa0186c5fa0046c5fa0106c5fa0')
RUNTIME_HELD_BYTES=bytes.fromhex(
    '862f962fa62fb62f224f20d1126001882e8fec7f115001882a8b1dd3125732522037'
    '258b1bd7135272577032208b1657335330371c8b14500188198b15500188168f00e1'
    '14d915dbf368936a111f121f0478231f107a041f926500e40b4bf3660820058df261'
    '82621032098d0479fc7900e0147f264ff66bf66af6690b00f668a039e98f0478f4af'
    '0900090098cdfcacc4c0fcac30cefcac607afc8c7846fc8c')
RUNTIME_HELD_RELOCATIONS=((140,'_fence_state',0),(144,'_a2_continuation',0),
    (148,'_traffic_scratch',124),(152,RUNTIME_HELD_TABLE,0),(156,'_fence_read_mmio',0))
RUNTIME_OWNER_BYTES=bytes.fromhex(
    '862f962f224f34d887511821068fd47f00e02c7f264ff6690b00f66882611821f78d'
    '00e082514031f38b2cd00b4009000820ed892ad1126288511032e88b29d112628951'
    '1032e38b27d112648a511034de8b26d112628b511032d98ff36924d00c7924d6922f'
    '00e70b408e550820cf8f20e621d0936583640b4038740820c78df3651ed900e40b49'
    '04750820c08df1511821bd8ff3651d940b4908750820b78df2518c521032b38f00e0'
    '0dd1126288511032ae8b0bd1126289511032a98b0ad112628a511032a48b08d18b52'
    '126110329faf290078010900c4c0fcac2049fc8c9458148c9058148c080000ff1c0000ff'
    '6043fc8cbc46fc8c8c47fc8cb054fc8c')
RUNTIME_OWNER_RELOCATIONS=((216,'_a2_continuation',0),(220,'_a2_held',0),
    (240,'_kui_armada_ce_maple_resolve',0),(244,'_traffic_read_p1',0),
    (248,'_a2_same_words',0),(252,'_a2_extension_word',0))
# GCC inlines a2_service. Pin its entire transition (round fuse, increment,
# held check, SERVICE/terminal exits) and the earlier resume-held check in the
# exact emitted dispatcher. These windows contain no POST or native call.
RUNTIME_HELD_RUN_BYTES=0x1378
RUNTIME_HELD_WINDOWS=((0x4c,bytes.fromhex('79d00b40090008201d8f00e5f3af6ce4')),
    (0x122a,bytes.fromhex('47db')),
    (0x126c,bytes.fromhex('b15159922631048f00e50191230166e4c6ed3ad001710b40111b0820048d01e0019e230e09001aee00e50191230167e4a6ed')))
RUNTIME_HELD_RUN_LITERALS=((0x234,'_a2_held'),(0x1348,'_a2_continuation'),(0x1368,'_a2_held'))
RUNTIME_PIO_BUS_BYTES=bytes.fromhex(
    '862f4368224f14d0fc7f0b40522ff26558251e8901caffc81b8f5638168d83625362'
    '23e1883216320c8f01e00bd200e1822500e023150ad21115241509d2121525151615'
    '047f264f0b00f6681be1eaaf5832f7af01e00900909bfc8cbc9cfc8c489dfc8cf49b'
    'fc8c')
# At byte34, mov #35,r1 bounds the diagnostic36-byte PIO state; the reverse
# overlap check remains27 for the unchanged28-byte bus descriptor.
RUNTIME_PIO_BUS_RELOCATIONS=((88,'_armada_pio_valid',0),(92,'_armada_pio_select_card',0),
    (96,'_armada_pio_transfer',0),(100,'_armada_pio_ticks',0))
# The only SD block-writer call clears transfer_block at the audited native
# ABI offset24, passes count1, and calls the separately bounded writer.
RUNTIME_PIO_WRITE_BYTES=bytes.fromhex('02d000e1636716142b4001e688b9fc8c')
RUNTIME_STORAGE_ALLOWED=frozenset(('_kui_loader_sd_read','_kui_sci_sd_sync','_kui_sci_sd_write'))
READONLY_METADATA={
    '_sd_name.3':b'K-UI Read-Only SD\0',
    '_sd_version.1':b'K-UI read-only fixed SD image; no writes or persistence\0',
}


def helper_image(sections):
    """Flat physical image; execution aliases remain distinct ELF sections."""
    return sections.get('.uncached',{}).get('data',b'')+sections['.hook']['data']


def helper_offset(address,hook=HOOK):
    return (address&0x1fffffff)-(hook&0x1fffffff)


def bridge_bytes(write_layout=False):
    body=bytearray(BRIDGE_BYTES)
    if write_layout:
        struct.pack_into('<I',body,BRIDGE_HELPER_LITERAL_OFFSET,WRITE_HOOK)
    return bytes(body)


def select_layout(data,raw,sections,entry):
    """Admit exactly the absent/default or absolute-one expanded marker.

    The entry alone cannot opt into a wider reservation. A typed object,
    section-relative symbol, duplicate marker or any other marker value is
    refused before applying the selected ELF geometry.
    """
    tab=sections.get('.symtab')
    require(tab is not None and tab['entsize']==SYM.size and tab['size']%SYM.size==0,
            'A2 symbol table missing')
    require(0<=tab['link']<len(raw),'A2 symbol string index invalid')
    names=raw[tab['link']]
    require(names[1]==3 and names[4]+names[5]<=len(data),'A2 symbol strings invalid')
    names=data[names[4]:names[4]+names[5]];markers=[]
    for at in range(0,tab['size'],SYM.size):
        name,value,size,info,_,index=SYM.unpack_from(tab['data'],at)
        if not name:continue
        end=names.find(b'\0',name)
        require(0<=name<len(names) and end>=0,'A2 symbol name invalid')
        if names[name:end].decode('ascii')==WRITE_LAYOUT:
            markers.append((value,size,info&15,index))
    require(not markers or markers==[(1,0,0,0xfff1)],
            'A2 expanded layout requires one absolute untyped value-one marker')
    write_layout=bool(markers)
    hook=WRITE_HOOK if write_layout else HOOK
    require(entry==hook,'A2 entry disagrees with its exact reservation marker')
    return write_layout,hook


def exact_absolute_marker(data,raw,sections,name):
    """An opt-in requires one exact untyped ABS-one symbol, never a section."""
    table=sections['.symtab'];names=raw[table['link']]
    names=data[names[4]:names[4]+names[5]];found=[]
    for at in range(0,table['size'],SYM.size):
        start,value,size,info,_,index=SYM.unpack_from(table['data'],at)
        if not start:continue
        end=names.find(b'\0',start)
        require(0<=start<len(names) and end>=0,'A2 marker name invalid')
        if names[start:end].decode('ascii')==name:found.append((value,size,info&15,index))
    require(not found or found==[(1,0,0,0xfff1)],'A2 runtime requires one absolute untyped value-one marker')
    return bool(found)


def helper_region(sections,address,size):
    for label in ('.uncached','.hook'):
        section=sections.get(label)
        if section and section['address']<=address and address+size<=section['address']+section['size']:
            at=address-section['address'];return section['data'][at:at+size]
    raise ValueError('A2 immutable address leaves its exact execution alias')
# Source-function/file-specific publisher callback sets. Unknown dispatch
# sites are rejected; this is populated from the authored final interface.
INDIRECT_A2={
    ('a2_publish_store','armada_ce_vmu_a2_publish.c'):('a2_store32',),
    ('kui_armada_ce_vmu_a2_publish','armada_ce_vmu_a2_publish.c'):A2_CALLBACKS,
}


def read_section_elf(data):
    require(len(data)>=EH.size,'A2 ELF header truncated')
    ident,kind,machine,version,entry,phoff,shoff,_,ehsize,phsize,phnum,shsize,shnum,shstr=EH.unpack_from(data)
    require(ident[:7]==b'\x7fELF\x01\x01\x01' and kind==2 and machine==42 and version==1,
            'A2 requires static little-endian SH ELF')
    require(ehsize==EH.size and phsize==PH.size and phnum in (2,4) and
            shsize==SH.size and 1<=shnum<=32 and shstr<shnum,'A2 ELF geometry changed')
    require(phoff+phnum*PH.size<=len(data) and shoff+shnum*SH.size<=len(data),'A2 ELF tables truncated')
    raw=[SH.unpack_from(data,shoff+i*SH.size) for i in range(shnum)]
    names=raw[shstr]
    require(names[1]==3 and names[4]+names[5]<=len(data),'A2 section names truncated')
    strings=data[names[4]:names[4]+names[5]]
    sections={}
    for section_index,row in enumerate(raw):
        name,typ,flags,address,offset,size,link,info,alignment,entsize=row
        require(name<len(strings),'A2 section name invalid')
        end=strings.find(b'\0',name)
        require(end>=0,'A2 section name unterminated')
        label=strings[name:end].decode('ascii')
        require(label not in sections,'Duplicate A2 section')
        require(typ not in (4,6,9),'A2 relocation/dynamic section forbidden')
        require(typ==8 or offset+size<=len(data),'A2 section leaves ELF')
        sections[label]=dict(type=typ,flags=flags,address=address,size=size,link=link,index=section_index,
            info=info,alignment=alignment,entsize=entsize,offset=offset,
            data=b'' if typ==8 else data[offset:offset+size])
    write_layout,hook=select_layout(data,raw,sections,entry)
    runtime=exact_absolute_marker(data,raw,sections,WRITE_RUNTIME)
    require(not runtime or write_layout,'A2 runtime requires the expanded reservation marker')
    shim_bytes=RUNTIME_SHIM_BYTES if runtime else SHIM_BYTES
    p1_hook=hook-0x20000000
    expected_bridge=bridge_bytes(write_layout)
    allocated={n:s for n,s in sections.items() if s['flags']&2}
    cached='.uncached' in allocated
    require(not runtime or cached,'Writable runtime requires the exact cached/P2 execution layout')
    expected_sections={'.bridge','.hook','.workspace','.stack','.log','.fence','.a2'}
    if runtime:expected_sections.update(('.vmu_handoff','.vmu_services','.worker_stack'))
    if cached:expected_sections.add('.uncached')
    require(set(allocated)==expected_sections,'Unexpected A2 allocated section')
    for label,address,size,typ,flags in [('.bridge',BRIDGE,92,1,6),
            ('.stack',STACK_BOTTOM,STACK_TOP-STACK_BOTTOM,8,3),('.log',LOG,LOG_BYTES,8,3),
            ('.fence',FENCE,FENCE_BYTES,8,3)]:
        s=allocated[label]
        require((s['address'],s['size'],s['type'],s['flags'])==(address,size,typ,flags),label+' layout changed')
    a2=allocated['.a2']
    require((a2['address'],a2['type'],a2['flags'])==(A2,8,3) and
            a2['size'] in (A2_BYTES,A2_BYTES+AUTO_BYTES,A2_BYTES+AUTO_BYTES+PROOF_COUNTER_BYTES),
            'A2 report/auto marker/proof counter layout changed')
    if runtime:
        for label,address,size in (('.vmu_handoff',RUNTIME_HANDOFF,RUNTIME_HANDOFF_BYTES),
                ('.vmu_services',RUNTIME_SERVICES,RUNTIME_SERVICES_BYTES),
                ('.worker_stack',RUNTIME_STACK_BOTTOM,RUNTIME_STACK_TOP-RUNTIME_STACK_BOTTOM)):
            s=allocated[label]
            require((s['address'],s['size'],s['type'],s['flags'])==(address,size,8,3),
                    'Runtime retained section layout changed: '+label)
    code=allocated['.hook'];work=allocated['.workspace']
    helper=dict(data=helper_image(sections),size=len(helper_image(sections)))
    if cached:
        p2=allocated['.uncached']
        require(phnum==4 and p2['address']==hook and p2['type']==1 and p2['flags']==6 and
                p2['size']>=shim_bytes and p2['size']%32==0 and
                code['address']==p1_hook+p2['size'] and code['type']==1 and code['flags']==6 and
                code['size']>0 and code['size']%32==0,
                'A2 cached/P2 immutable section layout changed')
    else:
        require(phnum==2 and code['address']==hook and code['type']==1 and code['flags']==6,
                'A2 uncached helper layout changed')
    workspace_limit=RUNTIME_HANDOFF if runtime else STACK_BOTTOM
    require(shim_bytes<=helper['size']<=workspace_limit-hook,'A2 helper bounds changed')
    require(work['type']==8 and work['flags']==3 and work['size']>0 and work['address']%4==0 and
            work['address']>=hook+helper['size'] and work['address']+work['size']<=workspace_limit,
            'A2 workspace overlaps immutable code or private stack')
    require(allocated['.bridge']['data']==expected_bridge,'Complete A2 CE bridge differs')
    if not runtime:
        require(helper['data'][:len(SHIM_PREFIX)]==SHIM_PREFIX,'Complete A2 private-stack shim differs')
    segments=[PH.unpack_from(data,phoff+i*PH.size) for i in range(phnum)]
    expected_segments=[(1,BRIDGE,BRIDGE,92,92,5)]
    if cached:
        expected_segments.extend([(1,hook,p1_hook,p2['size'],p2['size'],5),
            (1,code['address'],code['address'],code['size'],code['size'],5),
            (1,work['address'],work['address']-0x20000000,0,A2+a2['size']-work['address'],6)])
    else:expected_segments.append((1,hook,hook,helper['size'],A2+a2['size']-hook,7))
    require([(p[0],p[2],p[3],p[4],p[5],p[6]) for p in segments]==expected_segments,
            'A2 load segments changed')
    for p in segments:require(p[1]+p[4]<=len(data),'A2 load segment leaves ELF')
    loaded=b''.join(data[p[1]:p[1]+p[4]] for p in segments[1:] if p[4])
    require(data[segments[0][1]:segments[0][1]+92]==expected_bridge and loaded==helper['data'],
            'A2 segment differs from checked section')
    tab=sections.get('.symtab')
    require(tab is not None and tab['entsize']==SYM.size and tab['size']%SYM.size==0,
            'A2 symbol table missing')
    require(0<=tab['link']<len(raw),'A2 symbol string index invalid')
    names=raw[tab['link']]
    require(names[1]==3 and names[4]+names[5]<=len(data),'A2 symbol strings invalid')
    names=data[names[4]:names[4]+names[5]];symbols={};functions={};objects={}
    for at in range(0,tab['size'],SYM.size):
        name,value,size,info,_,index=SYM.unpack_from(tab['data'],at)
        require(at==0 or index!=0,'Unresolved A2 symbol')
        if not name:continue
        end=names.find(b'\0',name)
        require(0<=name<len(names) and end>=0,'A2 symbol name invalid')
        label=names[name:end].decode('ascii')
        require(label not in symbols or symbols[label]==value,'Ambiguous A2 symbol')
        symbols[label]=value
        if info&15==1:
            objects[label]=(value,size)
            if cached:
                object_sections=(code,work)+tuple(allocated[n] for n in
                    ('.vmu_handoff','.vmu_services') if runtime)
                section=next((s for s in object_sections if index==s['index']),None)
                require(section is not None and size>0 and section['address']<=value and
                        value+size<=section['address']+section['size'],
                        'Cached A2 object leaves immutable P1 data or private P2 workspace: '+label)
        if info&15==2:
            p2_function=label in (ARRAY_READ,snapshot_contract.LEAF_SYMBOL,'_'+AUTO_LEAF)
            section=p2 if cached and p2_function else code
            require(size>0 and index==section['index'] and section['address']<=value and
                    value+size<=section['address']+section['size'],
                    'A2 function outside its exact immutable execution alias: '+label)
            functions[label]=(value,size)
    for name,body in (() if runtime else READONLY_METADATA.items()):
        require(name in objects and objects[name][1]==len(body),
                'Readonly metadata exception is not a bounded constant object: '+name)
        require(helper_region(sections,objects[name][0],len(body))==body,
                'Readonly metadata constant bytes differ: '+name)
    if DIAGNOSTIC_MODE in symbols:
        require(DIAGNOSTIC_MODE in objects and objects[DIAGNOSTIC_MODE][1]==4,
                'A2 diagnostic mode must be one typed four-byte object')
        address=objects[DIAGNOSTIC_MODE][0];at=helper_offset(address,hook)
        require(address%4==0 and SHIM_BYTES<=at and at+4<=helper['size'],
                'A2 diagnostic mode leaves immutable helper data')
        require(not any(address<start+size and start<address+4 for start,size in functions.values()),
                'A2 diagnostic mode overlaps executable function bytes')
        require(struct.unpack_from('<I',helper['data'],at)[0] in DIAGNOSTIC_NAMES,
                'A2 diagnostic mode is outside full/gate/proof controls')
        require(struct.pack('<I',address) in helper['data'],
                'A2 diagnostic mode has no retained absolute reference')
    require(not any((name.startswith(FORBIDDEN_PREFIXES+('_kui_sci_',
                '_kui_retail_sd_','_kui_loader_sd_','_kui_vmu_persist_')) or
                name in FORBIDDEN_SYMBOLS) and name not in READONLY_METADATA and
                not(runtime and name in RUNTIME_STORAGE_ALLOWED) for name in symbols),
            'A2 helper retained an allocator, SDK, CE API or storage dependency')
    expected={'_kui_armada_ce_maple_bridge':BRIDGE,'__kui_maple_bridge_end':BRIDGE+92,
        '__kui_maple_fence_bridge_code_end':BRIDGE+84,
        '_kui_armada_ce_maple_hook':hook,'__kui_maple_hook_binary_end':hook+helper['size'],
        '__kui_maple_workspace_start':work['address'],'__kui_maple_workspace_end':work['address']+work['size'],
        '__kui_maple_stack_bottom':STACK_BOTTOM,'__kui_maple_stack_top':STACK_TOP,
        '_kui_armada_ce_maple_hook_log':LOG,'_kui_armada_ce_maple_traffic_freeze':LOG+120,
        '_kui_armada_ce_maple_fence_log':FENCE,
        '_kui_armada_ce_vmu_a2_log':A2,
        '__kui_maple_set_kmode':0x01DD6374,'__kui_maple_resume':0x01DD5B10}
    require(all(symbols.get(k)==v for k,v in expected.items()),'A2 linked symbol contract changed')
    if runtime:
        require(all(symbols.get(k)==v for k,v in {
            '__kui_vmu_worker_stack_bottom':RUNTIME_STACK_BOTTOM,
            '__kui_vmu_worker_stack_top':RUNTIME_STACK_TOP,
            '_kui_armada_ce_vmu_runtime_handoff':RUNTIME_HANDOFF,
            '_kui_armada_ce_vmu_runtime_services':RUNTIME_SERVICES}.items()),
            'Runtime retained handoff/services/worker symbols changed')
        require(objects.get('_kui_armada_ce_vmu_runtime_handoff')==(RUNTIME_HANDOFF,RUNTIME_HANDOFF_BYTES) and
                objects.get('_kui_armada_ce_vmu_runtime_services')==(RUNTIME_SERVICES,RUNTIME_SERVICES_BYTES),
                'Runtime handoff/services must be exact typed retained objects')
    if cached:
        require(all(symbols.get(k)==v for k,v in {
            '__kui_maple_uncached_end':hook+p2['size'],
            '__kui_maple_cached_start':code['address'],
            '__kui_maple_cached_end':code['address']+code['size']}.items()),
            'A2 explicit cached/P2 linker boundaries changed')
        require(work['address']%32==0 and work['address']==hook+helper['size'],
                'A2 mutable workspace shares an immutable cache line')
        for name,(address,size) in functions.items():
            if code['address']<=address<code['address']+code['size']:
                require(struct.pack('<I',address+0x20000000) not in helper['data'],
                        'Cached A2 helper retains a P2 alias of C code: '+name)
    automatic='_'+AUTO_LEAF in functions
    auto_symbols={'_kui_armada_ce_maple_auto_reason':A2+120,
        '_kui_armada_ce_maple_auto_phase':A2+124,
        '_kui_armada_ce_maple_auto_menu_expected':A2+128}
    metadata=a2['size']>=A2_BYTES+AUTO_BYTES
    if metadata:
        require(all(symbols.get(k)==v for k,v in auto_symbols.items()),
                'A2 automatic marker symbol contract changed')
    else:
        require(not any(k in symbols for k in auto_symbols),
                'A2 automatic markers leave the reserved report section')
    proof_symbols={'_kui_armada_ce_maple_proof_initial':A2+132,
        '_kui_armada_ce_maple_proof_final':A2+136}
    proof_metadata=a2['size']==A2_BYTES+AUTO_BYTES+PROOF_COUNTER_BYTES
    if proof_metadata:
        require(all(symbols.get(k)==v for k,v in proof_symbols.items()),
                'A2 proof counter symbol contract changed')
        require(DIAGNOSTIC_MODE in objects,'A2 matched proof counters lack a diagnostic mode word')
    else:
        require(not any(k in symbols for k in proof_symbols) and DIAGNOSTIC_MODE not in symbols,
                'A2 matched controls lack their exact proof counter reservation')
    require(not automatic or metadata,'A2 automatic terminal leaf lacks its marker reservation')
    if automatic:
        require(functions['_'+AUTO_LEAF][1]==len(AUTO_LEAF_BYTES),
                'A2 automatic terminal leaf size changed')
        require(helper_region(sections,functions['_'+AUTO_LEAF][0],len(AUTO_LEAF_BYTES))==AUTO_LEAF_BYTES,
                'Complete A2 automatic terminal leaf differs')
    copy_start=symbols.get('___movmemSI12_i4',0)-64
    require(copy_start>=(code['address'] if cached else hook+SHIM_BYTES) and
            copy_start+len(COPY_MEMBER_BYTES)==code['address']+code['size'],
            'A2 compiler copy member bounds changed')
    require(helper_region(sections,copy_start,len(COPY_MEMBER_BYTES))==COPY_MEMBER_BYTES,
            'Complete zero-stack compiler copy member differs')
    require(all(functions.get(name)==(copy_start+offset,size)
                for name,(offset,size) in COPY_SPANS.items()),
            'A2 compiler copy member symbol spans changed')
    require('_kui_armada_ce_vmu_readonly_init_reader' not in symbols and
            '_kui_vmu_readonly_init_reader' not in symbols,
            'A2 production helper retained a fixed-image reader initializer')
    require('_'+ROOT in functions,'A2 native C entry has no bounded function span')
    require(ARRAY_READ in functions and functions[ARRAY_READ][1]==len(ARRAY_READ_BYTES),
            'A2 cache-array leaf size changed')
    require(helper_region(sections,functions[ARRAY_READ][0],len(ARRAY_READ_BYTES))==ARRAY_READ_BYTES,
            'Complete A2 cache-array leaf differs')
    # Complete MMIO callbacks include their physical-register literals. The
    # only device store is MDEN with an admitted zero/one value.
    for name,body in MMIO_BODIES.items():
        require(name in functions and functions[name][1]==len(body),
                'A2 MMIO callback function span changed: '+name)
        require(helper_region(sections,functions[name][0],len(body))==body,
                'Complete A2 MMIO callback differs: '+name)
    for name,size in ((('_a2_context',44),) if runtime else
            (('_a2_context',44),('_a2_ready',4),('_a2_backend',596))):
        require(name in objects and objects[name][1]==size and
                work['address']<=objects[name][0] and
                objects[name][0]+size<=work['address']+work['size'],
                'Pinned A2 callback object layout changed: '+name)
    native_snapshot=snapshot_contract.LEAF_SYMBOL in functions
    if native_snapshot:
        require(snapshot_contract.JOB_SYMBOL in objects and
                objects[snapshot_contract.JOB_SYMBOL][1]==snapshot_contract.JOB_BYTES,
                'Native P2 snapshot job is not a typed bounded object')
        job=objects[snapshot_contract.JOB_SYMBOL][0]
        require(work['address']<=job and job+snapshot_contract.JOB_BYTES<=work['address']+work['size'],
                'Native P2 snapshot job leaves the private workspace')
        start,size=functions[snapshot_contract.LEAF_SYMBOL]
        snapshot_contract.check(helper_region(sections,start,size),job,write_layout=write_layout)
    else:
        require(snapshot_contract.JOB_SYMBOL not in symbols,
                'Private snapshot job exists without its pinned native leaf')
    for name,(template,relocations) in CALLBACK_RELOCATIONS.items():
        if runtime and name=='_a2_backend_call':
            template=RUNTIME_BACKEND_BYTES;relocations=RUNTIME_BACKEND_RELOCATIONS
        if write_layout and name=='_a2_store32':
            template=A2_WRITE_STORE_BYTES
            relocations=((124,'_a2_context',0),)
        require(name in functions and functions[name][1]==len(template),
                'Pinned A2 callback function span changed: '+name)
        body=bytearray(template)
        for offset,target,addend in relocations:
            require(target in symbols,'Pinned A2 callback target missing: '+target)
            struct.pack_into('<I',body,offset,symbols[target]+addend)
        require(helper_region(sections,functions[name][0],len(body))==body,
                'Complete relocation-resolved A2 callback differs: '+name)
    if cached:
        array=hook+shim_bytes+(len(AUTO_LEAF_BYTES) if automatic else 0)
        leaf=array+len(ARRAY_READ_BYTES)
        end=leaf+len(snapshot_contract.BODY)
        require(native_snapshot and functions[ARRAY_READ][0]==array and
                functions[snapshot_contract.LEAF_SYMBOL][0]==leaf and
                p2['size']==((end-hook+31)&~31) and
                p2['data'][end-hook:]==bytes(p2['size']-(end-hook)),
                'Cached A2 P2 island is not exactly shim/terminal/array/snapshot and zero padding')
        for caller,target in (('_traffic_read_cache',ARRAY_READ),
                ('_kui_armada_ce_cache_line_native',snapshot_contract.LEAF_SYMBOL)):
            require(struct.pack('<I',symbols[target]) in helper_region(sections,*functions[caller]),
                    'Cached A2 wrapper has no exact P2 leaf target: '+caller)
    require(symbols.get('__kui_maple_traffic_shim_end')==hook+shim_bytes,
            'A2 shim size differs from its exact profile')
    if runtime:
        body=bytearray(RUNTIME_SHIM)
        for at,target in RUNTIME_SHIM_RELOCATIONS:
            require(target in functions or target=='_kui_armada_ce_maple_bridge_return' and
                    target in objects and objects[target][1]==4,
                    'Runtime shim target lacks a bounded function or retained cell: '+target)
            struct.pack_into('<I',body,at,symbols[target])
        require(helper['data'][:shim_bytes]==body,'Complete runtime service/remask/terminal shim differs')
        require('_a2_backend' not in symbols and '_kui_armada_ce_vmu_readonly_handle' not in symbols,
                'Runtime helper retained the readonly blank backend')
        for name in ('_kui_armada_ce_vmu_runtime_bind','_kui_armada_ce_vmu_runtime_handle',
                '_kui_armada_ce_vmu_runtime_work','_kui_armada_ce_vmu_sd_init',
                '_kui_armada_ce_vmu_sd_read','_kui_armada_ce_vmu_sd_commit',
                '_kui_armada_ce_maple_runtime_owner'):
            require(name in functions,'Runtime capability lacks a bounded native entry: '+name)
        name='_armada_sd_pio_write'
        require(name in functions and functions[name][1]==len(RUNTIME_PIO_WRITE_BYTES),
                'Runtime sole PIO writer wrapper span changed')
        body=bytearray(RUNTIME_PIO_WRITE_BYTES)
        struct.pack_into('<I',body,12,symbols['_kui_sci_sd_write'])
        require(helper_region(sections,*functions[name])==body,
                'Runtime sole writer must clear block callback and write exactly one PIO sector')
        for name,template,relocations in (
                ('_a2_held',RUNTIME_HELD_BYTES,RUNTIME_HELD_RELOCATIONS),
                ('_kui_armada_ce_maple_runtime_owner',RUNTIME_OWNER_BYTES,RUNTIME_OWNER_RELOCATIONS),
                (RUNTIME_NATIVE_WAIT,RUNTIME_NATIVE_WAIT_BYTES,()),
                ('_kui_armada_ce_maple_runtime_wait',RUNTIME_WAIT_BYTES,RUNTIME_WAIT_RELOCATIONS),
                ('_kui_armada_ce_vmu_sd_pio_bus',RUNTIME_PIO_BUS_BYTES,RUNTIME_PIO_BUS_RELOCATIONS)):
            require(name in functions and functions[name][1]==len(template),
                    'Runtime fixed wait/PIO constructor span changed: '+name)
            body=bytearray(template)
            for offset,target,addend in relocations:
                require(target in symbols,'Runtime fixed call target missing: '+target)
                struct.pack_into('<I',body,offset,symbols[target]+addend)
            require(helper_region(sections,*functions[name])==body,
                    'Complete runtime wait/PIO constructor differs: '+name)
        require(objects.get(RUNTIME_HELD_TABLE)==(symbols[RUNTIME_HELD_TABLE],len(RUNTIME_HELD_TABLE_BYTES)) and
                helper_region(sections,symbols[RUNTIME_HELD_TABLE],len(RUNTIME_HELD_TABLE_BYTES))==RUNTIME_HELD_TABLE_BYTES,
                'Runtime held guard MMIO table changed')
        run=functions['_'+ROOT]
        require(run[1]==RUNTIME_HELD_RUN_BYTES,'Runtime parked dispatcher span changed')
        for offset,body in RUNTIME_HELD_WINDOWS:
            require(helper_region(sections,run[0]+offset,len(body))==body,
                    'Runtime parked service/resume transition changed')
        for offset,target in RUNTIME_HELD_RUN_LITERALS:
            require(helper_region(sections,run[0]+offset,4)==struct.pack('<I',symbols[target]),
                    'Runtime parked service/resume target changed')
    else:
        require(RUNTIME_NATIVE_WAIT not in symbols,'Readonly helper retained a native CE wait capability')
        require(helper['data'][len(SHIM_PREFIX):SHIM_BYTES]==struct.pack('<I',symbols['_'+ROOT]),
                'A2 shim does not call the bounded C root')
    return sections,symbols,functions


def stack_audit(directory,functions,cached=False):
    patterns=('traffic*.ci','fence*.ci','a2*.ci','readonly*.ci','vmu*.ci',
              'armada_ce_vmu_readonly.ci')
    reports=sorted({report for pattern in patterns for report in directory.glob(pattern)})
    require(bool(reports),'A2 C call graph missing')
    # Keep source-qualified locals distinct across separately compiled units.
    # A coincidentally equal local name cannot supply another unit's frame.
    frames,calls=load_graph(reports,qualified=True)
    by_base={}
    for name in frames:by_base.setdefault(base_name(name),[]).append(name)
    def resolve(name):
        if name in ASM_FRAMES:return name
        if name in frames:return name
        matches=by_base.get(base_name(name),[])
        require(len(matches)==1,'Missing or ambiguous A2 static frame: '+name)
        return matches[0]
    root=resolve(ROOT)
    retained={base_name(f[1:]) for f in functions if f[1:] not in ASM_FRAMES}
    require(all(name in by_base for name in retained),'Retained A2 C function lacks a static frame')
    callbacks={name:resolve(name) for name in CALLBACKS}
    require(len(set(callbacks.values()))==len(callbacks),'A2 callbacks are not distinct')
    # The complete pinned adapter rejects a non-NULL optional reader before
    # invoking the CE backend. The native blank transport never installs one.
    # No other caller may bypass that guard into CE packet handling.
    backend_callers={base_name(name) for name,edges in calls.items()
        if any(base_name(target)=='kui_armada_ce_vmu_readonly_handle' for target,_ in edges)}
    require(backend_callers=={'a2_backend_call'},'A2 backend is reachable outside the guarded adapter')
    visiting=set();seen=set();indirect_sites=set();null_reader_sites=set();memo={};best_next={}
    def depth(name):
        name=resolve(name)
        if name in ASM_FRAMES:
            seen.add(name);return ASM_FRAMES[name]
        require(name not in visiting,'A2 recursion is forbidden: '+name)
        if name in memo:return memo[name]
        visiting.add(name);seen.add(name);children=[]
        for target,label in calls.get(name,[]):
            if target!='__indirect_call':
                children.append((depth(target),resolve(target)));continue
            function=base_name(name);source=label.split(':',1)[0]
            if function=='read_blank' and source=='vmu_readonly.c':
                # A source-reviewed, complete-byte pinned guard proves this
                # optional fixed-reader dispatch is unreachable here.
                null_reader_sites.add((name,label));continue
            if source=='armada_ce_maple_mapping.h':selected=TABLE_CALLBACKS
            elif source in ('armada_ce_cache_word.h','armada_ce_cache_line.h'):
                require(not cached,'Cached A2 code retained a callback-based cache-array window')
                selected=CACHE_CALLBACKS
            elif source in ('armada_ce_maple_rx_slice.h','armada_ce_maple_traffic.h'):
                selected=READ_CALLBACKS
            elif source=='armada_ce_maple_fence.h':selected=MMIO_CALLBACKS
            elif function=='kui_vmu_emulation_handle' and source=='vmu_emulation.c':
                selected=BACKEND_CALLBACKS
            else:
                # New publisher dispatch sites are pinned once the separately
                # authored native unit is ready. Unknown sites fail closed.
                selected=INDIRECT_A2.get((function,source))
                require(selected is not None,'Unexpected indirect A2 call: '+name+' at '+label)
            indirect_sites.add((name,label,tuple(selected)))
            children.extend((depth(callbacks[callback]),callbacks[callback]) for callback in selected)
        visiting.remove(name)
        best=max(children,key=lambda entry:entry[0],default=(0,None))
        memo[name]=frames[name]+best[0];best_next[name]=best[1]
        return memo[name]
    worst=depth(root);path=[];at=root
    while at is not None:
        path.append(dict(function=at,frame_bytes=frames.get(at,ASM_FRAMES.get(at))))
        at=best_next.get(at)
    require(worst+16+64<=STACK_TOP-STACK_BOTTOM,'A2 private stack lacks assembly/margin budget')
    return dict(c_frame_bytes=worst,shim_frame_bytes=16,margin_bytes=64,
        available_bytes=STACK_TOP-STACK_BOTTOM,static_call_graph=True,
        source_qualified_locals=True,worst_path=path,indirect_callbacks=list(CALLBACKS),
        indirect_sites=[dict(function=name,site=label,callbacks=list(selected))
            for name,label,selected in sorted(indirect_sites)],
        null_optional_reader_branch_guarded=True,
        null_optional_reader_sites=[dict(function=name,site=label) for name,label in sorted(null_reader_sites)],
        retained_frame_count=len(seen))


def check_runtime_held_calls(frames,calls):
    """Bind the parked guard's emitted call sites to this exact traffic TU."""
    def node(name):
        matches=[n for n in frames if base_name(n)==name]
        require(len(matches)==1,'Missing or ambiguous parked-DMA frame: '+name)
        return matches[0]
    held=node('a2_held');owner=node('kui_armada_ce_maple_runtime_owner');run=node(ROOT)
    require(held=='src/loader/armada_ce_maple_traffic.c:a2_held',
            'Parked-DMA guard has the wrong source owner')
    require(not any(base_name(n)=='a2_service' for n in frames),
            'Parked-DMA service no longer matches its pinned inlined dispatcher')
    children={base_name(target) for target,_ in calls.get(held,[])}
    require(children=={'fence_read_mmio'},'Parked-DMA guard is not read-only MMIO')
    require(any(base_name(t)=='a2_held' for t,_ in calls.get(owner,[])),
            'Runtime worker owner omits its parked-DMA guard')
    sites=[label for target,label in calls.get(run,[]) if base_name(target)=='a2_held']
    require(len(sites)==2 and len(set(sites))==2,
            'Runtime dispatcher must retain separate resume and inlined-service held checks')
    return dict(guard_frame_bytes=frames[held],guard_read_only_mmio=True,
        guard_complete_bytes_pinned=True,guard_table_complete_bytes_pinned=True,
        owner_complete_bytes_pinned=True,service_inlined=True,
        service_and_resume_windows_complete_bytes_pinned=True,
        dispatcher_held_check_sites=sorted(sites),service_transition_has_no_post_call=True,
        dma_fence_retained_across_service_and_native_wait=True,
        scope='Compiled guard and transition admission; hardware behavior remains unobserved')


def runtime_stack_audit(directory,functions):
    """Separate proof and unmasked-service roots with fixed callback sets.

    The service may call exactly the two admitted low-resident loan functions;
    their emitted graph is measured separately, not assigned an arbitrary
    frame allowance. No provider or SD callback is reachable from the proof
    root. PIO's NULL block callback is established by the pinned constructor
    and the sole writer's unconditional clear.
    """
    reports=sorted({p for pattern in ('traffic*.ci','a2*.ci','vmu_emulation.ci')
        for p in directory.glob(pattern)}|set((directory/'runtime').rglob('*.ci')))
    require(bool(reports),'Runtime C call graph missing')
    require(all(p.read_text().splitlines()[-1:]==['}'] for p in reports),
            'Runtime C call graph is truncated')
    frames,calls=load_graph(reports,qualified=True)
    held=check_runtime_held_calls(frames,calls)
    low_reports=sorted((directory/'scia/lto').glob('*.ci'))
    require(bool(low_reports),'Runtime low-resident call graph missing')
    low_frames,low_calls=load_graph(low_reports+[directory/'bind-probe.ci'])
    low_frames[ARRAY_READ[1:]]=0  # Complete controller leaf pinned separately.
    loans=low_stack_worst(low_frames,low_calls,ce=True,armada_bind=True,
        armada_maple=True,armada_maple_proof=True,roots_override={
            'kui_retail_async_vmu_loan_begin':0,'kui_retail_async_vmu_loan_end':0})
    irq=low_stack_worst(low_frames,low_calls,ce=True,armada_bind=True,
        armada_maple=True,armada_maple_proof=True,roots_override={'kui_retail_ce_irq':28})
    external={name:entry['bytes'] for name,entry in loans.items()}
    by_base={}
    for name in frames:by_base.setdefault(base_name(name),[]).append(name)
    def resolve(name):
        if name in ASM_FRAMES or name in external:return name
        if name in frames:return name
        matches=by_base.get(base_name(name),[])
        exact=[n for n in matches if n.rsplit(':',1)[-1]==name]
        if exact:matches=exact
        require(len(matches)==1,'Missing or ambiguous runtime static frame: '+name)
        return matches[0]
    for function in functions:
        require(function[1:] in ASM_FRAMES or base_name(function[1:]) in by_base,
                'Retained runtime C function lacks a static frame: '+function)
    guard=('runtime_guard_enter','runtime_guard_leave','runtime_worker_ready')
    pio=('armada_pio_transfer','armada_pio_select_card','armada_pio_ticks')
    sector=('armada_sd_sector_read','armada_sd_sector_write','armada_sd_sector_sync')
    selected={
        ('a2_publish_store','armada_ce_vmu_a2_publish.c'):('a2_store32',),
        ('kui_armada_ce_vmu_a2_publish','armada_ce_vmu_a2_publish.c'):A2_CALLBACKS,
        ('kui_vmu_emulation_handle','vmu_emulation.c'):('ce_write_read','ce_write_commit'),
        ('ce_write_read','armada_ce_vmu_write.c'):('kui_vmu_deferred_io_read',),
        ('ce_write_commit','armada_ce_vmu_write.c'):('kui_vmu_deferred_io_commit',),
        ('request','vmu_deferred_io.c'):guard[:2],
        ('kui_vmu_deferred_io_poison','vmu_deferred_io.c'):guard[:2],
        ('kui_vmu_deferred_io_work','vmu_deferred_io.c'):guard+
            ('kui_armada_ce_vmu_sd_read','kui_armada_ce_vmu_sd_commit'),
        ('write_sync','vmu_store.c'):sector[1:],
        ('kui_vmu_store_open','vmu_store.c'):sector,
        ('kui_vmu_store_read','vmu_store.c'):sector[:1],
        ('armada_sd_healthy','armada_ce_vmu_sd.c'):guard[2:],
        ('armada_sd_wire','armada_ce_vmu_sd.c'):pio[:1],
        ('armada_sd_wait_byte','armada_ce_vmu_sd.c'):pio[2:],
        ('armada_sd_read_register','armada_ce_vmu_sd.c'):pio[1:2],
        ('armada_sd_execute','armada_ce_vmu_sd.c'):guard+tuple(external),
        ('transfer','sd_reader.c'):pio[:1],('ticks','sd_reader.c'):pio[2:],
        ('release','sd_reader.c'):pio[1:2],('kui_loader_sd_read','sd_reader.c'):pio[1:2],
        ('sci_storage_byte','sci_sd_storage.c'):pio[:1],
        ('sci_storage_ready','sci_sd_storage.c'):pio[2:],
        ('sci_storage_release','sci_sd_storage.c'):pio[1:2],
        ('kui_sci_sd_sync','sci_sd_storage.c'):pio[1:2],
        ('kui_sci_sd_write','sci_sd_storage.c'):pio[1:2],
    }
    visiting=set();memo={};best_next={};sites=set();null_sites=set();seen=set()
    def depth(name):
        name=resolve(name)
        if name in ASM_FRAMES:return ASM_FRAMES[name]
        if name in external:return external[name]
        require(name not in visiting,'Runtime recursion is forbidden: '+name)
        if name in memo:return memo[name]
        visiting.add(name);seen.add(name);children=[]
        for target,label in calls.get(name,[]):
            if target!='__indirect_call':children.append((depth(target),resolve(target)));continue
            function=base_name(name);source=label.split(':',1)[0]
            if ((function,source)==('read_payload','sd_reader.c') or
                (function,source)==('kui_sci_sd_write','sci_sd_storage.c') and label=='sci_sd_storage.c:83:17'):
                null_sites.add((name,label));continue
            if source=='armada_ce_maple_mapping.h':callbacks=TABLE_CALLBACKS
            elif source in ('armada_ce_maple_rx_slice.h','armada_ce_maple_traffic.h'):callbacks=READ_CALLBACKS
            elif source=='armada_ce_maple_fence.h':callbacks=MMIO_CALLBACKS
            else:callbacks=selected.get((function,source))
            require(callbacks is not None,'Unexpected runtime indirect call: '+name+' at '+label)
            sites.add((name,label,tuple(callbacks)))
            children.extend((depth(callback),resolve(callback)) for callback in callbacks)
        visiting.remove(name)
        best=max(children,key=lambda p:p[0],default=(0,None))
        memo[name]=frames[name]+best[0];best_next[name]=best[1];return memo[name]
    def path(root):
        answer=[];at=resolve(root)
        while at is not None:
            answer.append(dict(function=at,frame_bytes=frames.get(at,ASM_FRAMES.get(at,external.get(at)))))
            at=best_next.get(at)
        return answer
    proof=depth(ROOT);proof_path=path(ROOT);proof_seen=set(seen)
    no_io_initializers={'kui_armada_ce_vmu_sd_init','kui_armada_ce_vmu_sd_pio_init',
        'armada_sd_span','armada_sd_private','armada_sd_overlaps','armada_sd_zero'}
    physical_proof={base_name(n) for n in proof_seen if base_name(n).startswith(
        ('armada_sd_','armada_pio_','kui_loader_sd_','kui_sci_sd_','kui_armada_ce_vmu_sd_')) and
        base_name(n) not in no_io_initializers}
    require(not physical_proof,'Runtime proof root reaches physical storage: '+','.join(sorted(physical_proof)))
    service=depth('kui_armada_ce_vmu_runtime_work')
    private_seen=set(seen)
    require(not {'kui_armada_ce_maple_runtime_wait',RUNTIME_NATIVE_WAIT[1:]}.intersection(
        base_name(n) for n in private_seen),'Runtime native CE wait is reachable on a private stack')
    irq_bytes=irq['kui_retail_ce_irq']['bytes']
    require(proof+24+64<=STACK_TOP-STACK_BOTTOM,'Runtime proof stack exceeds its fixed budget')
    require(service+irq_bytes+64<=RUNTIME_STACK_TOP-RUNTIME_STACK_BOTTOM,
            'Runtime service stack exceeds provider/IRQ/margin budget')
    ordinary_wait=depth('kui_armada_ce_maple_runtime_wait')
    return dict(c_frame_bytes=proof,shim_frame_bytes=24,margin_bytes=64,
        available_bytes=STACK_TOP-STACK_BOTTOM,static_call_graph=True,
        source_qualified_locals=True,worst_path=proof_path,
        parked_dma_contract=held,
        physical_storage_unreachable_from_masked_proof=True,
        unmasked_service=dict(c_frame_bytes=service,irq_nested_bytes=irq_bytes,margin_bytes=64,
            available_bytes=RUNTIME_STACK_TOP-RUNTIME_STACK_BOTTOM,
            worst_path=path('kui_armada_ce_vmu_runtime_work'),external_loan_paths=loans,
            irq_path=irq['kui_retail_ce_irq']),
        original_ce_stack_wait=dict(local_call_graph_bytes=ordinary_wait,
            worst_local_path=path('kui_armada_ce_maple_runtime_wait'),
            native_adapter_frame_bytes=ASM_FRAMES[RUNTIME_NATIVE_WAIT[1:]],
            native_caller_home_bytes=16,native_caller_home_offsets=[0,4,8,12],
            native_adapter_preserves=['pr','gbr'],native_adapter_complete_bytes_pinned=True,
            native_target='source-specific COREDLL WaitForSingleObject via the admitted MAPLE import',
            reviewed_native_source=dict(CE_WAIT_ABI_METADATA),
            external_native_callee_depth_included=False,private_stack_budget_includes_wait=False,
            scope='Local compiler frames and fixed ABI adapter only; no total external CE stack bound claimed'),
        indirect_sites=[dict(function=n,site=l,callbacks=list(c)) for n,l,c in sorted(sites)],
        null_block_callback_sites=[dict(function=n,site=l) for n,l in sorted(null_sites)],
        retained_frame_count=len(seen))


def check(directory):
    directory=Path(directory);elf=(directory/'hook.elf').read_bytes()
    sections,symbols,functions=read_section_elf(elf)
    write_layout=WRITE_LAYOUT in symbols
    runtime=WRITE_RUNTIME in symbols
    hook=WRITE_HOOK if write_layout else HOOK
    expected_bridge=bridge_bytes(write_layout)
    require((directory/'bridge.bin').read_bytes()==expected_bridge,'A2 bridge binary differs from ELF')
    image=helper_image(sections);cached='.uncached' in sections
    require((directory/'hook.bin').read_bytes()==image,'A2 helper binary differs from ELF')
    dis=subprocess.check_output(['sh-elf-objdump','-d',str(directory/'hook.elf')],text=True)
    # Function-size spans omit C constants concatenated into the .hook section.
    spans=[(start,snapshot_contract.INSTRUCTION_BYTES if name==snapshot_contract.LEAF_SYMBOL else size)
        for name,(start,size) in functions.items()]+[(hook,RUNTIME_SHIM_BYTES if runtime else SHIM_BYTES),
        (symbols['___movmemSI12_i4']-64,len(COPY_MEMBER_BYTES))]
    filtered=[]
    for line in dis.splitlines():
        found=re.match(r'^\s*([0-9a-f]+):',line)
        if found and any(a<=int(found.group(1),16)<a+n for a,n in spans):filtered.append(line)
    instructions=audit_instructions('a2-helper','\n'.join(filtered))
    private_stack=runtime_stack_audit(directory,functions) if runtime else stack_audit(directory,functions,cached=cached)
    automatic='_'+AUTO_LEAF in functions
    mode_offset=helper_offset(symbols[DIAGNOSTIC_MODE],hook) if DIAGNOSTIC_MODE in symbols else None
    mode=struct.unpack_from('<I',image,mode_offset)[0] if mode_offset is not None else 0
    full=mode==0
    snapshot=None
    if snapshot_contract.LEAF_SYMBOL in functions:
        start,size=functions[snapshot_contract.LEAF_SYMBOL]
        snapshot=snapshot_contract.check(helper_region(sections,start,size),
            symbols[snapshot_contract.JOB_SYMBOL],write_layout=write_layout)
    report=dict(bridge_nominal_va=hex(BRIDGE),bridge_rva='0x67a4',bridge_bytes=92,
        bridge_sha256=hashlib.sha256(expected_bridge).hexdigest(),
        helper_p2_entry=hex(hook),helper_bytes=len(image),
        helper_sha256=hashlib.sha256(image).hexdigest(),
        helper_elf_sha256=hashlib.sha256(elf).hexdigest(),
        cached_c_execution=cached,
        cached_code_p1_range=[hex(sections['.hook']['address']),
            hex(sections['.hook']['address']+sections['.hook']['size'])] if cached else None,
        uncached_island_bytes=sections['.uncached']['size'] if cached else None,
        mutable_state_and_private_stack_p2=True,
        callback_based_cache_array_windows_forbidden=cached,
        log_p2=hex(LOG),report_bytes=120,freeze_word_p2=hex(LOG+120),
        reserved_range_p1=[hex(hook-0x20000000),hex(RESERVE_END)],
        workspace_bytes=sections['.workspace']['size'],private_stack=private_stack,
        bridge_and_shim_pinned=True,cache_array_leaf_pinned=True,mmio_callbacks_complete_bytes_pinned=True,
        cache_array_leaf_p2=hex(symbols[ARRAY_READ]),cache_array_leaf_stack_bytes=0,
        cache_array_post_read_nops=8,complete_compiled_c_bytes_pinned=False,
        compiler_copy_member_complete_bytes_pinned=True,compiler_copy_member_bytes=len(COPY_MEMBER_BYTES),
        compiler_copy_leaf_stack_bytes=0,compiler_copy_called_leaf='__movmemSI12_i4',
        unresolved_symbols=0,integer_instructions=instructions,
        ce_bridge_owned_stack_bytes=28,ce_bridge_and_setkmode_stack_bytes=48,
        fence_report_p2=hex(FENCE),fence_report_bytes=FENCE_BYTES,
        a2_report_p2=hex(A2),a2_report_bytes=A2_BYTES,a2_rx_store_complete_bytes_pinned=True,a2_backend_null_reader_guard_complete_bytes_pinned=True,
        pinned_callback_address_literals_resolved_against_bounded_symbols=True,
        automatic_terminal_capture=automatic,
        automatic_terminal_leaf_complete_bytes_pinned=automatic,
        automatic_metadata_bytes=sections['.a2']['size']-A2_BYTES,
        diagnostic_mode=mode,diagnostic_mode_name=DIAGNOSTIC_NAMES[mode],
        diagnostic_mode_word_helper_offset=mode_offset,
        diagnostic_mode_word_p2=hex(symbols[DIAGNOSTIC_MODE]) if mode_offset is not None and not cached else None,
        diagnostic_mode_word_p1=hex(symbols[DIAGNOSTIC_MODE]) if mode_offset is not None and cached else None,
        diagnostic_mode_word_bytes=4 if mode_offset is not None else 0,
        proof_initial_counter_p2=hex(symbols['_kui_armada_ce_maple_proof_initial']) if mode_offset is not None else None,
        proof_final_counter_p2=hex(symbols['_kui_armada_ce_maple_proof_final']) if mode_offset is not None else None,
        native_bulk_snapshot=snapshot,
        original_completion_called_on_ce_stack_and_previous_mode=True,
        worker_callsite_patched_before_nk='0x5db0: NOP; BSR0x67a4; original delay slot retained',
        original_completion_prologue_unchanged=True,
        mmio_write_domain='MDEN only: disable and restore original enabled state',
        capture_limited_per_visit=True,capture_active_after_first32_visits=True,
        virtual_vmu_enabled=full,virtual_slot='A2',synthetic_blank_card=full,
        read_only_backend=True,maple_register_gate_enabled=True,sd_io=False,save_data_written=False,
        guest_response_buffers_written=full,guest_tx_written=False,
        dma_payload_read=True,a2_hardware_test_pending=True,
        guest_reads='guarded RAM mappings, coherent cache-array extension words and bounded TX/RX request/response slices',
        mutable_write_domain='owned private workspace/stack/reports, bounded CE wrapper frame, guarded MDEN gate'+
            (' and admitted live A2 RX response slices only' if full else ' only; synthetic A2 publication disabled'),
        caveat='Source-specific calling convention, admission and pre-NK reservation audit remain required')
    if write_layout:
        report.update(write_layout=True,write_layout_marker=WRITE_LAYOUT,
            reserved_bytes=RESERVE_END-(hook-0x20000000),
            write_commands_enabled=False,
            write_layout_scope='Expanded RAM layout only; existing read-only backend and request policy remain')
    if runtime:
        report.update(write_runtime=True,write_runtime_marker=WRITE_RUNTIME,
            runtime_shim_bytes=RUNTIME_SHIM_BYTES,write_commands_enabled=full,
            read_only_backend=False,synthetic_blank_card=False,sd_io=full,
            physical_sd_writer_available=full,save_data_written=False,
            runtime_handoff_p2=hex(RUNTIME_HANDOFF),runtime_handoff_bytes=RUNTIME_HANDOFF_BYTES,
            runtime_services_p2=hex(RUNTIME_SERVICES),runtime_services_bytes=RUNTIME_SERVICES_BYTES,
            runtime_worker_stack_p2=[hex(RUNTIME_STACK_BOTTOM),hex(RUNTIME_STACK_TOP)],
            workspace_headroom_bytes=RUNTIME_HANDOFF-sections['.workspace']['address']-sections['.workspace']['size'],
            a2_backend_null_reader_guard_complete_bytes_pinned=False,
            runtime_backend_binding_complete_bytes_pinned=True,
            runtime_sole_pio_writer_complete_bytes_pinned=True,
            runtime_pio_bus_constructor_complete_bytes_pinned=True,
            ordinary_worker_wait_on_ce_stack=True,
            external_ce_wait_stack_in_private_budget=False,
            private_service_and_proof_stacks_separate=True,
            service_preserves_original_dma_fence=True,
            write_layout_scope='Explicit source-specific writable runtime; requires privately prepared card profile and hardware acceptance',
            mutable_write_domain=report['mutable_write_domain']+'; scoped SCI PIO and prepared SD image/journal only',
            mmio_write_domain='Maple MDEN gate and independently scoped SCI/GPIO writer acquisition, byte PIO and restoration',
            caveat='Native source/profile, sequencing and stack admission; physical save/load/recovery and external shutdown acceptance remain pending')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    try:print(json.dumps(check(args.directory),indent=2))
    except (OSError,ValueError,struct.error,UnicodeError) as error:
        parser.exit(1,'A2 hook audit failed: '+str(error)+'\n')

if __name__=='__main__':main()
