#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify source-owned combo route against original MDE and actual linked SH.

Original executable bytes remain external. No hardware hang cause is claimed.
"""
from pathlib import Path
import argparse,ctypes,hashlib,json,struct,subprocess,sys,tempfile
repo=Path(__file__).resolve().parents[1];sys.path.insert(0,str(repo/'tests'))
from test_smash_native_gin_linked import Machine,State,RETURN,STACK,read_elf
LOAD=0x8c010000;GETPAD=0x8c1a9648;FLAG=0x8c48f514;PAD=0x8c700000;PAD6=PAD+512

def u32(m,at):return struct.unpack('<I',m.block(at,4))[0]
def put32(m,at,v):m.put(at,struct.pack('<I',v))
def input_case(run,source,held,edge,secondary=False):
 m=Machine(run);m.put(LOAD,source);m.c.r[15]=STACK;m.c.pc=0x8c019894;m.c.pr=RETURN;m.c.sr=0x40000000
 for a in (PAD,PAD6):m.put(a,bytes(256));put32(m,a,1)
 chosen=PAD6 if secondary else PAD;put32(m,chosen+8,held);put32(m,chosen+16,edge)
 put32(m,FLAG,0);put32(m,0x8c48f40c,0)
 m.c.watch_first[0]=0x0c008100;m.c.watch_end[0]=0x0c00c000
 calls=[]
 for _ in range(4):
  status=m.execute(stop=RETURN,break_pc=GETPAD,budget=1000000)
  if status==0:break
  assert m.c.pc==GETPAD
  port=m.c.r[4];assert port in (0,6);calls.append(port);m.c.r[0]=PAD if port==0 else PAD6;m.c.pc=m.c.pr
 else:raise AssertionError('controller-state fixture bound exceeded')
 flag=u32(m,FLAG);assert flag==int(held&0x606==0x606 and edge&8!=0)
 assert m.c.watch_writes[0]==0
 return m,dict(held=hex(held),start_edge=bool(edge&8),secondary=secondary,flag=flag,
  actual_source_instructions=m.c.steps,controller_state_boundary_ports=calls,low_return_gate_writes=0)

def source_loop_return(m,plan=None):
 # Exact saved mainloop frame for the actual caller8c01ae64, whose preserved
 # variables select the two original post-emulation SDK calls.
 sp=STACK-0x100;words=[0]*4+[0x8c01ae6c,0x8c1a1c4c,0x8c1a2130,0x8c48f3e0,5,0x8c47ee1c,0,0x8c514000]
 m.put(sp,struct.pack('<12I',*words));m.c.r[15]=sp;m.c.r[14]=FLAG;m.c.r[12]=0
 m.c.pc=0x8c010488;put32(m,0x8c514000,0x8c720000)
 if plan:
  address,call,reset,old,new=struct.unpack('<3I2H',plan)
  assert (address,call,reset,old,new)==(0x8c01048e,0x8c01087a,0x8c0117f0,0xa1f8,0xa1f4)
  assert m.block(address,4)==struct.pack('<HH',old,0x2ec2)
  m.put(address,struct.pack('<H',new))
  status=m.execute(stop=0x8c0117f0,break_pc=0x8c1a2130,budget=1000)
  assert status==0 and m.c.pc==0x8c0117f0 and m.c.pr==0x8c01087e
  assert m.c.r[15]==sp and u32(m,FLAG)==0
  return dict(original_branch='0xa1f8',candidate_branch='0xa1f4',patch_address='0x8c01048e',
   branch_target='0x8c01087a',original_reset_call_target='0x8c0117f0',original_reset_call_pr='0x8c01087e',
   source_loop_frame_preserved=True,original_postloop_SDK_calls_not_entered=True)
 status=m.execute(stop=0x8c010492,break_pc=0x8c1a2130,budget=1000)
 assert status==1 and m.c.pc==0x8c1a2130 and m.c.pr==0x8c01ae78
 assert list(m.c.r[4:8])==[0x8c720000,0,0,0] and u32(m,FLAG)==0
 return dict(actual_emulator_return='0x8c010882',actual_caller='0x8c01ae6c',
  first_postloop_call='0x8c1a2130',first_call_arguments=[hex(v)for v in m.c.r[4:8]],
  next_postloop_call='0x8c1a1c4c',next_native_UI_loop='0x8c01adb0',
  full_MDE_reset_gate='0x8c0117f0',reset_gate_not_on_this_original_segment=True)

def linked_plan_and_publication(run,elf,source):
 # These exact source roles are also freshly discovered by the production C
 # sanitizer fixture. Here execute the actual linked planner/publisher, with
 # no substituted SHA, owner, comparison, memcpy or cache helper.
 ctx,digest,windows,out=0x8cd00000,0x8cd00100,0x8cd00140,0x8cd00200
 wrapper=0x8c1ba890;pc=wrapper+42;op=struct.unpack_from('<H',source,pc-LOAD)[0]
 assert op&0xff00==0xd300;slot=((pc+4)&~3)+4*(op&255)
 assert struct.unpack_from('<I',source,slot-LOAD)[0]==0x8c1cbc04
 protected=[(0x8c1cc140,12),(0x8c1a9810,12),(0x8c1a99c0,20),(0x8c0101a8,4),
  (0x8c1bacf0,4),(0x8c1cbc04,12),(wrapper,56),(slot,4),
  (0x8c1ab8cc,12),(0x8c1ab902,12),(0x8c1ad336,12)]
 m=Machine(run);m.load(elf);m.put(LOAD,source)
 m.c.watch_first[0]=LOAD&0x1fffffff;m.c.watch_end[0]=(LOAD+len(source))&0x1fffffff
 start=m.c.steps
 m.call(elf[4]['_kui_sha256_init'][0],[ctx])
 m.call(elf[4]['_kui_sha256_update'][0],[ctx,LOAD,len(source)])
 m.call(elf[4]['_kui_sha256_digest'][0],[ctx,digest])
 assert m.block(digest,32)==hashlib.sha256(source).digest()
 hash_steps=m.c.steps-start
 m.put(windows,b''.join(struct.pack('<II',*w) for w in protected))
 def planner(image_bytes=len(source),window_count=len(protected)):
  m.put(out,bytes([0xa5])*16);m.c.pc=elf[4]['_kui_smash_native_combo_plan'][0];m.c.pr=RETURN;m.c.r[15]=STACK
  m.c.r[4:8]=[LOAD,image_bytes,LOAD,digest]
  m.put(STACK,struct.pack('<III',windows,window_count,out));saved=list(m.c.r[8:15]);start=m.c.steps
  assert m.execute()==0 and m.c.r[15]==STACK and list(m.c.r[8:15])==saved
  return m.c.r[0],m.block(out,16),m.c.steps-start
 accepted,plan,plan_steps=planner();assert accepted==1
 assert struct.unpack('<3I2H',plan)==(0x8c01048e,0x8c01087a,0x8c0117f0,0xa1f8,0xa1f4)
 assert m.block(LOAD,len(source))==source and m.c.watch_writes[0]==0
 refusal_rows=[]
 for fault in ('digest','edge','call','literal','overlap','truncated'):
  target={'digest':digest,'edge':LOAD+0x490,'call':LOAD+0x87a,'literal':LOAD+0x520}.get(fault)
  if target:before=m.block(target,1);m.put(target,bytes([before[0]^1]))
  if fault=='overlap':saved=m.block(windows,8);m.put(windows,struct.pack('<II',0xac01048e,2))
  accepted,value,steps=planner(len(source)-1 if fault=='truncated' else len(source))
  assert accepted==0 and value==bytes([0xa5])*16
  if target:m.put(target,before)
  if fault=='overlap':m.put(windows,saved)
  refusal_rows.append(dict(fault=fault,output_unchanged=True,actual_instructions=steps))
 assert m.block(LOAD,len(source))==source and m.c.watch_writes[0]==0
 m.put(out,plan);m.c.cache_count=0;m.c.ccr_writes=0
 _,publish_steps,status=m.call(elf[4]['_smash_return_combo_publish'][0],[out]);assert status==0
 expected=bytearray(source);expected[0x48e:0x490]=struct.pack('<H',0xa1f4)
 assert m.block(LOAD,len(source))==expected and m.c.watch_writes[0]==1
 assert m.c.cache_count==2 and list(m.c.cache_lines[:2])==[0x8c010480]*2
 assert set(m.c.cache_ops[:2])=={0xa3} and m.c.ccr_writes==2 and m.c.ccr_last_write&0x800
 # A second publication has no write authority: source is no longer original.
 writes=m.c.watch_writes[0]
 _,_,status=m.call(elf[4]['_smash_return_combo_publish'][0],[out],break_pc=elf[4]['_relay_stopped'][0])
 assert status==1 and m.c.watch_writes[0]==writes and m.block(LOAD,len(source))==expected
 return plan,dict(fresh_full_source_sha256=hashlib.sha256(source).hexdigest(),fresh_hash_instructions=hash_steps,
  protected_windows=[dict(begin=hex(p),bytes=n)for p,n in protected],planner_instructions=plan_steps,
  publication_instructions=publish_steps,publication_target_writes=1,published_line='0x8c010480',
  purge_before_and_after=True,instruction_cache_invalidations=2,
  source_changed_exactly_two_bytes=True,second_publication_refused_without_write=True,refusals=refusal_rows)

def main():
 p=argparse.ArgumentParser();p.add_argument('--original-mde',type=Path,required=True);p.add_argument('--stage',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args();source=a.original_mde.read_bytes()
 assert len(source)==1848288 and hashlib.sha256(source).hexdigest()=='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
 with tempfile.TemporaryDirectory(prefix='smash-original-combo-')as t:
  lib=Path(t)/'integer.so';subprocess.run(['cc','-std=c11','-O2','-fPIC','-shared',str(repo/'tests/sh4_integer_fast.c'),'-o',str(lib)],check=True)
  dll=ctypes.CDLL(str(lib));run=dll.sh4_fast_run;run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
  elf=read_elf(a.stage);plan,qualification=linked_plan_and_publication(run,elf,source);rows=[]
  face_bits=[2,4,0x200,0x400]
  for mask in range(16):
   held=sum(bit for i,bit in enumerate(face_bits) if mask>>i&1)
   for edge in (0,8):
    for secondary in (False,True):
     m,row=input_case(run,source,held,edge,secondary);rows.append(row)
     if row['flag']:
      snapshot=m.snapshot();row['original_post_loop']=source_loop_return(m)
      clone=Machine(run,snapshot);row['candidate_post_loop']=source_loop_return(clone,plan)
     else:
      # Replay the same source condition with only the proposed branch changed;
      # the untaken non-combo path must still stop at the exact original fallthrough.
      m.c.pc=0x8c010488;m.c.r[14]=FLAG;m.c.r[12]=0
      before=m.snapshot();m.execute(stop=0x8c010492,budget=100)
      clone=Machine(run,before);clone.put(struct.unpack_from('<I',plan)[0],plan[14:16])
      clone.execute(stop=0x8c010492,budget=100)
      assert list(m.c.r)==list(clone.c.r) and m.c.sr==clone.c.sr and m.c.pc==clone.c.pc
      row['candidate_noncombo_cpu_exact']=True
 result=dict(status='PASS',kind='Actual linked production source plan/cache publication and original-MDE integer button path; controller state is explicit fixture boundary',
  original_mde_sha256=hashlib.sha256(source).hexdigest(),stage_sha256=hashlib.sha256(a.stage.read_bytes()).hexdigest(),linked=qualification,
  decoder_sha256=hashlib.sha256((repo/'tests/sh4_integer_fast.c').read_bytes()).hexdigest(),cases=rows,
  limits=['Coherent RAM/cache-operation observations are modeled; no physical cache/controller/IRQ/audio completion or worker retirement execution.','No hardware freeze cause asserted.','Original ABXY+Start reaches MDE options; the proposed policy intentionally routes this exact edge directly to the outer-return reset gate.','The linked publication and original code execute in private modeled RAM; no original file is changed.'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(status='PASS',report=str(a.report),cases=len(rows))))
if __name__=='__main__':main()
