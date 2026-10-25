#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual native hook: independent VBR/IPRB changes and release.

This proves the linked rearm predicate and owner bookkeeping. It does not
claim that a particular game has cleared IPRB or that it fixes console lag.
"""
import hashlib
import random
import struct

from check_vmu_native_cache_gateway import CacheGateway
from check_vmu_native_worker_gateway import CPU, MASK, symbol_bytes

IPRB=0xffd00008
REGION=0x8c420400
FRAME=REGION+0x240
HOOKED=REGION+0x2d0
REARMS=REGION+0x2e4
HOOK_SHA='3617673b84fa38f4341fc072ca4485a1a13d748dbfa0fe3ef9799d4a539884af'


def require(test,message):
    if not test:raise ValueError(message)


class Rearm(CacheGateway):
    def __init__(self,resident,seed,mask,same,hooked,field,body=None):
        begin,original,_=symbol_bytes(resident,'_hook',2)
        CPU.__init__(self,seed,begin,1)
        self.pc,self.blob,self.mem=begin,original if body is None else body,{}
        self.pr=begin+len(original)+0x100
        self.r[15]=0x8cff0000;self.sr=(self.sr&~0xf0)|(mask<<4)
        self.before=self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy()
        self.targets={}
        self.pointer=resident[4]['_kui_retail_async_region_storage'][0]
        self.vbr=REGION-0x100 if same else 0x8c00f400
        self.iprb=(random.Random(seed+992).getrandbits(16)&~0xf0)|field
        self.mem.update({self.pointer:REGION,FRAME+4:0x70,FRAME:0x8c00e800,
                         FRAME+8:1,HOOKED:int(hooked),REARMS:(0xfffffffc+seed)&MASK})
        self.old=self.vbr,self.iprb,self.mem.copy()
        self.same,self.hooked,self.field=same,hooked,field
        self.iprb_writes,self.vbr_writes=[],[]

    def read(self,address):
        if address in self.mem:return self.mem[address]
        return struct.unpack('<I',self.code(address,4))[0]

    def write(self,address,value):
        assert address in (FRAME,FRAME+4,FRAME+8,HOOKED,REARMS),'hook store escapes exact reader fields'
        self.mem[address]=value&MASK

    def plain(self,w,pc):
        n,m=w>>8&15,w>>4&15
        if w&0xf0ff==0x0022:self.r[n]=self.vbr;return
        if w&0xf0ff==0x402e:
            self.vbr=self.r[n];self.vbr_writes.append(self.vbr);return
        if w&0xf00f==0x6001:
            assert self.r[m]==IPRB,'unexpected MMIO read'
            self.r[n]=(self.iprb-(0x10000 if self.iprb&0x8000 else 0))&MASK;return
        if w&0xf00f==0x2001:
            assert self.r[n]==IPRB,'unexpected MMIO write'
            self.iprb=self.r[m]&0xffff;self.iprb_writes.append(self.iprb);return
        if w&0xf00f==0x300c:self.r[n]=(self.r[n]+self.r[m])&MASK;return
        if w>>8==0xcb:self.r[0]|=w&255;return
        super().plain(w,pc)

    def check(self):
        r,pr,sr,fpscr,fr=self.before
        assert self.r[8:]==r[8:] and self.pr==pr and self.sr&~1==sr&~1
        assert (self.fpscr,self.fr)==(fpscr,fr)
        vbr,iprb,mem=self.old
        changed=self.field!=0x10
        expected=mem.copy()
        if self.same:
            assert not self.vbr_writes and self.vbr==vbr
            if changed:
                expected[FRAME+4]=self.field
                expected[REARMS]=(mem[REARMS]+1)&MASK
        else:
            assert self.vbr_writes==[REGION-0x100] and self.vbr==REGION-0x100
            expected[FRAME]=vbr;expected[FRAME+8]=1;expected[HOOKED]=1
            if not self.hooked or changed:expected[FRAME+4]=self.field
        assert self.mem==expected,'hook corrupts owner/reader state or loses a rearm'
        wrote=not self.same or changed
        assert self.iprb_writes==([(iprb&~0xf0)|0x10] if wrote else [])
        assert self.iprb==((iprb&~0xf0)|0x10),'SCI field must be exactly lowest level; preserve all other fields'


def check(resident,*,self_test=False):
    marker=resident[4].get('__kui_native_irq_rearm_resident')
    if marker is None:return {'enabled':False}
    require(marker==(1,0,0,0xfff1),'invalid native IRQ rearm marker')
    begin,body,_=symbol_bytes(resident,'_hook',2)
    require(len(body)==132 and hashlib.sha256(body[:-8]).hexdigest()==HOOK_SHA,
            'linked native IRQ rearm hook instructions changed')
    require(body[-8:]==struct.pack('<II',resident[4]['_kui_retail_async_region_storage'][0],IPRB),
            'linked native rearm region/IPRB binding changed')
    cases=0
    for seed in range(16):
        for mask in (0,1,8,15):
            for same in (False,True):
                for hooked in (False,True):
                    for field in range(0,0x100,0x10):
                        Rearm(resident,seed,mask,same,hooked,field).run().check();cases+=1
    rejected=0
    if self_test:
        mutations=[]
        for pattern in (0xcb10,0x2501,0x2101,0x7301,0x1302,0x472e):
            for at in range(0,114,2):
                if struct.unpack_from('<H',body,at)[0]==pattern:
                    altered=bytearray(body);struct.pack_into('<H',altered,at,9)
                    mutations.append(bytes(altered))
        altered=bytearray(body);struct.pack_into('<I',altered,128,IPRB+4);mutations.append(bytes(altered))
        for altered in mutations:
            refused=False
            for same in (True,False):
                try:Rearm(resident,7,15,same,True,0x90,altered).run().check()
                except (AssertionError,ValueError,struct.error,KeyError):refused=True;break
            require(refused,'native IRQ rearm model accepted an unsafe mutation');rejected+=1
    return dict(enabled=True,hook_address=hex(begin),hook_bytes=len(body),
        complete_linked_hook_pinned=True,actual_instruction_model_cases=cases,
        mutation_rejections=rejected,all_sci_fields_checked=True,
        same_vbr_priority_rechecked=True,other_iprb_fields_preserved=True,
        owner_updated_priority_retained_for_release=True,reader_sci_level=1,
        unchanged_level_causes_no_mmio_write=True,new_resident_ram_bytes=0,
        console_cause_confirmed=False,console_performance_verified=False)
