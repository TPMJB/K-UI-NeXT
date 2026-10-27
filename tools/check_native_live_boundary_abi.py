#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual worker live-boundary caller and its private-driver ABI.

Only live_prepare/live_check are ABI-adversarial substitutes here. Their
validators are independently covered by production C tests and the owned
timer instruction proof. This model supplies no game, timer or storage calls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
from check_vmu_native_stage_stack import Machine, canonical
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from check_native_controller_failure_witness import owned_timer_profile
from test_native_dual_readers import object_section

PIN = (216, '96c97d3de94a0a04b7d45e736b0e4e5405fd2eeb8f7be1da8962389eacfe721f')
FIELDS = {
    **{'config.' + x: 'offsetof(struct kui_vmu_native_passive_config,' + x + ')'
       for x in ('tx_base', 'driver', 'layout', 'runtime')},
    **{'frame.' + x: 'offsetof(struct kui_vmu_native_passive_frame,' + x + ')'
       for x in ('sr', 'pr')},
    **{'snapshot.' + x: 'offsetof(struct kui_vmu_native_runtime_snapshot,' + x + ')'
       for x in ('tx_base',)},
    **{'live.' + x: 'offsetof(struct kui_vmu_native_live,' + x + ')'
       for x in ('ready',)},
    **{'diagnostic.' + x: 'offsetof(struct kui_vmu_native_layout_diagnostic,' + x + ')'
       for x in ('code', 'address', 'value', 'expected')},
    'diagnostic.bytes': 'sizeof(struct kui_vmu_native_layout_diagnostic)',
    'boundary.bytes': 'sizeof(struct kui_vmu_native_runtime_boundary)',
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def layout():
    compiler = shutil.which('sh-elf-gcc')
    require(compiler is not None, 'live-boundary ABI layout requires sh-elf-gcc')
    with tempfile.TemporaryDirectory(prefix='native-live-abi-layout-') as temporary:
        source, target = Path(temporary) / 'layout.c', Path(temporary) / 'layout.o'
        source.write_text('#include <stddef.h>\n#include "kui/vmu_native_passive.h"\n'
                          '#include "kui/vmu_native_live.h"\n'
                          'const uint32_t fields[] __attribute__((section(".rodata.live_abi"),used))={' +
                          ','.join(FIELDS.values()) + '};\n')
        subprocess.run([compiler, '-std=c11', '-Os', '-m4-single', '-ml', '-ffreestanding',
                        '-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1', '-I' + str(ROOT / 'include'),
                        '-c', str(source), '-o', str(target)], check=True, capture_output=True)
        raw = object_section(target, '.rodata.live_abi')
    require(len(raw) == 4 * len(FIELDS), 'live ABI console layout changed')
    result = dict(zip(FIELDS, struct.unpack('<' + 'I' * len(FIELDS), raw)))
    require(result['boundary.bytes'] == 28 and result['diagnostic.bytes'] == 16,
            'live ABI boundary/diagnostic record sizes changed')
    return result


def pinned(worker):
    _, body, section = symbol_bytes(worker, '_live_boundary', 2)
    from check_native_followup_profile import worker_pins
    expected=worker_pins(worker,'live_boundary',PIN)
    require(section[2] & 7 == 6 and (len(body), hashlib.sha256(body).hexdigest()) == expected,
            'live-boundary caller complete instruction/literal pin changed')
    return body


class LiveBoundary(Machine):
    def __init__(self, worker, offsets, ready, result, bad_tx, diagnostic, seed, mask,
                 phase, *, body=None):
        super().__init__(worker, '_live_boundary', seed, mask, 0x8c11fff0, body=body)
        self.offsets, self.ready, self.result = offsets, ready, result
        self.bad_tx, self.diagnostic = bad_tx, diagnostic
        self.symbols = worker[4]
        self.config, self.plan, self.state = (self.symbols[n][0]
                                              for n in ('_config', '_live_plan', '_state'))
        require(self.symbols['_state'][1] == 100, 'live ABI state object size changed')
        self.frame, self.snapshot = 0x8c11f200, 0x8c11fe80
        self.phase, self.dma, self.count = phase, 0x0c440000, 0x54
        self.source, self.tx, self.inherited_sr, self.inherited_pr = (
            0x0c220000, 0xac220000, 0x60000060 | (seed & 1), 0x8c12abcd)
        self.r[4:8] = [self.frame, phase, self.dma, self.count]
        self.set(self.start_sp, self.snapshot)
        self.set(self.frame + offsets['frame.sr'], self.inherited_sr)
        self.set(self.frame + offsets['frame.pr'], self.inherited_pr)
        self.set(self.config + offsets['config.tx_base'], self.tx)
        self.set(self.plan + offsets['live.ready'], 0x4e4c5031 if ready else 0)
        self.set(self.state + 20, self.source)
        for index in range(4):
            self.set(self.state + 64 + 4 * index, 0xa5010000 + index)
        self.initial_faults = [self.read(self.state + 64 + 4 * index) for index in range(4)]
        self.set(self.snapshot + offsets['snapshot.tx_base'], 0xf1f2f3f4)
        self.input_sr = self.sr
        self.owner_writes = []
        self.before = self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()

    def set(self, address, value):
        self.mem[canonical(address)] = value & 0xffffffff

    def write(self, address, value):
        require(not address & 3, 'live ABI caller emitted unaligned store')
        private_frame = self.start_sp - 80 <= address < self.start_sp
        status = address == self.state + 76
        fault = self.state + 64 <= address < self.state + 76
        require(private_frame or status or fault, 'live ABI caller writes outside private frame/status')
        if not private_frame:
            self.owner_writes.append(address)
        self.set(address, value)

    def plain(self, word, pc):
        if word & 0xf00f == 0x000e:  # MOV.L @(R0,Rm),Rn
            n, m = word >> 8 & 15, word >> 4 & 15
            self.r[n] = self.read((self.r[0] + self.r[m]) & 0xffffffff)
            return
        super().plain(word, pc)

    def boundary(self, address):
        require(self.start_sp - 80 <= address and address + 28 <= self.start_sp,
                'live ABI boundary leaves the exact caller frame')
        values = [self.read(address + 4 * index) for index in range(7)]
        require(values == [self.phase, self.input_sr, self.inherited_sr, self.inherited_pr,
                           self.source, self.dma, self.count],
                'live ABI boundary fields changed or arguments shifted')

    def call(self, target):
        if target == self.stop_pc or self.base <= target < self.base + len(self.blob):
            return False
        require(not self.calls, 'live boundary invokes multiple validators')
        prepare = self.symbols['_kui_vmu_native_live_prepare'][0]
        check = self.symbols['_kui_vmu_native_live_check'][0]
        require(target == (check if self.ready else prepare),
                'live boundary calls outside the exact selected validator')
        if self.ready:
            require(self.r[4] == self.plan and
                    self.r[5] == self.config + self.offsets['config.driver'] and
                    self.r[7] == self.symbols['_boundary_read'][0],
                    'live_check register ABI lost its private driver/reader')
            self.boundary(self.r[6])
            require([self.read(self.r[15] + 4 * index) for index in range(2)] == [0, self.snapshot],
                    'live_check stack ABI lost context/snapshot')
        else:
            require(self.r[4:8] == [self.plan, self.config + self.offsets['config.layout'],
                                    self.config + self.offsets['config.driver'],
                                    self.config + self.offsets['config.runtime']],
                    'live_prepare register ABI changed')
            args = [self.read(self.r[15] + 4 * index) for index in range(5)]
            self.boundary(args[0])
            require(args[1:4] == [self.symbols['_boundary_read'][0], 0, self.snapshot],
                    'live_prepare stack ABI changed')
            diagnostic = args[4]
            require(self.start_sp - 80 <= diagnostic and diagnostic + 16 <= self.start_sp,
                    'live diagnostic leaves its exact caller frame')
            require(all(self.read(diagnostic + 4 * index) == 0 for index in range(4)),
                    'live_prepare diagnostic was not zeroed')
            for name, value in zip(('code', 'address', 'value', 'expected'),
                                   (self.diagnostic, 0x8c234500, 0x12345678, 0x87654321)):
                self.set(diagnostic + self.offsets['diagnostic.' + name], value)
        self.set(self.snapshot + self.offsets['snapshot.tx_base'], self.tx ^ int(self.bad_tx))
        self.calls.append('live_check' if self.ready else 'live_prepare')
        self.abi_return(self.result)
        return True

    def check(self):
        self.run()
        registers, pr, mach, macl, gbr, sr, fpscr, fr = self.before
        require(self.r[8:] == registers[8:] and self.pr == pr and
                (self.mach, self.macl, self.gbr, self.sr & ~1, self.fpscr, self.fr) ==
                (mach, macl, gbr, sr & ~1, fpscr, fr), 'live ABI caller damaged preserved state')
        expected = self.result == 0 and not self.bad_tx
        require(self.r[0] == int(expected) and len(self.calls) == 1,
                'live ABI caller accepted a failed validator or mismatched TX')
        faults = [self.read(self.state + 64 + 4 * index) for index in range(4)]
        if not self.ready and self.result == -1 and self.diagnostic:
            require(faults == [0x8c234500, 0x12345678, 0x87654321, 0x10000 | self.diagnostic],
                    'live_prepare diagnostic refusal was not retained')
        else:
            require(faults[:3] == self.initial_faults[:3] and faults[3] == self.result & 0xffffffff,
                    'live ABI status write changed unrelated fault state')
        return self


def check(worker, self_test=False):
    if not owned_timer_profile(worker):
        return dict(enabled=False)
    body = pinned(worker)
    offsets = layout()
    cases = 0
    for ready in (False, True):
        for result in (0, -1, -2, -3, -4, -6, 1):
            for bad_tx in (False, True):
                for diagnostic in (0, 7):
                    for phase in (0, 1):
                        for mask in (0, 6, 15):
                            LiveBoundary(worker, offsets, ready, result, bad_tx, diagnostic,
                                         cases & 3, mask, phase).check()
                            cases += 1
    semantic = byte_mutants = 0
    if self_test:
        # Deliberately break each newly shifted ABI role and key failure gate.
        mutants = ((0x94, 0xd50a, 0x0009, True, 0),   # private driver in R5
                   (0x92, 0x6613, 0x0009, True, 0),   # boundary in R6
                   (0x90, 0xd70a, 0x0009, True, 0),   # exact reader in R7
                   (0x9a, 0x2f22, 0x2fa2, True, 0),   # null opaque context
                   (0x96, 0x1fa1, 0x1fa0, True, 0),   # snapshot on outgoing stack
                   (0x4e, 0xd61c, 0x0009, False, 0),  # prepare retains driver in R6
                   (0x26, 0xd423, 0x0009, True, 0),   # exact live-plan object
                   (0x9c, 0x2008, 0xe000, True, -4),  # validator failure retained
                   (0xa4, 0x52a2, 0x52a3, True, 0))   # compare snapshot TX, not RX
        for at, expected, replacement, ready, result in mutants:
            require(struct.unpack_from('<H', body, at)[0] == expected,
                    'live ABI semantic mutation site changed')
            altered = bytearray(body)
            struct.pack_into('<H', altered, at, replacement)
            try:
                LiveBoundary(worker, offsets, ready, result, False, 0, 1, 15, 1,
                             body=bytes(altered)).check()
            except (ValueError, AssertionError):
                semantic += 1
            else:
                raise ValueError('live ABI caller accepted an argument/failure semantic mutant')
        address, _, section = symbol_bytes(worker, '_live_boundary', 2)
        start = section[4] + address - section[3]
        for at in range(len(body)):
            data = bytearray(worker[0]); data[start + at] ^= 1
            try:
                pinned((bytes(data), *worker[1:]))
            except ValueError:
                byte_mutants += 1
            else:
                raise ValueError('live ABI caller accepted a changed complete function byte')
    return dict(enabled=True, actual_linked_caller_executed=True, cases=cases,
                function_bytes=len(body), function_sha256=hashlib.sha256(body).hexdigest(),
                both_prepare_and_check_abis_verified=True, private_driver_argument_verified=True,
                six_argument_live_check_abi_verified=True, nine_argument_prepare_abi_verified=True,
                strict_false_results_and_snapshot_tx_checked=True,
                diagnostic_refusal_fields_retained=True, preserved_owner_abi=True,
                no_game_timer_storage_or_rx_calls=True, console_layout=offsets,
                semantic_mutation_rejections=semantic, whole_body_byte_mutation_rejections=byte_mutants)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    print(json.dumps(check(read_elf(args.worker), args.self_test), indent=2))
