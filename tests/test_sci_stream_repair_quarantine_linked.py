#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute whole linked SCI take routines against authored receive-state RAM.

The supplied old/new resident ELFs provide every executed SH instruction,
including byte reversal, CRC16, missing-byte reconstruction and acceptance.
Only post-reception RAM is authored: no SCI, DMA, MMU, timing, game or VMU is
executed. Independent CRC16 builds fixture tails; it replaces no SH function.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from test_smash_menu_loader_runtime import RuntimeCPU
from test_smash_ce_gin_adoption import load_image

STOP,STACK,RESULT=0x8cff1234,0x8cff0000,0x8cfe0000
LBA,LOST,HEADER=700,146,176
MODEL_FILES=('tests/test_smash_menu_loader_runtime.py','tests/test_smash_menu_loader_entry.py',
 'tests/test_native_standalone_foreground_gin.py','tests/test_vmu_native_sdk_poll_execution.py',
 'tests/test_vmu_native_heap_execution.py','tests/test_smash_ce_gin_adoption.py')
SOURCE_FILES=('src/loader/sci_stream.c','src/loader/sci_stream.h',
 'src/loader/retail_async.h','tests/test_sci_stream_repair_quarantine.c')

def require(ok,message):
    if not ok:raise AssertionError(message)
def sha(data):return hashlib.sha256(data).hexdigest()
def physical(at):return at&0x1fffffff
def crc16(data):
    value=0
    for byte in data:
        value^=byte<<8
        for _ in range(8):value=((value<<1)^(0x1021 if value&0x8000 else 0))&65535
    return value

def reverse(byte):
    return int(f'{byte:08b}'[::-1],2)
def original():
    data=bytearray((i*13+(i>>8)*3+0x5a)&255 for i in range(512))
    data[HEADER:HEADER+16]=bytes([0,255,255,255,255,255,255,255,255,255,255,0,0,2,0,1])
    return data

class CPU(RuntimeCPU):
    def __init__(self,image):
        super().__init__(b'')
        load_image(self,image)
        self.path=[]
    def read(self,at,count=4):
        require(0x0c000000<=physical(at) and physical(at)+count<=0x0d000000,
                'SCI take attempted non-RAM read: '+hex(at))
        return super().read(at,count)
    def write(self,at,value,count=4):
        require(0x0c000000<=physical(at) and physical(at)+count<=0x0d000000,
                'SCI take attempted MMIO/VRAM write: '+hex(at))
        super().write(at,value,count)
    def step(self):
        self.path.append(self.pc)
        super().step()

def layout(image):
    sy=image[4]
    require(sy.get('__native_dma_region_bytes')==(1600,0,0,0xfff1),
            'Requires current native1600B foreground reader ABI')
    region=sy['_standalone_reader_region'][0]
    # Exact current native ABI: region.shared=0x4ac, shared.stream=296.
    # Bind these RAM fixture offsets to the actual take prologue's first
    # ready[0] literal, rather than trusting a detached host-width layout.
    stream=region+0x4ac+296
    cpu=CPU(image);entry=sy['_kui_sci_stream_take'][0]
    op=cpu.read(entry+16,2)
    require(op&0xff00==0xd100,'Take prologue no longer loads ready[0] intoR1')
    literal=((entry+20)&~3)+(op&255)*4
    require(cpu.read(literal)==stream+60,'Actual take ready pointer differs from fixture ABI')
    return dict(region=region,stream=stream,area0=region,area1=region+544,
                ready_literal=literal,ready_pointer=stream+60,entry=entry)

def exercise(image,case,old=False):
    loc=layout(image);cpu=CPU(image)
    region,stream,area=loc['region'],loc['stream'],loc['area0']
    cpu.store_block(region,bytes(1600))
    clean=original();payload=bytearray(clean)
    lost=LOST if case.startswith('lost_') else 0
    if case=='lost_collision':payload[176]^=1;payload[177]^=0x74
    if case=='lost_crc_already_matches':clean[145]=payload[145]=0
    if case=='intact_bad_crc':payload[200]^=1
    wire=case!='intact_cpu'
    crc=crc16(clean)
    encoded=bytes(reverse(b)for b in payload) if wire else bytes(payload)
    tail_high=reverse(crc>>8)if wire else crc>>8
    tail_low=reverse(crc&255)if wire else crc&255
    cpu.store_block(area,encoded+bytes([tail_high]))
    cpu.store_block(loc['area1'],bytes(clean)+bytes([crc>>8]))
    for off,value in ((4,area),(8,loc['area1']),(24,LBA)):
        cpu.store_block(stream+off,struct.pack('<I',value))
    cpu.store_block(stream+56,struct.pack('<H',lost))
    cpu.store_block(stream+60,bytes([1,0,int(wire),0,tail_low]))
    if case=='no_ready':cpu.store_block(stream+60,b'\0\0')
    if case=='kept':
        cpu.store_block(stream+60,b'\0\0')
        cpu.store_block(stream+70,b'\x02')
        cpu.store_block(stream+32,struct.pack('<I',LBA))
    cpu.store_block(RESULT,struct.pack('<I',0xdeadbeef))
    cpu.r[4],cpu.r[5],cpu.r[15]=LBA,RESULT,STACK
    cpu.pr,cpu.pc=STOP,loc['entry']
    before=(cpu.r[8:].copy(),cpu.gbr,cpu.vbr)
    cpu.writes=[]
    for _ in range(100000):
        if cpu.pc==STOP:break
        cpu.step()
    else:raise AssertionError('Whole linked SCI take exceeded100000 instructions')
    require((cpu.r[8:],cpu.gbr,cpu.vbr)==before,'SCI take failed returning callee ABI')
    require(not cpu.mmio_reads and not cpu.mmio_writes,'SCI take touched hardware')
    allowed=[(STACK-512,512),(area,544),(stream,84),(RESULT,4)]
    for _,at,_,count in cpu.writes:
        require(any(begin<=at and at+count<=begin+size for begin,size in allowed),
                'SCI take wrote outside receive state/private stack/result: '+hex(at))
    result=cpu.read(RESULT);returned=cpu.r[0];after=cpu.block(area,512)
    if case=='no_ready':expected_result,expected_return=1,0
    elif case=='kept':expected_result,expected_return=0,loc['area1']
    elif case=='intact_bad_crc' or (lost and not old):expected_result,expected_return=5,0
    else:expected_result,expected_return=0,area
    require((result,returned)==(expected_result,expected_return),
            f'{case}: returned{hex(returned)}/result{result}, expected{hex(expected_return)}/{expected_result}')
    if case not in ('no_ready','kept'):
        require(cpu.read(stream+60,1)==0,'Examined area remains marked ready')
        require(cpu.read(stream+70,1)==int(result==0),'Refused area became retained success')
    if result==0 and returned==area:
        if case=='lost_collision':
            require(old and after[176]==1 and after[177]==0x8b and after[145]==(clean[145]^0xcb),
                    'Old linked control did not recreate exact damaged-header collision')
            require(after!=clean and crc16(after)==crc,'Old collision not independently CRC-equal')
        else:require(after==clean,'Intact/clean reconstructed payload differs from authored bytes')
    if case=='kept':require(cpu.block(loc['area1'],512)==clean,'Kept payload changed')
    if case=='no_ready':require(after==encoded,'Pending read mutated payload')
    return dict(case=case,old_control=old,instructions=cpu.instructions,
        returned_pointer=hex(returned),result=result,source_lost_marker=lost,
        examined_area_ready=cpu.read(stream+60,1),kept=cpu.read(stream+70,1),
        source_payload_crc16=crc,post_take_crc16=crc16(after),
        post_take_header=list(after[176:178]),
        wrong_crc_equal_payload_accepted=bool(case=='lost_collision'and old),
        RAM_writes=len(cpu.writes),MMIO_reads=0,MMIO_writes=0,
        executed_pc_sha256=sha(struct.pack('<%dI'%len(cpu.path),*cpu.path)))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resident',type=Path,required=True)
    parser.add_argument('--old-resident',type=Path,required=True)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    source=(ROOT/'src/loader/sci_stream.c').read_text()
    require('if(((uint32_t)crc ^ sent) | lost)'in source,
            'Source is not the current lost-byte acceptance quarantine')
    new,old=read_elf(args.resident),read_elf(args.old_resident)
    cases=('lost_collision','lost_clean','lost_crc_already_matches','intact_wire',
           'intact_cpu','intact_bad_crc','no_ready','kept')
    rows=[exercise(image,case,is_old)for image,is_old in ((new,False),(old,True))for case in cases]
    functions={}
    for key,im in (('new',new),('old',old)):
        at,body,_=symbol_bytes(im,'_kui_sci_stream_take',2)
        functions[key]=dict(address=hex(at),bytes=len(body),sha256=sha(body),layout=layout(im))
    report=dict(schema='kui.sci-stream.repair-quarantine-linked.v1',status='PASS',
        resident_elf_sha256=sha(new[0]),old_resident_elf_sha256=sha(old[0]),
        inputs=dict(resident=str(args.resident.resolve()),old_resident=str(args.old_resident.resolve())),
        test_sha256=sha(Path(__file__).read_bytes()),
        source_sha256={p:sha((ROOT/p).read_bytes())for p in SOURCE_FILES},
        model_source_sha256={p:sha((ROOT/p).read_bytes())for p in MODEL_FILES},
        functions=functions,cases=rows,actual_whole_linked_take_executed=True,
        CRC_oracle_substitutions=0,function_substitutions=0,hardware_executed=False,
        limits=['Post-reception RAM fixtures are authored; SCI/DMA reception and timing are not executed.',
                'The old collision proves the actual repair routine can accept broader corruption; it does not establish all hardware failures share that cause.',
                'The new guard refuses every lost-byte-marked area. Fresh reception/retry and GD progress are covered by separate production host tests.',
                'No game, WinCE, cache coherence or hardware compatibility result is claimed.'])
    rendered=json.dumps(report,indent=2)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(rendered)
    print(rendered,end='')
if __name__=='__main__':main()
