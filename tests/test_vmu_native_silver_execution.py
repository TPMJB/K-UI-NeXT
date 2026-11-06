#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent original Silver startup, reclamation and cold-P2 proof.

Requires the owner's original startup; no retail words are embedded. Executes
the original normalizer and allocator, then calls the production extent/cache,
heap and temporary coherence installer. The production SH coherence gate is
executed with separate cached/RAM views, including terminal refusal cases.
This is a bounded integration proof, not a game emulator or console result.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from test_vmu_native_heap_execution import SH,physical
from test_vmu_native_heap_direct_execution import Heap,Call,Lease,READ,compile_api
from check_vmu_native_sdk_coherence import Gate,GATE_UNIFORM
from check_vmu_native_stage_stack import Machine,canonical
from check_vmu_native_worker_gateway import read_elf

LOAD=0x8c010000
SOURCE_BYTES=2956128
PREFIX_BYTES=2923360
SOURCE_SHA='4460c60597d6c1f587c125c2dd4b2b491d0a568f31b52c91bce7d8c74f9d550d'
NORMALIZER=0x8c0dde40
NORMALIZER_END=0x8c0dde68
LOCK=0x8c11cd24
P1_FLAG=LOCK+4
P2_FLAG=P1_FLAG|0xa0000000
STACK=0x8c00efc0
LOW_HEAP_HOOK=0x8c004200

SOURCES=('src/core/vmu_native_program.c','src/core/vmu_native_resolution_cache.c',
    'src/core/vmu_native_discovery.c','src/core/vmu_native_layout.c',
    'src/core/vmu_native_runtime.c','src/core/vmu_native_heap.c',
    'src/core/vmu_native_exit.c','src/core/vmu_native_patch.c',
    'src/core/hash.c','src/core/data.c')
PROOF_SOURCES=SOURCES+(
    'include/kui/vmu_native_discovery.h','include/kui/vmu_native_program.h',
    'include/kui/vmu_native_heap.h','include/kui/vmu_native_resolution_cache.h',
    'include/kui/vmu_native_layout.h','include/kui/vmu_native_runtime.h',
    'include/kui/vmu_native_exit.h','include/kui/vmu_native_passive.h',
    'include/kui/vmu_native_patch.h','include/kui/vmu_native_terminal_dispatch.h',
    'src/core/vmu_native_firmware_exit.inc','src/core/vmu_native_heap_bottom_code.inc',
    'src/core/vmu_native_stage_placement.c',
    'src/loader/vmu_native_sdk_coherence.c','src/loader/vmu_native_sdk_coherence.h',
    'src/loader/vmu_native_passive_stage.inc','src/loader/vmu_native_passive.c',
    'src/loader/vmu_native_sdk_coherence.S','src/loader/vmu_native_sdk_coherence_stage.inc',
    'tests/test_vmu_native_silver_execution.py','tests/test_vmu_native_heap_execution.py',
    'tests/test_vmu_native_heap_direct_execution.py','tests/test_armada_ce_maple_hook.py',
    'tools/check_vmu_native_sdk_coherence.py','tools/check_vmu_native_stage_stack.py',
    'tools/check_vmu_native_worker_gateway.py','tools/sh4_delay_slots.py')

def proof_hashes():
    return {name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in PROOF_SOURCES}


# The generated bridge includes the real temporary installer. Fixed mappings
# are private to this test process; no console addresses or hardware are used.
BRIDGE=r'''
#define _GNU_SOURCE
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#define KUI_VMU_NATIVE_CACHED_CODE 1
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_program.h"
#include "vmu_native_sdk_coherence.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#define DEST UINT32_C(0xac700000)
#define BYTES UINT32_C(0x4000)
#define KUI_RETAIL_EXEC_ADDRESS UINT32_C(0x8c010000)
#define NATIVE_LOW_HEAP_HOOK UINT32_C(0x8c004200)
static uint32_t exec_bytes,call_slot;
static struct kui_vmu_native_discovery native_driver;
static struct kui_vmu_native_layout passive_layout;
static struct kui_vmu_native_heap native_heap;
static struct kui_vmu_native_heap_call native_call;
static struct kui_vmu_native_exit native_exits;
static struct kui_vmu_native_passive_exports exports;
static uint8_t *original;
static unsigned patches;
static uint32_t native_word(uint32_t at) {return *(const uint32_t *)(uintptr_t)at;}
static bool passive_entry(uint32_t at,uint32_t dest,uint32_t bytes) {
    dest-=UINT32_C(0x20000000);return !(at&1u) && at>=dest+80u && at<dest+bytes;
}
static void kui_native_passive_publish(uint32_t begin,uint32_t end) {
    assert(begin==call_slot && end==begin+4u);
    memcpy((void *)(uintptr_t)(begin|UINT32_C(0x20000000)),(void *)(uintptr_t)begin,4u);
    ++patches;
}
#include "vmu_native_sdk_coherence_stage.inc"
static void map_ram(uint32_t at) {
    void *p=mmap((void *)(uintptr_t)at,0x1000000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(p==(void *)(uintptr_t)at);
}
void silver_setup(const void *image,size_t bytes) {
    static bool mapped;
    if(!mapped) {map_ram(UINT32_C(0x8c000000));map_ram(UINT32_C(0xac000000));mapped=true;}
    struct kui_vmu_native_resolution resolution;uint8_t wire[1024];
    assert(kui_vmu_native_resolution_cache_build(image,bytes,UINT32_C(0x8c010000),wire,sizeof(wire),&resolution));
    native_driver=resolution.driver;passive_layout=resolution.layout;
    native_heap=resolution.heap;native_call=resolution.call;native_exits=resolution.exits;exec_bytes=(uint32_t)bytes;
    free(original);original=malloc(bytes);assert(original);memcpy(original,image,bytes);
    /* Find the actual immutable R3 target literal in the reviewed44B wrapper. */
    unsigned count=0;
    for(uint32_t at=passive_layout.initializer_wrapper;at<passive_layout.initializer_wrapper+44u;at+=2u) {
        uint16_t op;memcpy(&op,original+at-UINT32_C(0x8c010000),2u);
        if((op&0xff00u)!=0xd300u)continue;
        uint32_t slot=((at+4u)&~3u)+4u*(op&255u),value;
        memcpy(&value,original+slot-UINT32_C(0x8c010000),4u);
        if(value==passive_layout.initializer) {call_slot=slot;++count;}
    }
    assert(count==1u);
}
void silver_reset(void) {
    memcpy((void *)(uintptr_t)UINT32_C(0x8c010000),original,exec_bytes);
    memcpy((void *)(uintptr_t)UINT32_C(0xac010000),original,exec_bytes);
    struct kui_vmu_native_passive_config c={0};c.version=1;c.bytes=sizeof(c);
    c.driver=native_driver;c.layout=passive_layout;
    memset((void *)kui_vmu_native_sdk_coherence_state,0,48u);
    assert(kui_vmu_native_sdk_coherence_prepare(&c)==1);
    uint32_t words[10]={UINT32_C(0x4b545231),2u,40u,UINT32_C(0x8c700100),
        UINT32_C(0x8c700200),UINT32_C(0x8c700300),UINT32_C(0x8c700400),
        UINT32_C(0xac700500),UINT32_C(0xac700600),DEST+BYTES};
    memcpy((void *)(uintptr_t)(DEST+0x80u),words,sizeof(words));
    memcpy((void *)(uintptr_t)(DEST+BYTES),(const void *)kui_vmu_native_sdk_coherence_state,48u);
    exports=(struct kui_vmu_native_passive_exports){.trace_exports=DEST+0x80u,
        .bss_begin=DEST+BYTES,.bss_end=DEST+BYTES+0x100u,.stack_bottom=DEST+0x1f000u};patches=0;
}
void silver_heap_hook(uint32_t target) {
    *(uint32_t *)(uintptr_t)native_heap.initializer_slot=target;
    *(uint32_t *)(uintptr_t)(native_heap.initializer_slot|UINT32_C(0x20000000))=target;
}
int silver_install(void) {return native_sdk_coherence_install(&exports,DEST,BYTES);}
int silver_admit(void) {return native_sdk_coherence_cached_admit();}
unsigned silver_patches(void) {return patches;}
uint32_t silver_slot(void) {return call_slot;}
uint32_t silver_lock_pointer(unsigned family) {
    struct kui_vmu_native_discovery d=native_driver;d.family=family;
    return kui_vmu_native_lock_pointer(&d);
}
int silver_normalizer_valid(const void *image,size_t bytes,const void *driver,
    const void *heap_call,uint32_t maple_call) {
    return kui_vmu_native_early_p2_calls_valid(image,bytes,driver,
        ((const struct kui_vmu_native_heap_call *)heap_call)->call,maple_call);
}
'''

def api(directory):
    bridge=Path(directory)/'bridge.c';bridge.write_text(BRIDGE)
    so=Path(directory)/'silver.so'
    command=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O2','-shared','-fPIC',
        '-Wall','-Wextra','-Werror','-Wpedantic','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
        '-DKUI_VMU_NATIVE_READONLY_PROBE=1','-DKUI_VMU_NATIVE_CACHED_CODE=1',
        '-I',str(ROOT/'include'),'-I',str(ROOT/'src/loader'),
        *[str(ROOT/s)for s in SOURCES],str(ROOT/'src/loader/vmu_native_sdk_coherence.c'),str(bridge),'-o',str(so)]
    compiled=subprocess.run(command,capture_output=True,text=True)
    if compiled.returncode:
        raise RuntimeError(compiled.stderr)
    lib=ct.CDLL(str(so));P=ct.c_void_p;N=ct.c_size_t;U=ct.c_uint32
    signatures={
        'kui_vmu_native_program_extent':([P,N,U,ct.POINTER(N)],ct.c_bool),
        'kui_vmu_native_resolution_cache_build':([P,N,U,P,N,P],ct.c_bool),
        'kui_vmu_native_resolution_cache_replay':([P,N,U,P,N,P,P],ct.c_bool),
        'kui_vmu_native_resolution_cache_validate_untrusted':([P,N,U,P,N,P],ct.c_bool),
        'silver_setup':([P,N],None),'silver_reset':([],None),'silver_install':([],ct.c_int),
        'silver_admit':([],ct.c_int),'silver_heap_hook':([U],None),'silver_patches':([],U),'silver_slot':([],U),
        'silver_lock_pointer':([U],U),'silver_normalizer_valid':([P,N,P,P,U],ct.c_int)}
    for name,(args,result)in signatures.items():
        f=getattr(lib,name);f.argtypes=args;f.restype=result
    return lib,command

def original_normalizer(source):
    cpu=SH(source);cpu.r[15]=STACK;cpu.sr=0x60000000
    before=cpu.block(LOAD,len(source))
    for pc in range(NORMALIZER,NORMALIZER_END,2):cpu.plain(cpu.read(pc,2),pc)
    assert cpu.read(LOCK)==P2_FLAG
    assert cpu.instructions==20 and cpu.block(LOAD,LOCK-LOAD)==before[:LOCK-LOAD]
    owner=[w for w in cpu.writes if w[1]==LOCK]
    assert len(owner)==1 and owner[0][0]==NORMALIZER_END-2
    return dict(instructions=cpu.instructions,lock_cell=hex(LOCK),before=hex(P1_FLAG),after=hex(cpu.read(LOCK)))

def cache_and_extent(lib,source):
    raw=ct.create_string_buffer(source);prefix=source[:PREFIX_BYTES]
    def extent(blob):
        value=ct.c_size_t(0x12345678);buf=ct.create_string_buffer(bytes(blob))
        ok=lib.kui_vmu_native_program_extent(buf,len(blob),LOAD,ct.byref(value))
        return ok,value.value
    assert extent(source)==(True,PREFIX_BYTES)
    assert extent(prefix)==(True,PREFIX_BYTES)
    wire=ct.create_string_buffer(1024)
    assert lib.kui_vmu_native_resolution_cache_build(raw,PREFIX_BYTES,LOAD,wire,1024,None)
    record=wire.raw;driver=record[64:168];layout=struct.unpack_from('<110I',record,168)
    assert struct.unpack_from('<I',driver,8)[0]==3,'Silver requires source-bound EARLY_P2_V1'
    assert record[136:168]==hashlib.sha256(prefix).digest()
    assert not lib.kui_vmu_native_resolution_cache_build(raw,len(source),LOAD,ct.create_string_buffer(1024),1024,None)
    pin=ct.create_string_buffer(hashlib.sha256(record[:992]).digest());output=ct.create_string_buffer(704)
    for verify in (False,True):
        if verify:
            assert lib.kui_vmu_native_resolution_cache_validate_untrusted(raw,PREFIX_BYTES,LOAD,wire,1024,output)
            assert not lib.kui_vmu_native_resolution_cache_validate_untrusted(raw,len(source),LOAD,wire,1024,output)
        else:
            assert lib.kui_vmu_native_resolution_cache_replay(raw,PREFIX_BYTES,LOAD,wire,1024,pin,output)
            assert not lib.kui_vmu_native_resolution_cache_replay(raw,len(source),LOAD,wire,1024,pin,output)
    refusals=0
    for offset in (0,16,37,0x60,0x300,0x306,0x31c,0x320,0x324,0x328,0x32c,0x7000,0x7fff):
        changed=bytearray(source);changed[PREFIX_BYTES+offset]^=1
        assert extent(changed)==(False,0x12345678);refusals+=1
    for cut in (1,2,4,16,32):assert extent(source[:-cut])==(False,0x12345678);refusals+=1
    for alias in (0,0x80000000,0xa0000000):
        for extra in (0,4):
            changed=bytearray(source);struct.pack_into('<I',changed,0x1000,(physical(LOAD)+PREFIX_BYTES+extra)|alias)
            assert extent(changed)==(False,0x12345678);refusals+=1
    # Printable per-title footer text does not authorize/disallow reclamation.
    changed=bytearray(source);changed[PREFIX_BYTES+0x80]=ord('X')
    assert extent(changed)==(True,PREFIX_BYTES)
    # A cached proof binds the pristine immutable prefix, not its old IP copy.
    for at in (NORMALIZER-LOAD,NORMALIZER_END-LOAD-2,layout[3]-LOAD,layout[2]-LOAD):
        changed=bytearray(prefix);changed[at]^=1;buf=ct.create_string_buffer(bytes(changed))
        assert not lib.kui_vmu_native_resolution_cache_replay(buf,len(prefix),LOAD,wire,1024,pin,output);refusals+=1
    assert raw.raw[:len(source)]==source
    return record,dict(prefix_bytes=PREFIX_BYTES,discarded_bytes=len(source)-PREFIX_BYTES,
        prefix_sha256=hashlib.sha256(prefix).hexdigest(),negative_cases=refusals,
        pristine_input_unchanged=True,raw_record_refused=True)

def source_binding(lib,source,record):
    lib.silver_setup(ct.create_string_buffer(source[:PREFIX_BYTES]),PREFIX_BYTES)
    driver=ct.create_string_buffer(record[64:168]);call=ct.create_string_buffer(record[680:700])
    layout=struct.unpack_from('<110I',record,168);raw=ct.create_string_buffer(source[:PREFIX_BYTES])
    assert lib.silver_normalizer_valid(raw,PREFIX_BYTES,driver,call,layout[4])
    refused=0
    for at in range(NORMALIZER-LOAD,NORMALIZER_END-LOAD):
        changed=bytearray(source[:PREFIX_BYTES]);changed[at]^=1
        blob=ct.create_string_buffer(bytes(changed))
        assert not lib.silver_normalizer_valid(blob,len(changed),driver,call,layout[4])
        # A malformed supported source head must not downgrade to the old
        # cached-lock family and thereby bypass the cold coherence gate.
        assert not lib.kui_vmu_native_resolution_cache_build(blob,len(changed),LOAD,
            ct.create_string_buffer(1024),1024,None),'malformed normalizer fell back to cached-lock admission'
        refused+=1
    for bit in (1,2,4,8,16,32,64,128):
        changed=bytearray(record[680:700]);changed[4]^=bit
        assert not lib.silver_normalizer_valid(raw,PREFIX_BYTES,driver,ct.create_string_buffer(bytes(changed)),layout[4]);refused+=1
    assert not lib.silver_normalizer_valid(raw,PREFIX_BYTES,driver,call,layout[4]+2);refused+=1
    for alias in (0,0x80000000,0xa0000000):
        changed=bytearray(source[:PREFIX_BYTES]);struct.pack_into('<I',changed,0x1000,physical(NORMALIZER+2)|alias)
        assert not lib.silver_normalizer_valid(ct.create_string_buffer(bytes(changed)),len(changed),driver,call,layout[4]);refused+=1
    # Encoded entry and address generation into the protected cold head must
    # remain refused. Plain code-as-data reads confer no entry authority.
    for word in (0xa000,0xb000,0x8900,0x8b00,0x8d00,0x8f00,0xc701):
        changed=bytearray(source[:PREFIX_BYTES]);struct.pack_into('<H',changed,NORMALIZER-LOAD-2,word)
        assert not lib.silver_normalizer_valid(ct.create_string_buffer(bytes(changed)),len(changed),driver,call,layout[4]);refused+=1
    # An alias edit of the normalizer's cell literal still normalizes the
    # same physical flag; it cannot silently select cached-lock family2.
    pc=NORMALIZER+18;op=struct.unpack_from('<H',source,pc-LOAD)[0]
    slot=((pc+4)&~3)+4*(op&255)-LOAD
    for alias in (0,0xa0000000):
        changed=bytearray(source[:PREFIX_BYTES]);struct.pack_into('<I',changed,slot,physical(LOCK)|alias)
        cpu=SH(bytes(changed));cpu.r[15]=STACK;cpu.sr=0x60000000
        for at in range(NORMALIZER,NORMALIZER_END,2):cpu.plain(cpu.read(at,2),at)
        assert cpu.read(LOCK)==P2_FLAG
        blob=ct.create_string_buffer(bytes(changed))
        assert not lib.silver_normalizer_valid(blob,len(changed),driver,call,layout[4])
        assert not lib.kui_vmu_native_resolution_cache_build(blob,len(changed),LOAD,
            ct.create_string_buffer(1024),1024,None),'lockcell alias edit downgraded to cached-lock admission'
        refused+=1
    assert lib.silver_lock_pointer(2)==P1_FLAG
    assert lib.silver_lock_pointer(3)==P2_FLAG
    return dict(normalizer_mutations_and_call_binding_refusals=refused,old_family_exact_p1=True,new_family_exact_p2=True,
        alias_literal_edits_replay_actual_p2_but_refuse_source=True)

def installer(lib,source,record):
    prefix=source[:PREFIX_BYTES];lib.silver_setup(ct.create_string_buffer(prefix),len(prefix));lib.silver_reset()
    layout=struct.unpack_from('<110I',record,168);wrapper,init=layout[3],layout[2]
    assert lib.silver_admit()
    assert lib.silver_install()==-1 and not lib.silver_patches(),'late install must require its owned heap hook'
    lib.silver_heap_hook(LOW_HEAP_HOOK)
    assert lib.silver_install()==1 and lib.silver_patches()==1
    assert ct.string_at(init,12)==prefix[init-LOAD:init-LOAD+12]
    refusals=0
    for begin,count in ((wrapper,44),(init,12)):
        for byte in range(count):
            # The helper BSR is a decoded in-image edge, not a pinned
            # displacement. Test its opcode class separately below.
            if begin==wrapper and byte in (28,29):continue
            lib.silver_reset();before=ct.string_at(lib.silver_slot(),4)
            private=ct.string_at(0xac704000,48)
            cell=ct.c_ubyte.from_address(begin+byte);cell.value^=1
            assert not lib.silver_admit()
            lib.silver_heap_hook(LOW_HEAP_HOOK)
            assert lib.silver_install()==-1 and not lib.silver_patches()
            assert ct.string_at(lib.silver_slot(),4)==before and ct.string_at(0xac704000,48)==private;refusals+=1
    lib.silver_reset();ct.c_ushort.from_address(wrapper+28).value^=0x1000
    assert not lib.silver_admit()
    lib.silver_heap_hook(LOW_HEAP_HOOK)
    assert lib.silver_install()==-1 and not lib.silver_patches();refusals+=1
    for index in range(12):
        lib.silver_reset();lib.silver_heap_hook(LOW_HEAP_HOOK)
        ct.c_uint32.from_address(0xac704000+4*index).value^=1
        private=ct.string_at(0xac704000,48);before=ct.string_at(lib.silver_slot(),4)
        assert lib.silver_install()==-1 and not lib.silver_patches()
        assert ct.string_at(lib.silver_slot(),4)==before and ct.string_at(0xac704000,48)==private;refusals+=1
    for index in (0,8):
        lib.silver_reset();lib.silver_heap_hook(LOW_HEAP_HOOK)
        ct.c_uint32.from_address(0xac704000+4*index).value^=0x20000000
        assert lib.silver_install()==-1 and not lib.silver_patches();refusals+=1
    # The cold source proof ran before the borrowed initializer pointer was
    # replaced. At real late install the owned LOW hook is already there;
    # re-running an unchanged-image predicate would reject this valid state.
    lib.silver_reset()
    assert lib.silver_admit()
    lib.silver_heap_hook(LOW_HEAP_HOOK)
    assert lib.silver_install()==1 and lib.silver_patches()==1,'late SDK install rejected its already-owned heap-hook substitution'
    for wrong in (0,LOW_HEAP_HOOK+2,LOW_HEAP_HOOK|0x20000000):
        lib.silver_reset();assert lib.silver_admit();lib.silver_heap_hook(wrong)
        assert lib.silver_install()==-1 and not lib.silver_patches();refusals+=1
    lib.silver_reset()
    return dict(actual_wrapper_bytes=44,actual_sdk_prefix_bytes=12,installer_mutation_refusals=refusals,
        one_target_literal_patched=True,original_initializer_unmodified=True,post_heap_hook_order_checked=True,bridge_owned_hook=hex(LOW_HEAP_HOOK))

def heap_transactions(directory,source,record):
    lib=compile_api(directory);call=Call.from_buffer_copy(record[680:700]);owner=Heap.from_buffer_copy(record[608:680]);results=[]
    raw_owner=Heap.from_buffer_copy(bytes(owner));raw_owner.image_bytes=len(source)
    assert call.entry_begin==0x8c2d9b80 and call.entry_bytes==0xd26480
    for lease_bytes in (0x47000,0x14000):
        cpu=SH(source);_,_,steps=cpu.run(owner.initializer,[call.entry_begin,call.entry_bytes],STACK)
        read_addresses=[];reader_fails=False
        @READ
        def read(_context,address,out):
            read_addresses.append(address)
            if reader_fails:return False
            out[0]=cpu.read(address);return True
        lease=Lease()
        assert not lib.bottom_fresh(ct.byref(raw_owner),read,None,call.entry_begin,call.entry_bytes,1,lease_bytes,ct.byref(lease))
        assert lib.bottom_fresh(ct.byref(owner),read,None,call.entry_begin,call.entry_bytes,1,lease_bytes,ct.byref(lease))
        assert lib.preallocate(ct.byref(lease),read,None,1)
        placeholder,_,_=cpu.run(owner.allocate,[call.entry_bytes-lease_bytes-64],STACK)
        assert lib.bottom_placeholder(ct.byref(lease),read,None,1,placeholder)
        payload,_,_=cpu.run(owner.allocate,[lease_bytes],STACK)
        assert lib.bottom_allocated(ct.byref(lease),read,None,1,payload)
        _,_,_=cpu.run(owner.release,[placeholder],STACK)
        assert lib.bottom_ready(ct.byref(lease),read,None,1)
        expected=bytearray(source[:PREFIX_BYTES])
        # The original allocator owns its live sentinel/control block inside
        # the image. Its code and all other reclaimed-prefix bytes stay exact.
        at=owner.sentinel-LOAD
        expected[at:at+48]=cpu.block(owner.sentinel,48)
        assert cpu.block(LOAD,PREFIX_BYTES)==bytes(expected)
        witnesses=(owner.begin_cell,owner.begin_copy_cell,owner.bytes_cell,
            owner.initializer+0x14c,owner.initializer+0x150,owner.initializer+0x154,
            owner.sentinel+4,lease.header+4)
        for address in witnesses:
            value=cpu.read(address);cpu.write(address,value^1)
            assert not lib.lease_valid(ct.byref(lease),read,None,1)
            cpu.write(address,value)
            assert lib.lease_valid(ct.byref(lease),read,None,1)
        reader_fails=True
        assert not lib.lease_valid(ct.byref(lease),read,None,1)
        reader_fails=False
        assert lib.lease_valid(ct.byref(lease),read,None,1)
        assert read_addresses and all(read_addresses),'direct identity read a fabricated null dispatch role'
        cpu.store_block(payload,bytes((i*37+19)&255 for i in range(lease_bytes)))
        protected=cpu.block(call.entry_begin,lease_bytes+32)
        for count in (32,2048,65536):
            pointer,writes,_=cpu.run(owner.allocate,[count],STACK)
            assert lib.lease_valid_with_release(ct.byref(lease),read,None,1,pointer,None)
            cpu.run(owner.release,[pointer],STACK)
            assert cpu.block(call.entry_begin,lease_bytes+32)==protected
            assert lib.lease_valid(ct.byref(lease),read,None,1)
        assert not lib.lease_valid(ct.byref(lease),read,None,2)
        results.append(dict(lease_bytes=lease_bytes,payload=hex(payload),initializer_instructions=steps,
            subsequent_alloc_free_pairs=3,full_source_overlap_refused=True,
            prefix_unchanged_except_original_allocator_control_block=True,
            mutated_identity_word_refusals=len(witnesses),reader_failure_refused=True,
            no_null_dispatch_reads=True,retained_lease_preserved=True))
    return results

def compile_gate(directory):
    cc=Path(os.environ.get('SH_ELF_CC',str(ROOT/'.deps/sh-elf/bin/sh-elf-gcc')))
    stub=Path(directory)/'gate-stub.S'
    stub.write_text('.text\n.global _kui_vmu_native_runtime_fault_hook\n_kui_vmu_native_runtime_fault_hook:\n bra .\n nop\n.bss\n.balign 4\n.global _kui_vmu_native_sdk_coherence_state\n.type _kui_vmu_native_sdk_coherence_state,@object\n_kui_vmu_native_sdk_coherence_state:\n.space 48\n.size _kui_vmu_native_sdk_coherence_state,48\n')
    elf=Path(directory)/'gate.elf'
    result=subprocess.run([str(cc),'-ml','-m4-single','-nostdlib','-Wl,-Ttext=0x8c700000,-Tbss=0xac710000,-e,_kui_vmu_native_sdk_coherence_uniform_gate',
        str(ROOT/'src/loader/vmu_native_sdk_coherence.S'),str(stub),'-o',str(elf)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    return elf

def coherence(elf,record,normalizer):
    image=read_elf(elf);d=struct.unpack_from('<18I',record,64);l=struct.unpack_from('<110I',record,168)
    cases=0
    for seed in range(8):
        for outcome in ('ok','ready','kind','dma','p2held','p1held','pointer','postpointer','postflag'):
            cpu=Gate(image,0,seed,metadata=(d[11],d[9],d[10],l[2]),outcome=outcome)
            if outcome=='ok':cpu.cached_pointer=int(normalizer['after'],16)
            cpu.check();cases+=1
        # A skipped normalizer is a different alias, not an acceptable value.
        cpu=Gate(image,0,seed,metadata=(d[11],d[9],d[10],l[2]));cpu.cached_pointer=P1_FLAG;cpu.run()
        assert cpu.pc==cpu.terminal and cpu.r[4]==0x20007
        assert cpu.r[5:8]==[LOCK,P1_FLAG,P2_FLAG] and not cpu.owner_writes and not cpu.purges;cases+=1
    for mask in range(15):Gate(image,0,3,mask,metadata=(d[11],d[9],d[10],l[2])).check();cases+=1
    return dict(instruction_cases=cases,distinct_p1_p2_views=True,no_either_alias_acceptance=True,
        mdst_and_both_flags_checked_before_ocbp=True,ocbp_before_tas=True,terminal_refusal_before_owner_stack=True,
        production_assembly_sha256=hashlib.sha256((ROOT/'src/loader/vmu_native_sdk_coherence.S').read_bytes()).hexdigest(),
        elf_sha256=hashlib.sha256(Path(elf).read_bytes()).hexdigest())

class SourceMachine(Machine):
    """One missing generic integer opcode used by the original SDK wrapper."""
    def plain(self,word,pc):
        if word&0xf0ff==0x4009: # SHLR2 Rn; does not change T.
            self.r[word>>8&15]>>=2;return
        super().plain(word,pc)

def original_wrapper_prefix(elf,source,record):
    image=read_elf(elf);d=struct.unpack_from('<18I',record,64);l=struct.unpack_from('<110I',record,168)
    wrapper,initializer=l[3],l[2]
    def literal(at):
        op=struct.unpack_from('<H',source,at-LOAD)[0]
        assert op>>12==0xd
        return struct.unpack_from('<I',source,((at+4)&~3)+4*(op&255)-LOAD)[0]
    op=struct.unpack_from('<H',source,wrapper+28-LOAD)[0]
    assert op>>12==0xb
    displacement=op&4095;displacement-=4096 if displacement&2048 else 0
    helper=wrapper+32+2*displacement
    assert literal(wrapper+34)==initializer
    checked=0
    for seed in range(8):
        original=SourceMachine(image,GATE_UNIFORM,seed,seed%15,STACK)
        original.base=LOAD;original.blob=source[:PREFIX_BYTES];original.pc=wrapper;original.stop_pc=initializer
        incoming=original.r.copy();original.r[4]=0x8c200000;original.r[5]=0x8c220000
        helper_calls=[]
        def call(target):
            if target==helper:
                helper_calls.append(target);original.calls.append(target)
                original.abi_return(seed);return True
            assert target==initializer
            return False
        original.call=call;original.run()
        assert helper_calls==[helper] and original.r[15]==STACK-16
        assert original.r[4:8]==[0x8c220000,0xc000,0x8c200000,0xc000]
        assert original.pr==wrapper+44 and original.sr&0xf0==0xf0
        assert original.r[8:15]==incoming[8:15]
        # Enter the exact production gate in place of the original R3 target.
        gated=Gate(image,0,seed,metadata=(d[11],d[9],d[10],initializer),stack=original.r[15])
        gated.r=original.r.copy();gated.r[3]=gated.base+0x20000000
        gated.pr,gated.mach,gated.macl,gated.gbr,gated.sr,gated.fpscr,gated.fr=(
            original.pr,original.mach,original.macl,original.gbr,original.sr,original.fpscr,original.fr.copy())
        gated.initial=(gated.r.copy(),gated.pr,gated.mach,gated.macl,gated.gbr,gated.sr,gated.fpscr,gated.fr.copy())
        gated.check()
        reference=SourceMachine(image,GATE_UNIFORM,seed,15,original.r[15])
        reference.base=LOAD;reference.blob=source[:PREFIX_BYTES];reference.pc=initializer;reference.stop_pc=initializer+12
        reference.r=original.r.copy();reference.mem=original.mem.copy()
        reference.pr,reference.mach,reference.macl,reference.gbr,reference.sr,reference.fpscr,reference.fr=(
            original.pr,original.mach,original.macl,original.gbr,original.sr,original.fpscr,original.fr.copy())
        reference.run()
        assert gated.r==reference.r
        assert (gated.pr,gated.mach,gated.macl,gated.gbr,gated.sr,gated.fpscr,gated.fr)==(
            reference.pr,reference.mach,reference.macl,reference.gbr,reference.sr,reference.fpscr,reference.fr)
        assert all(reference.mem[canonical(a)]==v for a,v in gated.owner_writes)
        checked+=1
    return dict(original_wrapper_and_prefix_cases=checked,wrapper_bytes=44,prefix_bytes=12,
        helper_calls='One actual helper edge with adversarial ABI-return values; helper body not executed.',
        exact_register_sr_pr_and_stack_replay=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--silver',type=Path,required=True)
    parser.add_argument('--worker',type=Path,help='Optional final linked worker; otherwise isolate the unchanged production gate assembly')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();source=args.silver.read_bytes();source_hashes=proof_hashes()
    assert len(source)==SOURCE_BYTES and hashlib.sha256(source).hexdigest()==SOURCE_SHA,'wrong original Silver input'
    with tempfile.TemporaryDirectory(prefix='kui-silver-original-')as work:
        lib,command=api(work);record,extent=cache_and_extent(lib,source)
        normalizer=original_normalizer(source);gate=args.worker or compile_gate(work)
        report=dict(schema='kui.native.silver.original-integration.v1',source_bytes=len(source),source_sha256=SOURCE_SHA,
            normalizer=normalizer,extent=extent,source_binding=source_binding(lib,source,record),
            installer=installer(lib,source,record),heap=heap_transactions(work,source,record),
            coherence=coherence(gate,record,normalizer),original_wrapper=original_wrapper_prefix(gate,source,record),host_compile_command=command,
            linked_complete_worker=bool(args.worker),console_execution=False,proof_source_sha256=source_hashes,
            limits=['Original startup normalizer, original allocator and production coherence/metadata contracts only.',
                'No full game, SCI route, prepared final module, console save/load or measured hardware timing is claimed.'])
        assert args.silver.read_bytes()==source,'owner input was changed'
        assert proof_hashes()==source_hashes,'proof source changed during execution; rerun on a stable boundary'
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS original Silver: source-bound P2 normalizer, strict IP reclamation/cache binding,44B wrapper/12B prefix, both bottom leases and cold coherence refusals')

if __name__=='__main__':main()
