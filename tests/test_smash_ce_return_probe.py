#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual terminal CE E0 entry and read-only witness path."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_runtime import RuntimeCPU,returning_seed

def crc(b,previous=0):return zlib.crc32(b,previous)&0xffffffff
class ProbeCPU(RuntimeCPU):
    def read(self,address,count=4):
        if address in (0xff000010,0xff000008):
            self.mmio_reads.append(address);return 1 if address==0xff000010 else 0x8c123000
        return super().read(address,count)

def audit(elf,helper,profile,resident=None):
    data,_,segments,_,symbols=read_elf(elf);blob=helper.read_bytes()
    assert profile in (1,2) and 16<=len(blob)<=0xa200
    entry=symbols['_kui_smash_ce_return_probe_entry'][0]
    assert entry==symbols['__smash_ce_return_begin'][0]==0xac008300
    assert symbols['__smash_ce_return_bss_end'][0]<=0xac00b800
    assert (symbols['__smash_ce_return_stack_bottom'][0],symbols['__smash_ce_return_stack_top'][0])==(0xac00b800,0xac00c000)
    runtime,n,_,_=symbols['_kui_smash_ce_return_probe_runtime'];assert n==396
    begin,end=entry,symbols['__smash_ce_return_binary_end'][0]
    cases=[]
    for altered in (False,True):
        cpu=ProbeCPU(bytes(2048))
        low=read_elf(resident) if resident else None
        if low:
            for p in low[2]:
                if p[0]==1:cpu.store_block(p[2],low[0][p[1]:p[1]+p[4]]+bytes(p[5]-p[4]))
        for p in segments:
            if p[0]==1:cpu.store_block(p[2],data[p[1]:p[1]+p[4]]+bytes(p[5]-p[4]))
        code=bytearray(cpu.block(begin,end-begin));code[runtime-begin:runtime-begin+n]=bytes(n)
        report,trace=(low[4]['_kui_retail_menu_return'][0]+0x20000000,
                      low[4]['_kui_native_ocram_trace'][0]+0x20000000) if low else (0xac005000,0xac007b00)
        record=bytearray(returning_seed()+struct.pack('<7I',0,0x12345678,len(blob),crc(blob),report,trace,profile))
        struct.pack_into('<I',record,368,crc(record,crc(code)));cpu.store_block(runtime,record)
        actual=bytearray(blob)
        if altered:actual[20]^=1
        cpu.store_block(0xacfc4000,actual)
        thread=0x8c1d1894 if profile==2 else 0x8c1d5894
        cpu.store_block(thread,struct.pack('<I',0x8c310000));cpu.store_block(thread-4,struct.pack('<I',0x8c320000))
        caller=0x01d2190e if profile==2 else 0x01d21902
        start=low[4]['_kui_retail_menu_hook'][0] if low else entry
        cpu.r[4],cpu.pr,cpu.sr,cpu.r[15],cpu.pc=1,caller,0x40000001,0x06123450,start
        initial_sp,initial_sr=cpu.r[15],cpu.sr
        main=symbols['_kui_smash_ce_return_probe_main'][0];seen=False
        for _ in range(3000000):
            if cpu.pc==main and not seen:
                assert cpu.r[4:8]==[1,caller,initial_sr,initial_sp]
                assert cpu.r[15]==0xac00bff0 and cpu.sr==initial_sr|0xf0
                seen=True
            if cpu.pc==report:break
            cpu.step()
        else:raise AssertionError('Terminal CE witness did not reach original renderer')
        assert seen and cpu.r[4:7]==[1,caller,initial_sp]
        witness=struct.unpack('<24I',cpu.block(symbols['_kui_smash_ce_return_probe_witness'][0],96))
        assert witness[:3]==(0x31524353,1,1)
        assert witness[3]==(0x2008 if altered else 0x2000)
        assert witness[4:8]==(1,caller,initial_sr,initial_sp)
        assert witness[8:12]==(initial_sr|0xf0,1,0x8c123000,0x909)
        assert witness[12:14]==((0,0) if altered else (0x8c310000,0x8c320000))
        assert witness[14:17]==(0xffffffff,)*3 and witness[21]==0
        assert not cpu.cache_writes and not cpu.raw_calls and not cpu.mmio_writes
        assert cpu.read(trace+14*4)==witness[3] and cpu.read(trace+15*4)==0x53434552
        cases.append(dict(profile=profile,helper_corruption=altered,refusal=hex(witness[3]),
            instructions=cpu.instructions,original_command_caller_sr_sp_exact=True,
            private_stack='0xac00b800..0xac00c000',terminal_renderer_reached=True,
            actual_resident_e0_route=bool(low),no_raw_storage_calls=True,
            no_mmu_ccr_or_hardware_writes=True,no_retirement_or_cold_entry=True))
    return dict(schema='kui.smash-ce.terminal-return.v1',status='PASS',elf_sha256=hashlib.sha256(data).hexdigest(),
        helper_sha256=hashlib.sha256(blob).hexdigest(),cases=cases,
        limits=['Actual linked low entry/C/CRC execute; the original terminal renderer is a stop boundary.',
            'Read-only hardware/KData are authored scalar fixtures; no CE scheduler/cache/console timing is certified.',
            'CE return remains terminal UNAVAILABLE; no retirement, storage or outer-menu cold restart is implemented.'])
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--elf',required=True,type=Path)
    p.add_argument('--helper',required=True,type=Path);p.add_argument('--profile',required=True,type=int);p.add_argument('--report',type=Path)
    p.add_argument('--resident',type=Path,help='Bind and execute the actual resident E0 route too')
    a=p.parse_args();r=audit(a.elf,a.helper,a.profile,a.resident);out=json.dumps(r,indent=2)+'\n'
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(out)
    print(out,end='')
if __name__=='__main__':main()
