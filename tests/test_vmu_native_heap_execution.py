#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute owner-supplied original heap code at its actual allocation placement.

No retail instructions or executable bytes are embedded in this test. Supply
the exact external startups and a built stage/worker, for example:

  python3 tests/test_vmu_native_heap_execution.py --build build/native-compat \
    --ps2 /path/to/PS2/1ST_READ.BIN --skies /path/to/Skies/1ST_READ.BIN \
    --sa2 /path/to/SA2/1ST_READ.BIN

Equivalent environment variables are KUI_NATIVE_HEAP_BUILD,
KUI_NATIVE_PS2_STARTUP, KUI_NATIVE_SKIES_STARTUP and KUI_NATIVE_SA2_STARTUP.
The independent, bounded SH integer model follows the actual initializer,
dispatch helper, bootstrap release and first tail allocation. It then models
the worker's complete copy/zero write span, checking stage and installer-stack
canaries. Unsupported instructions fail rather than being approximated.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
MASK = 0xFFFFFFFF
RAM_BEGIN, RAM_END = 0x0C000000, 0x0D000000
WORKER_SPAN, LEASE_BYTES = 0x20000, 0x30000
OLD_INSTALLER_SP = 0x8CFF0000 - 32
OPTIONS = argparse.Namespace(build=os.environ.get("KUI_NATIVE_HEAP_BUILD"),
    **{game: os.environ.get(f"KUI_NATIVE_{game.upper()}_STARTUP") for game in ("ps2", "skies", "sa2")})


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def physical(address):
    return address & 0x1FFFFFFF


def intersects(first, count, other, other_count):
    return physical(first) < physical(other) + other_count and physical(other) < physical(first) + count


class SH:
    """One flat main-RAM model with physical aliasing and explicit SR/PR."""

    def __init__(self, image):
        self.memory = bytearray(RAM_END - RAM_BEGIN)
        self.memory[0x10000:0x10000 + len(image)] = image
        self.r = [(0x11335577 + 0x01030507 * i) & MASK for i in range(16)]
        self.pc = self.pr = self.executing = 0
        self.sr = 0x600000F0
        self.writes = []
        self.instructions = 0

    @staticmethod
    def offset(address, count):
        at = physical(address)
        assert address & 0xE0000000 in (0, 0x80000000, 0xA0000000)
        assert RAM_BEGIN <= at <= RAM_END - count, (hex(address), count)
        return at - RAM_BEGIN

    def read(self, address, count=4):
        at = self.offset(address, count)
        return int.from_bytes(self.memory[at:at + count], "little")

    def write(self, address, value, count=4):
        value &= (1 << (count * 8)) - 1
        self.writes.append((self.executing, address, value, count))
        at = self.offset(address, count)
        self.memory[at:at + count] = value.to_bytes(count, "little")

    def block(self, address, count):
        at = self.offset(address, count)
        return bytes(self.memory[at:at + count])

    def store_block(self, address, data):
        at = self.offset(address, len(data))
        self.memory[at:at + len(data)] = data

    def flag(self, value):
        self.sr = (self.sr & ~1) | int(bool(value))

    def plain(self, op, pc):
        self.executing = pc
        self.instructions += 1
        n, m = (op >> 8) & 15, (op >> 4) & 15
        if op == 0x0009:  # nop
            pass
        elif op >> 12 == 0xE:
            self.r[n] = signed(op & 255, 8) & MASK
        elif op >> 12 == 0x7:
            self.r[n] = (self.r[n] + signed(op & 255, 8)) & MASK
        elif op >> 12 == 0xD:
            self.r[n] = self.read(((pc + 4) & ~3) + (op & 255) * 4)
        elif op >> 12 == 0x9:
            self.r[n] = signed(self.read(pc + 4 + (op & 255) * 2, 2), 16) & MASK
        elif op >> 12 == 0x1:
            self.write(self.r[n] + (op & 15) * 4, self.r[m])
        elif op >> 12 == 0x5:
            self.r[n] = self.read(self.r[m] + (op & 15) * 4)
        elif op & 0xF00F == 0x6003:
            self.r[n] = self.r[m]
        elif op & 0xF00F == 0x6002:
            self.r[n] = self.read(self.r[m])
        elif op & 0xF00F == 0x6001:
            self.r[n] = signed(self.read(self.r[m], 2), 16) & MASK
        elif op & 0xF00F == 0x6006:
            value = self.read(self.r[m])
            self.r[m] = (self.r[m] + 4) & MASK
            self.r[n] = value
        elif op & 0xF00F == 0x600D:
            self.r[n] = self.r[m] & 0xFFFF
        elif op & 0xF00F == 0x2002:
            self.write(self.r[n], self.r[m])
        elif op & 0xF00F == 0x2001:
            self.write(self.r[n], self.r[m], 2)
        elif op & 0xF00F == 0x2006:
            self.r[n] = (self.r[n] - 4) & MASK
            self.write(self.r[n], self.r[m])
        elif op & 0xF0FF == 0x4022:  # sts.l pr,@-Rn
            self.r[n] = (self.r[n] - 4) & MASK
            self.write(self.r[n], self.pr)
        elif op & 0xF0FF == 0x4026:
            self.pr = self.read(self.r[n])
            self.r[n] = (self.r[n] + 4) & MASK
        elif op & 0xF0FF == 0x0002:
            self.r[n] = self.sr
        elif op & 0xF0FF == 0x400E:
            self.sr = self.r[n]
        elif op & 0xF0FF == 0x4009:
            self.r[n] >>= 2
        elif op & 0xF0FF == 0x4001:
            self.flag(self.r[n] & 1)
            self.r[n] >>= 1
        elif op & 0xF0FF == 0x4008:
            self.r[n] = (self.r[n] << 2) & MASK
        elif op & 0xF0FF == 0x4000:
            self.flag(self.r[n] & 0x80000000)
            self.r[n] = (self.r[n] << 1) & MASK
        elif op & 0xF00F == 0x2009:
            self.r[n] &= self.r[m]
        elif op & 0xF00F == 0x200B:
            self.r[n] |= self.r[m]
        elif op & 0xF00F == 0x2008:
            self.flag(not (self.r[n] & self.r[m]))
        elif op & 0xFF00 == 0xC900:
            self.r[0] &= op & 255
        elif op & 0xFF00 == 0xCB00:
            self.r[0] |= op & 255
        elif op & 0xF00F == 0x300C:
            self.r[n] = (self.r[n] + self.r[m]) & MASK
        elif op & 0xF00F == 0x3008:
            self.r[n] = (self.r[n] - self.r[m]) & MASK
        elif op & 0xF00F == 0x3000:
            self.flag(self.r[n] == self.r[m])
        elif op & 0xF00F == 0x3002:
            self.flag(self.r[n] >= self.r[m])
        elif op & 0xF00F == 0x3006:
            self.flag(self.r[n] > self.r[m])
        else:
            raise AssertionError(f"Unsupported heap-model instruction {op:04x} at {pc:08x}")

    def run(self, entry, args, stack):
        saved = self.r[8:15]
        self.pc, self.pr, self.r[15] = entry, 0xDEADBEEE, stack
        for index, value in enumerate(args):
            self.r[4 + index] = value
        first, instructions = len(self.writes), self.instructions
        for _ in range(20000):
            pc = self.pc
            if pc == 0xDEADBEEE:
                assert self.r[15] == stack, "Owner heap call did not restore SP"
                assert self.r[8:15] == saved, "Owner heap call did not preserve R8..R14"
                assert self.sr & 0xF0 == 0xF0, "Owner heap call did not retain IMASK15"
                return self.r[0], self.writes[first:], self.instructions - instructions
            op = self.read(pc, 2)
            target, delay, n = None, False, (op >> 8) & 15
            if op >> 12 in (0xA, 0xB):
                target, delay = (pc + 4 + 2 * signed(op & 0xFFF, 12)) & MASK, True
                if op >> 12 == 0xB:
                    self.pr = pc + 4
            elif op & 0xF0FF in (0x400B, 0x402B):
                target, delay = self.r[n], True
                if op & 0xF0FF == 0x400B:
                    self.pr = pc + 4
            elif op == 0x000B:
                target, delay = self.pr, True
            elif op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
                taken = bool(self.sr & 1) == (op >> 8 in (0x89, 0x8D))
                delay = op >> 8 in (0x8D, 0x8F)
                target = pc + 4 + 2 * signed(op & 255, 8) if taken else pc + (4 if delay else 2)
            if target is None:
                self.plain(op, pc)
                self.pc = pc + 2
            else:
                self.instructions += 1
                if delay:
                    self.plain(self.read(pc + 2, 2), pc + 2)
                self.pc = target
        raise AssertionError("Heap model exceeded its instruction bound")


def stage_layout(build):
    """Read actual ELF32 allocation and the exact sized installer-stack object."""
    data = (build / "stage.elf").read_bytes()
    assert data[:7] == b"\x7fELF\x01\x01\x01", "Requires little-endian ELF32"
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    _, _, machine, _, _, phoff, shoff, _, _, phsize, phnum, shsize, shnum, _ = header
    assert machine == 42 and phsize == 32 and shsize == 40
    sections = [struct.unpack_from("<10I", data, shoff + i * shsize) for i in range(shnum)]
    symbols = {}
    for section in sections:
        _, kind, _, _, offset, count, link, _, _, stride = section
        if kind != 2:
            continue
        assert stride == 16
        strings = sections[link]
        table = data[strings[4]:strings[4] + strings[5]]
        for at in range(offset, offset + count, stride):
            name, value, size, info, _, index = struct.unpack_from("<IIIBBH", data, at)
            if not name or not index:
                continue
            text = table[name:table.index(0, name)].decode()
            symbols[text] = (value, size, info & 15, index)
    stack, size, kind, index = symbols["_native_installer_stack"]
    assert size == 65536 and kind == 1 and not stack & 31
    section = sections[index]
    assert section[1] == 8 and section[2] & 3 == 3, "Stack must be writable allocated BSS"
    begin, end = 0x8CE00000, symbols["__retail_stage_bss_end"][0]
    assert begin <= stack < stack + size <= end <= 0x8CFE0000
    loads = []
    for i in range(phnum):
        kind, offset, address, _, stored, memory, flags, _ = struct.unpack_from("<8I", data, phoff + i * phsize)
        if kind == 1:
            assert begin <= address <= end - memory and stored <= memory
            loads.append((address, data[offset:offset + stored], memory, flags))
    assert loads and any(address <= stack and stack + size <= address + memory and flags & 2
                         for address, _, memory, flags in loads)
    worker = (build / "native-worker.bin").read_bytes()
    assert 80 <= len(worker) < WORKER_SPAN - 0x1000
    export = struct.unpack_from("<20I", worker)
    assert export[:3] == (0x4B4E5031, 1, 80)
    origin = physical(export[7]) - len(worker)
    assert physical(export[10]) == physical(export[11]) == origin + WORKER_SPAN
    assert physical(export[9]) == origin + WORKER_SPAN - 0x1000
    return begin, end, stack, size, loads, worker


class HeapExecutionTests(unittest.TestCase):
    def execute(self, slug):
        path = getattr(OPTIONS, slug)
        if not path:
            self.skipTest(f"Supply --{slug} or KUI_NATIVE_{slug.upper()}_STARTUP")
        if not OPTIONS.build:
            self.skipTest("Supply --build or KUI_NATIVE_HEAP_BUILD for the actual stage")
        wire = (ROOT / "tests/fixtures/native-cache" / f"{slug}.nvr").read_bytes()
        self.assertEqual(len(wire), 1024)
        self.assertEqual(wire[:8], b"KUINVR01")
        self.assertEqual(hashlib.sha256(wire[:992]).digest(), wire[992:])
        image = Path(path).read_bytes()
        self.assertEqual(hashlib.sha256(image).digest(), wire[32:64])
        self.assertEqual(len(image), struct.unpack_from("<I", wire, 24)[0])
        heap = struct.unpack_from("<18I", wire, 608)
        _, _, _, begin, count = struct.unpack_from("<5I", wire, 680)
        initializer, allocate, dispatch, table, sentinel = heap[6], heap[9], heap[11], heap[12], heap[13]
        stage_begin, stage_end, stack, stack_bytes, loads, worker = stage_layout(Path(OPTIONS.build))
        new_sp = stack + stack_bytes - 32
        model = SH(image)
        # Protect holes and BSS as well as file-backed stage text/rodata/data.
        model.store_block(stage_begin, bytes([0xA5]) * (stage_end - stage_begin))
        for address, stored, _, _ in loads:
            model.store_block(address, stored)
        original_stage = model.block(stage_begin, stage_end - stage_begin)
        _, init_writes, init_steps = model.run(initializer, [begin, count], 0x8C00EFC0)
        self.assertEqual(model.block(stage_begin, stage_end - stage_begin), original_stage)
        self.assertTrue(init_writes)
        self.assertTrue(all(not intersects(address, size, stage_begin, stage_end - stage_begin)
                            for _, address, _, size in init_writes))
        initialized_cells = {dispatch, sentinel, sentinel + 4, heap[14], heap[15], heap[16], heap[17], begin, begin + 4}
        self.assertEqual(len(init_writes), 14)
        self.assertTrue(all((size == 4 and address in initialized_cells)
                            or (0x8C00EFC0 - 12 <= address < address + size <= 0x8C00EFC0)
                            for _, address, _, size in init_writes))
        self.assertEqual(model.read(heap[15]), begin)
        self.assertEqual(model.read(heap[16]), count)
        self.assertEqual(model.read(heap[17]), begin)
        self.assertEqual(model.read(dispatch), table)
        self.assertEqual(model.read(table), heap[9])
        self.assertEqual(model.read(table + 4), heap[10])
        self.assertEqual(model.read(sentinel), begin)
        self.assertEqual(model.read(sentinel + 4), 0)
        self.assertEqual(model.read(heap[14]), sentinel)
        self.assertEqual(model.read(begin), sentinel)
        self.assertEqual(model.read(begin + 4), count // 32)
        result, alloc_writes, alloc_steps = model.run(allocate, [LEASE_BYTES], new_sp)
        payload, header = begin + count - LEASE_BYTES, begin + count - LEASE_BYTES - 32
        self.assertEqual(result, payload)
        self.assertEqual(model.read(header + 4), LEASE_BYTES // 32 + 1)
        self.assertEqual(model.read(begin + 4), count // 32 - LEASE_BYTES // 32 - 1)
        self.assertFalse(intersects(payload, WORKER_SPAN, stage_begin, stage_end - stage_begin))
        self.assertTrue(all(not intersects(address, size, stage_begin, stage_end - stage_begin)
                            or stack + 64 <= address < address + size <= new_sp
                            for _, address, _, size in alloc_writes))
        self.assertEqual(len(alloc_writes), 5)
        self.assertTrue(all((size == 4 and address in {begin + 4, header + 4, heap[14]})
                            or new_sp - 8 <= address < address + size <= new_sp
                            for _, address, _, size in alloc_writes))
        # Every stage byte outside the declared call stack stays unchanged.
        after_allocation = model.block(stage_begin, stage_end - stage_begin)
        for index, (old, new) in enumerate(zip(original_stage, after_allocation)):
            self.assertTrue(old == new or stack + 64 <= stage_begin + index < new_sp)
        stack_before_worker = model.block(stack, stack_bytes)
        model.store_block(payload, worker)
        model.store_block(payload + len(worker), bytes(WORKER_SPAN - len(worker)))
        self.assertEqual(model.block(stage_begin, stage_end - stage_begin), after_allocation)
        self.assertEqual(model.block(stack, stack_bytes), stack_before_worker)
        self.assertEqual(model.read(header + 4), LEASE_BYTES // 32 + 1)
        self.assertEqual(model.read(begin), sentinel)
        # Reproduce the former overwrite, rather than merely comparing bounds.
        old_frame = OLD_INSTALLER_SP - 128
        model.store_block(old_frame, bytes([0x69]) * 160)
        model.store_block(payload + len(worker), bytes(WORKER_SPAN - len(worker)))
        old_corrupted = model.block(old_frame, 160) != bytes([0x69]) * 160
        self.assertEqual(old_corrupted, slug in ("skies", "sa2"))
        if slug in ("skies", "sa2"):
            self.assertEqual(physical(payload), 0x0CFD0000)
            self.assertTrue(intersects(old_frame, 160, payload, WORKER_SPAN))
        print(f"Actual {slug} heap: {init_steps} initializer/release + {alloc_steps} allocation instructions; "
              f"payload={payload:08x}, new SP={new_sp:08x}, stage/stack preserved, "
              f"old-stack overwrite={old_corrupted}")

    def test_ps2(self):
        self.execute("ps2")

    def test_skies(self):
        self.execute("skies")

    def test_sa2(self):
        self.execute("sa2")


if __name__ == "__main__":
    if not __debug__:
        raise SystemExit("Run actual heap model with Python assertions enabled")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=os.environ.get("KUI_NATIVE_HEAP_BUILD"))
    for game in ("ps2", "skies", "sa2"):
        parser.add_argument("--" + game, type=Path, default=os.environ.get(f"KUI_NATIVE_{game.upper()}_STARTUP"))
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
