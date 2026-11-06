#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual outer handoff's original-GIN continuation capture.

The SAMPLE input is authored data with the required CRC, not original game
code. Bounded pure CRC leaf calls use independent zlib oracles. The actual
cache-scope assembly, handoff C, typed seed checks/capture and seed CRC run as
linked SH. Raw media is a four-block fixture. No game compatibility is implied.
"""
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
from test_smash_menu_loader_runtime import (prepare,RuntimeCPU,RAW,TARGET,GIN,IP,
    FIRMWARE_ADDRESSES,FIRMWARE,make_capsule,returning_seed,crc)

BYTES,OWNER_CRC,SOURCE,FRAME=204576,0x655ed733,0x8cf90000,0x8c00f3ac
GIN_OFFSET,GIN_BYTES=0x200,160
ORIGINAL_SHA='82bb1160814548f4656b273353fd6168a691fb5f215b77377860ff9fd2156c8a'

def authored_source():
    data=bytearray((i*17+9)&255 for i in range(BYTES));initial=crc(data)
    columns=[]
    for bit in range(32):
        changed=bytearray(data);changed[-4+bit//8]^=1<<(bit%8)
        columns.append(crc(changed)^initial)
    delta=OWNER_CRC^initial
    rows=[sum(((columns[c]>>r)&1)<<c for c in range(32))|(((delta>>r)&1)<<32) for r in range(32)]
    for column in range(32):
        found=next(r for r in range(column,32) if rows[r]>>column&1)
        rows[column],rows[found]=rows[found],rows[column]
        for r in range(32):
            if r!=column and rows[r]>>column&1:rows[r]^=rows[column]
    mask=sum(((row>>32)&1)<<((row&0xffffffff).bit_length()-1) for row in rows)
    for bit in range(32):
        if mask>>bit&1:data[-4+bit//8]^=1<<(bit%8)
    assert crc(data)==OWNER_CRC and hashlib.sha256(data).hexdigest()!=ORIGINAL_SHA
    return bytes(data)

class Handoff(RuntimeCPU):
    def step(self):
        pc=self.pc&~0x20000000
        if pc==self.crc_leaf:
            previous,address,count=self.r[4:7]
            assert count in (BYTES,2048),'Unexpected pure CRC oracle extent'
            assert address&0x1fffffff==TARGET&0x1fffffff
            data=self.block(address,count)
            value=zlib.crc32(data,previous)&0xffffffff
            self.events.append(('source_crc' if count==BYTES else 'capsule_crc',value))
            self.r[0],self.pc=value,self.pr
            return
        if pc==self.capable_entry:self.events.append(('capable',None))
        if pc==self.capture_entry:
            self.events.append(('capture',None))
            if self.variant=='capture_fault':self.write(self.r[4]+20,1)
        super().step()

def prepare_case(image,source,profile,pattern,ccr,variant):
    cpu,_,_,_,_,_=prepare(image[0],image[2],image[4],'shining_force')
    cpu.__class__=Handoff
    cpu.events=[];cpu.variant=variant
    cpu.crc_leaf=image[4]['_kui_cold_load_crc32_leaf'][0]&~0x20000000
    cpu.capable_entry=image[4]['_kui_smash_ce_gin_capable'][0]&~0x20000000
    cpu.capture_entry=image[4]['_kui_smash_ce_gin_capture'][0]&~0x20000000
    expected=bytearray(make_capsule(False,profile))
    expected[GIN_OFFSET:GIN_OFFSET+GIN_BYTES]=struct.pack('<6I',0x5349554b,0x314e4947,1,160,profile,0)+bytes(136)
    if variant=='bad_capability':expected[GIN_OFFSET]^=1
    if variant=='wrong_profile':struct.pack_into('<I',expected,GIN_OFFSET+16,3-profile)
    if variant=='nonblank_seed':struct.pack_into('<I',expected,GIN_OFFSET+24,1)
    actual=bytearray(expected)
    if variant=='bad_capsule_crc':actual[1700]^=1
    cpu.capsule=bytes(actual)
    runtime=image[4]['_kui_smash_menu_loader_runtime'][0]
    wire=bytearray(cpu.block(runtime,632));at=24+64*profile
    plan=bytearray(wire[at:at+64]);struct.pack_into('<2I',plan,24,crc(expected),64)
    struct.pack_into('<I',plan,60,0);struct.pack_into('<I',plan,60,crc(plan));wire[at:at+64]=plan
    struct.pack_into('<I',wire,220,0);struct.pack_into('<I',wire,220,crc(wire[:224]));cpu.store_block(runtime,wire)
    first,second=(400,500) if profile==1 else (600,700)
    cpu.raw_map={first:0,first+1:1,second:2,second+1:3}
    cpu.store_block(TARGET,source);cpu.store_block(SOURCE,source)
    words=[0x47494e53,1,0x3ac59e76,8192,SOURCE,100,4,1]
    cpu.store_block(GIN,struct.pack('<8I',*words))
    folder='VIRTCOP2' if profile==1 else 'SWIRL'
    argv=('\\'+folder+'\\SAMPLE.BIN').encode().ljust(256,b'\0')
    ip=folder.encode().ljust(12,b' ')+b'\x01\x04\xa7\xb8'
    cpu.store_block(GIN+256,argv);cpu.store_block(IP,ip)
    cpu.r[:8]=[(pattern^(0x10203040+i*0x1020304))&0xffffffff for i in range(8)]
    cpu.original_ccr=ccr
    original_frame=[cpu.macl,cpu.mach,cpu.gbr,cpu.vbr,cpu.sr,cpu.pr]+list(reversed(cpu.r[:15]))
    return cpu,bytes(expected),original_frame,ip

def exercise(image,source,profile,pattern,ccr,variant):
    cpu,expected,frame,ip=prepare_case(image,source,profile,pattern,ccr,variant)
    diagnostic=image[4]['_kui_smash_menu_loader_diagnostic'][0]
    reason=0
    for _ in range(14000000):
        if cpu.pc in (RAW,RAW-0x20000000):cpu.raw_read()
        if cpu.pc==TARGET:break
        if cpu.pc==0xac007000:
            reason=cpu.read(diagnostic+4);break
        cpu.step()
    else:raise AssertionError('Original-GIN handoff exceeded instruction bound')
    event_names=[x[0] for x in cpu.events]
    assert event_names[:2]==['source_crc','capsule_crc']
    assert cpu.events[0][1]==OWNER_CRC
    assert cpu.block(SOURCE,BYTES)==source,'Original staged SAMPLE was overwritten'
    expected_reason={'valid':0,'bad_capability':18,'wrong_profile':18,
        'nonblank_seed':18,'bad_capsule_crc':11,'capture_fault':19}[variant]
    assert reason==expected_reason
    if not reason:
        assert event_names==['source_crc','capsule_crc','capable','capture']
        assert cpu.events[1][1]==crc(expected)
        seed=cpu.block(TARGET+GIN_OFFSET,GIN_BYTES);words=struct.unpack('<40I',seed)
        assert words[:6]==(0x5349554b,0x314e4947,1,160,profile,ccr)
        assert list(words[6:27])==frame and not any(words[28:])
        normalized=bytearray(seed);normalized[108:112]=bytes(4)
        assert words[27]==crc(normalized)
        assert cpu.block(FRAME,84)==struct.pack('<21I',*frame)
        actual=bytearray(cpu.block(TARGET,2048))
        actual[64:496]=expected[64:496];actual[GIN_OFFSET:GIN_OFFSET+GIN_BYTES]=expected[GIN_OFFSET:GIN_OFFSET+GIN_BYTES]
        assert actual==expected,'Unexpected capsule bytes changed'
        assert cpu.block(TARGET+96,16)==ip
        assert cpu.block(TARGET+128,368)==returning_seed()
        assert [cpu.read(a) for a in FIRMWARE_ADDRESSES]==FIRMWARE
    else:
        assert cpu.pc!=TARGET
        assert [cpu.read(a) for a in FIRMWARE_ADDRESSES]==[0xdeadbeef]*9
        if variant=='bad_capsule_crc':assert event_names==['source_crc','capsule_crc']
        elif variant=='capture_fault':assert event_names==['source_crc','capsule_crc','capable','capture']
        else:assert event_names==['source_crc','capsule_crc','capable']
    return dict(profile=profile,variant=variant,pattern=hex(pattern),ccr=hex(ccr),reason=reason,
        actual_SH_instructions=cpu.instructions,CRC_oracles=cpu.events,
        original_staged_SAMPLE_unchanged=True,raw_reads=len(cpu.raw_calls))

def audit(elf):
    image=read_elf(elf);source=authored_source()
    rows=[]
    for profile in (1,2):
        for pattern,ccr in ((0,0x109),(0xdeadbeef,0x909)):
            rows.append(exercise(image,source,profile,pattern,ccr,'valid'))
        for variant in ('bad_capability','wrong_profile','nonblank_seed','bad_capsule_crc','capture_fault'):
            rows.append(exercise(image,source,profile,0xa5a5a5a5,0x109,variant))
    return dict(schema='kui.smash-ce.original-gin-handoff.v1',status='PASS',
        elf_sha256=hashlib.sha256(image[0]).hexdigest(),actual_SH_cases=len(rows),cases=rows,
        authored_source_bytes=len(source),authored_source_crc32=hex(crc(source)),
        authored_source_sha256=hashlib.sha256(source).hexdigest(),original_SAMPLE_used=False,
        source_full_SHA_owner_tested_in_separate_adoption_check=True,
        limits=['The authored CRC-collision fixture is not original or executable SAMPLE code.',
            'Whole-source and whole-capsule pure CRC leaves use independent zlib oracles; seed CRC and handoff/capture logic execute as linked SH.',
            'Media is a bounded four-block fixture; OCBP and CCR instruction ordering is modeled without timing.',
            'No original SDK, MAINANA, CE gameplay or console compatibility claim.'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--report',type=Path);a=p.parse_args();r=audit(a.elf);rendered=json.dumps(r,indent=2)+'\n'
    if a.report:a.report.write_text(rendered)
    print(rendered,end='')
