#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute linked CE helper raw reads; only physical SPI and SCI MMIO are authored."""
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_retail_stack import load,worst
from test_smash_phase_clock_linked import ClockCPU

STOP,TRANSFER,SELECT,OUT,STACK=0x8c003000,0x8c700040,0x8c700080,0x8c600020,0x8c00d000
SMR,BRR,SCR,SSR,RDR,SCMR,SPTR=0xffe00000,0xffe00004,0xffe00008,0xffe00010,0xffe00014,0xffe00018,0xffe0001c
STB,PCTR,PDTR=0xffc00004,0xff80002c,0xff800030
WIDTH={SMR:1,BRR:1,SCR:1,SSR:1,RDR:1,SCMR:1,SPTR:1,STB:1,PCTR:4,PDTR:2}

def require(ok,message):
    if not ok:raise ValueError(message)
def sha(data):return hashlib.sha256(data).hexdigest()
def crc16(data):
    value=0
    for byte in data:
        value^=byte<<8
        for _ in range(8):value=((value<<1)^(0x1021 if value&0x8000 else 0))&65535
    return value

def layout(build,cc):
    """Read actual target layouts, without guessing the compact profile's offsets."""
    members=('engine.manifest','engine.active','engine.opened','engine.in_irq','engine.hooked',
        'engine.release.armed','engine.release.depth','shared.service.initialized','shared.service.pending',
        'shared.service.executing','shared.card.device.sd','shared.card.device.sd.blocks',
        'shared.card.device.sd.high_capacity','shared.card.device.sd.ready','shared.card.device.sd.slow',
        'shared.card.device.sd.bus.transfer','shared.card.device.sd.bus.select','shared.stream.stats.polled',
        'shared.port.work','shared.port.slow','shared.port.acquired','shared.port.fault')
    source='#include "retail_async.h"\nconst unsigned raw_layout[] __attribute__((section(".raw_layout")))={sizeof(kui_retail_async_region_type),'+','.join(
        'offsetof(kui_retail_async_region_type,'+name+')' for name in members)+'};\n'
    flags=shlex.split((build/'config').read_text().splitlines()[2])
    with tempfile.TemporaryDirectory(prefix='kui-ce-raw-layout-') as folder:
        obj,wire=Path(folder)/'layout.o',Path(folder)/'layout.bin'
        subprocess.run([*shlex.split(cc),*flags,'-fno-lto','-x','c','-c','-o',str(obj),'-'],
            input=source,text=True,cwd=ROOT,check=True,capture_output=True)
        objcopy=shlex.split(cc)[0].replace('gcc','objcopy')
        subprocess.run([objcopy,'-O','binary','-j','.raw_layout',str(obj),str(wire)],check=True,capture_output=True)
        values=struct.unpack('<'+'I'*(len(members)+1),wire.read_bytes())
    return dict(zip(('bytes',*members),values))

class RawCPU(ClockCPU):
    def __init__(self,image,shape,case):
        super().__init__(image);self.strict=False
        self.shape=shape;self.case=case;self.region=image[4]['_standalone_reader_region'][0]
        self.store_block(self.region,bytes(shape['bytes']))
        self.registers={address:0 for address in WIDTH};self.registers[SSR]=0x84
        self.registers[PDTR]=0x80;self.saved_registers=self.registers.copy()
        self.io=[];self.spi=[];self.selected=False;self.packet=[];self.queue=deque();self.commands=[]
        self.fetches=[];self.lowest_sp=STACK;self.payload=bytes((i*31+17)&255 for i in range(512))
        self.put('engine.manifest',image[4]['_manifest'][0])
        self.store_block(image[4]['_manifest'][0],struct.pack('<3Q',2048,1,2048))
        self.put('shared.service.initialized',1)
        self.put('shared.card.device.sd.blocks',2048,8)
        self.put('shared.card.device.sd.high_capacity',1,1)
        self.put('shared.card.device.sd.ready',1,1)
        self.put('shared.card.device.sd.bus.transfer',TRANSFER)
        self.put('shared.card.device.sd.bus.select',SELECT)
        if case=='busy':self.put('engine.active',1)
        self.store_block(OUT-32,bytes([0xa7])*576)
        self.strict=True

    def put(self,name,value,width=4):
        self.write(self.region+self.shape[name],value,width)

    def read(self,address,count=4):
        if getattr(self,'strict',False) and not 0x0c000000<=address&0x1fffffff<0x0d000000:
            require(address in WIDTH and WIDTH[address]==count,f'Unexpected MMIO read {address:08x}/{count}')
            self.io.append(('read',address,count));return self.registers[address]
        # Bypass observational clock's deliberately narrower MMIO contract.
        return super(ClockCPU,self).read(address,count)

    def write(self,address,value,count=4):
        if getattr(self,'strict',False) and not 0x0c000000<=address&0x1fffffff<0x0d000000:
            require(address in WIDTH and WIDTH[address]==count,f'Unexpected MMIO write {address:08x}/{count}')
            self.io.append(('write',address,count));self.registers[address]=value&((1<<(8*count))-1);return
        return super(ClockCPU,self).write(address,value,count)

    def transfer(self,value):
        self.spi.append(value)
        if not self.selected:return 255
        if self.packet or value in (0x52,0x4c):
            self.packet.append(value)
            if len(self.packet)==6:
                command=self.packet[0];self.commands.append(bytes(self.packet));self.packet=[]
                if command==0x52:
                    checksum=crc16(self.payload)^(1 if self.case=='crc' else 0)
                    self.queue.extend([0,0xfd] if self.case=='token' else
                        [0,0xfe,*self.payload,checksum>>8,checksum&255])
                else:self.queue.extend([255,1 if self.case=='stop' else 0,255])
            return 255
        return self.queue.popleft() if self.queue else 255

    def step(self):
        self.lowest_sp=min(self.lowest_sp,self.r[15])
        if self.pc in (TRANSFER,SELECT):
            # These are peripheral callbacks, not substituted reader functions.
            value=self.transfer(self.r[5]&255) if self.pc==TRANSFER else 0
            if self.pc==SELECT:self.selected=bool(self.r[5])
            self.put('shared.port.work',self.read(self.region+self.shape['shared.port.work'])+8)
            self.put('shared.port.slow',0,1);self.registers[SCR]=0x30;self.registers[BRR]=0
            for r in range(1,8):self.r[r]=0xa5a50000+r
            self.r[0]=value;self.sr^=1;self.pc=self.pr;return
        if self.pc==self.fetch_at:self.fetches.append(self.r[4:7].copy())
        return super().step()

    def plain(self,op,pc):
        if op==0x0018:  # SETT
            self.instructions+=1;self.sr|=1;return
        return super().plain(op,pc)

def exercise(image,shape,case):
    cpu=RawCPU(image,shape,case);cpu.fetch_at=image[4]['_kui_sci_stream_fetch.constprop.0'][0]
    cpu.r=[0x53530000+i*0x12345 for i in range(16)];cpu.r[4:6]=[100,OUT];cpu.r[15]=STACK
    cpu.sr=0x700000f0;cpu.pr=STOP;cpu.pc=image[4]['_kui_vc2_ce_vmu_read_card'][0]
    nonvolatile=cpu.r[8:15].copy();cpu.writes=[];cpu.instructions=0
    for _ in range(1000000):
        if cpu.pc==STOP:break
        cpu.step()
    else:raise ValueError(f'Raw read did not complete: {case} at{cpu.pc:08x}')
    expected={'ok':0,'crc':5,'token':4,'stop':7,'busy':2}[case]
    require(cpu.r[0]==expected,f'{case}: expected status{expected}, got{cpu.r[0]}')
    require(cpu.r[8:15]==nonvolatile and cpu.r[15]==STACK and cpu.sr&~1==0x700000f0,
        case+': raw reader changed caller nonvolatile/IRQ state')
    require(cpu.block(OUT-32,32)==bytes([0xa7])*32 and cpu.block(OUT+512,32)==bytes([0xa7])*32,
        case+': raw output escaped its512-byte buffer')
    copied=case in ('ok','stop')
    require(cpu.block(OUT,512)==(cpu.payload if copied else bytes([0xa7])*512),case+': output integrity differs')
    require(all(args[0]==100 and args[1]==1 for args in cpu.fetches),'CE helper read did not force PIO')
    quarantined=case=='stop'
    require(bool(cpu.read(cpu.region+shape['shared.port.acquired'],1))==quarantined,
        case+': raw lease/quarantine differs')
    require(bool(cpu.read(cpu.region+shape['shared.port.fault'],1))==quarantined,
        case+': sticky fault differs')
    if case!='busy':
        require([x[0] for x in cpu.commands]==[0x52,0x4c],case+': CMD18/CMD12 lifecycle differs')
        if not quarantined:require(cpu.registers==cpu.saved_registers,case+': SCI/GPIO registers not restored')
    else:require(not cpu.spi and not cpu.io and not cpu.fetches,'Busy preflight performed I/O')
    quarantine_reentry=False
    if quarantined:
        before=(len(cpu.spi),len(cpu.io),len(cpu.fetches))
        cpu.r[4:6]=[101,OUT];cpu.pc=image[4]['_kui_vc2_ce_vmu_read_card'][0];cpu.pr=STOP
        for _ in range(1000):
            if cpu.pc==STOP:break
            cpu.step()
        else:raise ValueError('Quarantined raw reader did not refuse reentry')
        require(cpu.r[0]==7 and before==(len(cpu.spi),len(cpu.io),len(cpu.fetches)),
            'Sticky quarantine reentry accessed the card')
        quarantine_reentry=True
    return dict(case=case,result=expected,instructions=cpu.instructions,stack_bytes=STACK-cpu.lowest_sp,
        spi_bytes=len(cpu.spi),commands=[x.hex() for x in cpu.commands],fetch_force_PIO=[x[1]for x in cpu.fetches],
        dma_register_accesses=0,output_bytes_checked=512,bus_released=not quarantined,sticky_quarantine=quarantined,
        quarantine_reentry_no_IO=quarantine_reentry)

def helper_span(image,shape):
    """Replay the admitted helper's96 one-sector calls against one live reader state."""
    cpu=RawCPU(image,shape,'ok');cpu.fetch_at=image[4]['_kui_sci_stream_fetch.constprop.0'][0]
    cpu.store_block(OUT-32,bytes([0xa7])*(49152+64));expected=[]
    for index in range(96):
        cpu.payload=bytes((i*31+17+index*7)&255 for i in range(512));expected.append(cpu.payload)
        cpu.r=[0x53530000+i*0x12345 for i in range(16)];cpu.r[4:6]=[100+index,OUT+512*index]
        cpu.r[15]=STACK;cpu.sr=0x700000f0;cpu.pr=STOP;cpu.pc=image[4]['_kui_vc2_ce_vmu_read_card'][0]
        for _ in range(100000):
            if cpu.pc==STOP:break
            cpu.step()
        else:raise ValueError('Helper-span read did not complete')
        require(cpu.r[0]==0 and not cpu.read(cpu.region+shape['shared.port.acquired'],1) and
            cpu.registers==cpu.saved_registers,'Helper-span read failed/restoration differs')
    require(cpu.block(OUT,49152)==b''.join(expected),'Helper-span output has unchecked/changed bytes')
    require(cpu.block(OUT-32,32)==cpu.block(OUT+49152,32)==bytes([0xa7])*32,'Helper-span escaped output')
    require(len(cpu.fetches)==96 and all(args[0]==100+i and args[1]==1 for i,args in enumerate(cpu.fetches)) and
        len(cpu.commands)==192 and all(int.from_bytes(cpu.commands[2*i][1:5],'big')==100+i for i in range(96)),
        'Helper-span receive policy or lifecycle differs')
    return dict(raw_calls=96,bytes=49152,exact_output_sha256=sha(cpu.block(OUT,49152)),
        individually_CRC_checked_blocks=96,force_PIO_calls=96,dma_register_accesses=0,
        acquired_leases_restored=96,instructions=cpu.instructions,stack_bytes=STACK-cpu.lowest_sp)


def at(image,address,count):
    rows=[p for p in image[2]if p[0]==1 and p[2]<=address and address+count<=p[2]+p[4]]
    require(len(rows)==1,'Unowned initialized linked target')
    p=rows[0];return image[0][p[1]+address-p[2]:p[1]+address-p[2]+count]


def linked_target(old,new,before,after):
    """Authorize relocation by the same linked symbol/offset, or exact unique string."""
    matches=[]
    for name,(address,size,kind,section) in old[4].items():
        if name not in new[4]:continue
        other,other_size,other_kind,_=new[4][name]
        if kind!=other_kind:continue
        if (before==address and after==other) or \
           (kind in (1,2) and size and other_size and 0<=before-address<size and before-address==after-other<other_size):
            matches.append((name,before-address,kind,size,other_size))
    if matches:
        name,offset,kind,size,other_size=sorted(matches,key=lambda x:(x[1]!=0,x[0]))[0]
        if kind==1:
            require(size==other_size and at(old,old[4][name][0],size)==at(new,new[4][name][0],size),
                'Relocated data object changed: '+name)
        return dict(symbol=name,offset=offset,before=hex(before),after=hex(after))
    def string(image,address):
        result=bytearray()
        for i in range(128):
            value=at(image,address+i,1);result+=value
            if value==b'\0':return bytes(result)
        raise ValueError('Relocation target has no bounded string owner')
    value=string(old,before)
    require(len(value)>1 and value==string(new,after),'Relocated anonymous string changed')
    require(old[0].count(value)==new[0].count(value)==1,'Relocated anonymous string is ambiguous')
    return dict(string_sha256=sha(value),bytes=len(value),before=hex(before),after=hex(after))


def relocation_only(old,new,name):
    old_at,before,_=symbol_bytes(old,name,2);new_at,after,_=symbol_bytes(new,name,2)
    require(len(before)==len(after),'Undeclared function body size change: '+name)
    normalized=bytearray(after);relocations=[];covered=set()
    for offset in range(0,len(before),2):
        if offset in covered or before[offset:offset+2]==after[offset:offset+2]:continue
        a,b=struct.unpack_from('<H',before,offset)[0],struct.unpack_from('<H',after,offset)[0]
        if a>>12 in (10,11) and a>>12==b>>12:
            def branch(pc,op):
                displacement=op&4095
                if displacement&2048:displacement-=4096
                return (pc+4+2*displacement)&0xffffffff
            target=linked_target(old,new,branch(old_at+offset,a),branch(new_at+offset,b))
            normalized[offset:offset+2]=before[offset:offset+2]
            relocations.append(dict(offset=offset,kind='same linked branch target',target=target));continue
        word=((old_at+offset)&~3)-old_at
        require(0<=word<=len(before)-4 and (new_at+word)%4==0,
            'Changed instruction is not a linked branch/literal relocation: '+name)
        def references(body,pc,target):
            return [i for i in range(0,len(body),2) if (struct.unpack_from('<H',body,i)[0]>>12)==13 and
                ((pc+i+4)&~3)+4*(struct.unpack_from('<H',body,i)[0]&255)==target]
        require(references(before,old_at,old_at+word) and references(after,new_at,new_at+word),
            'Changed pointer is not a referenced MOV.L literal: '+name)
        target=linked_target(old,new,struct.unpack_from('<I',before,word)[0],struct.unpack_from('<I',after,word)[0])
        normalized[word:word+4]=before[word:word+4];covered.update((word,word+2))
        relocations.append(dict(offset=word,kind='same linked MOV.L target',target=target))
    require(normalized==before,'Undeclared instruction change remains after relocation proof: '+name)
    return dict(before_address=hex(old_at),after_address=hex(new_at),bytes=len(before),
        before_sha256=sha(before),after_sha256=sha(after),relocations=relocations)


def return_delta(build,image,reference,proof_path=None):
    old=read_elf(reference/'resident-sci.elf')
    def flags(directory):
        result=[]
        for flag in shlex.split((directory/'config').read_text().splitlines()[2]):
            if flag.startswith(('-DKUI_BUILD_ID=','-DKUI_SMASH_CE_RETURN=')):continue
            if flag.startswith('-I'):
                path=Path(flag[2:]).resolve()
                if path.is_relative_to(directory.resolve()):flag='-I<build>/'+str(path.relative_to(directory.resolve()))
            result.append(flag)
        return result
    require(flags(build)==flags(reference),'Return reference uses different resident compiler/profile flags')
    require('-DKUI_SMASH_CE_RETURN=1' in (build/'config').read_text().splitlines()[2],
        'Explicit return mode requires a RETURN1 resident')
    require('-DKUI_SMASH_CE_RETURN=1' not in (reference/'config').read_text().splitlines()[2],
        'Return reference must retain RETURN0')
    old_functions={n for n,(a,size,kind,_)in old[4].items()if kind==2 and size and 0x8c004000<=a<0x8c008000}
    new_functions={n for n,(a,size,kind,_)in image[4].items()if kind==2 and size and 0x8c004000<=a<0x8c008000}
    require(old_functions==new_functions,'Return mode added/removed an undeclared typed low function')
    changed={'_native_reader_report.isra.0':'src/loader/vc2_ce_vmu_late_report.inc',
        '_kui_retail_resident_dispatch':'src/loader/vc2_ce_vmu_gate.inc'}
    require(set(changed)<=old_functions,'Missing intended return/diagnostic source owner')
    normal={n:relocation_only(old,image,n)for n in sorted(old_functions-set(changed))}
    # A deliberately changed ordinary instruction must never be covered by
    # the explicit two-body allowance or by pointer/branch normalization.
    name='_kui_sci_stream_take';address,body,section=symbol_bytes(image,name,2)
    poisoned=bytearray(image[0]);location=section[4]+address-section[3]
    poisoned[location:location+2]=struct.pack('<H',0 if body[:2]!=bytes(2) else 9)
    rejected=False
    try:relocation_only(old,(bytes(poisoned),*image[1:]),name)
    except ValueError:rejected=True
    require(rejected,'Return-mode relocation proof accepted an undeclared instruction change')
    exceptions={}
    for name,source in changed.items():
        a,b,_=symbol_bytes(old,name,2);c,d,_=symbol_bytes(image,name,2)
        require(b!=d,'Declared return body change is absent: '+name)
        exceptions[name]=dict(source=source,source_sha256=sha((ROOT/source).read_bytes()),
            reference_address=hex(a),current_address=hex(c),reference_bytes=len(b),current_bytes=len(d),
            reference_sha256=sha(b),current_sha256=sha(d))
    sources=('src/loader/vc2_ce_vmu_gate.inc','src/loader/vc2_ce_vmu_late_report.inc',
        'src/loader/retail_resident.c','src/loader/retail_async.c','tests/test_smash_ce_return_late.py')
    crossbinding=dict(source_sha256={p:sha((ROOT/p).read_bytes())for p in sources},
        separate_late_gate_proof_required=True,late_gate_execution_claimed_here=False)
    if proof_path:
        wire=proof_path.read_bytes();proof=json.loads(wire)
        require(proof.get('schema')=='kui.smash-ce.late-copy.v1' and proof.get('status')=='PASS',
            'Separate late-copy/gate proof did not pass')
        for source in ('src/loader/vc2_ce_vmu_gate.inc','src/loader/smash_ce_return_copy.S'):
            require(proof['source_sha256'][source]==sha((ROOT/source).read_bytes()),'Separate gate proof source is stale')
        require(proof.get('test_sha256')==sha((ROOT/'tests/test_smash_ce_return_late.py').read_bytes()),
            'Separate gate proof test is stale')
        require(proof['controller_elf_sha256']==sha((build/'ce-return.elf').read_bytes()) and
            proof['helper_binary_sha256']==sha((build/'helper/helper.bin').read_bytes()),
            'Separate gate proof uses another controller/helper')
        crossbinding.update(late_gate_report_sha256=sha(wire),late_gate_execution_claimed_here=False,
            linked_controller_and_helper_crossbound=True)
    return dict(reference_resident_sha256=sha(old[0]),reference_config_sha256=sha((reference/'config').read_bytes()),
        current_config_sha256=sha((build/'config').read_bytes()),
        intended_source_body_changes=exceptions,other_typed_functions=normal,
        other_typed_function_bodies= len(normal),all_other_typed_instructions_identical_after_validated_relocations=True,
        negative_control_undeclared_instruction_refused=True,
        separate_gate_proof=crossbinding,
        limits='Checks every typed low C/libgcc function. The two intentional source-owned bodies have independent gate/copy tests; untyped assembly/data is outside this delta check. Does not claim entire ordinary-reader identity.')

def audit(build,accepted,cc,return_reference=None,return_proof=None):
    image=read_elf(build/'resident-sci.elf');shape=layout(build,cc)
    at,body,_=symbol_bytes(image,'_kui_vc2_ce_vmu_read_card',2)
    raw=(build/'vc2-raw.bin').read_bytes();reference=accepted.read_bytes()
    require(at==0xac008100 and raw==body+bytes(512-len(body)),'Raw gate/padded island mismatch')
    require(reference.count(raw)==1,'CE raw gate is not uniquely byte-identical to accepted72')
    part=next(p for p in image[2]if p[0]==1 and p[2]==0x8c004000)
    normal=image[0][part[1]:part[1]+part[4]];labels=re.findall(rb'BUILD ([0-9a-f]{12})\x00',normal)
    require(len(labels)==1,'Ambiguous reader label');normalized=normal.replace(labels[0],b'0'*12)
    reference_normalized=re.sub(rb'BUILD [0-9a-f]{12}\x00',b'BUILD '+b'0'*12+b'\x00',reference)
    identical=reference_normalized.count(normalized)==1
    if return_reference is None:require(identical,'Ordinary CE reader changed beyond its build label')
    delta=return_delta(build,image,return_reference,return_proof)if return_reference else None
    symbols=image[4];require(symbols['__retail_hook_stack_bottom'][0]==0x8c007ea0 and
        symbols['__retail_hook_stack'][0]==0x8c008000,'CE low352-byte stack changed')
    cases=[exercise(image,shape,case)for case in ('ok','crc','token','stop','busy')]
    require(max(x['stack_bytes']for x in cases)<=352,'Linked raw read exceeded low-stack bound')
    graphs=sorted(build.glob('resident-sci.elf.ltrans*.ci'));require(graphs,'Missing compiler stack graphs')
    frames,calls=load(graphs);paths=worst(frames,calls,roots_override={'kui_vc2_ce_vmu_read_card':0})
    require(paths['kui_vc2_ce_vmu_read_card']['bytes']+64<=352-16,'Actual bus callback closure exceeds guarded stack')
    return dict(schema='kui.smash.ce.raw-policy.linked.v1',status='PASS',
        resident_elf_sha256=sha(image[0]),accepted_package_sha256=sha(reference),
        raw_sha256=sha(raw),raw_code_bytes=len(body),raw_island_bytes=512,
        accepted_raw_byte_identical=True,ordinary_reader_identical_except_build=identical,
        return_source_delta=delta,
        normal_reader_bytes=len(normal),normal_stack_bytes=352,target_layout=shape,cases=cases,
        helper_span=helper_span(image,shape),
        compiler_stack=dict(roots=paths,library_and_bottom_guard_reserve=64,top_reserve=16,
            graph_sha256={p.name:sha(p.read_bytes())for p in graphs}),
        limits='Actual linked open/fetch/PIO receive/take CRC/copy/CMD12/release execute. Only physical SPI callbacks and SCI MMIO are authored; no hardware, cache, interrupt or timing claim.')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--accepted',type=Path,required=True);parser.add_argument('--cc',default=os.environ.get('SH_CC','sh-elf-gcc'))
    parser.add_argument('--return-reference',type=Path,help='Fresh same-source RETURN0 build; enables the explicit narrow RETURN1 typed-code delta audit')
    parser.add_argument('--return-proof',type=Path,help='Optional separate late-copy/gate JSON, crossbound to current sources/controller/helper')
    parser.add_argument('--report',type=Path);args=parser.parse_args()
    require(not args.return_proof or args.return_reference,'--return-proof requires --return-reference')
    result=audit(args.build,args.accepted,args.cc,args.return_reference,args.return_proof);rendered=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(rendered)
    print(rendered,end='')
if __name__=='__main__':main()
