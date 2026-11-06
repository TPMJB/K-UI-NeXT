#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked observational phase clock with read-only MMIO fixtures."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from test_native_standalone_foreground_gin import Gin

PMCR,FRQCR,HI,LO=0xff000084,0xffc00000,0xff100004,0xff100008
RTC_HI,RTC_LO,FIFO=0xa0710000,0xa0710004,0xa05f688c
G2=tuple(0xa05f7814+0x20*i+j for i in range(4) for j in (0,4))
WIDTHS={PMCR:2,FRQCR:2,HI:4,LO:4,RTC_HI:4,RTC_LO:4,FIFO:4,**{p:4 for p in G2}}
BEFORE,AFTER,OUT,STACK,CALLER=0x8cc00000,0x8cc00100,0x8cc00200,0x8c00d000,0x8c003000
CYCLES,NOMINAL,RTC=1,2,4
MASK48=(1<<48)-1


def require(value,message):
    if not value:raise ValueError(message)


class ClockCPU(Gin):
    def __init__(self,image,registers=None):
        self.strict=False;self.fast_only=False
        super().__init__(b'',b'',b'')
        self.fr=[0x53530000+i for i in range(16)];self.fpscr=0x40001
        for part in image[2]:
            if part[0]==1:self.store_block(part[2],image[0][part[1]:part[1]+part[4]])
        self.registers=registers or {};self.register_reads=Counter();self.hardware_reads=[]
        self.strict=True

    def read(self,address,count=4):
        if not getattr(self,'strict',False):return super().read(address,count)
        if 0x0c000000<=address&0x1fffffff<0x0d000000:return super().read(address,count)
        if self.fast_only:
            require(address in (PMCR,FRQCR,HI,LO),f'Fast clock read forbidden hardware {address:08x}/{count}')
        require(address in WIDTHS and WIDTHS[address]==count,
                f'Clock read unexpected hardware/width {address:08x}/{count}')
        self.register_reads[address]+=1;index=self.register_reads[address]-1
        self.hardware_reads.append((hex(address),count))
        value=self.registers.get(address)
        if value is None:
            value={PMCR:0xc023,FRQCR:0x0e0a,HI:0,RTC_HI:0x1234,RTC_LO:0x5678}.get(address,0)
            if address==LO:value=0x12345678+1000*index
        if callable(value):value=value(index)
        if isinstance(value,(tuple,list)):value=value[min(index,len(value)-1)]
        return value

    def write(self,address,value,count=4):
        if getattr(self,'strict',False):
            require(0x0c000000<=address&0x1fffffff<0x0d000000,
                    f'Observational clock wrote hardware {address:08x}/{count}')
        return super().write(address,value,count)

    def plain(self,op,pc):
        if self.fast_only:
            require(op&0xf0ff!=0x0002,f'Fast clock read SR at{pc:08x}')
        require(op&0xf00f not in (0x400e,0x4007),
                f'Clock changed IRQ/bank/control ownership at{pc:08x}')
        require(op&0xf0ff not in (0x0093,0x00a3,0x00b3),
                f'Clock changed cache ownership at{pc:08x}')
        if op==0x0008:self.instructions+=1;self.sr&=~1;return  # CLRT
        if op==0x0019:self.instructions+=1;self.sr&=~0x301;return  # DIV0U
        if op&0xf00f==0x300e:  # ADDC
            self.instructions+=1
            n,m=(op>>8)&15,(op>>4)&15;value=self.r[n]+self.r[m]+(self.sr&1)
            self.r[n]=value&0xffffffff;self.sr=(self.sr&~1)|int(value>0xffffffff);return
        if op&0xf00f==0x300a:  # SUBC
            self.instructions+=1
            n,m=(op>>8)&15,(op>>4)&15;value=self.r[n]-self.r[m]-(self.sr&1)
            self.r[n]=value&0xffffffff;self.sr=(self.sr&~1)|int(value<0);return
        if op&0xf00f==0x3005:  # DMULU.L
            self.instructions+=1
            n,m=(op>>8)&15,(op>>4)&15;value=self.r[n]*self.r[m]
            self.macl=value&0xffffffff;self.mach=value>>32;return
        if op&0xf0ff in (0x000a,0x001a):
            self.instructions+=1;self.r[(op>>8)&15]=self.macl if op&0x10 else self.mach;return
        if op&0xf00f==0x3004:  # DIV1: SH integer divider's Q/M/T step.
            self.instructions+=1;n,m=(op>>8)&15,(op>>4)&15
            old_q=(self.sr>>8)&1;mode=(self.sr>>9)&1;q=self.r[n]>>31
            value=((self.r[n]<<1)|(self.sr&1))&0xffffffff
            subtract=old_q==mode
            updated=(value-self.r[m] if subtract else value+self.r[m])&0xffffffff
            carry=int(updated>value if subtract else updated<value)
            # Q follows the old-Q/M quadrant, then T records Q==M.
            new_q=(carry if not q else 1-carry) if not mode else (1-carry if not q else carry)
            self.r[n]=updated;self.sr=(self.sr&~0x101)|(new_q<<8)|int(new_q==mode);return
        if op&0xf0ff==0x4024:  # ROTCL Rn.
            self.instructions+=1;n=(op>>8)&15;value=self.r[n]
            self.r[n]=((value<<1)|(self.sr&1))&0xffffffff;self.sr=(self.sr&~1)|(value>>31);return
        if op&0xf00f==0x200e:  # MULU.W Rm,Rn.
            self.instructions+=1;self.macl=(self.r[(op>>8)&15]&65535)*(self.r[(op>>4)&15]&65535);return
        if op&0xf00f==0x200d:  # XTRCT Rm,Rn.
            self.instructions+=1;n,m=(op>>8)&15,(op>>4)&15
            self.r[n]=(self.r[n]>>16)|(self.r[m]<<16&0xffffffff);return
        return super().plain(op,pc)


def invoke(cpu,symbols,name,args,sr=0x700000f0):
    cpu.fast_only=name.endswith('_read_fast')
    cpu.r=[(0x53530000+i*0x12345)&0xffffffff for i in range(16)]
    cpu.r[4:4+len(args)]=args;cpu.r[15]=STACK
    cpu.sr=sr;cpu.pc=symbols[name][0];cpu.pr=CALLER
    before=(cpu.r[8:15].copy(),cpu.gbr,cpu.vbr,cpu.fr.copy(),cpu.fpscr)
    cpu.writes=[];cpu.instructions=0
    for _ in range(200000):
        if cpu.pc==CALLER:break
        if name=='_kui_smash_phase_clock_read' and '_kui_smash_phase_clock_read_fast' in symbols:
            require(cpu.pc!=symbols['_kui_smash_phase_clock_read_fast'][0],
                    'Full phase clock nested the fast public API and added a low-stack frame')
        cpu.step()
    else:raise ValueError('Linked phase clock exceeded its finite instruction bound')
    require(cpu.r[8:15]==before[0] and cpu.r[15]==STACK and cpu.pr==CALLER and
            (cpu.gbr,cpu.vbr,cpu.fr,cpu.fpscr)==before[1:] and cpu.sr&~0x301==sr&~0x301,
            'Phase clock changed caller nonvolatile/IRQ/FPU state')
    require(len(cpu.hardware_reads)<256,'Clock polling is not bounded')
    require(all((OUT<=address<address+width<=OUT+32) or
                STACK-1024<=address<address+width<=STACK for _,address,_,width in cpu.writes),
            'Clock wrote beyond its result or caller frame')
    return cpu.instructions


def stamp(flags=7,cycles=0,seconds=100,pmcr=0xc023,frqcr=0x0e0a):
    return struct.pack('<8I',flags,pmcr,frqcr,cycles>>32,cycles&0xffffffff,seconds,0,0)


def audit(path,require_delta=True,require_fast=False):
    require(__debug__,'Run instruction models without Python optimization')
    image=read_elf(path);symbols=image[4];helpers={}
    for name in ('_kui_smash_phase_clock_read','_kui_smash_phase_clock_read_fast','_kui_smash_phase_clock_delta'):
        if name.endswith('_read_fast') and name not in symbols:
            require(not require_fast,'Fast clock audit requires the linked cycle-only helper')
            continue
        if name.endswith('_delta') and name not in symbols:
            require(not require_delta,'Full clock audit requires the linked delta helper')
            continue
        address,body,_=symbol_bytes(image,name,2)
        require(body,'Missing actual phase clock helper: '+name)
        helpers[name]=dict(address=hex(address),bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
    cases=[]
    def read_case(name,registers=None,sr=0x700000f0,fast=False):
        cpu=ClockCPU(image,registers);cpu.store_block(OUT-32,bytes([0xa7])*96)
        entry='_kui_smash_phase_clock_read_fast' if fast else '_kui_smash_phase_clock_read'
        instructions=invoke(cpu,symbols,entry,[OUT],sr)
        words=struct.unpack('<8I',cpu.block(OUT,32))
        require(cpu.block(OUT-32,32)==bytes([0xa7])*32 and cpu.block(OUT+32,32)==bytes([0xa7])*32,
                'Stamp output escaped its32-byte object')
        if fast:
            require(len(cpu.hardware_reads)<=30,'Fast clock exceeded two bounded four-attempt samples')
            require(not any(words[i] for i in (5,6,7)),'Fast clock published RTC/G2/reserved fields')
            require(not words[0]&(RTC|0x800|0x1000),'Fast clock published RTC validity/refusal flags')
        cases.append(dict(case=name,entry=entry,instructions=instructions,flags=words[0],hardware_reads=len(cpu.hardware_reads)))
        return words,cpu
    for sr in (0x40000000,0x500000f0,0x60000001,0x700000f1):
        words,_=read_case('stable_sr_'+hex(sr),sr=sr)
        expected=7 if sr&0xf0==0xf0 else 3
        require(words[0]&7==expected,'Cycle clock depends on IRQ delivery or RTC ignored IRQ ownership')
        if expected&RTC:require(words[5]==0x12345678,'Stable RTC lost its value')
    words,_=read_case('stopped_counter',{PMCR:0x23})
    require(not words[0]&CYCLES,'Stopped counter granted elapsed authority')
    words,_=read_case('wrong_counter_event',{PMCR:0xc024})
    require(not words[0]&CYCLES,'Wrong event was interpreted as elapsed cycles')
    words,_=read_case('counter_mode_changed',{PMCR:[0xc023,0xc024]})
    require(not words[0]&CYCLES,'Counter configuration change was ignored')
    words,_=read_case('counter_mode_changed_during_final_sample',{PMCR:[0xc023,0xc023,0xc024]})
    require(not words[0]&CYCLES,'Counter configuration was not checked after the final split read')
    words,_=read_case('frequency_changed_during_final_sample',{FRQCR:[0x0e0a,0x0e0a,0x0e09]})
    require(not words[0]&CYCLES,'Frequency was not checked after the final split read')
    words,_=read_case('unknown_frequency',{FRQCR:0x0e09})
    require(words[0]&CYCLES and not words[0]&NOMINAL,'Unknown frequency received nominal milliseconds')
    words,_=read_case('counter_stalled',{LO:0x12345678})
    require(not words[0]&CYCLES,'Stalled counter produced a valid zero elapsed sample')
    words,_=read_case('torn_counter_then_stable',{HI:[0,1,1,1,1,1]})
    require(words[0]&CYCLES,'Coherent split-counter retry failed')
    words,_=read_case('counter_never_coherent',{HI:lambda n:n})
    require(not words[0]&CYCLES,'Never-coherent counter received elapsed authority')
    for address in G2+(FIFO,):
        words,cpu=read_case('busy_g2_'+hex(address),{address:1})
        require(not words[0]&RTC and not cpu.register_reads[RTC_HI] and not cpu.register_reads[RTC_LO],
                'RTC read through a busy G2 path')
    for bit in (0x10,0x20):
        words,cpu=read_case('fifo_busy_'+hex(bit),{FIFO:bit})
        require(not words[0]&RTC and not cpu.register_reads[RTC_HI],'RTC ignored a live FIFO producer')
    for address in (G2[0],G2[-1],FIFO):
        words,cpu=read_case('g2_became_busy_'+hex(address),{address:[0,1]})
        require(not words[0]&RTC and cpu.register_reads[RTC_HI],
                'RTC remained valid after a producer became busy during the sample')
    words,_=read_case('rtc_rollover',{RTC_HI:[0x1234,0x1235,0x1235,0x1235],RTC_LO:[0xffff,0,0,0]})
    require(words[0]&RTC and words[5]==0x12350000,'RTC rollover was not sampled coherently')
    words,_=read_case('rtc_never_stable',{RTC_LO:lambda n:n})
    require(not words[0]&RTC,'Unstable RTC received seconds authority')
    if '_kui_smash_phase_clock_read_fast' in helpers:
        for sr in (0x40000000,0x500000f0,0x60000001,0x700000f1):
            words,cpu=read_case('fast_stable_sr_'+hex(sr),{**{p:1 for p in G2},FIFO:0x31},sr=sr,fast=True)
            require(words[0]==CYCLES|NOMINAL,'Fast cycle clock depends on IRQ/G2 state')
            require(set(cpu.register_reads)=={PMCR,FRQCR,HI,LO},'Fast sample missed its validated four-address contract')
        for name,registers,flags in (
            ('bad_mode',{PMCR:0xc024},0x100),
            ('mode_changed',{PMCR:[0xc023,0xc024]},0x100),
            ('late_mode_changed',{PMCR:[0xc023,0xc023,0xc024]},0x100),
            ('frequency_changed',{FRQCR:[0xe0a,0xe09]},0x100),
            ('late_frequency_changed',{FRQCR:[0xe0a,0xe0a,0xe09]},0x100),
            ('unknown_frequency',{FRQCR:0xe09},CYCLES),
            ('stalled',{LO:0x12345678},0x400),
            ('never_coherent',{HI:lambda n:n},0x200),
            ('second_sample_torn',{HI:lambda n:0 if n<2 else n},0x200),
            ('four_attempts_each',{HI:lambda n:n%8 if n%8<6 else 6},CYCLES|NOMINAL)):
            words,cpu=read_case('fast_'+name,registers,fast=True)
            require(words[0]==flags,'Fast sample changed established CPU validation: '+name)
            if name=='four_attempts_each':
                require(len(cpu.hardware_reads)==30 and cpu.register_reads[HI]==16 and cpu.register_reads[LO]==8,
                        'Fast split-counter retry bounds differ')
    def report(delta_cases):
        return dict(schema='kui.smash-phase.clock-linked.v1',status='PASS',elf_sha256=hashlib.sha256(image[0]).hexdigest(),
            helpers=helpers,read_cases=cases,delta_cases=delta_cases,actual_instruction_cases=len(cases)+len(delta_cases),
            scope='observer_and_delta' if delta_cases else 'observer_only_low_island',
            delta_helper_executed=bool(delta_cases),
            fast_helper_executed='_kui_smash_phase_clock_read_fast' in helpers,
            fast_allowed_mmio=[hex(p) for p in (PMCR,FRQCR,HI,LO)],
            hardware_writes=0,unexpected_hardware_reads=0,
            clock_ownership_changes=0,irq_or_cache_control_writes=0,irq_mask_independent=True,
            limits=[('Actual linked observer and delta instructions execute.' if delta_cases else
                     'Only the actual linked observer executes; the low island does not link the delta helper.'),
                    'MMIO values are authored fixtures and do not establish console oscillator accuracy or elapsed phase times.',
                    'The model preserves SR mode and banks symbolically; physical G2 arbitration, timer progression and RTC accuracy require console results.',
                    'A transient counter reconfiguration that returns to the same mode and a plausible count can remain undetected when no independent RTC sample is available.'])
    if '_kui_smash_phase_clock_delta' not in helpers:return report([])
    delta_cases=[]
    def delta_case(name,first,last,expected=None,flags=7):
        cpu=ClockCPU(image);cpu.store_block(BEFORE,first);cpu.store_block(AFTER,last)
        cpu.store_block(OUT-32,bytes([0xa7])*96)
        instructions=invoke(cpu,symbols,'_kui_smash_phase_clock_delta',[BEFORE,AFTER,OUT])
        value=struct.unpack('<Q4I',cpu.block(OUT,24))
        require(value[1]&7==flags,'Delta source-validity flags differ')
        if expected is not None:require(value[:4]==expected,'Linked clock conversion differs from integer oracle')
        require(not cpu.hardware_reads,'Delta arithmetic touched hardware')
        require(cpu.block(BEFORE,32)==first and cpu.block(AFTER,32)==last,'Delta changed input samples')
        require(cpu.block(OUT-32,32)==bytes([0xa7])*32 and cpu.block(OUT+24,40)==bytes([0xa7])*40,
                'Delta escaped its24-byte object')
        delta_cases.append(dict(case=name,instructions=instructions,cycles=value[0],flags=value[1],cpu_ms=value[2],rtc_seconds=value[3]))
        return value
    delta_case('twenty_seconds_masked',stamp(cycles=100),stamp(cycles=4000000100,seconds=120),
               (4000000000,7,20000,20))
    delta_case('one_minute_64bit_division',stamp(cycles=100),stamp(cycles=12000000100,seconds=160),
               (12000000000,7,60000,60))
    bad=delta_case('maximum48bit_interval_refused',stamp(cycles=0,seconds=0),
                  stamp(cycles=MASK48,seconds=1407374),flags=RTC)
    require(bad[0]==MASK48 and bad[1]&0x2000,'Implausible full-wrap interval was not retained/refused')
    delta_case('48bit_counter_wrap',stamp(cycles=MASK48-100),stamp(cycles=500,seconds=100),(601,7,0,0))
    delta_case('rtc_seconds_wrap',stamp(cycles=0,seconds=0xfffffffd),stamp(cycles=1200000000,seconds=3),
               (1200000000,7,6000,6))
    invalid=delta_case('invalid_cycle_sample',stamp(flags=RTC,cycles=0),stamp(cycles=100),flags=RTC)
    require(not invalid[1]&NOMINAL,'Unavailable clock became a valid0ms interval')
    delta_case('unsupported_frequency_is_cycles_only',stamp(flags=5,cycles=0,frqcr=0xe09),
               stamp(flags=5,cycles=1000000,frqcr=0xe09),(1000000,5,0,0),flags=5)
    delta_case('frequency_changed_between_samples',stamp(cycles=0),stamp(cycles=1000000,frqcr=0xe09),flags=RTC)
    delta_case('counter_changed_between_samples',stamp(cycles=0),stamp(cycles=1000000,pmcr=0xc024),flags=RTC)
    delta_case('high_counter_bits_invalid',stamp(cycles=0),stamp(cycles=1<<48),flags=RTC)
    delta_case('zero_cycles_not_elapsed_authority',stamp(cycles=100),stamp(cycles=100),flags=RTC)
    delta_case('one_hour_boundary',stamp(cycles=100),stamp(cycles=720000000100,seconds=3700),
               (720000000000,7,3600000,3600))
    bad=delta_case('over_one_hour_refused',stamp(cycles=100),stamp(cycles=720000000101,seconds=3700),flags=RTC)
    require(bad[1]&0x2000,'Over-one-hour interval was not flagged')
    bad=delta_case('counter_reset_same_mode_refused',stamp(cycles=6000000000),stamp(cycles=1000000,seconds=120),flags=RTC)
    require(bad[1]&0x2000,'Counter reset disguised as wrap was not flagged')
    bad=delta_case('twenty_seconds_vs_stalled_rtc',stamp(cycles=100),stamp(cycles=4000000100),flags=CYCLES|RTC)
    require(bad[1]&0x4000,'Independent clock disagreement retained nominal milliseconds')
    bad=delta_case('rtc_reset_same_cpu_mode',stamp(cycles=100),stamp(cycles=4000000100,seconds=0),flags=CYCLES|RTC)
    require(bad[1]&0x4000,'RTC reset was treated as a calibrated CPU interval')
    for seconds in (19,21):
        delta_case('rtc_quantization_'+str(seconds),stamp(cycles=100),stamp(cycles=4000000100,seconds=100+seconds),
                   (4000000000,7,20000,seconds))
    delta_case('rtc_unavailable_endpoint_limitation',stamp(flags=3,cycles=100),
               stamp(flags=3,cycles=4000000100),(4000000000,3,20000,0),flags=3)
    return report(delta_cases)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--elf',required=True,type=Path)
    parser.add_argument('--read-only-helper',action='store_true',help='Allow a low island that links only the read helper')
    parser.add_argument('--require-fast',action='store_true',help='Require and execute the four-address cycle-only helper')
    parser.add_argument('--report',type=Path);args=parser.parse_args();report=audit(args.elf,not args.read_only_helper,args.require_fast)
    text=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
