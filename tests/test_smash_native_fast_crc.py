#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Require and execute the actual byte-table CRC in the native menu stage."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_loader_layout import SYM,region
from check_vmu_native_stage_stack import require
from test_native_standalone_foreground_gin import Gin


def table_bytes():
    words=[]
    for value in range(256):
        crc=value
        for _ in range(8):crc=(crc>>1)^(0xedb88320 if crc&1 else 0)
        words.append(crc)
    return struct.pack('<256I',*words)


def linked_byte_table(image,helper_body):
    """Keep same-named local tables from different translation units distinct."""
    expected=table_bytes();tables=set()
    for symbols in image[3]:
        if symbols[1]!=2:continue
        for at in range(symbols[4],symbols[4]+symbols[5],SYM.size):
            _,address,size,info,_,owner=SYM.unpack_from(image[0],at)
            if info&15!=1 or size!=1024 or owner>=len(image[3]):continue
            section=image[3][owner]
            if section[1]!=1 or section[2]&7!=2:continue
            require(section[3]<=address<address+size<=section[3]+section[5],
                'Byte CRC table symbol leaves its allocated read-only section')
            data=region(image[0],section[4]+address-section[3],size,'byte CRC table')
            if data==expected:tables.add(address)
    linked=[address for address in tables if helper_body.count(struct.pack('<I',address))==1]
    require(len(linked)==1,'Actual fast CRC helper must bind one complete byte CRC table')
    return linked[0],len(tables)


def audit(path,helper='_kui_stage_crc32',relay='_kui_native_heap_base_relay',stack_limit=64):
    require(__debug__,'Run actual CRC instruction checks without Python optimization')
    image=read_elf(path)
    entry,body,_=symbol_bytes(image,helper,2)
    expected=table_bytes();table,table_candidates=linked_byte_table(image,body)
    if relay is not None:
        _,relay_body,_=symbol_bytes(image,relay,2)
        if image[4].get('__kui_native_cached_boot_crc'):
            scope,scope_body,_=symbol_bytes(image,'_kui_native_stage_cached_crc32',2)
            leaf,leaf_body,_=symbol_bytes(image,'_kui_retail_stage_crc32_leaf',2)
            require(struct.pack('<I',scope) in relay_body and struct.pack('<I',leaf) in scope_body and
                    struct.pack('<I',entry) in leaf_body,
                    'Whole-image relay does not bind its pure cache scope to the linked fast CRC helper')
        else:
            require(struct.pack('<I',entry) in relay_body,'Whole-image/resident relay does not call the linked fast CRC helper')

    class CRC(Gin):
        def __init__(self):
            super().__init__(b'',b'',b'');self.table_reads=self.input_reads=0
        def read(self,address,count=4):
            if table<=address<table+1024:
                require(count==4 and address%4==0,'CRC table read width/alignment differs');self.table_reads+=1
            if 0x8cc10000<=address<0x8cc14000:
                require(count==1,'CRC input is not consumed byte-exact');self.input_reads+=1
            return super().read(address,count)
    cpu=CRC()
    for segment in image[2]:
        if segment[0]==1:cpu.store_block(segment[2],image[0][segment[1]:segment[1]+segment[4]])
    data=bytes((i*73+19)&255 for i in range(8192));cpu.store_block(0x8cc10000,data)
    cases=steps=stack_bytes=0
    for previous in (0,0x12345678):
        for offset in range(4):
            for size in (0,1,2,3,4,5,7,8,15,16,31,32,255,256,257,4097):
                cpu.r=[(i*0x123457+0x53535353)&0xffffffff for i in range(16)]
                cpu.r[4:7]=[previous,0x8cc10000+offset,size];cpu.r[15]=0x8c00d000
                before=cpu.r[8:].copy();cpu.pc=entry;cpu.pr=0x8c003000
                cpu.writes=[];cpu.instructions=cpu.table_reads=cpu.input_reads=0
                for _ in range(size*24+200):
                    if cpu.pc==0x8c003000:break
                    cpu.step()
                require(cpu.pc==0x8c003000 and cpu.r[0]==zlib.crc32(data[offset:offset+size],previous),
                        'Linked byte CRC differs from independent zlib oracle')
                require(cpu.r[8:]==before and cpu.table_reads==size and cpu.input_reads==size,
                        'CRC changed nonvolatile state, overread input, or used more than one table lookup per byte')
                require(all(0x8c00d000-stack_limit<=address<address+width<=0x8c00d000
                    for _,address,_,width in cpu.writes),'CRC wrote outside its caller frame')
                stack_bytes=max(stack_bytes,max((0x8c00d000-address for _,address,_,_ in cpu.writes),default=0))
                require(not cpu.mmio_reads and not cpu.mmio_writes,'CRC touched hardware')
                cases+=1;steps+=cpu.instructions
    require(cpu.block(0x8cc10000,len(data))==data,'CRC changed its source bytes')
    return dict(schema='kui.smash-native.fast-crc.v1',status='PASS',
        elf_sha256=hashlib.sha256(image[0]).hexdigest(),helper=hex(entry),helper_bytes=len(body),
        helper_sha256=hashlib.sha256(body).hexdigest(),byte_table=hex(table),table_bytes=1024,
        complete_readonly_table_candidates=table_candidates,helper_bound_tables=1,
        table_sha256=hashlib.sha256(expected).hexdigest(),actual_SH_cases=cases,instructions=steps,
        one_table_read_per_byte=True,independent_oracle='zlib.crc32',
        whole_image_relay_bound=relay is not None,observed_private_stack_bytes=stack_bytes,
        hardware_timing_verified=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--report',type=Path);args=parser.parse_args();report=audit(args.elf)
    text=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
