#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit actual linked physical-disc VMU assembly interfaces.

Checks controlling PC-relative literals and every instruction at the bootstrap,
executable-entry frame, and relay. This is an ABI audit, not SH CPU emulation.
The optional support ELF also proves terminal entry never borrows a caller stack.
"""
import argparse
from pathlib import Path
import struct

from check_loader_layout import EH, SH, SYM, region


class Image:
    def __init__(self, data):
        self.data = data
        h = EH.unpack(region(data, 0, EH.size, 'ELF header'))
        if (h[0][:7] != b'\x7fELF\x01\x01\x01' or h[1:4] != (2, 42, 1) or
                h[11] != SH.size or not 0 < h[12] <= 4096):
            raise ValueError('Requires linked little-endian SH ELF')
        self.sections = [SH.unpack(region(data, h[6]+i*SH.size, SH.size, 'section'))
                         for i in range(h[12])]
        self.symbols = {}
        for s in self.sections:
            if s[1] != 2:
                continue
            if s[9] != SYM.size or s[5] % SYM.size or s[6] >= len(self.sections):
                raise ValueError('Invalid symbol table')
            strings = self.sections[s[6]]
            names = region(data, strings[4], strings[5], 'symbol names')
            for at in range(s[4], s[4]+s[5], SYM.size):
                n, value, _, info, _, section = SYM.unpack(region(data, at, SYM.size, 'symbol'))
                if not info >> 4 or not n:
                    continue
                end = names.find(b'\0', n)
                if n >= len(names) or end < 0:
                    raise ValueError('Invalid symbol name')
                name = names[n:end].decode('ascii')
                if name in self.symbols and self.symbols[name] != value:
                    raise ValueError('Conflicting public symbol')
                self.symbols[name] = value

    def symbol(self, name):
        if name not in self.symbols:
            raise ValueError(f'Missing linked {name}')
        return self.symbols[name]

    def offset(self, address, size):
        found = [s for s in self.sections if s[1] == 1 and s[2] & 2 and
                 s[3] <= address and address+size <= s[3]+s[5]]
        if len(found) != 1:
            raise ValueError(f'Unmapped linked bytes at {address:08x}')
        s = found[0]
        return s[4]+address-s[3]

    def read(self, address, size):
        return region(self.data, self.offset(address, size), size, 'linked bytes')


class Cursor:
    def __init__(self, image, name, witnesses):
        self.image, self.pc, self.witnesses = image, image.symbol(name), witnesses

    def op(self, code, label):
        actual = struct.unpack('<H', self.image.read(self.pc, 2))[0]
        if actual != code:
            raise ValueError(f'{label} at {self.pc:08x}: {actual:04x} != {code:04x}')
        self.witnesses.append((label, self.pc, 2))
        self.pc += 2

    def literal(self, register, value, label):
        code = struct.unpack('<H', self.image.read(self.pc, 2))[0]
        if code & 0xff00 != 0xd000 | register << 8:
            raise ValueError(f'{label}: expected MOV.L literal into r{register}')
        address = ((self.pc+4) & ~3)+(code & 255)*4
        actual = struct.unpack('<I', self.image.read(address, 4))[0]
        if actual != value:
            raise ValueError(f'{label}: {actual:08x} != {value:08x}')
        self.witnesses.extend(((label+' load', self.pc, 2), (label+' value', address, 4)))
        self.pc += 2

    def nops(self):
        for _ in range(8):
            self.op(0x0009, 'CCR synchronization')

    def publish(self):
        self.literal(0, 0x8c000000, 'RAM publication start')
        self.literal(1, 0x8d000000, 'RAM publication end')
        for op in (0x00a3, 0x7020, 0x3012, 0x8bfb):
            self.op(op, 'complete RAM publication loop')


def audit_stage(data):
    image, w = Image(data), []
    c = Cursor(image, '_kui_disc_vmu_stage_enter', w)
    c.literal(0, (c.pc+6) | 0x20000000, 'uncached bootstrap wrapper')
    c.op(0x402b, 'bootstrap uncached jump'); c.op(0x0009, 'jump delay')
    c.publish()
    c.literal(0, 0xff00001c, 'CCR address'); c.literal(1, 0x909, 'native CCR')
    c.op(0x2012, 'publish native CCR'); c.nops()
    c.literal(0, 0x700000f0, 'native SR/SSR')
    c.op(0x403e, 'set SSR'); c.op(0x400e, 'set SR')
    c.literal(0, 0x8c000000, 'native GBR'); c.op(0x401e, 'set GBR')
    c.literal(15, 0x8c00f400, 'native stack'); c.op(0x4f2e, 'native VBR')
    c.literal(0, 0x40001, 'native FPSCR'); c.op(0x406a, 'set FPSCR')
    c.literal(0, c.pc+44, 'bootstrap return handler'); c.op(0x402a, 'set PR')
    c.literal(14, 0xac00e000, 'native bootstrap2'); c.op(0x4e4e, 'set SPC')
    for r in range(14):
        c.op(0xe000 | r << 8, 'clear native register')
    for op in (0x0028, 0x0008, 0x4e2b, 0xee00):
        c.op(op, 'native bootstrap final jump')
    c.literal(15, 0x8cff0000, 'returned bootstrap private stack')
    c.literal(0, image.symbol('_kui_disc_vmu_stage_returned'), 'returned bootstrap handler')
    c.op(0x402b, 'returned bootstrap jump'); c.op(0x0009, 'return delay')

    c = Cursor(image, '__disc_vmu_trampoline_start', w)
    start = c.pc
    if image.symbol('__disc_vmu_trampoline_end')-start != 128:
        raise ValueError('Trampoline must be exactly128 bytes')
    for r in range(15):
        c.op(0x2f06 | r << 4, f'entry saves r{r}')
    for op in (0x4f22, 0x4f03, 0x4f23, 0x4f13, 0x4f02, 0x4f12):
        c.op(op, 'entry saves PR/SR/VBR/GBR/MACH/MACL')
    for op in (0x0002, 0xcbf0, 0x400e):
        c.op(op, 'entry masks before relay')
    c.literal(0, image.symbol('_kui_disc_vmu_game_resume')+0x20000000, 'uncached relay')
    c.op(0x402b, 'entry jumps to relay'); c.op(0x0009, 'entry jump delay')
    end = image.symbol('__disc_vmu_trampoline_code_end')
    if end-start > 64 or end-start < c.pc-start or any(image.read(end, start+128-end)):
        raise ValueError('Trampoline code/padding differs from bounded64-byte entry')

    c = Cursor(image, '_kui_disc_vmu_game_resume', w)
    c.op(0x68f3, 'retain owner frame in r8')
    c.literal(15, 0x8cff0000, 'relay private stack')
    c.literal(0, 0xff00001c, 'relay CCR address'); c.op(0x6902, 'retain owner CCR in r9')
    c.publish(); c.literal(0, 0xff00001c, 'relay CCR write address')
    c.literal(1, 0x908, 'scan with uncached mutable RAM')
    c.op(0x2012, 'set scan CCR'); c.nops()
    c.op(0x6483, 'pass saved frame'); c.op(0x6593, 'pass saved CCR')
    c.literal(0, image.symbol('_kui_disc_vmu_stage_relay'), 'temporary C relay')
    c.op(0x400b, 'call C relay'); c.op(0x0009, 'call delay')
    c.literal(0, 0x808, 'cache invalidation bits'); c.op(0x209b, 'retain owner CCR mode')
    c.literal(1, 0xff00001c, 'restore CCR address'); c.op(0x2102, 'restore owner CCR')
    c.nops(); c.op(0x6f83, 'restore owner frame SP')
    for op in (0x4f16, 0x4f06, 0x4f17, 0x4f27, 0x7f04, 0x4f26):
        c.op(op, 'restore MACL/MACH/GBR/VBR and PR, defer SR')
    for r in range(14, 0, -1):
        c.op(0x60f6 | r << 8, f'restore r{r}')
    for op in (0x60f3, 0x70c0, 0x6002, 0x400e):
        c.op(op, 'restore SR from frame offset before final jump')
    c.literal(0, 0xac010000, 'original executable entry')
    c.op(0x402b, 'resume original executable'); c.op(0x60f6, 'restore r0 and original SP in delay')
    return image, w


def audit_support(data):
    image, w = Image(data), []
    c = Cursor(image, '_kui_disc_vmu_support_terminal', w)
    c.op(0x0002, 'terminal captures SR'); c.literal(1, 0x100000f0, 'terminal mask')
    c.op(0x201b, 'terminal masks interrupts'); c.op(0x400e, 'terminal sets masked SR')
    c.literal(1, image.symbol('_kui_disc_vmu_support_state'), 'terminal immutable bound state')
    # The remainder is an exact no-stack guard interface. Resolve branch
    # displacements against the one terminal stop loop after the sole jump.
    checks = (0x6012, ('lit', 2, 0x44565342), 0x3020, 'bf', 0x5311, 0x5412,
              ('lit', 1, image.symbol('_kui_disc_vmu_support_exports')), 0x3410, 'bf',
              ('lit', 2, 0x12000), 0x3128, 0x6012, ('lit', 2, 0x4b4e5031), 0x3020, 'bf',
              0x5011, 0x8804, 'bf', 0x5012, 0x8850, 'bf', 0x5016, 0x3030, 'bf',
              0x501a, 0x3040, 'bf', 0x501b, 0x3040, 'bf',
              ('lit', 2, 0x20000000), 0x3128, 0x7150, 0x3312, 'bf',
              0x6143, 0x3128, ('lit', 0, 4096), 0x3108, 0x3312, 'bt',
              0x6033, 0xc801, 'bf', 0x6f43, 0x3f28, 0x432b, 0x0009)
    # Count is fixed by this public assembly ABI; discover stop from the
    # linked jump's fallthrough instead of trusting an exported local label.
    words = sum(1 for _ in checks)
    stopped = c.pc+words*2
    for item in checks:
        if isinstance(item, tuple):
            c.literal(item[1], item[2], 'terminal bound pointer/identity')
        elif item in ('bf', 'bt'):
            displacement = (stopped-(c.pc+4))//2
            c.op((0x8b00 if item == 'bf' else 0x8900) | (displacement & 255), 'terminal refusal edge')
        else:
            c.op(item, 'terminal validates before using worker stack')
    c.op(0xaffe, 'terminal unbound stop loop'); c.op(0x0009, 'terminal stop delay')
    return image, w


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage', type=Path)
    p.add_argument('--support', type=Path)
    args = p.parse_args()
    try:
        _, stage = audit_stage(args.stage.read_bytes())
        support = audit_support(args.support.read_bytes())[1] if args.support else []
    except (ValueError, OSError, struct.error) as error:
        p.exit(1, f'Physical VMU handoff audit failed: {error}\n')
    print(f'PASS physical VMU linked handoff: {len(stage)} stage and {len(support)} support witnesses; '
          'bootstrap2, exact84-byte owner frame, cache mode, executable return and terminal private stack')


if __name__ == '__main__':
    main()
