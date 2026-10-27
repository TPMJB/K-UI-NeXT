#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay exact DOA2 source paths that conflict with two proposed RAM arenas.

Supply the owner's original startup and --resource-layouts metadata ZIP. No
retail instruction bytes/assets are embedded, and no input is modified. This
bounded integer execution uses explicit external leaves, records file-load
arguments, and composes paths from a retained, completed transition RAM snapshot.
It is not retail gameplay, console validation, or admission of another arena.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('startup',type=Path)
p.add_argument('metadata_zip',type=Path)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--phase',type=int,choices=range(5),default=0,help='Original mode phase0..4 (default0)')
p.add_argument('--repository-root',type=Path,default=Path(__file__).resolve().parents[1],
               help='Repository containing tools/probe_doa2_resource_memory.py')
args=p.parse_args()
if args.output.exists(): p.error('Output already exists; choose another path')
sys.path.insert(0,str(args.repository_root/'tools'))
from probe_doa2_resource_memory import PartitionSH,EXPECTED_SHA256,EXPECTED_BYTES,BASE
from test_vmu_native_heap_execution import signed,validate_delay_slot
IMAGE=args.startup.read_bytes()
if len(IMAGE)!=EXPECTED_BYTES or hashlib.sha256(IMAGE).hexdigest()!=EXPECTED_SHA256:
    p.error('Original startup identity differs from the reviewed input')
with zipfile.ZipFile(args.metadata_zip) as z:
    info=z.getinfo('startup-info.json')
    if info.file_size>2*1024*1024:p.error('Resource metadata exceeds the bounded size')
    META=json.loads(z.read(info))
if META.get('boot_sha256')!=EXPECTED_SHA256 or META.get('boot_bytes')!=EXPECTED_BYTES:
    p.error('Metadata startup identity differs from the reviewed input')
if META.get('resource_layouts',{}).get('schema')!='kui.doa2.chr-layout-observation.v1':
    p.error('Use the --resource-layouts report with observed CHR headers/tables')
FILES={e['path'].upper():e for e in META['entries'] if not e['directory']}
LAYOUTS={e['path'].upper():e for e in META['resource_layouts']['entries']}
assert len(LAYOUTS)==86
for path,row in LAYOUTS.items():
    h=[int(v,16)for v in row['header_words']]
    assert len(h)==5 and row['file_bytes']==FILES[path]['bytes']
    assert hashlib.sha256(struct.pack('<5I',*h)).hexdigest()==row['header_sha256']
    spans=[]
    for kind,offset,count,stride in [('pointer_table',h[1],h[3],4),('record_table',h[2],h[4],12)]:
        table=row[kind];words=[int(v,16)for v in table['words']]
        raw=struct.pack('<'+'I'*len(words),*words)
        assert len(raw)==count*stride and 20<=offset<=FILES[path]['bytes']-len(raw)
        assert hashlib.sha256(raw).hexdigest()==table['sha256']
        spans.append((offset,offset+len(raw)))
    assert spans[0][1]<=spans[1][0] or spans[1][1]<=spans[0][0]

class WitnessFound(Exception): pass
class StopAtLoad(Exception): pass

class ConsumerSH(PartitionSH):
    STUBS=set()
    def __init__(self,image):
        super().__init__(image)
        self.asset_span=None;self.defined=set();self.asset_reads=0;self.all_reads=[]
    def read(self,address,count=4):
        at=address&0x1fffffff
        if self.asset_span and at<self.asset_span[1] and self.asset_span[0]<at+count:
            assert all(v in self.defined for v in range(at,at+count)), ('unobserved asset byte',hex(address),count,hex(self.executing))
            self.asset_reads+=count
        self.all_reads.append((self.executing,address,count))
        return super().read(address,count)
    def plain(self,op,pc):
        n,m=(op>>8)&15,(op>>4)&15
        if op&0xf0ff==0x4018: # SHLL8
            self.executing=pc;self.instructions+=1
            self.r[n]=self.r[n]<<8&0xffffffff
        elif op&0xf00f==0x6000: # MOV.B @Rm,Rn
            self.executing=pc;self.instructions+=1
            self.r[n]=signed(self.read(self.r[m],1),8)&0xffffffff
        elif op&0xff00==0x8100: # MOV.W R0,@(disp,Rn)
            self.executing=pc;self.instructions+=1
            self.write(self.r[m]+2*(op&15),self.r[0],2)
        else:super().plain(op,pc)
    def seed(self,address,words):
        raw=struct.pack('<'+'I'*len(words),*words)
        self.store_block(address,raw)
        self.defined.update(range(address&0x1fffffff,(address&0x1fffffff)+len(raw)))

class TransitionSH(PartitionSH):
    # These are outside the critical source dataflow. All are listed in output.
    STUBS = {0x8c0145f6, 0x8c0f81a0, 0x8c015fd8, 0x8c100020,
             0x8c0d8418, 0x8c0d8794, 0x8c01660e, 0x8c015b7a}

    def __init__(self, data):
        super().__init__(data)
        self.calls = []
        self.critical = []


    def plain(self, op, pc):
        n, m = (op >> 8) & 15, (op >> 4) & 15
        if op & 0xf00f == 0x6000:  # MOV.B @Rm,Rn
            self.executing = pc; self.instructions += 1
            self.r[n] = signed(self.read(self.r[m], 1), 8) & 0xffffffff
        elif op & 0xff00 == 0x8400:  # MOV.B @(disp,Rm),R0
            self.executing = pc; self.instructions += 1
            self.r[0] = signed(self.read(self.r[(op >> 4) & 15] + (op & 15), 1), 8) & 0xffffffff
        elif op & 0xf00f == 0x000c:  # MOV.B @(R0,Rm),Rn
            self.executing = pc; self.instructions += 1
            self.r[n] = signed(self.read(self.r[0] + self.r[m], 1), 8) & 0xffffffff
        elif op & 0xf00f == 0x0004:  # MOV.B Rm,@(R0,Rn)
            self.executing = pc; self.instructions += 1
            self.write(self.r[0] + self.r[n], self.r[m], 1)
        elif op & 0xf00f == 0x600e:  # EXTS.B Rm,Rn
            self.executing = pc; self.instructions += 1
            self.r[n] = signed(self.r[m] & 255, 8) & 0xffffffff
        else:
            super().plain(op, pc)

class M(ConsumerSH):
    STUBS=set()
    LEAVES={0x8c038a20:'controller state action',0x8c0f7dd8:'task registration',0x8c01f0f6:'resource timer sample',0x8c0f7e4e:'scheduler yield'}
    ALLOWED=[(0x8c01b840,0x8c01bca4),(0x8c039e20,0x8c03a210),(0x8c01d150,0x8c01d1a4),(0x8c01d448,0x8c01e6ca),(0x8c01bcca,0x8c01bd46),(0x8c016082,0x8c0160c8)]
    def __init__(self,image):super().__init__(image);self.events=[];self.load=None;self.dispatch_path=[]
    def read(self,address,count=4):
        if count==2 and address==0x8c0159bc:
            dest,fn=self.r[4:6]
            name=IMAGE[fn-BASE:].split(b'\0',1)[0].decode().removeprefix('c:/').upper()
            size=FILES[name]['bytes'];a=dest&0x1fffffff;n=(size+2047)&~2047
            self.load=dict(entry=f'{address:08x}',caller=f'{self.pr-4:08x}',filename=name,destination=f'{dest:08x}',file_bytes=size,transfer_bytes=n,end=f'{a+n|0x80000000:08x}',candidate_192k_overlap=max(0,min(a+n,0x0ce00000)-max(a,0x0cdd0000)))
            raise StopAtLoad
        if count==2 and address in self.LEAVES:
            self.events.append(dict(entry=f'{address:08x}',caller=f'{self.pr-4:08x}',purpose=self.LEAVES[address],args=[f'{v:08x}'for v in self.r[4:8]]))
            self.r[0]=0;return 0x000b
        if count==2 and address-2 in self.LEAVES:return 0x0009
        if count==2 and address==self.pc and address!=0xdeadbeee:
            assert any(a<=address<b for a,b in self.ALLOWED),('unreviewed instruction fetch',hex(address),hex(self.pr-4))
            if 0x8c01d448<=address<0x8c01e6ca:self.dispatch_path.append(f'{address:08x}')
        return super().read(address,count)
    def run(self,entry,args,stack):
        saved=self.r[8:15];self.pc,self.pr,self.r[15]=entry,0xdeadbeee,stack
        for i,v in enumerate(args):self.r[4+i]=v
        first,ins=len(self.writes),self.instructions
        for _ in range(20000):
            pc=self.pc
            if pc==0xdeadbeee:
                assert self.r[15]==stack and self.r[8:15]==saved
                return self.r[0],self.writes[first:],self.instructions-ins
            op=self.read(pc,2);target,delay,n=None,False,(op>>8)&15
            if op>>12 in (0xa,0xb):
                target,delay=(pc+4+2*signed(op&4095,12))&0xffffffff,True
                if op>>12==0xb:self.pr=pc+4
            elif op&0xf0ff in (0x0003,0x0023):
                target,delay=(pc+4+self.r[n])&0xffffffff,True
                if op&0xf0ff==0x0003:self.pr=pc+4
            elif op&0xf0ff in (0x400b,0x402b):
                target,delay=self.r[n],True
                if op&0xf0ff==0x400b:self.pr=pc+4
            elif op==0x000b:target,delay=self.pr,True
            elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                taken=bool(self.sr&1)==(op>>8 in (0x89,0x8d));delay=op>>8 in (0x8d,0x8f)
                target=pc+4+2*signed(op&255,8) if taken else pc+(4 if delay else 2)
            if target is None:self.plain(op,pc);self.pc=pc+2
            else:
                self.instructions+=1
                if delay:
                    slot=self.read(pc+2,2);validate_delay_slot(slot,pc);self.plain(slot,pc+2)
                self.pc=target
        raise AssertionError('instruction bound exceeded')
    def plain(self,op,pc):
        n,m=(op>>8)&15,(op>>4)&15
        if op&0xff00==0xca00:
            self.executing=pc;self.instructions+=1;self.r[0]^=op&255
        elif op&0xf00f==0x000c:
            self.executing=pc;self.instructions+=1
            v=self.read(self.r[0]+self.r[m],1);self.r[n]=v if v<128 else (v-256)&0xffffffff
        elif op&0xf00f==0x0004:
            self.executing=pc;self.instructions+=1;self.write(self.r[0]+self.r[n],self.r[m],1)
        elif op&0xf00f==0x000d:
            self.executing=pc;self.instructions+=1
            v=self.read(self.r[0]+self.r[m],2);self.r[n]=v if v<32768 else (v-65536)&0xffffffff
        elif op&0xff00==0xc700:
            self.executing=pc;self.instructions+=1;self.r[0]=((pc+4)&~3)+4*(op&255)
        elif op&0xf00f==0x200e:
            self.executing=pc;self.instructions+=1;self.macl=(self.r[n]&0xffff)*(self.r[m]&0xffff)
        elif op&0xff00==0x8400:
            self.executing=pc;self.instructions+=1
            v=self.read(self.r[m]+(op&15),1);self.r[0]=v if v<128 else (v-256)&0xffffffff
        else:super().plain(op,pc)

class CommonSH(ConsumerSH):
    LEAVES={0x8c0145f6:'filesystem load',0x8c08a708:'common model setup',0x8c0f81a0:'GPU binding',0x8c101030:'model compilation',0x8c0d87ba:'common state cleanup'}
    def __init__(self,image):
        super().__init__(image);self.loads=[];self.leaf_calls=[];self.spans=[]
    def read(self,address,count=4):
        if count==2 and address in self.LEAVES:
            self.leaf_calls.append(dict(entry=f'{address:08x}',caller=f'{self.pr-4:08x}',purpose=self.LEAVES[address],args=[f'{v:08x}'for v in self.r[4:8]]))
            if address==0x8c0145f6:
                dest,fn=self.r[4:6];path=IMAGE[fn-BASE:].split(b'\0',1)[0].decode().removeprefix('c:/').upper()
                entry=FILES[path];n=(entry['bytes']+2047)&~2047;a=dest&0x1fffffff
                overlap=max(0,min(a+n,0x0ce00000)-max(a,0x0cdc0000))
                self.loads.append(dict(caller=f'{self.pr-4:08x}',path=path,destination=f'{dest:08x}',file_bytes=entry['bytes'],transfer_bytes=n,end=f'{a+n|0x80000000:08x}',candidate_overlap_bytes=overlap))
                if path in LAYOUTS:
                    l=LAYOUTS[path];h=[int(v,16)for v in l['header_words']]
                    self.seed(dest,h)
                    self.seed(dest+h[1],[int(v,16)for v in l['pointer_table']['words']])
                    self.seed(dest+h[2],[int(v,16)for v in l['record_table']['words']])
                    self.spans.append((a,a+entry['bytes']))
            self.r[0]=0;return 0x000b
        if count==2 and address-2 in self.LEAVES:return 0x0009
        at=address&0x1fffffff
        if any(at<b and a<at+count for a,b in self.spans):
            assert all(x in self.defined for x in range(at,at+count)),('unobserved CHR source',hex(address),count)
            self.asset_reads+=count
        return super().read(address,count)

class Model(CommonSH,M,TransitionSH):
    STUBS=set()
    ALLOWED=[(BASE,BASE+len(IMAGE))]
    LEAVES=dict(CommonSH.LEAVES)
    LEAVES.update(M.LEAVES)
    LEAVES.update({a:'transition rendering/sound/event leaf' for a in TransitionSH.STUBS
                   if a not in CommonSH.LEAVES})

m=Model(IMAGE)
# Source transition input: player0 switches from mode0 to mode10.
for a,v in [(0x8c21d15a,0),(0x8c21d1af,10),(0x8c21a22c,0),
            (0x8c21965e,args.phase^1),(0x8c21d164,args.phase)]:m.write(a,v,1)
_,_,transition_steps=m.run(0x8c0302e4,[],0x8c00e000)
assert transition_steps==11783 and m.sr&0xf0==0xf0
assert m.read(0x8c21a22c,1)==10 and m.read(0x8c21d1c0,1)==10
assert m.read(0x8c21a624)==0x8cd5d000
transition_steps=m.instructions
critical=[dict(pc=f'{pc:08x}',address=f'{a:08x}',value=f'{v:08x}',bytes=n)
          for pc,a,v,n in m.writes if pc and a in (0x8c21a22c,0x8c21d1c0,0x8c21a624)]
efc=next(r for r in m.loads if r['path']=='DOA2/EFC1.BIN')
assert efc['caller']=='8c01c096' and efc['destination']=='acd5d000'
assert efc['end']=='8cdca000' and efc['candidate_overlap_bytes']==40960
transition_file_calls=list(m.loads)
leaves=list(m.leaf_calls)
# Re-enter original controller using precisely the retained RAM, not a newly
# fabricated mode/partition pair. This is path composition, not game scheduling.
_,_,controller_steps=m.run(0x8c039e20,[0],0x8c00c000)
queue=dict(active=m.read(0x8c21865c,1),flag=m.read(0x8c21865c+18,1),
           row=m.read(0x8c21865c+36,1),scratch=f'{m.read(0x8c21a624):08x}')
assert queue==dict(active=1,flag=1,row=1,scratch='8cd5d000')
start=m.instructions
try:m.run(0x8c01d448,[],0x8c00c000)
except StopAtLoad:pass
else:raise AssertionError('Async file argument sink was not reached')
team=m.load
assert team['filename']=='DOA2/TEAM_SPR.BIN' and team['caller']=='8c01e5e4'
assert team['destination']=='acd5d000' and team['end']=='8cdde800'
assert team['candidate_192k_overlap']==59392
team['guarded_interval_overlap']=59392+32
out=dict(schema='kui.doa2.fixed-arena-source-counterexamples.v1',
    startup_sha256=EXPECTED_SHA256,
    metadata_zip_sha256=hashlib.sha256(args.metadata_zip.read_bytes()).hexdigest(),
    validated_headers=len(LAYOUTS),
    observed_resource_metadata=dict(schema=META['resource_layouts']['schema'],
       available=META['resource_layouts']['available'],unavailable=META['resource_layouts']['unavailable'],
       selected_field_bytes=META['resource_layouts']['selected_field_bytes'],reads=META['reads']),
    original_transition=dict(entry='8c0302e4',completed_and_returned=True,selected_mode=10,phase=args.phase,
       instructions=transition_steps,critical_writes=critical,common_file_calls=transition_file_calls),
    rejected_256k=dict(payload_begin='8cdc0000',end='8ce00000',bytes=262144,
       original_file_call=efc,conflicting_payload_bytes=40960),
    rejected_192k=dict(payload_begin='8cdd0000',guarded_begin='8cdcffe0',end='8ce00000',bytes=196608,
       controller_entry='8c039e20',controller_instructions=controller_steps,queue_after_controller=queue,
       dispatcher_entry='8c01d448',dispatcher_instructions=m.instructions-start,
       descriptor='8c15245c',descriptor_words=[f'{v:08x}'for v in struct.unpack_from('<3I',IMAGE,0x8c15245c-BASE)],
       original_async_call=team,conflicting_payload_bytes=59392,conflicting_guarded_bytes=59424),
    explicit_leaf_calls=[dict(entry=key[0],caller=key[1],purpose=key[2],calls=sum(
        1 for r in m.leaf_calls if (r['entry'],r['caller'],r['purpose'])==key))
        for key in sorted({(r['entry'],r['caller'],r['purpose'])for r in m.leaf_calls})],
    model_metadata_gap=dict(binding_entry='8c100d60',initial_model_prefix_bytes=28,
        first_record_offset=24,mesh_header_bytes=80,
        next_record='current+80+u32(current+76)',terminator='u32(record)==0',
        file_offset='CHR data_offset + encoded_pointer - encoded_base',
        encoded_bases=dict(character='0c300000',stage_common='0c400000'),observed=False),
    production_changed=False,permanent_interval_proved=False,
    limits=[
       'The original mode transition completes and returns with its ABI restored. Its controller and dispatcher are then composed through retained RAM; retail task scheduling, gameplay and console hardware are not executed.',
       'Both partition3 and current-mode10 are assigned by the actual transition; the controller/dispatcher model does not seed an arbitrary mode/partition pair.',
       'The source common CHR header/table mutations execute. Named file, rendering, model, event, scheduler and sound leaves are explicit ABI-return stubs.',
       'File-load arguments are original; transfer byte bounds come from the owner filesystem metadata and original2048-byte sector-rounding rule. File-copy payload/GPU operations do not execute.',
       'No safety or admission follows for any alternative arena. Current CHR observations lack model prefixes and chained mesh headers consumed by8c100d60.'
    ])
args.output.write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps(dict(headers=86,transition_instructions=transition_steps,
    controller_instructions=controller_steps,dispatcher_instructions=m.instructions-start,
    rejected_256k_payload_bytes=40960,rejected_192k_guarded_bytes=59424),sort_keys=True))
