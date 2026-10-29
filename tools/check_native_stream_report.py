#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked terminal SCI counter report with a display-only sink.

The existing integer SH model runs the real report helper. Only the display
formatter is replaced, so this checks counter selection and ABI without
emulating the unrelated terminal framebuffer clear. No transport is called.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_native_dual_readers import ScalarOps
from check_retail_stack import base,load
from check_vmu_native_sdk_sci_route import SDKCPU,RETURN,assert_c_abi
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from test_native_dual_readers import object_section
from test_vmu_native_heap_execution import SH

HELPER='_native_stream_report'
REGION,TOP=0x8c420400,0x8c007cc0
FIELDS=('blocks','polled','starts','kept','overruns','crc_errors','token_errors',
        'foreign','repaired','ahead','deferred')
ROWS=(('SD DMA PIO CMD KEEP',0,4),('SD OVR CRC TOK FOR',4,4),('SD FIX AHEAD DEFER',8,3))


def require(ok,message):
    if not ok:raise ValueError(message)


def console_layout(resident,path):
    """Use the wrapper's real SH flags and independently named offsetofs."""
    compiler=shutil.which('sh-elf-gcc')
    require(compiler is not None,'sh-elf-gcc must be on PATH for the console layout proof')
    require('__kui_native_standalone_reader_resident' in resident[4],
            'Terminal stream control is restricted to the standalone reader')
    makefile='Makefile.native-standalone-stream-report'
    variable='RESIDENT_FLAGS'
    fields={'pointer_bytes':'sizeof(uintptr_t)','region_bytes':'sizeof(kui_retail_async_region_type)',
            'stats_bytes':'sizeof(struct kui_sci_stream_stats)',
            **{name:'offsetof(kui_retail_async_region_type,shared.stream.stats.'+name+')' for name in FIELDS}}
    with tempfile.TemporaryDirectory(prefix='kui-stream-report-layout-') as temporary:
        directory=Path(temporary);flags_file=directory/'flags'
        (directory/'flags.mk').write_text('include '+str(ROOT/makefile)+'\n'+
            'dump-stream-flags:\n\t$(file >'+str(flags_file)+', $('+variable+'))\n\t@true\n')
        subprocess.run(['make','--no-print-directory','-s','-f',str(directory/'flags.mk'),'dump-stream-flags'],cwd=ROOT,check=True)
        flags=[x for x in shlex.split(flags_file.read_text()) if x.startswith(('-D','-I'))]
        source=directory/'layout.c';binary=directory/'layout.o'
        source.write_text('#include <stddef.h>\n#include "retail_async.h"\n'+
            'const uint32_t stream_layout[] __attribute__((section(".rodata.stream_layout"),used))={'+
            ','.join(fields.values())+'};\n')
        subprocess.run([compiler,'-std=c11','-Os','-m4-single','-ml','-ffreestanding',*flags,
                        '-c',str(source),'-o',str(binary)],cwd=ROOT,check=True)
        data=object_section(binary,'.rodata.stream_layout')
    require(len(data)==4*len(fields),'Console layout scalar count differs')
    result=dict(zip(fields,struct.unpack('<'+str(len(fields))+'I',data)))
    require(result['pointer_bytes']==4 and result['stats_bytes']==44,'Native counter/pointer geometry differs')
    require([result[n]-result['blocks'] for n in FIELDS]==list(range(0,44,4)),
            'Named SCI counter fields are not the expected contiguous eleven words')
    return result


class ReportCPU(ScalarOps,SDKCPU):
    def __init__(self,resident,layout,values,seed=0,mask=15,bank=0):
        backing=SH(b'')
        for kind,offset,address,_,count,_,_,_ in resident[2]:
            if kind==1 and count:backing.store_block(address,resident[0][offset:offset+count])
        self.memory=bytes(backing.memory);self.overlay={};self.writes=[]
        rng=random.Random(seed);self.r=[rng.getrandbits(32) for _ in range(16)];self.r[15]=TOP
        self.other=[rng.getrandbits(32) for _ in range(8)]
        self.sr=0x40000100|(bank<<29)|(mask<<4)|(seed&1);self.pr=RETURN
        self.mach=self.macl=self.gbr=self.fpscr=self.fpul=seed
        self.fr=[rng.getrandbits(32) for _ in range(32)]
        self.control=dict(vbr=0x8c00f400,ssr=seed,spc=seed,sgr=seed,dbr=seed)
        self.symbols=resident[4];self.pc=self.executing=self.instructions=0;self.watched_slots=set();self.table_reads=[]
        self.iprb=0;self.iprb_writes=[];self.vbr_writes=[];self.layout=layout;self.shown=[]
        self.targets={self.symbols['_retail_display_values'][0]}
        self.begin,self.body,_=symbol_bytes(resident,HELPER,2)
        self.write(self.symbols['_kui_retail_async_region_storage'][0],REGION)
        for at in range(0,layout['region_bytes'],4):self.write(REGION+at,(at*391+seed)&0xffffffff)
        for name,value in zip(FIELDS,values):self.write(REGION+layout[name],value)
        self.expected=[(label,tuple(values[start:start+count])) for label,start,count in ROWS]
        self.region_before=self.bytes_at(REGION,layout['region_bytes'])
        self.writes=[];self.before=self.snapshot()

    def bytes_at(self,address,count):
        return bytes(self.read(address+i,1) for i in range(count))

    def read(self,address,count=4):
        physical=address&0x1fffffff
        require(0x0c000000<=physical<=0x0d000000-count,'Terminal report accessed MMIO/non-RAM')
        if count==2 and address==self.pc:
            require(self.begin<=address<self.begin+len(self.body),'Report called a non-display helper/transport')
        return super().read(address,count)

    def write(self,address,value,count=4):
        physical=address&0x1fffffff
        require(0x0c000000<=physical<=0x0d000000-count,'Terminal report wrote MMIO/non-RAM')
        return super().write(address,value,count)

    def callee(self):
        require(self.pc in self.targets,'Terminal report called transport or an unexpected helper')
        label=[]
        for at in range(64):
            byte=self.read(self.r[4]+at,1)
            if not byte:break
            label.append(byte)
        else:raise ValueError('Unbounded counter label')
        count=self.r[6];require(0<count<=4,'Terminal row count escaped its four-word limit')
        row=(bytes(label).decode('ascii'),tuple(self.read(self.r[5]+4*i) for i in range(count)))
        self.shown.append(row)
        continuation=self.pr
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd+len(self.shown))&0xffffffff
        # Match the formatter's ordinary RTS return. The report tail-jumps
        # to its final formatter after restoring its own caller PR.
        self.pr=continuation;self.sr^=1;self.pc=continuation

    def check(self):
        self.run_symbol(HELPER)
        require(self.shown==self.expected,'Linked terminal labels/counts/counter selection differ')
        require(self.bytes_at(REGION,self.layout['region_bytes'])==self.region_before,
                'Terminal report mutated retained reader state/counters')
        require(all(TOP-128<=at<TOP for _,at,_,_ in self.writes),'Terminal report wrote outside its caller stack')
        require(not self.iprb_writes and not self.vbr_writes,'Terminal report changed interrupt ownership')
        assert_c_abi(self,self.before)
        return len(self.shown)


def check(path,self_test=False):
    resident=read_elf(path)
    require(resident[4].get('__kui_native_stream_report_resident')==(1,0,0,0xfff1),
            'Resident lacks the exact terminal stream-report marker')
    require('__kui_native_ram_map_resident' not in resident[4],'Stream control must not mark or scan RAM')
    layout=console_layout(resident,path);address,body,section=symbol_bytes(resident,HELPER,2)
    require(section[2]&7==6,'Report helper must be immutable executable code')
    reports=sorted(path.parent.glob(path.name+'.*.ci'));require(reports,'Missing linked compiler call graph')
    _,calls=load(reports)
    incoming={base(source) for source,edges in calls.items() if any(base(target)=='native_stream_report' for target,_ in edges)}
    require(incoming=={'native_reader_report'},'Stream report is not called solely by terminal reader report')
    cases=0
    for seed in range(4):
        values=[((0x13579bdf*(i+1))^(seed*0x2468ace1))&0xffffffff for i in range(11)]
        for mask in (0,6,15):
            for bank in (0,1):ReportCPU(resident,layout,values,seed,mask,bank).check();cases+=1
    rejected=0
    if self_test:
        # Changing an actual compiled MOV #4,R6 must corrupt one printed
        # count; independent distinct words catch shifted field selection.
        for at in range(0,len(body)-1,2):
            if struct.unpack_from('<H',body,at)[0]!=0xe604:continue
            data=bytearray(resident[0]);struct.pack_into('<H',data,section[4]+address-section[3]+at,0xe603)
            changed=(bytes(data),*resident[1:])
            try:ReportCPU(changed,layout,list(range(101,112))).check()
            except (AssertionError,ValueError):rejected+=1
            else:raise ValueError('Broken actual report count instruction was accepted')
        require(rejected>=1,'No compiled counter-count mutation was exercised')
    return dict(schema='kui.native.terminal-stream-report.audit.v1',elf_sha256=hashlib.sha256(resident[0]).hexdigest(),
        actual_linked_report_cases=cases,actual_counter_count_mutants_rejected=rejected,
        actual_sh_layout=layout,rows=[dict(label=label,fields=list(FIELDS[at:at+count])) for label,at,count in ROWS],
        terminal_only_caller=True,reader_state_unchanged=True,stack_only_writes=True,no_transport_or_mmio_calls=True,
        console_performance_measured=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf',type=Path)
    parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    try:print(json.dumps(check(args.elf,self_test=args.self_test),indent=2))
    except (OSError,KeyError,ValueError,AssertionError,struct.error) as error:
        parser.exit(2,'Native stream report audit refused: '+str(error)+'\n')


if __name__=='__main__':main()
