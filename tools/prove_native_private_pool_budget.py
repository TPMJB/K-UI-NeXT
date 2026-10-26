#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay one externally supplied original private-pool capacity regression.

No game executable or asset bytes are embedded. The known input identity is an
analysis fixture, never a runtime admission rule. Actual original startup,
SDK allocation/free, private-pool initialization and rendering-work allocation
instructions execute in the independent bounded SH model. A freshly compiled
production C adapter supplies the changed reserve word; it does not replace
any original allocator. Nothing is written to the supplied image.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
from test_vmu_native_heap_execution import SH, MASK, physical, signed

SCHEMA = 'kui.native.private-pool-capacity-original.v1'
INPUT_SHA256 = 'b05853a539011b02fff09062441372a924f4ac2465abc498d69631d4b137bdf5'
INPUT_BYTES = 3473412
LOAD = 0x8c010000
PARENT = 0x8c010680
BOUNDARY = 0x8c0106ac
BREAK_CELL = 0x8c34c45c
POOL_CELL = 0x8c360030
SCENE_ENTRY = 0x8c2ad102
SCENE_CALL = 0x8c0110ac
SCENE_STOP = 0x8c2ad22a
SCENE_POINTER = 0x8c4ffb70
SCENE_USER_BUFFER = 0x8c350970
STACK = 0x8c52f000
LEASE_BYTES = 0x60000
LEASE_COST = LEASE_BYTES + 32
PLAN_FIELDS = ('reserve_address', 'reserve_before', 'reserve_after',
               'child_bytes_before', 'child_bytes_after')
SOURCE_FILES = ('tools/prove_native_private_pool_budget.py',
                'tests/test_vmu_native_heap_execution.py',
                'src/core/vmu_native_heap.c',
                'src/core/vmu_native_heap_bottom_code.inc',
                'src/core/vmu_native_heap_budget.c',
                'include/kui/vmu_native_heap.h',
                'include/kui/vmu_native_heap_budget.h')


def sha(data):
    return hashlib.sha256(data).hexdigest()


class ProductionBudget:
    """A fresh production C discovery/budget helper, with coherent model reads."""
    Reader = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_void_p,
                             ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32))

    def __init__(self, directory, image):
        library_path = Path(directory) / 'private-pool-budget.so'
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-shared', '-fPIC',
                   '-I', str(ROOT / 'include'), str(ROOT / 'src/core/vmu_native_heap.c'),
                   str(ROOT / 'src/core/vmu_native_heap_budget.c'), '-o', str(library_path)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        self.library = ctypes.CDLL(str(library_path))
        pointer, word = ctypes.c_void_p, ctypes.c_uint32
        self.discover = self.library.kui_vmu_native_heap_discover
        self.discover.argtypes = (pointer, ctypes.c_size_t, word, pointer)
        self.discover.restype = ctypes.c_int
        self.call_discover = self.library.kui_vmu_native_heap_call_discover
        self.call_discover.argtypes = (pointer, ctypes.c_size_t, pointer, pointer)
        self.call_discover.restype = ctypes.c_bool
        self.prepare = self.library.kui_vmu_native_heap_budget_prepare
        self.prepare.argtypes = (pointer, ctypes.c_size_t, pointer, pointer,
                                 self.Reader, pointer, word, pointer)
        self.prepare.restype = ctypes.c_int
        self.image = bytes(image)
        self.source = ctypes.create_string_buffer(self.image)
        self.heap = (word * 18)()
        self.call = (word * 5)()
        assert self.discover(self.source, len(image), LOAD, self.heap) == 1
        assert self.call_discover(self.source, len(image), self.heap, self.call)
        assert tuple(self.call) == (BOUNDARY - 12, BOUNDARY - 4, BOUNDARY, 0x8c59dd00, 0xa62300)

    def plan(self, model, *, image=None, cost=LEASE_COST, deny=None):
        original = self.source if image is None else ctypes.create_string_buffer(bytes(image))
        errors, reads = [], []

        def read(_context, address, output):
            reads.append(address)
            if address == deny:
                return False
            try:
                assert not address & 3
                output[0] = model.read(address)
                return True
            except (AssertionError, IndexError) as error:
                errors.append(str(error))
                return False

        callback = self.Reader(read)
        output = (ctypes.c_uint32 * 5)()
        result = self.prepare(original, len(self.image), self.heap, self.call,
                              callback, None, cost, output)
        assert not errors, errors
        return result, dict(zip(PLAN_FIELDS, map(int, output))), len(reads)


class PoolSH(SH):
    """Original heap model plus the exact scalar FPU prefix used before malloc.

    Only FLDI0, FLOAT, FCMP/GT, register FMOV and one FR15 stack save are
    added. The positive integer count 230100 is exactly representable, so
    this reviewed branch has no rounding ambiguity. No movie FPU operations,
    PVR/AICA hardware, interrupt delivery or complete gameplay are emulated.
    """
    def __init__(self, image, mode, helper, mutation_checks=False):
        super().__init__(image)
        self.image, self.mode, self.helper = image, mode, helper
        self.injected = False
        self.fr, self.fpul = [0.0] * 16, 0
        self.lease = self.placeholder = None
        self.worker_canary = self.worker_header = None
        self.plan_output = None
        self.stop_at = None
        self.phase = 'startup'
        self.allocations = []
        self.child_argument = None
        self.mutation_checks = mutation_checks
        self.refusals = []

    def budget_refusals(self):
        source_mutations = {
            'parent-code': (BOUNDARY + 0x1c - LOAD, 1, 2),
            'child-code': (0x8c0183d6 - LOAD, 1, 2),
            # A second PC-relative load and an immutable address-of-slot.
            'second-consumer': (0x8c010360 - LOAD,
                                0xd000 | ((0x8c01073c - 0x8c010364) // 4), 2),
            'address-of-reserve': (0x8c0107fc - LOAD, 0x8c01073c, 4),
        }
        for label, (offset, value, width) in source_mutations.items():
            changed = bytearray(self.image)
            if label in ('parent-code', 'child-code'):
                value ^= int.from_bytes(changed[offset:offset + width], 'little')
            changed[offset:offset + width] = value.to_bytes(width, 'little')
            result, plan, _ = self.helper.plan(self, image=changed)
            assert result == -1 and not any(plan.values()), label
            self.refusals.append(label)
        live_mutations = {'live-break': (BREAK_CELL, self.read(BREAK_CELL) + 32),
                          'published-child': (POOL_CELL, 0x8c700000),
                          'dispatch-target': (int(self.helper.heap[12]), 0)}
        for label, (address, value) in live_mutations.items():
            before = self.read(address)
            self.write(address, value)
            result, plan, _ = self.helper.plan(self)
            self.write(address, before)
            assert result == -1 and not any(plan.values()), label
            self.refusals.append(label)
        for label, cost in (('unaligned-cost', LEASE_COST + 1),
                            ('oversize-cost', 0xffffffe0)):
            result, plan, _ = self.helper.plan(self, cost=cost)
            assert result == -1 and not any(plan.values()), label
            self.refusals.append(label)
        result, plan, _ = self.helper.plan(self, deny=BREAK_CELL)
        assert result == -1 and not any(plan.values())
        self.refusals.append('denied-live-read')

    def boundary(self):
        self.injected = True
        result, plan, reads = self.helper.plan(self)
        assert result == 1 and reads
        self.plan_output = plan
        if self.mutation_checks:
            self.budget_refusals()
        saved = self.r[:], self.pc, self.pr, self.sr, self.executing, self.phase
        allocator, release = int(self.helper.heap[9]), int(self.helper.heap[10])
        self.phase = 'reservation'
        count = int(self.helper.call[4])
        if self.mode == 'tail':
            self.lease, _, _ = self.run(allocator, [LEASE_BYTES], STACK - 0x1000)
        elif self.mode in ('bottom', 'bottom-budget'):
            self.placeholder, _, _ = self.run(allocator, [count - LEASE_BYTES - 64], STACK - 0x1000)
            self.lease, _, _ = self.run(allocator, [LEASE_BYTES], STACK - 0x1000)
            self.run(release, [self.placeholder], STACK - 0x1000)
        self.r, self.pc, self.pr, self.sr, self.executing, self.phase = saved
        if self.mode == 'bottom-budget':
            assert self.read(plan['reserve_address']) == plan['reserve_before']
            self.write(plan['reserve_address'], plan['reserve_after'])
        if self.lease is not None:
            self.worker_canary = bytes([0xa7]) * LEASE_BYTES
            self.store_block(self.lease, self.worker_canary)
            self.worker_header = self.block(self.lease - 32, 32)

    def plain(self, op, pc):
        if pc == self.stop_at:
            raise StopIteration
        if pc == BOUNDARY and not self.injected:
            self.boundary()
        if pc == int(self.helper.heap[9]):
            self.allocations.append(dict(phase=self.phase, request=self.r[4], return_pr=self.pr))
        if pc == 0x8c0183be:
            self.child_argument = (self.r[4], self.r[5])
        n, m = (op >> 8) & 15, (op >> 4) & 15
        if op & 0xf00f == 0x0006:
            self.executing = pc
            self.instructions += 1
            self.write((self.r[0] + self.r[n]) & MASK, self.r[m])
        elif op & 0xf0ff == 0xf0fb:
            self.executing = pc
            self.instructions += 1
            self.r[15] -= 4
            self.write(self.r[15], struct.unpack('<I', struct.pack('<f', self.fr[n]))[0])
        elif op & 0xf0ff == 0x4011:
            self.executing = pc
            self.instructions += 1
            self.flag(not (self.r[n] & 0x80000000))
        elif op & 0xf0ff == 0x405a:
            self.executing = pc
            self.instructions += 1
            self.fpul = self.r[n]
        elif op & 0xf0ff == 0xf02d:
            self.executing = pc
            self.instructions += 1
            self.fr[n] = float(signed(self.fpul, 32))
        elif op & 0xf0ff == 0xf08d:
            self.executing = pc
            self.instructions += 1
            self.fr[n] = 0.0
        elif op & 0xf00f == 0xf005:
            self.executing = pc
            self.instructions += 1
            self.flag(self.fr[n] > self.fr[m])
        elif op & 0xf00f == 0xf00c:
            self.executing = pc
            self.instructions += 1
            self.fr[n] = self.fr[m]
        else:
            super().plain(op, pc)

    def run_to(self, entry, args, stack, stop):
        self.stop_at = stop
        try:
            self.run(entry, args, stack)
        except StopIteration:
            return
        raise AssertionError('Original scene code did not reach the bounded stop')


def scene_arguments(image):
    """Read the actual caller operands; each source value is independently bound.

    The scene entry takes four ABI registers and a fifth caller-stack word.
    The caller's immediate-constant/PC-relative expression is checked rather
    than supplying an invented allocation request to the SDK allocator.
    """
    def read(address, width=2):
        return int.from_bytes(image[address - LOAD:address - LOAD + width], 'little')
    expected = {0x8c01109e: 0xe300, 0x8c0110a2: 0x6533,
                0x8c0110a6: 0x6733, 0x8c0110aa: 0x2f36,
                SCENE_CALL: 0x420b, SCENE_CALL + 2: 0xe4ff}
    for address, op in expected.items():
        assert read(address) == op, hex(address)
    op = read(0x8c0110a0)
    assert op & 0xff00 == 0xd600
    count_slot = ((0x8c0110a0 + 4) & ~3) + (op & 255) * 4
    count = read(count_slot, 4)
    op = read(0x8c0110a4)
    assert op & 0xff00 == 0xd200
    target_slot = ((0x8c0110a4 + 4) & ~3) + (op & 255) * 4
    assert read(target_slot, 4) == SCENE_ENTRY
    assert count == 230000
    return [MASK, 0, count, 0], 0


def prove(image: Path, expected_plan: dict | None = None) -> dict:
    image = Path(image)
    assert image.stat().st_size == INPUT_BYTES, 'Expected exact external capacity-regression input'
    data = image.read_bytes()
    assert len(data) == INPUT_BYTES and sha(data) == INPUT_SHA256, 'Expected exact external capacity-regression input'
    arguments, fifth = scene_arguments(data)
    rows, plans, refusals = [], [], []
    with tempfile.TemporaryDirectory(prefix='kui-private-pool-proof-') as temporary:
        helper = ProductionBudget(temporary, data)
        for mode in ('baseline', 'tail', 'bottom', 'bottom-budget'):
            model = PoolSH(data, mode, helper, mutation_checks=(mode == 'bottom-budget'))
            original_return, _, _ = model.run(PARENT, [], STACK)
            assert model.injected and model.child_argument is not None
            plan = model.plan_output
            assert plan is not None
            if expected_plan is not None:
                assert set(expected_plan) == set(PLAN_FIELDS), 'Compiled budget plan fields differ'
                assert all(type(expected_plan[key]) is int for key in PLAN_FIELDS)
                assert plan == expected_plan, 'Actual compiled budget plan differs from source-helper replay'
            plans.append(plan)
            pool, requested = model.child_argument
            assert pool == model.read(POOL_CELL)
            assert original_return == pool
            head = [model.read(pool + 4 * i) for i in range(8)]
            sentinel = pool + requested - 32
            end_words = [model.read(sentinel + 4 * i) for i in range(8)]
            assert head == [0x6e6d0008, 0, requested - 32, 0,
                            sentinel, sentinel, 0, sentinel]
            assert end_words == [0x6e6d0000, 0, 32, pool, 0, 0, pool, sentinel]
            assert pool + requested == 0x8d000000 - (LEASE_COST if mode == 'tail' else 0)
            cursor = model.read(int(helper.heap[14]))
            free = model.read(cursor)
            free_before = model.read(free + 4) * 32
            assert model.read(SCENE_USER_BUFFER) == model.read(SCENE_POINTER) == 0
            model.write(STACK, fifth)
            model.phase = 'scene'
            model.run_to(SCENE_ENTRY, arguments, STACK, SCENE_STOP)
            scene_allocs = [row for row in model.allocations if row['phase'] == 'scene']
            assert len(scene_allocs) == 1
            scene_request = scene_allocs[0]['request']
            assert scene_request == 920752
            published = model.read(SCENE_POINTER)
            returned = published & ~0x20000000
            if mode in ('baseline', 'bottom-budget'):
                assert returned and returned + ((scene_request + 31) & ~31) == pool - 32
            else:
                assert returned == 0 and published == 0x20000000
            if model.lease is not None:
                assert model.block(model.lease, LEASE_BYTES) == model.worker_canary
                assert model.block(model.lease - 32, 32) == model.worker_header
                lease_begin, lease_end = physical(model.lease - 32), physical(model.lease) + LEASE_BYTES
                assert physical(pool) >= lease_end or physical(pool + requested) <= lease_begin
                if returned:
                    assert physical(returned - 32) >= lease_end or physical(returned + scene_request) <= lease_begin
            rows.append(dict(mode=mode, application_pool=pool, application_pool_bytes=requested,
                application_pool_end=pool + requested, application_head=head,
                application_sentinel=sentinel, sentinel_words=end_words,
                reserve_literal=model.read(plan['reserve_address']), lease=model.lease,
                free_sdk_bytes_before_scene=free_before, scene_request_bytes=scene_request,
                scene_allocator_return=returned, scene_published_pointer=published,
                worker_payload_preserved=True if model.lease is not None else None,
                worker_header_preserved=True if model.lease is not None else None,
                canary_bytes=LEASE_BYTES if model.lease is not None else 0,
                scene_allocator_calls=scene_allocs,
                original_instructions=model.instructions))
            refusals.extend(model.refusals)
    assert all(plan == plans[0] for plan in plans)
    assert plans[0] == dict(zip(PLAN_FIELDS, (0x8c01073c, 0xe0b84, 0x140ba4, 0x981360, 0x921340)))
    assert [r['free_sdk_bytes_before_scene'] for r in rows] == [921472, 528224, 528224, 921472]
    assert [r['scene_allocator_return'] for r in rows] == [0x8c59dfc0, 0, 0, 0x8c5fdfe0]
    assert len(refusals) == 10
    return dict(schema=SCHEMA, status='PASS', input_bytes=len(data), input_sha256=sha(data),
        lease_payload_bytes=LEASE_BYTES, lease_physical_cost=LEASE_COST, plan=plans[0],
        source_files={name: sha((ROOT / name).read_bytes()) for name in SOURCE_FILES},
        original_entries=dict(startup=PARENT, initializer_return=BOUNDARY,
                              scene=SCENE_ENTRY, scene_caller=SCENE_CALL, stop=SCENE_STOP),
        scene_arguments=arguments, scene_fifth_argument=fifth,
        cases=rows, refusal_cases=refusals,
        fpu_model_scope='Only original scalar FLDI0/FLOAT/FCMP-GT/FMOV and FR15 stack-save operations before allocation. Positive integer230100 is exactly representable. No movie FPU or hardware emulation.',
        limits=[
            'The full original startup/private-pool initializer and reviewed scene entry execute; intervening game entry calls and all assets are not executed.',
            'Scene arguments come from the exact original caller. A separate static startup/gateway review establishes that the default SDK allocation branch is reached without an earlier user-buffer setter.',
            'This counterfactual proves an allocation defect and its budget correction, not the live console rendering pointer or successful playback.',
            'The corrected child pool is smaller by the physical lease cost. Future peak child-pool demand remains a hardware/gameplay test.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True, help='Unmodified external original startup image')
    parser.add_argument('--output', type=Path, help='Write successful capacity proof JSON')
    args = parser.parse_args()
    if not __debug__:
        parser.exit(2, 'Private-pool replay requires Python assertions enabled\n')
    try:
        if args.output and args.output.resolve() == args.image.resolve():
            raise ValueError('Proof output must differ from the original input')
        report = prove(args.image)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(dict(status=report['status'], schema=SCHEMA,
                              cases=len(report['cases']), refusals=len(report['refusal_cases']),
                              input_sha256=report['input_sha256'], plan=report['plan']), indent=2))
    except (OSError, ValueError, TypeError, AssertionError, ctypes.ArgumentError,
            subprocess.CalledProcessError) as error:
        detail = (error.stderr if isinstance(error, subprocess.CalledProcessError) else
                  type(error).__name__ if isinstance(error, TypeError) else str(error))
        parser.exit(2, 'Private-pool original replay refused: ' + (detail or type(error).__name__) + '\n')


if __name__ == '__main__':
    main()
