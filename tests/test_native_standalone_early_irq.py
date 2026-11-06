#!/usr/bin/env python3
"""Private-original early SDK installer -> linked standalone binder probe.
No original code is retained in the report. No console performance is inferred.
"""
import argparse, ctypes, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_native_standalone_reader import BinderCPU,StageCPU,StageStop,assert_publication,PRIVATE_TOP,RETURN
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from test_vmu_native_heap_execution import MASK,signed
LOAD=0x8c010000
HASHES=dict(intevt='b51932943fb69df82c162dec6b1572e9eff85e2186b0ba069ee7ad5969deb19c',
 expevt='1c2c17ac31cff395918a7e216e07c053b67b15f283f180156c905f28e600bd22',
 common='8bffd9df8f7a4b54440889df44d59672bfea1630c6facb4cf16547ca8957f4bd',
 nesting='ae51f5334862b3746b2d104331b462cb9023b7d38d996d073e263dbf423513ed',
 install='84c30a9f200e26a3c07eb38b3fc8d5e91db6a9a497e61eba26f533445389e7a1',
 init='6c5b89764fba51b37c25a3bfa66c83e60116fa56e0c177a2009cc067f5a49f87',
 default='1fdf9ed9981bc3cbcd9e6f7eba8936004e854db0b0e686f5aa0198216578caec')
def need(v,m):
 if not v:raise ValueError(m)
def u32(b,at):return struct.unpack_from('<I',b,at)[0]
def normalized(b,holes=()):
 b=bytearray(b)
 for h in holes:b[h:h+4]=bytes(4)
 return hashlib.sha256(b).hexdigest()
def contract(path):
 b=Path(path).read_bytes();n=len(b)
 stubs=[at for at in range(0x34,n-0x34+1,4) if u32(b,at)==0x00090009 and normalized(b[at:at+0x34],(0x28,0x2c,0x30))==HASHES['intevt']]
 need(len(stubs)==1,'unique independently fingerprinted early INTEVT stub absent')
 i=stubs[0];e=i-0x34;c=i+0x34;nest=c+0x14c;event=u32(b,i+0x28);area=event&0xe0000000
 raw=lambda a:(a&0x1fffffff)|area
 need(area in (0,0x80000000) and event==raw(LOAD+c+0x2d4),'event/alias relationship differs')
 for at,count,holes,h in ((e,0x34,(0x28,0x2c,0x30),'expevt'),(c,0x14c,(0x148,),'common'),(nest,0x180,(0x17c,),'nesting')):
  need(normalized(b[at:at+count],holes)==HASHES[h],h+' original instructions differ')
 inits=[at for at in range(0,n-0x2e+1,2) if b[at:at+4]==bytes.fromhex('11951194') and normalized(b[at:at+0x2e],(0x2a,))==HASHES['init']]
 need(len(inits)==1,'initializer is absent or ambiguous');init=inits[0];install=init-0x15a
 need(install>=0 and normalized(b[install:install+0x84])==HASHES['install'],'installer instructions differ')
 roles=((0x1e,7,raw(LOAD+i+0x30)),(0x2e,0,raw(LOAD+e+0x30)),(0x30,4,raw(LOAD+nest)),(0x42,4,raw(LOAD+c)))
 for instruction,reg,value in roles:
  pc=LOAD+install+instruction;op=struct.unpack_from('<H',b,install+instruction)[0]
  need(op&0xff00==0xd000|(reg<<8),'installer role literal opcode differs')
  at=((pc+4)&~3)+4*(op&255)-LOAD;need(u32(b,at)==value,'installer role target differs')
 default=u32(b,init+0x2a);d=(default&0x1fffffff)-(LOAD&0x1fffffff)
 need(default&0xe0000000==area and not default&3 and normalized(b[d:d+0xc4],(0xb8,0xbc,0xc0))==HASHES['default'],'original default differs')
 need(u32(b,c+0x148)==u32(b,nest+0x17c)==event,'common event literals differ')
 return dict(image=b,digest=hashlib.sha256(b).hexdigest(),eri_slot=0x8c00f65c,rxi_slot=0x8c00f660,vector=LOAD+i,commons=(raw(LOAD+c),raw(LOAD+nest)),default=default,vbr=0x8c00f400,install=LOAD+install,table_init=LOAD+init,event=event,path=str(path))
class EarlyCPU(BinderCPU):
 def plain(self,op,pc):
  n=op>>8&15
  if op&0xf00f==0x3006:self.flag(self.r[n]>self.r[op>>4&15]);return
  if op&0xf0ff==0x4021:
   self.flag(self.r[n]&1);self.r[n]=(self.r[n]>>1)|(self.r[n]&0x80000000);return
  super().plain(op,pc)
def installed(resident,owner,mode,roles):
 c=EarlyCPU(resident,owner,cache=False,common=0)
 for i,value in enumerate((owner['vector'],*roles,owner['default'],0x53524431)):c.put(c.region+4*i,value)
 c.r[4]=mode;c.r[5]=0x0c573700 if mode else 0;c.r[15]=0x8c00e000;c.sr=0x600000e0;c.pr=RETURN
 c.run_to(owner['install'],RETURN)
 selected=owner['commons'][bool(mode)]
 need(c.read(owner['vector']-4)==c.read(owner['vector']+0x30)==c.read(owner['vbr']+0x630)==selected,'actual installer did not select coherent templates/live stub')
 need(c.read(c.eri_slot)==c.read(c.rxi_slot)==owner['default'],'actual initializer did not leave both default slots')
 need(c.r[15]==0x8c00e000 and c.sr&0xf0==0xe0,'installer changed caller stack/mask')
 for off,source in ((0x100,owner['vector']-0x34),(0x400,owner['vector']-0x34),(0x600,owner['vector'])):
  need(all(c.read(owner['vbr']+off+4*i)==c.read(source+4*i) for i in range(13)),'installer copied vector differs')
 c.cache_on=True;c.cache.clear();c.r[15]=PRIVATE_TOP;c.sr=0x400001f0;c.pr=RETURN
 return c
def no_publish(c):
 need(not c.ready_ram() and not [(a,v,n) for _,a,v,n in c.writes if not PRIVATE_TOP-128<=a<PRIVATE_TOP] and not c.flushes,'refused binder published externally')

class Family(ctypes.Structure):
 _fields_=[(name,ctypes.c_uint32) for name in ('vector','normal','fpu','original')]
def production_family_library(directory):
 output=Path(directory)/'family.so'
 subprocess.run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(ROOT/'include'),
  *[str(ROOT/name) for name in ('src/core/native_sdk_sci_family.c','src/core/hash.c','src/core/data.c')],'-o',str(output)],check=True)
 library=ctypes.CDLL(str(output));f=library.kui_native_sdk_sci_family_find
 f.argtypes=(ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint32,ctypes.POINTER(Family));f.restype=ctypes.c_int
 return library
class EarlyStageCPU(StageCPU):
 """Actual caller/publication, with pure family C evaluated on exact RAM bytes."""
 def __init__(self,stage,resident,owner,library):
  self.library=library;self.family_calls=[]
  BinderCPU.__init__(self,resident,owner,cache=False)
  self.stage=stage;self.resident=resident;self.symbols=stage[4]
  for kind,offset,address,_,count,_,_,_ in stage[2]:
   if kind==1 and count:self.put_block(address,stage[0][offset:offset+count])
  self.r[15]=0x8cfeffe0;self.pr=RETURN;self.sr=0x400001f0;self.ccr=0
  self.leaves=[];self.publisher_visits=0
  self.api={stage[4][name][0]:name for name in ('_stopped','_relay_stopped')}
  self.targets=set(self.api)
  blob=stage[4]['__retail_resident_sci_blob_start'][0]
  for name,value in (('_resident_blob',blob),('_resident_bytes',resident[4]['__retail_resident_binary_end'][0]-0x8c004000),('_resident_limit',0x8c007ba0),('_exec_bytes',len(owner['image']))):
   self.put(stage[4][name][0],value)
  for i in (0,4,8,12,16,0x530,0x534,0x538):self.put(self.region+i,0)
  self.family_target=stage[4]['_kui_native_sdk_sci_family_find'][0];self.targets.add(self.family_target)
 def plain(self,op,pc):
  if op&0xf00f==0x2000:
   self.write(self.r[op>>8&15],self.r[op>>4&15],1);return
  return super().plain(op,pc)
 def callee(self):
  if self.pc!=self.family_target:return super().callee()
  address,count,load,out=self.r[4:8];continuation=self.pr
  need(address==LOAD and load==LOAD and count==len(self.owner['image']),'source caller changed immutable family authority')
  source=self.ram_bytes(self.offset(address,count),count);buffer=ctypes.create_string_buffer(source);family=Family()
  result=self.library.kui_native_sdk_sci_family_find(buffer,count,load,ctypes.byref(family))
  self.put_block(out,bytes(family));self.family_calls.append(dict(result=result,roles=list(struct.unpack('<4I',bytes(family)))))
  for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd)&MASK
  self.r[0]=result&MASK;self.sr^=1;self.pr^=0x13572468;self.pc=continuation
def stage_roles(stage,resident,owner,library):
 c=EarlyStageCPU(stage,resident,owner,library)
 c.call('_standalone_source_check')
 need(c.family_calls==[dict(result=1,roles=[owner['vector'],*owner['commons'],owner['default']])],'production family lost independently proved alternate')
 # Its own installer can change both selected-template words without changing
 # the independently retained family. The actual source wrapper checks this.
 for mode in (0,0x80000000):
  chosen=owner['commons'][bool(mode)]
  c.put(owner['vector']-4,chosen);c.put(owner['vector']+0x30,chosen)
  c.call('_standalone_source_check')
 c.call('_standalone_source_publish')
 values=[c.read(c.region+4*i) for i in range(5)]
 need(values==[owner['vector'],*owner['commons'],owner['default'],0x53524431],'actual publisher lost proven source roles')
 need(c.read(c.region+0x534)==owner['default'] and not c.read(c.region+0x530) and not c.read(c.region+0x538),'cold publisher armed live SDK route')
 # Source roles cannot change between the two wrapper calls. Optional nested
 # proof damage must remove the alternate and therefore stop the second call.
 r=EarlyStageCPU(stage,resident,owner,library);r.call('_standalone_source_check')
 at=(owner['commons'][1]&0x1fffffff)|0x80000000
 r.put(at,r.read(at)^1);refused=False
 try:r.call('_standalone_source_check')
 except StageStop:refused=True
 need(refused and not r.publisher_visits,'source wrapper reused changed second-handler proof')
 return values,dict(production_family_calls=c.family_calls,changed_alternate_stops=True,source_descriptor_words=values)


def callback_path(c,owner,mode):
 # Both complete common handlers are fingerprinted above. Execute their
 # integer event/table lookup and JSR, then the actual linked RTS callback.
 base=(owner['commons'][bool(mode)]&0x1fffffff)|0x80000000
 dispatch=base+(0xa8 if mode else 0x84);continuation=base+(0xd6 if mode else 0xb2)
 c.write(c.active,0);c.iprb=0xa50f;c.run_symbol('_hook')
 need(c.iprb==0xa51f and not c.vbr_writes,'real readiness hook did not select SCI1')
 c.calls=0;c.targets={c.symbols['_kui_retail_async_irq'][0]};c.callee_mode='deep-reader';c.callee_return=0
 c.r[15]=0x8c00ed00;c.r[0]=c.sr;c.write(owner['event'],0x4e0)
 c.run_to(dispatch,c.callback)
 need(c.r[4]==0x4e0 and c.pr==continuation,'original common failed event/table/JSR callback contract')
 before=c.snapshot();c.run_symbol('_kui_retail_sdk_sci_entry',continuation)
 need(c.calls==1 and c.r[8:]==before[0][8:] and c.pr==before[2] and c.sr==before[3] and
      (c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(before[6],before[7],before[8],before[9],before[10]),'published callback changed SDK C ABI/control state')
 return dict(actual_integer_lookup_and_jsr=True,actual_linked_callback=True,deep_reader='explicit ABI adversary',full_fpu_frame=False)

def main():
 p=argparse.ArgumentParser();p.add_argument('--old-build',type=Path,required=True);p.add_argument('--new-build',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('originals',type=Path,nargs='+');a=p.parse_args()
 old=read_elf(a.old_build/'resident-sci.elf');new=read_elf(a.new_build/'resident-sci.elf') if a.new_build else None
 if new:
  for symbol in ('__retail_hook_stack_bottom','__retail_hook_stack'):
   need(old[4][symbol][0]==new[4][symbol][0],'standalone fixed service stack moved')
  need(old[4]['_standalone_reader_region'][1]==new[4]['_standalone_reader_region'][1]==0x760,'standalone region geometry changed')
  need(new[4]['__retail_resident_bss_end'][0]<=new[4]['__retail_hook_stack_bottom'][0],'resident exceeds fixed low-RAM budget')
 stage=read_elf(a.new_build/'stage.elf') if a.new_build else None
 temporary=tempfile.TemporaryDirectory(prefix='kui-early-family-');library=production_family_library(temporary.name)
 rows=[]
 for path in a.originals:
  o=contract(path);row=dict(source_name=Path(path).name,source_sha256=hashlib.sha256(o['image']).hexdigest(),roles={k:hex(o[k]) for k in ('vector','default','install')},modes=[])
  if new:
   _,row['source_caller_publication']=stage_roles(stage,new,o,library)
  for mode in (0,0x80000000):
   c=installed(old,o,mode,(o['commons'][0],o['commons'][0]));c.bind()
   if not mode:assert_publication(c)
   else:no_publish(c)
   m=dict(mode=hex(mode),old_installed=bool(c.ready_ram()),old_binder_instructions=c.instructions)
   if new:
    c=installed(new,o,mode,o['commons']);c.bind();assert_publication(c)
    m.update(new_installed=True,new_binder_instructions=c.instructions)
    m['callback']=callback_path(c,o,mode)
    refusals={
     'template_pair_disagree':lambda x:x.put(o['vector']-4,o['commons'][not bool(mode)]),
     'live_common_disagrees':lambda x:x.put(o['vbr']+0x630,o['commons'][not bool(mode)]),
     'unknown_source_live_common':lambda x:[x.put(at,o['commons'][bool(mode)]+4) for at in (o['vector']-4,o['vector']+0x30,o['vbr']+0x630)],
     'eri_owned':lambda x:x.put(x.eri_slot,x.callback),
     'rxi_owned':lambda x:x.put(x.rxi_slot,x.callback),
     'active_reader':lambda x:x.put(x.region+0x2bc,1),
    }
    for name,mutate in refusals.items():
     c=installed(new,o,mode,o['commons']);mutate(c);c.cache.clear();c.bind()
     try:no_publish(c)
     except ValueError as e:raise ValueError(name+': '+str(e)) from e
    m['new_refusals']=list(refusals)
   row['modes'].append(m)
  rows.append(row)
 result=dict(schema='kui.native.standalone-early-irq.execution.v1',result='PASS',cases=rows,old_resident_sha256=hashlib.sha256((a.old_build/'resident-sci.elf').read_bytes()).hexdigest(),new_resident_sha256=hashlib.sha256((a.new_build/'resident-sci.elf').read_bytes()).hexdigest() if new else None,console_execution=False,hardware_performance=False)
 result['source_sha256']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ('tests/test_native_standalone_early_irq.py','src/core/native_sdk_sci_family.c','src/core/native_sdk_sci_family.inc','src/loader/native_standalone_reader.inc','src/loader/native_standalone_reader_stage.inc','src/loader/retail_async.c','src/loader/retail_resident.S')}
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
