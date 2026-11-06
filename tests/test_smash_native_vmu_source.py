#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the supplied MDE's native BUP presence/descriptor admission.

The existing Gin scalar interpreter runs actual original SDK functions with
RAM records and production C INFO bytes. Hardware DMA, interrupt timing and
BUP filesystem jobs are outside this narrow test. No game bytes are shipped.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tests'), str(ROOT/'tools')]
from test_native_standalone_foreground_gin import Gin
from test_vmu_native_sdk_poll_execution import actual_vmu_info

OWNER = '04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
LOAD, MANAGER, STACK, STOP = 0x8c010000, 0x8c680000, 0x8cf00000, 0xdeadbeee
CELL, STATUS = 0x8c63e0a0, 0x8c5fc430
INFO = MANAGER+0x44c+120

def call(cpu, pc, args):
    cpu.r[15], cpu.pr, cpu.pc = STACK, STOP, pc
    cpu.r[4:4+len(args)] = args
    before = cpu.instructions
    for _ in range(10000):
        if cpu.pc == STOP:
            assert cpu.r[15] == STACK
            return cpu.r[0], cpu.instructions-before
        cpu.step()
    raise AssertionError('MDE BUP source exceeded bounded scalar replay')

def source_contract(app):
    word=lambda pc:struct.unpack_from('<H', app, pc-LOAD)[0]
    longword=lambda pc:struct.unpack_from('<I', app, pc-LOAD)[0]
    expected = {0x8c010312:0xd234, 0x8c010314:0x420b, 0x8c010316:0x0009,
        0x8c010318:0xde33, 0x8c01031a:0x63e2, 0x8c01031c:0x2338,
        0x8c01031e:0x8939, 0x8c010320:0xd332, 0x8c010322:0x430b,
        0x8c018114:0x430b, 0x8c018116:0xe400,
        0x8c0181e2:0x63b1, 0x8c0181f2:0x2338,
        0x8c0181fe:0x8d8d, 0x8c018200:0xff8d,
        0x8c018242:0x032d, 0x8c01824e:0x3303,
        0x8c1cde18:0x6320, 0x8c1cde20:0xc802,
        0x8c01bf12:0x2668, 0x8c01bf1c:0x2e11}
    for pc, op in expected.items():
        assert word(pc)==op, (hex(pc), hex(word(pc)), hex(op))
    def literal(pc):
        op=word(pc);assert op>>12==13
        return longword(((pc+4)&~3)+(op&255)*4)
    assert literal(0x8c010312)==0x8c018066
    assert literal(0x8c010318)==0x8c48f3e0
    assert literal(0x8c010320)==0x8c0180f0
    assert longword(0x8c0181bc)==0x8c15daf0
    assert longword(0x8c0181a8)==0x8c01bd0e
    displacement=word(0x8c0181fe)&255
    if displacement&128:displacement-=256
    assert 0x8c0181fe+4+2*displacement==0x8c01811c
    return {hex(pc):hex(op) for pc,op in expected.items()}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args=parser.parse_args();app=args.startup.read_bytes()
    assert len(app)==1848288 and hashlib.sha256(app).hexdigest()==OWNER
    guards=source_contract(app);packets=actual_vmu_info()
    cpu=Gin(b'', b'', b'');cpu.store_block(LOAD,app);cpu.write(CELL,MANAGER)
    history=[]
    for status in (0,1,2,255):
        for function in (0,1,2,4,6,14):
            cpu.write(STATUS+24,status,1)
            cpu.store_block(INFO,packets[1][4:])
            # The SDK's INFO parser normalizes only the function word to its
            # numeric native form. Descriptor bytes remain the protocol bytes.
            cpu.write(INFO,function)
            value,steps=call(cpu,0x8c1cddf8,[0])
            assert value==int(bool(status) and bool(function&2))
            history.append(dict(status=status,function=hex(function),present=value,instructions=steps))
    cpu.store_block(INFO,packets[1][4:]);cpu.write(INFO,int.from_bytes(packets[1][4:8],'big'))
    out=0x8c690000;cpu.store_block(out,bytes([0xa5])*32)
    result,steps=call(cpu,0x8c1bb178,[out,INFO])
    descriptor=struct.unpack('<5H',cpu.block(out,10))
    assert result==0 and descriptor==(1,512,4,1,0), descriptor
    assert cpu.block(out+10,22)==bytes([0xa5])*22
    # Execute the source's exact status load and condition test. The intervening
    # UI floating-point layout and branch delay FLDI0 are source-checked above;
    # no FP renderer is substituted into the scalar interpreter.
    branches=[]
    for status in (0,1,2,0xffff):
        cpu.write(0x8c576530,status,2);cpu.r[11]=0x8c576530
        cpu.pc=0x8c0181e2;cpu.step();cpu.pc=0x8c0181f2;cpu.step()
        no_card=bool(cpu.sr&1)
        assert no_card==(status==0)
        branches.append(dict(app_status=status,generic_insert_vmu_branch=no_card))
    assert not cpu.mmio_reads and not cpu.mmio_writes
    report=dict(format=1,owner_sha256=OWNER,scope=__doc__.strip(),source_guards=guards,
        checkpoint=dict(parser_call='0x8c010314',parser='0x8c018066',
        sram_bytes_cell='0x8c48f3e0',load='0x8c01031a',test='0x8c01031c',
        skip_if_zero='0x8c01031e',save_check_call='0x8c010322',save_check='0x8c0180f0',
        meaning='Nonzero SRAM size is observed only after ROM save-header parsing, immediately before the native RPG save-admission dialog'),
        presence=history,descriptor=dict(raw=packets[1][8:12].hex(),
        partitions=descriptor[0],block_bytes=descriptor[1],write_phases=descriptor[2],
        read_phases=descriptor[3],extra_crc=descriptor[4],instructions=steps),
        app_message=branches,limitations=['No hardware DMA or interrupt timing modeled',
        'Presence record input is supplied RAM state, not a claim that the production hook publishes it',
        'Native BUP MEDIA/root/FAT admission jobs and save/load persistence are not replayed'])
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS original MDE BUP presence:24 status/function cases; production VMU descriptor1x512,4writes,1read; generic insert-VMU message is zero-status gate')

if __name__=='__main__':main()
