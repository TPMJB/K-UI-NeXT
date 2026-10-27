#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay an NVR-resolved original SDK with the actual linked native worker C.

All game addresses come from a freshly verified NVR and its original image.
The supported replay shapes are the reviewed uniform and explicit-slot SDKs.
This is flat coherent RAM and synchronous Maple DMA, not cache, IRQ scheduling,
cycle timing, a game mainloop, FMV or storage-transport evidence. No original
executable bytes are embedded in this tool or its report.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from profile_vmu_native_sdk_worker_sh import WorkerSH, CFG, GUARD, make_handoff, diff
from test_vmu_native_heap_execution import SH, MASK, signed, verified_program_image
from test_vmu_native_sdk_poll_execution import PollSH
from sh4_delay_slots import validate_delay_slot
from check_vmu_native_worker_gateway import read_elf, symbol_bytes

OWNER_STACK = 0x8c00e000
RETURN = 0xdeadbeee
MDSTAR, MDST = 0xa05f6c04, 0xa05f6c18
VIRTUAL_ADDRESS = 2
MEMCARD, LCD, CLOCK = 0x02000000, 0x04000000, 0x08000000


def fresh_verify(startup, record):
    """Run production raw-source extent normalization and complete fresh proof."""
    return verified_program_image(startup,record)


class Record:
    def __init__(self, wire, image, source_image=None):
        assert len(wire) == 1024 and wire[:8] == b'KUINVR01', 'NVR format'
        header = struct.unpack_from('<4I',wire,8)
        assert header[:2] == (1,1024) and header[2] >= 1 and header[3] == 1, 'NVR schema'
        assert hashlib.sha256(wire[:992]).digest() == wire[992:], 'NVR integrity'
        assert not any(wire[748:992]), 'NVR reserved bytes'
        self.image_bytes, self.load = struct.unpack_from('<2I', wire, 24)
        assert self.load == 0x8c010000, 'Shared RAM loader requires conventional executable load'
        assert len(image) == self.image_bytes, 'Original image length'
        self.digest = hashlib.sha256(image).digest()
        assert self.digest == wire[32:64] == wire[136:168], 'Original image SHA256'
        self.wire, self.image = wire, image
        self.source_image = image if source_image is None else source_image
        assert self.source_image[:len(image)] == image
        self.source_digest = hashlib.sha256(self.source_image).digest()
        self.driver = struct.unpack_from('<18I', wire, 64)
        self.layout = struct.unpack_from('<110I', wire, 168)
        self.heap = struct.unpack_from('<18I', wire, 608)
        self.heap_call = struct.unpack_from('<5I', wire, 680)
        self.exits = struct.unpack_from('<12I', wire, 700)
        d, l = self.driver, self.layout
        assert d[:6] == (1, 104, 1, self.image_bytes, self.load, 7)
        assert (l[0] in (1,2) and l[1] == 440 and l[6] in (0,1) and
                l[59] == (1 if l[6] == 0 else 12) and
                (l[6] == 1 or l[2] == d[8])), 'Reviewed RX replay schema'
        assert self.heap_call[2] == self.heap_call[1] + 4
        self.submit_hook, self.initializer = d[6], l[2]
        self.gate, self.ready, self.consumer = d[15:17] + (d[14],)
        self.tx, self.rx = l[5] & 0x1fffffff, l[7] & 0x1fffffff
        self.rx_slots = tuple(value & 0x1fffffff if value else 0 for value in l[8:56])
        self.rx_ranges = tuple((address,address+1024) for address in self.rx_slots if address)
        # Exact family-relative reviewed submit prologue; no title addresses.
        self.submit_entry = self.submit_hook - 0x112
        assert self.half(self.submit_entry) == 0x2fe6
        assert self.half(self.submit_entry + 14) == 0x4f22
        # The recorded completion call has one exact wrapper mask/gate prefix.
        self.completion_wrapper = d[13] - 74
        prefix = {0:0x2fe6,2:0x2fd6,4:0x2fc6,6:0x2fb6,8:0x2fa6,
                  10:0x2f96,12:0x2f86,14:0x4f22,16:0x7ff8,
                  18:0x0002,20:None,22:0xe801,26:0x4009,30:0x4009,
                  32:0xc90f,34:0x1f01,36:0x0002,38:0x2039,
                  40:0xcbf0,42:0x400e,46:0x420b,48:0x0009}
        for offset, expected in prefix.items():
            if expected is not None:
                assert self.half(self.completion_wrapper + offset) == expected, ('Completion wrapper shape', offset)
        assert self.word_literal(self.completion_wrapper + 20, 3) == 0xff0f
        assert self.literal(self.completion_wrapper + 44, 2)[1] == self.gate
        assert self.half(d[13]) == 0x430b and self.half(d[13] + 2) == 9
        assert self.literal(d[13] - 2, 3) == (d[12], self.consumer)
        # Capture an actual immutable MOV.L/JSR/NOP caller fragment for submit.
        callers = []
        for offset in range(0, len(image) - 6, 2):
            pc, op = self.load + offset, struct.unpack_from('<H', image, offset)[0]
            if op >> 12 != 13:
                continue
            register = (op >> 8) & 15
            if self.half(pc + 2) != (0x400b | register << 8) or self.half(pc + 4) != 9:
                continue
            try:
                if self.literal(pc, register)[1] & 0x1fffffff == self.submit_entry & 0x1fffffff:
                    callers.append(pc)
            except AssertionError:
                pass
        assert callers, 'No actual original submit caller fragment'
        self.submit_caller, self.submit_callers = min(callers), callers
        caller_register = self.half(self.submit_caller) >> 8 & 15
        self.submit_entry = self.literal(self.submit_caller,caller_register)[1]

    def bytes(self, address, count):
        offset = (address & 0x1fffffff) - (self.load & 0x1fffffff)
        assert 0 <= offset <= len(self.image) - count, (hex(address), count)
        return self.image[offset:offset + count]

    def half(self, address):
        return int.from_bytes(self.bytes(address, 2), 'little')

    def literal(self, pc, register):
        op = self.half(pc)
        assert op & 0xff00 == 0xd000 | register << 8
        slot = ((pc + 4) & ~3) + (op & 255) * 4
        return slot, int.from_bytes(self.bytes(slot, 4), 'little')

    def word_literal(self, pc, register):
        op = self.half(pc)
        assert op & 0xff00 == 0x9000 | register << 8
        return int.from_bytes(self.bytes(pc + 4 + (op & 255) * 2, 2), 'little')


class RecordWorker(WorkerSH):
    def __init__(self, startup, build, handoff, record, physical_a1=False):
        self.record = record
        self.build = build
        self.build_inputs = {name:(build/name).read_bytes() for name in
                             ('native-worker.elf','native-worker.bin','native-worker.reloc')}
        worker = read_elf(build/'native-worker.elf')
        self.bottom_lease = worker[4].get('__kui_native_bottom_heap_worker') == (1,0,0,0xfff1)
        self.lease_bytes = 0x30000
        self.lease_geometry_source = 'historical non-dual 192 KiB tail fixture'
        if '__kui_native_dual_readers_worker' in worker[4]:
            assert worker[4]['__kui_native_dual_readers_worker'] == (1,0,0,0xfff1)
            stage = read_elf(build/'stage.elf')
            _, capability, _ = symbol_bytes(stage,'_kui_native_dual_reader_capability',1)
            assert len(capability) == 32
            descriptor = struct.unpack('<8I',capability)
            assert descriptor[:6] == (0x3152444b,0x4c415544,1,32,1,2) and descriptor[7] == 0
            self.lease_bytes = descriptor[6]
            assert self.lease_bytes >= 0x30000 and not self.lease_bytes & 31
            assert self.bottom_lease == (stage[4].get('__kui_native_bottom_heap_stage') == (1,0,0,0xfff1))
            self.build_inputs['stage.elf'] = (build/'stage.elf').read_bytes()
            self.lease_geometry_source = 'actual linked dual-reader stage capability and paired worker/stage bottom markers'
        begin,count = record.heap_call[3:5]
        assert self.lease_bytes <= count-64
        payload = begin+32 if self.bottom_lease else begin+count-self.lease_bytes
        worker_base = payload&0x1fffffff|0xa0000000
        super().__init__(startup, build, handoff, cached_stack=True,worker_base=worker_base)
        self.worker_physical_high = self.worker_physical_low+self.lease_bytes
        assert (self.symbols['__vmu_native_worker_end'][0]&0x1fffffff) <= self.worker_physical_high
        self.check_build_unchanged()
        assert self.block(record.load,len(record.source_image)) == record.source_image, 'Executed image must match verified bytes'
        self.original_image = record.image
        self.original_ranges = []
        self.patch_ranges = []  # Generic instruction masking below uses NVR load.
        self.callback_contexts = []
        self.submit_callbacks = self.complete_callbacks = 0
        self.physical_a1 = physical_a1
        self.physical_a1_packets = 0
        self.physical_a1_reply_ranges = []
        self.physical_a1_memory = {}
        self.physical_a1_write_phases = {}
        self.physical_a1_lcd = bytes(192)

    def rx_snapshot(self):
        """Logical SDK slot order, retaining holes in explicit-slot layouts."""
        return b''.join(self.block(address,1024) if address else bytes(1024)
                        for address in self.record.rx_slots)

    def check_build_unchanged(self):
        assert all((self.build/name).read_bytes() == data for name,data in self.build_inputs.items()), 'Worker build changed during replay'

    def step(self):
        pc = self.pc
        op = self.read(pc, 2)
        target, delay, n = None, False, op >> 8 & 15
        if op >> 12 in (0xa, 0xb):
            target, delay = pc + 4 + 2 * signed(op & 4095, 12) & MASK, True
            if op >> 12 == 0xb:
                self.pr = pc + 4
        elif op & 0xf0ff in (0x400b, 0x402b):
            target, delay = self.r[n], True
            if op & 0xf0ff == 0x400b:
                self.pr = pc + 4
        elif op == 0xb:
            target, delay = self.pr, True
        elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
            taken = bool(self.sr & 1) == (op >> 8 in (0x89, 0x8d))
            delay = op >> 8 in (0x8d, 0x8f)
            target = pc + 4 + 2 * signed(op & 255, 8) if taken else pc + (4 if delay else 2)
        if target is None:
            self.plain(op, pc)
            self.pc = pc + 2
        else:
            self.instructions += 1
            if delay:
                slot = self.read(pc + 2, 2)
                validate_delay_slot(slot, pc)
                self.plain(slot, pc + 2)
            self.pc = target & MASK

    def until(self, stop, limit=2000000):
        first = self.instructions
        for _ in range(limit):
            if self.pc == stop:
                return self.instructions - first
            if self.pc == self.symbols.get('_fail', (None,))[0]:
                detail = ''
                if '_state' in self.symbols:
                    state = self.symbols['_state'][0]
                    values = [SH.read(self,state+offset) for offset in (76,64,68,72)]
                    detail = '; runtime/fault_at/value/expect='+'/'.join(hex(v) for v in values)
                raise AssertionError(f'Actual worker refused with code {self.r[4]:#x}'+detail)
            self.step()
        raise AssertionError('Record replay exceeded instruction bound')

    def plain(self, op, pc):
        if pc & 0x1fffffff == self.record.submit_hook & 0x1fffffff and not getattr(self, 'inside_worker', False):
            self.bridge_submit()
        n,m = op >> 8 & 15,op >> 4 & 15
        if op & 0xf00f == 0x300e:
            value = self.r[n]+self.r[m]+(self.sr&1)
            self.r[n] = value&MASK; self.flag(value>MASK)
        elif op & 0xf00f == 0x300a:
            value = self.r[n]-self.r[m]-(self.sr&1)
            self.r[n] = value&MASK; self.flag(value<0)
        elif op & 0xf00f == 0x200e:
            self.macl = (self.r[n]&65535)*(self.r[m]&65535)
        elif op & 0xf00f == 0x200f:
            self.macl = signed(self.r[n]&65535,16)*signed(self.r[m]&65535,16)&MASK
        elif op & 0xf0ff == 0x00a3:
            self.flat_ocbp.append(dict(pc=hex(pc),address=hex(self.r[n]),coherent_memory_noop=True))
        else:
            return PollSH.plain(self,op,pc)
        self.executing = pc
        self.instructions += 1

    def read(self, address, count=4):
        if getattr(self,'active',False):
            if count == 2 and address in (self.pc,self.pc+2):
                self.instruction_fetch_reads += 1
            else:
                self.data_reads += 1
                if address&0xe0000000 == 0xa0000000 and self.worker_physical_low <= address&0x1fffffff < self.worker_physical_high:
                    self.private_p2_data_reads += 1
        if count == 2 and not getattr(self, 'inside_worker', False) and any(
                lo & 0x1fffffff <= address & 0x1fffffff and
                (address & 0x1fffffff)+count <= (hi & 0x1fffffff)
                for lo, hi in self.original_ranges):
            return int.from_bytes(self.record.bytes(address, count), 'little')
        return super().read(address, count)

    def write(self, address, value, count=4):
        if getattr(self,'active',False):
            self.data_writes += 1
        return super().write(address,value,count)

    def execute_worker(self, name, args):
        self.pc, self.pr = self.symbols[name][0], RETURN
        stack = self.symbols['__vmu_native_worker_stack_top'][0] - 32
        if name in ('_kui_vmu_native_passive_submit', '_kui_vmu_native_passive_complete',
                    '_kui_vmu_native_passive_split_submit'):
            stack = stack & 0x1fffffff | 0x80000000
        saved = self.r[8:15]
        self.r[15] = stack
        for i, value in enumerate(args):
            assert i < 4
            self.r[4+i] = value
        self.instructions = self.private_reads = self.private_writes = self.guest_reads = 0
        self.data_reads = self.data_writes = self.instruction_fetch_reads = self.private_p2_data_reads = 0
        self.active = True
        self.until(RETURN)
        assert self.r[15] == stack and self.r[8:15] == saved
        self.active = False
        return dict(result=self.r[0], instructions=self.instructions,
                    private_p2_reads=self.private_reads, private_p2_writes=self.private_writes,
                    guest_reads=self.guest_reads,data_reads=self.data_reads,data_writes=self.data_writes,
                    instruction_fetch_reads=self.instruction_fetch_reads,
                    private_p2_data_reads=self.private_p2_data_reads)

    def worker(self, name, args):
        from collections import Counter
        self.sr = 0x600000f0  # Actual gateway's private C mask contract.
        self.inside_worker = True
        self.access_reads = Counter(); self.access_writes = Counter()
        self.p1_stack_reads = self.p1_stack_writes = 0
        self.unknown_instruction_pcs = Counter(); self.function_instructions = Counter()
        self.function_guest_reads = Counter(); self.function_p2_reads = Counter(); self.function_p2_writes = Counter()
        try:
            result = self.execute_worker(name, args)
            result.update(read_objects=dict(self.access_reads), write_objects=dict(self.access_writes),
                          private_p1_stack_reads=self.p1_stack_reads, private_p1_stack_writes=self.p1_stack_writes)
            result.update(instructions_by_function=dict(self.function_instructions),
                          private_p2_reads_by_function=dict(self.function_p2_reads))
            assert not self.stubs, 'No worker helper stubs allowed'
            return result
        finally:
            self.active = False
            self.inside_worker = False

    def prepare(self):
        r = self.record
        hw = r.heap
        begin, count = r.heap_call[3:5]
        _, _, self.heap_initializer_instructions = SH.run(self, hw[6], [begin, count], OWNER_STACK)
        self.heap_calls = []
        if self.bottom_lease:
            request = count-self.lease_bytes-64
            placeholder,_,steps = SH.run(self,hw[9],[request],OWNER_STACK)
            assert placeholder == begin+self.lease_bytes+64
            self.heap_calls.append(dict(operation='upper_placeholder_allocate',request=request,result=placeholder,instructions=steps))
        payload,_,steps = SH.run(self,hw[9],[self.lease_bytes],OWNER_STACK)
        self.heap_calls.append(dict(operation='retained_allocate',request=self.lease_bytes,result=payload,instructions=steps))
        self.heap_allocate_instructions = sum(row['instructions'] for row in self.heap_calls)
        self.heap_release_instructions = 0
        if self.bottom_lease:
            _,_,steps = SH.run(self,hw[10],[placeholder],OWNER_STACK)
            self.heap_release_instructions = steps
            self.heap_calls.append(dict(operation='upper_placeholder_release',request=placeholder,instructions=steps))
            assert payload == begin+32
            assert self.read(hw[13]) == begin+self.lease_bytes+32
            assert self.read(begin+self.lease_bytes+36) == (count-self.lease_bytes-32)//32
        assert payload & 0x1fffffff == self.worker_physical_low, 'Original allocator selected placement'
        self.actual_heap_payload = payload
        units = self.lease_bytes//32+1
        lease = [1, 116, 2, 1, *hw, begin, count, self.lease_bytes, payload-32, payload,
                 units, count//32-units]
        runtime = [1,236,payload,self.lease_bytes,32,0x20000,0,0,0,0,0] + [0]*48
        wire = r.wire
        blob = (struct.pack('<3I',1,2300,r.layout[5]) + wire[64:168] +
                struct.pack('<29I',*lease) + wire[168:608] + struct.pack('<59I',*runtime) +
                bytes(56) + self.handoff + struct.pack('<3I',0x8c004100,0x8c004200,0x8c004300) +
                wire[700:748] + bytes(4))
        assert len(blob) == 2300
        self.store_block(CFG, blob)
        for name, value in [('_kui_vmu_native_passive_resume', r.driver[7]),
                            ('_kui_vmu_native_passive_consumer', r.consumer),
                            ('_kui_vmu_native_readonly_release_resume', hw[10]+20)]:
            self.write(self.symbols[name][0], value)
        bottom = self.symbols['__vmu_native_worker_stack_bottom'][0]
        for i in range(16):
            self.write(bottom+i*4, GUARD)
        self.arm_counts = self.worker('_kui_vmu_native_passive_arm', [CFG,0])
        assert self.arm_counts['result'] == 0, ('Worker arm refused',self.arm_counts['result'])
        exports = self.symbols['_kui_vmu_native_worker_exports'][0]
        hooks = [(r.submit_hook,self.read(exports+16)),(hw[10],self.read(exports+56)),
                 (hw[6],0x8c004200)] + [(a,0x8c004200) for a in r.exits[6:6+r.exits[5]]]
        for at, target in hooks:
            result = self.worker('_kui_vmu_native_patch_encode', [at,target,0x8c00b000])
            assert result['result'] == 1
            self.store_block(at,self.block(0x8c00b000,12))
            self.original_ranges.append((at,at+12))
        self.write(hw[7],0x8c004300)
        self.write(r.driver[12], self.read(exports+20))
        self.install_sdk_initializer()  # Existing descriptor checks are layout-derived.

    def initialize_sdk(self):
        r = self.record
        wrapper = r.layout[3]
        irq_call = wrapper+(36 if r.layout[6] == 0 else 40)
        after_initializer = wrapper+(52 if r.layout[6] == 0 else 56)
        assert r.half(irq_call) >> 12 == 0xb
        self.pc, self.pr, self.r[15] = wrapper, RETURN, OWNER_STACK
        self.sr = 0x60000060  # Explicit external entry input; wrapper masks it itself.
        if r.layout[6] == 0:
            self.r[4], self.r[5] = r.layout[86], r.layout[62]
        else:
            self.r[4:8] = [r.layout[61],r.layout[60],r.literal(r.layout[4]-4,7)[1],r.layout[59]]
        prefix = self.until(irq_call)
        # IRQ-registration scaffolding is outside this bounded model. Execute
        # its actual delay-slot lock normalization store without the call.
        slot = self.read(irq_call+2,2)
        validate_delay_slot(slot,irq_call)
        self.plain(slot,irq_call+2)
        self.pc = irq_call+4
        continuation = self.until(after_initializer)
        self.owner_sr = self.sr
        state = self.symbols['_kui_vmu_native_sdk_coherence_state'][0]
        assert self.read(state+44) == 1
        lock_p1 = (r.driver[11]&0x1fffffff)|0x80000000
        assert [int(event['address'],16) for event in self.flat_ocbp] == [lock_p1,lock_p1+4]
        assert all(int(event['pc'],16)&0xe0000000 == 0xa0000000 for event in self.flat_ocbp)
        self.coherence_installation.update(actual_gate_executed=True,successful_handoffs=1)
        assert self.read(r.driver[9]) == r.driver[10]
        assert self.read(r.driver[11]) == (r.driver[11]+4 | 0xa0000000)
        assert self.read(r.driver[10]+0xfc4) == r.layout[5]
        assert self.read(r.driver[10]+0xfc8) == r.layout[7]
        return dict(instructions=prefix+1+continuation, wrapper=hex(wrapper),initial_wrapper_sr='0x60000060',
                    inherited_callback_sr=hex(self.owner_sr), successful_cold_handoffs=1)

    def frame(self, kind):
        vals = [0,self.macl,0,self.pr,*self.r[14:1:-1],self.sr,self.r[1]]
        assert len(vals) == 19
        # Mirror actual assembly saves beneath the executed owner's SP. The
        # completion gateway also saves R0 after the shared 76-byte frame.
        complete = kind != 'submit'
        self.frame_address = (self.r[15]-(80 if complete else 76))&MASK
        if complete:
            vals.append(self.r[0])
        self.store_block(self.frame_address,struct.pack('<%dI'%len(vals),*vals))
        context = dict(kind=kind,pr=hex(self.pr),sr=hex(self.sr),frame_address=hex(self.frame_address))
        self.callback_contexts.append(context)
        return context

    def bridge_submit(self):
        before = self.r.copy(),self.pr,self.sr,self.pc,self.macl,self.instructions
        self.frame('submit')
        self.submit_counts = self.worker('_kui_vmu_native_passive_submit',[self.frame_address])
        self.submit_callbacks += 1
        self.r,self.pr,self.sr,self.pc,self.macl,self.instructions = before

    def submit(self):
        r = self.record
        before = self.submit_callbacks
        self.submit_counts = None
        self.pc,self.pr,self.r[15],self.sr = r.submit_caller,RETURN,OWNER_STACK,self.owner_sr
        self.until(r.submit_entry)
        assert self.pr == r.submit_caller+6
        steps = self.until(r.submit_caller+6)
        assert self.r[0] == 0
        assert self.submit_counts is not None and self.submit_callbacks == before+1
        return steps

    def complete(self):
        r = self.record
        self.pc,self.pr,self.r[15],self.sr = r.completion_wrapper,RETURN,OWNER_STACK,self.owner_sr
        hook = self.read(r.driver[12])
        wrapper_instructions = self.until(hook)
        assert self.pr == r.driver[13]+4
        self.frame('complete')
        before = self.r.copy(),self.pr,self.sr,self.pc,self.macl,self.instructions
        result = self.worker('_kui_vmu_native_passive_complete',[self.frame_address])
        self.r,self.pr,self.sr,self.pc,self.macl,self.instructions = before
        self.complete_callbacks += 1
        return wrapper_instructions, result

    def original_call(self,address,args=()):
        result,_,steps = SH.run(self,address,args,OWNER_STACK)
        assert result == 0,(hex(address),hex(result))
        return steps

    def dma(self):
        source = self.mmio[MDSTAR]
        assert 0x0c000000 <= source < 0x0d000000
        # The actual full C path may submit its private filtered physical chain.
        at,frames = source,[]
        self.physical_a1_reply_ranges = []
        for _ in range(256):
            control = self.read(at); at += 4
            if control == 0x700:
                continue
            rx,header = self.read(at),self.read(at+4)
            words = control & 255
            payload = self.block(at+8,4*words)
            at += 8 + 4*words
            dest,command = header >> 8 & 255,header & 255
            port = dest >> 6
            assert dest & 63 == 32 or (self.physical_a1 and dest == 1), hex(header)
            assert dest != VIRTUAL_ADDRESS, 'Virtual A2 must never reach physical Maple DMA'
            assert at <= source+0x3000
            response = MASK
            if dest == 1:
                assert command in (1,2,3,9,10,11,12,13,14),hex(header)
                self.physical_a1_packets += 1
                packet = self.physical_a1_response(header,payload)
                self.store_block(rx,packet)
                response = int.from_bytes(packet[:4],'little')
                self.physical_a1_reply_ranges.append((rx,self.block(rx,1024)))
            elif self.controllers & (1 << port):
                assert command in (1,2,9)
                size = 28 if command == 1 else 48 if command == 2 else 3
                response = size << 24 | dest << 16 | port << 14 | (5 if command == 1 else 6 if command == 2 else 8)
                if self.physical_a1 and port == 0:
                    response |= 1 << 16
                self.store_block(rx,bytes((size+1)*4)); self.write(rx+4,0x01000000)
                if command == 9:
                    self.write(rx+8,MASK); self.write(rx+12,0x80808080)
            self.write(rx,response)
            frames.append(dict(control=hex(control),rx=hex(rx),header=hex(header),
                               payload_bytes=len(payload),payload_sha256=hashlib.sha256(payload).hexdigest(),
                               model_response=hex(response)))
            if control & 0x80000000:
                break
        else:
            raise AssertionError('No LAST within DMA bound')
        self.mmio[MDST] = 0
        self.batch = dict(source=hex(source),frames=frames)

    def physical_a1_response(self,header,payload):
        """Authored physical VMU wire fixture, independent of virtual backend."""
        command = header & 255
        if command in (1,2):
            assert not payload
            words = 28 if command == 1 else 48
            body = struct.pack('<I',MEMCARD|LCD|CLOCK)+bytes(4*words-4)
            return struct.pack('<I',(words<<24)|(1<<16)|(5 if command == 1 else 6))+body
        if command == 3:
            assert not payload
            return struct.pack('<I',(1<<16)|7)
        assert len(payload) >= 4
        function = int.from_bytes(payload[:4],'little')
        if command == 9:
            assert len(payload) == 4 and function in (MEMCARD,LCD,CLOCK)
            body = payload+bytes(8)
            return struct.pack('<I',(3<<24)|(1<<16)|8)+body
        if command == 14:
            assert (function == LCD and len(payload) == 196) or (function == CLOCK and len(payload) == 8)
            if function == LCD:
                self.physical_a1_lcd = payload[4:]
            return struct.pack('<I',(1<<16)|7)
        if command == 12 and function == LCD:
            assert len(payload) == 200 and payload[4:8] == bytes(4)
            self.physical_a1_lcd = payload[8:]
            return struct.pack('<I',(1<<16)|7)
        assert function == MEMCARD and len(payload) == (136 if command == 12 else 8)
        if command == 10:
            assert payload[4:] == bytes(4)
            body = payload[:4]+bytes(24)
            return struct.pack('<I',(7<<24)|(1<<16)|8)+body
        if command == 11:
            body = payload+self.physical_a1_memory.get(payload[4:],bytes(512))
            return struct.pack('<I',(130<<24)|(1<<16)|8)+body
        if command == 12:
            # The fixture records each exact physical phase; it does not model
            # physical flash timing or promise physical write durability.
            self.physical_a1_write_phases[payload[4:8]] = payload[8:]
        return struct.pack('<I',(1<<16)|7)


def negative_cases(cpu):
    cpu.submit()
    # Execute actual wrapper/gate to its callback entry, then snapshot its
    # genuine saved PR/SR before altering isolated completion contexts.
    r = cpu.record
    cpu.pc,cpu.pr,cpu.r[15],cpu.sr = r.completion_wrapper,RETURN,OWNER_STACK,cpu.owner_sr
    cpu.until(cpu.read(r.driver[12])); cpu.frame('negative_baseline')
    state = cpu.symbols['_state'][0]
    source = cpu.read(state+20)
    cell,manager = r.driver[9],cpu.read(r.driver[9])
    pair = cpu.symbols['_controller_pair'][0]
    stack = cpu.symbols['__vmu_native_worker_stack_bottom'][0]
    rx = cpu.read(pair+156)
    cases = [
        ('manager_unsupported_area',lambda c:c.write(cell,manager&0x1fffffff|0xe0000000)),
        ('manager_unaligned',lambda c:c.write(cell,manager+1)),
        ('manager_wrong_identity',lambda c:c.write(cell,manager+32)),
        ('source_rx_unaligned',lambda c:c.write(source+4,c.read(source+4)+1)),
        ('source_rx_outside_ram',lambda c:c.write(source+4,0x0d000000)),
        ('private_rx_unaligned',lambda c:c.write(pair+156,rx+1)),
        ('private_rx_outside_ram',lambda c:c.write(pair+156,0x0d000000)),
        ('private_rx_unsupported_area',lambda c:c.write(pair+156,0xec000000)),
        ('private_rx_function_past_ram',lambda c:(c.write(pair+156,0x0cfffffc),c.write(0x0cfffffc,0x03200008))),
        ('physical_response_header',lambda c:c.write(rx,c.read(rx)^1)),
        ('physical_a2_attached',lambda c:c.write(rx,c.read(rx)|(VIRTUAL_ADDRESS<<16))),
        ('live_bank_selector',lambda c:c.write(manager+0x10b4,c.read(manager+0x10b4)^1)),
        ('stack_guard',lambda c:c.write(stack,0))]
    results = []
    for name,mutate in cases:
        trial = copy.copy(cpu)
        trial.memory = bytearray(cpu.memory); trial.mmio = dict(cpu.mmio); trial.r = cpu.r.copy()
        trial.writes = []; trial.mmio_reads = []; trial.mmio_writes = []
        mutate(trial); trial.writes = []
        tx,reply = trial.block(r.tx,0x6000),trial.rx_snapshot()
        try:
            trial.worker('_kui_vmu_native_passive_complete',[trial.frame_address])
        except AssertionError as error:
            assert str(error).startswith('Actual worker refused with code '),(name,str(error))
            assert trial.block(r.tx,0x6000) == tx and trial.rx_snapshot() == reply,name
            writes = [(pc,a,n) for pc,a,_,n in trial.writes if
                      r.tx <= a&0x1fffffff < r.tx+0x6000 or any(
                          lo <= a&0x1fffffff < hi for lo,hi in r.rx_ranges)]
            assert not writes,(name,writes)
            results.append(dict(case=name,refusal=str(error),tx_writes=0,parent_or_reply_writes=0))
        else:
            raise AssertionError('Actual linked worker accepted '+name)
    return results


def cold_target_cases(startup,build,handoff,record):
    """Mutate only the proved initializer target before actual cold arm."""
    pc = record.layout[3]+(46 if record.layout[6] else 42)
    slot,original = record.literal(pc,3)
    results = []
    for name,target in [('initializer_target_unsupported_area',(original&0x1fffffff)|0x40000000),
                        ('initializer_target_wrong_physical',original+2)]:
        class MutatedTarget(RecordWorker):
            def worker(self,name,args):
                if name == '_kui_vmu_native_passive_arm':
                    self.write(slot,target)
                    self.before_arm = self.block(record.load,len(record.image))
                return super().worker(name,args)
        trial = MutatedTarget(startup,build,handoff,record)
        try:
            trial.prepare()
        except AssertionError as error:
            assert error.args == (('Worker arm refused',0xfffffffd),),(name,error.args)
            assert trial.block(record.load,len(record.image)) == trial.before_arm,name
            results.append(dict(case=name,result='0xfffffffd',executable_writes_after_mutation=0))
        else:
            raise AssertionError('Actual linked worker accepted '+name)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup',type=Path,required=True)
    parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--rounds',type=int,default=64)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--negative-cases',action='store_true')
    parser.add_argument('--physical-a1',action='store_true',
                        help='Attach an authored physical A1 memory/LCD/clock VMU wire fixture')
    args = parser.parse_args()
    if not 6 <= args.rounds <= 1000:
        parser.error('--rounds must be between 6 and 1000')
    if not __debug__:
        parser.error('Python assertions must be enabled')
    source,image,extent = fresh_verify(args.startup,args.record)
    record = Record(args.record.read_bytes(),image,source)
    assert args.startup.read_bytes() == record.source_image and args.record.read_bytes() == record.wire
    handoff = make_handoff()
    cpu = RecordWorker(args.startup,args.build,handoff,record,args.physical_a1)
    cpu.prepare()
    initializer = cpu.initialize_sdk()
    history = []
    for i in range(args.rounds):
        submit = cpu.submit()
        pending = cpu.read(cpu.symbols['_controller_pair'][0])
        live_ready = cpu.read(cpu.symbols['_live_plan'][0]+4)
        tx = cpu.block(record.tx,0x6000); rx = cpu.rx_snapshot()
        wrapper,complete = cpu.complete()
        tx_after = cpu.block(record.tx,0x6000); rx_after = cpu.rx_snapshot()
        for address,packet in cpu.physical_a1_reply_ranges:
            assert cpu.block(address,len(packet)) == packet, 'Physical A1 reply changed by virtual A2 publication'
        consumer = cpu.original_call(record.consumer)
        consumer_rx = cpu.rx_snapshot()
        ready = cpu.original_call(record.ready)
        assert cpu.read(record.driver[11]+4,1) == 0
        state = cpu.symbols['_state'][0]
        assert cpu.read(state+8) == cpu.read(state+12) == i+1
        assert cpu.submit_callbacks == cpu.complete_callbacks == i+1
        changes = diff(rx,rx_after)
        assert tx == tx_after and consumer_rx == rx_after
        if i >= 2:
            assert pending == 1 and live_ready == 0x4e4c5031,(i,pending,hex(live_ready))
            before = 0x21 if args.physical_a1 else 0x20
            assert changes == [dict(offset=hex((i%2)*0x6000+2),before=hex(before),after=hex(before|VIRTUAL_ADDRESS))],(i,changes)
        history.append(dict(round=i,original_submit_instructions=submit,completion_wrapper_instructions=wrapper,
                            submit=cpu.submit_counts,complete=complete,consumer_instructions=consumer,
                            ready_instructions=ready,controller_pair_pending=pending,live_ready=hex(live_ready),
                            physical_batch=cpu.batch,complete_rx_differences=changes,
                            complete_tx_differences=[],consumer_rx_differences=[]))
    negatives = negative_cases(cpu) if args.negative_cases else []
    cold_negatives = cold_target_cases(args.startup,args.build,handoff,record) if args.negative_cases else []
    state = cpu.symbols['_state'][0]
    cpu.check_build_unchanged()
    report = dict(schema='kui.native.record-linked-worker.v1',executable_sha256=record.digest.hex(),
                  executable_bytes=record.image_bytes,source_bytes=len(record.source_image),
                  source_sha256=record.source_digest.hex(),program_bytes=record.image_bytes,
                  discarded_suffix_bytes=len(record.source_image)-record.image_bytes,
                  program_extent_production_verified=True,program_extent=extent,nvr_record_digest=record.wire[992:].hex(),
                  nvr_sha256=hashlib.sha256(record.wire).hexdigest(),
                  worker_elf_sha256=hashlib.sha256(cpu.build_inputs['native-worker.elf']).hexdigest(),
                  worker_binary_sha256=hashlib.sha256(cpu.build_inputs['native-worker.bin']).hexdigest(),
                  worker_relocation=dict(actual_heap_payload=hex(cpu.actual_heap_payload),p2_base=hex(cpu.worker_base),
                    lease_bytes=cpu.lease_bytes,placement='bottom' if cpu.bottom_lease else 'tail',
                    geometry_source=cpu.lease_geometry_source,heap_calls=cpu.heap_calls,
                    stage_elf_sha256=hashlib.sha256(cpu.build_inputs['stage.elf']).hexdigest() if 'stage.elf' in cpu.build_inputs else None),
                  rx_snapshot_encoding='48 SDK slots in bank order; each slot 1024 bytes; absent slots zero-filled',
                  sdk_rx_slots=[hex(address) if address else None for address in record.rx_slots],
                  handoff_sha256=hashlib.sha256(handoff).hexdigest(),
                  original_bytes_embedded=False,fresh_production_record_verification=True,worker_helper_stubs=False,
                  virtual_transport=dict(address=VIRTUAL_ADDRESS,slot='A2',parent_attach_bit=VIRTUAL_ADDRESS,
                                         filtered_from_hardware=True),
                  physical_a1_packets=cpu.physical_a1_packets,physical_a1_replies_preserved=True,
                  hardware_inputs=dict(asic_revision=2,physical_controller_mask=1,
                    physical_accessories=['A1 memory/LCD/clock VMU fixture'] if args.physical_a1 else [],maple_dma='synchronous'),
                  sh4_slot_legality_enforced=True,heap_initializer_instructions=cpu.heap_initializer_instructions,
                  heap_allocate_instructions=cpu.heap_allocate_instructions,heap_release_instructions=cpu.heap_release_instructions,arm=cpu.arm_counts,
                  sdk_initializer=initializer,cold_coherence=cpu.coherence_installation,flat_ocbp=cpu.flat_ocbp,
                  original_entrypoints={k:hex(v) for k,v in dict(initializer=record.initializer,
                    submit_entry=record.submit_entry,submit_hook=record.submit_hook,submit_caller=record.submit_caller,
                    completion_wrapper=record.completion_wrapper,completion_call=record.driver[13],
                    gate=record.gate,consumer=record.consumer,ready=record.ready).items()},
                  callback_contexts=cpu.callback_contexts,rounds=history,negative_cases=negatives,
                  cold_target_negative_cases=cold_negatives,
                  validated_callback_pairs=len(history),submit_caller_fragments=[hex(a) for a in record.submit_callers],
                  limitations=[
                    'Flat coherent RAM; no operand/instruction cache, bus latency, IRQ scheduling, video, FMV, game mainloop, SD transport, or cycle timing.',
                    'Actual linked C callbacks execute without worker helper stubs. Assembly gateway save/restore is bridged with executed PR/SR/integer registers and tracked MACL; unused GBR/MACH frame fields are zero placeholders.',
                    'Actual original initializer-wrapper prefix normalizes lock and masks SR; its IRQ-registration BSR scaffolding is omitted while its actual delay slot executes.',
                    'Submission executes the lowest-address immutable MOV.L/JSR/NOP caller fragment and inherits SR from the executed initializer wrapper. Fragment discovery is not a surrounding game/IRQ dispatcher reachability proof.',
                    'Completion wrapper prefix executes through actual gate and actual callback JSR. Its captured PR/SR are used in the callback frame; consumer and ready are called separately afterward.',
                    'Cold target installation validates the actual descriptor/slot then writes one target word; production stage installer does not execute in this flat model. OCBP is recorded as coherent-memory no-op.',
                    'Negative cases stop at the actual nonreturning _fail entry before display/reporting hardware; the failure entry is never substituted with a successful return.',
                    'Valid blank-card durable handoff is generated by the existing host fixture; automatic INFO enumeration makes no storage request.',
                    'Optional physical A1 is an independently authored protocol fixture. Its wire payloads and physical device effects are modeled, not real VMU firmware or flash timing.'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(output=str(args.output),pairs=len(history),negative_cases=len(negatives),
                          last_pair_instructions=history[-1]['submit']['instructions']+history[-1]['complete']['instructions'],
                          callback_contexts=cpu.callback_contexts[:2])))


if __name__ == '__main__':
    main()
