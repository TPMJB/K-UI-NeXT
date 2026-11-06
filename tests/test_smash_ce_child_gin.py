#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the two selected CE children to their original GIN/IP/CRT contract.

Private original executable bytes remain external. Original scalar SH code
executes; media, MMU/cache timing, native SDK and CE execution are not modeled.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tests'), str(ROOT/'tools')]
from test_native_standalone_foreground_gin import Gin, GIN_STAGE, LOAD, TARGET
from test_smash_menu_handoff_gin import (original_call, ARGUMENT_PARSER,
    FOLDER_SETTER, MODE_SETTER, MENU_CONTEXT)
from test_smash_menu_loader_runtime import RuntimeCPU

SAMPLE_SHA='82bb1160814548f4656b273353fd6168a691fb5f215b77377860ff9fd2156c8a'
MENU_SHA='3a311b189a4870ccbefc4fcd306e2d38e5ae810ae5edd225af6b9aa6d5df5f10'
GIN_SHA='fa12196993a7c757a78565963e257cd33443f80b3f226729fdc1cf7da51ed5b3'

def sha(data): return hashlib.sha256(data).hexdigest()

class SampleCPU(RuntimeCPU):
    def __init__(self):
        super().__init__(bytes(2048));self.opaque_crt_reads=[]
    def read(self,address,count=4):
        # CRT's first warm-up byte comes from an opaque original word before
        # data relocation. Its result is dead in the scalar span below.
        # Preserve this explicit external-bus boundary rather than treating
        # that non-RAM address as modeled main RAM.
        if getattr(self,'executing',0)==0x8c013306 and count==1:
            self.opaque_crt_reads.append(address);return 0xa7
        return super().read(address,count)

def audit(gin,menu,vc2,swirl):
    assert sha(gin)==GIN_SHA and sha(menu)==MENU_SHA
    assert vc2==swirl and len(vc2)==204576 and sha(vc2)==SAMPLE_SHA
    assert zlib.crc32(vc2)==0x655ed733
    parser=Gin(gin,menu,b'');parser.store_block(LOAD,menu)
    rows=[]
    for profile,folder in [(1,'VIRTCOP2'),(2,'SWIRL   ')]:
        command=('\\'+folder.strip()+'\\SAMPLE.BIN').encode()
        at=menu.index(command+b'\0');fo=menu.index(folder.encode()+b'\0',0x17f28)
        original_call(parser,ARGUMENT_PARSER,[MENU_CONTEXT,LOAD+at])
        original_call(parser,FOLDER_SETTER,[LOAD+fo]);original_call(parser,MODE_SETTER,[4])
        argv=parser.block(MENU_CONTEXT+256,256);ip=parser.block(0x8c0080f0,16)
        assert argv==(command+b'\0').ljust(256,b'\0')
        assert ip[:14]==folder.encode().ljust(12,b' ')+b'\x01\x04'
        cpu=Gin(gin,vc2,b'')
        retained=bytes((i*13+0x65)&255 for i in range(0x8000))
        cpu.store_block(0x8c004000,retained)
        cpu.store_block(GIN_STAGE+256,argv);cpu.store_block(0x8c0080f0,ip)
        for _ in range(1000000):
            if cpu.pc==TARGET: break
            cpu.step()
        else: raise AssertionError('Original SAMPLE GIN copy exceeded bound')
        assert cpu.source==0x8cf90000 and cpu.sectors==100
        assert cpu.block(LOAD,len(vc2))==vc2
        # The folder bytes are deliberately inside the retained low span.
        expected=bytearray(retained);expected[0x40f0:0x4100]=ip
        assert cpu.block(0x8c004000,len(expected))==expected
        assert cpu.r[15]==cpu.vbr==0x8c00f400 and cpu.sr==0x700000f0
        assert cpu.macl==40 and cpu.pr==cpu.mach==0 and cpu.gbr==0x8c000000
        # The authored cold stage supplies this same scalar restart frame.
        # Stop before the first SDK initializer: CRT code/data copies alone.
        cold=SampleCPU();cold.store_block(LOAD,vc2)
        cold.store_block(0x8c004000,expected)
        cold.r=list(cpu.r);cold.pc=TARGET
        cold.sr,cold.pr,cold.macl,cold.mach,cold.gbr,cold.vbr=cpu.sr,cpu.pr,cpu.macl,cpu.mach,cpu.gbr,cpu.vbr
        for _ in range(4000000):
            if cold.pc==0x8c013328: break
            cold.step()
        else: raise AssertionError('Original SAMPLE scalar CRT exceeded bound')
        assert cold.block(0x8c004000,len(expected))==expected
        assert cold.block(0x8c00c000,0x3400)==struct.pack('<I',0x41474553)*(0x3400//4)
        assert cold.cache_writes==[0x909|0x800]
        rows.append(dict(profile=profile,folder=folder.strip(),original_command=command.decode(),
            gin_source=hex(cpu.source),gin_sectors=cpu.sectors,gin_instructions=cpu.instructions,
            original_sample_exact=True,original_ip_exact=True,
            crt_instructions=cold.instructions,crt_stop='0x8c013328',
            retained_low_begin='0x8c004000',retained_low_end='0x8c00c000',
            retained_low_unchanged=True,original_crt_fill_begin='0x8c00c000',original_crt_fill_end='0x8c00f400'))
    return dict(schema='kui.smash-ce.original-child-gin.v1',status='PASS',cases=rows,
        source_sha256={'GINXFER.BIN':GIN_SHA,'1ST_READ.BIN':MENU_SHA,'SAMPLE.BIN':SAMPLE_SHA},
        limits=['Scalar original GIN/parser/setters/SAMPLE CRT only; original files remain external.',
            'Stops before SDK, MAINANA and CE; one original opaque CRT warm-up byte read is an explicit external-bus boundary.',
            'No console, SD, interrupt, MMU or cache timing claim.',
            'The existing scalar GIN model does not model physical register bank switching.'])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--originals',required=True,type=Path);p.add_argument('--swirl-originals',required=True,type=Path)
    p.add_argument('--report',type=Path);a=p.parse_args()
    r=audit((a.originals/'GINXFER.BIN').read_bytes(),(a.originals/'1ST_READ.BIN').read_bytes(),
        (a.originals/'VIRTCOP2/SAMPLE.BIN').read_bytes(),(a.swirl_originals/'SWIRL/SAMPLE.BIN').read_bytes())
    out=json.dumps(r,indent=2)+'\n'
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(out)
    print(out,end='')
if __name__=='__main__':main()
