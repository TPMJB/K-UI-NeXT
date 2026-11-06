#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked retirement gateway and bind its separate typed export.

The worker C body is an ABI adversary in this narrow assembly test. Actual
retirement guards and durable protocol execute in test_smash_native_return.py.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_vmu_native_stage_stack import Machine,require


def audit(path):
    require(__debug__,'Run linked gateway models without Python optimization')
    image=read_elf(path)
    entry,body,_=symbol_bytes(image,'_kui_smash_native_return_entry',2)
    target=image[4]['_kui_smash_native_return_retire'][0]
    preflight=image[4]['_kui_smash_native_return_preflight'][0]
    stack=image[4]['__vmu_native_worker_stack_top'][0]-0x20000000-16
    descriptor,wire,section=symbol_bytes(image,'_kui_smash_native_return_exports',1)
    _,base_exports,_=symbol_bytes(image,'_kui_vmu_native_worker_exports',1)
    require(len(base_exports)==80 and struct.unpack_from('<3I',base_exports)==(0x4b4e5031,4,80),
            'Separate retirement descriptor changed the ordinary 80-byte worker export ABI')
    require(section[2]&7==2 and descriptor%4==0 and
            wire==struct.pack('<4I',0x4b535254,1,16,entry),
            'Retirement descriptor does not name its exact linked gateway')
    expected=struct.pack('<18H3I',0x2f86,0x4f22,0x68f3,0xd007,0x400b,9,
        0x8801,0x8b03,0xdf05,0xd006,0x400b,9,0x6f83,0x4f26,0x68f6,0x000b,9,9,
        preflight,stack,target)
    require(body==expected,'Retirement gateway changed its complete switched-stack ABI')

    class Gateway(Machine):
        def __init__(self,seed,caller,ready,result):
            super().__init__(image,'_kui_smash_native_return_entry',seed,15,caller)
            self.sr=(0x100000f0,0x500000f0,0x700000f0)[seed%3]
            self.result=result
            self.ready=ready
            self.worker_words={at:(0xa5a50000^at) for at in range(stack-4080,stack+16,4)}
            self.mem.update(self.worker_words)
            self.saved=(self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy())
        def call(self,address):
            if address==self.stop_pc:return False
            if address==preflight:
                require(not self.calls and self.r[15]==self.start_sp-8,
                        'Pure preflight switched or overwrote the worker stack before checking busy')
                self.calls.append(address);self.abi_return(self.ready)
            else:
                require(address==target and self.calls==[preflight] and self.ready==1 and self.r[15]==stack,
                        'Full retirement calls a different body or uses the caller stack')
                self.calls.append(address)
                self.mem[stack-4]=0xc0decafe # Model a callee's private frame.
                self.abi_return(self.result)
            return True
        def check(self):
            self.run()
            registers,pr,sr,fpscr,fr=self.saved
            expected_calls=[preflight]+([target] if self.ready==1 else [])
            outcome=self.result if self.ready==1 else self.ready
            require(self.calls==expected_calls and self.r[0]==outcome&0xffffffff and
                    self.r[8:]==registers[8:] and self.pr==pr and self.sr&~1==sr&~1 and
                    self.fpscr==fpscr and self.fr==fr and not self.published,
                    'Retirement gateway changes caller nonvolatile, SR bank/mode, FPU or cache state')
            if self.ready!=1:
                require(all(self.mem.get(at)==value for at,value in self.worker_words.items()),
                        'Busy/invalid preflight touched existing worker stack frames')
    cases=0
    for seed in range(8):
        for caller in (0x8c00b7c0,0xac00b7c0):
            for ready in (-1,0,1):
                for result in ((-1,0,1) if ready==1 else (ready,)):
                    Gateway(seed,caller,ready,result).check();cases+=1
    return dict(schema='kui.smash-native.return-gateway.v1',status='PASS',
        elf_sha256=hashlib.sha256(image[0]).hexdigest(),descriptor=hex(descriptor),
        descriptor_bytes=16,gateway=hex(entry),preflight=hex(preflight),c_body=hex(target),worker_stack=hex(stack),
        cases=cases,base_worker_exports_unchanged=True,
        busy_preflight_leaves_private_stack_unchanged=True,
        limits='Actual linked gateway; C body is an ABI adversary. Production C guards and SD protocol are separate host tests; no hardware timing.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args();report=audit(args.elf);text=json.dumps(report,indent=2)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(text)
    print(text,end='')

if __name__=='__main__':main()
