#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute outer diagnostic video publication and its actual readback guard."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_phase_clock_linked import ClockCPU
from test_smash_menu_loader_runtime import FIRMWARE,returning_seed,phase_video_seed,session_flow,card_receipt

STACK,CALLER=0x8c00d000,0x8c003000


class PublishCPU(ClockCPU):
    def __init__(self,image,video,fault=False):
        self.video=video;self.fault=fault
        self.injected=False;self.stop=None;self.purges=0
        self.captures=[]
        super().__init__(image)
        self.symbols=image[4]

    def plain(self,op,pc):
        if op&0xf0ff==0x00a3:
            self.purges+=1;self.instructions+=1;return
        return super().plain(op,pc)

    def write(self,address,value,count=4):
        if self.fault and not self.injected and address==self.video+16 and value&255==5:
            value^=1;self.injected=True
        return super().write(address,value,count)

    def invoke(self,name,until=None):
        self.r=[0x53530000+i*0x12345 for i in range(16)]
        self.r[15],self.pr,self.pc,self.sr=STACK,CALLER,self.symbols[name][0],0x700000f0
        before=(self.r[8:15].copy(),self.gbr,self.vbr,self.fr.copy(),self.fpscr)
        self.instructions=0
        for _ in range(2000000):
            if until is not None and self.pc==until:return 'video_prepared'
            if self.pc==self.symbols['_kui_smash_phase_video_capture'][0]:
                self.captures.append(tuple(self.r[4:6]))
            if self.pc==CALLER:
                assert (self.r[8:15],self.gbr,self.vbr,self.fr,self.fpscr)==before
                assert self.r[15]==STACK and self.sr&~0x301==0x700000f0&~0x301
                return 'returned'
            if self.pc==self.symbols['_stopped'][0]:
                text=bytearray()
                for n in range(80):
                    b=self.read(self.r[4]+n,1)
                    if not b:break
                    text.append(b)
                self.stop=(text.decode(),self.r[5]);return 'refused'
            self.step()
        raise ValueError('Outer video publication exceeded its finite instruction bound')


def audit(build):
    build=Path(build);stage=read_elf(build/'stage.elf');handoff=read_elf(build/'handoff.elf')
    s,h=stage[4],handoff[4]
    video,n,kind,owner=h['_kui_smash_menu_loader_phase_video']
    assert n==80 and kind==1 and handoff[3][owner][1]==8
    constants={k:int(v,16) for k,v in re.findall(r'#define (KUI_SMASH_MENU_\w+) UINT32_C\((0x[0-9a-f]+)\)',
                                               (build/'smash_menu_loader_symbols.h').read_text())}
    assert constants['KUI_SMASH_MENU_PHASE_VIDEO_P2']==video
    raw=(build/'smash-menu-raw.bin').read_bytes();code=(build/'handoff.bin').read_bytes()
    plans=[]
    for first,second in ((100,200),(400,500),(600,700)):
        seed=[0x5349554b,0x31444c4d,1,64,2048,2048,0xface1234,64,first,2,second,4,
              zlib.crc32(code),zlib.crc32(raw),0,0]
        seed[15]=zlib.crc32(struct.pack('<16I',*seed));plans+=seed
    collection=[0x5349554b,0x31434d53,1,224,3,7]+plans+[0,0]
    collection[-1]=zlib.crc32(struct.pack('<56I',*collection))
    runtime=struct.pack('<66I',*(collection+FIRMWARE+[constants['KUI_SMASH_MENU_STOP_P2']]))+returning_seed()+\
        (card_receipt() if session_flow(h) else b'')
    runtime_bytes=632+96*session_flow(h)
    assert len(runtime)==runtime_bytes and s['_smash_menu_prepared'][1]==runtime_bytes
    source=phase_video_seed();cases=[]
    for fault in (False,True):
        cpu=PublishCPU(stage,video,fault)
        cpu.store_block(s['_smash_menu_prepared'][0],runtime)
        cpu.store_block(s['_smash_menu_prepared_video'][0],source)
        cpu.store_block(constants['KUI_SMASH_MENU_FIRMWARE_P2'],struct.pack('<9I',*FIRMWARE))
        root=returning_seed(True)
        cpu.store_block(0xac036a5c,struct.pack('<I',4))
        cpu.store_block(0xac036b44,root[96:352]);cpu.store_block(0xac0080f0,root[352:368])
        outcome=cpu.invoke('_smash_menu_loader_publish')
        if fault:
            assert outcome=='refused' and cpu.injected
            assert cpu.stop==('SMASH TIMING VIDEO COPY CHANGED',video)
        else:
            assert outcome=='returned' and cpu.block(video,80)==source
            expected=bytearray(runtime)
            if session_flow(h):expected[264:632]=root
            assert cpu.block(constants['KUI_SMASH_MENU_RUNTIME_P2'],runtime_bytes)==expected
            assert cpu.block(0xac008100,512)==raw
            copied=bytearray(cpu.block(0xac008300,len(code)))
            at=constants['KUI_SMASH_MENU_RUNTIME_P2']-0xac008300
            copied[at:at+runtime_bytes]=bytes(runtime_bytes)
            assert copied==code
        assert not cpu.hardware_reads
        cases.append(dict(case='readback_corruption' if fault else 'exact_publication',
                          outcome=outcome,instructions=cpu.instructions,purged_lines=cpu.purges,
                          source_video_crc32=source[72:76].hex(),working_runtime_bytes=runtime_bytes))
    slot=s['__smash_menu_return_video_seed'][0]
    assert slot==s['__smash_menu_return_seed'][0]+368
    blank=struct.pack('<4I',0x5349554b,0x31444956,1,80)+bytes(64)
    assert PublishCPU(stage,video).block(slot,80)==blank
    game=bytearray(source[16:72]);struct.pack_into('<I',game,8,0x200000)
    prepare_cases=[('initial_KOS',False,source[16:72],blank,True),
        ('initial_game_shape_refused',False,bytes(game),blank,False),
        ('initial_occupied_VID_refused',False,source[16:72],source,False),
        ('returned_blank_VID_refused',True,bytes(game),blank,False)]
    corrupt=bytearray(source);corrupt[20]^=1
    prepare_cases.append(('returned_corrupt_VID_refused',True,bytes(game),bytes(corrupt),False))
    # Two consecutive returned prepares must preserve the original record,
    # despite distinct inherited game framebuffer captures on each cycle.
    carried=source
    for cycle in (1,2):
        current=bytearray(game);struct.pack_into('<I',current,8,0x200000+cycle*0x100000)
        prepare_cases.append(('returned_cycle_'+str(cycle),True,bytes(current),carried,True))
    prepared=[]
    for label,returned,regs,incoming,valid in prepare_cases:
        if label.startswith('returned_cycle_'):incoming=carried
        capture=PublishCPU(stage,video)
        capture.store_block(s['_display'][0],regs)
        capture.store_block(s['__smash_menu_return_seed'][0],returning_seed(returned))
        capture.store_block(slot,incoming)
        outcome=capture.invoke('_smash_menu_loader_prepare',until=s['_kui_sha256_init'][0])
        assert outcome==('video_prepared' if valid else 'refused')
        assert not capture.hardware_reads
        if valid:
            actual=capture.block(s['_smash_menu_prepared_video'][0],80)
            assert actual==source
            if label=='initial_KOS' or label.startswith('returned_cycle_'):carried=actual
        if returned:assert not capture.captures,'Returned prepare must not recapture game PVR state'
        elif label!='initial_occupied_VID_refused':
            assert capture.captures==[(s['_smash_menu_prepared_video'][0],s['_display'][0])]
        prepared.append(dict(case=label,outcome=outcome,capture_calls=len(capture.captures),
                             original_VID_exact=valid,instructions=capture.instructions))
    return dict(schema='kui.smash-phase.video-publication.v1',status='PASS',
                stage_elf_sha256=hashlib.sha256(stage[0]).hexdigest(),
                handoff_elf_sha256=hashlib.sha256(handoff[0]).hexdigest(),
                low_video_address=hex(video),video_bytes=80,cases=cases,prepare_cases=prepared,
                capture_bound_to_outer_prepare=True,capture_arguments_exact=True,hardware_writes=0,
                limits=['Actual linked publish/copy/readback instructions execute; prepared launch metadata and the original K-UI video capture are authored input fixtures.',
                        'OCBP is counted without cache effects. The low child hydration and real renderer/import have separate linked proofs; console visibility remains unverified.'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True)
    p.add_argument('--report',type=Path);a=p.parse_args();text=json.dumps(audit(a.build),indent=2)+'\n'
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(text)
    print(text,end='')


if __name__=='__main__':main()
