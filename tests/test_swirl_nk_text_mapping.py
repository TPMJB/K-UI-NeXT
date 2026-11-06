#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise Swirl NK's original ROM O32 mapper and direct-RAM page mapping.

Only source hashes and authored scalar metadata are emitted. The process page
table and critical-section callbacks are authored boundaries; no MMU, cache,
interrupts or console module-loading compatibility are simulated.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from armada_ce_rom_audit import ROMInventory
from test_native_standalone_foreground_gin import Gin
from test_vmu_native_sdk_poll_execution import PollSH

SHA='a19080f2e243c04e01a3a473c7a3caa36fb85a5a8fbb2163c8f7a1b2c8ed5ea8'
LOAD=0x8c021378
MAP=0x8c0248bc
STOP=0x8c7f0000
CONTAINER=0x8c700000
E32=0x8c700100
OBJECTS=0x8c700200
TABLE=0x8c600000
BLOCK=0x8c610000
DESTINATION=0x03ce1000
RELOCATED=0x8cfb8000

def sha(data):return hashlib.sha256(data).hexdigest()

class Kernel(Gin):
    def __init__(self,rom,source):
        PollSH.__init__(self,b'',2,1)
        self.gbr=self.mach=self.macl=self.spc=self.ssr=0
        self.vbr=0x8c00f400
        self.sr=0x40008000
        self.store_block(rom.base,rom.data[rom.body_offset:])
        module=rom.modules['maple.dll']
        self.write(module['o32_address'],0x6e00)
        self.write(module['o32_address']+12,source)
        self.write(CONTAINER,module['toc_address'])
        self.write(CONTAINER+4,3,1) # The existing ROM-module container kind.
        self.write(CONTAINER+6,0,2)
        self.write(E32,1,2) # Isolate the existing first text section.
        self.write(E32+32,0xffffffff)
        # An authored, empty destination address block in slot1. Neither the
        # ROM source pointer nor ROMHDR bounds participate in this fixture.
        self.write(0x8c1d1800+160+4,TABLE)
        self.write(TABLE+((DESTINATION>>16)&511)*4,BLOCK)
        self.write(BLOCK+6,(DESTINATION>>16)&511,2)
        self.pc,self.pr=LOAD,STOP
        self.r[4:8]=[CONTAINER,E32,OBJECTS,0]
        self.r[15]=0x8c7ff000
        self.calls=[];self.executed=[];self.reads=[];self.writes=[]

    def read(self,address,count=4):
        if hasattr(self,'reads'):self.reads.append((address,count))
        return super().read(address,count)

    def step(self):
        self.executed.append(self.pc)
        if self.pc in (0x8c02ae98,0x8c02aedc):
            assert self.r[4]==0x8c1d9380
            self.calls.append(dict(boundary='critical_section',entry=hex(self.pc)))
            self.pc=self.pr
            return
        if self.pc==MAP:
            self.calls.append(dict(boundary='original_virtual_copy',arguments=list(self.r[4:8])))
        super().step()

def audit(path):
    path=Path(path);data=path.read_bytes()
    assert len(data)==1828864 and sha(data)==SHA
    rom=ROMInventory(path)
    before=[]
    for source in (0x8c0b1000,RELOCATED):
        cpu=Kernel(rom,source)
        for _ in range(20000):
            if cpu.pc==STOP:break
            cpu.step()
        else:raise AssertionError('NK text mapper exceeded bound')
        assert cpu.r[0]==0
        calls=[c for c in cpu.calls if c['boundary']=='original_virtual_copy']
        assert len(calls)==1 and calls[0]['arguments']==[DESTINATION,source,0x6e00,32],calls
        ptes=[cpu.read(BLOCK+12+i*4) for i in range(1,8)]
        assert [p&0x1ffff000 for p in ptes]==[(source&0x1ffff000)+4096*i for i in range(7)],ptes
        assert len({p&4095 for p in ptes})==1
        assert cpu.read(OBJECTS+8)==DESTINATION
        bounds=(rom.header_address+8,rom.header_address+12)
        assert not any(a<=b<a+n for a,n in cpu.reads for b in bounds)
        assert 0x8c0249ba in cpu.executed and 0x8c0249d2 in cpu.executed
        before.append(dict(source=hex(source),outside_original_rom=source>=rom.header['physlast'],
            status='PASS',original_virtual_copy_arguments=[hex(x) for x in calls[0]['arguments']],
            page_physical_addresses=[hex(p&0x1ffff000) for p in ptes],page_attribute_bits=hex(ptes[0]&4095),
            real_address_unchanged=hex(cpu.read(OBJECTS+8)),
            executed_instructions=cpu.instructions,romhdr_physfirst_last_read=False,
            original_direct_source_path_executed=True))
    nk=rom.modules['nk.exe'];text=nk['sections'][0]
    windows=[]
    for begin,end,meaning in [(0x8c021378,0x8c0216e4,'ROM O32 conversion and section mapper'),
                              (0x8c0248bc,0x8c024c94,'VirtualCopy direct source and page table publication'),
                              (0x8c025600,0x8c02574c,'Destination page block and empty-page admission')]:
        windows.append(dict(begin=hex(begin),end=hex(end),bytes=end-begin,
            sha256=sha(rom.address_read(begin,end-begin)),meaning=meaning))
    return dict(schema='kui.swirl.original-nk-text-map.v1',status='PASS',kernel_sha256=SHA,
        source_nk_text_sha256=text['stored_sha256'],
        original_rom_physlast=hex(rom.header['physlast']),cases=before,windows=windows,
        conclusions=['Original ROM O32 mapping accepts both original and relocated aligned direct-RAM sources in this authored destination-page context.',
            'Raw O32 data_address changes the mapped physical source; real_address and nominal virtual mapping remain unchanged.',
            'The actual replayed ROM branch and direct-source VirtualCopy branch perform no ROMHDR physfirst/physlast read.'],
        limits=['Original source-bound NK scalar instructions execute; no function bytes are embedded or emitted.',
            'The ROM container kind, one-section runtime E32 record, process table, empty page block and critical-section callbacks are authored boundaries.',
            'This is not a full NK startup/module-loader, allocator, cache, TLB, interrupt or hardware execution model.'])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel',type=Path,required=True);p.add_argument('--report',type=Path)
    a=p.parse_args();out=audit(a.kernel);text=json.dumps(out,indent=2)+'\n'
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
