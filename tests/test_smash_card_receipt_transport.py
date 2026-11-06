#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute both linked low receipt transports, including the real capsule CRC.

Raw SD, worker retirement and hardware status are explicit fixture boundaries.
CRC leaves and complete OCBP loops execute their actual linked SH instructions
in the independently cross-checked fast integer decoder; other low code and
the original MDE argument helpers execute in the existing Python SH decoder.
The pure high-stage receipt sequence operation also executes as linked SH.
Epoch admission, fresh card identity and high-stage publication remain separate gates.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from runtime_package import flatten_elf
from test_smash_native_gin_linked import Machine,State,RETURN
import test_smash_menu_loader_runtime as forward
import test_smash_menu_return_runtime as returning

SLOT,SIZE,NATIVE_TARGET=0x2a0,96,0xacc00000


def sha(data):return hashlib.sha256(data).hexdigest()


class ActualPureSH:
    def bind(self,elf,run,target,expected_crc):
        self.fast=Machine(run);self.fast.load(elf)
        self.pure={v[0]&0x1fffffff for n,v in elf[4].items()
            if n in ('_kui_cold_load_crc32_leaf','_kui_retail_crc32')}
        self.expected_crc,self.receipt_target=expected_crc,target+SLOT
        self.crc_entry=elf[4]['_kui_cold_load_crc32'][0]&0x1fffffff
        self.crc_return=None;self.full_crc_verified=False
        self.receipt_writes=0;self.fast_instructions=0;self.fast_cache_lines=0
        self.video_target=target+128+368 if '__kui_smash_return_phase_video_transport' in elf[4] else 0
        self.video_writes=0
        self.video_blank_probe_bytes=0
        self.observe_receipt=True

    def step(self):
        if hasattr(self,'fast'):
            pc=self.pc&0x1fffffff
            if self.crc_return==self.pc:
                self.full_crc_verified=self.r[0]==self.expected_crc
                self.crc_return=None
            if pc==self.crc_entry and (self.r[5]&0x1fffffff)==self.receipt_target-SLOT&0x1fffffff:
                self.crc_return=self.pr
            op=self.read(self.pc,2)
            loop=(op&0xf0ff==0x00a3 and self.block(self.pc+2,6)==
                struct.pack('<3H',0x4110,0x8ffc,0x7020|((op>>8&15)<<8)))
            if pc in self.pure or loop:
                c=self.fast.c;sp=self.r[15];old_pr=self.pr
                for i,value in enumerate(self.r):c.r[i]=value
                c.pc=self.pc;c.pr=old_pr if loop else RETURN;c.sr=self.sr
                c.gbr,c.vbr,c.mach,c.macl=self.gbr,self.vbr,self.mach,self.macl
                c.ccr=self.original_ccr;c.minimum_stack=sp&0x1fffffff
                before=self.block(sp-128,128);self.fast.put(sp-128,before)
                if not loop:
                    self.fast.put(self.r[5],self.block(self.r[5],self.r[6]))
                first,end=sp&0x1fffffff,(sp&0x1fffffff)-128
                c.watch_first[0]=0x0c000000;c.watch_end[0]=end;c.watch_writes[0]=0
                c.watch_first[1]=first;c.watch_end[1]=0x0d000000;c.watch_writes[1]=0
                steps,cache=c.steps,c.cache_count
                result=self.fast.execute(stop=RETURN,break_pc=self.pc+8 if loop else 0)
                assert result==(1 if loop else 0)
                assert not c.watch_writes[0] and not c.watch_writes[1], 'Pure helper wrote outside private frame'
                assert c.r[15]==sp and c.ccr==self.original_ccr
                after=self.fast.block(sp-128,128)
                for i,(a,b) in enumerate(zip(before,after)):
                    if a!=b:self.write(sp-128+i,b,1)
                self.r[:]=list(c.r);self.sr=c.sr;self.mach,self.macl=c.mach,c.macl
                self.gbr,self.vbr=c.gbr,c.vbr;self.pr=old_pr
                self.pc=c.pc if loop else old_pr
                count=c.steps-steps;self.instructions+=count;self.fast_instructions+=count
                self.fast_cache_lines+=c.cache_count-cache
                if hasattr(self,'private_min_sp'):
                    self.private_min_sp=min(self.private_min_sp,c.minimum_stack|0xa0000000)
                return
        super().step()

    def write(self,address,value,count=4):
        if getattr(self,'observe_receipt',False) and self.video_target and \
                self.video_target<=address<self.video_target+80 and \
                self.pc not in (forward.RAW,forward.RAW-0x20000000):
            assert self.retired_success
            if not self.full_crc_verified:
                # The immutable first-sector probe includes the16-byte
                # blank VID1 header at496. It grants no video authority.
                offset=address-(self.video_target-496)
                expected=self.capsule[offset:offset+count]
                assert address+count<=self.video_target+16 and int.from_bytes(expected,'little')==value, \
                    'Live video state written before complete capsule CRC'
                self.video_blank_probe_bytes+=count
            else:self.video_writes+=count
        if getattr(self,'observe_receipt',False) and self.receipt_target<=address<self.receipt_target+SIZE and \
                self.pc not in (forward.RAW,forward.RAW-0x20000000):
            assert self.full_crc_verified, 'Live receipt written before complete capsule CRC success'
            if hasattr(self,'retired_success'):assert self.retired_success
            self.receipt_writes+=count
        return super().write(address,value,count)


class ForwardCPU(ActualPureSH,forward.RuntimeCPU):
    def raw_read(self):
        lba,destination,count=self.r[4:7]
        assert 0<count<=4096
        self.raw_calls.append((lba,destination,count))
        for i in range(count):
            block=self.capsule[self.raw_map[lba+i]*512:(self.raw_map[lba+i]+1)*512]
            assert len(block)==512
            self.store_block(destination+i*512,block)
        self.r[0]=0;self.pc=self.pr
class ReturnCPU(ActualPureSH,returning.ReturnCPU):
    def __init__(self,data,kind):
        super().__init__(data,kind)
        self.bind(ACTIVE_RETURN,ACTIVE_RUN,returning.TARGET,
            zlib.crc32(returning.capsule(True,returning.phase_video_flow(ACTIVE_RETURN[4]))))
        # Fixture construction precedes every runtime ownership boundary.
        self.observe_receipt=False


ACTIVE_RETURN=ACTIVE_RUN=None


def forward_case(elf,native,run,receipt,corrupt=False):
    original=forward.RuntimeCPU;forward.RuntimeCPU=ForwardCPU
    try:cpu,_,argv,ip,selection,_=forward.prepare(elf[0],elf[2],elf[4],'shining_force')
    finally:forward.RuntimeCPU=original
    payload,_=flatten_elf(native[0]);payload=bytes(payload)
    assert payload[0x1e0:0x1f0]==b'KUINAD01'+struct.pack('<2I',1,16)
    assert payload[SLOT:SLOT+16]==b'KUISCR02'+struct.pack('<2I',2,96) and payload[SLOT+16:SLOT+SIZE]==bytes(80)
    assert native[4]['__retail_map'][0]==0x8c011000, 'Fixed map offset moved'
    prefix=struct.pack('<4I',0x5349554b,0x31554e4d,1,304)
    offsets=[i for i in range(0,len(payload)-304,4) if payload[i:i+16]==prefix and payload[i+16:i+304]==bytes(288)]
    assert len(offsets)==1
    offset=offsets[0];padded=(len(payload)+511)&~511;expected=payload+bytes(padded-len(payload))
    cpu.capsule=bytearray(expected)
    if corrupt:cpu.capsule[-1]^=1
    blocks=padded//512;first=min(128,blocks)
    second=1000 if first<blocks else 0
    cpu.raw_map={100+i:i for i in range(first)}
    cpu.raw_map.update({second+i:first+i for i in range(blocks-first)})
    rt=elf[4]['_kui_smash_menu_loader_runtime'][0]
    wire=bytearray(cpu.block(rt,728));plan=bytearray(wire[24:88])
    struct.pack_into('<8I',plan,16,len(payload),padded,zlib.crc32(expected),offset,100,first,second,blocks)
    struct.pack_into('<I',plan,60,0);struct.pack_into('<I',plan,60,zlib.crc32(plan))
    wire[24:88]=plan;struct.pack_into('<I',wire,220,0);struct.pack_into('<I',wire,220,zlib.crc32(wire[:224]))
    wire[632:728]=receipt;cpu.store_block(rt,wire)
    cpu.bind(elf,run,NATIVE_TARGET,zlib.crc32(expected));diagnostic=elf[4]['_kui_smash_menu_loader_diagnostic'][0]
    reason=0
    for _ in range(5000000):
        if cpu.pc in (forward.RAW,forward.RAW-0x20000000):cpu.raw_read()
        if cpu.pc==NATIVE_TARGET:break
        if cpu.pc==0xac007000:
            reason=cpu.read(diagnostic+4);break
        cpu.step()
    else:raise AssertionError('Actual SESSION1 forward exceeded bound')
    if corrupt:
        assert reason==11 and not cpu.receipt_writes and not cpu.full_crc_verified
    else:
        assert not reason and cpu.pc==NATIVE_TARGET and cpu.full_crc_verified and cpu.receipt_writes==96
        assert cpu.block(NATIVE_TARGET+SLOT,SIZE)==receipt
        assert cpu.block(NATIVE_TARGET+0x1000,4096)==expected[0x1000:0x2000]
        assert cpu.block(NATIVE_TARGET+offset+32,256)==argv and cpu.block(NATIVE_TARGET+offset+288,16)==ip
    return dict(case='bad_native_capsule_crc' if corrupt else 'outer_to_actual_native_prefix',reason=reason,
        actual_instructions=cpu.instructions,fast_actual_SH_instructions=cpu.fast_instructions,
        actual_OCBP_lines=cpu.fast_cache_lines,receipt_bytes_written=cpu.receipt_writes,
        receipt_written_only_after_complete_CRC=True,actual_prefix_map_unchanged=True,
        capsule_blocks=blocks,raw_calls=[dict(lba=a,blocks=n) for a,_,n in cpu.raw_calls])


def audit(outer,native,originals,run):
    global ACTIVE_RETURN,ACTIVE_RUN
    outer,native,originals=map(Path,(outer,native,originals))
    out=read_elf(outer/'handoff.elf');ret=read_elf(native/'return-loader.elf');entry=read_elf(native/'entry.elf')
    assert forward.session_flow(out[4]) and forward.session_flow(ret[4])
    video_transport='__kui_smash_return_phase_video_transport' in ret[4]
    return_bytes=516+(80 if video_transport else 0)
    assert out[4]['_kui_smash_menu_loader_runtime'][1]==728 and ret[4]['_kui_smash_menu_return_runtime'][1]==return_bytes
    mde=(originals/'MDE_US.BIN').read_bytes();assert sha(mde)=='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
    ACTIVE_RETURN,ACTIVE_RUN=ret,run
    original=returning.ReturnCPU
    class BoundReturn(ReturnCPU):
        def step(self):
            self.observe_receipt=True
            super().step()
    returning.ReturnCPU=BoundReturn
    try:
        good,cpu=returning.exercise(ret[0],ret[2],ret[4],mde,'sf_return',return_cpu=True)
        bad=returning.exercise(ret[0],ret[2],ret[4],mde,'changed_card_receipt')
    finally:returning.ReturnCPU=original
    assert cpu.receipt_writes==96 and cpu.full_crc_verified
    if video_transport:assert cpu.video_writes==80
    receipt=cpu.block(returning.TARGET+SLOT,SIZE);assert receipt==returning.card_receipt()
    # Execute the exact pure high-stage owner/sequence/CRC operation. Its
    # caller's fresh card identity and terminal ownership gates are separate.
    stage=read_elf(outer/'stage.elf');machine=Machine(run);machine.load(stage);address=0x8c100000
    machine.put(address,receipt);result,steps,_=machine.call(stage[4]['_kui_smash_card_receipt_transfer'][0],[address,1])
    assert result==1
    transferred=machine.block(address,96)
    assert transferred[:16]==receipt[:16] and transferred[20:88]==receipt[20:88]
    assert struct.unpack_from('<I',transferred,16)[0]==1|(1<<8)
    assert struct.unpack_from('<I',transferred,88)[0]==2
    assert zlib.crc32(transferred[:92]+bytes(4))==struct.unpack_from('<I',transferred,92)[0]
    ce_operations=[]
    for owner in (3,4):
        before=forward.card_receipt(owner=owner,sequence=1);machine.put(address,before)
        result,_,status=machine.call(stage[4]['_kui_smash_card_receipt_transfer'][0],[address,1])
        assert status==0 and result==0 and machine.block(address,96)==before
        result,_,status=machine.call(stage[4]['_kui_smash_card_receipt_ce_return_valid'][0],[address])
        assert status==0 and result==1 and machine.block(address,96)==before
        result,ce_steps,status=machine.call(stage[4]['_kui_smash_card_receipt_ce_return'][0],[address])
        after=machine.block(address,96)
        assert status==0 and result==1 and after[:16]==before[:16] and after[20:88]==before[20:88]
        assert struct.unpack_from('<I',after,16)[0]==1|(1<<8) and struct.unpack_from('<I',after,88)[0]==2
        assert zlib.crc32(after[:92]+bytes(4))==struct.unpack_from('<I',after,92)[0]
        ce_operations.append(dict(previous_owner=owner,next_owner=1,previous_sequence=1,next_sequence=2,
            generic_transfer_refused=True,dedicated_transfer_executed=True,actual_SH_instructions=ce_steps))
    cases=[forward_case(out,entry,run,transferred),forward_case(out,entry,run,transferred,True)]
    good.update(receipt_written_only_after_retirement_and_complete_CRC=True,
        receipt_bytes_written=cpu.receipt_writes,phase_video_bytes_written=cpu.video_writes,
        blank_video_header_bytes_from_initial_probe=cpu.video_blank_probe_bytes,
        phase_video_written_only_after_retirement_and_complete_CRC=video_transport,
        fast_actual_SH_instructions=cpu.fast_instructions)
    return dict(schema='kui.smash.card-receipt-transport.v2',status='PASS',
        artifacts_sha256={'outer_handoff':sha(out[0]),'native_return':sha(ret[0]),'native_entry':sha(entry[0]),'outer_stage':sha(stage[0])},
        native_to_outer=good,receipt_corruption_before_retirement=bad,
        high_pure_sequence_operation=dict(actual_SH_instructions=steps,previous_owner=2,next_owner=1,
            previous_sequence=1,next_sequence=2),
        dedicated_CE_return_operations=ce_operations,
        outer_to_native=cases,runtime_bytes=dict(loader=728,returning=return_bytes),receipt_bytes=96,receipt_offset='0x2a0',
        phase_video_transport=video_transport,
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in (
            'tests/test_smash_card_receipt_transport.py','tests/test_smash_menu_loader_runtime.py',
            'tests/test_smash_menu_return_runtime.py','tests/sh4_integer_fast.c')},
        limits=['Worker retirement and raw SD/peripheral hardware use explicit fixture results.',
            'The pure high receipt operation executes, while its fresh card identity/quiescent lifecycle gates and high-stage publication are separate qualifications.',
            'Integer/coherent-memory/cache-operation sequencing only; no console timing, gameplay or physical save durability claim.'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('outer','native','originals'):p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--report',type=Path);args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='kui-session-transport-') as directory:
        library=Path(directory)/'integer.so'
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic','-fPIC','-shared',
            str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(library)],check=True)
        loaded=ctypes.CDLL(str(library));run=loaded.sh4_fast_run
        run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
        result=audit(args.outer,args.native,args.originals,run)
    text=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(text)
    print(text,end='')
