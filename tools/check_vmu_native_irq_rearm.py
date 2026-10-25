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
VBR_CHANGES=REGION+0x278
HOOK_SHA='3617673b84fa38f4341fc072ca4485a1a13d748dbfa0fe3ef9799d4a539884af'
TIMELINE_HOOK_SHA='a02b6467322a9aa97cfe8c7f57aa713573d0b17ec8558645285a1c3192796e07'
PRIORITY7_HOOK_SHA='42f77ad89a90755953c14afd1d8ced039b24ee825990d57fa3efd65e16b36e55'
UNHOOK_SHA={1:'d705da7a67919d14f75d58793e0f095d69150557c43334675da202707f745e0d',
            7:'3c2f7406fd6ab8a43585efe8ebc2e1c42b4f33d77da7b11c6ee96d9a0582410e'}
RELEASE_SHA='f3ecff9cf5d93f81046f29f3a0b81741cc031478bd8439ac3a2dfa2f11342715'
REHOOK_SHA={1:'aed4b2e5f0665337f11867eb58ffd64d971fa3f349da7a4a1960b3f02b36160c',
            7:'ea24a0abf5d1ca45512731d0e78d1c4536e847edcd58fe747287bb0a51386c50'}
PRIORITY_MARKER='__kui_native_sci_priority7_'


def require(test,message):
    if not test:raise ValueError(message)


class Rearm(CacheGateway):
    def __init__(self,resident,seed,mask,same,hooked,field,body=None,*,same_owner=False):
        begin,original,_=symbol_bytes(resident,'_hook',2)
        CPU.__init__(self,seed,begin,1)
        self.pc,self.blob,self.mem=begin,original if body is None else body,{}
        self.pr=begin+len(original)+0x100
        self.r[15]=0x8cff0000;self.sr=(self.sr&~0xf0)|(mask<<4)
        self.before=self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy()
        self.targets={}
        self.pointer=resident[4]['_kui_retail_async_region_storage'][0]
        self.timeline='__kui_native_loader_timeline_resident' in resident[4]
        self.level=7 if PRIORITY_MARKER+'resident' in resident[4] else 1
        self.selected=self.level<<4
        self.vbr=REGION-0x100 if same else 0x8c00f400
        self.iprb=(random.Random(seed+992).getrandbits(16)&~0xf0)|field
        self.mem.update({self.pointer:REGION,FRAME+4:0x70,FRAME:self.vbr if same_owner else 0x8c00e800,
                         FRAME+8:1,HOOKED:int(hooked),REARMS:(0xfffffffc+seed)&MASK})
        if self.timeline:self.mem[VBR_CHANGES]=(0xfffffffe+seed)&MASK
        self.old=self.vbr,self.iprb,self.mem.copy()
        self.same,self.hooked,self.field=same,hooked,field
        self.iprb_writes,self.vbr_writes=[],[]

    def read(self,address):
        if address in self.mem:return self.mem[address]
        return struct.unpack('<I',self.code(address,4))[0]

    def write(self,address,value):
        allowed=(FRAME,FRAME+4,FRAME+8,HOOKED,REARMS)+((VBR_CHANGES,) if self.timeline else ())
        assert address in allowed,'hook store escapes exact reader fields'
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
        changed=self.field!=self.selected
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
            if self.timeline and self.hooked and mem[FRAME]!=vbr:
                expected[VBR_CHANGES]=(mem[VBR_CHANGES]+1)&MASK
        assert self.mem==expected,'hook corrupts owner/reader state or loses a rearm'
        wrote=not self.same or changed
        assert self.iprb_writes==([(iprb&~0xf0)|self.selected] if wrote else [])
        assert self.iprb==((iprb&~0xf0)|self.selected),'SCI field must be exactly the selected level; preserve all other fields'


class Unhook(Rearm):
    def __init__(self,resident,seed,mask,same,hooked,field,body=None):
        super().__init__(resident,seed,mask,same,hooked,field)
        begin,original,_=symbol_bytes(resident,'_unhook',2)
        self.base=self.pc=begin;self.blob=original if body is None else body
        self.pr=begin+len(original)+0x100
        self.before=self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy()

    def check(self):
        r,pr,sr,fpscr,fr=self.before
        assert self.r[8:]==r[8:] and self.pr==pr and self.sr&~1==sr&~1
        assert (self.fpscr,self.fr)==(fpscr,fr)
        vbr,iprb,mem=self.old;expected=mem.copy();expected[FRAME+8]=0
        if self.hooked:expected[HOOKED]=0
        assert self.mem==expected,'unhook changed saved owner or other reader state'
        restore=self.hooked and self.field==self.selected
        desired=(iprb&~0xf0)|mem[FRAME+4] if restore else iprb
        assert self.iprb==desired and self.iprb_writes==([desired] if restore else [])
        restore_vbr=self.hooked and self.same
        assert self.vbr==(mem[FRAME] if restore_vbr else vbr)
        assert self.vbr_writes==([mem[FRAME]] if restore_vbr else [])


def linked_bytes(image,name,count):
    """Assembly entry labels have zero ELF size; admit an exact bounded span."""
    address,size,kind,owner=image[4][name]
    require(size==0 and kind==0 and owner<len(image[3]),'invalid assembly vector entry '+name)
    section=image[3][owner]
    require(section[1]==1 and section[2]&6==6 and
            section[3]<=address<address+count<=section[3]+section[5],
            'unbounded assembly vector entry '+name)
    at=section[4]+address-section[3]
    return address,image[0][at:at+count]


def vector_pins(resident,level):
    begin,release=linked_bytes(resident,'_kui_retail_release_100',152)
    rehook_begin,rehook=linked_bytes(resident,'_kui_retail_rehook',128)
    pointer=resident[4]['_kui_retail_async_region_storage'][0]
    require(resident[4]['_kui_retail_release_400'][0]==begin+10 and
            resident[4]['_kui_retail_release_600'][0]==begin+20 and rehook_begin==begin+152,
            'native release/rehook entries moved within their exact assembly body')
    require(hashlib.sha256(release[:-12]).hexdigest()==RELEASE_SHA and
            release[-12:]==struct.pack('<III',pointer,IPRB,rehook_begin),
            'native release instructions or saved-owner bindings changed')
    require(hashlib.sha256(rehook[:-12]).hexdigest()==REHOOK_SHA[level] and
            rehook[-12:]==struct.pack('<III',pointer,pointer,IPRB),
            'native rehook instructions or selected SCI level/bindings changed')
    require(struct.unpack_from('<H',rehook,0x58)[0]==(0xca00|(level<<4)),
            'native rehook must restore the same SCI level as the C hook')
    return dict(release_bytes=len(release),rehook_bytes=len(rehook),
                exact_saved_owner_release_pinned=True,selected_level_rehook_pinned=True)


def check(resident,*,worker=None,self_test=False):
    require(__debug__,'rearm instruction model requires Python assertions; run without -O/PYTHONOPTIMIZE')
    marker=resident[4].get('__kui_native_irq_rearm_resident')
    priority=resident[4].get(PRIORITY_MARKER+'resident')
    assembly=resident[4].get(PRIORITY_MARKER+'rehook')
    worker_priority=worker[4].get(PRIORITY_MARKER+'worker') if worker is not None else None
    require(bool(priority)==bool(assembly),'SCI priority7 C/assembly marker mismatch')
    if worker is not None:
        require(bool(priority)==bool(worker_priority),'SCI priority7 worker/resident marker mismatch')
    if priority:
        require(worker is not None,'SCI priority7 requires the paired worker artifact')
        require(priority==assembly==worker_priority==(7,0,0,0xfff1),
                'invalid paired SCI priority7 experiment markers')
    if marker is None:
        require(not priority,'SCI priority7 requires native IRQ rearm')
        return {'enabled':False}
    require(marker==(1,0,0,0xfff1),'invalid native IRQ rearm marker')
    begin,body,_=symbol_bytes(resident,'_hook',2)
    timeline=resident[4].get('__kui_native_loader_timeline_resident')
    require(timeline is None or timeline==(1,0,0,0xfff1),'invalid native timeline marker')
    require(not priority or timeline,'SCI priority7 requires the native timeline profile')
    level=7 if priority else 1
    expected_size,expected_sha=(148,PRIORITY7_HOOK_SHA if priority else TIMELINE_HOOK_SHA) if timeline else (132,HOOK_SHA)
    require(len(body)==expected_size and hashlib.sha256(body[:-8]).hexdigest()==expected_sha,
            'linked native IRQ rearm hook instructions changed')
    require(body[-8:]==struct.pack('<II',resident[4]['_kui_retail_async_region_storage'][0],IPRB),
            'linked native rearm region/IPRB binding changed')
    _,unhook,_=symbol_bytes(resident,'_unhook',2)
    require(len(unhook)==92 and hashlib.sha256(unhook[:-8]).hexdigest()==UNHOOK_SHA[level] and
            unhook[-8:]==body[-8:],'linked native unhook instructions/SCI owner binding changed')
    vectors=vector_pins(resident,level)
    cases=unhook_cases=0
    for seed in range(16):
        for mask in range(16):
            for same in (False,True):
                for hooked in (False,True):
                    for field in range(0,0x100,0x10):
                        for same_owner in ((False,True) if timeline else (False,)):
                            Rearm(resident,seed,mask,same,hooked,field,same_owner=same_owner).run().check();cases+=1
                        Unhook(resident,seed,mask,same,hooked,field).run().check();unhook_cases+=1
    rejected=vector_rejected=unhook_rejected=0
    if self_test:
        mutations=[]
        patterns=(0xcb00|(level<<4),0x8800|(level<<4),0x2501,0x2101,0x7301,0x1302,0x472e)
        if timeline:patterns+=(0x452e,0x7701,0x117f,0x3730)
        for pattern in patterns:
            for at in range(0,len(body)-8,2):
                if struct.unpack_from('<H',body,at)[0]==pattern:
                    altered=bytearray(body);struct.pack_into('<H',altered,at,9)
                    mutations.append(bytes(altered))
        altered=bytearray(body);struct.pack_into('<I',altered,len(body)-4,IPRB+4);mutations.append(bytes(altered))
        for altered in mutations:
            refused=False
            for same in (True,False):
                for same_owner in ((False,True) if timeline else (False,)):
                    try:Rearm(resident,7,15,same,True,0x90,altered,same_owner=same_owner).run().check()
                    except (AssertionError,ValueError,struct.error,KeyError):refused=True;break
                if refused:break
            require(refused,'native IRQ rearm model accepted an unsafe mutation');rejected+=1
        for at in range(0,len(unhook)-8,2):
            word=struct.unpack_from('<H',unhook,at)[0]
            if word in (0x8800|(level<<4),0x2101,0x2501):
                altered=bytearray(unhook);struct.pack_into('<H',altered,at,9)
                refused=False
                for same in (False,True):
                    for field in (level<<4,0x90):
                        try:Unhook(resident,7,15,same,True,field,altered).run().check()
                        except (AssertionError,ValueError,struct.error,KeyError):refused=True;break
                    if refused:break
                require(refused,'native unhook model accepted an unsafe ownership mutation')
                unhook_rejected+=1
        # The entire release/rehook span is pinned, including every pointer
        # literal and the priority immediate. Single-byte corruption must
        # never be hidden by choosing the other experiment's instruction pin.
        for name,count in (('_kui_retail_release_100',152),('_kui_retail_rehook',128)):
            address=resident[4][name][0];section=resident[3][resident[4][name][3]]
            offset=section[4]+address-section[3]
            for at in range(count):
                altered=bytearray(resident[0]);altered[offset+at]^=1
                image=(bytes(altered),*resident[1:])
                try:vector_pins(image,level)
                except ValueError:vector_rejected+=1
                else:raise ValueError('native vector pin accepted a changed instruction/literal')
    return dict(enabled=True,hook_address=hex(begin),hook_bytes=len(body),
        complete_linked_hook_pinned=True,actual_instruction_model_cases=cases,
        actual_unhook_instruction_model_cases=unhook_cases,all_16_caller_imasks_checked=True,
        mutation_rejections=rejected,all_sci_fields_checked=True,
        unhook_mutation_rejections=unhook_rejected,vector_pin_mutation_rejections=vector_rejected,
        same_vbr_priority_rechecked=True,other_iprb_fields_preserved=True,
        vbr_change_counter_modelled=bool(timeline),
        owner_updated_priority_retained_for_release=True,reader_sci_level=level,
        priority7_paired_markers=bool(priority),**vectors,
        unchanged_level_causes_no_mmio_write=True,new_resident_ram_bytes=0,
        console_cause_confirmed=False,console_performance_verified=False)
