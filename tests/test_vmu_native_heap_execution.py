#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute owner-supplied original heap code at its actual allocation placement.

No retail instructions or executable bytes are embedded in this test. Supply
the exact external startups and a built stage/worker, for example:

  python3 tests/test_vmu_native_heap_execution.py --build build/native-compat \
    --ps2 /path/to/PS2/1ST_READ.BIN --skies /path/to/Skies/1ST_READ.BIN \
    --sa2 /path/to/SA2/1ST_READ.BIN --grandia2 /path/to/Grandia2/1ST_READ.BIN

Equivalent environment variables are KUI_NATIVE_HEAP_BUILD,
KUI_NATIVE_PS2_STARTUP, KUI_NATIVE_SKIES_STARTUP, KUI_NATIVE_SA2_STARTUP and
KUI_NATIVE_GRANDIA2_STARTUP and KUI_NATIVE_EVOLUTION1_STARTUP.
The independent, bounded SH integer model follows the actual initializer,
dispatch helper, bootstrap release and first tail allocation. It then models
the worker's complete copy/zero write span, checking stage and installer-stack
canaries, and executes subsequent original allocation/free operations while
retaining the worker lease. Unsupported instructions fail rather than being
approximated.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_vmu_native_worker_gateway import read_elf, symbol_bytes

MASK = 0xFFFFFFFF
RAM_BEGIN, RAM_END = 0x0C000000, 0x0D000000
WORKER_SPAN, LEASE_BYTES = 0x20000, 0x30000
OLD_INSTALLER_SP = 0x8CFF0000 - 32
OPTIONS = argparse.Namespace(build=os.environ.get("KUI_NATIVE_HEAP_BUILD"),
    **{game: os.environ.get(f"KUI_NATIVE_{game.upper()}_STARTUP") for game in ("ps2", "skies", "sa2", "grandia2", "evolution1")})


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
    assert export[:3] == (0x4B4E5031, 3, 80)
    origin = physical(export[7]) - len(worker)
    assert all(not pointer & 1 and
        pointer & 0xe0000000 == export[3] & 0xe0000000 and
        origin + 80 <= physical(pointer) < origin + len(worker) for pointer in export[16:19])
    table_at = physical(export[19]) - origin
    assert not export[19] & 3 and 80 <= table_at <= len(worker) - 40
    trace = struct.unpack_from('<10I', worker, table_at)
    assert trace[:3] == (0x4b545231, 2, 40)
    # The mixed-alias descriptor owns four ordinary P1 trace entries, two
    # exact P2 cold SDK entries and one48B private P2 scratch object. Read
    # their actual ELF types/owners instead of accepting any in-span address.
    worker_image = read_elf(build / "native-worker.elf")
    address, body, section = symbol_bytes(worker_image, "_kui_vmu_native_trace_exports", 1)
    assert address == export[19] and section[2] & 7 == 2 and len(body) == 40
    assert address & 0xff000000 == 0x8c000000 and body == worker[table_at:table_at + 40]
    gate_names = ("_kui_vmu_native_trace_frame_entry", "_kui_vmu_native_trace_wait_loop",
                  "_kui_vmu_native_trace_wait_before", "_kui_vmu_native_trace_wait_after",
                  "_kui_vmu_native_sdk_coherence_uniform_gate",
                  "_kui_vmu_native_sdk_coherence_variable_gate")
    for index, name in enumerate(gate_names):
        entry, _, code = symbol_bytes(worker_image, name, 2)
        assert not entry & 1 and entry & 0xff000000 == 0x8c000000 and code[2] & 7 == 6
        assert origin + 80 <= physical(entry) < origin + len(worker)
        assert trace[3 + index] == entry + (0x20000000 if index >= 4 else 0)
    state, count, kind, owner = worker_image[4]["_kui_vmu_native_sdk_coherence_state"]
    bss = worker_image[3][owner]
    bss_begin = worker_image[4]["__vmu_native_worker_bss_begin"]
    bss_end = worker_image[4]["__vmu_native_worker_bss_end"]
    assert (bss_begin[0], bss_end[0]) == export[7:9] and owner == bss_begin[3] == bss_end[3]
    assert count == 48 and kind == 1 and not state & 3 and state & 0xff000000 == 0xac000000
    assert bss[1] == 8 and bss[2] & 7 == 3 and export[7] <= state < state + count <= export[8]
    assert bss[3] <= state < state + count <= bss[3] + bss[5] and trace[9] == state
    assert physical(export[10]) == physical(export[11]) == origin + WORKER_SPAN
    assert physical(export[9]) == origin + WORKER_SPAN - 0x1000
    return begin, end, stack, size, loads, worker


class HeapExecutionTests(unittest.TestCase):
    def allocator_evolution(self, model, heap, begin, count, payload, stack):
        """Original SH transitions, checked against an independent interval set.

        This exercises the ordinary SDK allocation/free contract. It does not
        model arbitrary game writes or substitute for the runtime release-hook
        admission tests. The retained worker is never passed to release.
        """
        allocate, release, sentinel, cursor = heap[9], heap[10], heap[13], heap[14]
        header, alias = payload - 32, begin & 0xE0000000
        arena_begin, arena_end = physical(begin), physical(begin) + count
        lease_begin, lease_end = physical(header), physical(payload) + LEASE_BYTES
        identity = {heap[15]: begin, heap[17]: begin, heap[16]: count,
                    heap[11]: heap[12], heap[12]: allocate, heap[12] + 4: release,
                    sentinel + 4: 0, header + 4: LEASE_BYTES // 32 + 1}
        retained = model.block(header, LEASE_BYTES + 32)
        live, operations, maximum_nodes, steps = {}, 0, 0, 0
        exact_fits = split_fits = failed_allocations = 0

        def merged(intervals):
            result = []
            for first, end in sorted(intervals):
                if result and result[-1][1] == first:
                    result[-1] = (result[-1][0], end)
                else:
                    if result:
                        self.assertLess(result[-1][1], first)
                    result.append((first, end))
            return result

        def check():
            nonlocal maximum_nodes
            for address, value in identity.items():
                self.assertEqual(model.read(address), value, hex(address))
            self.assertEqual(model.block(header, LEASE_BYTES + 32), retained)
            nodes, intervals, node = {sentinel}, [], model.read(sentinel)
            for _ in range(1024):
                if node == sentinel:
                    break
                self.assertNotIn(node, nodes, "Free list must not cycle")
                nodes.add(node)
                self.assertEqual(node & 0xE0000000, alias)
                self.assertEqual(node & 31, 0)
                at, units = physical(node), model.read(node + 4)
                self.assertGreaterEqual(at, arena_begin)
                self.assertGreater(units, 0)
                end = at + units * 32
                self.assertLessEqual(end, arena_end)
                self.assertTrue(end <= lease_begin or lease_end <= at)
                if intervals:
                    self.assertLess(intervals[-1][1], at, "Free nodes must be sorted and coalesced")
                for address, (allocated_units, pattern) in live.items():
                    first = physical(address) - 32
                    self.assertTrue(end <= first or first + allocated_units * 32 <= at)
                    self.assertEqual(model.read(address - 28), allocated_units)
                    self.assertEqual(model.block(address, len(pattern)), pattern)
                intervals.append((at, end))
                node = model.read(node)
            else:
                self.fail("Original allocator free list exceeded 1024 nodes")
            self.assertIn(model.read(cursor), nodes)
            maximum_nodes = max(maximum_nodes, len(intervals))
            return intervals

        def alloc(size):
            nonlocal operations, steps, exact_fits, split_fits, failed_allocations
            before = check()
            result, writes, instructions = model.run(allocate, [size], stack)
            operations += 1
            steps += instructions
            units = (size + 31) // 32 + 1
            if not result:
                self.assertFalse(any(end - first >= units * 32 for first, end in before))
                self.assertEqual(check(), before)
                failed_allocations += 1
                return result
            self.assertEqual(result & 0xE0000000, alias)
            self.assertEqual(result & 31, 0)
            self.assertNotIn(result, live)
            first, end = physical(result) - 32, physical(result) - 32 + units * 32
            containing = [(a, b) for a, b in before if a <= first < end <= b]
            self.assertEqual(len(containing), 1)
            self.assertEqual(end, containing[0][1], "Reviewed allocator takes the free block tail")
            expected = [interval for interval in before if interval != containing[0]]
            if containing[0][0] < first:
                expected.append((containing[0][0], first))
                split_fits += 1
            else:
                exact_fits += 1
            self.assertTrue(all(not intersects(address, width, header, LEASE_BYTES + 32)
                                for _, address, _, width in writes))
            pattern = bytes([0x30 + operations % 160]) * ((units - 1) * 32)
            model.store_block(result, pattern)
            live[result] = (units, pattern)
            self.assertEqual(check(), sorted(expected))
            return result

        def free(address):
            nonlocal operations, steps
            before = check()
            units, _ = live.pop(address)
            _, writes, instructions = model.run(release, [address], stack)
            operations += 1
            steps += instructions
            first = physical(address) - 32
            self.assertTrue(all(not intersects(at, width, header, LEASE_BYTES + 32)
                                for _, at, _, width in writes))
            self.assertEqual(check(), merged(before + [(first, first + units * 32)]))

        initial = check()
        # An isolated hole is consumed exactly; then free it in the opposite
        # order to exercise joining an adjacent upper and lower free block.
        first, middle, last = alloc(33), alloc(64), alloc(32)
        free(middle)
        replacement = alloc(64)
        self.assertEqual(replacement, middle)
        free(first)
        free(last)
        free(replacement)
        self.assertEqual(check(), initial)
        varied = [alloc(size) for size in (1, 31, 32, 33, 63, 64, 65, 127, 128, 129, 1023, 4096, 8191)]
        for address in varied[1::2] + varied[::2]:
            free(address)
        self.assertEqual(check(), initial)
        fragmented = [alloc(1 + (i * 97) % 2048) for i in range(64)]
        for address in fragmented[1::2]:
            free(address)
        replacements = [alloc((1, 20, 96, 320, 1024)[i % 5]) for i in range(32)]
        # A request larger than the entire arena must fail without changing
        # the free set or the retained header/worker, even when fragmented.
        self.assertEqual(alloc(count), 0)
        remaining = fragmented[::2] + replacements
        for address in remaining[1::2] + remaining[::2]:
            free(address)
        self.assertFalse(live)
        self.assertEqual(check(), initial)
        self.assertGreaterEqual(maximum_nodes, 32)
        self.assertGreater(exact_fits, 0)
        self.assertGreater(split_fits, 0)
        self.assertEqual(failed_allocations, 1)
        return operations, maximum_nodes, steps

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
        operations, free_nodes, evolution_steps = self.allocator_evolution(
            model, heap, begin, count, payload, new_sp)
        # Reproduce the former overwrite, rather than merely comparing bounds.
        old_frame = OLD_INSTALLER_SP - 128
        model.store_block(old_frame, bytes([0x69]) * 160)
        model.store_block(payload + len(worker), bytes(WORKER_SPAN - len(worker)))
        old_corrupted = model.block(old_frame, 160) != bytes([0x69]) * 160
        self.assertEqual(old_corrupted, slug in ("skies", "sa2", "evolution1"))
        if slug in ("skies", "sa2", "evolution1"):
            self.assertEqual(physical(payload), 0x0CFD0000)
            self.assertTrue(intersects(old_frame, 160, payload, WORKER_SPAN))
        print(f"Actual {slug} heap: {init_steps} initializer/release + {alloc_steps} allocation instructions; "
              f"payload={payload:08x}, new SP={new_sp:08x}, stage/stack preserved, "
              f"old-stack overwrite={old_corrupted}")
        print(f"Actual {slug} lifetime: {operations} allocation/free operations, "
              f"{free_nodes} maximum free nodes, {evolution_steps} SH instructions; "
              "exact/split/failure/coalescing transitions preserve owner/header/worker")

    def test_ps2(self):
        self.execute("ps2")

    def test_skies(self):
        self.execute("skies")

    def test_sa2(self):
        self.execute("sa2")

    def test_grandia2(self):
        self.execute("grandia2")

    def test_evolution1(self):
        self.execute("evolution1")


if __name__ == "__main__":
    if not __debug__:
        raise SystemExit("Run actual heap model with Python assertions enabled")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=os.environ.get("KUI_NATIVE_HEAP_BUILD"))
    for game in ("ps2", "skies", "sa2", "grandia2", "evolution1"):
        parser.add_argument("--" + game, type=Path, default=os.environ.get(f"KUI_NATIVE_{game.upper()}_STARTUP"))
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
