#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the original preheap clear and linked one-shot installer bridge.

Owner instructions are external inputs. Every original clear instruction
executes in a scalar SH decoder; short complete-state differentials bind that
decoder to the independent Python model. The linked prepare, arm, wrapper,
heap hook, actual allocator/guards and final tail execute unchanged SH code.
The existing cold proof's separately validated worker-handoff leaf is the
boundary for installing a patterned retained payload; relocation/worker
arming remain separate mandatory proofs. This is not whole-game emulation.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
import check_native_bottom_heap as cold
from check_vmu_native_worker_gateway import read_elf
from native_original_clear_sh import OriginalClear
from profile_vmu_native_release_sh import ReleaseSH
from sh4_delay_slots import validate_delay_slot
from test_vmu_native_heap_execution import SH,MASK,signed
from check_native_preclear import FLOW_SOURCES,compiled_profile

OWNER_SHA='a9fa66b6bcc482dfaa47683d552e42f24a8200c8efa5ed905d8184eedeba3ac0'
CLEAR=0x8c01c6e8
RETURN=0xdeadbeee
STAGE=0x8ce00000
OWNER_SP=0x8c00f3c4

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def differential(engine,stage,source):
    checks=0
    for alias in (0,0x80000000,0xa0000000):
        for count in (0,1,2,31,32,33,513):
            slow=ReleaseSH(stage);slow.store_block(0x8c010000,source)
            slow.r[4:7]=[0x0c300003|alias,0x5a,count];slow.pr=RETURN;slow.r[15]=0x8c00ed00
            fast=copy.deepcopy(slow)
            _,writes,instructions=slow.run(CLEAR,slow.r[4:7],slow.r[15])
            row=engine.run(fast,CLEAR)
            assert (slow.r,slow.sr,slow.pr,slow.memory)==(fast.r,fast.sr,fast.pr,fast.memory)
            assert len(writes)==row['byte_writes'] and instructions==row['instructions']
            checks+=1
    return checks

class Flow(cold.Cold):
    def __init__(self,build,original,record,reader,engine):
        super().__init__(build,dict(slug='time-stalkers',startup=str(original),record=str(record)),reader=reader)
        self.watch=False;self.engine=engine;self.reader=reader
        self.resident=read_elf(build/('resident-sci.elf' if reader==1 else 'resident-legacy.elf'))
        for kind,offset,address,_,stored,memory,_,_ in self.resident[2]:
            if kind==1:self.store_block(address,self.resident[0][offset:offset+stored]+bytes(memory-stored))
        self.control_at=self.resident[4]['_kui_vmu_observe_boot_control'][0]|0x20000000
        self.cell=self.resident[4]['_kui_vmu_native_heap_original'][0]
        self.low_hook=self.resident[4]['_kui_vmu_observe_heap_hook'][0]
        self.low_terminal=self.resident[4]['_kui_vmu_observe_return_hook'][0]
        offset=self.resident[4]['__kui_native_heap_resume_offset']
        assert offset[1:]==(0,0,0xfff1)
        self.low_resume=self.low_hook+offset[0]
        assert self.block(self.low_resume,4)==struct.pack('<2H',0x6f83,0x4f26)
        self.store_block(0x8c010000,self.image)
        # The execution boundary is the original clear call after CRT BSS
        # initialization. Remove the cold model's earlier isolated-init state.
        self.store_block(self.owner[13],bytes(0x30))
        self.store_block(self.symbols['_native_driver'][0]+72,hashlib.sha256(self.image).digest())
        self.write(self.owner[7],self.owner[8])
        self.after_erase=False;self.in_installer=False;self.installer_returned=False
        self.clear_calls=[];self.publications=[];self.owner_post=None
        self.init_return=None;self.initializer_calls=0;self.abstract_worker_handoffs=0
        self.retained=None;self.original_state=None
        self.hook_checkpoint=self.finish_checkpoint=None

    def clone(self):
        result=Flow.__new__(Flow)
        result.__dict__={name:(value if name=='engine' else copy.deepcopy(value))
            for name,value in self.__dict__.items() if name not in ('hook_checkpoint','finish_checkpoint')}
        result.hook_checkpoint=result.finish_checkpoint=None
        return result

    def read(self,address,count=4):
        if getattr(self,'after_erase',False):
            assert not SH.offset(STAGE,self.stage_end-STAGE)<=SH.offset(address,count)<SH.offset(self.stage_end,0),\
                'A high-stage read/fetch follows its erasure'
        return super().read(address,count)

    def write(self,address,value,count=4):
        if getattr(self,'after_erase',False):
            p=address&0x1fffffff
            assert not (STAGE&0x1fffffff)<=p<(self.stage_end&0x1fffffff),\
                'A high-stage write follows its erasure'
        return super().write(address,value,count)

    def plain(self,op,pc):
        n=(op>>8)&15
        if op&0xf0ff==0x002a:
            self.r[n]=self.pr;self.instructions+=1;return
        if op&0xf0ff in (0x4002,0x4012):
            self.r[n]=(self.r[n]-4)&MASK
            self.write(self.r[n],self.mach if op&0xff==2 else self.macl)
            self.instructions+=1;return
        if op&0xf0ff in (0x4006,0x4016):
            value=self.read(self.r[n]);self.r[n]=(self.r[n]+4)&MASK
            if op&0xff==6:self.mach=value
            else:self.macl=value
            self.instructions+=1;return
        if op&0xf0ff==0x4007:
            self.sr=self.read(self.r[n]);self.r[n]=(self.r[n]+4)&MASK
            self.instructions+=1;return
        if op&0xf0ff==0x00a3 and not self.in_installer:
            address=self.r[(op>>8)&15]&~31
            expected={0x8c0102c0,*range((OWNER_SP-52)&~31,OWNER_SP,32)}
            assert (address&0xdfffffff) in expected,('Unexpected publication',hex(address),hex(pc))
            self.publications.append(address);self.instructions+=1;return
        return super().plain(op,pc)

    def leaf(self,name):
        super().leaf(name)
        if name=='_native_passive_run':
            # The cold checker validates the exact lease/header at this
            # existing handoff. Model only its declared return postconditions
            # and a full retained-payload canary, not worker machine code.
            payload=self.begin+32
            self.store_block(payload,bytes((i*37+(i>>5)+19)&255 for i in range(self.lease_bytes)))
            self.retained=self.block(self.begin,self.lease_bytes+32)
            SH.write(self,self.control_at,0);SH.write(self,self.control_at+4,0)
            SH.write(self,self.control_at+8,4)
            self.abstract_worker_handoffs+=1

    def scalar(self):
        pc=self.pc;op=self.read(pc,2);n=(op>>8)&15;target=None;delay=False
        if STAGE<=pc<self.stage_end:self.executed.add(pc)
        if op>>12 in (0xa,0xb):
            target=(pc+4+2*signed(op&0xfff,12))&MASK;delay=True
            if op>>12==0xb:self.pr=pc+4
        elif op&0xf0ff in (0x400b,0x402b):
            target=self.r[n];delay=True
            if op&0xf0ff==0x400b:self.pr=pc+4
        elif op==0xb:target=self.pr;delay=True
        elif op>>8 in (0x89,0x8b,0x8d,0x8f):
            taken=bool(self.sr&1)==(op>>8 in (0x89,0x8d));delay=op>>8 in (0x8d,0x8f)
            target=pc+4+2*signed(op&255,8) if taken else pc+(4 if delay else 2)
        if target is None:self.plain(op,pc);self.pc=pc+2
        else:
            self.instructions+=1
            if delay:
                slot=self.read(pc+2,2);validate_delay_slot(slot,pc);self.plain(slot,pc+2)
                if STAGE<=pc+2<self.stage_end:self.executed.add(pc+2)
            self.pc=target&MASK

    def run_to(self,stop):
        for _ in range(20000000):
            if self.pc==stop:return
            if self.pc==CLEAR:
                args=self.r[4:7].copy();stage_before=self.block(STAGE,self.stage_end-STAGE)
                if len(self.clear_calls)<2:
                    expected=([self.begin,0,STAGE-self.begin] if not self.clear_calls else
                        [self.stage_end,0,self.begin+self.count-self.stage_end])
                    assert args==expected and not self.installer_returned
                else:
                    assert len(self.clear_calls)==2 and self.installer_returned and args==[STAGE,0,self.stage_end-STAGE]
                    assert self.pr==self.low_resume,'Deferred clear lost the LOW return'
                    assert self.r[15]==self.stack,'Deferred clear retained a high frame'
                    assert self.sr&0x100000f0==0x100000f0
                    assert self.block(self.cell+16,16)==bytes(16),'Deferred tuple not retired before erase'
                row=self.engine.run(self,CLEAR);self.clear_calls.append(dict(args=args,**row))
                if len(self.clear_calls)<3:
                    assert self.block(STAGE,self.stage_end-STAGE)==stage_before,'Original partial clear erased owned stage'
                else:
                    assert not any(self.block(STAGE,self.stage_end-STAGE))
                    assert self.block(self.begin,self.lease_bytes+32)==self.retained
                    self.after_erase=True
                continue
            if self.pc==self.owner[6]:
                self.initializer_calls+=1;self.init_return=self.pr
            if self.pc==self.init_return and self.owner_post is None:
                self.owner_post=(self.r.copy(),self.sr,self.mach,self.macl,self.gbr)
            if self.pc==self.symbols['_kui_vmu_observe_stage_install'][0]:
                old_return=cold.RETURN;old_before=self.before
                self.before=(self.r.copy(),self.pr,self.sr);cold.RETURN=self.pr
                self.in_installer=self.watch=True
                try:assert self.run_cold(),self.refusal
                finally:
                    self.watch=self.in_installer=False;cold.RETURN=old_return;self.before=old_before
                self.installer_returned=True;continue
            if self.pc==self.symbols['_kui_native_preclear_finish'][0] and self.finish_checkpoint is None:
                self.finish_checkpoint=self.clone()
            if self.pc in self.leaves:
                self.leaf(self.leaves[self.pc]);continue
            self.scalar()
        raise AssertionError('Preheap bridge exceeded its exact scalar bound')

    def function(self,name,args=()):
        before=self.r[8:15].copy();self.r[15]=self.stack;self.pc=self.symbols[name][0];self.pr=RETURN
        for index,value in enumerate(args):self.r[4+index]=value
        self.run_to(RETURN)
        assert self.r[15]==self.stack and self.r[8:15]==before,'Preclear helper lost C ABI'

    def exercise(self):
        self.function('_native_preclear_prepare')
        self.function('_native_arm_heap_hook',(0x8c00f3ac,))
        self.function('_native_preclear_arm')
        self.hook_checkpoint=self.clone()
        assert self.read(self.control_at)==self.symbols['_kui_native_preclear_stage_install'][0]
        # Startup21e establishes IMASK15. The intervening025be8 graph
        # preserves that mask;03ab34 additionally clears BL at03acc2.
        self.pc=0x8c010226;self.pr=RETURN;self.r[15]=OWNER_SP;self.sr=0x600000f1
        before=self.r[8:15].copy();self.run_to(0x8c010230)
        assert self.r[0]==self.begin and self.sr&1 and self.r[15]==OWNER_SP and self.r[8:15]==before
        assert self.read(0x8c0102d0)==self.low_terminal and self.read(self.owner[7])==self.low_hook
        self.run_to(0x8c01023a)
        assert len(self.clear_calls)==3 and self.initializer_calls==1 and self.abstract_worker_handoffs==1
        assert sum(row['byte_writes'] for row in self.clear_calls)==self.count
        assert self.owner_post is not None and self.r==self.owner_post[0]
        assert (self.sr,self.mach,self.macl,self.gbr)==self.owner_post[1:]
        assert self.pr==0x8c01023a and self.r[15]==OWNER_SP
        assert self.block(self.begin,self.lease_bytes+32)==self.retained
        # Reenter the original sole caller. Its restored argument loads and
        # delay slot must reach retained terminal code before any clear runs.
        repeat=self.clone();repeat.pc=0x8c010226;repeat.pr=RETURN;repeat.r[15]=OWNER_SP
        repeat.writes=[];repeat.run_to(self.low_terminal)
        assert len(repeat.clear_calls)==3 and not repeat.writes
        assert repeat.block(repeat.begin,repeat.lease_bytes+32)==repeat.retained
        return dict(reader=self.reader,stage_end=hex(self.stage_end),owner_sp=hex(OWNER_SP),
            low_resume=hex(self.low_resume),entry_sr='0x600000f1',clear_calls=self.clear_calls,initializer_calls=1,
            actual_allocator_calls=self.owner_calls,complete_stage_erased_after_installer=True,
            no_high_fetch_read_write_after_erasure=True,full_retained_header_and_payload_preserved=True,
            all_original_initializer_registers_and_SR_MAC_GBR_restored=True,
            repeated_original_clear_reaches_low_terminal_before_any_store=True,
            worker_handoff_leaf_postconditions_modeled=self.abstract_worker_handoffs)

    def refusal_cases(self):
        rows=[]
        hook_cases=('pr','begin','value','bytes','mask','control','tuple','binding','retired-literal')
        for name in hook_cases:
            cpu=self.hook_checkpoint.clone();cpu.pc=cpu.symbols['_native_preclear_hook'][0]
            cpu.pr=0x8c010230;cpu.r[4:7]=[cpu.begin,0,cpu.count];cpu.r[15]=OWNER_SP;cpu.sr=0x600000f0
            if name=='pr':cpu.pr+=2
            elif name=='begin':cpu.r[4]+=32
            elif name=='value':cpu.r[5]=1
            elif name=='bytes':cpu.r[6]-=1
            elif name=='mask':cpu.sr&=~0x10
            elif name=='control':cpu.write(cpu.control_at+8,2)
            elif name=='tuple':cpu.write(cpu.cell+28,1)
            elif name=='binding':cpu.write(cpu.symbols['_kui_native_preclear_deferred'][0],cpu.cell+0x20000014)
            else:cpu.write(0x8c0102d0,CLEAR)
            try:cpu.run_to(0x8c010230)
            except cold.Refused:pass
            else:raise AssertionError('Preclear hook admitted '+name)
            assert cpu.refusal==(2,0x51) and not cpu.clear_calls
            self.executed|=cpu.executed
            rows.append(dict(phase='hook',mutation=name,refusal=cpu.refusal))
        finish_cases=('pr','BL','mask','binding','span','control','installer','stack',
            'tuple-target','tuple-begin','tuple-bytes','tuple-state','literal',
            'restored-original-literal',
            *('plan-'+str(i) for i in range(5)))
        for name in finish_cases:
            cpu=self.finish_checkpoint.clone();expected=0x56
            if name=='pr':cpu.r[4]+=2
            elif name=='BL':cpu.sr&=~0x10000000
            elif name=='mask':cpu.sr&=~0x10
            elif name=='binding':cpu.write(cpu.symbols['_kui_native_preclear_deferred'][0],cpu.cell+0x20000014)
            elif name=='span':
                at=cpu.symbols['_native_preclear_bytes'][0];cpu.write(at,cpu.read(at)+4)
            elif name.startswith('plan-'):
                at=cpu.symbols['_native_preclear'][0]+4*int(name[5:]);cpu.write(at,cpu.read(at)^1)
            else:
                expected=0x57
                if name=='control':cpu.write(cpu.control_at+8,3)
                elif name=='installer':cpu.write(cpu.control_at,1)
                elif name=='stack':cpu.write(cpu.control_at+4,1)
                elif name=='literal':cpu.write(0x8c0102d0,cpu.symbols['_native_preclear_hook'][0])
                elif name=='restored-original-literal':cpu.write(0x8c0102d0,CLEAR)
                else:
                    index={'tuple-target':0,'tuple-begin':1,'tuple-bytes':2,'tuple-state':3}[name]
                    at=cpu.cell+16+4*index;cpu.write(at,cpu.read(at)^1)
            try:cpu.run_to(RETURN)
            except cold.Refused:pass
            else:raise AssertionError('Preclear finish admitted '+name)
            assert cpu.refusal==(2,expected) and len(cpu.clear_calls)==2
            assert cpu.block(cpu.begin,cpu.lease_bytes+32)==cpu.retained
            assert any(cpu.block(STAGE,cpu.stage_end-STAGE))
            self.executed|=cpu.executed
            rows.append(dict(phase='finish',mutation=name,refusal=cpu.refusal))
        for offset,word,name in ((16,9,'missing-low-PR-pop'),(34,0x430b,'JSR-instead-of-tail-JMP'),
                                 (36,9,'missing-delay-slot-retirement')):
            cpu=self.finish_checkpoint.clone();at=cpu.symbols['_kui_native_preclear_stage_install'][0]+offset
            cpu.write(at,word,2)
            try:cpu.run_to(0x8c01023a)
            except AssertionError as error:
                expected=('Deferred tuple not retired before erase' if offset==36 else
                    'Deferred clear lost the LOW return')
                assert str(error)==expected,(name,str(error))
            else:raise AssertionError('Preclear machine-code counterfactual accepted '+name)
            assert not cpu.after_erase and len(cpu.clear_calls)==2
            self.executed|=cpu.executed
            rows.append(dict(phase='tail-code',mutation=name,erasure_not_admitted=True))
        return rows

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True)
    p.add_argument('--original',type=Path,required=True);p.add_argument('--record',type=Path,required=True)
    p.add_argument('--output',type=Path);a=p.parse_args();source=a.original.read_bytes()
    assert hashlib.sha256(source).hexdigest()==OWNER_SHA
    with tempfile.TemporaryDirectory(prefix='native-preclear-original-') as directory:
        engine=OriginalClear(directory);truth=differential(engine,a.build/'stage.elf',source)
        rows=[];refusals=[];executed=set()
        for reader in (1,2):
            cpu=Flow(a.build,a.original,a.record,reader,engine);rows.append(cpu.exercise())
            refusals.extend(dict(reader=reader,**row) for row in cpu.refusal_cases())
            executed|=cpu.executed
        ordinary=ReleaseSH(a.build/'stage.elf');ordinary.store_block(0x8c010000,source)
        ordinary.r[4:7]=[0x8c3b0000,0,0xc50000];ordinary.pr=0x8c010230
        baseline=engine.run(ordinary,CLEAR)
        stage_end=read_elf(a.build/'stage.elf')[4]['__retail_stage_bss_end'][0]
        assert not any(ordinary.block(STAGE,stage_end-STAGE))
    report=dict(schema='kui.native.preheap-clear.actual-execution.v1',source_sha256=OWNER_SHA,
        compiled_profile=compiled_profile(read_elf(a.build/'stage.elf')),
        artifacts={name:sha(a.build/name) for name in ('stage.elf','native-worker.elf','resident-sci.elf','resident-legacy.elf')},
        original_clear_scalar_differential_cases=truth,cases=rows,refusals=refusals,
        executed_stage_addresses=sorted(executed),
        original_unintercepted_clear_erases_stage=baseline,
        inherited_context_source_review=dict(imask_set_at='0x8c01021e',intervening_entry='0x8c025be8',
            mask_preserved_by=['0x8c03ab34','0xac025710'],mask_restored_at=['0x8c03ac8c','0xac025796'],
            BL_cleared_at='0x8c03acc2',following_no_SR_write_calls=['0x8c03acd4','0xac02501c','0x8c03ad78'],
            clear_call_boundary='0x8c010226',inherited_IMASK=15,inherited_BL=0,
            positive_entry_SR='0x600000f1',cache_loop_final_compare_sets_T=True,
            cache_reference=dict(url='https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware',
                document='SH7750/SH7750S/SH7750R Hardware Manual R01UH0456EJ0702 Rev7.02',section='4.5.3',page=134,
                premise='A non-associative operand-cache address-array write publishes a dirty valid line before replacing its tag.')),
        proof_source_sha256={name:sha(ROOT/name) for name in FLOW_SOURCES},
        limits=['Execution begins at the exact original preheap clear call; earlier CRT/hardware initialization and whole game are not emulated.',
            'Worker relocation/arming is the existing separately validated handoff boundary; its return postconditions and retained payload canary are modeled.',
            'RAM is coherent in this scalar model. Publication addresses and original P1 cache semantics are reviewed; physical cache lines and CCR timing are not emulated.',
            'Every original clear opcode/branch/delay slot/store and every new linked bridge/ownership predicate executes.'])
    if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
    print('Actual preheap-clear linked flow:',len(rows),'readers;',truth,'complete scalar differentials')
if __name__=='__main__':main()
