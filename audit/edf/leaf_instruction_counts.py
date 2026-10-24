#!/usr/bin/env python3
"""Execute actual SH integer leaf bodies; count instructions, never cycles."""
import json,sys
from pathlib import Path
ROOT=Path('/workspace/scratch/45de2288a575/K-UI-recovery')
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools'),str(Path(__file__).parent)]
from test_vmu_native_heap_execution import SH,MASK,signed
from compare_hotpaths import Image
class Audit(SH):
 def __init__(self,image):
  super().__init__(b'');self.image=image;self.mmio={};self.mmio_reads=[];self.mmio_writes=[]
  for s in image.sections:
   if s[1]==1 and s[2]&2:self.store_block(s[3],image.data[s[4]:s[4]+s[5]])
 def read(self,a,count=4):
  if a>=0xff000000:
   self.mmio_reads.append((a,count));return sum(self.mmio.get(a+i,0)<<(8*i) for i in range(count))
  return super().read(a,count)
 def write(self,a,v,count=4):
  if a>=0xff000000:
   self.mmio_writes.append((a,count));
   for i in range(count):self.mmio[a+i]=(v>>(8*i))&255
   return
  return super().write(a,v,count)
 def plain(self,op,pc):
  n,m=(op>>8)&15,(op>>4)&15
  if op&0xf0ff==0x4010:
   self.instructions+=1;self.r[n]=(self.r[n]-1)&MASK;self.flag(self.r[n]==0)
  elif op&0xff00==0xc800:
   self.instructions+=1;self.flag(not(self.r[0]&(op&255)))
  elif op&0xf00f==0x6000:
   self.instructions+=1;self.r[n]=signed(self.read(self.r[m],1),8)&MASK
  elif op&0xff00==0x8400:
   self.instructions+=1;self.r[0]=signed(self.read(self.r[m]+(op&15),1),8)&MASK
  elif op&0xff00==0x8500:
   self.instructions+=1;self.r[0]=signed(self.read(self.r[m]+(op&15)*2,2),16)&MASK
  elif op&0xff00==0x8000:
   self.instructions+=1;self.write(self.r[m]+(op&15),self.r[0],1)
  elif op&0xf00f==0x2000:
   self.instructions+=1;self.write(self.r[n],self.r[m],1)
  elif op&0xf00f==0x600c:
   self.instructions+=1;self.r[n]=self.r[m]&255
  elif op&0xff00==0x8800:
   self.instructions+=1;self.flag(self.r[0]==(signed(op&255,8)&MASK))
  else:super().plain(op,pc)

def count_copies(image):
 rows=[]
 for length in (0,1,3,4,5,512,2048):
  for misalignment in (0,1):
   m=Audit(image);src=0x8c800000+misalignment;dst=0x8c900000+misalignment
   data=bytes((i*71+29)&255 for i in range(length));m.store_block(src,data)
   result,_,count=m.run(image.symbols['_memcpy'][0],[dst,src,length],0x8cfd0000)
   assert result==dst and m.block(dst,length)==data
   rows.append({'bytes':length,'misalignment':misalignment,'instructions':count})
 return rows

def count_release(image):
 m=Audit(image);port=image.symbols['_port'][0]
 # Saved mode,data,SMR,BRR,SCR,PTR,standby,acquired,fault.
 m.write(port+4,0xc000);m.write(port+8,0x80,2)
 for off,val in ((10,0x11),(11,0x22),(12,0x04),(13,0x33),(14,0),(15,1),(16,0)):m.write(port+off,val,1)
 for address,count,val in ((0xffe00010,1,4),(0xff80002c,4,0x1234),(0xff800030,2,0),(0xffc00004,1,0)):
  m.write(address,val,count)
 m.mmio_reads=[];m.mmio_writes=[]
 _,_,count=m.run(image.symbols['_kui_sci_sd_release'][0],[],0x8cfd0000)
 assert m.read(port+15,1)==0 and m.read(port+16,1)==0
 return {'instructions':count,'mmio_reads':len(m.mmio_reads),'mmio_writes':len(m.mmio_writes),'read_addresses':[hex(a) for a,c in m.mmio_reads]}

def count_map(image,name):
 m=Audit(image)
 if name=='_observe_map':
  control=image.symbols['_kui_vmu_observe_boot_control'][0]
  for off,val in ((0,0),(4,0),(8,4),(12,0x0cfd0000),(32,0xacff0000)):m.write(control+off,val)
 result,_,count=m.run(image.symbols[name][0],[0,0x8c900000,2048,2],0x8cfc0000)
 assert result==0xac900000
 return count

if __name__=='__main__':
 paths=[Path('/workspace/scratch/45de2288a575/edf-binary-audit/rebuilt/resident-sci.elf'),ROOT/'build/retry-standard/resident-sci.elf']
 result={}
 for key,path in zip(('edf','current'),paths):
  i=Image(path);result[key]={'memcpy':count_copies(i),'release':count_release(i),'map_without_cache_operation':count_map(i,'_observe_map' if key=='current' else '_map_guest')}
 out=Path(__file__).with_suffix('.json');out.write_text(json.dumps(result,indent=2)+'\n')
 for k,r in result.items():print(k,'release',r['release'],'map',r['map_without_cache_operation']);print('copies',r['memcpy'])
