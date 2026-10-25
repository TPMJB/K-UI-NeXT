#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked read-ahead cold binder with explicit memory aliases.

The binder, geometry helpers, all branches, cache purges and publication
instructions run as SH instructions. Bounded memory primitives, the separately
tested relocator, code publisher and initialization gateway are strict ABI
substitutes. This is an installer admission proof, not console verification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_sdk_sci_route import ColdBinderCPU,BinderStop,GUARD,RETURN,COOKIE,assert_c_abi
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from test_vmu_native_heap_execution import MASK,signed
from sh4_delay_slots import validate_delay_slot
from vmu_native_relocations import generate

BASE=0x8c421000
SPAN=0x7000
LEASE=0x60000
DATA_OFFSET=0x30000
DATA_BYTES=0x30000
CANARY=0x4b524147
PUBLIC_BYTES=76
DIAGNOSTIC_PUBLIC_BYTES=656
DIAGNOSTIC_MARKER='__kui_native_readahead_diagnostic_auxiliary'
PIO_MARKER='__kui_native_readahead_pio_auxiliary'
PIO_PAIR_MARKER='__kui_native_readahead_pio_pair_auxiliary'
MULTI_OWNER_MARKER='__kui_native_sdk_sci_multi_owner_stage'
# Pins are reviewed against the candidate's complete linked body and literals.
# The combined read-ahead audit also pins the complete module and low table.
PINS={
    '_native_readahead_bind':(1208,'f96dfa876eef36b3682d881905eb70127147191505d380663b89fd6ac1aa07b9'),
    '_kui_vmu_native_background_map_address':(92,'cd9d8aba83db8374543f4850bf079245710ba3854dbf128c98f20c890a9753c8'),
}
DIAGNOSTIC_PINS={
    '_native_readahead_bind':(1208,'c40ba2d564954783d9d4734af094def2f7c5ece92e4954a1855cdf32e5a02aef'),
    '_kui_vmu_native_background_map_address':(92,'cd9d8aba83db8374543f4850bf079245710ba3854dbf128c98f20c890a9753c8'),
}
PIO_PINS={
    '_native_readahead_bind':(1208,'fb4565079ccf9662f82b82294f47feee66e1ca944094d6d5aab191845bca4542'),
    '_kui_vmu_native_background_map_address':(92,'cd9d8aba83db8374543f4850bf079245710ba3854dbf128c98f20c890a9753c8'),
}
PIO_PAIR_PINS={
    '_native_readahead_bind':(1208,'e8fdbae5d43b7851901bfe8a0d313e6921446033cef06018eb0ebe934d92aeb6'),
    '_kui_vmu_native_background_map_address':(92,'cd9d8aba83db8374543f4850bf079245710ba3854dbf128c98f20c890a9753c8'),
}
MULTI_OWNER_PINS={
    '_native_readahead_bind':(1208,'7a663ad2b17d8c3ebd542ce5aa11745fa29eee788046e5941450a9a757dfb431'),
    '_kui_vmu_native_background_map_address':(92,'cd9d8aba83db8374543f4850bf079245710ba3854dbf128c98f20c890a9753c8'),
}


def require(ok,message):
    if not ok:raise ValueError(message)


def physical(address):return address&0x1fffffff


def multi_owner_profile(stage,layout):
    marker=stage[4].get(MULTI_OWNER_MARKER)
    if marker is None:return False
    require(marker==(1,0,0,0xfff1) and layout['pair'] and layout['pio'] and layout['diagnostic'],
            'cold multi-owner stage marker differs or lacks the exact PIO pair profile')
    return True


def auxiliary_layout(aux):
    marker=aux[4].get(DIAGNOSTIC_MARKER)
    diagnostic=marker is not None
    require(not diagnostic or marker==(1,0,0,0xfff1),
            'cold read-ahead diagnostic marker is not absolute one')
    pio_marker=aux[4].get(PIO_MARKER);pio=pio_marker is not None
    require(not pio or (pio_marker==(1,0,0,0xfff1) and diagnostic and
            aux[4].get('__kui_native_readahead_repair_diagnostic_auxiliary')==(1,0,0,0xfff1)),
            'cold read-ahead PIO marker differs or lacks its diagnostic profiles')
    pair_marker=aux[4].get(PIO_PAIR_MARKER);pair=pair_marker is not None
    require(not pair or (pair_marker==(1,0,0,0xfff1) and pio),
            'cold read-ahead PIO pair marker differs or lacks its PIO profile')
    context=aux[4]['_kui_native_readahead_aux_context']
    expected=904 if diagnostic else 172
    require(context[2]==1 and context[1]==expected and not context[0]&31 and
            0xac421040<=context[0]<context[0]+context[1]<=0xac422000 and
            context[3]<len(aux[3]) and aux[3][context[3]][2]&7==3,
            'cold read-ahead full mutable context layout differs')
    return dict(diagnostic=diagnostic,pio=pio,pair=pair,public_bytes=DIAGNOSTIC_PUBLIC_BYTES if diagnostic else PUBLIC_BYTES,
                context_bytes=expected,config=656 if diagnostic else 76,
                cache=688 if diagnostic else 108,initialized=708 if diagnostic else 128)


def flat_aux(aux):
    loads=[p for p in aux[2] if p[0]==1 and p[4]]
    length=max(physical(p[3])+p[4] for p in loads)-physical(BASE)
    require(0x1002<=length<=0x5000,'read-ahead initialized auxiliary span differs')
    output=bytearray(length)
    for _,offset,_,pa,count,_,_,_ in loads:
        at=physical(pa)-physical(BASE)
        require(0<=at<=length-count,'auxiliary load escapes flat initialized image')
        output[at:at+count]=aux[0][offset:offset+count]
    return bytes(output)


class ReadAheadBinderCPU(ColdBinderCPU):
    def __init__(self,stage,resident,aux,*,cache=True,payload=0x8c400000,stream=0,owner_name='Evolution2'):
        super().__init__(stage,resident,cache=cache,payload=payload,stream=stream,owner_name=owner_name)
        self.layout=auxiliary_layout(aux)
        self.aux=aux;self.aux_binary=flat_aux(aux)
        self.aux_wire=generate(aux[0],self.aux_binary,BASE,SPAN)
        self.descriptor=resident[4]['_kui_native_ra_reader_ops'][0]
        self.low_ops=struct.unpack('<13I',bytes(self.read(self.descriptor+i,1) for i in range(52)))
        self.blob_start=stage[4]['_native_ra_aux_blob_start'][0]
        self.blob_end=stage[4]['_native_ra_aux_blob_end'][0]
        self.wire_start=stage[4]['_native_ra_aux_reloc_start'][0]
        self.wire_end=stage[4]['_native_ra_aux_reloc_end'][0]
        require(self.blob_end-self.blob_start==len(self.aux_binary),'stage embeds a different auxiliary length')
        require(bytes(self.read(self.blob_start+i,1) for i in range(len(self.aux_binary)))==self.aux_binary,
                'stage embeds a different auxiliary image')
        require(self.wire_end-self.wire_start==len(self.aux_wire),'stage embeds a different relocation length')
        require(bytes(self.read(self.wire_start+i,1) for i in range(len(self.aux_wire)))==self.aux_wire,
                'stage embeds a different relocation table')
        self.load=((payload&0x1fffffff)|0x80000000)+0x21000
        self.dest=self.load|0x20000000
        self.data=((payload&0x1fffffff)|0xa0000000)+DATA_OFFSET
        self.stack_bottom=((payload&0x1fffffff)|0xa0000000)+0x27000
        self.stack_top=((payload&0x1fffffff)|0xa0000000)+0x28000
        self.put32(self.region+0x500,0);self.put32(self.region+0x504,0)
        self.put32(self.region+0x530,COOKIE)
        # Read-ahead follows successful SDK publication. Seed that installed
        # state faithfully, rather than a cookie with original callback slots.
        self.sdk_vbr=0x8c00f400;self.sdk_previous=self.owner_constants['default']
        self.put32(self.region+0x534,self.sdk_previous);self.put32(self.region+0x538,self.sdk_vbr)
        for at in (self.sdk_vbr+0x25c,self.sdk_vbr+0x260):self.put32(at,self.callback)
        self.expected_sdk=(self.callback,self.callback,self.sdk_previous,self.sdk_vbr,COOKIE)
        self.api={stage[4][name][0]:name for name in ('_memcpy','_memcmp','_memset',
            '_native_report','_retail_display_line','_kui_vmu_native_relocate',
            '_kui_native_passive_publish','_kui_vmu_native_worker_call')}
        self.targets=set(self.api);self.apis=[];self.flushes=[];self.readbacks=[]
        self.relocate_result=0;self.init_result=0;self.aux_mutation=None
        self.init_mutation=None;self.config_mutation=None
        self.ready=False;self.pre_reloc_flushes=[];self.init_calls=0
        installer,installer_bytes,_,_=stage[4]['_native_installer_stack']
        assert installer_bytes==0x10000 and installer&31==0
        self.r[15]=installer+installer_bytes-32
        self.publisher_calls=[];self.writes=[];self.initial=self.snapshot()

    def plain(self,op,pc):
        if op&0xf0ff==0x4010: # DT Rn: decrement and set T exactly on zero.
            n=op>>8&15;self.r[n]=(self.r[n]-1)&MASK;self.flag(self.r[n]==0);return
        if op&0xf00f==0x6008: # SWAP.B: exchange only the low two bytes.
            n,m=op>>8&15,op>>4&15;v=self.r[m]
            self.r[n]=(v&0xffff0000)|((v&255)<<8)|((v>>8)&255);return
        if op&0xf0ff==0x4028: # SHLL16.
            n=op>>8&15;self.r[n]=(self.r[n]<<16)&MASK;return
        if op&0xf00f==0x0006: # MOV.L Rm,@(R0,Rn).
            n,m=op>>8&15,op>>4&15
            self.write((self.r[0]+self.r[n])&MASK,self.r[m]);return
        if op>>8==0x80: # MOV.B R0,@(disp,Rn).
            self.write(self.r[op>>4&15]+(op&15),self.r[0],1);return
        super().plain(op,pc)

    def run_to(self,entry,stop):
        self.pc=entry
        for _ in range(120000):
            if self.pc==stop:return
            pc=self.pc;op=self.read(pc,2);n=op>>8&15
            target=None;delay=False
            if op>>12 in (0xa,0xb):
                target=pc+4+2*signed(op&4095,12);delay=True
                if op>>12==0xb:self.pr=pc+4
            elif op&0xf0ff in (0x400b,0x402b):
                target=self.r[n];delay=True
                if op&0xf0ff==0x400b:self.pr=pc+4
            elif op==0xb:target=self.pr;delay=True
            elif op==0x2b:raise ValueError('cold read-ahead binder must not use RTE')
            elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                taken=bool(self.sr&1)==(op>>8 in (0x89,0x8d))
                delay=op>>8 in (0x8d,0x8f)
                target=pc+4+2*signed(op&255,8) if taken else pc+(4 if delay else 2)
            if target is None:self.plain(op,pc);self.pc=pc+2;continue
            if delay:
                slot=self.read(pc+2,2);validate_delay_slot(slot,pc+2);self.plain(slot,pc+2)
            self.pc=target&MASK
            if self.pc in self.targets:self.callee()
        raise ValueError('read-ahead cold binder exceeded its reviewed instruction bound')

    def _relocate(self):
        args=self.r[4:8]+[self.read(self.r[15]+4*i) for i in range(3)]
        assert args==[self.blob_start,len(self.aux_binary),self.wire_start,len(self.aux_wire),
                      self.dest,SPAN,self.load],list(map(hex,args))
        self.pre_reloc_flushes=self.flushes.copy()
        if self.relocate_result:return self.relocate_result
        image=bytes(self.read(self.blob_start+i,1) for i in range(len(self.aux_binary)))
        wire=bytes(self.read(self.wire_start+i,1) for i in range(len(self.aux_wire)))
        if image!=self.aux_binary or wire!=self.aux_wire:return -2
        changed=bytearray(image);delta=physical(self.load)-physical(BASE)
        for at in range(72,len(wire),8):
            offset,old=struct.unpack_from('<2I',wire,at)
            assert struct.unpack_from('<I',image,offset)[0]==old
            struct.pack_into('<I',changed,offset,(old+delta)&MASK)
        for at in range(0,len(changed),4):
            chunk=changed[at:at+4]
            self.write(self.dest+at,int.from_bytes(chunk,'little'),len(chunk))
        if self.aux_mutation:self.aux_mutation(self)
        return 0

    def _initialize(self):
        self.init_calls+=1
        entry,stack,cfg,unused=self.r[4:8]
        header=[self.read(self.dest+4*i) for i in range(16)]
        assert entry==header[4] and stack==self.stack_top-32
        assert self.r[15]<=cfg<=self.r[15]+256-32
        assert self.r[15]<=unused<=self.r[15]+256-4
        config=[self.read(cfg+4*i) for i in range(8)]
        assert config==[1,32,self.region,self.descriptor,self.data,DATA_BYTES,
                        self.payload&0x1fffffff,LEASE],list(map(hex,config))
        assert header[5]&0xe0000000==0xa0000000
        assert self.dest+64<=header[5]<=self.dest+0x1000-self.layout['public_bytes']
        actual_context=self.dest+physical(self.aux[4]['_kui_native_readahead_aux_context'][0])-physical(BASE)
        assert self.dest+64<=actual_context<=self.dest+0x1000-self.layout['context_bytes']
        assert len(self.publisher_calls)==1
        # Exercise the actual clearing instructions with nonzero prior
        # observations, rather than merely observing the cold zero image.
        for at in range(4,self.layout['public_bytes']):self.write(actual_context+at,0xa5,1)
        if self.layout['diagnostic']:
            for at in range(752,904):self.write(actual_context+at,0xa5,1)
        if self.config_mutation:self.config_mutation(self,cfg,header[5])
        # Execute the actual relocated C initializer on the admitted private
        # stack. Only the enclosing worker-call gateway is substituted.
        before=self.snapshot();owner_sp=self.r[15]
        self.r[15]=stack;self.r[4]=cfg;self.r[5]=unused;self.pr=RETURN+4
        self.run_to(entry,RETURN+4)
        actual=self.r[0]
        assert self.r[8:15]==before[0][8:15] and self.r[15]==stack
        assert self.other==before[1] and self.sr&~1==before[3]&~1
        assert self.control==before[10]
        self.r[15]=owner_sp
        if self.config_mutation:
            assert actual!=0,'actual auxiliary initializer accepted malformed config'
        else:
            assert actual==0,('actual auxiliary initializer refused canonical config',hex(actual))
            assert [self.read(actual_context+self.layout['config']+4*i) for i in range(8)]==config
            assert self.read(actual_context+self.layout['cache'])==self.data
            assert self.read(actual_context+self.layout['cache']+12)==0 # no valid cache blocks
            assert self.read(actual_context+self.layout['initialized'])==1
            assert not any(self.read(actual_context+i,1) for i in range(self.layout['public_bytes']))
            if self.layout['diagnostic']:
                assert not any(self.read(actual_context+i,1) for i in range(752,904))
        if self.init_mutation:self.init_mutation(self,header[5])
        return self.init_result if self.init_result else actual

    def callee(self):
        name=self.api[self.pc];self.apis.append(name);continuation=self.pr;result=0
        if name=='_native_report':raise BinderStop(self.r[4],self.r[5])
        if name=='_memcpy':
            dest,source,count=self.r[4:7];assert count in (4,52)
            data=bytes(self.read(source+i,1) for i in range(count))
            for i,b in enumerate(data):self.write(dest+i,b,1)
            result=dest
        elif name=='_memcmp':
            left,right,count=self.r[4:7];assert count==52
            result=int(any(self.read(left+i,1)!=self.read(right+i,1) for i in range(count)))
        elif name=='_memset':
            dest,value,count=self.r[4:7]
            assert dest==self.dest+len(self.aux_binary) and value==0 and count==SPAN-len(self.aux_binary)
            for i in range(count):self.write(dest+i,0,1)
            result=dest
        elif name=='_kui_vmu_native_relocate':result=self._relocate()
        elif name=='_kui_native_passive_publish':
            first,last=self.r[4:6];assert (first,last)==(self.load,self.load+len(self.aux_binary))
            for at in range(first&~31,last,32):
                physical_at=self.offset(at,1)&~31;entry=self.cache.pop(physical_at,None)
                if entry and entry[1]:self.ram.update((physical_at+i,b) for i,b in enumerate(entry[0]))
            self.publisher_calls.append((first,last))
        elif name=='_kui_vmu_native_worker_call':result=self._initialize()
        else:
            assert name=='_retail_display_line'
            expected=b'BOUNDED READ AHEAD READY / 192 KIB CACHE\0'
            assert bytes(self.read(self.r[4]+i,1) for i in range(len(expected)))==expected
            self.ready=True
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd+len(self.apis))&MASK
        self.r[0]=result&MASK;self.sr^=1;self.pr^=0x13572468;self.pc=continuation

    def patch_low(self,word,value,*,live=True,immutable=True):
        if live:self.put32(self.descriptor+4*word,value)
        if immutable:self.put32(self.blob+self.descriptor-0x8c004000+4*word,value)

    def patch_fixture32(self,address,value):
        # Fixture mutation precedes execution, unlike an emulated P2 store.
        # Retire a line the constructor read while checking embedded bytes.
        self.cache.pop(self.offset(address,4)&~31,None)
        self.put32(address,value)

    def execute(self):
        self.run_symbol('_native_readahead_bind')
        assert self.ready and self.init_calls==1
        assert_c_abi(self,self.initial)
        entry=self.read(self.dest+12);context=self.read(self.dest+20)
        assert self.read((self.region+0x500)|0x20000000)==entry
        assert self.read((self.region+0x504)|0x20000000)==context
        activation=[(address,value) for _,address,value,count in self.writes
                    if address in (self.region+0x500,self.region+0x504) and count==4]
        assert activation==[(self.region+0x504,context),(self.region+0x500,entry)]
        expected=[self.offset(self.load+i,1) for i in range(0,SPAN,32)]
        expected.extend(self.offset(self.data+i,1) for i in range(0,DATA_BYTES,32))
        assert self.pre_reloc_flushes==expected
        assert not any(self.data<=address<self.data+DATA_BYTES for _,address,_,_ in self.writes)
        assert self.read(self.region+0x530)==COOKIE
        assert self.sdk_state()==self.expected_sdk,'read-ahead changed the admitted SDK owner tuple'
        # Any subsequent eviction must preserve the entry/context publication.
        for at,(line,dirty) in tuple(self.cache.items()):
            if dirty:self.ram.update((at+i,b) for i,b in enumerate(line))
        self.cache.clear()
        assert self.read((self.region+0x500)|0x20000000)==entry
        assert self.read((self.region+0x504)|0x20000000)==context
        assert self.sdk_state()==self.expected_sdk,'eviction changed the admitted SDK owner tuple'
        return self

    def sdk_state(self):
        return tuple(self.read(at|0x20000000) for at in
            (self.sdk_vbr+0x25c,self.sdk_vbr+0x260,self.region+0x534,self.region+0x538,self.region+0x530))


def pin_stage(stage,*,diagnostic=False,pio=False,pair=False,multi_owner=False):
    result={}
    require(not pio or diagnostic,'cold PIO stage pins require the diagnostic layout')
    require(not pair or pio,'cold PIO pair stage pins require the PIO profile')
    require(not multi_owner or (pair and stage[4].get(MULTI_OWNER_MARKER)==(1,0,0,0xfff1)),
            'cold multi-owner pins require the exact stage marker and PIO pair')
    selected=MULTI_OWNER_PINS if multi_owner else PIO_PAIR_PINS if pair else PIO_PINS if pio else DIAGNOSTIC_PINS if diagnostic else PINS
    for name,(count,digest) in selected.items():
        address,body,section=symbol_bytes(stage,name,2)
        require(section[2]&4 and 0x8ce00000<=address<address+count<=0x8cfe0000,
                'cold read-ahead code leaves the temporary stage')
        require(len(body)==count and hashlib.sha256(body).hexdigest()==digest,
                'cold read-ahead instruction/literal pin changed: '+name)
        result[name]=(address,count,digest)
    require(bool(result),'cold read-ahead reviewed pins are missing')
    return result


def check_stage(stage,resident,aux,*,self_test=False):
    require(__debug__,'cold read-ahead proof requires Python assertions')
    layout=auxiliary_layout(aux)
    multi_owner=multi_owner_profile(stage,layout)
    def pin_selected(image):return pin_stage(image,diagnostic=layout['diagnostic'],pio=layout['pio'],
                                              pair=layout['pair'],multi_owner=multi_owner)
    pins=pin_selected(stage)
    owners=('Evolution1','Evolution2') if multi_owner else ('Evolution2',)
    positives=negatives=mutations=pin_mutations=layout_mutations=stage_marker_mutations=0
    for cache in (False,True):
        for payload in (0x0c400000,0x8c500000,0xacfa0000):
            for stream in (0,1):
                for owner in owners:
                    ReadAheadBinderCPU(stage,resident,aux,cache=cache,payload=payload,stream=stream,owner_name=owner).execute()
                    positives+=1
    cases=[]
    def case(name,detail,change):cases.append((name,detail,change))
    case('source_entry_misaligned',2,lambda c:c.put32(c.blob+40,c.descriptor|1))
    case('source_descriptor_outside',2,lambda c:c.put32(c.blob+40,0x8c003ffc))
    case('ops_magic',3,lambda c:c.patch_low(0,0))
    case('ops_version',3,lambda c:c.patch_low(1,2))
    case('ops_size',3,lambda c:c.patch_low(2,48))
    case('ops_storage_alignment',3,lambda c:c.patch_low(3,c.cell|1))
    case('ops_storage_code',3,lambda c:c.patch_low(3,c.low_end-4))
    case('ops_storage_guard',3,lambda c:c.patch_low(3,GUARD))
    case('selected_standard_bound',3,lambda c:c.put32(stage[4]['_resident_limit'][0],0x8c007800))
    case('live_entry_disagrees',3,lambda c:c.put32(0x8c004028,0))
    case('live_descriptor_disagrees',3,lambda c:c.patch_low(1,2,immutable=False))
    case('ops_target_odd',4,lambda c:c.patch_low(4,c.low_ops[4]|1))
    case('ops_target_before_code',4,lambda c:c.patch_low(5,0x8c00402a))
    case('ops_target_at_binary_end',4,lambda c:c.patch_low(12,c.low_end))
    case('retained_cell_disagrees',5,lambda c:c.put32(c.cell,c.region+32))
    case('expanded_lease_past_ram',5,lambda c:setattr(c,'r',[*c.r[:4],0x8cfa0020,*c.r[5:]]))
    case('lease_invalid_alias',5,lambda c:setattr(c,'r',[*c.r[:4],0xec400000,*c.r[5:]]))
    case('lease_unaligned',5,lambda c:setattr(c,'r',[*c.r[:4],0x8c400001,*c.r[5:]]))
    case('retained_lease_disagrees',5,lambda c:c.put32(resident[4]['_kui_vmu_observe_boot_control'][0]+12,0))
    case('wrong_control_status',5,lambda c:c.put32(resident[4]['_kui_vmu_observe_boot_control'][0]+8,3))
    case('pending_demand',6,lambda c:c.put32(c.region+0x370,1))
    case('executing_demand',6,lambda c:c.put32(c.region+0x374,1))
    case('engine_active',6,lambda c:c.put32(c.region+0x2bc,1))
    case('reader_in_irq',6,lambda c:c.put32(c.region+0x2cc,1))
    case('reader_hooked',6,lambda c:c.put32(c.region+0x2d0,1))
    case('loan_active',6,lambda c:c.put32(c.region+0x2f4,1))
    case('stream_receiving',6,lambda c:c.put32(c.region+0x470,2))
    case('missing_sdk_cookie',6,lambda c:c.put32(c.region+0x530,0))
    case('preexisting_cache_entry',6,lambda c:c.put32(c.region+0x500,1))
    case('preexisting_cache_context',6,lambda c:c.put32(c.region+0x504,1))
    case('unmasked_installer',6,lambda c:c.set_sr(c.sr&~0xf0))
    case('wrong_relocation_span',7,lambda c:c.patch_fixture32(c.wire_start+20,0x6000))
    case('wrong_relocation_base',7,lambda c:c.patch_fixture32(c.wire_start+24,0x8c422000))
    case('relocation_failure',8,lambda c:setattr(c,'relocate_result',-1))
    case('relocation_wire_corruption',8,lambda c:c.patch_fixture32(c.wire_start+36,0))
    def aux_word(index,value):return lambda c:setattr(c,'aux_mutation',lambda x:x.put32(x.dest+4*index,value(x) if callable(value) else value))
    case('aux_magic',9,aux_word(0,0))
    case('aux_version',9,aux_word(1,2))
    case('aux_size',9,aux_word(2,60))
    case('aux_entry_in_header',9,aux_word(3,lambda c:c.load+64))
    case('aux_init_at_image_end',9,aux_word(4,lambda c:c.load+len(c.aux_binary)))
    case('aux_context_p1',9,aux_word(5,lambda c:c.load+64))
    case('aux_context_alignment',9,aux_word(5,lambda c:c.dest+65))
    case('aux_context_at_code',9,aux_word(5,lambda c:c.dest+0x1000))
    case('aux_context_public_prefix_past_page',9,aux_word(5,
        lambda c:(c.dest+0x1000-c.layout['public_bytes']+32)&~31))
    case('aux_image_end_disagrees',9,aux_word(6,lambda c:c.load+len(c.aux_binary)+32))
    case('aux_stack_begin_disagrees',9,aux_word(7,lambda c:c.stack_bottom+32))
    case('aux_stack_top_disagrees',9,aux_word(8,lambda c:c.stack_top-32))
    case('aux_memory_end_disagrees',9,aux_word(9,lambda c:c.stack_top+32))
    case('aux_reserved_nonzero',9,aux_word(10,1))
    case('init_failure',10,lambda c:setattr(c,'init_result',1))
    case('init_busy',10,lambda c:setattr(c,'init_mutation',lambda x,context:x.write(context,1)))
    case('init_low_guard_damage',10,lambda c:setattr(c,'init_mutation',lambda x,context:x.write(x.stack_bottom,0)))
    case('init_high_guard_damage',10,lambda c:setattr(c,'init_mutation',lambda x,context:x.write(x.stack_top-16,0)))
    def bad_config(word,value):
        return lambda c:setattr(c,'config_mutation',lambda x,cfg,context:x.write(cfg+4*word,value(x,context) if callable(value) else value))
    case('actual_init_version',10,bad_config(0,2))
    case('actual_init_size',10,bad_config(1,28))
    case('actual_init_region',10,bad_config(2,lambda c,context:c.region+32))
    case('actual_init_data_alias',10,bad_config(4,lambda c,context:c.data&~0x20000000))
    case('actual_init_data_context_overlap',10,bad_config(4,lambda c,context:context))
    case('actual_init_data_size',10,bad_config(5,DATA_BYTES-512))
    case('actual_init_lease',10,bad_config(6,lambda c,context:(c.payload&0x1fffffff)+32))
    case('actual_init_full_context_past_page',10,bad_config(6,lambda c,context:
        (c.payload&0x1fffffff)-((0x1000-c.layout['context_bytes']-64)//32+1)*32))
    case('actual_init_legacy_lease_size',10,bad_config(7,0x30000))
    case('actual_init_busy',10,lambda c:setattr(c,'config_mutation',
        lambda x,cfg,context:x.write(context,1)))
    case('actual_init_initialized',10,lambda c:setattr(c,'config_mutation',
        lambda x,cfg,context:x.write(context+x.layout['initialized'],1)))
    for cache,owner in ((cache,owner) for cache in (False,True) for owner in owners):
        for name,detail,change in cases:
            c=ReadAheadBinderCPU(stage,resident,aux,cache=cache,owner_name=owner);change(c)
            sdk_before=c.sdk_state()
            try:c.execute()
            except BinderStop as stop:
                assert (stop.phase,stop.detail)==(11,detail),(name,stop)
                assert not c.ready
                assert not any(address in (c.region+0x500,c.region+0x504) for _,address,_,_ in c.writes)
                assert c.sdk_state()==sdk_before,'refused read-ahead changed the SDK owner tuple'
                negatives+=1
            else:raise ValueError('cold read-ahead binder accepted malformed '+name)
    if self_test:
        context_name='_kui_native_readahead_aux_context'
        symbol_mutations=[]
        context=aux[4][context_name]
        for index,value in ((0,context[0]|1),(0,0x8c421040),(0,0xac421fc0),
                            (1,context[1]-4),(1,904 if not layout['diagnostic'] else 172),
                            (2,2),(3,0xfff1)):
            record=list(context);record[index]=value
            symbol_mutations.append((context_name,tuple(record)))
        if layout['diagnostic']:
            for marker in (None,(0,0,0,0xfff1),(1,4,0,0xfff1),(1,0,1,0xfff1),(1,0,0,1)):
                symbol_mutations.append((DIAGNOSTIC_MARKER,marker))
        else:symbol_mutations.append((DIAGNOSTIC_MARKER,(1,0,0,0xfff1)))
        if layout['pio']:
            for marker in ((0,0,0,0xfff1),(1,4,0,0xfff1),(1,0,1,0xfff1),(1,0,0,1)):
                symbol_mutations.append((PIO_MARKER,marker))
            symbol_mutations.append(('__kui_native_readahead_repair_diagnostic_auxiliary',None))
        if layout['pair']:
            for marker in ((0,0,0,0xfff1),(1,4,0,0xfff1),(1,0,1,0xfff1),(1,0,0,1)):
                symbol_mutations.append((PIO_PAIR_MARKER,marker))
            symbol_mutations.append((PIO_MARKER,None))
        for name,record in symbol_mutations:
            symbols=aux[4].copy()
            if record is None:symbols.pop(name,None)
            else:symbols[name]=record
            try:auxiliary_layout((*aux[:4],symbols))
            except ValueError:layout_mutations+=1
            else:raise ValueError('cold read-ahead audit accepted changed context/diagnostic schema')
        for name,(address,count,_) in pins.items():
            _,_,section=symbol_bytes(stage,name,2)
            start=section[4]+address-section[3]
            for i in range(count):
                changed=bytearray(stage[0]);changed[start+i]^=1
                try:pin_selected((bytes(changed),*stage[1:]))
                except ValueError:pin_mutations+=1
                else:raise ValueError('cold read-ahead audit accepted changed reviewed instructions')
        address,body,section=symbol_bytes(stage,'_native_readahead_bind',2)
        at=[i for i in range(0,len(body),2) if struct.unpack_from('<H',body,i)[0]&0xf0ff==0x00a3]
        require(len(at)==4,'cold read-ahead publisher OCBP instruction count changed')
        for i,detail in zip(at[-2:],(11,12)):
            data=bytearray(stage[0]);start=section[4]+address-section[3]
            struct.pack_into('<H',data,start+i,0x0009)
            c=ReadAheadBinderCPU((bytes(data),*stage[1:]),resident,aux,cache=True)
            try:c.execute()
            except BinderStop as stop:
                assert (stop.phase,stop.detail)==(11,detail),stop;mutations+=1
            else:raise ValueError('cold read-ahead accepted omitted activation publication')
        if multi_owner:
            for marker in (None,(0,0,0,0xfff1),(1,4,0,0xfff1),(1,0,1,0xfff1),(1,0,0,1)):
                symbols=stage[4].copy()
                if marker is None:symbols.pop(MULTI_OWNER_MARKER)
                else:symbols[MULTI_OWNER_MARKER]=marker
                changed=(*stage[:4],symbols)
                try:
                    selected=multi_owner_profile(changed,layout)
                    pin_stage(changed,diagnostic=layout['diagnostic'],pio=layout['pio'],pair=layout['pair'],multi_owner=selected)
                except ValueError:stage_marker_mutations+=1
                else:raise ValueError('cold read-ahead accepted changed multi-owner stage marker')
    return dict(actual_linked_cold_binder_executed=True,positive_cases=positives,
        malformed_descriptor_context_aux_and_init_cases=negatives,omitted_activation_ocbp_rejections=mutations,
        reviewed_body_byte_mutation_rejections=pin_mutations,
        context_and_diagnostic_schema_mutation_rejections=layout_mutations,
        multi_owner_stage_marker_mutation_rejections=stage_marker_mutations,
        exact_instruction_literal_pins=pins,p1_cache_and_p2_ram_distinct=True,
        cold_aux_and_cache_lines_purged=(SPAN+DATA_BYTES)//32,cache_data_not_initialized_or_trusted=True,
        context_published_before_entry=True,guards_checked_after_init=True,
        memcpy_memcmp_memset='faithful bounded ABI substitutes',
        relocator='exact separately tested input image/table; explicit pointer-record substitute',
        code_publisher='exact owned span and cache-flush substitute; actual publisher independently audited',
        initializer_gateway='strict private-SP gateway substitute; actual relocated auxiliary C init executed',
        actual_auxiliary_c_initializer_executed=True,
        diagnostic_auxiliary=layout['diagnostic'],public_prefix_bytes=layout['public_bytes'],
        pio_auxiliary=layout['pio'],
        pio_pair_auxiliary=layout['pair'],
        multi_owner_stage=multi_owner,exact_sdk_owners_tested=list(owners),
        admitted_sdk_callback_default_vbr_cookie_preserved=True,
        sdk_owner_scope='Begins after independently proved SDK admission and callback publication',
        complete_owned_context_bytes=layout['context_bytes'],
        actual_initializer_clears_complete_public_prefix=True,
        actual_initializer_copies_owned_config_values_not_stage_pointer=True,
        actual_initializer_starts_with_no_valid_cache_blocks=True,
        console_boot_verified=False,console_performance_verified=False)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--self-test',action='store_true');a=p.parse_args()
    try:
        s,r,x=(read_elf(a.directory/name) for name in ('stage.elf','resident-sci.elf','native-ra-aux.elf'))
        result=check_stage(s,r,x,self_test=a.self_test)
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as e:
        p.exit(2,'Read-ahead cold binder audit refused: '+str(e)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
