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

class ExceptionReader(Machine):
 def __init__(self,worker,seed,mask,*,body=None):
  super().__init__(worker,'_kui_vmu_native_trace_irq',seed,mask,0xac11ff00,body=body)
  self.r[4]=self.start_sp-16
  self.control={0x0042:(0x8c000000+seed*0x202)&MASK,
   0x0032:(0x60000100|(seed<<4))&MASK,0x0022:(0x8c010000+seed*0x100)&MASK}
  self.expected=[self.control[op] for op in (0x0042,0x0032,0x0022)]
  self.initial=self.r.copy(),self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr.copy()
 def plain(self,op,pc):
  if op&0xf0ff in self.control:self.r[op>>8&15]=self.control[op&0xf0ff];return
  super().plain(op,pc)
 def verified(self):
  self.run()
  assert self.r[1:]==self.initial[0][1:] and self.r[0]==self.expected[2]
  assert (self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr)==self.initial[1:]
  assert self.mem=={canonical(self.r[4])+4*i:v for i,v in enumerate(self.expected)}

def exception_reader(worker,self_test):
 _,body,_=symbol_bytes(worker,'_kui_vmu_native_trace_irq',2)
 assert body==bytes.fromhex('420002243200011422000b000214'), 'saved exception reader is not the exact read-only leaf'
 cases=0
 for seed in range(32):
  for mask in (0,6,15):ExceptionReader(worker,seed,mask).verified();cases+=1
 mutations=0
 if self_test:
  for at in range(0,len(body),2):
   changed=body[:at]+b'\x09\x00'+body[at+2:]
   try:ExceptionReader(worker,7,6,body=changed).verified()
   except (ValueError,AssertionError,KeyError,struct.error):mutations+=1
   else:raise ValueError('saved exception reader mutation accepted')
 return dict(instruction_cases=cases,mutation_rejections=mutations,
  cpu_control_register_writes=False,output_words=3,arbitrary_fault_pc_claim=False)

def video_witness(game):
 def words(at,count):return struct.unpack_from('<'+'H'*count,game,at-0x8c010000)
 def word(at):return struct.unpack_from('<I',game,at-0x8c010000)[0]
 assert words(0x8c1cb9ce,2)==(0x4e0b,0xe444) and words(0x8c1cb9f2,2)==(0x4e0b,0xe444)
 assert word(0x8c1cba24)==0x8c1f1f04 and word(0x8c1cba28)==0x8c358c70
 assert words(0x8c1cba08,3)==(0x6132,0x3410,0x89fb) and word(0x8c1cba38)==0x8c358d18
 assert words(0x8c1f1200,4)==(0xd122,0x6312,0x7301,0x2132) and word(0x8c1f128c)==0x8c358d18
 assert words(0x8c1f1444,4)==(0xe3fe,0x6022,0x2039,0x2802)
 assert words(0x8c1f1452,3)==(0x6123,0x6212,0x2822)
 return dict(explicit_video_disable_calls=['8c1cb9ce','8c1cb9f2'],
  counter_wait='8c1cba06..8c1cba0c',counter_address='8c358d18',
  video_handler_counter_increment='8c1f1200..8c1f1206',
  temporary_video_disable_restore=['8c1f144a','8c1f1456'],counter_can_also_be_reset=True)

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
   arbitrary_stall_pc_capture=False,saved_exception_reader=exception_reader(worker,self_test),
   original_video_witness=video_witness(game))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('directory');p.add_argument('startup');p.add_argument('--self-test',action='store_true')
 a=p.parse_args();print(json.dumps(check(a.directory,a.startup,a.self_test),indent=2))
