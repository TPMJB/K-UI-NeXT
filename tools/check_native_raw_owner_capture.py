#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked PROFILE terminal before its first display write.

The model executes the actual diagnostic completion/capture instructions,
including their bounds and original-alias reads. It stops at display_restore
before that function can write video hardware. RAM aliases are coherent in
this integer model; no cache, timing, game-PC or console claim follows.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from profile_vmu_native_release_sh import ReleaseSH
from test_native_dual_readers import object_section
from test_vmu_native_heap_execution import MASK, signed
from sh4_delay_slots import validate_delay_slot

SCHEMA = 'kui.native.raw-owner-terminal.audit.v1'
MARKER = '__kui_native_raw_owner_terminal_worker'
PINS = {
    '_diagnostic_complete': (3610, 'a7625ca1340577ff5f25b141afd8c6f234e033f50c33c47d3a2b55f5358ab437'),
    '_native_phase_span_from': (128, '2f2d811580469162b3f1b715592e0cf3c96f78a0ee86ac80b6bd7b9a1be09d1f'),
    '_diagnostic_phase_read': (136, '300072e2b8bad5d5aa1ac8e52af5bfe1c52c1b256541ff3281f449642641533a'),
    '_diagnostic_end': (1492, 'd211408c02c00be15410e700b2d8314c2a87da093dfead3936986807aef7ccfd'),
    '_diagnostic_sample': (164, '50b9c67715a1d674f12e8e951d6feb6f41b892fa3d8d785fc6302bc2458f21c3'),
    '_memset': (26, '84a19f7aa0536a77835dd9917cd47ea65e6d6edf6013064ab39a36c0a05e118a'),
    '_memcpy': (72, 'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
}


def code_pins(worker):
    from check_native_followup_profile import bottom_worker_profile, worker_pins
    return worker_pins(worker, 'raw_owner', PINS) if bottom_worker_profile(worker) else PINS


DISPLAY_PINS = {
    'src/loader/vmu_native_passive.c': (32, '0a86992205c28bd335dd6f39422e9037bae6d5094494570ed1c4e07c7121afa3'),
    'src/loader/vmu_native_reader_capture.inc': (24, '658c1d666a07cd72d56f0a6fd7659e863e3bd6309f52b07c94866d827ef3b2c3'),
    'src/loader/vmu_native_loader_timeline.inc': (6, '223a6868c578044c2b9ee2e3af1b97adfd8427d59b771efa3af84ff5c89eaabe'),
}
SOURCES = ('src/loader/vmu_native_phase.inc', 'src/loader/vmu_native_passive.c',
           'src/loader/vmu_native_reader_capture.inc', 'src/loader/vmu_native_loader_timeline.inc',
           'tests/test_vmu_native_phase.c', 'tools/check_native_raw_owner_capture.py')
FIELDS = {
    'config.bytes': 'sizeof(config)',
    'state.bytes': 'sizeof(state)',
    'phase.bytes': 'sizeof(diagnostic_phase)',
    'raw.bytes': 'sizeof(diagnostic_phase.raw)',
    **{'config.' + x: 'offsetof(__typeof__(config),' + x + ')'
       for x in ('diagnostics', 'lease.payload', 'lease.lease_bytes')},
    **{'state.' + x: 'offsetof(__typeof__(state),' + x + ')'
       for x in ('completed', 'frame_address', 'frame_pr', 'frame_sr')},
    **{'diagnostic.' + x: 'offsetof(__typeof__(diagnostic),' + x + ')'
       for x in ('active', 'capture_poll_start', 'clock_last')},
    **{'phase.' + x: 'offsetof(struct native_phase_state,' + x + ')'
       for x in ('enabled', 'raw', 'raw.frame', 'raw.words', 'raw.flags', 'raw.data')},
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def layout():
    compiler = shutil.which('sh-elf-gcc')
    require(compiler is not None, 'raw observer layout requires the target compiler')
    with tempfile.TemporaryDirectory(prefix='kui-raw-owner-layout-') as temporary:
        source, target = Path(temporary) / 'layout.c', Path(temporary) / 'layout.o'
        source.write_text('#include <stddef.h>\n'
            '#define KUI_VMU_NATIVE_PASSIVE_HOST_TEST 1\n'
            '#define KUI_VMU_NATIVE_READONLY_PROBE 1\n'
            '#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1\n'
            '#include "' + str(ROOT / 'src/loader/vmu_native_passive.c') + '"\n'
            'const uint32_t raw_layout[] __attribute__((section(".rodata.raw_layout"),used))={' +
            ','.join(FIELDS.values()) + '};\n')
        subprocess.run([compiler, '-std=c11', '-Os', '-m4-single', '-ml', '-ffreestanding',
                        '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src/loader'),
                        '-c', str(source), '-o', str(target)],
                       check=True, capture_output=True)
        data = object_section(target, '.rodata.raw_layout')
    require(len(data) == len(FIELDS) * 4, 'raw observer target layout is incomplete')
    result = dict(zip(FIELDS, struct.unpack('<' + 'I' * len(FIELDS), data)))
    require(result['phase.bytes'] == 908 and result['raw.bytes'] == 812 and result['state.bytes'] == 100,
            'raw observer private representation changed')
    return result


class Terminal(ReleaseSH):
    VIDEO = {0xa05f80d8: 0x01060359, 0xa05f80cc: 0x00150104,
             0xa05f80d0: 0x100, 0xa05f810c: 260, 0xff000028: 0x320}
    CLOCK = {0xffc00000: 0xe0a, 0xffd80008: MASK, 0xffd80010: 2,
             0xffd80004: 1, 0xffd8000c: 0xfff00000, 0xff00001c: 0x105,
             0xffd00008: 0, 0xffa0001c: 0, 0xffa00018: 0,
             0xffa00040: 0, 0xffe00010: 0}

    def __init__(self, path, offsets, frame, *, mode=1, phase_enabled=0,
                 lease=0x0c100000, lease_bytes=0x60000, mutation=None):
        self.observing = False
        self.raw_started = False
        self.video_reads, self.owner_reads, self.entered = [], [], set()
        super().__init__(path)
        self.o, self.frame = offsets, frame
        self.worker = self.elf
        self.reader_begin, self.reader_bytes = self.symbols['_diagnostic_phase_read'][:2]
        self.bus_address = None
        self.phase = self.symbols['_diagnostic_phase'][0]
        self.config = self.symbols['_config'][0]
        self.state = self.symbols['_state'][0]
        self.diagnostic = self.symbols['_diagnostic'][0]
        require(self.symbols['_config'][1] == offsets['config.bytes'] and
                self.symbols['_diagnostic_phase'][1] == offsets['phase.bytes'],
                'raw terminal object sizes differ from the compiler layout')
        self.span, self.stack = 0xac11f100, 0xac11fff0
        self.store_block(self.span, struct.pack('<6I', 0xe0a, MASK, 2, 1, 0xfff00020, 1))
        self.put(self.config, 'config.diagnostics', mode)
        self.put(self.config, 'config.lease.payload', lease)
        self.put(self.config, 'config.lease.lease_bytes', lease_bytes)
        self.put(self.state, 'state.completed', 1200)
        self.put(self.state, 'state.frame_address', frame)
        self.put(self.state, 'state.frame_pr', 0x8c0d8a22)
        self.put(self.state, 'state.frame_sr', 0x60000160)
        self.put(self.diagnostic, 'diagnostic.active', 1)
        self.put(self.diagnostic, 'diagnostic.capture_poll_start', 0)
        self.put(self.phase, 'phase.enabled', phase_enabled)
        self.expected_words = [(0x12345678 ^ (i * 0x01010101)) & MASK for i in range(192)]
        self.expected_words[3], self.expected_words[17], self.expected_words[100] = (
            0x8c0d8a22, 0x60000160, 0x8c010e86)
        physical = frame & 0x1fffffff
        if frame & 0xe0000000 in (0, 0x80000000, 0xa0000000) and (
                0x0c008100 <= physical <= 0x0d000000 - 768) and not physical & 3 and (
                physical + 768 <= 0x0c0fffe0 or physical >= 0x0c160000):
            self.store_block(frame, struct.pack('<192I', *self.expected_words))
        if mutation is not None:
            address, word = mutation
            self.store_block(address, struct.pack('<H', word))
        self.r[15], self.r[4:7] = self.stack, [self.span, 0, 1]
        self.pc, self.pr = self.symbols['_diagnostic_complete'][0], 0xdeadbeee
        self.sr = 0x600000f0
        self.instructions = 0
        self.observing = True
        self.writes.clear()

    def put(self, base, field, value):
        super().write(base + self.o[field], value)

    def read(self, address, count=4):
        if address in self.VIDEO:
            if self.observing:
                self.video_reads.append((address, count))
            return self.VIDEO[address]
        if address in self.CLOCK:
            return self.CLOCK[address]
        if (self.observing and count == 4 and self.reader_begin <= self.pc <
                self.reader_begin + self.reader_bytes and address == self.bus_address) and (
                self.frame & 0x1fffffff) <= (address & 0x1fffffff) < (self.frame & 0x1fffffff) + 768:
            self.owner_reads.append(address)
        return super().read(address, count)

    def write(self, address, value, count=4):
        if self.observing:
            at = address & 0x1fffffff
            if at == self.phase & 0x1fffffff and value == 2:
                self.raw_started = True
            private = (self.phase & 0x1fffffff) <= at and at + count <= (
                self.phase & 0x1fffffff) + self.o['phase.bytes']
            stack = 0x0c11f000 <= at and at + count <= 0x0c120000
            require((private or stack) if self.raw_started else (
                    0x0c100000 <= at and at + count <= 0x0c160000),
                    f'raw terminal wrote outside its private worker/stack: {address:#x} at {self.pc:#x}')
        return super().write(address, value, count)

    def plain(self, op, pc):
        if op & 0xf00f in (0x3005, 0x300d):  # DMULU.L / DMULS.L Rm,Rn
            n, m = op >> 8 & 15, op >> 4 & 15
            a, b = self.r[n], self.r[m]
            if op & 15 == 0xd:
                a, b = signed(a, 32), signed(b, 32)
            value = (a * b) & ((1 << 64) - 1)
            self.mach, self.macl = value >> 32, value & MASK
            self.executing = pc
            self.instructions += 1
            return
        if op & 0xf0ff == 0x000a:  # STS MACH,Rn
            self.r[op >> 8 & 15] = self.mach
            self.executing = pc
            self.instructions += 1
            return
        if op & 0xf0ff in (0x4018, 0x4019):  # SHLL8 / SHLR8 Rn
            n = op >> 8 & 15
            self.r[n] = (self.r[n] << 8) & MASK if op & 255 == 0x18 else self.r[n] >> 8
            self.executing = pc
            self.instructions += 1
            return
        if op & 0xf0ff == 0x0022:  # STC VBR,Rn (observation only)
            self.r[op >> 8 & 15] = 0x8c00f400
            self.executing = pc
            self.instructions += 1
            return
        if op & 0xf00f == 0x6008:  # SWAP.B Rm,Rn
            n, m = op >> 8 & 15, op >> 4 & 15
            value = self.r[m]
            self.r[n] = (value & 0xffff0000) | ((value & 255) << 8) | ((value >> 8) & 255)
            self.executing = pc
            self.instructions += 1
            return
        if op & 0xf0ff in (0x4011, 0x4015):  # CMP/PZ or CMP/PL Rn
            value = signed(self.r[op >> 8 & 15], 32)
            self.flag(value >= 0 if op & 0xff == 0x11 else value > 0)
            self.executing = pc
            self.instructions += 1
            return
        super().plain(op, pc)

    def run_prefix(self):
        restore = self.symbols['_retail_display_restore'][0]
        entries = {value[0]: name for name, value in self.symbols.items() if value[2] == 2}
        for _ in range(200000):
            pc = self.pc
            if pc == restore:
                require(self.sr & 0xf0 == 0xf0, 'raw capture lost interrupt exclusion')
                self.observing = False
                return
            if pc in entries:
                self.entered.add(entries[pc])
                if pc == self.reader_begin:
                    self.bus_address = self.r[5]
                require(not any(part in entries[pc] for part in ('_sd_', '_durable_', '_async_irq')),
                        'raw terminal called a storage/reader entry')
            op = self.read(pc, 2)
            target, delay, n = None, False, (op >> 8) & 15
            if op >> 12 in (0xa, 0xb):
                target, delay = pc + 4 + 2 * signed(op & 0xfff, 12) & MASK, True
                if op >> 12 == 0xb:
                    self.pr = pc + 4
            elif op & 0xf0ff in (0x400b, 0x402b):
                target, delay = self.r[n], True
                if op & 0xf0ff == 0x400b:
                    self.pr = pc + 4
            elif op == 0x000b:
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
                    validate_delay_slot(slot, pc + 2)
                    self.plain(slot, pc + 2)
                self.pc = target & MASK
        raise ValueError('raw terminal prefix exceeded its instruction bound')

    def raw_words(self):
        return [super(Terminal, self).read(self.phase + self.o['phase.raw.data'] + 4 * i)
                for i in range(192)]


def scalar_truth():
    """Independent edge cases for the extra decoded scalar instructions."""
    values = (0, 1, 0x7f, 0x80, 0xff, 0x100, 0x12345678,
              0x7fffffff, 0x80000000, 0xfffffffe, MASK)
    cases = 0
    cpu = Terminal.__new__(Terminal)
    cpu.observing = False
    for value in values:
        for opcode, expected, change_t in (
                (0x6358, (value & 0xffff0000) | ((value & 255) << 8) | ((value >> 8) & 255), False),
                (0x4318, value << 8 & MASK, False),
                (0x4319, value >> 8, False),
                (0x4311, value, True), (0x4315, value, True)):
            cpu.r = [0xabcde000 + i for i in range(16)]
            cpu.r[3] = cpu.r[5] = value
            before = cpu.r.copy()
            cpu.sr, cpu.instructions = 0x600000f1, 0
            cpu.plain(opcode, 0x1000)
            require(cpu.r[3] == expected and cpu.r[:3] == before[:3] and cpu.r[4:] == before[4:],
                    'raw interpreter scalar register truth differs')
            expected_t = int((signed(value, 32) >= 0) if opcode & 255 == 0x11 else
                             (signed(value, 32) > 0)) if change_t else 1
            require(cpu.sr == (0x600000f0 | expected_t), 'raw interpreter scalar T truth differs')
            cases += 1
    for a in values:
        for b in values:
            for opcode, is_signed in ((0x3355, False), (0x335d, True)):
                cpu.r = [0xabcde000 + i for i in range(16)]
                cpu.r[3], cpu.r[5] = a, b
                before = cpu.r.copy()
                cpu.sr, cpu.instructions = 0x600000f1, 0
                cpu.plain(opcode, 0x1000)
                product = (signed(a, 32) * signed(b, 32) if is_signed else a * b) & ((1 << 64) - 1)
                require(cpu.r == before and cpu.sr == 0x600000f1 and
                        cpu.mach == product >> 32 and cpu.macl == product & MASK,
                        'raw interpreter double multiply truth differs')
                cases += 1
    for n in range(16):
        cpu.r = [0xabcde000 + i for i in range(16)]
        before = cpu.r.copy()
        cpu.sr, cpu.instructions, cpu.mach = 0x600000f1, 0, 0x89abcdef
        cpu.plain(0x0022 | n << 8, 0x1000)
        expected = before.copy();expected[n] = 0x8c00f400
        require(cpu.r == expected and cpu.sr == 0x600000f1, 'raw interpreter VBR observation truth differs')
        cpu.r = before.copy();cpu.plain(0x000a | n << 8, 0x1000)
        expected[n] = 0x89abcdef
        require(cpu.r == expected and cpu.sr == 0x600000f1, 'raw interpreter MACH observation truth differs')
        cases += 2
    return cases


def host_checks():
    with tempfile.TemporaryDirectory(prefix='kui-raw-owner-host-') as temporary:
        target = Path(temporary) / 'test-phase'
        subprocess.run([os.environ.get('HOST_CC', 'cc'), '-std=c11', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-fno-pie', '-no-pie', '-I' + str(ROOT / 'include'),
                        str(ROOT / 'tests/test_vmu_native_phase.c'),
                        str(ROOT / 'src/core/vmu_native_profile.c'), '-o', str(target)],
                       check=True, capture_output=True)
        result = subprocess.run([str(target)], check=True, capture_output=True, text=True,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
    match = re.search(r'Native optional phase capture: (\d+) checks;', result.stdout)
    require(match is not None and int(match[1]) == 99169,
            'raw owner host sanitizer proof has changed scope')
    return dict(checks=int(match[1]), sanitizers=['address', 'undefined'],
                output=result.stdout, output_sha256=sha(result.stdout.encode()))


def numeric_display_rows():
    """Keep the old numeric rows while captions and private layout change.

    Golden hashes come from the separately retained f241 source. Only the new
    terminal raw block is omitted; the two typed E1 array slices are expanded
    to their original logical fields, independently checked by the C suite.
    """
    result = {}
    for name, expected in DISPLAY_PINS.items():
        source = (ROOT / name).read_text()
        source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
        start = source.find('if(diagnostic_phase.enabled==NATIVE_RAW_OWNER_MODE)')
        if start >= 0:
            end = source.index('{', start) + 1;depth = 1
            while depth:
                if source[end] == '{':depth += 1
                elif source[end] == '}':depth -= 1
                end += 1
            source = source[:start] + source[end:]
        source = source.replace('diagnostic_phase_row(r->words+8u)',
            'diagnostic_phase_row((uint32_t[5]){r->pc,r->pr,r->sr,r->event,r->video_counter})')
        source = source.replace('diagnostic_phase_row(r->words+3u)',
            'diagnostic_phase_row((uint32_t[5]){r->load,r->vblank,r->control,r->tick_begin,r->tick_end})')
        source = re.sub(r'"(?:\\.|[^"\\])*"', 'STR', source)
        rows = []
        for match in re.finditer(r'\b(retail_display_values|retail_display_hex|diagnostic_phase_row)\s*\(', source):
            start = end = match.end();depth = 1
            while depth:
                if source[end] == '(':depth += 1
                elif source[end] == ')':depth -= 1
                end += 1
            if source[end:].lstrip().startswith('{'):
                continue  # a function declaration, not a report row
            args = source[start:end-1]
            if match[1] != 'diagnostic_phase_row':
                nesting = 0
                for index, char in enumerate(args):
                    if char in '({[':nesting += 1
                    elif char in ')}]':nesting -= 1
                    elif char == ',' and not nesting:
                        args = args[index+1:];break
            rows.append(match[1] + ':' + re.sub(r'\s+', '', args))
        digest = sha('\n'.join(rows).encode())
        require((len(rows), digest) == expected, 'existing numerical report row changed: ' + name)
        result[name] = dict(calls=len(rows), sha256=digest)
    return result


def run_case(path, offsets, case, *, mutation=None):
    frame, mode, enabled, accept, lease, count = case
    cpu = Terminal(path, offsets, frame, mode=mode, phase_enabled=enabled,
                   lease=lease, lease_bytes=count, mutation=mutation)
    cpu.run_prefix()
    captured_mode = cpu.read(cpu.phase + offsets['phase.enabled'])
    words = cpu.read(cpu.phase + offsets['phase.raw.words'])
    flags = cpu.read(cpu.phase + offsets['phase.raw.flags'])
    if accept:
        require(captured_mode == 2 and words == 192 and flags == 7,
                'actual terminal did not capture a complete admitted snapshot')
        require(cpu.owner_reads == [frame + 4 * i for i in range(192)],
                'actual raw stack read count/order/original alias differs')
        require(cpu.raw_words() == cpu.expected_words, 'actual raw stack payload differs')
        require(cpu.video_reads == [(address, 4) for address in Terminal.VIDEO],
                'actual raw video snapshot reads differ')
    else:
        require(not cpu.owner_reads and not cpu.video_reads,
                f'unavailable/non-PROFILE terminal sampled owner or PVR state: '
                f'{frame:#x}, mode{mode}, lease{lease:#x}/{count:#x}')
        require(not words and not flags and captured_mode == (2 if mode == 1 else 0),
                'unavailable terminal incorrectly claims a completed snapshot')
    return cpu


def require_companion(worker, proof):
    present = worker[4].get(MARKER)
    if present is None:
        require(proof is None or (isinstance(proof, dict) and proof.get('enabled') is False),
                'unmarked worker cannot claim a raw terminal companion')
        return False
    require(present == (1, 0, 0, 0xfff1) and isinstance(proof, dict) and
            proof.get('schema') == SCHEMA and proof.get('enabled') is True and
            proof.get('artifact_sha256') == sha(worker[0]) and proof.get('instruction_cases') == 40,
            'raw terminal companion lacks matching actual worker instruction evidence')
    required = ('original_alias_order_and_full_192_words', 'low_stack_and_upper_ram_boundaries',
                'complete_lease_guard_exclusion', 'unavailable_and_non_profile_zero_owner_or_video_reads',
                'capture_before_first_display_restore', 'private_worker_writes_only',
                'no_storage_or_reader_callback', 'full_guard_length_overflow_unavailable')
    require(all(proof.get(name) is True for name in required),
            'raw terminal companion omits an observation-only boundary')
    target_layout = proof.get('target_layout')
    require(isinstance(target_layout, dict) and target_layout.get('phase.bytes') == 908 and
            target_layout.get('raw.bytes') == 812 and target_layout.get('phase.raw.data') == 68 and
            proof.get('scalar_instruction_truth_cases') == 329,
            'raw terminal companion representation/scalar truth differs')
    host = proof.get('host_sanitizer')
    require(isinstance(host, dict) and host.get('checks') == 99169 and
            host.get('sanitizers') == ['address', 'undefined'] and
            isinstance(host.get('output'), str) and
            host.get('output_sha256') == sha(host['output'].encode()),
            'raw terminal companion omits the matching host sanitizer observations')
    require(proof.get('source_sha256') == {name: sha((ROOT / name).read_bytes()) for name in SOURCES},
            'raw terminal companion source identities differ')
    require(proof.get('preserved_numeric_display_rows') == {
            name: dict(calls=count, sha256=digest) for name, (count, digest) in DISPLAY_PINS.items()},
            'raw terminal companion omits the original numeric report rows')
    pins = proof.get('exact_code_pins', {})
    selected = code_pins(worker)
    require(isinstance(pins, dict) and set(pins) == set(selected), 'raw terminal companion code scope differs')
    for name, expected in selected.items():
        address, body, _ = symbol_bytes(worker, name, 2)
        require((len(body), sha(body)) == expected and pins[name] == dict(
                address=hex(address), bytes=len(body), sha256=sha(body)),
                'raw terminal companion complete code/literal identity differs')
    return True


def check(directory, self_test=False):
    directory = Path(directory)
    path = directory / 'native-worker.elf'
    worker = read_elf(path)
    present = worker[4].get(MARKER)
    if present is None:
        return dict(schema=SCHEMA, enabled=False)
    require(present == (1, 0, 0, 0xfff1), 'raw terminal marker must be exact absolute one')
    pins = {}
    for name, expected in code_pins(worker).items():
        address, body, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6 and (len(body), sha(body)) == expected,
                'raw terminal complete code/literal pin differs: ' + name)
        pins[name] = dict(address=hex(address), bytes=len(body), sha256=sha(body))
    offsets = layout()
    scalar_cases = scalar_truth()
    cases = []
    for alias in (0, 0x80000000, 0xa0000000):
        for physical in (0x0c008100, 0x0c00f224, 0x0c300000, 0x0c0ffce0,
                         0x0c160000, 0x0d000000 - 768):
            cases.append((physical | alias, 1, 0, True, 0x0c100000, 0x60000))
    for frame in (0, 1, 0x4c00f224, 0x8c007d00, 0x8c0080fc, 0x8c00f225,
                  0x8d000000 - 764, 0x8d000000, MASK, 0x8c0fffe0, 0x8c15fffc):
        cases.append((frame, 1, 0, False, 0x0c100000, 0x60000))
    for lease, count in ((0, 0x60000), (28, 0x60000), (0x0d000000, 1),
                         (0x0c100000, MASK), (0x0c100000, MASK-31),
                         (0x0c100000, MASK-32), (0x0c100000, 0x1000000)):
        cases.append((0x8c00f224, 1, 0, False, lease, count))
    for mode in (0, 2, 3, 4):
        cases.append((0x8c00f224, mode, 0, False, 0x0c100000, 0x60000))
    calls = set()
    for case in cases:
        cpu = run_case(path, offsets, case)
        calls.update(cpu.entered)
    result = dict(schema=SCHEMA, enabled=True, artifact_sha256=sha(worker[0]),
                exact_code_pins=pins, target_layout=offsets, instruction_cases=len(cases),
                scalar_instruction_truth_cases=scalar_cases,
                host_sanitizer=host_checks(),
                preserved_numeric_display_rows=numeric_display_rows(),
                original_alias_order_and_full_192_words=True,
                low_stack_and_upper_ram_boundaries=True, complete_lease_guard_exclusion=True,
                unavailable_and_non_profile_zero_owner_or_video_reads=True,
                capture_before_first_display_restore=True, private_worker_writes_only=True,
                no_storage_or_reader_callback=True, executed_functions=sorted(calls),
                full_guard_length_overflow_unavailable=True,
                source_sha256={name: sha((ROOT / name).read_bytes()) for name in SOURCES},
                limits=['Coherent integer RAM/MMIO model; no cache or hardware timing claim.',
                        'Raw stack words are observations, not a proved interrupted game PC.',
                        'Existing C sanitizer suite separately injects every read failure.',
                        'Completion timing observations before raw capture retain their existing owned-region counter writes.',
                        'The diagnostic entry is forced for non-PROFILE cases to test that raw sampling remains disabled; normal caller gating is independently covered by worker tests.',
                        'Video words are SPG_LOAD, SPG_VBLANK, SPG_CONTROL, SPG_STATUS and INTEVT; framebuffer enable FB_R_CTRL is not sampled.'])
    require_companion(worker, result)
    if self_test:
        refusals = 0
        for name in ('artifact_sha256', 'schema', 'instruction_cases', 'private_worker_writes_only',
                     'complete_lease_guard_exclusion', 'capture_before_first_display_restore',
                     'full_guard_length_overflow_unavailable', 'source_sha256', 'host_sanitizer',
                     'target_layout', 'scalar_instruction_truth_cases', 'exact_code_pins',
                     'preserved_numeric_display_rows'):
            changed = copy.deepcopy(result);changed[name] = None
            try:
                require_companion(worker, changed)
            except ValueError:
                refusals += 1
            else:
                raise ValueError('raw terminal accepted a missing companion obligation: ' + name)
        byte_pins = 0
        for name, expected in code_pins(worker).items():
            _, body, _ = symbol_bytes(worker, name, 2)
            for index in range(len(body)):
                changed = bytearray(body);changed[index] ^= 1
                require((len(changed), sha(changed)) != expected,
                        'raw terminal complete-byte pin missed a mutation')
                byte_pins += 1
        address, body, _ = symbol_bytes(worker, '_native_phase_span_from', 2)
        branch_mutants = 0
        for index in range(0, len(body), 2):
            word = struct.unpack_from('<H', body, index)[0]
            if word >> 8 not in (0x89, 0x8b, 0x8d, 0x8f):
                continue
            detected = False
            for case in cases:
                try:
                    run_case(path, offsets, case, mutation=(address + index, word ^ 0x0200))
                except (ValueError, AssertionError):
                    detected = True;break
            require(detected, 'raw terminal accepted a span-guard branch inversion at ' + hex(index))
            branch_mutants += 1
        result['self_tests'] = dict(companion_refusals=refusals,
                                   complete_body_byte_pin_mutants=byte_pins,
                                   span_guard_branch_inversions=branch_mutants)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--host-log', type=Path)
    args = parser.parse_args()
    result = check(args.directory, self_test=args.self_test)
    if args.host_log and result.get('enabled'):
        args.host_log.write_text(result['host_sanitizer']['output'])
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
