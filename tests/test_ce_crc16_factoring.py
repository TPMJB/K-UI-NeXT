#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the shared CE CRC16 table factoring without removing block checks.

The exact old production fragment reconstructs the complete historical source
with a pinned SHA. Pure old/current updates execute exhaustively on host and
bounded emitted SH updates execute with authored table/code memory. Physical
SD, DMA timing and elapsed console performance are outside this test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

BASELINE_SHA='3d849d7d4ba389c025eb5a8d2a141a7888ae3b4ce202d8b995c583fe1a8d9afe'
FRAGMENT_SHA='b70333c356d5b8f835f1587d25ff6129afdc4a5d4a12ba41b418fed5b1362efc'
SOURCE='src/loader/sci_stream.c'
FIXTURE='tests/fixtures/ce-crc16-baseline.txt'
MASK=0xffffffff

HARNESS=r'''
#include <stdio.h>
int main(void) {
    uint32_t pairs=0u;
    for(uint32_t state=0u;state<65536u;state++)for(uint32_t byte=0u;byte<256u;byte++) {
        uint32_t previous=old_crc16(state,(uint8_t)byte);
        uint32_t current=current_crc16(state,(uint8_t)byte);
        if(current!=previous || current>65535u) {
            fprintf(stderr,"CRC mismatch state=%u byte=%u old=%u current=%u\n",
                state,byte,previous,current);return 1;
        }
        ++pairs;
    }
    printf("{\"exhaustive_pairs\":%u,\"normalized_16bit_state\":true}\n",pairs);
    return 0;
}
'''

def sha(body):return hashlib.sha256(body).hexdigest()
def signed(value,bits):return value-(1<<bits) if value&(1<<(bits-1)) else value
def fragments():
    source=(ROOT/SOURCE).read_text();old=(ROOT/FIXTURE).read_text()
    if sha(old.encode())!=FRAGMENT_SHA:raise AssertionError('Historical production CRC fragment changed')
    start=source.index('#ifdef KUI_RETAIL_CE\n/* The byte update is linear:')
    end=source.index('#else\nstatic inline uint16_t crc16',start)
    baseline=source[:start]+old+source[end:]
    if sha(baseline.encode())!=BASELINE_SHA:raise AssertionError('Historical complete SCI source differs')
    current=source[start:end]
    old=old.replace('static inline uint32_t crc16(',
        'uint32_t __attribute__((noinline,noipa)) old_crc16(').replace(
            'ce_crc16_table','old_ce_crc16_table')
    current=current.replace('static inline uint32_t crc16(',
        'uint32_t __attribute__((noinline,noipa)) current_crc16(')
    # Each fragment ends immediately before the native #else.
    return source,baseline,'#include <stdint.h>\n'+old+'#endif\n'+current+'#endif\n'

class Primitive:
    def __init__(self,image,name,state,byte):
        self.at,self.code,_=symbol_bytes(image,name,2)
        self.tables=[]
        for table in ('_old_ce_crc16_table','_ce_crc16_table','_ce_crc16_low','_ce_crc16_high'):
            if table in image[4]:
                at,data,_=symbol_bytes(image,table,1);self.tables.append((at,data))
        self.pc=self.at;self.r=[0x24680000+i*0x1123 for i in range(16)]
        self.r[4:6]=[state,byte];self.before=self.r.copy();self.steps=0;self.table_reads=0;self.t=0
    def read(self,at,width):
        if at&(width-1):raise ValueError('Unaligned CRC primitive access')
        for begin,data in [(self.at,self.code),*self.tables]:
            if begin<=at and at+width<=begin+len(data):
                if begin!=self.at:
                    if width!=2:raise ValueError('CRC table access width changed')
                    self.table_reads+=1
                return int.from_bytes(data[at-begin:at-begin+width],'little')
        raise ValueError('CRC primitive accessed unowned memory')
    def plain(self,w,pc):
        n=(w>>8)&15;m=(w>>4)&15;low=w&0xf00f
        if w==9:return
        if low==0x6003:self.r[n]=self.r[m];return
        if low==0x600c:self.r[n]=self.r[m]&255;return
        if low==0x600d:self.r[n]=self.r[m]&65535;return
        if low==0x200a:self.r[n]^=self.r[m];return
        if low==0x300c:self.r[n]=(self.r[n]+self.r[m])&MASK;return
        if w&0xf0ff==0x4019:self.r[n]>>=8;return
        if w&0xf0ff==0x4009:self.r[n]>>=2;return
        if w&0xf0ff==0x4018:self.r[n]=(self.r[n]<<8)&MASK;return
        if w&0xff00==0xc900:self.r[0]&=w&255;return
        if w&0xff00==0xc800:self.t=int(not(self.r[0]&(w&255)));return
        if w>>12==0xd:self.r[n]=self.read(((pc+4)&~3)+4*(w&255),4);return
        if low==0x000d:
            self.r[n]=signed(self.read((self.r[0]+self.r[m])&MASK,2),16)&MASK;return
        raise ValueError(f'Unadmitted CRC instruction/store/call {w:04x} at {pc:08x}')
    def run(self):
        while True:
            self.steps+=1
            if self.steps>64:raise ValueError('CRC instruction limit')
            w=self.read(self.pc,2)
            if w==0x000b:
                self.steps+=1;self.plain(self.read(self.pc+2,2),self.pc+2);break
            if w&0xff00 in (0x8900,0x8b00,0x8d00,0x8f00):
                pc=self.pc;take=bool(self.t)==(w&0xff00 in (0x8900,0x8d00))
                if w&0x0400:self.steps+=1;self.plain(self.read(pc+2,2),pc+2)
                self.pc=pc+4+2*signed(w&255,8) if take else pc+(4 if w&0x0400 else 2)
                continue
            self.plain(w,self.pc);self.pc+=2
        if self.r[8:16]!=self.before[8:16]:raise ValueError('CRC callee-save damage')
        return self

def resident_hotloop(path):
    """Observe the actual linked byte-update loop, including LTO allocation.

    Select a backward conditional loop containing normalized register CRC
    update. Its closed instruction vocabulary admits no stack/data access
    except a halfword table lookup and PC literals. Larger outer loops may
    save reverse/cursor context; that is not a per-byte accumulator spill.
    """
    image=read_elf(path);at,code,_=symbol_bytes(image,'_kui_sci_stream_take',2)
    table,table_bytes,_=symbol_bytes(image,'_ce_crc16_table',1)
    if len(table_bytes)!=256:raise ValueError('Linked CE table is not the factored128-entry table')
    matches=[]
    def word(pc):return struct.unpack_from('<H',code,pc-at)[0]
    for offset in range(0,len(code)-1,2):
        pc=at+offset;op=word(pc)
        if op&0xff00 not in (0x8900,0x8b00,0x8d00,0x8f00):continue
        start=pc+4+2*signed(op&255,8);end=pc+(4 if op&0x0400 else 2)
        if not at<=start<pc or end>at+len(code):continue
        ops=[(p,word(p)) for p in range(start,end,2)]
        accumulators=[]
        for _,w in ops:
            if w&0xf0ff!=0x4018:continue
            reg=(w>>8)&15
            if any((x&0xf00f)==0x600d and (x>>8)&15==reg and (x>>4)&15==reg for _,x in ops) and any(
                (x&0xf00f)==0x200a and (x>>8)&15==reg for _,x in ops):accumulators.append(reg)
        if len(accumulators)!=1:continue
        allowed=True;table_literal=False
        for p,w in ops:
            low=w&0xf00f
            if w>>12==0xd:
                loc=((p+4)&~3)+4*(w&255)
                if at<=loc and loc+4<=at+len(code):
                    table_literal|=int.from_bytes(code[loc-at:loc-at+4],'little')==table
                continue
            if w>>12==9:continue
            if w&0xff00 in (0x8900,0x8b00,0x8d00,0x8f00):
                target=p+4+2*signed(w&255,8)
                if not start<=target<=end:allowed=False
                continue
            if (w==9 or low in (0x6003,0x600c,0x600d,0x200a,0x300c,0x000d) or
                w&0xf0ff in (0x4019,0x4018,0x4010) or w&0xff00 in (0xc800,0xc900)):
                continue
            allowed=False
        if allowed and table_literal:
            matches.append(dict(begin=hex(start),end=hex(end),accumulator_register='r'+str(accumulators[0]),
                instruction_sites=len(ops),per_byte_memory_stores=0,per_byte_stack_accesses=0,
                loop_sha256=sha(code[start-at:end-at])))
    if len(matches)!=1:raise ValueError('Could not uniquely qualify register-only linked CRC byte loop')
    return dict(elf=str(path),elf_sha256=sha(image[0]),take_sha256=sha(code),table_bytes=len(table_bytes),
        table_sha256=sha(table_bytes),**matches[0],hardware_executed=False)

def run(command):
    return subprocess.run(command,cwd=ROOT,text=True,capture_output=True,
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
def require(result,label):
    if result.returncode:raise AssertionError(label+'\n'+result.stdout[-2000:]+result.stderr)
    return result.stdout

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path)
    parser.add_argument('--resident',type=Path,action='append',default=[])
    parser.add_argument('--toolchain',type=Path);args=parser.parse_args()
    source,baseline,pure=fragments();cc=os.environ.get('HOST_CC','cc')
    report=dict(schema='kui.ce.crc16-table-factoring.v1',status='FAIL',
        source_sha256=sha(source.encode()),historical_source_sha256=sha(baseline.encode()),
        historical_fragment_sha256=FRAGMENT_SHA,table_bytes_before=512,table_bytes_after=256,
        hardware_executed=False,limits=[
            'Emitted pure primitive checks authored code/table memory only; no SCI/SD DMA or console executes.',
            'Factoring adds high-bit testing and conditional XOR per byte; no speedup or elapsed-time claim.'])
    with tempfile.TemporaryDirectory(prefix='kui-ce-crc-factor-') as temporary:
        folder=Path(temporary);test=folder/'crc.c';test.write_text(pure+HARNESS)
        binary=folder/'crc'
        flags=[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wpedantic',
            '-DKUI_RETAIL_CE=1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie']
        require(run([*flags,str(test),'-o',str(binary)]),'host CRC build')
        report['host']=json.loads(require(run([str(binary)]),'exhaustive CRC'))
        mutant=folder/'mutant.c';mutant.write_text((pure+HARNESS).replace(
            'if(index&128u)value^=0x9188u;',
            'if(index&128u)value^=0x9189u;',1))
        require(run([*flags,str(mutant),'-o',str(binary)]),'mutated table build')
        result=run([str(binary)])
        if result.returncode!=1 or 'CRC mismatch' not in result.stderr:raise AssertionError('Table corruption admitted')
        report['table_mutation_refused']=True
        previous=folder/'old-sci-stream.c';previous.write_text(baseline)
        report['native_preprocessing']={}
        for name,defines in {'native':(), 'native-background':('KUI_RETAIL_ASYNC=1','KUI_RETAIL_GD_ASYNC=1')}.items():
            flags=[cc,'-E','-P','-Iinclude','-Isrc/loader',*['-D'+x for x in defines]]
            current=require(run([*flags,SOURCE]),'current native preprocessing')
            old=require(run([*flags,str(previous)]),'old native preprocessing')
            if current!=old:raise AssertionError('Native SCI preprocessing changed: '+name)
            report['native_preprocessing'][name]=True
        toolchain=args.toolchain or ROOT/'.deps/sh-elf'
        if (toolchain/'bin/sh-elf-gcc').exists():
            test.write_text(pure);elf=folder/'crc.elf'
            require(run([str(toolchain/'bin/sh-elf-gcc'),'-Os','-DKUI_RETAIL_CE=1','-m4-single','-ml',
                '-ffreestanding','-fno-builtin','-mfixed-range=fr0-fr15,fpul-fpul',
                '-nostdlib','-Wl,-e,_current_crc16','-Wl,-Ttext=0x8c004000',str(test),'-o',str(elf)]),'emitted CRC link')
            image=read_elf(elf);cases=0;counts={'old':set(),'current':set()};reads={}
            for state in (0,1,15,16,127,128,255,256,1023,4095,4096,32767,32768,65534,65535):
                for byte in range(256):
                    old=Primitive(image,'_old_crc16',state,byte).run()
                    new=Primitive(image,'_current_crc16',state,byte).run()
                    if old.r[0]!=new.r[0] or new.r[0]>65535:raise AssertionError('Emitted CRC mismatch')
                    cases+=1;counts['old'].add(old.steps);counts['current'].add(new.steps)
                    reads.update(old=old.table_reads,current=new.table_reads)
            report['emitted_sh']=dict(cases=cases,instructions_per_update={k:sorted(v) for k,v in counts.items()},
                table_reads_per_update=reads,stack_bytes=0,elf_sha256=sha(image[0]))
        report['actual_resident_hotloops']=[resident_hotloop(path) for path in args.resident]
        report['status']='PASS'
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
