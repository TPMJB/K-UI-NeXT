#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute actual relocated worker C bodies in original owner-supplied SDK RAM.

Replay actual Evolution heap initialization/allocation, SDK initialization,
submission/consumer lifecycle, and linked worker callbacks, including actual C
VMU INFO enumeration. Count actual callback integer instructions and private P2
accesses by ELF object/stack. Model only flat coherent RAM and synchronous Maple
DMA packets. No SDK/worker helper is substituted, except bridging assembly
save/restore boundaries. This does not reproduce a game mainloop, interrupts,
cache state, bus timing, SD operations, FMV playback or a hardware freeze.
No original game code is copied into this script or its JSON output.
"""
from pathlib import Path
import sys,struct,json,subprocess,tempfile,argparse,hashlib,os,copy
from collections import Counter
from bisect import bisect_right
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from test_vmu_native_sdk_poll_execution import PollSH,INITIALIZE,SUBMIT,READY,CONSUME,TX,RX,MMIO,DIGEST,IMAGE_BYTES
from test_vmu_native_heap_execution import SH,MASK,signed
import profile_vmu_native_release_sh as release
from check_vmu_native_worker_gateway import read_elf
from check_loader_layout import SYM
from check_vmu_native_readonly import relocated
BUILD=ROOT/'build/mixed-poll-final'
BASE=0xacfd0000
CFG=0x8c00a000
FRAME=0x8c2ff800
GUARD=0x4b535431
def all_functions(elf,relocation_delta=0xed0000):
 # read_elf exposes names as a dict; local static names can repeat by object.
 data,_,_,sections,_=elf
 records=[]
 for section in sections:
  if section[1]!=2:continue
  strings=sections[section[6]];names=data[strings[4]:strings[4]+strings[5]]
  for offset in range(section[4],section[4]+section[5],SYM.size):
   name,value,size,info,_,_=SYM.unpack_from(data,offset)
   if info&15!=2 or not size:continue
   name=names[name:].split(b'\0',1)[0].decode('ascii')
   records.append((value+relocation_delta,value+relocation_delta+size,name))
 counts=Counter(name for _,_,name in records)
 return sorted((lo,hi,name if counts[name]==1 else name+'@'+hex(lo)) for lo,hi,name in records)
class WorkerSH(PollSH):
 def __init__(self,startup,build,handoff,cached_stack=False,worker_base=BASE):
  self.startup=startup;self.handoff=handoff;self.cached_stack=cached_stack
  self.worker_base=worker_base
  self.worker_physical_low=worker_base&0x1fffffff;self.worker_physical_high=self.worker_physical_low+0x30000
  relocation_delta=self.worker_physical_low-0xc100000
  super().__init__(startup.read_bytes(),2,1,None)
  elf=read_elf(build/'native-worker.elf')
  # SHN_ABS profile/geometry constants are not addresses and never relocate.
  self.symbols={n:(v[0] if v[3]==0xfff1 else (v[0]+relocation_delta)&MASK,*v[1:])
                for n,v in elf[4].items()}
  self.objects=sorted((a,a+z,n) for n,(a,z,t,_) in self.symbols.items() if t==1 and z)
  self.functions=all_functions(elf,relocation_delta);self.function_starts=[lo for lo,_,_ in self.functions]
  self.stack_physical_low=self.symbols['__vmu_native_worker_stack_bottom'][0]&0x1fffffff;self.stack_physical_high=self.symbols['__vmu_native_worker_stack_top'][0]&0x1fffffff
  self.symbols.pop('_read_heap',None)
  blob=relocated((build/'native-worker.bin').read_bytes(),(build/'native-worker.reloc').read_bytes(),worker_base)
  self.store_block(worker_base,bytes(0x20000));self.store_block(worker_base,blob)
  self.active=False;self.guest_words=set();self.stubs={};self.macl=0
  self.private_reads=self.private_writes=self.guest_reads=0
  self.flat_ocbp=[]
 def plain(self,op,pc):
  if pc==0x8c1dae38 and not getattr(self,"inside_worker",False):
   self.bridge_submit()
  n,m=(op>>8)&15,(op>>4)&15
  if op&0xf00f==0x300e:
   value=self.r[n]+self.r[m]+(self.sr&1);self.r[n]=value&MASK;self.flag(value>MASK)
  elif op&0xf00f==0x300a:
   value=self.r[n]-self.r[m]-(self.sr&1);self.r[n]=value&MASK;self.flag(value<0)
  elif op&0xf00f==0x200e:self.macl=(self.r[n]&65535)*(self.r[m]&65535)
  elif op&0xf00f==0x200f:self.macl=(signed(self.r[n]&65535,16)*signed(self.r[m]&65535,16))&MASK
  elif op&0xf0ff==0x00a3:
   # This interpreter has one coherent physical RAM image, hence OCBP has
   # no data effect. Cache-aware subclasses intercept this opcode and must
   # implement real dirty write-back/invalidation instead of this no-op.
   self.flat_ocbp.append({'pc':hex(pc),'address':hex(self.r[n]),'coherent_memory_noop':True})
  else:return super().plain(op,pc)
  self.executing=pc;self.instructions+=1
 def worker(self,name,args):
  self.sr=0x600000f0
  release.STACK=self.symbols['__vmu_native_worker_stack_top'][0]-32
  if self.cached_stack and name in ('_kui_vmu_native_passive_submit','_kui_vmu_native_passive_complete',
                                   '_kui_vmu_native_passive_split_submit'):
   release.STACK=(release.STACK&0x1fffffff)|0x80000000
  self.inside_worker=True
  self.access_reads=Counter();self.access_writes=Counter();self.p1_stack_reads=self.p1_stack_writes=0
  self.unknown_instruction_pcs=Counter();self.function_instructions=Counter();self.function_guest_reads=Counter();self.function_p2_reads=Counter();self.function_p2_writes=Counter()
  try:
   result=release.ReleaseSH.call(self,name,args)
   result['read_objects']=dict(self.access_reads);result['write_objects']=dict(self.access_writes)
   result['private_p1_stack_reads']=self.p1_stack_reads;result['private_p1_stack_writes']=self.p1_stack_writes
   result['unknown_instruction_pcs']={hex(a):v for a,v in self.unknown_instruction_pcs.items()};result['instructions_by_function']=dict(self.function_instructions);result['guest_reads_by_function']=dict(self.function_guest_reads);result['private_p2_reads_by_function']=dict(self.function_p2_reads);result['private_p2_writes_by_function']=dict(self.function_p2_writes)
   assert sum(self.function_instructions.values())==result['instructions']
   return result
  except Exception:
   self.active=False
   raise
  finally:self.inside_worker=False
 def discard_stack(self):
  # This is a logical dead-stack dependency test, not a cache simulation.
  bottom=self.symbols['__vmu_native_worker_stack_bottom'][0];top=self.symbols['__vmu_native_worker_stack_top'][0]
  self.store_block(bottom+256,bytes([0xa5])*(top-bottom-256))
 def word(self,a,v):self.write(a,v)
 def cword(self,off,v):self.write(CFG+off,v)
 def prepare(self):
  cache=(ROOT/'tests/fixtures/native-cache/evolution1.nvr').read_bytes()
  driver=cache[64:168];layout=cache[168:608];heap=cache[608:680];exits=cache[700:752]
  hw=list(struct.unpack('<18I',heap))
  init,alloc=hw[6],hw[9]
  self.heap_initializer_instructions=SH.run(self,init,[0x8c35e200,0xca1e00],0x8c00e000)[2]
  result,_,steps=SH.run(self,alloc,[0x30000],0x8c00e000)
  self.heap_allocate_instructions=steps
  assert result==0x8cfd0000
  lease=[1,116,2,1,*hw,0x8c35e200,0xca1e00,0x30000,0x8cfcffe0,0x8cfd0000,0x1801,0x650f0-0x1801]
  # calculate free units independently, matching full heap allocation.
  lease[-1]=0xca1e00//32-0x1801
  runtime=[1,236,0x8cfd0000,0x30000,32,0x20000,0,0,0,0,0]+[0]*48
  blob=struct.pack('<3I',1,2304,0xac248420)+driver+struct.pack('<29I',*lease)+layout+struct.pack('<59I',*runtime)+bytes(56)+self.handoff+struct.pack('<3I',0x8c004100,0x8c004200,0x8c004300)+exits+struct.pack('<I',0)
  assert len(blob)==2304,len(blob)
  self.store_block(CFG,blob)
  for name,value in [('_kui_vmu_native_passive_resume',0x8c1dae44),('_kui_vmu_native_passive_consumer',0x8c1daf4e),('_kui_vmu_native_readonly_release_resume',hw[10]+20)]:self.write(self.symbols[name][0],value)
  bottom=self.symbols['__vmu_native_worker_stack_bottom'][0]
  for i in range(16):self.write(bottom+i*4,GUARD)
  self.arm_counts=self.worker('_kui_vmu_native_passive_arm',[CFG,0]);assert self.arm_counts['result']==0
  self.install(hw,driver,exits)

 def install(self,hw,driver,exits):
  d=list(struct.unpack('<18I',driver[:72]));e=list(struct.unpack('<13I',exits))
  exports=self.symbols['_kui_vmu_native_worker_exports'][0]
  hooks=[(d[6],self.read(exports+16)),(hw[10],self.read(exports+56)),(hw[6],0x8c004200)]
  hooks += [(a,0x8c004200) for a in e[6:6+e[5]]]
  self.patch_ranges=[]
  for at,target in hooks:
   result=self.worker('_kui_vmu_native_patch_encode',[at,target,0x8c00b000]);assert result['result']==1
   self.store_block(at,self.block(0x8c00b000,12));self.patch_ranges.append((at,at+12))
  self.write(hw[7],0x8c004300);self.write(d[12],self.read(exports+20))
  self.install_sdk_initializer()
  self.original_image=self.startup.read_bytes()
 def install_sdk_initializer(self):
  # The flat orchestration writes the independently witnessed call-target
  # word; the cache-aware integration executes the real stage installer.
  # Never mask this slot as original code in patch_ranges: the actual cold
  # gateway and original initializer continuation must both execute.
  self.sdk_initializer_target=INITIALIZE
  self.coherence_installation={'available':False,'production_stage_executed':False}
  exports=self.symbols['_kui_vmu_native_worker_exports'][0]
  descriptor=self.read(exports+76)
  header=struct.unpack('<3I',self.block(descriptor,12))
  if header==(0x4b545231,1,32):return
  assert header==(0x4b545231,2,40),tuple(hex(v) for v in header)
  table=struct.unpack('<10I',self.block(descriptor,40))
  config=self.symbols['_config'][0]
  initializer=self.read(config+232+8);wrapper=self.read(config+232+12);kind=self.read(config+232+24)
  assert kind in (0,1)
  load=wrapper+(46 if kind else 42)
  op=self.read(load,2)
  assert op&0xff00==0xd300,(hex(load),hex(op))
  slot=((load+4)&~3)+4*(op&255)
  assert self.read(slot)&0x1fffffff==initializer&0x1fffffff,(hex(slot),hex(self.read(slot)),hex(initializer))
  target=table[7+kind]
  expected=self.symbols[('_kui_vmu_native_sdk_coherence_uniform_gate','_kui_vmu_native_sdk_coherence_variable_gate')[kind]][0]+0x20000000
  assert target==expected and table[9]==self.symbols['_kui_vmu_native_sdk_coherence_state'][0]
  assert self.read(table[9]+28)==0x4e434f31
  assert self.read(table[9]+12)==initializer+12 and self.read(table[9]+40)==initializer
  before=self.block(initializer,12)
  self.write(slot,target)
  assert self.block(initializer,12)==before
  self.sdk_initializer_target=target
  self.coherence_installation={'available':True,'production_stage_executed':False,
   'call_target_slot':hex(slot),'target':hex(target),'initializer':hex(initializer),
   'original_initializer_prefix_unchanged':True}
 def function(self,a):
  index=bisect_right(self.function_starts,a)-1
  if index>=0:
   lo,hi,label=self.functions[index]
   if lo<=a<hi:return label
  return 'unknown'
 def label(self,a):
  a=(a&0x1fffffff)|0xa0000000
  bottom=self.symbols['__vmu_native_worker_stack_bottom'][0];top=self.symbols['__vmu_native_worker_stack_top'][0]
  if bottom<=a<top:return 'private_stack'
  cfg=self.symbols['_config'][0]
  if cfg<=a<cfg+2304:
   off=a-cfg
   for label,end in [('header',12),('driver',116),('lease',232),('layout',672),('runtime',908),('display',964),('durable',2236),('callbacks',2248),('exits',2300),('diagnostics',2304)]:
    if off<end:return 'config.'+label
  for lo,hi,label in self.objects:
   if lo<=a<hi:return label
  return 'private_other'
 def read(self,a,count=4):
  if getattr(self,'active',False):
   if count==2 and a in (self.pc,self.pc+2):
    label=self.function(a);self.function_instructions[label]+=1
    if label=='unknown':self.unknown_instruction_pcs[a]+=1
   at=a&0x1fffffff
   if self.worker_physical_low<=at<self.worker_physical_high:
    if a&0xe0000000==0xa0000000:
     self.private_reads+=1;self.access_reads[self.label(a)]+=1;self.function_p2_reads[self.function(self.pc)]+=1
    elif a&0xe0000000==0x80000000 and self.stack_physical_low<=at<self.stack_physical_high:
     self.p1_stack_reads+=1;self.access_reads['p1.private_stack']+=1
   else:
    self.guest_reads+=1;self.function_guest_reads[self.function(self.pc)]+=1
    self.access_reads['mmio' if a in MMIO else 'callback_frame' if FRAME<=a<FRAME+76 else 'owner_ram']+=1
  if count==2 and not getattr(self,'inside_worker',False) and any(lo<=a<a+count<=hi for lo,hi in getattr(self,'patch_ranges',[])):
   return int.from_bytes(self.original_image[a-0x8c010000:a-0x8c010000+count],'little')
  return PollSH.read(self,a,count)
 def write(self,a,v,count=4):
  if getattr(self,'active',False) and a&0xe0000000==0xa0000000 and self.worker_physical_low<=a&0x1fffffff<self.worker_physical_high:
   self.private_writes+=1;self.access_writes[self.label(a)]+=1;self.function_p2_writes[self.function(self.pc)]+=1
  elif getattr(self,'active',False) and a&0xe0000000==0x80000000 and self.worker_physical_low<=a&0x1fffffff<self.worker_physical_high and self.stack_physical_low<=a&0x1fffffff<self.stack_physical_high:
   self.p1_stack_writes+=1;self.access_writes['p1.private_stack']+=1
  return PollSH.write(self,a,v,count)
 def bridge_submit(self):
  original_instructions=self.instructions
  before=(self.r.copy(),self.pr,self.sr,self.pc,self.macl)
  self.bridge_bank=int(self.mmio[0xa05f6c04]!=TX)
  r,pr,sr,pc,macl=before
  vals=[0,macl,0,pr,*r[14:0:-1]]
  # Frame has r14..r2 then SR then r1, not a flat descending register tail.
  vals=[0,macl,0,pr,*r[14:1:-1],sr,r[1]]
  assert len(vals)==19
  self.store_block(FRAME,struct.pack('<19I',*vals))
  result=self.worker('_kui_vmu_native_passive_submit',[FRAME])
  self.submit_counts=result
  self.r,self.pr,self.sr,self.pc,self.macl=before
  self.instructions=original_instructions
 def complete_bridge(self):
  vals=[0,0,0,0x8c1b1b2a]+[0]*13+[0x600000f0,0]
  self.store_block(FRAME,struct.pack('<19I',*vals))
  return self.worker('_kui_vmu_native_passive_complete',[FRAME])
 def dma(self):
  source=self.mmio[0xa05f6c04];at=source
  frames=[]
  for _ in range(256):
   ctl=self.read(at);at+=4
   if ctl==0x700:continue
   rx,hdr=self.read(at),self.read(at+4);words=ctl&255;at+=8+4*words
   dest=(hdr>>8)&255;port=dest>>6;cmd=hdr&255
   frames.append({'control':hex(ctl),'rx':hex(rx),'header':hex(hdr)})
   assert dest&63==32,hex(hdr)
   resp=MASK
   if self.controllers&(1<<port):
    assert cmd in (1,2,9)
    size=28 if cmd==1 else 48 if cmd==2 else 3
    resp=(size<<24)|(dest<<16)|(port<<14)|(5 if cmd==1 else 6 if cmd==2 else 8)
    self.store_block(rx,bytes((size+1)*4));self.write(rx+4,0x01000000)
    if cmd==9:self.write(rx+8,MASK);self.write(rx+12,0x80808080)
   self.write(rx,resp)
   if ctl&0x80000000:break
  else:raise AssertionError('NoLAST')
  self.mmio[0xa05f6c18]=0;self.batch={'source':hex(source),'frames':frames}

def diff(a,b):
 return [{'offset':hex(i),'before':hex(x),'after':hex(y)} for i,(x,y) in enumerate(zip(a,b)) if x!=y]
def negative_cases(cpu):
 # Original submit/gate execute once; each negative branch has an isolated RAM
 # copy. Stop at actual worker _fail entry before display hardware/reporting.
 cpu.original_call(SUBMIT);cpu.original_call(0x8c1daef6)
 source=cpu.read(cpu.symbols['_state'][0]+20)
 driver=cpu.symbols['_config'][0]+12
 manager_cell=cpu.read(driver+36);manager=cpu.read(manager_cell)
 pair=cpu.symbols['_controller_pair'][0]
 stack=cpu.symbols['__vmu_native_worker_stack_bottom'][0]
 rx=cpu.read(pair+28+128)
 cases=[
  ('manager_unsupported_area',lambda c:c.write(manager_cell,(manager&0x1fffffff)|0xe0000000)),
  ('manager_unaligned',lambda c:c.write(manager_cell,manager+1)),
  ('manager_wrong_identity',lambda c:c.write(manager_cell,manager+32)),
  ('source_rx_unaligned',lambda c:c.write(source+4,c.read(source+4)+1)),
  ('source_rx_outside_ram',lambda c:c.write(source+4,0x0d000000)),
  ('private_rx_unaligned',lambda c:c.write(pair+28+128,rx+1)),
  ('private_rx_outside_ram',lambda c:c.write(pair+28+128,0x0d000000)),
  ('private_rx_unsupported_area',lambda c:c.write(pair+28+128,0xec000000)),
  ('private_rx_function_past_ram',lambda c:(c.write(pair+28+128,0x0cfffffc),c.write(0x0cfffffc,0x03200008))),
  ('physical_response_header',lambda c:c.write(rx,c.read(rx)^1)),
  ('physical_a2_attached',lambda c:c.write(rx,c.read(rx)|0x00020000)),
  ('live_bank_selector',lambda c:c.write(manager+0x10b4,c.read(manager+0x10b4)^1)),
  ('stack_guard',lambda c:c.write(stack,0)),
 ]
 results=[]
 for name,mutate in cases:
  trial=copy.copy(cpu);trial.memory=bytearray(cpu.memory);trial.mmio=dict(cpu.mmio);trial.r=cpu.r.copy();trial.writes=[];trial.mmio_reads=[];trial.mmio_writes=[]
  mutate(trial);trial.writes=[]
  tx=trial.block(TX,0x6000);reply=trial.block(RX,0xc000)
  try:trial.complete_bridge()
  except AssertionError as failure:
   assert str(failure).startswith('Actual worker refused with code '),(name,str(failure))
   assert trial.block(TX,0x6000)==tx and trial.block(RX,0xc000)==reply,name
   owner_writes=[(hex(pc),hex(a),width) for pc,a,_,width in trial.writes if TX<=(a&0x1fffffff)<TX+0x6000 or RX<=(a&0x1fffffff)<RX+0xc000]
   assert not owner_writes,(name,owner_writes)
   results.append({'case':name,'refusal':str(failure),'parent_or_reply_writes':0,'tx_writes':0})
  else:raise AssertionError('Actual linked worker unexpectedly accepted '+name)
 return results

def make_handoff():
 """Use existing host fixture to seal a valid, generated blank-card handoff."""
 sources=['src/core/vmu_native_heap.c','src/core/vmu_native_runtime.c','src/core/vmu_native_layout.c','src/core/vmu_native_live.c','src/core/vmu_native_lifetime.c','src/core/vmu_native_profile.c','src/core/vmu_native_discovery.c','src/core/vmu_native_patch.c','src/core/hash.c','src/core/data.c','src/core/vmu_durable.c','src/core/vmu_store.c','src/core/vmu_emulation.c','src/core/retail_image.c','src/loader/vmu_readonly_overlay.c','src/loader/vmu_volatile_chain.c','src/loader/sd_reader.c','src/loader/vmu_backing.c','src/dreamcast/sci_sd_storage.c']
 wrapper='#define main reference_main\n#include "'+str(ROOT/'tests/test_vmu_native_persistent.c')+'"\n#undef main\nint main(void) {model_reset();fixture();prepare_handoff();return fwrite(&input.durable,1,sizeof(input.durable),stdout)==sizeof(input.durable)?0:1;}\n'
 with tempfile.TemporaryDirectory(prefix='kui-poll-worker-') as scratch:
  exe=Path(scratch)/'handoff'
  subprocess.run(['cc','-std=c11','-O2','-ffunction-sections','-fdata-sections','-I',str(ROOT/'include'),'-DKUI_VMU_EXPERIMENTAL=1','-x','c','-',*[str(ROOT/p) for p in sources],'-Wl,--gc-sections','-o',str(exe)],input=wrapper,text=True,check=True,capture_output=True)
  result=subprocess.run([str(exe)],check=True,capture_output=True).stdout
 assert len(result)==1272
 return result

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--startup',type=Path,default=os.environ.get('KUI_NATIVE_EVOLUTION1_STARTUP'))
 parser.add_argument('--build',type=Path,default=ROOT/'build/mixed-poll-final')
 parser.add_argument('--rounds',type=int,default=60)
 parser.add_argument('--output',type=Path,required=True)
 parser.add_argument('--negative-cases',action='store_true',help='Replay malformed completion contexts through actual linked C and require refusal before any RX/TX change')
 parser.add_argument('--discard-dead-stack',action='store_true',help='Poison dead private stack after returned callbacks; not a cache/timing model')
 parser.add_argument('--cached-stack',action='store_true',help='Use P1 private SP for C callbacks; requires linked worker admitting alias-normalized stack guards')
 parser.add_argument('--reference',action='store_true',help='Require full C guard path after NORMAL arm; tracing remains disabled')
 args=parser.parse_args()
 if not args.startup:parser.error('--startup is required')
 if not 6<=args.rounds<=1000:parser.error('--rounds must be between 6 and 1000')
 assert __debug__
 image=args.startup.read_bytes();assert len(image)==IMAGE_BYTES and hashlib.sha256(image).hexdigest()==DIGEST
 handoff=make_handoff()
 cpu=WorkerSH(args.startup,args.build,handoff,args.cached_stack);cpu.prepare()
 for pc in (0x8c1b1790,0x8c1b1792,0x8c1b1794,0x8c1b1796,0x8c1b179a):cpu.plain(cpu.read(pc,2),pc)
 cpu.r[3]=cpu.sdk_initializer_target
 sdk_initializer=cpu.original_call(cpu.sdk_initializer_target,[0x8c248408,0xc000,0x8c23c3e8,0xc000])
 if cpu.coherence_installation['available']:
  state=cpu.symbols['_kui_vmu_native_sdk_coherence_state'][0]
  assert cpu.read(state+44)==1
  assert [int(item['address'],16) for item in cpu.flat_ocbp]==[cpu.read(state),cpu.read(state)+4]
  assert all(int(item['pc'],16)&0xe0000000==0xa0000000 for item in cpu.flat_ocbp)
  cpu.coherence_installation.update(actual_gate_executed=True,successful_handoffs=1)
 if args.reference:
  cpu.write(cpu.symbols['_config'][0]+2300,4)
 history=[]
 for i in range(args.rounds):
  orig_submit=cpu.original_call(SUBMIT)
  if args.discard_dead_stack:cpu.discard_stack()
  pending=cpu.read(cpu.symbols['_controller_pair'][0]);live_ready=cpu.read(cpu.symbols['_live_plan'][0]+4)
  gate=cpu.original_call(0x8c1daef6)
  tx_before=cpu.block(TX,0x6000);rx_before=cpu.block(RX,0xc000)
  complete=cpu.complete_bridge()
  if args.discard_dead_stack:cpu.discard_stack()
  tx_after=cpu.block(TX,0x6000);rx_after=cpu.block(RX,0xc000)
  consumer=cpu.original_call(CONSUME)
  consumer_rx=cpu.block(RX,0xc000)
  ready=cpu.original_call(READY)
  result={'round':i,'original_submit_instructions':orig_submit,'gate_instructions':gate,'submit':cpu.submit_counts,'complete':complete,'consumer_instructions':consumer,'ready_instructions':ready,'controller_pair_pending_at_submit':pending,'live_ready':live_ready,'physical_batch':cpu.batch,'complete_tx_differences':diff(tx_before,tx_after),'complete_rx_differences':diff(rx_before,rx_after),'consumer_rx_differences':diff(rx_after,consumer_rx)}
  history.append(result)
  assert not result['complete_tx_differences']
  assert not result['consumer_rx_differences']
  if i>=2 and not args.reference:assert live_ready==0x4e4c5031, (i,hex(live_ready))
  if i>=2 and not args.reference:
   assert pending==1
   assert result['complete_rx_differences']==[{'offset':hex((i%2)*0x6000+2),'before':'0x20','after':'0x22'}]
 negative_results=negative_cases(cpu) if args.negative_cases else []
 summary={'negative_cases':negative_results,'schema':'kui.native.original-sdk-linked-worker.v1','executable_sha256':DIGEST,'worker_elf_sha256':hashlib.sha256((args.build/'native-worker.elf').read_bytes()).hexdigest(),'worker_binary_sha256':hashlib.sha256((args.build/'native-worker.bin').read_bytes()).hexdigest(),'handoff_sha256':hashlib.sha256(handoff).hexdigest(),'retail_code_embedded':False,'reference_full_path':args.reference,'cached_callback_stack':args.cached_stack,'discard_dead_stack':args.discard_dead_stack,'heap_initializer_instructions':cpu.heap_initializer_instructions,'heap_allocate_instructions':cpu.heap_allocate_instructions,'arm':cpu.arm_counts,'sdk_initializer_instructions':sdk_initializer,'limitations':['Flat coherent memory; no cache, bus latency, interrupts, scheduled callbacks, FMV, game mainloop, SD transport, or cycle timing.','Actual SH C callbacks execute without worker helper stubs; original SDK submit hooks are bridged at the exact overwritten instruction boundary. Assembly save/restore gateways are not interpreted.','Actual original SDK upper initializer lock-normalization block executes; IRQ-registration scaffolding does not.','Completion caller PR and inherited SR are provided from original wrapper contract, then original consumer and ready functions execute.','Valid blank-card handoff comes from existing host model; no storage request occurs during automatic INFO-only VMU enumeration.','Reference mode changes diagnostics selector after NORMAL arm to require full C path while leaving trace disabled.'], 'rounds':history}
 summary['cold_coherence']=cpu.coherence_installation
 summary['flat_ocbp']=cpu.flat_ocbp
 summary['limitations'].extend([
  'Cold call-target orchestration validates the actual descriptor, MOV.L R3 slot and original target, then writes that one word; this flat replay does not execute the production stage installer.',
  'The actual linked cold gate and original SDK initializer execute. OCBP is a recorded no-op ONLY because this flat model has one coherent RAM image; dirty-cache causality requires the separate cache-aware integration.'])
 args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(summary,indent=2)+'\n')
 print(json.dumps({'output':str(args.output),'rounds':len(history),'reference':args.reference,'arm_instructions':cpu.arm_counts['instructions'],'first_pair_instructions':history[0]['submit']['instructions']+history[0]['complete']['instructions'],'steady_last_pair':{'instructions':history[-1]['submit']['instructions']+history[-1]['complete']['instructions'],'p2_reads':history[-1]['submit']['private_p2_reads']+history[-1]['complete']['private_p2_reads'],'p2_writes':history[-1]['submit']['private_p2_writes']+history[-1]['complete']['private_p2_writes'],'guest_reads':history[-1]['submit']['guest_reads']+history[-1]['complete']['guest_reads']}}))

if __name__=="__main__":
 main()
