#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute inserted SH gateways against the exact original Evolution code."""
import argparse, hashlib, json, struct
from pathlib import Path
from check_vmu_native_stage_stack import Machine, canonical
from check_vmu_native_worker_gateway import read_elf, symbol_bytes, MASK

DIGEST='ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35'
GATES=(
 ('_kui_vmu_native_trace_frame_entry',0x41,0x8c1663f8,0x8c166410),
 ('_kui_vmu_native_trace_wait_loop',0x42,0x8c166422,0x8c166436),
 ('_kui_vmu_native_trace_wait_before',0x43,0x8c1cd000,0x8c1cd000),
 ('_kui_vmu_native_trace_wait_after',0x44,0x8c166454,0x8c166464))

class TraceGate(Machine):
 def __init__(self,worker,game,gate,seed,mask,*,original=False,body=None):
  name,self.event,start,stop=gate
  super().__init__(worker,name,seed,mask,0x8c2fd900,body=body)
  self.game,self.original=game,original
  self.r[9]=0x8c163f5a;self.r[10]=0x8c163f20;self.r[13]=0x8c163f16
  self.r[12]=0x8c304ff4;self.r[11]=0
  self.pr=0x8c0f6c72 if self.event==0x41 else 0x8c166454 if self.event==0x43 else 0x8c0f6d0e
  self.mem.update({canonical(self.r[12]):123456,0xffd8000c:0xfffd0000,
    0x8c3049a8:1,0x8c304854:7,0x8c304834:9,0x8c30482c:11,0x8c304830:13})
  if self.event==0x43:self.r[3]=start if original else self.base
  self.initial=self.r.copy(),self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr.copy()
  self.initial_mem=self.mem.copy();self.stores=[]
  self.stop_pc=stop
  if original:self.pc=start
  self.visits=0
 def code(self,a,n):
  if self.base<=a<a+n<=self.base+len(self.blob):return super().code(a,n)
  if 0x8c010000<=a<a+n<=0x8c010000+len(self.game):
   return self.game[a-0x8c010000:a-0x8c010000+n]
  return super().code(a,n)
 def write(self,a,v):
  a=canonical(a)
  assert (self.start_sp-128<=a<self.start_sp or 0x8c11fe00<=a<0x8c120000 or
    a in (0x8c30482c,0x8c304830,0x8c304834,0x8c304854)),hex(a)
  self.mem[a]=v&MASK;self.stores.append(a)
 def plain(self,op,pc):
  n,m=op>>8&15,op>>4&15
  if op&0xf0ff==0x402a:self.pr=self.r[n];return
  if op&0xf00f==0x0007:self.macl=self.r[n]*self.r[m]&MASK;return
  if op&0xf0ff==0x001a:self.r[n]=self.macl;return
  if op&0xf00f==0x400d:
   shift=self.r[m]&MASK
   self.r[n]=((self.r[n]>>((-shift)&31) if shift&0x80000000 and shift&31 else
     0 if shift&0x80000000 else self.r[n]<<(shift&31)))&MASK;return
  super().plain(op,pc)
 def call(self,target):
  if target!=self.image[4]['_kui_vmu_native_trace_game'][0]:return False
  assert not self.original
  self.visits+=1
  assert self.r[4:6]==[self.event,self.start_sp-80]
  assert self.r[15]==self.image[4]['__vmu_native_worker_stack_top'][0]-4
  assert self.sr&0xf0==0xf0
  f=self.start_sp-80
  assert self.read(f+12)==self.initial[1] and self.read(f+68)==self.initial[5]
  assert self.read(f+76)==self.initial[0][0]
  self.mach^=0x12345678;self.macl^=0xfedcba98;self.gbr^=0x8abcdef0
  self.abi_return(0xdeadbeef);return True
 def equivalent(self,reference):
  assert self.visits==1
  assert self.r==reference.r,(hex(self.event),[(i,hex(a),hex(b)) for i,(a,b) in enumerate(zip(self.r,reference.r)) if a!=b])
  assert (self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr)==(
    reference.pr,reference.mach,reference.macl,reference.gbr,reference.sr,reference.fpscr,reference.fr)
  for a in self.initial_mem:assert self.mem[a]==reference.mem[a],hex(a)
  assert all(a in range(self.start_sp-128,self.start_sp) or 0x8c11fe00<=a<0x8c120000 or
    a in reference.stores for a in self.stores)

def check(directory,startup,self_test=False):
 game=Path(startup).read_bytes();assert len(game)==2143652 and hashlib.sha256(game).hexdigest()==DIGEST
 worker=read_elf(Path(directory)/'native-worker.elf');cases=0
 for gate in GATES:
  for seed in range(12):
   for mask in (0,6,15):
    reference=TraceGate(worker,game,gate,seed,mask,original=True).run()
    TraceGate(worker,game,gate,seed,mask).run().equivalent(reference);cases+=1
 mutations=0
 if self_test:
  for gate in GATES:
   base,body,_=symbol_bytes(worker,gate[0],2)
   edits=[]
   callback=struct.pack('<I',worker[4]['_kui_vmu_native_trace_game'][0])
   assert body.count(callback)==1
   edits.append(body.replace(callback,struct.pack('<I',0x8c000000)))
   at=body.find(b'\x07\x4f');assert at>=0
   edits.append(body[:at]+b'\x09\x00'+body[at+2:])
   for changed in edits:
    try:
     ref=TraceGate(worker,game,gate,7,6,original=True).run()
     TraceGate(worker,game,gate,7,6,body=changed).run().equivalent(ref)
    except (ValueError,AssertionError,KeyError,struct.error):mutations+=1
    else:raise ValueError('trace gateway mutation accepted')
 return dict(original_instruction_equivalence_cases=cases,mutation_rejections=mutations,
   original_sha256=DIGEST,all_integer_sr_pr_mac_gbr_preserved=True,
   common_path_owner_writes='private gateway frame and original replay stores only',
   arbitrary_stall_pc_capture=False)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('directory');p.add_argument('startup');p.add_argument('--self-test',action='store_true')
 a=p.parse_args();print(json.dumps(check(a.directory,a.startup,a.self_test),indent=2))
