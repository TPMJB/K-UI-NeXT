#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked terminal cache-off loop with an explicit SH7750 cache model.

Renesas R01UH0456EJ0702 Rev7.02, section4.5.3, printed pages133-134:
A=0 addresses an entry directly; A=1 uses associative comparison and, when
MMU.AT=1, UTLB translation. Missing/mismatched translations suppress that write.
The model supplies authored cache contents and UTLB mappings. It proves the
flush defect and repair for those states, not the console's failing cache state
or that this defect caused the reported VC2 warm-start black screen.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_vmu_native_stage_stack import Machine, require
from check_vmu_native_worker_gateway import read_elf, symbol_bytes

SYMBOL = '_kui_native_return_cache_off'
ARRAY, ASSOCIATIVE, CCR = 0xf4000000, 0xf4000008, 0xff00001c
MANUAL = 'https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware'


def sha(data):
    return hashlib.sha256(data).hexdigest()


class CacheCPU(Machine):
    """The inherited decoder executes the linked instructions without leaf stubs."""
    instruction_limit = 4000

    def __init__(self, image, body, mmu, translation, mixed):
        super().__init__(image, SYMBOL, 0x531, 15, 0xac00bfc0, body=body)
        self.mmu = mmu
        self.translation = translation
        self.trace, self.array_reads, self.array_writes = [], [], []
        self.suppressed = self.writebacks = self.discarded = 0
        self.ccr_step = None
        self.lines = []
        self.ram = {}
        for i in range(512):
            physical = 0x0c000000 + 32 * i
            valid = not mixed or i % 3 != 2
            dirty = valid and (not mixed or i % 3 == 0)
            payload = bytes((i * 17 + j * 13 + 1) & 255 for j in range(32))
            self.lines.append(dict(tag=physical & 0x1ffffc00, physical=physical,
                                   valid=valid, dirty=dirty, payload=payload))
            self.ram[physical] = bytes([0xa5]) * 32 if dirty else payload
        self.before_lines = copy.deepcopy(self.lines)
        self.before_ram = self.ram.copy()
        self.saved = (self.r.copy(), self.pr, self.sr, self.gbr,
                      self.mach, self.macl, self.fpscr, self.fr.copy())

    def code(self, address, size):
        offset = (address & 0x1fffffff) - (self.base & 0x1fffffff)
        require(0 <= offset and offset + size <= len(self.blob),
                'Cache leaf escaped its linked instruction/literal body')
        data = self.blob[offset:offset + size]
        if size == 2:
            self.trace.append((address, struct.unpack('<H', data)[0]))
        return data

    @staticmethod
    def array_index(address):
        require(address & 0xff000000 == ARRAY and not address & 3,
                'Unexpected cache-array address')
        require(address & 0xffffc017 == ARRAY,
                'Cache-array access changed reserved address bits')
        return (address >> 5) & 511

    def read(self, address):
        if address & 0xff000000 == ARRAY:
            i = self.array_index(address)
            self.array_reads.append(address)
            line = self.lines[i]
            # Reads return physical tag and U/V for the addressed entry,
            # regardless of A. Reserved upper tag bits are authored zero.
            return line['tag'] | int(line['dirty']) << 1 | int(line['valid'])
        return super().read(address)

    def translated_tag(self, data):
        address = data & 0xfffffc00
        if not self.mmu:
            return address & 0x1ffffc00
        if self.translation == 'missing':
            return None
        if self.translation == 'remapped':
            return (address + 0x01000000) & 0x1ffffc00
        require(self.translation == 'identity', 'Unknown UTLB fixture')
        return address & 0x1ffffc00

    def write(self, address, value):
        if address & 0xff000000 == ARRAY:
            require(self.ccr_step is None, 'Cache write after cache invalidation')
            require(self.pc >> 29 == 5, 'Cache-array writer must execute in P2')
            i = self.array_index(address)
            self.array_writes.append((address, value))
            line = self.lines[i]
            if address & 8:
                translated = self.translated_tag(value)
                if translated is None or translated != line['tag'] or not line['valid']:
                    self.suppressed += 1
                    return
            if line['valid'] and line['dirty']:
                self.ram[line['physical']] = line['payload']
                self.writebacks += 1
            if not address & 8:
                line['tag'] = value & 0x1ffffc00
            line['valid'], line['dirty'] = bool(value & 1), bool(value & 2)
            return
        require(address == CCR and value == 0x808, 'Unexpected MMIO/cache control write')
        require(self.ccr_step is None and len(self.array_writes) == 512,
                'Cache invalidated before the complete512-entry walk')
        self.ccr_step = len(self.trace) - 1
        for line in self.lines:
            self.discarded += int(line['valid'] and line['dirty'])
            line['valid'] = line['dirty'] = False

    def check(self):
        self.run()
        registers, pr, sr, gbr, mach, macl, fpscr, fr = self.saved
        require(self.r[4:] == registers[4:] and self.pr == pr and
                self.sr & ~1 == sr & ~1 and self.gbr == gbr and self.mach == mach and
                self.macl == macl and self.fpscr == fpscr and self.fr == fr,
                'Cache-off leaf changed a nonvolatile register, stack or special state')
        require(len(self.array_reads) == len(self.array_writes) == 512,
                'Cache-off leaf did not read/write every entry exactly once')
        require([a for a, _ in self.array_writes] == self.array_reads and
                [self.array_index(a) for a in self.array_reads] == list(range(512)),
                'Cache-off leaf changed the complete indexed entry sequence')
        require(self.ccr_step is not None and
                [op for _, op in self.trace[self.ccr_step + 1:]] == [9] * 8 + [11, 9],
                'Cache-off leaf changed the eight-NOP wait or return delay slot')
        lost = [i for i, line in enumerate(self.before_lines)
                if line['valid'] and line['dirty'] and
                self.ram[line['physical']] != line['payload']]
        require(lost == [i for i, line in enumerate(self.before_lines)
                         if self.lines[i]['physical'] in self.ram and
                         self.ram[line['physical']] != line['payload']],
                'Clean/invalid cache payload changed unexpectedly')
        return dict(mmu_on=bool(self.mmu), utlb=self.translation, writes=512,
                    writebacks=self.writebacks, suppressed=self.suppressed,
                    dirty_lines_discarded=self.discarded, lost_payload_lines=len(lost),
                    authored_ram_witness_preserved=0 not in lost,
                    instruction_count=len(self.trace), abi_and_wait_preserved=True)


def audit(path, old_path=None):
    require(__debug__, 'Run instruction checks without Python optimization')
    image = read_elf(path)
    _address, body, _ = symbol_bytes(image, SYMBOL, 2)
    positions = [i for i in range(0, len(body) - 3, 4)
                 if struct.unpack_from('<I', body, i)[0] == ARRAY]
    require(len(positions) == 1 and struct.pack('<I', ASSOCIATIVE) not in body,
            'Production cache-off leaf must use exactly one non-associative base')
    changed = bytearray(body)
    struct.pack_into('<I', changed, positions[0], ASSOCIATIVE)
    require(sum(a != b for a, b in zip(body, changed)) == 1 and
            sum((a ^ b).bit_count() for a, b in zip(body, changed)) == 1,
            'Historical negative control changed more than the association bit')
    old_hash = None
    if old_path is not None:
        old_image = read_elf(old_path)
        _, old_body, _ = symbol_bytes(old_image, SYMBOL, 2)
        require(old_body == changed, 'Actual old ELF differs beyond the one association bit')
        old_hash = sha(old_image[0])
    fixed, negative = [], []
    for mixed in (False, True):
        for mmu, mapping in ((0, 'missing'), (1, 'identity'),
                             (1, 'missing'), (1, 'remapped')):
            good = CacheCPU(image, body, mmu, mapping, mixed).check()
            old = CacheCPU(image, bytes(changed), mmu, mapping, mixed).check()
            good['mixed_lines'] = old['mixed_lines'] = mixed
            require(not good['lost_payload_lines'] and not good['suppressed'] and
                    not good['dirty_lines_discarded'], 'Direct entry walk failed to publish dirty data')
            require(good['instruction_count'] == old['instruction_count'],
                    'One-bit repair changed executed instruction count')
            if mmu and mapping in ('missing', 'remapped'):
                require(old['lost_payload_lines'] and old['dirty_lines_discarded'] and
                        not old['authored_ram_witness_preserved'],
                        'Historical MMU-on negative control did not expose stale RAM')
            else:
                require(not old['lost_payload_lines'], 'Historical identity/MMU-off control failed')
            fixed.append(good)
            negative.append(old)
    return dict(schema='kui.native-return-cache.v1', status='PASS',
                elf_sha256=sha(image[0]), function_sha256=sha(body),
                actual_old_elf_sha256=old_hash,
                manual=MANUAL, section='4.5.3', printed_pages=[133, 134],
                fixed=fixed, historical_one_bit_control=negative,
                limits='Actual linked SH instructions; authored cache/UTLB/RAM model. '
                       'No physical console cache capture or causal attribution to VC2 black screen.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, help='Actual linked stage/controller ELF')
    parser.add_argument('--old-elf', type=Path, help='Optional actual pre-fix ELF; requires exact one-bit body delta')
    parser.add_argument('--cc', default=os.environ.get('SH_CC', 'sh-elf-gcc'))
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kui-native-return-cache-') as tmp:
        elf = args.elf
        if elf is None:
            compiler = shutil.which(args.cc)
            if not compiler:
                parser.error('Provide --elf or a cross compiler with --cc')
            elf = Path(tmp) / 'cache.elf'
            command = [compiler, '-ml', '-m4-single-only', '-nostdlib',
                       '-Wl,--build-id=none', '-Wl,-Ttext,0x8c008300',
                       '-Wl,-e,' + SYMBOL, str(ROOT / 'src/loader/native_return_hardware.S'),
                       '-o', str(elf)]
            subprocess.run(command, check=True, capture_output=True)
        report = audit(elf, args.old_elf)
    text = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
