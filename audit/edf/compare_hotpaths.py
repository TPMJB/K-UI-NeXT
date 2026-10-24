#!/usr/bin/env python3
"""Static SH comparison: normalize resolved literals/branches; do not infer timing."""
import json,re,struct,subprocess,sys
from pathlib import Path
ROOT=Path('/workspace/scratch/45de2288a575/K-UI-recovery')
sys.path.insert(0,str(ROOT/'tools'))
from check_vmu_native_worker_gateway import read_elf,literal_aware_instructions
OBJDUMP=ROOT/'.deps/sh-elf/bin/sh-elf-objdump'
class Image:
 def __init__(self,path):
  self.path=Path(path); self.image=read_elf(self.path)
  self.data,self.h,self.segments,self.sections,self.symbols=self.image
  self.funcs=sorted(set(s[0] for s in self.symbols.values() if s[2]==2))
  self.dis=subprocess.check_output([str(OBJDUMP),'-d',str(path)],text=True)
  self.decoded=literal_aware_instructions(self.dis)
 def read(self,at,n):
  for s in self.sections:
   if s[1]==1 and s[2]&2 and s[3]<=at and at+n<=s[3]+s[5]:
    return self.data[s[4]+at-s[3]:s[4]+at-s[3]+n]
  raise ValueError(hex(at))
 def resolve(self,at):
  exact=[(name,s) for name,s in self.symbols.items() if s[0]==at and s[2] in (1,2)]
  if exact:
   return sorted(exact,key=lambda x:(x[1][2]!=2,x[0]))[0][0]
  containers=[(name,s) for name,s in self.symbols.items() if s[2] in (1,2) and s[0]<at<s[0]+s[1]]
  if containers:
   name,s=min(containers,key=lambda x:x[1][1]);return name+'+'+str(at-s[0])
  try:
   raw=self.read(at,1)
   b=bytearray()
   for i in range(192):
    c=self.read(at+i,1)[0]
    if c==0 and b and all(x in (9,10,13) or 32<=x<127 for x in b):return 'string:'+b.decode('ascii')
    if c==0 or (c not in (9,10,13) and not 32<=c<127):break
    b.append(c)
  except ValueError:pass
  return hex(at)
 def normalized(self,name):
  start,size,kind,section=self.symbols[name]
  end=min([start+size]+[x for x in self.funcs if x>start])
  instructions={r[0]:r for r in self.decoded if start<=r[0]<end}
  work=[start];reachable=set()
  while work:
   pc=work.pop()
   if pc in reachable or pc not in instructions:continue
   reachable.add(pc);at,op,mn,args=instructions[pc]
   if op&0xf000 in (0xa000,0xb000) or op&0xff00 in (0x8900,0x8b00,0x8d00,0x8f00):
    bits=12 if op&0xf000 in (0xa000,0xb000) else 8
    disp=op&((1<<bits)-1)
    if disp&(1<<(bits-1)):disp-=1<<bits
    target=at+4+2*disp
    if op&0xf000==0xa000:reachable.add(pc+2);work.append(target)
    elif op&0xf000==0xb000:reachable.add(pc+2);work.append(pc+4)
    elif mn.endswith('.s'):reachable.add(pc+2);work.extend((target,pc+4))
    else:work.extend((target,pc+2))
   elif mn in ('rts','rte','jmp','braf'):
    reachable.add(pc+2)
   elif mn in ('jsr','bsrf'):
    reachable.add(pc+2);work.append(pc+4)
   else:work.append(pc+2)
  rows=[]
  for at,op,mn,args in self.decoded:
   if at not in reachable:continue
   if op&0xf000==0xd000 and mn=='mov.l':
    ptr=((at+4)&~3)+4*(op&255)
    value=struct.unpack('<I',self.read(ptr,4))[0]
    row=['literal32','r'+str((op>>8)&15),self.resolve(value)]
   elif op&0xf000==0x9000 and mn=='mov.w':
    ptr=at+4+2*(op&255);value=struct.unpack('<H',self.read(ptr,2))[0]
    row=['literal16','r'+str((op>>8)&15),hex(value)]
   elif op&0xf000 in (0xa000,0xb000) or op&0xff00 in (0x8900,0x8b00,0x8d00,0x8f00):
    disp=op&0xfff if op&0xf000 in (0xa000,0xb000) else op&255
    bits=12 if op&0xf000 in (0xa000,0xb000) else 8
    if disp&(1<<(bits-1)):disp-=1<<bits
    target=at+4+2*disp
    row=[mn,'local+'+hex(target-start) if start<=target<end else self.resolve(target)]
   elif op&0xff00==0xc700:
    target=((at+4)&~3)+4*(op&255)
    row=['mova','local+'+hex(target-start) if start<=target<end else self.resolve(target)]
   else:row=[hex(op),mn,args]
   rows.append(row)
  return rows,end-start,size

def compare(a,b,names):
 out={}
 for name in names:
  if name not in a.symbols or name not in b.symbols:continue
  ra,ea,sa=a.normalized(name);rb,eb,sb=b.normalized(name)
  diffs=[{'index':i,'a':ra[i] if i<len(ra) else None,'b':rb[i] if i<len(rb) else None} for i in range(max(len(ra),len(rb))) if (ra[i] if i<len(ra) else None)!=(rb[i] if i<len(rb) else None)]
  out[name]={'equal':not diffs,'instructions':[len(ra),len(rb)],'effective_bytes':[ea,eb],'symbol_sizes':[sa,sb],'differences':diffs[:12]}
 return out
if __name__=='__main__':
 a,b=map(Image,sys.argv[1:3]);names=sys.argv[4:] or sorted(set(a.symbols)&set(b.symbols))
 result=compare(a,b,names)
 Path(sys.argv[3]).write_text(json.dumps(result,indent=2)+'\n')
 for n,r in result.items():
  if len(sys.argv)>4 or not r['equal']:print(n,r['equal'],r['instructions'],r['effective_bytes'])
