#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay supplied original early exits/compact loader and terminal veneers.

No retail instruction bytes are embedded. The shared SH integer decoder
executes the original pair, loader and thunk. File APIs, the platform cleanup
calls, the loaded executable, and the final LOW terminal are explicit model
boundaries. This proves their ordering and interception, not their hardware
implementation or whole-game compatibility.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

from test_vmu_native_heap_execution import SH, MASK, signed, validate_delay_slot

ROOT = Path(__file__).resolve().parents[1]
OWNER_SHA = '8a65a81fb0f862da473c0e3489c063defb4dac25b9ee382d13108cd59635b86e'
LOAD = 0x8c010000
RETURN = 0x8c00ed00
BIOS = 0x8c008100
STACK = 0x8c00f000
FILENAME = 0x8c700100
FILE_DATA = bytes(range(32))  # Authored API output, never executable fixture code.


class Exit(ct.Structure):
    _fields_ = [(name, ct.c_uint32) for name in
                ('version', 'bytes', 'family', 'load', 'image_bytes', 'count')]
    _fields_ += [('entries', ct.c_uint32 * 4)]
    _fields_ += [(name, ct.c_uint32) for name in
                ('bios_vector_literal', 'exec_thunk', 'cleanup')]


def compile_api(directory):
    bridge = directory / 'bridge.c'
    bridge.write_text('''#include "kui/vmu_native_exit.h"
unsigned reviewed_exit_window(const struct kui_vmu_native_exit *e,unsigned i) {
    return kui_vmu_native_exit_window(e,i);
}
int reviewed_exit_shape(const struct kui_vmu_native_exit *e) {
    return kui_vmu_native_exit_shape_valid(e);
}
''')
    output = directory / 'api.so'
    subprocess.run([*shlex.split(os.environ.get('HOST_CC', 'cc')), '-std=c11',
                    '-O2', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'include'), str(bridge),
                    *[str(ROOT / path) for path in (
                        'src/core/vmu_native_exit.c', 'src/core/vmu_native_heap.c',
                        'src/core/vmu_native_patch.c')], '-o', str(output)],
                   check=True, capture_output=True, text=True)
    lib = ct.CDLL(str(output))
    signatures = {
        'kui_vmu_native_exit_discover': ([ct.c_void_p, ct.c_size_t, ct.c_uint32,
                                        ct.POINTER(Exit)], ct.c_bool),
        'kui_vmu_native_patch_encode': ([ct.c_uint32, ct.c_uint32,
                                        ct.POINTER(ct.c_uint16)], ct.c_bool),
        'kui_vmu_native_patch_encode_head': ([ct.c_uint32,
                                             ct.POINTER(ct.c_uint16)], ct.c_bool),
        'reviewed_exit_window': ([ct.POINTER(Exit), ct.c_uint], ct.c_uint),
        'reviewed_exit_shape': ([ct.POINTER(Exit)], ct.c_int),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(lib, name)
        function.argtypes = arguments
        function.restype = result
    assert ct.sizeof(Exit) == 52
    return lib


def long_literal(source, pc):
    at = pc - LOAD
    opcode = struct.unpack_from('<H', source, at)[0]
    assert opcode >> 12 == 13
    slot = ((pc + 4) & ~3) + (opcode & 255) * 4
    return struct.unpack_from('<I', source, slot - LOAD)[0]


class Flow(SH):
    """Original instruction execution with named synchronous API boundaries."""

    def __init__(self, source, owner, opened, terminal=None):
        super().__init__(source)
        self.owner = owner
        self.loader = owner.entries[2]
        self.opened = opened
        self.terminal = terminal
        self.calls = []
        self.executed_pcs = []
        self.loaded_target = None
        self.expected_filename = None
        self.expected_destination = None
        self.handle = 0x8c700200
        self.api_targets = {long_literal(source, self.loader + offset): name
                            for offset, name in ((8, 'setup'), (16, 'open'),
                                                 (30, 'size'), (38, 'read'),
                                                 (48, 'close'), (54, 'cleanup'))}
        assert len(self.api_targets) == 6
        self.store_block(0x8c0000e0, struct.pack('<I', BIOS))
        self.store_block(FILENAME, b'arbitrary.bin\0')

    def boundary(self, name):
        args = tuple(self.r[4:7])
        self.calls.append(dict(name=name, pc=hex(self.pc), args=list(args)))
        if name == 'setup':
            assert args[0] == long_literal(self.memory[0x10000:0x10000 + self.owner.image_bytes],
                                           self.loader + 10)
            # The first JSR delay slot must save R5 before the callee can
            # legally clobber it; later calls must reload that saved value.
            assert self.read(self.r[15] + 4) == self.expected_destination
        elif name == 'open':
            assert args[:2] == (self.expected_filename, 0)
        elif name == 'size':
            assert self.opened and args[:2] == (self.handle, self.r[15])
            self.write(args[1], len(FILE_DATA))
        elif name == 'read':
            assert self.opened and args == (self.handle, len(FILE_DATA),
                                            self.expected_destination)
            # This is the declared file API boundary. Every surrounding
            # loader instruction, including argument reloads, executes.
            for at in range(0, len(FILE_DATA), 4):
                self.write(args[2] + at, int.from_bytes(FILE_DATA[at:at + 4], 'little'))
        elif name == 'close':
            assert self.opened and args[0] == self.handle
        elif name == 'loaded-executable':
            assert self.pc == self.expected_destination and self.pr == self.loader + 66
            assert self.calls[-2]['name'] == 'cleanup'
            self.loaded_target = self.pc
        elif name not in ('cleanup', 'pair-cache-cleanup'):
            raise AssertionError('Unknown API boundary: ' + name)
        # Legal caller-saved clobbers force actual reload/stack transport.
        for register in range(1, 8):
            self.r[register] = 0xa5a50000 + register
        self.r[0] = self.handle if name == 'open' and self.opened else 0
        self.pc = self.pr

    def execute(self, entry, arguments):
        self.pc, self.pr, self.r[15] = entry, RETURN, STACK
        self.sr = 0x600000f1
        for index, value in enumerate(arguments):
            self.r[4 + index] = value
        initial = self.r[:]
        if entry == self.owner.exec_thunk:
            self.expected_filename = long_literal(
                self.memory[0x10000:0x10000 + self.owner.image_bytes], entry)
            self.expected_destination = 0x8c008300
        else:
            self.expected_filename, self.expected_destination = self.r[4:6]
        for _ in range(256):
            pc = self.pc
            if pc == self.terminal:
                return dict(stop='low-terminal', initial=initial)
            if pc == RETURN:
                assert self.r[15] == STACK and self.r[8:15] == initial[8:15]
                return dict(stop='original-return', initial=initial)
            if pc == BIOS:
                self.calls.append(dict(name='BIOS-vector', pc=hex(pc), args=list(self.r[4:7])))
                return dict(stop='BIOS-vector', initial=initial)
            if pc == self.expected_destination and self.calls:
                self.boundary('loaded-executable')
                continue
            if pc in self.api_targets:
                self.boundary(self.api_targets[pc])
                continue
            if pc == self.owner.cleanup:
                self.boundary('pair-cache-cleanup')
                continue
            assert ((self.owner.entries[0] <= pc < self.owner.entries[0] + 26) or
                    self.loader <= pc < self.loader + 84), ('Execution escaped reviewed body', hex(pc))
            opcode = self.read(pc, 2)
            self.executed_pcs.append(pc)
            target, delay, register = None, False, (opcode >> 8) & 15
            if opcode >> 12 in (0xa, 0xb):
                target = (pc + 4 + 2 * signed(opcode & 0xfff, 12)) & MASK
                delay = True
                if opcode >> 12 == 0xb:
                    self.pr = pc + 4
            elif opcode & 0xf0ff in (0x400b, 0x402b):
                target, delay = self.r[register], True
                if opcode & 0xf0ff == 0x400b:
                    self.pr = pc + 4
            elif opcode == 0x000b:
                target, delay = self.pr, True
            elif opcode >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
                taken = bool(self.sr & 1) == (opcode >> 8 in (0x89, 0x8d))
                delay = opcode >> 8 in (0x8d, 0x8f)
                target = pc + 4 + 2 * signed(opcode & 255, 8) if taken else pc + (4 if delay else 2)
            if target is None:
                self.plain(opcode, pc)
                self.pc = pc + 2
            else:
                self.instructions += 1
                if delay:
                    slot = self.read(pc + 2, 2)
                    validate_delay_slot(slot, pc)
                    self.executed_pcs.append(pc + 2)
                    self.plain(slot, pc + 2)
                self.pc = target
        raise AssertionError('Original early/compact path exceeded bound')


def patches(lib, owner, target):
    result = []
    for index in (1, 2):
        words = (ct.c_uint16 * 6)()
        assert lib.kui_vmu_native_patch_encode(owner.entries[index], target, words)
        result.append((owner.entries[index], struct.pack('<6H', *words)))
    head = (ct.c_uint16 * 4)()
    assert lib.kui_vmu_native_patch_encode_head(owner.entries[0], head)
    result.append((owner.entries[0], struct.pack('<4H', *head)))
    return result


def terminal_result(cpu, result, owner, entry, target):
    assert result['stop'] == 'low-terminal' and not cpu.calls and not cpu.writes
    expected = result['initial'][:]
    if entry == owner.exec_thunk:
        expected[4:7] = [0x8c670d70, 0x8c008300, 1]
    assert cpu.r[1:] == expected[1:] and cpu.r[0] == target
    assert cpu.r[15] == STACK and cpu.pr == RETURN and cpu.sr == 0x600000f1
    return dict(entry=hex(entry), terminal=hex(target),
                instructions=cpu.instructions,
                executed_pcs=[hex(pc) for pc in cpu.executed_pcs],
                original_calls=0, RAM_stores=0,
                owner_SP_PR_SR_and_nonscratch_GPRs_preserved=True)


def proof(source, lib):
    image = ct.create_string_buffer(source)
    owner = Exit()
    assert lib.kui_vmu_native_exit_discover(image, len(source), LOAD, ct.byref(owner))
    assert (owner.version, owner.bytes, owner.family, owner.count) == (2, 52, 8, 3)
    assert list(owner.entries) == [0x8c60808c, 0x8c608094, 0x8c648ad8, 0]
    assert owner.exec_thunk == 0x8c648b24 and owner.bios_vector_literal == 0x8c6080a8
    assert owner.cleanup == 0x8c60055c and lib.reviewed_exit_shape(ct.byref(owner))
    assert [lib.reviewed_exit_window(ct.byref(owner), i) for i in range(3)] == [8, 12, 12]
    original, intercepted = [], []
    for index in (0, 1):
        cpu = Flow(source, owner, False)
        result = cpu.execute(owner.entries[index], [FILENAME, 0x8c700000, 0x12345678])
        assert result['stop'] == 'BIOS-vector' and cpu.calls[-1]['args'][0] == index + 1
        assert [call['name'] for call in cpu.calls] == (
            ['BIOS-vector'] if not index else ['pair-cache-cleanup', 'BIOS-vector'])
        assert cpu.r[15] == STACK and cpu.r[8:15] == result['initial'][8:15]
        original.append(dict(path='exit-' + str(index), instructions=cpu.instructions,
                             calls=cpu.calls, stores=len(cpu.writes)))
    for opened in (False, True):
        for entry, destination in ((owner.entries[2], 0x8c700000),
                                   (owner.entries[2], 0xac700000),
                                   (owner.exec_thunk, 0x8c008300)):
            cpu = Flow(source, owner, opened)
            result = cpu.execute(entry, [FILENAME, destination, 0x12345678])
            names = [call['name'] for call in cpu.calls]
            assert result['stop'] == 'original-return' and cpu.loaded_target == destination
            assert names == (['setup', 'open', 'size', 'read', 'close', 'cleanup', 'loaded-executable']
                             if opened else ['setup', 'open', 'cleanup', 'loaded-executable'])
            assert cpu.r[0] == 1
            if opened:
                assert cpu.block(destination, len(FILE_DATA)) == FILE_DATA
            original.append(dict(path='loader' if entry == owner.entries[2] else 'thunk',
                                 opened=opened, destination=hex(destination),
                                 instructions=cpu.instructions, calls=cpu.calls, stores=len(cpu.writes)))
    for target in (0x8c007200, 0xac007200):
        for entry in (*owner.entries[:3], owner.exec_thunk):
            cpu = Flow(source, owner, True, terminal=target)
            for address, encoded in patches(lib, owner, target):
                cpu.store_block(address, encoded)
            result = cpu.execute(entry, [FILENAME, 0x8c700000, 0x12345678])
            intercepted.append(terminal_result(cpu, result, owner, entry, target))
    # The execution oracle must distinguish a missing borrowed literal, a
    # thunk bypass, a changed return PR, and an unwanted delay-slot store.
    # These corrupt authored veneers only, not the supplied original image.
    rejections = []
    target = 0x8c007200
    for fault in ('missing-paired-veneer', 'missing-loader-veneer',
                  'JSR-instead-of-terminal-JMP', 'delay-slot-stack-write'):
        cpu = Flow(source, owner, True, terminal=target)
        for address, encoded in patches(lib, owner, target):
            if fault == 'missing-paired-veneer' and address == owner.entries[1]:
                continue
            if fault == 'missing-loader-veneer' and address == owner.entries[2]:
                continue
            cpu.store_block(address, encoded)
        entry = owner.entries[0] if fault == 'missing-paired-veneer' else owner.entries[2]
        if fault == 'missing-loader-veneer':
            entry = owner.exec_thunk
        elif fault == 'JSR-instead-of-terminal-JMP':
            cpu.store_block(entry + 2, struct.pack('<H', 0x400b))
        elif fault == 'delay-slot-stack-write':
            cpu.store_block(entry + 4, struct.pack('<H', 0x4f22))
        try:
            result = cpu.execute(entry, [FILENAME, 0x8c700000, 0x12345678])
            terminal_result(cpu, result, owner, entry, target)
        except AssertionError:
            rejections.append(fault)
        else:
            raise AssertionError('Execution proof accepted corrupt terminal veneer: ' + fault)
    # The generic proof must also refuse dangerous variants of this actual
    # image, rather than merely passing an independently assembled fixture.
    # Every mutation is temporary; no owner-supplied file is changed.
    discovery_refusals = []
    def changed_image(label, offset, replacement, accepted=False):
        changed = bytearray(source)
        changed[offset:offset + len(replacement)] = replacement
        buffer = ct.create_string_buffer(bytes(changed))
        result = Exit()
        found = lib.kui_vmu_native_exit_discover(buffer, len(changed), LOAD, ct.byref(result))
        assert found == accepted, label
        if accepted:
            assert bytes(result) == bytes(owner), label
        else:
            assert bytes(result) == bytes(ct.sizeof(Exit)), label
            discovery_refusals.append(label)
    boot_slot = owner.entries[2] + 0x70 - LOAD
    for bit in range(32):
        changed_image('bootstrap-literal-bit-' + str(bit), boot_slot,
                      struct.pack('<I', 0x8c008300 ^ (1 << bit)))
    for alias in (0, 0x80000000, 0xa0000000):
        for offset in (12, 30, 60, 78, 82, 84, 112):
            changed_image(f'outside-pointer-{alias:x}-{offset}', 0x100,
                          struct.pack('<I', alias | ((owner.entries[2] + offset) & 0x1fffffff)))
        for offset in (0, 76):
            changed_image(f'allowed-pointer-{alias:x}-{offset}', 0x100,
                          struct.pack('<I', alias | ((owner.entries[2] + offset) & 0x1fffffff)), True)
    for offset in (12, 30, 60, 78, 84, 112):
        at = owner.entries[2] - 0x80
        displacement = (owner.entries[2] + offset - at - 4) // 2
        changed_image('outside-branch-' + str(offset), at - LOAD,
                      struct.pack('<H', 0xa000 | (displacement & 0xfff)))
    at = owner.entries[2] - 8
    changed_image('outside-pool-MOV.L', at - LOAD,
                  struct.pack('<H', 0xd300 | ((owner.entries[2] + 84 - ((at + 4) & ~3)) // 4)))
    for offset in (78, 80):
        opcode = struct.unpack_from('<H', source, owner.entries[2] + offset - LOAD)[0]
        changed_image('malformed-thunk-' + str(offset), owner.entries[2] + offset - LOAD,
                      struct.pack('<H', opcode ^ 1))
    unchanged = Exit()
    assert lib.kui_vmu_native_exit_discover(image, len(source), LOAD, ct.byref(unchanged))
    assert bytes(unchanged) == bytes(owner)
    return dict(original=original, intercepted=intercepted, veneer_fault_rejections=rejections,
                actual_image_discovery_refusals=discovery_refusals,
                allowed_full_entry_and_thunk_pointer_aliases=6)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sonic-adventure', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    source = args.sonic_adventure.read_bytes()
    assert len(source) == 6751168 and hashlib.sha256(source).hexdigest() == OWNER_SHA
    with tempfile.TemporaryDirectory(prefix='early-compact-original-') as directory:
        result = proof(source, compile_api(Path(directory)))
    report = dict(schema='kui.native.early-compact.original-execution.v1',
                  source_name=args.sonic_adventure.name, source_bytes=len(source),
                  source_sha256=OWNER_SHA, **result,
                  scope='Original supplied pair/loader/thunk instructions plus production discovery, '
                        'header admission/window and encoded terminal veneers.',
                  explicit_model_boundaries=['File setup/open/size/read/close APIs',
                                             'Pair cache cleanup and platform cleanup APIs',
                                             'Loaded executable call and terminal LOW entry'],
                  limits=['No original file API/cleanup implementation or LOW report/hardware execution.',
                          'No earlier application caller, whole-game compatibility, or save durability proof.'],
                  proof_source_sha256={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                       for name in ('tests/test_vmu_native_early_compact_execution.py',
                                                    'tests/test_vmu_native_heap_execution.py',
                                                    'src/core/vmu_native_exit.c',
                                                    'include/kui/vmu_native_exit.h',
                                                    'src/core/vmu_native_patch.c')})
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS actual original early/compact exits: 8 original paths, 8 production-patched '
          'entry/thunk paths, 4 veneer-fault refusals and '
          f'{len(result["actual_image_discovery_refusals"])} actual-image discovery refusals; '
          'file/cleanup/target ordering and zero-store terminal interception')


if __name__ == '__main__':
    main()
