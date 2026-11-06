#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked early CE GIN diagnostic-state selection only.

All entry, copy and validator instructions come from the actual stage ELF.
CRC leaf calls use a pure zlib oracle. CPU RAM/SR/register banks are modeled;
PVR, VRAM, DMA, storage and the game are not executed or substituted.
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
from test_smash_ce_gin_adoption import AdoptionCPU,load_image,continuation_seed,GIN_SR
from test_smash_menu_loader_runtime import phase_video_seed,PHASE_VIDEO_REGS

STOP=0x8cff1234
STACK=0x8cff0000
IP=0x8c008000
GIN=0x8c010200
SOURCES=('src/loader/smash_ce_gin_stage.inc','src/core/smash_ce_gin.c',
         'src/core/smash_phase_video.c','src/loader/retail_display.c')
DEPENDENCIES=('tests/test_smash_ce_gin_adoption.py','tests/test_smash_menu_loader_runtime.py',
              'tests/test_smash_menu_loader_entry.py','tests/test_native_standalone_foreground_gin.py',
              'tests/test_vmu_native_sdk_poll_execution.py','tests/test_vmu_native_heap_execution.py')

def sha(data):return hashlib.sha256(data).hexdigest()
def require(ok,message):
    if not ok:raise AssertionError(message)
def p1(value):return value&0xdfffffff

class CPU(AdoptionCPU):
    def __init__(self,image):
        super().__init__()
        self.symbols=image[4]
        self.crc_entries={p1(v[0])for k,v in self.symbols.items()
                          if k in ('_kui_retail_crc32','_kui_stage_crc32','_kui_cold_load_crc32_leaf')}
        self.crc_calls=[];self.validation_calls=[];self.stopped=False;self.reason=None
        self.current_function=None
        load_image(self,image)
        self.writes=[];self.mmio_reads=[];self.mmio_writes=[]
    def read(self,at,count=4):
        require(0x0c000000<=at&0x1fffffff and (at&0x1fffffff)+count<=0x0d000000,
                'Early diagnostic selection attempted non-RAM read: '+hex(at))
        return super().read(at,count)
    def write(self,at,value,count=4):
        require(0x0c000000<=at&0x1fffffff and (at&0x1fffffff)+count<=0x0d000000,
                'Early diagnostic selection attempted PVR/VRAM/MMIO write: '+hex(at))
        super().write(at,value,count)
    def text(self,at):return self.block(at,100).split(b'\0',1)[0].decode('ascii')
    def step(self):
        at=p1(self.pc)
        if at in self.crc_entries:
            previous,source,count=self.r[4:7]
            self.crc_calls.append((source,count))
            self.r[0]=zlib.crc32(self.block(source,count),previous)&0xffffffff
            self.pc=self.pr
            return
        if at==p1(self.symbols['_stopped'][0]):
            self.stopped=True;self.reason=self.text(self.r[4]);return
        if at==p1(self.symbols['_kui_smash_phase_video_valid'][0]):
            self.validation_calls.append(self.r[4])
        super().step()

def video(kind):
    regs=list(PHASE_VIDEO_REGS)
    if kind=='interlace':
        regs[6]|=16;regs[3]=1280;regs[4]=319|(239<<10)|(321<<20)
    data=bytearray(phase_video_seed(regs))
    if kind=='bad_crc':data[72]^=1
    if kind=='bad_base':
        struct.pack_into('<I',data,24,0x100000)
        struct.pack_into('<I',data,72,0)
        struct.pack_into('<I',data,72,zlib.crc32(data))
    return bytes(data)

def run(image,profile,kind,old=False):
    sy=image[4]
    require(sy['__kui_smash_ce_child'][0]==profile,'Stage belongs to a different CE child')
    for name in ('_smash_ce_gin_begin','_smash_ce_gin_saved','_smash_ce_gin_ip_crc',
                 '_display','__smash_ce_return_video_seed','_stopped','_kui_smash_phase_video_valid'):
        require(name in sy,'Missing linked symbol '+name)
    cpu=CPU(image)
    seed,frame=continuation_seed(profile)
    if kind=='bad_gin':seed=seed[:108]+bytes([seed[108]^1])+seed[109:]
    original=bytearray((n*29+0x67)&255 for n in range(0x100))
    saved=[5,1,0x100000,0x100000,159|(239<<10)|(1<<20)]+[0]*9
    inherited=struct.pack('<14I',*saved)
    v=video(kind)
    cpu.store_block(GIN,seed);cpu.store_block(IP,original)
    cpu.store_block(sy['_display'][0],inherited)
    cpu.store_block(sy['__smash_ce_return_video_seed'][0],v)
    cpu.r[15]=STACK;cpu.pr=STOP;cpu.pc=sy['_smash_ce_gin_begin'][0]
    initial=(cpu.r[8:].copy(),cpu.gbr,cpu.vbr,list(cpu.inactive))
    cpu.writes=[]
    for _ in range(50000):
        if cpu.pc==STOP or cpu.stopped:break
        cpu.step()
    else:raise AssertionError('Early GIN selection exceeded bounded instruction budget')
    require(cpu.block(GIN,160)==seed and cpu.block(IP,0x100)==original,
            'Early selection modified original GIN/frame/IP bytes')
    require(not cpu.mmio_reads and not cpu.mmio_writes and not cpu.bank_changes,
            'Early selection touched hardware or changed SR register banks')
    if not cpu.stopped:
        require(cpu.r[8:]==initial[0] and cpu.gbr==initial[1] and cpu.vbr==initial[2] and
                cpu.inactive==initial[3], 'Early selection failed caller ABI preservation')
    require(cpu.block(sy['__smash_ce_return_video_seed'][0],80)==v,
            'Early selection modified the source VID1 descriptor')
    owned=[(sy['_smash_ce_gin_saved'][0],160),(sy['_smash_ce_gin_ip_crc'][0],4),
           (sy['_display'][0],56),(STACK-512,512)]
    for _,at,_,count in cpu.writes:
        at=p1(at)
        require(any(start<=at and at+count<=start+length for start,length in owned),
                'Early selection wrote outside saved state/private frame: '+hex(at))
    out=cpu.block(sy['_display'][0],56)
    invalid=kind in ('bad_crc','bad_base','bad_gin')
    if kind=='bad_gin':
        require(cpu.stopped and cpu.reason=='SMASH CE NEEDS ORIGINAL GIN CONTINUATION',
                'Original GIN refusal changed')
        require(not cpu.crc_calls or (IP,0x100)not in cpu.crc_calls,'Invalid GIN reached retained-IP check')
    elif invalid and not old:
        require(cpu.stopped and cpu.reason=='SMASH CE DIAGNOSTIC VIDEO REFUSED',
                'Invalid VID1 did not refuse before publication')
        require(out==inherited,'Invalid VID1 published display state')
    else:
        require(cpu.pc==STOP and not cpu.stopped,'Valid early GIN did not return')
        require(out==(inherited if old else v[16:72]),
                'Linked display state differs from '+('old mismatch control'if old else'validated VID1'))
        require(cpu.block(sy['_smash_ce_gin_saved'][0],160)==seed,'GIN saved frame changed')
        require(cpu.read(sy['_smash_ce_gin_ip_crc'][0])==zlib.crc32(original),
                'Original retained-IP CRC changed')
        require(cpu.crc_calls.count((IP,0x100))==1,'Retained-IP CRC boundary count changed')
        require(bool(cpu.validation_calls)!=old,'VID1 validator execution did not match component')
    # A refusal need not restore its live C frame: stopped is terminal. The
    # success ABI assertion above is deliberately limited to returning cases.
    return dict(case=kind,outcome='stopped'if cpu.stopped else'returned',
                stop_reason=cpu.reason,instructions=cpu.instructions,
                display_matches_VID1=out==v[16:72],display_retains_inherited=out==inherited,
                original_GIN_IP_unchanged=True,MMIO_reads=0,MMIO_writes=0,
                crc_oracle_calls=len(cpu.crc_calls),actual_VID1_validator_calls=len(cpu.validation_calls),
                RAM_writes=len(cpu.writes))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--old-stage',type=Path,required=True)
    p.add_argument('--profile',type=int,choices=(1,2),required=True)
    p.add_argument('--report',type=Path)
    a=p.parse_args()
    before={str(path.resolve()):sha(path.read_bytes())for path in (a.stage,a.old_stage)}
    current,old=read_elf(a.stage),read_elf(a.old_stage)
    rows=[run(current,a.profile,k)for k in ('progressive','interlace','bad_crc','bad_base','bad_gin')]
    negative=[run(old,a.profile,k,True)for k in ('progressive','interlace')]
    after={str(path.resolve()):sha(path.read_bytes())for path in (a.stage,a.old_stage)}
    require(before==after,'Linked stage input changed during execution')
    report=dict(schema='kui.smash.ce-diagnostic-video-linked.v1',status='PASS',profile=a.profile,
        stage_elf_sha256=before[str(a.stage.resolve())],old_stage_elf_sha256=before[str(a.old_stage.resolve())],
        inputs=before,inputs_immutable=True,cases=rows,old_negative=negative,
        source_sha256={name:sha((ROOT/name).read_bytes())for name in SOURCES},
        test_sha256=sha(Path(__file__).read_bytes()),
        model_source_sha256={name:sha((ROOT/name).read_bytes())for name in DEPENDENCIES},
        limits=['CRC leaves use a pure zlib oracle; actual linked begin, validators and memcpy execute.',
                'Authored SR/RAM and scanout descriptors; no game, timing, cache or hardware acceptance.'])
    text=json.dumps(report,indent=2)+'\n'
    if a.report:a.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
