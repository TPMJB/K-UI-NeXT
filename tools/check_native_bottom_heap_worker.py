#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked worker's endpoint lease and complete free-list guards.

Fixtures are authored allocator metadata, not retail executable bytes. Every
heap predicate executes its actual linked SH instructions. Only the supplied
coherent RAM-reader interface is folded to its specified read/output/boolean
operation; no identity, geometry, lease or release-walk predicate is replaced.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from profile_vmu_native_release_sh import ReleaseSH,fixture,READER,STACK,LEASE,FRAME
from test_vmu_native_heap_execution import SH,MASK,signed
from test_native_dual_readers import object_section
from check_vmu_native_worker_gateway import read_elf
from check_vmu_native_persistent import prepared_symbol_records
from check_native_followup_profile import bottom_worker_profile
from sh4_delay_slots import validate_delay_slot
from native_runtime_layout import LEASE_BYTES, layout_for
from check_native_general_lean_profile import worker_profile
from check_native_general_lean_heap_pins import WORKER_FUNCTIONS

SCHEMA='kui.native.bottom-heap.compiled-worker.v1'
RETURN=0xdeadbeee
OUTPUT=FRAME
PINS = {'_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_index@vmu_native_heap.c': (100, 'b6d11408b1855b59adb23eb88af064de182c1b5ceb784eedf901f4ff7c6db6fc'),
 '_metadata@vmu_native_heap.c': (416, 'ae4af783f24f41eae20da598b6910f054e571b512c1b1f08c3dc4e5c68bf2483'),
 '_read_is@vmu_native_heap.c': (80, '0d9eb79846bed68111e888d7d83da465c1dbfff4019f018049ed44d33068e5e7'),
 '_bottom_geometry@vmu_native_heap.c': (24,
                                        'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_identity@vmu_native_heap.c': (412, '7bc3dcf8abc60e627d27a8aa1914df151637074b32f6ae211f320a984af8d302'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   '16a2a46cd54b7bf82b732e28b5297ec4fe2a3803fa38f2b1246157f973525b6b'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '3f730f877ba979f408d6c596f8643448183a9cbb9f88761731580f34be8e133f'),
 '_kui_vmu_native_heap_lease_valid': (28, 'd15c3ca657bca47aea6bb764676750e1a474eb5ea90062e315e7e55ca20fd5f2')}
DISPATCH_GATE_PINS = {'_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_index@vmu_native_heap.c': (100, 'a9aa696f5722b6ba5f00b4779f89ba37e21624b46c6ac440c171afe5b6b04334'),
 '_metadata@vmu_native_heap.c': (416, 'fb6a9ee3425f555fe873833b7161f9ea3c287aa3525033f0900321bb394d8a38'),
 '_read_is@vmu_native_heap.c': (80, '34414e24b9c009d0b915e202e1f09e6248a2b868a7f423f1eab4c1132c85037a'),
 '_bottom_geometry@vmu_native_heap.c': (24,
                                        'fe83cd5cec158b6d373003e684f8d52b59b7f5200e588cfe204826026a080668'),
 '_identity@vmu_native_heap.c': (412, 'ebe054d88dead365d0eaf102bc8b2991d50c01be8114b3d9a064874a52ff9893'),
 '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                   '85559f7e6bb73d9b5ed3e5bad8f2366980539a98477c4b4e681f13dbaf7b8aae'),
 '_kui_vmu_native_heap_lease_identity_valid': (76,
                                               '79a1ea16da8da8eeee75d90cbfc5159823a23ba3aa871de989554a606c6313cc'),
 '_kui_vmu_native_heap_lease_valid': (28, 'ec3c8bce81483e723c16ff9f4ba19b982ff81e3dde4701f2706817b52e3462b3')}
DIRECT_FUNCTIONS=frozenset(PINS)-{'_bottom_geometry@vmu_native_heap.c'}
# Independently rebuilt pristine 3a6ae064 General/session A1 and A2 workers
# retain these same complete bodies. This modern direct-malloc closure inlines
# bottom geometry and binds the immutable two-shape table; it is not the
# historical nine-function closure above. No expected pin is learned from the
# candidate. Their exact addresses/literal targets are included in raw hashes.
SESSION_DIRECT_PINS={
 '_span@vmu_native_heap.c':(76,'64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305'),
 '_index@vmu_native_heap.c':(100,'c637e830fbe24402c127227a42e033eb0dbe087c58fb4e506e1c287d6a558f72'),
 '_metadata@vmu_native_heap.c':(436,'660d4aea44dbad2b4dd11ab50c1b1331383fc030759c75514afd12abba9354e0'),
 '_read_is@vmu_native_heap.c':(80,'cdf2e10e5f7e188d19273bede627fc11ec73b8f837cc429a161afe3242670e6e'),
 '_identity@vmu_native_heap.c':(444,'6d46107fe1f8b05973dcc77d14ca96bc1388110eb51f4ecc1b9e258e8521efa9'),
 '_kui_vmu_native_heap_lease_identity_valid':(76,'7a9b2982584158380cfa4e3db32f550184e08ad2ce45b96b3a17a6869221b20c'),
 '_kui_vmu_native_heap_lease_valid_with_release':(512,'a6f20c65fced0cbeaf7825baa0baceede1b54d526e16f6f7656bae9376e66dcf'),
 '_kui_vmu_native_heap_lease_valid':(52,'9c7327ee6f04ea86e61596db47984bf589331a6164f950b639db39bef89f58d1')}
SESSION_DIRECT_OBJECTS={'_heap_shapes@vmu_native_heap.c':
    (16,'fcf8e4fc85390977b00ad86a99a683d47e5e633fff6d62b0cb5d93837803e2c0')}
# Independently pristine3a6 ordinary A1/A2 references preserve these complete
# guard bodies and the16-byte immutable shape object. Slot-specific raw pins
# include every actual pointer relocation; no candidate supplies expectations.
ORDINARY_DIRECT_PINS={1: {'_identity@vmu_native_heap.c': (444, '006fecf2e6f25632d12a3ad435bf72f2d55554dcb6bed3b6bd3fc5cc00f7230d'),
     '_index@vmu_native_heap.c': (100, '66676f102412c8ecc27ce9f0143f03a9594deca4f4245784860c7d675875f921'),
     '_kui_vmu_native_heap_lease_identity_valid': (76,
                                                   '359f305ee7ed3c776321cdc82bcabfd847ce87042f734d1d7210bf0b5a0414a9'),
     '_kui_vmu_native_heap_lease_valid': (52, 'd0513e0edf81e206a40a0416326fc17fab96556207ff06db7cc9337678680f94'),
     '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                       'd17a47aabddfd6ae39dba071689b6658984616aa52c7cf9a81203120e889e7b1'),
     '_metadata@vmu_native_heap.c': (436, 'dc5bcf893c96670cce10942be17c20414a586e291770083ee817f89cc8fa1cf0'),
     '_read_is@vmu_native_heap.c': (80, '5524658ff175a69946be734798d5c531e8747ad0706ac9f514708f0c284a2657'),
     '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305')},
 2: {'_identity@vmu_native_heap.c': (444, 'd2c4049919bb3860844e575908ebb38d9801e04137162fea966b590eb48edd1a'),
     '_index@vmu_native_heap.c': (100, '468b8be593a9f50192981498e5670b73f6ecbdebda2936341744b2976f4fb3c8'),
     '_kui_vmu_native_heap_lease_identity_valid': (76,
                                                   'ae45f68a7fd97986bb319ed76003d325483972d3493ade44edd86b409ceaafeb'),
     '_kui_vmu_native_heap_lease_valid': (52, '191609eda161cef5ffed68ede10939328b71b89fb0bb1c849bdd80b5e2925c48'),
     '_kui_vmu_native_heap_lease_valid_with_release': (512,
                                                       '9aa49a668ea37e1ae1dc928fa3fa33b6efb3ed8d3a62a65d0adf520460c61dc8'),
     '_metadata@vmu_native_heap.c': (436, '0150a88517cc6913208591909703a4b552b1173fc8f8b2e13b047e2837aa101e'),
     '_read_is@vmu_native_heap.c': (80, '49269d3712b0cacc6782f3e7255b455c4d7faf5667683a9e616b0e88debb7bc5'),
     '_span@vmu_native_heap.c': (76, '64d42c5ab08e5e0c1e43b9aa8b809ce1d2d0be16e4467bcb8f5443ca6b59f305')}}
ORDINARY_DIRECT_OBJECTS={'_heap_shapes@vmu_native_heap.c': (16, 'fcf8e4fc85390977b00ad86a99a683d47e5e633fff6d62b0cb5d93837803e2c0')}
SOURCES=('tools/native_runtime_layout.py','tools/check_native_general_lean_profile.py',
         'tools/check_native_general_lean_heap_pins.py','tools/check_native_compact_background_profile.py',
         'tools/check_native_bottom_heap_worker.py','tools/profile_vmu_native_release_sh.py',
         'tests/test_vmu_native_heap_execution.py','tests/test_native_compact_background_profile.py',
         'src/core/vmu_native_heap.c','src/core/vmu_native_heap_bottom_code.inc',
         'include/kui/vmu_native_heap.h')
SOURCES+=('tools/check_native_session_witness_profile.py','tools/check_native_ordinary_general_profile.py')


def require(value,message):
    if not value:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def records(worker,direct=None):
    result={};names=DIRECT_FUNCTIONS if direct is True or direct_profile(worker) else set(PINS)
    if direct is True:
        # The original-execution harness separately validates the exact
        # physical-disc or compact exports/layout. Keep FILE identity here
        # without asking the image-only reader-layout oracle to select a
        # physical-disc lease profile that it does not implement.
        from check_native_bottom_heap import records as typed_records
        for name,(address,body,kind,section) in typed_records(worker).items():
            if name not in names:continue
            require(kind==2 and section[1]==1 and section[2]&7==6,
                    'Direct guard is not complete executable code: '+name)
            result[name]=(address,body,section[4]+address-section[3])
        require(set(result)==names,'Direct worker heap guard closure differs')
        return result
    for name,address,count,kind,owner in prepared_symbol_records(worker):
        if name not in names:continue
        require(name not in result and kind==2 and owner<len(worker[3]),'Ambiguous/untyped heap kernel: '+name)
        section=worker[3][owner]
        require(section[1]==1 and section[2]&7==6 and section[3]<=address<address+count<=section[3]+section[5],
                'Heap kernel is not complete executable code: '+name)
        at=section[4]+address-section[3];body=worker[0][at:at+count]
        result[name]=(address,body,at)
    require(set(result)==names,'Worker heap guard closure differs')
    return result


def pinned(worker):
    gate=worker[4].get('__kui_native_dispatch_read_gate_worker')
    require(gate is None or gate==(1,0,0,0xfff1) and bottom_worker_profile(worker),
            'Dispatch-gated worker lacks exact bottom capability')
    if session_direct_profile(worker):
        pins=SESSION_DIRECT_PINS
        require(set(pins)==DIRECT_FUNCTIONS,'Session worker direct predicate closure differs')
    elif ordinary_direct_profile(worker):
        from check_native_ordinary_general_profile import profile as ordinary_slot
        pins=ORDINARY_DIRECT_PINS[ordinary_slot(worker)]
        require(set(pins)==DIRECT_FUNCTIONS,'Ordinary worker direct predicate closure differs')
    elif worker_profile(worker):
        require(gate==(1,0,0,0xfff1), 'Lean heap worker requires its exact dispatch gate')
        from check_native_compact_background_profile import worker_profile as compact_profile
        if compact_profile(worker):
            from check_native_compact_background_profile import WORKER_FUNCTIONS as pins
            require(set(pins)==DIRECT_FUNCTIONS,'Direct compact heap predicate closure differs')
        else:
            pins=WORKER_FUNCTIONS
            require(set(pins)==set(PINS)==set(DISPATCH_GATE_PINS),
                    'Lean heap worker must retain the complete historical closure')
    else:
        pins=DISPATCH_GATE_PINS if gate is not None else PINS
    result={}
    for name,(address,body,_) in records(worker).items():
        require((len(body),sha(body))==pins[name],'Worker heap guard instruction/literal pin differs: '+name)
        result[name]=dict(address=hex(address),bytes=len(body),sha256=sha(body))
    return result


def object_pins(worker):
    """Keep compact metadata's complete immutable shape contract explicit."""
    if not direct_profile(worker):return {}
    if session_direct_profile(worker):pins=SESSION_DIRECT_OBJECTS
    elif ordinary_direct_profile(worker):pins=ORDINARY_DIRECT_OBJECTS
    else:
        from check_native_compact_background_profile import WORKER_OBJECTS as pins
    from check_native_bottom_heap import records as typed_records
    available=typed_records(worker);result={}
    require(set(pins)=={'_heap_shapes@vmu_native_heap.c'},
            'Direct worker immutable shape closure differs')
    for name,expected in pins.items():
        require(name in available,'Direct worker shape object missing: '+name)
        address,body,kind,section=available[name]
        require(kind==1 and section[2]&7==2 and (len(body),sha(body))==expected,
                'Direct worker immutable shape pin differs: '+name)
        result[name]=dict(address=hex(address),bytes=len(body),sha256=sha(body))
    return result


def layout():
    compiler=shutil.which('sh-elf-gcc')
    if compiler is None:
        local=ROOT/'.deps/sh-elf/bin/sh-elf-gcc'
        if local.is_file():compiler=str(local)
    require(compiler is not None,'Worker heap ABI check needs sh-elf-gcc')
    names=['sizeof(struct kui_vmu_native_heap_lease)',
           *['offsetof(struct kui_vmu_native_heap_lease,'+n+')' for n in
             ('owner','begin','heap_bytes','lease_bytes','header','payload','units','remaining')],
           'sizeof(struct kui_vmu_native_heap)']
    with tempfile.TemporaryDirectory(prefix='native-bottom-worker-layout-') as temporary:
        source=Path(temporary)/'layout.c';target=Path(temporary)/'layout.o'
        source.write_text('#include <stddef.h>\n#include "kui/vmu_native_heap.h"\n'
          'const uint32_t fields[] __attribute__((section(".rodata.bottom_worker"),used))={' + ','.join(names)+'};\n')
        subprocess.run([compiler,'-std=c11','-Os','-m4-single','-ml','-ffreestanding','-I'+str(ROOT/'include'),
                        '-c',str(source),'-o',str(target)],check=True,capture_output=True)
        wire=object_section(target,'.rodata.bottom_worker')
    expected=(116,16,88,92,96,100,104,108,112,72)
    require(wire==struct.pack('<10I',*expected),'Actual console heap ABI changed')
    return dict(zip(('lease.bytes','lease.owner','lease.begin','lease.heap_bytes','lease.lease_bytes',
                    'lease.header','lease.payload','lease.units','lease.remaining','owner.bytes'),expected))


class Worker(ReleaseSH):
    def __init__(self,path,direct=None):
        super().__init__(path)
        self.closure=records(self.elf,direct)
        self.reader_failure=False
        self.reader_source=None
        self.executed=set();self.reader_addresses=[]
        self.tracking=False

    def write(self,address,value,count=4):
        if getattr(self,'tracking',False):
            at=address&0x1fffffff;stack=STACK&0x1fffffff;out=OUTPUT&0x1fffffff
            require(stack-4096<=at<at+count<=stack+16 or out<=at<at+count<=out+4,
                    'Heap validator wrote outside private stack/output')
        return super().write(address,value,count)

    def instruction(self,address):
        found=[name for name,(begin,body,_) in self.closure.items() if begin<=address<address+2<=begin+len(body)]
        require(len(found)==1,'Worker heap execution leaves its complete pinned closure')
        self.executed.add(found[0])
        return self.read(address,2)

    def call(self,name,args):
        require(not self.stubs,'Heap worker proof cannot fold a predicate')
        before=self.r[8:15].copy();before_sr=self.sr;lease_before=self.block(LEASE,116)
        self.pc,self.pr,self.r[15]=self.symbols[name][0],RETURN,STACK
        for i,value in enumerate(args):
            if i<4:self.r[4+i]=value
            else:self.write(STACK+4*(i-4),value)
        self.instructions=0;self.active=True;self.tracking=True;self.writes.clear();self.reader_addresses=[]
        try:
            for _ in range(250000):
                pc=self.pc
                if pc==RETURN:
                    require(self.r[15]==STACK and self.r[8:15]==before and self.sr&~1==before_sr&~1,
                            'Heap kernel changed C ABI or interrupt mask')
                    require(self.block(LEASE,116)==lease_before,'Read-only heap validator changed lease metadata')
                    return dict(result=self.r[0],instructions=self.instructions,reader_calls=len(self.reader_addresses))
                if pc==READER:
                    require(self.r[4]==0 and not self.r[5]&3,'Coherent reader context/address changed')
                    SH.offset(self.r[5],4);SH.offset(self.r[6],4)
                    self.reader_addresses.append(self.r[5])
                    if not self.reader_failure:self.write(self.r[6],
                        (self.read if self.reader_source is None else self.reader_source)(self.r[5]))
                    self.r[0]=0 if self.reader_failure else 1;self.pc=self.pr;continue
                op=self.instruction(pc);target=None;delay=False;n=(op>>8)&15
                if op>>12 in (0xa,0xb):
                    target=(pc+4+2*signed(op&0xfff,12))&MASK;delay=True
                    if op>>12==0xb:self.pr=pc+4
                elif op&0xf0ff in (0x400b,0x402b):
                    target=self.r[n];delay=True
                    if op&0xf0ff==0x400b:self.pr=pc+4
                elif op==0xb:target=self.pr;delay=True
                elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                    taken=bool(self.sr&1)==(op>>8 in (0x89,0x8d));delay=op>>8 in (0x8d,0x8f)
                    target=pc+4+2*signed(op&255,8) if taken else pc+(4 if delay else 2)
                if target is None:self.plain(op,pc);self.pc=pc+2
                else:
                    self.instructions+=1
                    if delay:
                        slot=self.instruction(pc+2);validate_delay_slot(slot,pc);self.plain(slot,pc+2)
                    self.pc=target&MASK
            raise ValueError('Worker heap guard exceeded bounded instruction count')
        finally:self.active=False;self.tracking=False


def authored(path,anchor,alias,case,direct=False):
    cpu=Worker(path);fixture(cpu,0,0)
    words=list(struct.unpack('<29I',cpu.block(LEASE,116)))
    if direct:
        # A direct owner has no dispatch objects. Its three original code
        # literals witness the addresses of the live control cells instead.
        words[6:8]=[3,2]
        words[13:17]=[words[10]+0xae,words[10]+0x160,0,0]
    begin=0x0c600000|alias;count=0x100000;length=layout_for(cpu.elf[4]).LEASE_BYTES
    if case=='minimum':count=length+64
    if case=='ram-end':begin=0x0cf00000|alias
    header=begin if anchor=='head' else begin+count-length-32
    payload=header+32;units=length//32+1;remaining=count//32-units
    words[22:]=[begin,count,length,header,payload,units,remaining]
    sentinel,table=words[17],words[16]
    cpu.store_block(LEASE,struct.pack('<29I',*words))
    identity_roles=(((words[10]+0x14c,words[19]),(words[10]+0x150,words[21]),
        (words[10]+0x154,words[20])) if direct else
        ((sentinel-4,table),(table,words[13]),(table+4,words[14])))
    for at,value in (*identity_roles,(sentinel+4,0),(sentinel+0x20,sentinel),(sentinel+0x24,begin),(sentinel+0x28,count),
       (sentinel+0x2c,begin),(header+4,units)):cpu.write(at,value)
    free_begin=payload+length if anchor=='head' else begin
    free_end=begin+count if anchor=='head' else header
    blocks=[(free_begin,(free_end-free_begin)//32)]
    candidate=0;expected_identity=expected_walk=1;initializations=1
    if case in ('empty','middle','wrong-header','wrong-payload','wrong-alias','reader-failure'):
        blocks=[]
    if case in ('candidate','candidate-free','candidate-zero','candidate-too-large','candidate-alias','fragmented'):
        c_header=free_begin+0x1000;candidate=c_header+32;cpu.write(c_header+4,2)
        if case=='candidate-free':pass
        elif case=='fragmented':blocks=[(free_begin+i*64,1) for i in range(32)]
        else:blocks=[(free_begin,0x1000//32),(c_header+64,(free_end-c_header-64)//32)]
        if case=='candidate-zero':cpu.write(c_header+4,0);expected_walk=0
        if case=='candidate-too-large':cpu.write(c_header+4,0xffffffff);expected_walk=0
        if case=='candidate-alias':candidate=(candidate&0x1fffffff)|(0xa0000000 if alias!=0xa0000000 else 0x80000000);expected_walk=0
        if case=='candidate-free':expected_walk=0
    if case.startswith('bad-lease-'):
        index,value={
          'version':(0,2),'bytes':(1,112),'phase':(2,1),'initializations':(3,2),
          'begin-alignment':(22,begin+1),'heap-alignment':(23,count-1),
          'length-zero':(24,0),'length-alignment':(24,length+1),'length-large':(24,count),
          'units':(27,units+1),'remaining':(28,remaining+1)}[case[10:]]
        words[index]=value;expected_identity=expected_walk=0
    if case=='middle':words[25:27]=[begin+0x20000,begin+0x20020];cpu.write(words[25]+4,units);expected_identity=expected_walk=0
    if case=='wrong-header':words[25]+=32;cpu.write(words[25]+4,units);expected_identity=expected_walk=0
    if case=='wrong-payload':words[26]+=32;expected_identity=expected_walk=0
    if case=='wrong-alias':words[26]=(payload&0x1fffffff)|(0xa0000000 if alias!=0xa0000000 else 0x80000000);expected_identity=expected_walk=0
    if case=='overflow':words[22]=0x0cfff000|alias;words[23]=0x4000;expected_identity=expected_walk=0
    if case=='initializations':initializations=2;expected_identity=expected_walk=0
    if case=='reader-failure':cpu.reader_failure=True;expected_identity=expected_walk=0
    if case=='free-intrusion':blocks=[(header,units)];expected_walk=0
    if case=='free-cross-lease':blocks=[(header-32 if anchor=='tail' else payload+length-32,2)];expected_walk=0
    if case=='free-zero':blocks=[(free_begin,0)];expected_walk=0
    if case=='free-too-large':blocks=[(free_begin,remaining+1)];expected_walk=0
    if case in ('free-unsorted','free-overlap'):
        blocks=[(free_begin,4),(free_begin+(64 if case=='free-overlap' else 128),1)]
        if case=='free-unsorted':blocks.reverse()
        expected_walk=0
    cpu.store_block(LEASE,struct.pack('<29I',*words))
    cpu.write(sentinel,blocks[0][0] if blocks else sentinel)
    for i,(at,n) in enumerate(blocks):
        cpu.write(at,blocks[i+1][0] if i+1<len(blocks) else sentinel);cpu.write(at+4,n)
    if case=='cursor-node':cpu.write(sentinel+0x20,free_begin)
    if case=='cursor-unseen':cpu.write(sentinel+0x20,free_begin+0x800);expected_walk=0
    if case=='free-cycle':cpu.write(free_begin,free_begin);expected_walk=0
    if case=='free-unaligned':cpu.write(sentinel,free_begin+1);expected_walk=0
    if case=='free-outside':cpu.write(sentinel,(0x0cfff000|alias));expected_walk=0
    if case.startswith('witness-'):
        addresses=[sentinel+0x24,sentinel+0x2c,sentinel+0x28,
            *[at for at,_ in identity_roles],sentinel+4,header+4]
        at=addresses[int(case.split('-')[1])];cpu.write(at,cpu.read(at)^1);expected_identity=expected_walk=0
    if case.startswith('direct-'):
        field,value={'family':(6,1),'layout':(7,1),'dispatch-cell':(15,sentinel-4),
            'dispatch-table':(16,sentinel+0x40),'initializer-alignment':(10,words[10]+2)}[case[7:]]
        words[field]=value;expected_identity=expected_walk=0
        cpu.store_block(LEASE,struct.pack('<29I',*words))
    if case in ('release-lease','release-inside','release-unaligned','release-before','release-after'):
        candidate={'release-lease':payload,'release-inside':payload+32,'release-unaligned':free_begin+33,
                   'release-before':begin,'release-after':begin+count+32}[case];expected_walk=0
    cpu.write(OUTPUT,0x5a3c7e19)
    return cpu,candidate,initializations,expected_identity,expected_walk


CASES=('valid','empty','minimum','ram-end','cursor-node','candidate','fragmented','middle','wrong-header',
 'wrong-payload','wrong-alias','overflow','initializations','reader-failure',
 *('bad-lease-'+n for n in ('version','bytes','phase','initializations','begin-alignment','heap-alignment',
                           'length-zero','length-alignment','length-large','units','remaining')),
 *('witness-'+str(i) for i in range(8)),
 'free-intrusion','free-cross-lease','free-zero','free-too-large','free-unsorted','free-overlap',
 'cursor-unseen','free-cycle','free-unaligned','free-outside','candidate-free','candidate-zero',
 'candidate-too-large','candidate-alias','release-lease','release-inside','release-unaligned','release-before','release-after')
DIRECT_CASES=(*CASES,*('direct-'+name for name in
    ('family','layout','dispatch-cell','dispatch-table','initializer-alignment')))


def execute_cases(path,direct=False):
    rows=[];entered=set()
    for anchor in ('head','tail'):
        for alias in (0,0x80000000,0xa0000000):
            for case in DIRECT_CASES if direct else CASES:
                cpu,candidate,initializations,identity,walk=authored(path,anchor,alias,case,direct)
                first=cpu.call('_kui_vmu_native_heap_lease_identity_valid',[LEASE,READER,0,initializations])
                require(first['result']==identity,'Worker identity result differs: '+str((anchor,hex(alias),case,first)))
                result=cpu.call('_kui_vmu_native_heap_lease_valid_with_release',
                                [LEASE,READER,0,initializations,candidate,OUTPUT])
                require(result['result']==walk,'Worker release walk result differs: '+str((anchor,hex(alias),case,result)))
                output=cpu.read(OUTPUT)
                require(output==(64 if candidate else 0) if walk else output==0x5a3c7e19,
                        'Worker release-size output differs')
                if not candidate:
                    simple=cpu.call('_kui_vmu_native_heap_lease_valid',[LEASE,READER,0,initializations])
                    require(simple['result']==walk,'Worker full lease wrapper result differs')
                entered.update(cpu.executed)
                require(not direct or all(cpu.reader_addresses),'Direct worker read an absent zero dispatch role')
                rows.append(dict(anchor=anchor,alias=hex(alias),case=case,identity=identity,walk=walk,
                                 instructions=first['instructions']+result['instructions']))
    require(entered==set(cpu.closure),'Worker cases did not execute every pinned heap predicate')
    return rows


def direct_profile(worker):
    from check_native_compact_background_profile import worker_profile as compact_profile
    compact=compact_profile(worker)
    session=session_direct_profile(worker)
    ordinary=ordinary_direct_profile(worker)
    require(sum(bool(p) for p in (compact,session,ordinary))<=1,
            'Worker cannot mix compact/session/ordinary General heap profiles')
    return compact or session or ordinary


def session_direct_profile(worker):
    from check_native_session_witness_profile import profile as session_profile
    slot=session_profile(worker)
    if slot is None:return False
    require(slot in (1,2) and worker_profile(worker) and bottom_worker_profile(worker) and
            worker[4].get('__kui_native_dispatch_read_gate_worker')==(1,0,0,0xfff1),
            'Session heap requires its exact General/bottom/dispatch-gated profile')
    return True



def ordinary_direct_profile(worker):
    from check_native_ordinary_general_profile import profile as ordinary_profile
    slot=ordinary_profile(worker)
    if slot is None:return False
    require(slot in ORDINARY_DIRECT_PINS and worker_profile(worker) and bottom_worker_profile(worker) and
            worker[4].get('__kui_native_dispatch_read_gate_worker')==(1,0,0,0xfff1),
            'Ordinary heap requires its exact General/bottom/dispatch-gated profile')
    return True

def require_companion(worker,proof):
    require(isinstance(proof,dict) and proof.get('lease_bytes')==layout_for(worker[4]).LEASE_BYTES,
            'Bottom worker companion runtime geometry differs')
    require(bottom_worker_profile(worker),'Bottom worker companion needs exact worker capability')
    require(isinstance(proof,dict) and proof.get('schema')==SCHEMA and proof.get('enabled') is True and
            proof.get('artifact_sha256')==sha(worker[0]),'Bottom worker companion artifact/schema differs')
    require(proof.get('exact_code_pins')==pinned(worker),'Bottom worker companion lacks exact guard pins')
    require(proof.get('exact_object_pins')==object_pins(worker),'Bottom worker companion lacks exact shape pins')
    require(proof.get('case_count')==len(CASES)*6 and proof.get('anchors')==['head','tail'] and
            proof.get('aliases')==[0,0x80000000,0xa0000000] and proof.get('all_heap_predicates_executed') is True and
            proof.get('no_predicate_stubs') is True,'Bottom worker companion coverage differs')
    rows=proof.get('cases')
    require(isinstance(rows,list) and len(rows)==len(CASES)*6 and
            {(r.get('anchor'),r.get('alias'),r.get('case')) for r in rows if isinstance(r,dict)}==
            {(anchor,hex(alias),case) for anchor in ('head','tail')
             for alias in (0,0x80000000,0xa0000000) for case in CASES} and
            all(isinstance(r,dict) and r.get('identity') in (0,1) and r.get('walk') in (0,1) and
                isinstance(r.get('instructions'),int) and r['instructions']>0 for r in rows),
            'Bottom worker companion authored-case matrix differs')
    require(proof.get('coherent_reader_only_fold') is True and proof.get('owner_and_lease_writes')==0,
            'Bottom worker companion read-only execution differs')
    direct=proof.get('direct_family_cases')
    require(isinstance(direct,list) and len(direct)==(len(DIRECT_CASES)*6 if direct_profile(worker) else 0),
            'Bottom worker companion direct-family coverage differs')
    if direct_profile(worker):
        require({(r.get('anchor'),r.get('alias'),r.get('case')) for r in direct if isinstance(r,dict)}==
            {(anchor,hex(alias),case) for anchor in ('head','tail')
             for alias in (0,0x80000000,0xa0000000) for case in DIRECT_CASES} and
            all(isinstance(r,dict) and r.get('identity') in (0,1) and r.get('walk') in (0,1) and
                isinstance(r.get('instructions'),int) and r['instructions']>0 for r in direct),
            'Bottom worker companion direct-family case matrix differs')
    require(proof.get('target_layout')==dict(zip(('lease.bytes','lease.owner','lease.begin','lease.heap_bytes',
      'lease.lease_bytes','lease.header','lease.payload','lease.units','lease.remaining','owner.bytes'),
      (116,16,88,92,96,100,104,108,112,72))),'Bottom worker companion ABI differs')
    require(proof.get('source_sha256')=={n:sha((ROOT/n).read_bytes()) for n in SOURCES},
            'Bottom worker companion source identities differ')
    return True


def check(directory,self_test=False):
    path=Path(directory)/'native-worker.elf';worker=read_elf(path)
    if not bottom_worker_profile(worker):return dict(schema=SCHEMA,enabled=False)
    pins=pinned(worker);objects=object_pins(worker);abi=layout();rows=execute_cases(path)
    direct_rows=execute_cases(path,True) if direct_profile(worker) else []
    report=dict(schema=SCHEMA,enabled=True,artifact_sha256=sha(worker[0]),exact_code_pins=pins,exact_object_pins=objects,
      lease_bytes=layout_for(worker[4]).LEASE_BYTES,
      target_layout=abi,case_count=len(rows),anchors=['head','tail'],aliases=[0,0x80000000,0xa0000000],
      cases=rows,direct_family_cases=direct_rows,all_heap_predicates_executed=True,no_predicate_stubs=True,
      coherent_reader_only_fold=True,owner_and_lease_writes=0,
      source_sha256={n:sha((ROOT/n).read_bytes()) for n in SOURCES},
      limits=['Authored RAM metadata fixtures execute linked software guards, not a cycle/cache/hardware model.',
              'A disjoint valid allocated release candidate is assumed to come from the unchanged original allocator; arbitrary interior pointers are not generally proved to be legitimate allocations.',
              'Original allocator transactions, cold publication and actual game compatibility require their independent proofs.'])
    require_companion(worker,report)
    if self_test:
        byte_mutants=0
        for name,(_,body,at) in records(worker).items():
            for i in range(len(body)):
                changed=bytearray(worker[0]);changed[at+i]^=1
                try:pinned((bytes(changed),*worker[1:]))
                except ValueError:byte_mutants+=1
                else:raise ValueError('Heap byte pin accepted mutation: '+name)
        object_mutants=0
        if objects:
            from check_native_bottom_heap import records as typed_records
            for name in objects:
                address,body,_,section=typed_records(worker)[name]
                at=section[4]+address-section[3]
                for index in range(len(body)):
                    for bit in range(8):
                        changed=bytearray(worker[0]);changed[at+index]^=1<<bit
                        try:object_pins((bytes(changed),*worker[1:]))
                        except ValueError:object_mutants+=1
                        else:raise ValueError('Worker shape pin accepted changed bit: '+name)
        semantic=0
        for value,case,expected in ((0,'valid',1),(1,'middle',0)):
            cpu,_,initializations,_,_=authored(path,'head',0x80000000,case)
            begin=cpu.closure['_identity@vmu_native_heap.c' if direct_profile(worker)
                else '_bottom_geometry@vmu_native_heap.c'][0]
            cpu.store_block(begin,struct.pack('<3H',0xe000|value,0x000b,0x0009))
            result=cpu.call('_kui_vmu_native_heap_lease_identity_valid',[LEASE,READER,0,initializations])
            require(result['result']!=expected,'Worker semantic fixture missed changed endpoint geometry')
            semantic+=1
        refusals=0
        for field,value in [('schema','bad'),('artifact_sha256','0'*64),('case_count',0),('lease_bytes',0),
                            ('all_heap_predicates_executed',False),('no_predicate_stubs',False),
                            ('exact_code_pins',{}),('source_sha256',{}),('target_layout',{}),
                            ('exact_object_pins',{} if objects else {'invented':{}}),
                            ('cases',[]),('direct_family_cases',[] if direct_rows else [{}]),
                            ('owner_and_lease_writes',1),('coherent_reader_only_fold',False)]:
            changed={**report,field:value}
            try:require_companion(worker,changed)
            except ValueError:refusals+=1
            else:raise ValueError('Worker companion accepted changed field: '+field)
        report['self_tests']=dict(whole_guard_byte_mutants=byte_mutants,
          whole_shape_object_bit_mutants=object_mutants,
          actual_endpoint_semantic_counterfactuals=semantic,companion_refusals=refusals)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path);parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    try:result=check(args.directory,args.self_test)
    except (OSError,ValueError,AssertionError,KeyError,struct.error,subprocess.SubprocessError) as error:
        parser.exit(2,'Bottom worker heap audit refused: '+str(error)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
