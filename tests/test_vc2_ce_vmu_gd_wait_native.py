#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual fixed VC2 GD wait gate against hostile ABI boundaries.

The admission helper and native Sleep are scalar-only adversaries. The code,
delay slots, literal targets, register saves and stack switches come from the
newly assembled production gate. Scheduler/SD timing is outside this model.
"""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_armada_ce_maple_hook import CPU as BaseCPU, MASK, ROOT
from sh4_delay_slots import validate_delay_slot

ENTRY=0xacfce000
ADMIT=0x8c100000
FAULT=0x8c100100
SLEEP=0x01cf581c
RETRY=0x8c004200
LEGACY=0x8c004300
ACTIVE_P1=0x8c007000
ACTIVE_P2=0xac007000
FRAME=0x7e00fd00
PANIC_TOP=0xacfcfe00
LIMIT=1024


class CPU(BaseCPU):
    def __init__(self,code,*,function=0,sleeps=2,admitted=True,state=2,changed_state=None,seed=1,caller_pr=None):
        super().__init__(seed,0x01cf0000,1)
        self.binary=code
        self.pc=ENTRY
        self.r[0]=ACTIVE_P1;self.r[2]=RETRY;self.r[3]=LEGACY
        self.r[4:8]=[24,0x7e00f100,0x22334455,function]
        self.r[8]=self.r[15]=FRAME;self.r[9]=3
        self.sr=0x400080f0
        self.incoming_sr=0x40008000
        self.caller_pr=caller_pr if caller_pr is not None else (0x01cf23ce if function==4 else 0x01cf1f52)
        self.initial_args=self.r[4:8].copy()
        self.initial_special=(self.gbr,self.mach,self.macl)
        self.initial_saved=self.r[8:15].copy()
        self.frame=[0xa5000000+37*i for i in range(7)]+[self.caller_pr,self.incoming_sr]
        self.mem={FRAME+4*i:word for i,word in enumerate(self.frame)}
        self.mem[ACTIVE_P2]=state
        self.target_sleeps=sleeps;self.admitted=admitted;self.changed_state=changed_state
        self.admit_calls=[];self.sleep_calls=[];self.fault_calls=[]
        self.home_writes=[];self.polls=0;self.stop=None;self.steps=0
        self.writes=[];self.stack_changes=[];self.min_ce_sp=FRAME

    def code(self,address,size):
        if ENTRY<=address and address+size<=ENTRY+len(self.binary):
            return self.binary[address-ENTRY:address-ENTRY+size]
        raise AssertionError('Unadmitted instruction/literal read '+hex(address))

    def read(self,address):
        if address==ACTIVE_P2:
            self.polls+=1
            return self.mem[address]
        assert address%4==0
        if address in self.mem:return self.mem[address]
        return struct.unpack('<I',self.code(address,4))[0]

    def write(self,address,value):
        assert address%4==0
        ce=FRAME-48<=address<FRAME
        panic=PANIC_TOP-16<=address<PANIC_TOP
        assert ce or panic,'Write escaped caller homes/saved area: '+hex(address)
        if panic:assert self.sr&0xf0==0xf0,'Private stack written while unmasked'
        if ce:self.min_ce_sp=min(self.min_ce_sp,address)
        self.mem[address]=value&MASK;self.writes.append(address)

    def homes(self):
        assert self.r[15]==FRAME-48
        values=[]
        for i in range(4):
            at=self.r[15]+4*i
            self.write(at,0xc0de0000+len(self.home_writes)*16+i)
            values.append(at)
        self.home_writes.append(values)

    def clobber(self):
        saved=self.r[8:16].copy()
        for i in range(8):self.r[i]=(0xdead0000+17*i+len(self.sleep_calls))&MASK
        self.gbr^=0x12345678;self.mach^=0x89abcdef;self.macl^=0xfedcba98
        self.pr^=0x87654320;self.sr^=1
        assert self.r[8:16]==saved

    def admit(self):
        assert self.sr&0xf0==0xf0
        assert self.r[4:8]==[self.caller_pr,self.incoming_sr,self.initial_args[0],3|(self.initial_args[3]<<8)]
        self.admit_calls.append(self.r[4:8].copy())
        self.homes();target=self.pr;self.clobber()
        self.r[0]=SLEEP if self.admitted else 0
        self.pc=target

    def sleep(self):
        assert self.sr&0x500000f0==0x40000000,'Sleep called masked or blocked'
        assert self.sr&~1==self.incoming_sr&~1
        assert self.r[15]==FRAME-48 and self.r[8]==FRAME,'Sleep escaped original CE frame'
        assert self.r[4]==1
        self.sleep_calls.append((self.r[4],self.r[15],self.sr))
        self.homes();target=self.pr;self.clobber()
        if self.target_sleeps is not None and len(self.sleep_calls)>=self.target_sleeps:
            self.mem[ACTIVE_P2]=0 if self.changed_state is None else self.changed_state
        self.pc=target

    def fatal(self):
        assert self.sr&0xf0==0xf0,'Terminal helper called unmasked'
        assert self.r[15]==PANIC_TOP-16
        self.fault_calls.append(self.r[4:8].copy())
        self.stop='fault'

    def plain(self,word,pc):
        n=(word>>8)&15;m=(word>>4)&15
        if word&0xf0ff==0x4001:
            self.t(self.r[n]&1);self.r[n]>>=1;return
        if word&0xf0ff==0x4018:
            self.r[n]=(self.r[n]<<8)&MASK;return
        if word&0xf00f==0x200b:
            self.r[n]|=self.r[m];return
        if word&0xf00f==0x2008:
            self.t(not(self.r[n]&self.r[m]));return
        if word>>12==9:
            value=struct.unpack('<h',self.code(pc+4+2*(word&255),2))[0]
            self.r[n]=value&MASK;return
        if word>>8==0x88:
            value=word&255;value=value-256 if value&128 else value
            self.t(self.r[0]==value&MASK);return
        if word&0xf0ff==0x0002:
            self.r[n]=self.sr;return
        for opcode,attribute in ((0x0012,'gbr'),(0x000a,'mach'),(0x001a,'macl')):
            if word&0xf0ff==opcode:self.r[n]=getattr(self,attribute);return
        for opcode,attribute in ((0x401e,'gbr'),(0x400a,'mach'),(0x401a,'macl')):
            if word&0xf0ff==opcode:setattr(self,attribute,self.r[n]);return
        if word&0xf0ff==0x400e:
            assert self.sr&0x40000000
            self.sr=self.r[n];return
        old=self.r[15]
        super().plain(word,pc)
        if self.r[15]!=old:
            self.stack_changes.append((old,self.r[15],self.sr))
            if self.r[15]>=0x80000000:assert self.sr&0xf0==0xf0

    def run(self):
        while self.stop is None:
            if self.pc in (RETRY,LEGACY):
                self.stop='retry' if self.pc==RETRY else 'legacy';break
            self.steps+=1;assert self.steps<100000
            pc=self.pc;word=struct.unpack('<H',self.code(pc,2))[0]
            jsr=word&0xf0ff==0x400b;jump=word&0xf0ff==0x402b
            if word>>12==0xa or jsr or jump:
                if word>>12==0xa:
                    displacement=word&4095
                    if displacement&2048:displacement-=4096
                    target=(pc+4+2*displacement)&MASK
                else:
                    target=self.r[(word>>8)&15]
                    if jsr:self.pr=pc+4
                slot=struct.unpack('<H',self.code(pc+2,2))[0]
                validate_delay_slot(slot,pc);self.plain(slot,pc+2)
                if target==ADMIT:self.admit()
                elif target==SLEEP:self.sleep()
                elif target==FAULT:self.fatal()
                else:self.pc=target
            elif word>>8 in (0x89,0x8b):
                displacement=word&255
                if displacement&128:displacement-=256
                take=bool(self.sr&1)==(word>>8==0x89)
                self.pc=(pc+4+2*displacement if take else pc+2)&MASK
            else:self.plain(word,pc);self.pc=pc+2
        assert [self.mem[FRAME+4*i] for i in range(9)]==self.frame,'Original 36-byte low frame changed'
        return self


class GDWaitNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc=os.environ.get('KUI_SH_GCC') or shutil.which('sh-elf-gcc')
        if not gcc:
            candidate=ROOT.parent/'sdk-restored/.deps/sh-elf/bin/sh-elf-gcc'
            gcc=str(candidate) if candidate.exists() else None
        if not gcc:raise unittest.SkipTest('Native SH toolchain unavailable')
        prefix=Path(gcc).resolve().parent
        cls.temporary=tempfile.TemporaryDirectory(prefix='vc2-gd-wait-native-')
        cls.addClassCleanup(cls.temporary.cleanup)
        folder=Path(cls.temporary.name)
        linker=folder/'test.ld'
        linker.write_text('''OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vc2_ce_vmu_gd_wait)
SECTIONS { . = 0xacfce000; .gate : { KEEP(*(.gd_wait.entry)) }
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }
''')
        obj=folder/'gate.o';elf=folder/'gate.elf';binary=folder/'gate.bin'
        flags=['-m4-single-only','-ml','-ffreestanding','-I'+str(ROOT/'include')]
        subprocess.run([gcc,*flags,'-c',str(ROOT/'src/loader/vc2_ce_vmu_gd_wait.S'),'-o',str(obj)],check=True)
        subprocess.run([gcc,*flags,'-nostdlib','-Wl,--build-id=none',
            '-Wl,--defsym,_kui_vc2_ce_vmu_gd_wait_admit='+hex(ADMIT),
            '-Wl,--defsym,_kui_vc2_ce_vmu_gd_wait_fault='+hex(FAULT),
            '-T'+str(linker),str(obj),'-o',str(elf)],check=True)
        subprocess.run([str(prefix/'sh-elf-objcopy'),'-O','binary','--only-section=.gate',
            str(elf),str(binary)],check=True)
        cls.code=binary.read_bytes()

    def cpu(self,**kwargs):return CPU(self.code,**kwargs).run()

    def test_non_request_keeps_legacy_busy_without_push_or_callback(self):
        for function in (1,2):
            cpu=self.cpu(function=function)
            self.assertEqual(cpu.stop,'legacy')
            self.assertEqual(cpu.r[15],FRAME)
            self.assertEqual(cpu.r[4:8],cpu.initial_args)
            self.assertFalse(cpu.writes or cpu.admit_calls or cpu.sleep_calls or cpu.polls)

    def test_drive_query_waits_for_storage_loan_without_fabricating_error(self):
        for sleeps in (1,2,8):
            cpu=self.cpu(function=4,sleeps=sleeps)
            self.assertEqual(cpu.stop,'retry')
            self.assertEqual(len(cpu.sleep_calls),sleeps)
            self.assertEqual(cpu.r[4:8],cpu.initial_args)
            self.assertEqual(cpu.r[15],FRAME)
            self.assertEqual((cpu.gbr,cpu.mach,cpu.macl),cpu.initial_special)
            self.assertEqual(cpu.admit_calls,[[cpu.caller_pr,cpu.incoming_sr,cpu.initial_args[0],0x403]])

    def test_all_source_control_calls_retry_with_original_arguments(self):
        controls=((0x1522,3),(0x174e,7),(0x1788,6),(0x1846,8),(0x1924,13),
                  (0x194a,11),(0x1978,12),(0x1bb0,6),(0x1be6,11),(0x1ee0,3),(0x23ce,4))
        for site,function in controls:
            for sleeps in (0,1,8):
                cpu=self.cpu(function=function,caller_pr=0x01cf0000|site,
                             sleeps=sleeps,state=2 if sleeps else 0)
                self.assertEqual(cpu.stop,'retry')
                self.assertEqual(len(cpu.sleep_calls),sleeps)
                self.assertEqual(cpu.r[4:8],cpu.initial_args)
                self.assertEqual(cpu.r[15],FRAME)
                self.assertEqual((cpu.gbr,cpu.mach,cpu.macl),cpu.initial_special)
                self.assertEqual(cpu.r[8:11],cpu.initial_saved[:3])

    def test_request_yields_on_existing_ce_frame_then_retries_raw_args(self):
        for sleeps in (1,2,8,31):
            for seed in range(5):
                cpu=self.cpu(sleeps=sleeps,seed=seed)
                self.assertEqual(cpu.stop,'retry')
                self.assertEqual(len(cpu.admit_calls),1)
                self.assertEqual(len(cpu.sleep_calls),sleeps)
                self.assertEqual(cpu.polls,sleeps+1)
                self.assertEqual(cpu.r[15],FRAME)
                self.assertEqual(cpu.r[4:8],cpu.initial_args)
                self.assertEqual((cpu.gbr,cpu.mach,cpu.macl),cpu.initial_special)
                self.assertEqual(cpu.r[8:11],cpu.initial_saved[:3])
                self.assertEqual(cpu.min_ce_sp,FRAME-48)
                self.assertEqual(cpu.sr&0xf0,0xf0)
                self.assertEqual(len(cpu.home_writes),sleeps+1)
                self.assertTrue(all(FRAME-48<=at<FRAME for at in cpu.writes))

    def test_already_released_flag_retries_without_sleep(self):
        cpu=self.cpu(state=0)
        self.assertEqual(cpu.stop,'retry')
        self.assertFalse(cpu.sleep_calls)
        self.assertEqual(cpu.r[4:8],cpu.initial_args)
        self.assertEqual((cpu.gbr,cpu.mach,cpu.macl),cpu.initial_special)

    def test_refused_admission_reports_exact_original_scalars(self):
        cpu=self.cpu(admitted=False)
        self.assertEqual(cpu.stop,'fault')
        self.assertEqual(cpu.fault_calls,[[1,0,cpu.caller_pr,cpu.incoming_sr]])
        self.assertFalse(cpu.sleep_calls or cpu.polls)

    def test_unexpected_flag_reports_state_and_never_retries(self):
        for state in (1,3,0xffffffff):
            cpu=self.cpu(state=state)
            self.assertEqual(cpu.stop,'fault')
            self.assertEqual(cpu.fault_calls,[[3,state,cpu.caller_pr,cpu.incoming_sr]])
            self.assertFalse(cpu.sleep_calls)
        cpu=self.cpu(sleeps=2,changed_state=1)
        self.assertEqual(cpu.stop,'fault')
        self.assertEqual(len(cpu.sleep_calls),2)
        self.assertEqual(cpu.fault_calls,[[3,1,cpu.caller_pr,cpu.incoming_sr]])

    def test_timeout_is_bounded_and_uses_masked_terminal_stack(self):
        cpu=self.cpu(sleeps=None)
        self.assertEqual(cpu.stop,'fault')
        self.assertEqual(len(cpu.sleep_calls),LIMIT-1)
        self.assertEqual(cpu.polls,LIMIT)
        self.assertEqual(cpu.fault_calls,[[2,LIMIT,cpu.caller_pr,cpu.incoming_sr]])
        self.assertEqual(cpu.r[15],PANIC_TOP-16)


if __name__=='__main__':unittest.main()
