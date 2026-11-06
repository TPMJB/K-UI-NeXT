#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real outer capture/prepare branch with GUI and returned-game PVR fixtures.

The actual14-register capture and prepare-owned VID1 branch execute, including
CRC creation/refusal. The accepted seed then passes actual linked publication
with authored runtime metadata. Owner verification and media I/O are separate
boundaries. No capture, validity or publication helper is substituted. The
optional refusal mode records the regression in a retained old ELF.
"""
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
from test_smash_phase_video import VideoCPU,REGS,STACK,CALLER
from test_smash_menu_loader_runtime import PHASE_VIDEO_REGS,phase_video_seed,FIRMWARE,returning_seed,card_receipt

def sha(data):return hashlib.sha256(data).hexdigest()
class ReturnVideoCPU(VideoCPU):
    def plain(self,op,pc):
        if op&0xf0ff==0x00a3:self.instructions+=1;return
        return super().plain(op,pc)
def publish(elf,build,seed,regs,returned):
    constants={k:int(v,16) for k,v in re.findall(r'#define (KUI_SMASH_MENU_\w+) UINT32_C\((0x[0-9a-f]+)\)',
        (build/'smash_menu_loader_symbols.h').read_text())}
    plans=[]
    for first in (100,400,700):
        words=[0x5349554b,0x31444c4d,1,64,2048,2048,0xface1234,64,first,4,0,4,
            constants['KUI_SMASH_MENU_HANDOFF_CRC32'],constants['KUI_SMASH_MENU_RAW_CRC32'],0,0]
        words[-1]=zlib.crc32(struct.pack('<16I',*words));plans+=words
    collection=[0x5349554b,0x31434d53,1,224,3,7]+plans+[0,0]
    collection[-1]=zlib.crc32(struct.pack('<56I',*collection))
    runtime=struct.pack('<66I',*(collection+FIRMWARE+[constants['KUI_SMASH_MENU_STOP_P2']]))+returning_seed(returned)+card_receipt()
    assert len(runtime)==728 and elf[4]['_smash_menu_prepared'][1]==728
    cpu=ReturnVideoCPU(elf,regs);cpu.store_block(elf[4]['_smash_menu_prepared'][0],runtime)
    cpu.store_block(elf[4]['_smash_menu_prepared_video'][0],seed)
    cpu.store_block(constants['KUI_SMASH_MENU_FIRMWARE_P2'],struct.pack('<9I',*FIRMWARE))
    root=returning_seed(True)
    cpu.store_block(0xac036a5c,struct.pack('<I',4))
    cpu.store_block(0xac036b44,root[96:352]);cpu.store_block(0xac0080f0,root[352:368])
    assert cpu.invoke('_smash_menu_loader_publish')=='returned'
    expected=bytearray(runtime);expected[264:632]=root
    assert cpu.block(constants['KUI_SMASH_MENU_RUNTIME_P2'],728)==expected
    assert cpu.block(constants['KUI_SMASH_MENU_PHASE_VIDEO_P2'],80)==seed
    assert not cpu.pvr_reads and not cpu.pvr_writes and not cpu.vram_writes and list(cpu.pvr.values())==regs
    return {'actual_publication':True,'published_VID1_sha256':sha(seed),'published_runtime_bytes':728,
        'publication_preserves_current_game_PVR':True}
def exercise(elf,build,name,regs,returned,carried=None,want_success=True,seed_output=None):
    cpu=ReturnVideoCPU(elf,regs);symbols=elf[4];display=symbols['_display'][0]
    cpu.store_block(symbols['_smash_outer_return_trace'][0],bytes((returned,)))
    cpu.store_block(symbols['__smash_menu_return_seed'][0],returning_seed(returned))
    if carried is not None:cpu.store_block(symbols['__smash_menu_return_seed'][0]+368,carried)
    assert cpu.invoke('_retail_display_capture',[display])=='returned'
    captured=cpu.block(display,56)
    assert captured==struct.pack('<14I',*regs) and cpu.pvr_reads==list(REGS)
    assert not cpu.pvr_writes and not cpu.vram_writes
    cpu.r=[(0x53530000+i*0x12345)&0xffffffff for i in range(16)]
    cpu.r[15],cpu.pr,cpu.pc,cpu.sr=STACK,CALLER,symbols['_smash_menu_loader_prepare'][0],0x700000f0
    capture_pc=symbols['_kui_smash_phase_video_capture'][0]
    arguments=None;seed=None;refused=None;capture_calls=0;validation_calls=0;accepted=False
    prepared=symbols['_smash_menu_prepared_video'][0]
    canary=bytes([0xa5])*80;cpu.store_block(prepared,canary)
    for _ in range(300000):
        if cpu.pc==capture_pc:
            capture_calls+=1;assert capture_calls==1,'Outer prepare captured VID1 more than once'
            arguments=cpu.r[4:6].copy()
            assert arguments[0]==symbols['_smash_menu_prepared_video'][0]
            assert cpu.block(arguments[1],56)==captured
        if cpu.pc==symbols['_kui_smash_phase_video_valid'][0]:validation_calls+=1
        if cpu.pc==symbols['_kui_sha256_init'][0]:accepted=True;break
        if cpu.pc==symbols['_stopped'][0]:
            refused=(cpu.string(cpu.r[4]),cpu.r[5]);break
        cpu.step()
    else:raise AssertionError('Outer prepare capture did not reach its finite branch boundary')
    seed=cpu.block(prepared,80)
    assert not cpu.pvr_writes and not cpu.vram_writes and cpu.pvr_reads==list(REGS)
    assert list(cpu.pvr.values())==regs and cpu.block(display,56)==captured
    assert accepted==want_success,(name,refused)
    if accepted:
        assert refused is None
        assert cpu.invoke('_kui_smash_phase_video_valid',[prepared])=='returned' and cpu.r[0]==1
        assert seed==(carried if returned and carried is not None else phase_video_seed(regs))
        if seed_output is not None:seed_output.append(seed)
    else:
        expected=('SMASH TIMING RETURN VIDEO REFUSED',3) if returned and carried is not None else \
            ('SMASH TIMING ORIGINAL VIDEO REFUSED',1 if carried is not None else 0)
        assert refused==expected and seed==canary
    publication=publish(elf,build,seed,regs,returned) if accepted else {'actual_publication':False}
    return {'case':name,'returned_outer':returned,'captured_actual14_regs':regs,
        'prepare_capture_arguments':[hex(v) for v in arguments] if arguments else None,
        'actual_live_VID1_capture_calls':capture_calls,'actual_carried_VID1_validation_calls':validation_calls,
        'prepare_capture_success':accepted,'terminal_refusal':list(refused) if refused else None,
        'seed_sha256':sha(seed),'hardware_writes':0,'capture_and_prepare_helpers_substituted':False,**publication}
def audit(path,expect_refusal=False):
    path=Path(path);elf=read_elf(path);before=sha(elf[0]);baseline=PHASE_VIDEO_REGS.copy()
    nonzero=baseline.copy();nonzero[2]=0x200000;nonzero[3]=0x200500
    blank=nonzero.copy();blank[0]&=~1;blank[10]|=0x108
    small=nonzero.copy();small[4]=159|(239<<10)|(1<<20)
    empty=struct.pack('<4I',0x5349554b,0x31444956,1,80)+bytes(64)
    good=phase_video_seed(baseline)
    carried=None if expect_refusal else good
    build=path.parent
    first_prepared=[]
    rows=[exercise(elf,build,'initial GUI KOS framebuffer0',baseline,False,None if expect_refusal else empty),
        exercise(elf,build,'returned outer KOS control',baseline,True,carried),
        exercise(elf,build,'returned game valid nonzero framebuffer',nonzero,True,carried,not expect_refusal,first_prepared),
        exercise(elf,build,'returned game disabled/blank framebuffer',blank,True,carried,not expect_refusal),
        exercise(elf,build,'returned game320x240 framebuffer',small,True,carried,not expect_refusal)]
    if not expect_refusal:
        assert elf[4]['__smash_menu_return_video_seed'][0]==elf[4]['__smash_menu_return_seed'][0]+368
        assert rows[0]['actual_live_VID1_capture_calls']==1
        assert all(not row['actual_live_VID1_capture_calls'] and row['actual_carried_VID1_validation_calls']
            for row in rows[1:])
        bad=bytearray(good);bad[72]^=1
        rows += [exercise(elf,build,'returned missing carried VID1',nonzero,True,empty,False),
            exercise(elf,build,'returned bad CRC carried VID1',nonzero,True,bytes(bad),False),
            exercise(elf,build,'initial GUI refuses occupied carried VID1',baseline,False,good,False)]
        # A second return carries the exact already prepared bytes, rather
        # than recapturing the next game's framebuffer or synthesizing modes.
        assert len(first_prepared)==1 and first_prepared[0]==good
        second=exercise(elf,build,'second return preserves first prepared VID1',blank,True,first_prepared[0])
        assert second['seed_sha256']==rows[2]['seed_sha256']==sha(good)
        rows.append(second)
    assert before==sha(path.read_bytes()),'Outer ELF changed during regression proof'
    return {'schema':'kui.smash.outer-return-video.v1',
        'status':'REGRESSION_REPRODUCED' if expect_refusal else 'PASS',
        'outer_stage_elf_sha256':before,'cases':rows,
        'source_sha256':{p:sha((ROOT/p).read_bytes()) for p in (
            'src/loader/smash_menu_loader_stage.inc','src/core/smash_phase_video.c',
            'src/loader/retail_display.c','tests/test_smash_outer_return_video.py')},
        'physical_display_timing_measured':False,
        'limits':'PVR values and prepared runtime metadata are authored. Real capture, prepare VID1 branch and publication/copy/readback execute. Original owner verification and media I/O are separate boundaries; physical display timing is not modeled.'}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--expect-return-refusal',action='store_true');parser.add_argument('--report',type=Path)
    args=parser.parse_args();result=audit(args.elf,args.expect_return_refusal);output=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(output)
    print(output,end='')
if __name__=='__main__':main()
