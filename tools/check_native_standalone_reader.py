#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked standalone first binder against original DOA2 SDK RAM.

The binder and original SDK initializer execute their real scalar instructions.
P1 data uses a separate write-back cache; P2 reads observe backing RAM. The
deep SD reader payload in callback ABI cases remains an explicit adversary.
This proves neither SD hardware performance nor console game compatibility.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_sdk_sci_route import SDKCPU,IPRB,RETURN,COOKIE,PRIVATE_TOP
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from prove_native_sdk_sci_structural_runtime import original_contract,original_dispatch
from test_vmu_native_heap_execution import MASK,signed
from sh4_delay_slots import validate_delay_slot

SCHEMA='kui.native.standalone-reader.execution.v1'
SOURCES=('tools/check_native_standalone_reader.py','tools/check_vmu_native_sdk_sci_route.py',
    'tools/prove_native_sdk_sci_structural_runtime.py','tests/test_vmu_native_sci_sdk_restore.py',
    'tests/test_vmu_native_heap_execution.py','tests/test_native_sdk_sci_owner_contract.py',
    'tests/test_native_sdk_sci_helper_contract.py','tools/sh4_delay_slots.py',
    'src/loader/retail_async.c','src/loader/retail_async.h',
    'src/loader/native_standalone_reader.inc','src/loader/retail_resident.c',
    'src/loader/retail_resident.S','include/kui/native_sdk_sci_runtime.h',
    'include/kui/native_standalone_reader.h','src/loader/retail_stage.c',
    'src/loader/native_standalone_reader_stage.inc','src/core/native_standalone_admission.c',
    'include/kui/native_standalone_admission.h','Makefile.native-standalone-doa2',
    'src/loader/retail_stage.S','src/loader/retail_resident_async_low.ld',
    'tools/native_standalone_symbols.py','tools/check_vmu_native_worker_gateway.py',
    'tools/check_loader_layout.py','tests/test_native_standalone_admission.c',
    'src/loader/sd_reader.c','src/loader/retail_sd.c','src/loader/retail_storage.c',
    'src/loader/sci_sd_bus.c','src/core/ata.c','src/dreamcast/ata_bus.c',
    'src/loader/minic.c','src/core/retail_image.c','src/loader/retail_display.c',
    'src/core/hash.c','src/core/data.c','src/core/retail_gd.c','src/core/retail_cursor.c',
    'src/loader/sci_stream.c','src/loader/retail_entry.S','src/loader/retail_stage.ld',
    'src/loader/retail_entry.ld','include/kui/retail_image.h','include/kui/retail_gd.h',
    'include/kui/retail_loader_layout.h','src/loader/sci_stream.h','src/loader/sci_sd_bus.h')

def require(value,message):
    if not value:raise ValueError(message)

def sha(data):return hashlib.sha256(data).hexdigest()


class BinderCPU(SDKCPU):
    """Immutable instruction fetch plus scalar write-back/P2 alias model.

    Instruction fetch reads immutable backing code; no instruction-cache timing
    or replacement behavior is modeled. Only scalar P1 data reads fill D lines.
    """
    def __init__(self,resident,owner,*,cache=True,common=1):
        super().__init__(resident,seed=11,mask=15,bank=0,owner=owner['image'],
                         previous=owner['default'],vbr=owner['vbr'])
        self.owner=owner;self.region=self.symbols['_standalone_reader_region'][0]
        self.ram=dict(self.overlay);self.cache={};self.cache_on=cache
        self.flushes=[];self.readbacks=[];self.data_reads=[];self.fault_readback=None
        self.ccr=0x921;self.ccr_writes=[]
        self.external_fetch=False
        self.put(self.pointer,self.region)
        for i in range(0,0x760,4):self.put(self.region+i,0)
        for i,value in enumerate((owner['vector'],*owner['commons'],owner['default'],0x53524431)):
            self.put(self.region+4*i,value)
        self.put(self.region+0x534,owner['default'])
        self.put(self.region+0x378,1);self.put(self.active,1)
        self.put(self.eri_slot,0);self.put(self.rxi_slot,0)
        self.r[15]=PRIVATE_TOP;self.pr=RETURN;self.targets=set()
        self.control['vbr']=owner['vbr'];self.sr=0x400001f0
        # The full original initializer is replayed separately. This bounded
        # source-derived copied-vector fixture also covers both original modes.
        for i in range(24):
            self.put(owner['vbr']+0x600+4*i,self.read(owner['vector']+4*i))
        self.put(owner['vbr']+0x658,owner['commons'][common])
        self.put(self.eri_slot,owner['default']);self.put(self.rxi_slot,owner['default'])
        self.reset_trace()

    def put(self,address,value,count=4):
        at=self.offset(address,count)
        self.ram.update((at+i,b) for i,b in enumerate((value&((1<<(8*count))-1)).to_bytes(count,'little')))

    def ram_bytes(self,at,count):return bytes(self.ram.get(at+i,self.memory[at+i]) for i in range(count))

    def line(self,at):
        line=at&~31
        if line not in self.cache:self.cache[line]=[bytearray(self.ram_bytes(line,32)),False]
        return self.cache[line]

    def read(self,address,count=4):
        if not hasattr(self,'ram'):return super().read(address,count)
        if address==IPRB:return super().read(address,count)
        if address==0xff00001c:
            require(count==4,'Original CCR access width differs');return self.ccr
        at=self.offset(address,count)
        if address&0xe0000000==0x80000000 and self.cache_on:
            data=bytes(self.line(at+i)[0][(at+i)&31] for i in range(count))
        else:data=self.ram_bytes(at,count)
        value=int.from_bytes(data,'little')
        if hasattr(self,'data_reads'):self.data_reads.append((self.executing,address,count))
        if address in (self.eri_slot|0x20000000,self.rxi_slot|0x20000000):
            self.readbacks.append((self.executing,address,value))
            if self.fault_readback==address:return value^1
        return value

    def write(self,address,value,count=4):
        if not hasattr(self,'ram'):return super().write(address,value,count)
        if address==IPRB:return super().write(address,value,count)
        if address==0xff00001c:
            require(count==4,'Original CCR access width differs')
            self.ccr=value&MASK;self.ccr_writes.append((self.executing,self.ccr));return
        value&=(1<<(8*count))-1;self.writes.append((self.executing,address,value,count))
        at=self.offset(address,count)
        if address&0xe0000000==0x80000000 and self.cache_on:
            for i,b in enumerate(value.to_bytes(count,'little')):
                entry=self.line(at+i);entry[0][(at+i)&31]=b;entry[1]=True
        else:self.put(address,value,count)

    def plain(self,op,pc):
        self.executing=pc;n=op>>8&15
        if op&0xf0ff in (0x00a3,0x0093,0x00b3):
            line=self.offset(self.r[n],1)&~31;entry=self.cache.pop(line,None)
            if op&0xf0ff!=0x0093 and entry and entry[1]:
                self.ram.update((line+i,b) for i,b in enumerate(entry[0]))
            self.flushes.append((pc,self.r[n]&~31,op&0xf0ff));return
        if op&0xf0ff==0x00e3:return # ICBI has no scalar-data effect.
        if op&0xf0ff==0x4010:
            self.r[n]=(self.r[n]-1)&MASK;self.flag(self.r[n]==0);return
        if op&0xf00f==0x3003:
            self.flag(signed(self.r[n],32)>=signed(self.r[op>>4&15],32));return
        if op&0xf00f==0x600b:self.r[n]=(-self.r[op>>4&15])&MASK;return
        if op>>8==0xc8:self.flag(not(self.r[0]&(op&255)));return
        if op&0xf00f in (0x6004,0x6005):
            m=op>>4&15;count=1 if op&15==4 else 2;address=self.r[m]
            self.r[n]=signed(self.read(address,count),8*count)&MASK
            if n!=m:self.r[m]=(address+count)&MASK
            return
        if op&0xf00f in (0x000c,0x000d):
            count=1 if op&15==12 else 2
            self.r[n]=signed(self.read((self.r[0]+self.r[op>>4&15])&MASK,count),8*count)&MASK;return
        if op&0xf00f in (0x6000,0x6001):
            count=1 if op&15==0 else 2
            self.r[n]=signed(self.read(self.r[op>>4&15],count),8*count)&MASK;return
        if op>>8==0x84:self.r[0]=signed(self.read(self.r[op>>4&15]+(op&15),1),8)&MASK;return
        if op&0xf00f==0x600c:self.r[n]=self.r[op>>4&15]&255;return
        super().plain(op,pc)

    def run_to(self,entry,stop,limit=100000):
        self.pc=entry;retired=self.instructions
        for _ in range(limit):
            if self.pc==stop:return
            pc=self.pc;op=int.from_bytes(self.ram_bytes(self.offset(pc,2),2),'little')
            n=op>>8&15;target=None;delay=False
            retired+=1;self.instructions=retired
            if op>>12 in (0xa,0xb):
                target=pc+4+2*signed(op&4095,12);delay=True
                if op>>12==0xb:self.pr=pc+4
            elif op&0xf0ff in (0x400b,0x402b):
                target=self.r[n];delay=True
                if op&0xf0ff==0x400b:self.pr=pc+4
            elif op==0xb:target=self.pr;delay=True
            elif op==0x2b:target=self.control['spc'];delay=True
            elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                taken=bool(self.sr&1)==(op>>8 in (0x89,0x8d));delay=op>>8 in (0x8d,0x8f)
                target=pc+4+2*signed(op&255,8) if taken else pc+(4 if delay else 2)
            if target is None:
                self.plain(op,pc);self.instructions=retired;self.pc=pc+2;continue
            if delay:
                slot=int.from_bytes(self.ram_bytes(self.offset(pc+2,2),2),'little')
                validate_delay_slot(slot,pc+2);self.plain(slot,pc+2)
                retired+=1;self.instructions=retired
            if op==0x2b:self.set_sr(self.control['ssr'])
            self.pc=target&MASK
            if self.pc in self.targets:self.callee()
        raise ValueError('Standalone instruction replay exceeded its bound at '+hex(self.pc))

    def reset_trace(self):
        self.writes=[];self.flushes=[];self.readbacks=[];self.data_reads=[];self.instructions=0

    def bind(self):
        self.pr=RETURN;before=self.snapshot();self.reset_trace()
        self.run_symbol('_kui_native_standalone_bind')
        require(self.r[8:]==before[0][8:] and self.other==before[1] and self.pr==before[2] and
                (self.sr&~1)==(before[3]&~1) and self.control==before[10] and
                (self.gbr,self.fpscr,self.fpul,self.fr)==(before[6],before[7],before[8],before[9]),
                'Binder changed preserved C ABI/control state')
        return self

    def ready_ram(self):
        return int.from_bytes(self.ram_bytes(self.offset(self.region+0x530,4),4),'little')


def assert_publication(c):
    external=[(a,v,n) for _,a,v,n in c.writes if not PRIVATE_TOP-128<=a<PRIVATE_TOP]
    require(external==[(c.region+0x538,c.owner['vbr'],4),(c.eri_slot,c.callback,4),
                       (c.rxi_slot,c.callback,4),(c.region+0x530,COOKIE,4)],
            'First binder published unexpected memory or sequence')
    require([a for _,a,_ in c.flushes]==[(c.region+0x534)&~31,c.eri_slot&~31,
                                      c.rxi_slot&~31,(c.region+0x530)&~31] and
            all(op==0xa3 for _,_,op in c.flushes),'Binder OCBP publication sequence differs')
    require([a for _,a,_ in c.readbacks]==[c.eri_slot|0x20000000,c.rxi_slot|0x20000000] and
            c.ready_ram()==COOKIE,'Cookie became ready without complete coherent callback publication')
    require(c.read(c.eri_slot|0x20000000)==c.read(c.rxi_slot|0x20000000)==c.callback,
            'Published slots are stale in RAM')
    require(not c.iprb_writes and not c.vbr_writes,'Binder changed SCI priority or VBR')


def publication_cases(resident,owner):
    rows=[]
    for cache in (False,True):
        for common in (0,1):
            c=BinderCPU(resident,owner,cache=cache,common=common)
            # Distinct dirty neighboring words ensure whole-line OCBP preserves
            # pending cached data while writing the admitted slots/metadata.
            c.write(c.eri_slot-4,0xabc00123);c.write(c.rxi_slot+4,0xabc00456)
            c.write(c.region+0x53c,0xabc00789)
            c.bind();assert_publication(c)
            require(c.read((c.eri_slot-4)|0x20000000)==0xabc00123 and
                    c.read((c.rxi_slot+4)|0x20000000)==0xabc00456 and
                    c.read((c.region+0x53c)|0x20000000)==0xabc00789,
                    'Binder discarded neighboring dirty data')
            first=c.instructions;c.bind()
            require(not [a for _,a,_,_ in c.writes if not PRIVATE_TOP-128<=a<PRIVATE_TOP],
                    'First-install-only binder wrote on a repeated visit')
            rows.append(dict(cache=cache,common=hex(owner['commons'][common]),
                first_instructions=first,repeat_instructions=c.instructions,ready=True,
                first_install_only=True,dirty_neighbors_preserved=True))
    return rows


def refusal_cases(resident,owner):
    rows=[]
    mutations={
        'missing_descriptor':lambda c:c.put(c.region+16,0),
        'source_vector':lambda c:c.put(c.region,0),
        'source_normal':lambda c:c.put(c.region+4,0),
        'source_fpu':lambda c:c.put(c.region+8,0),
        'source_default':lambda c:c.put(c.region+12,0),
        'retained_default':lambda c:c.put(c.region+0x534,0),
        'owner_already_set':lambda c:c.put(c.region+0x538,owner['vbr']),
        'cookie_already_set':lambda c:c.put(c.region+0x530,COOKIE),
        'no_resident_lock':lambda c:c.put(c.active,0),
        'wrong_resident_lock':lambda c:c.put(c.active,2),
        'not_initialized':lambda c:c.put(c.region+0x378,0),
        'gd_pending':lambda c:c.put(c.region+0x370,1),
        'gd_executing':lambda c:c.put(c.region+0x374,1),
        'reader_active':lambda c:c.put(c.region+0x2bc,1),
        'reader_irq':lambda c:c.put(c.region+0x2cc,1),
        'reader_hooked':lambda c:c.put(c.region+0x2d0,1),
        'release_armed':lambda c:c.put(c.region+0x248,1),
        'release_depth':lambda c:c.put(c.region+0x24c,1),
        'bus_fault':lambda c:c.put(c.region+0x4ec,1,1),
        'opened_mismatch':lambda c:c.put(c.region+0x2c4,1),
        'acquired_mismatch':lambda c:c.put(c.region+0x4eb,1,1),
        'stream_receiving':lambda c:c.put(c.region+0x470,2),
        'stream_unknown':lambda c:c.put(c.region+0x470,3),
        'eri_owned':lambda c:c.put(c.eri_slot,0x8c123456),
        'rxi_owned':lambda c:c.put(c.rxi_slot,0x8c123456),
        'bad_common':lambda c:c.put(owner['vbr']+0x658,0x8c123456),
        'bad_source_common':lambda c:c.write(owner['vector']+0x58,0x8c123456),
    }
    for word in range(24):
        if word in (20,22):continue
        mutations['vector_word_'+str(word)]=lambda c,w=word:c.put(owner['vbr']+0x600+4*w,c.read(owner['vector']+4*w)^1)
    for mask in range(15):mutations['mask_'+str(mask)]=lambda c,m=mask:setattr(c,'sr',0x40000100|(m<<4))
    mutations['user_mode']=lambda c:setattr(c,'sr',0x1f0)
    mutations['foreign_vbr']=lambda c:c.control.update(vbr=0x8cc80000)
    for name,mutate in mutations.items():
        c=BinderCPU(resident,owner);mutate(c);c.bind()
        require(not [a for _,a,_,_ in c.writes if not PRIVATE_TOP-128<=a<PRIVATE_TOP] and
                not c.flushes and not c.iprb_writes and not c.vbr_writes,
                'Refused first binder changed external state: '+name)
        rows.append(dict(name=name,external_writes=0,publication=False,instructions=c.instructions))
    for address in (owner['eri_slot']|0x20000000,owner['rxi_slot']|0x20000000):
        c=BinderCPU(resident,owner);c.fault_readback=address;c.bind()
        require(c.ready_ram()==0 and all(a!=c.region+0x530 for _,a,_,_ in c.writes),
                'Failed coherent readback published readiness')
        rows.append(dict(name='readback_'+hex(address),publication=False,
            partial_slots_forward_only=True,instructions=c.instructions))
    return rows


def original_initialization(resident,owner):
    c=BinderCPU(resident,owner);c.cache={}
    # Before source SDK initialization its vector/table are unready. Actual
    # binder must leave all of them untouched, then accept only after the real
    # original scalar initializer has run, copied/published vectors and slots.
    for at in range(0x1c8,0x2c4,4):c.put(owner['vbr']+at,0)
    for at in range(0x600,0x660,4):c.put(owner['vbr']+at,0)
    c.bind();require(c.ready_ram()==0 and not c.flushes,'Pre-SDK bind was admitted')
    c.r[4]=0x80000000;c.r[5]=0x8c312a00;c.pr=RETURN;c.reset_trace()
    c.run_to(0x8c123250,RETURN)
    original_instructions=c.instructions
    require(c.read(c.eri_slot)==c.read(c.rxi_slot)==owner['default'],
            'Actual original initializer did not publish its SCI defaults')
    require(c.read(owner['vbr']+0x658)==owner['commons'][1],
            'Actual original initializer did not select its expected FPU common')
    c.r[15]=PRIVATE_TOP;c.sr=0x400001f0;c.bind();assert_publication(c)
    c.write(c.active,0);c.iprb=0xa50f;c.run_symbol('_hook')
    require(c.iprb==0xa51f and not c.vbr_writes,'Published route did not enable the real SCI1 hook')
    c.calls=0;c.targets={c.symbols['_kui_retail_async_irq'][0]};c.callee_mode='deep-reader';c.callee_return=0
    c.r[15]=0x8c00ed00;c.r[0]=c.sr;c.write(owner['event'],0x4e0)
    c.run_to(owner['dispatch'],c.callback)
    require(c.r[4]==0x4e0 and c.pr==owner['continuation'],'Original SDK did not reach newly installed callback')
    before=c.snapshot();c.run_symbol('_kui_retail_sdk_sci_entry',owner['continuation'])
    require(c.calls==1 and c.r[8:]==before[0][8:] and c.pr==before[2] and c.sr==before[3],
            'Real published route/dispatcher did not preserve original callback ABI')
    return dict(entry='0x8c123250',return_instruction='0x8c123422',
        actual_original_instructions=original_instructions,pre_init_no_write=True,
        copied_vector_and_table_initializer_executed=True,binder_executed_without_stub=True,
        published_only_after_original_initialization=True,real_readiness_hook_and_dispatch_executed=True,
        original_lookup_reaches_newly_published_callback=True,deep_reader_c='explicit ABI adversary',
        full_fpu_frame_executed=False)


class StageStop(Exception):pass


class StageCPU(BinderCPU):
    """Actual stage guards/publisher; SHA256 compression and CRC are pure leaves."""
    def __init__(self,stage,resident,owner):
        super().__init__(resident,owner,cache=False)
        self.resident=resident;self.stage=stage;self.symbols=stage[4]
        for kind,offset,address,_,count,_,_,_ in stage[2]:
            if kind==1 and count:self.put_block(address,stage[0][offset:offset+count])
        self.r[15]=0x8cfeffe0;self.pr=RETURN;self.sr=0x400001f0;self.ccr=0
        self.sha_contexts={};self.leaves=[];self.sha_inputs=[];self.publisher_visits=0
        self.api={stage[4][name][0]:name for name in ('_kui_sha256_init','_kui_sha256_update',
            '_kui_sha256_digest','_kui_retail_crc32','_stopped','_relay_stopped','_retail_display_pause')}
        self.targets=set(self.api)
        self.blob=stage[4]['__retail_resident_sci_blob_start'][0]
        self.blob_bytes=resident[4]['__retail_resident_binary_end'][0]-0x8c004000
        for name,value in (('_resident_blob',self.blob),('_resident_bytes',self.blob_bytes),
                           ('_resident_limit',0x8c007ba0),('_exec_bytes',len(owner['image'])),
                           ('_boot_crc',zlib.crc32(owner['image']))):self.put(stage[4][name][0],value)
        for i in range(5):self.put(self.region+4*i,0)
        for i in (0x530,0x534,0x538):self.put(self.region+i,0)
        entry_bytes=stage[4]['__retail_trampoline_end'][0]-stage[4]['__retail_trampoline_start'][0]
        require(entry_bytes==128,'Standalone boot trampoline size changed')
        self.put_block(stage[4]['_original_entry'][0],owner['image'][:entry_bytes])
        self.frame=0x8c00e000
        for i in range(21):self.put(self.frame+4*i,0)
        self.put(self.frame+12,owner['vbr']);self.put(self.frame+16,0x600000f0)
        self.reset_trace()

    def put_block(self,address,data):
        at=self.offset(address,len(data));self.ram.update((at+i,b) for i,b in enumerate(data))

    def plain(self,op,pc):
        if pc==self.symbols['_standalone_source_publish'][0]:self.publisher_visits+=1
        return super().plain(op,pc)

    def callee(self):
        name=self.api[self.pc];self.leaves.append(name);continuation=self.pr;result=0
        if name in ('_stopped','_relay_stopped'):raise StageStop(name,self.r[4],self.r[5])
        if name=='_kui_sha256_init':self.sha_contexts[self.r[4]]=hashlib.sha256()
        elif name=='_kui_sha256_update':
            ctx,address,count=self.r[4:7]
            data=bytes(self.read(address+i,1) for i in range(count))
            self.sha_contexts[ctx].update(data);self.sha_inputs.append((address,count,sha(data)))
        elif name=='_kui_sha256_digest':
            self.put_block(self.r[5],self.sha_contexts[self.r[4]].digest())
        elif name=='_kui_retail_crc32':
            seed,address,count=self.r[4:7]
            result=zlib.crc32(bytes(self.read(address+i,1) for i in range(count)),seed)
        else:require(name=='_retail_display_pause','Unexpected stage proof leaf')
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd+len(self.leaves))&MASK
        self.r[0]=result&MASK;self.sr^=1;self.pr^=0x13572468;self.pc=continuation

    def call(self,name,args=()):
        self.r[4:4+len(args)]=args;self.pr=RETURN;self.reset_trace()
        self.run_to(self.symbols[name][0],RETURN,limit=1000000)
        return self


def stage_cases(stage,resident,owner):
    # Exercise the actual pure launch selector; failure must retain the entire
    # decoded manifest and success may change only its reader word.
    selector=[];manifest=stage[4]['_manifest'][0]
    def fresh_manifest(c):
        data=bytearray(1020)
        for off,value in ((44,1),(48,0),(40,len(owner['image'])),(24,2),(28,2)):
            struct.pack_into('<I',data,off,value)
        c.put_block(manifest,data);return bytes(data)
    for name,off,value in (('valid',None,None),('transport',44,0),('reader_x',48,1),
        ('reader_y',48,2),('size',40,len(owner['image'])-1),('tracks_zero',24,0),
        ('tracks_large',24,65),('extents_zero',28,0),('extents_large',28,63),
        ('cd',52,1),('scrambled',52,2)):
        c=StageCPU(stage,resident,owner);before=fresh_manifest(c)
        if off is not None:c.put(manifest+off,value);before=bytes(c.read(manifest+i,1) for i in range(1020))
        c.call('_kui_native_standalone_launch_prepare',(manifest,))
        after=bytes(c.read(manifest+i,1) for i in range(1020))
        expected=bytearray(before)
        if name=='valid':struct.pack_into('<I',expected,48,1)
        require(c.r[0]==int(name=='valid') and after==expected,'Standalone decoded selector mismatch: '+name)
        selector.append(dict(name=name,accepted=name=='valid',only_decoded_reader_changed=True))
    # The real SHA admission function and its stage wrapper execute; only pure
    # SHA compression is delegated with exact bytes, count and output digest.
    identities=[]
    for name in ('valid','source_bit','wrong_bytes'):
        c=StageCPU(stage,resident,owner)
        if name=='source_bit':c.put(0x8c010100,c.read(0x8c010100)^1)
        if name=='wrong_bytes':c.put(stage[4]['_exec_bytes'][0],len(owner['image'])-1)
        refused=False
        try:c.call('_standalone_source_check')
        except StageStop:refused=True
        require(refused==(name!='valid') and c.publisher_visits==0,'Stage exact-image refusal differs: '+name)
        identities.append(dict(name=name,refused=refused,sha_calls=len(c.sha_inputs)))
    # Run the complete real relay: original entry restore, whole-source CRC,
    # exact resident comparison, full SHA admission, actual P2 descriptor stores.
    c=StageCPU(stage,resident,owner)
    trampoline=stage[4]['__retail_trampoline_start'][0]
    c.put_block(0x8c010000,bytes(c.read(trampoline+i,1) for i in range(128)))
    require(bytes(c.read(0x8c010000+i,1) for i in range(128))!=owner['image'][:128],
            'Relay fixture failed to replace the original entry with its real trampoline')
    c.call('_kui_retail_stage_relay',(c.frame,0))
    require(bytes(c.read(0x8c010000+i,1) for i in range(128))==owner['image'][:128],
            'Complete relay failed to restore the actual original entry')
    expected=[(c.region|0x20000000)+4*i for i in range(4)]
    expected+=[(c.region+0x534)|0x20000000,(c.region+16)|0x20000000]
    stores=[(a,v,n) for _,a,v,n in c.writes if a&0x1fffffff in
            {((c.region+i)&0x1fffffff) for i in (0,4,8,12,16,0x534)}]
    require([a for a,_,_ in stores]==expected and [v for _,v,_ in stores]==
        [owner['vector'],*owner['commons'],owner['default'],owner['default'],0x53524431],
        'Actual relay source descriptor differs or was published early')
    require(c.publisher_visits==1 and c.sha_inputs==[(0x8c010000,len(owner['image']),owner['digest'])],
            'Complete relay did not pass exact full SHA before publication')
    expected_physical={a&0x1fffffff for a in expected}
    for _,address,_,count in c.writes:
        physical=address&0x1fffffff
        require((0x0ce00000<=physical<0x0cffffff) or
                (0x0c010000<=physical and physical+count<=0x0c010080) or
                (physical in expected_physical and count==4),
                'Actual stage relay wrote outside its stage/entry/descriptor authority')
    require(not c.iprb_writes and not c.vbr_writes and
            not any(a&0x1fffffff in (owner['eri_slot']&0x1fffffff,owner['rxi_slot']&0x1fffffff)
                    for _,a,_,_ in c.writes),'Stage activated SCI routing before the game SDK initializer')
    refusals=[]
    mutations={
        'cache_data':lambda x:setattr(x,'ccr',1),'cache_instruction':lambda x:setattr(x,'ccr',0x100),
        'mask14':lambda x:setattr(x,'sr',0x400001e0),
        'wrong_limit':lambda x:x.put(stage[4]['_resident_limit'][0],0x8c007b80),
        'binary_overlaps_region':lambda x:x.put(stage[4]['_resident_bytes'][0],x.region-0x8c004000+4),
        'entry_descriptor':lambda x:x.put(0x8c004024,0),
        'region_pointer':lambda x:x.put(resident[4]['_kui_retail_async_region_storage'][0],x.region+32),
    }
    for i in range(5):mutations['descriptor_'+str(i)]=lambda x,i=i:x.put(x.region+4*i,1)
    for off in (0x530,0x534,0x538):mutations['owned_'+hex(off)]=lambda x,off=off:x.put(x.region+off,1)
    for i in range(5):
        mutations['export_'+str(i)]=lambda x,i=i:x.put(resident[4]['_kui_native_sdk_sci_route_exports'][0]+4*i,0)
    for name,mutate in mutations.items():
        trial=StageCPU(stage,resident,owner);mutate(trial);refused=False
        try:trial.call('_standalone_source_publish')
        except StageStop:refused=True
        require(refused and not [a for _,a,_,_ in trial.writes if trial.region<=a&~0x20000000<trial.region+0x760],
                'Invalid stage publication changed retained data: '+name)
        refusals.append(dict(name=name,refused=True,retained_writes=0))
    for name,mutate in (
        ('source',lambda x:x.put(0x8c010100,x.read(0x8c010100)^1)),
        ('resident',lambda x:x.put(0x8c004100,x.read(0x8c004100)^1))):
        trial=StageCPU(stage,resident,owner);mutate(trial);refused=False
        try:trial.call('_kui_retail_stage_relay',(trial.frame,0))
        except StageStop:refused=True
        require(refused and trial.publisher_visits==0,'Changed relay input reached source publisher')
        refusals.append(dict(name='relay_'+name,refused=True,publisher_visits=0))
    return dict(status='PASS',actual_linked_selector=selector,actual_sha_admission=identities,
        complete_relay_executed=True,full_source_crc_and_sha_before_publication=True,
        complete_resident_byte_comparison_before_publication=True,p2_descriptor_store_order=stores,
        actual_publication_refusals=refusals,only_pure_hash_crc_and_display_leaves_substituted=True,
        temporary_stage_pointers_retained=False,sci_slots_untouched_by_stage=True)


def audit(args):
    provenance=json.loads((args.build/'entry.compile.json').read_text())
    require(args.development or (provenance['commit']==args.source_commit and
            provenance['source_dirty'] is False),'Standalone artifacts lack clean frozen provenance')
    require(provenance['elf_sha256']==sha((args.build/'entry.elf').read_bytes()),
            'Standalone entry differs from its build provenance')
    resident=read_elf(args.build/'resident-sci.elf');owner=original_contract(args.startup)
    require(owner['digest']=='105056d9ffef86bf5e470fa28dc5907443156f9ab1c6bc009f73661d9c047a85',
            'Standalone execution proof requires exact reviewed DOA2 source')
    syms=resident[4];end=syms['__retail_resident_bss_end'][0];bottom=syms['__retail_hook_stack_bottom'][0]
    require(syms.get('__kui_native_standalone_reader_resident')==(1,0,0,0xfff1),
            'Standalone low resident capability marker differs')
    require(syms['_start'][0]==0x8c004000 and bottom==0x8c007ba0 and end<=bottom,
            'Standalone reader escaped the existing low reservation')
    require(syms['_standalone_reader_region'][1]==0x760 and syms['_manifest'][1]==1020,
            'Static retained reader map/region geometry differs')
    require(not any('readahead' in n or 'passive' in n or 'vmu_observe' in n for n in syms),
            'Standalone resident contains VMU/high-worker/read-ahead symbols')
    _,binder,_=symbol_bytes(resident,'_kui_native_standalone_bind',2)
    stage=read_elf(args.build/'stage.elf')
    require(all(stage[4].get(name)==(1,0,0,0xfff1) for name in
        ('__kui_native_standalone_reader_stage','__kui_native_standalone_doa2_stage')),
        'Standalone stage capability markers differ')
    demand_path=args.proof_dir/'standalone-demand.json'
    demand=json.loads(demand_path.read_text())
    require(demand['schema']=='kui.native.standalone-reader-demand-model.v1' and demand['result']=='PASS',
            'Missing actual demand delivery companion')
    require(demand['modes']['ubsan']['actual_c_first_binding'] is True and
            demand['modes']['ubsan']['route_preinstalled_by_fixture'] is False and
            demand['modes']['asan-ubsan']['route_preinstalled_by_fixture'] is True,
            'Demand companion conceals its first-bind/substitution boundaries')
    for name,digest in demand['source_sha256'].items():
        require(sha((ROOT/name).read_bytes())==digest,'Demand companion source changed: '+name)
    for mode in demand['modes'].values():
        require(mode['result']['byte_exact_requests']>0 and mode['result']['modeled_sci_entries']>0 and
                mode['result']['vbr_writes']==0 and mode['result']['hardware_executed'] is False and
                sha(Path(mode['executable']).read_bytes())==mode['sha256'],
                'Demand companion executable/result binding differs')
    host=args.proof_dir/'stage-admission-host.log'
    host_text=host.read_text();match=re.search(r'Standalone exact DOA2/SCI/admission: (\d+) checks PASS',host_text)
    require(match is not None,'Missing host exact-source/launch-admission validation')
    host_path=args.proof_dir/'stage-admission-host.json';host_proof=json.loads(host_path.read_text())
    require(host_proof['schema']=='kui.native.standalone-reader-stage-admission-host.v1' and
            host_proof['status']=='PASS' and host_proof['input_sha256']==owner['digest'] and
            host_proof['checks']==int(match[1]) and host_proof['log_sha256']==sha(host.read_bytes()),
            'Host stage admission companion/input/log differs')
    for name,digest in host_proof['source_sha256'].items():
        require(sha((ROOT/name).read_bytes())==digest,'Host stage admission source changed: '+name)
    return dict(schema=SCHEMA,status='PASS',source_commit=args.source_commit,launchable=True,
        standalone_reader=True,virtual_vmu=False,readahead=False,heap_reservation_bytes=0,
        development=args.development,
        artifact_sha256={name:sha((args.build/name).read_bytes()) for name in
            ('entry.elf','stage.elf','resident-sci.elf') if (args.build/name).is_file()},
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCES},
        input_sha256=sha(args.startup.read_bytes()),
        geometry=dict(bss_end=hex(end),stack_bottom=hex(bottom),spare_bytes=bottom-end,
            region=hex(syms['_standalone_reader_region'][0]),region_bytes=0x760,
            manifest=hex(syms['_manifest'][0]),manifest_bytes=1020),
        binder=dict(symbol='_kui_native_standalone_bind',bytes=len(binder),sha256=sha(binder),
            actual_instructions=True,no_success_or_admission_stub=True,
            publication_cases=publication_cases(resident,owner),refusals=refusal_cases(resident,owner),
            original_initialization=original_initialization(resident,owner)),
        stage_model=stage_cases(stage,resident,owner),original_callback=original_dispatch(resident,args.startup),
        companions=dict(demand=dict(file=demand_path.name,sha256=sha(demand_path.read_bytes()),
            schema=demand['schema'],actual_production_demand_model=True,
            ubsan=demand['modes']['ubsan']['result'],asan_ubsan=demand['modes']['asan-ubsan']['result']),
            stage_admission_host=dict(file=host_path.name,sha256=sha(host_path.read_bytes()),
                schema=host_proof['schema'],log=host.name,log_sha256=sha(host.read_bytes()),checks=int(match[1]))),
        reviewed_lifetime='Exact stage-verified DOA2 SDK code remains unchanged until late bind; no new game heap ownership.',
        limitations=__doc__.strip())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','startup','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--proof-dir',type=Path,required=True)
    p.add_argument('--source-commit',required=True)
    p.add_argument('--development',action='store_true',help='Pilot inspection only; delivery requires a clean frozen build')
    args=p.parse_args();result=audit(args)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(status='PASS',publication_cases=len(result['binder']['publication_cases']),
        refusals=len(result['binder']['refusals']),spare_bytes=result['geometry']['spare_bytes'],
        output=str(args.output))))

if __name__=='__main__':main()
