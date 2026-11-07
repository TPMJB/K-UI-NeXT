#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the emitted CE halfword sink with bounded authored RAM/VRAM spans.

The closed integer decoder rejects calls, unknown instructions, byte/wide
payload stores, extra memory access and callee-saved register damage. It models
neither CE page translation nor physical bus/cache timing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_vmu_native_worker_gateway import read_elf,symbol_bytes

MASK=0xffffffff
def signed(n,bits):return n-(1<<bits) if n&(1<<(bits-1)) else n

class Sink:
    def __init__(self,address,code,destination,source,count):
        self.address=address;self.code=code;self.destination=destination;self.source=source;self.count=count
        self.pc=address;self.end=0xdead0000;self.r=[0x13570000+i*0x123 for i in range(16)]
        self.r[4:7]=[destination,source,count];self.r[15]=0x8c100000;self.before=self.r.copy()
        self.t=0;self.macl=0;self.steps=0;self.stores=[];self.reads=[];self.stack={};self.stack_min=self.r[15]
        self.expected=[(i*109+7)&255 for i in range(count)]
        self.memory={source+i:v for i,v in enumerate(self.expected)}
        if count:
            for at in range(destination&~1,(destination+count+1)&~1):self.memory[at]=0x5a
    def fetch(self,at):
        if not self.address<=at<=self.address+len(self.code)-2:raise ValueError('code escapes sink')
        return struct.unpack_from('<H',self.code,at-self.address)[0]
    def read(self,at,width):
        if at&(width-1):raise ValueError('unaligned read')
        self.reads.append((at,width))
        if width==4 and at in self.stack:return self.stack[at]
        if self.address<=at and at+width<=self.address+len(self.code):
            return int.from_bytes(self.code[at-self.address:at-self.address+width],'little')
        if any(at+i not in self.memory for i in range(width)):raise ValueError('unowned source/destination read')
        return sum(self.memory[at+i]<<(8*i) for i in range(width))
    def write(self,at,value,width):
        if at&(width-1):raise ValueError('unaligned write')
        if width==4 and self.before[15]-8<=at<self.before[15]:
            self.stack[at]=value;self.stack_min=min(self.stack_min,at);return
        if width!=2:raise ValueError('payload store is not a halfword')
        if any(at+i not in self.memory for i in range(width)) or not (
            self.destination&~1)<=at<((self.destination+self.count+1)&~1):
            raise ValueError('unowned destination write')
        self.stores.append((at,width))
        for i in range(width):self.memory[at+i]=(value>>(8*i))&255
    def plain(self,w,pc):
        n=(w>>8)&15;m=(w>>4)&15;low=w&0xf00f
        if w==9:return
        if w>>12==0xe:self.r[n]=signed(w&255,8)&MASK;return
        if w>>12==7:self.r[n]=(self.r[n]+signed(w&255,8))&MASK;return
        if w>>12==9:self.r[n]=signed(self.read(pc+4+2*(w&255),2),16)&MASK;return
        if low==0x6003:self.r[n]=self.r[m];return
        if low==0x600c:self.r[n]=self.r[m]&255;return
        if low==0x600d:self.r[n]=self.r[m]&65535;return
        if low in (0x6000,0x6001,0x6002,0x6004,0x6006):
            width={0x6000:1,0x6001:2,0x6002:4,0x6004:1,0x6006:4}[low]
            value=self.read(self.r[m],width)
            if low in (0x6004,0x6006) and n!=m:self.r[m]=(self.r[m]+width)&MASK
            self.r[n]=signed(value,width*8)&MASK;return
        if w&0xff00==0x8400:
            self.r[0]=signed(self.read(self.r[(w>>4)&15]+(w&15),1),8)&MASK;return
        if low==0x2006:
            self.r[n]=(self.r[n]-4)&MASK;self.write(self.r[n],self.r[m],4);return
        if low in (0x2000,0x2001,0x2002):
            self.write(self.r[n],self.r[m],{0x2000:1,0x2001:2,0x2002:4}[low]);return
        if low==0x2008:self.t=int(not(self.r[n]&self.r[m]));return
        if low==0x2009:self.r[n]&=self.r[m];return
        if low==0x200b:self.r[n]|=self.r[m];return
        if low==0x300c:self.r[n]=(self.r[n]+self.r[m])&MASK;return
        if low==0x3008:self.r[n]=(self.r[n]-self.r[m])&MASK;return
        if low==0x0007:self.macl=(self.r[n]*self.r[m])&MASK;return
        if w&0xf0ff==0x001a:self.r[n]=self.macl;return
        if w&0xf0ff==0x4010:
            self.r[n]=(self.r[n]-1)&MASK;self.t=int(not self.r[n]);return
        if w&0xf0ff==0x4001:self.t=self.r[n]&1;self.r[n]>>=1;return
        if w&0xf0ff==0x4018:self.r[n]=(self.r[n]<<8)&MASK;return
        if w&0xff00==0xc800:self.t=int(not(self.r[0]&(w&255)));return
        if w&0xff00==0xc900:self.r[0]&=w&255;return
        raise ValueError(f'unadmitted instruction {w:04x} at {pc:08x}')
    def run(self):
        while self.pc!=self.end:
            self.steps+=1
            if self.steps>10000:raise ValueError('sink instruction limit')
            pc=self.pc;w=self.fetch(pc)
            if w==0x000b or w>>12==0xa:
                target=self.end if w==0x000b else pc+4+2*signed(w&4095,12)
                self.steps+=1;self.plain(self.fetch(pc+2),pc+2);self.pc=target
            elif w&0xff00 in (0x8900,0x8b00,0x8d00,0x8f00):
                take=bool(self.t)==(w&0xff00 in (0x8900,0x8d00))
                if w&0x0400:self.steps+=1;self.plain(self.fetch(pc+2),pc+2)
                self.pc=pc+4+2*signed(w&255,8) if take else pc+(4 if w&0x0400 else 2)
            else:self.plain(w,pc);self.pc=pc+2
        if self.r[8:16]!=self.before[8:16]:raise ValueError('callee-saved register/stack damage')
        for i,v in enumerate(self.expected):
            if self.memory[self.destination+i]!=v:raise ValueError('payload differs')
        if self.count:
            if self.destination&1 and self.memory[self.destination-1]!=0x5a:raise ValueError('head neighbor differs')
            if (self.destination+self.count)&1 and self.memory[self.destination+self.count]!=0x5a:raise ValueError('tail neighbor differs')
        return self

def audit_code(address,code):
    if not 0<len(code)<=256:raise ValueError('unbounded sink')
    cases=0;largest=0;stack=0;store_sites=set();aligned512=None;odd513=None
    # Include both sides of minimum SH page boundaries without allocating an
    # adjacent page just to make a malformed wider access look successful.
    for offset in (0,1,2,1022,1023):
        for count in (0,1,2,3,17,255,256,511,512,513):
            for source_offset in (0,1,3):
                c=Sink(address,code,0x4243e000+offset,0x8c200000+source_offset,count).run()
                cases+=1;largest=max(largest,c.steps);stack=max(stack,c.before[15]-c.stack_min)
                if offset==0 and count==512 and source_offset==0:aligned512=c.steps
                if offset==1 and count==513 and source_offset==1:odd513=c.steps
    # Mutate every MOV.W payload store, separately. Source/destination value
    # and memory-access oracles must refuse byte and long store variants.
    rejected=[]
    for at in range(0,len(code)-1,2):
        w=struct.unpack_from('<H',code,at)[0]
        if w&0xf00f!=0x2001:continue
        for width,op in ((1,0),(4,2)):
            changed=bytearray(code);struct.pack_into('<H',changed,at,(w&~15)|op)
            failed=False
            for offset,count in ((1,1),(0,1),(0,512)):
                try:Sink(address,changed,0x4243e000+offset,0x8c200000,count).run()
                except ValueError:failed=True;break
            if not failed:raise ValueError('store-width mutation admitted')
            rejected.append(dict(address=hex(address+at),bytes=width))
        store_sites.add(address+at)
    if len(store_sites)!=3:raise ValueError('expected head/interior/tail halfword stores')
    return dict(status='PASS',cases=cases,max_instructions=largest,stack_bytes=stack,
        aligned_512_instructions=aligned512,odd_513_instructions=odd513,
        payload_halfword_store_sites=[hex(x) for x in sorted(store_sites)],
        rejected_store_mutations=rejected,calls=0,hardware_executed=False)

def audit(path):
    image=read_elf(path);out=[]
    for name in image[4]:
        if name.startswith('_kui_retail_ce_vram_write'):
            at,code,_=symbol_bytes(image,name,2)
            out.append(dict(symbol=name,address=hex(at),sha256=hashlib.sha256(code).hexdigest(),**audit_code(at,code)))
    if not out:raise ValueError('generic CE sink missing from actual linked owner')
    return dict(schema='kui.generic-ce.virtual-vram-width.v1',status='PASS',
        elf=str(path),elf_sha256=hashlib.sha256(image[0]).hexdigest(),sinks=out,
        limits=['Bounded emitted integer instructions execute against authored memory; no MMU/cache/bus timing or CE kernel executes.'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();report=audit(a.elf);text=json.dumps(report,indent=2)+'\n'
    if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
