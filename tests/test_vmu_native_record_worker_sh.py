#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic metadata and strict delay-slot checks for the generic SDK replay."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'),str(ROOT/'tests')]
from profile_vmu_native_record_worker_sh import Record, RecordWorker, VIRTUAL_ADDRESS, MEMCARD, LCD
from test_vmu_native_heap_execution import SH
from sh4_delay_slots import SlotIllegalInstruction
from vmu_native_cache_seed import DETECTOR_EPOCH


def fixture(layout_version=1):
    """Authored synthetic instruction shapes, with no original image bytes."""
    base = 0x8c010000
    image = bytearray(0x1000)
    def half(pc, op):
        struct.pack_into('<H',image,pc-base,op)
    def word(pc, value):
        struct.pack_into('<I',image,pc-base,value)
    def literal(pc, reg, slot, value):
        half(pc,0xd000|reg<<8|(slot-((pc+4)&~3))//4)
        word(slot,value)
    hook = base+0x650
    entry = hook-0x112
    half(entry,0x2fe6); half(entry+14,0x4f22)
    wrapper = base+0x800
    prefix = {0:0x2fe6,2:0x2fd6,4:0x2fc6,6:0x2fb6,8:0x2fa6,10:0x2f96,
              12:0x2f86,14:0x4f22,16:0x7ff8,18:0x0002,22:0xe801,
              26:0x4009,30:0x4009,32:0xc90f,34:0x1f01,36:0x0002,
              38:0x2039,40:0xcbf0,42:0x400e,46:0x420b,48:9}
    for off,op in prefix.items():
        half(wrapper+off,op)
    mask = wrapper+0xd0
    half(wrapper+20,0x9300|(mask-(wrapper+24))//2); half(mask,0xff0f)
    gate,ready,consumer = base+0x900,base+0x920,base+0x940
    literal(wrapper+44,2,wrapper+0xc4,gate)
    complete = wrapper+74
    literal(complete-2,3,wrapper+0xc0,consumer)
    half(complete,0x430b); half(complete+2,9)
    caller = base+0xa00
    literal(caller,1,caller+0x80,entry)
    half(caller+2,0x410b); half(caller+4,9)
    digest = hashlib.sha256(image).digest()
    wire = bytearray(1024)
    wire[:8] = b'KUINVR01'
    struct.pack_into('<6I',wire,8,1,1024,DETECTOR_EPOCH,1,len(image),base)
    wire[32:64] = digest
    driver = [1,104,1,len(image),base,7,hook,hook+12,base+0x100,
              base+0xb00,base+0xc00,base+0xb10,wrapper+0xc0,complete,
              consumer,gate,ready,13]
    struct.pack_into('<18I',wire,64,*driver); wire[136:168] = digest
    layout = [0]*110
    layout[:8] = [layout_version,440,driver[8],base+0x200,base+0x300,0xac200000,0,0xac210000]
    layout[59] = 1
    struct.pack_into('<110I',wire,168,*layout)
    struct.pack_into('<5I',wire,680,base+0x400,base+0x404,base+0x408,base+0xd00,0x2000)
    struct.pack_into('<13I',wire,700,2,52,1,base,len(image),3,
                     base+0x100,base+0x200,base+0x300,0,base+0x400,base+0x500,base+0x600)
    wire[992:] = hashlib.sha256(wire[:992]).digest()
    return bytes(wire),bytes(image)


class MinimalCPU(RecordWorker):
    def __init__(self, words):
        SH.__init__(self,struct.pack('<%dH'%len(words),*words))
        self.pc = 0x8c010000
    def read(self,address,count=4):
        return SH.read(self,address,count)
    def plain(self,op,pc):
        return SH.plain(self,op,pc)


class RecordReplayTests(unittest.TestCase):
    def test_derives_actual_callers_and_wrapper(self):
        wire,image = fixture()
        record = Record(wire,image)
        self.assertEqual(record.submit_caller,record.load+0xa00)
        self.assertEqual(record.completion_wrapper,record.load+0x800)
        self.assertEqual(record.driver[13]+4,record.completion_wrapper+78)

    def test_accepts_infile_layout_schema(self):
        wire,image = fixture(layout_version=2)
        self.assertEqual(Record(wire,image).layout[0],2)

    def test_rejects_unsupported_layout_versions(self):
        for version in (0,3,0xffffffff):
            with self.subTest(version=version):
                wire,image = fixture(layout_version=version)
                with self.assertRaisesRegex(AssertionError,'Reviewed RX replay schema'):
                    Record(wire,image)

    def test_accepts_explicit_slot_layout_schema(self):
        wire,image = fixture()
        wire = bytearray(wire)
        struct.pack_into('<I',wire,168+6*4,1)
        struct.pack_into('<I',wire,168+59*4,12)
        wire[992:] = hashlib.sha256(wire[:992]).digest()
        self.assertEqual(Record(bytes(wire),image).layout[6],1)

    def test_derives_physical_alias_submit_caller(self):
        wire,image = fixture()
        image = bytearray(image)
        original = struct.unpack_from('<I',image,0xa80)[0]
        struct.pack_into('<I',image,0xa80,original&0x1fffffff)
        wire = bytearray(wire)
        wire[32:64] = wire[136:168] = hashlib.sha256(image).digest()
        wire[992:] = hashlib.sha256(wire[:992]).digest()
        self.assertEqual(Record(bytes(wire),bytes(image)).submit_entry,original&0x1fffffff)

    def test_virtual_slot_is_a2(self):
        self.assertEqual(VIRTUAL_ADDRESS,2)

    def test_rejects_resealed_old_epoch(self):
        wire,image = fixture()
        for epoch in range(1,DETECTOR_EPOCH):
            with self.subTest(epoch=epoch):
                changed = bytearray(wire)
                struct.pack_into('<I',changed,16,epoch)
                changed[992:] = hashlib.sha256(changed[:992]).digest()
                with self.assertRaisesRegex(AssertionError,'NVR schema'):
                    Record(bytes(changed),image)

    def test_fourth_terminal_entry_is_typed(self):
        wire,image = fixture()
        for family,count,fourth,accepted in ((6,4,0x8c010700,True),
                                             (6,3,0x8c010700,False),
                                             (6,4,0,False),
                                             (1,3,0x8c010700,False)):
            with self.subTest(family=family,count=count,fourth=fourth):
                changed = bytearray(wire)
                for offset,value in ((708,family),(720,count),(736,fourth)):
                    struct.pack_into('<I',changed,offset,value)
                changed[992:] = hashlib.sha256(changed[:992]).digest()
                if accepted:
                    record = Record(bytes(changed),image)
                    self.assertEqual(record.exits[6:10],
                                     (0x8c010100,0x8c010200,0x8c010300,fourth))
                else:
                    with self.assertRaisesRegex(AssertionError,'NVR exit schema'):
                        Record(bytes(changed),image)

    def test_physical_a1_long_payload_fixture(self):
        cpu = RecordWorker.__new__(RecordWorker)
        cpu.physical_a1_memory = {}
        cpu.physical_a1_write_phases = {}
        bitmap = bytes(range(192))
        ack = struct.pack('<I',0x00010007)
        self.assertEqual(cpu.physical_a1_response(0x3100010e,struct.pack('<I',LCD)+bitmap),ack)
        self.assertEqual(cpu.physical_a1_lcd,bitmap)
        other = bitmap[::-1]
        self.assertEqual(cpu.physical_a1_response(0x3200010c,struct.pack('<2I',LCD,0)+other),ack)
        self.assertEqual(cpu.physical_a1_lcd,other)
        phase = struct.pack('<2I',MEMCARD,0x2a000000)+bytes(range(128))
        self.assertEqual(cpu.physical_a1_response(0x2200010c,phase),ack)
        self.assertEqual(cpu.physical_a1_write_phases[phase[4:8]],phase[8:])

    def test_rejects_changed_image(self):
        wire,image = fixture()
        changed = bytearray(image); changed[-1] ^= 1
        with self.assertRaisesRegex(AssertionError,'SHA256'):
            Record(wire,bytes(changed))

    def test_rejects_record_digest_corruption(self):
        wire,image = fixture()
        changed = bytearray(wire); changed[80] ^= 1
        with self.assertRaisesRegex(AssertionError,'integrity'):
            Record(bytes(changed),image)

    def test_rejects_unrecognized_completion_wrapper(self):
        wire,image = fixture()
        changed = bytearray(image)
        struct.pack_into('<H',changed,0x82a,9)
        changed_wire = bytearray(wire)
        digest = hashlib.sha256(changed).digest()
        changed_wire[32:64] = changed_wire[136:168] = digest
        changed_wire[992:] = hashlib.sha256(changed_wire[:992]).digest()
        with self.assertRaisesRegex(AssertionError,'Completion wrapper shape'):
            Record(bytes(changed_wire),bytes(changed))

    def test_frame_tracks_executed_owner_sp_and_registers(self):
        cpu = RecordWorker.__new__(RecordWorker)
        SH.__init__(cpu,b'')
        cpu.r[15],cpu.pr,cpu.sr,cpu.macl = 0x8c00e000,0x8c011234,0x600000f1,0x12345678
        cpu.callback_contexts = []
        for kind,size in (('submit',76),('complete',80)):
            context = cpu.frame(kind)
            self.assertEqual(cpu.frame_address,cpu.r[15]-size)
            self.assertEqual(SH.read(cpu,cpu.frame_address+12),cpu.pr)
            self.assertEqual(SH.read(cpu,cpu.frame_address+68),cpu.sr)
            self.assertEqual(SH.read(cpu,cpu.frame_address+4),cpu.macl)
            if kind == 'complete':
                self.assertEqual(SH.read(cpu,cpu.frame_address+76),cpu.r[0])
            self.assertEqual(int(context['frame_address'],16),cpu.frame_address)

    def test_c_runner_rejects_pc_relative_in_any_delay_slot(self):
        # Includes conditional delayed branches when not taken.
        for branch in (0xa000,0xb000,0x400b,0x402b,0x000b,0x8d00,0x8f00):
            for literal in (0xd000,0x9000,0xc700):
                with self.subTest(branch=hex(branch),slot=hex(literal)):
                    cpu = MinimalCPU([branch,literal,9])
                    cpu.sr = 0x600000f0
                    with self.assertRaises(SlotIllegalInstruction):
                        cpu.step()
                    self.assertEqual(cpu.instructions,1)

    def test_c_runner_executes_legal_delay_slot(self):
        cpu = MinimalCPU([0xa001,0xe507,9,9])
        cpu.step()
        self.assertEqual(cpu.r[5],7)
        self.assertEqual(cpu.pc,0x8c010006)
        self.assertEqual(cpu.instructions,2)


if __name__ == '__main__':
    unittest.main()
