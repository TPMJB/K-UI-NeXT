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
        self.image, self.pc, self.witnesses = image, image.symbol(name) if isinstance(name, str) else name, witnesses

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
    audit_entry_return(image, w)
    return image, w


def typed(image, name, kind):
    """Resolve one exact local/global typed record without nearest symbols."""
    found = []
    for table in image.sections:
        if table[1] != 2:
            continue
        strings = image.sections[table[6]]
        names = image.data[strings[4]:strings[4]+strings[5]]
        for at in range(table[4], table[4]+table[5], SYM.size):
            n, address, size, info, _, owner = SYM.unpack_from(image.data, at)
            end = names.find(b'\0', n)
            if n and end >= 0 and names[n:end].decode('ascii') == name:
                found.append((address, size, info & 15, owner))
    if len(found) != 1 or found[0][2] != kind or not found[0][1]:
        raise ValueError('Requires one exact typed return-binding record: '+name)
    address, size, _, owner = found[0]
    if owner >= len(image.sections):
        raise ValueError('Return-binding record lacks a section: '+name)
    section = image.sections[owner]
    if (not section[2] & 2 or not section[3] <= address < address+size <= section[3]+section[5] or
            (kind == 2 and (section[1] != 1 or not section[2] & 4 or address & 1))):
        raise ValueError('Return-binding record escapes its exact section: '+name)
    return address, size


def function_ops(image, name):
    begin, size = typed(image, name, 2)
    literals, ops = set(), []
    for pc in range(begin, begin+size, 2):
        if pc in literals:
            continue
        op = struct.unpack('<H', image.read(pc, 2))[0]
        ops.append((pc, op))
        if op >> 12 == 0xd:
            literal = ((pc+4) & ~3)+(op & 255)*4
            literals.update((literal, literal+2))
        elif op >> 12 == 9:
            literals.add(pc+4+(op & 255)*2)
    return ops


def linked_call(image, caller, callee, witnesses):
    """Pin the actual controlling literal, call and its bounded argument slot."""
    target, _ = typed(image, callee, 2)
    ops = dict(function_ops(image, caller))
    found = []
    for pc, op in ops.items():
        if op >> 12 != 0xd:
            continue
        literal = ((pc+4) & ~3)+(op & 255)*4
        if struct.unpack('<I', image.read(literal, 4))[0] != target:
            continue
        register = op >> 8 & 15
        for gap in (2, 4, 6, 8):
            call = pc+gap
            if ops.get(call) != 0x400b | register << 8:
                continue
            # A bounded group of scalar argument setup instructions may
            # separate the target load/call; none may overwrite that target.
            between = [ops.get(at, 0) for at in range(pc+2, call, 2)]
            if any(op >> 12 not in (5, 6, 7, 0xd, 0xe) or op >> 8 & 15 == register for op in between):
                continue
            found.append((pc, literal, call))
    if len(found) != 1:
        raise ValueError('Requires one direct resolved return-binding call: '+caller+' -> '+callee)
    pc, literal, call = found[0]
    witnesses.extend(((callee+' controlling load', pc, 2), (callee+' controlling literal', literal, 4),
                      (callee+' exact call', call, 2)))
    return call


def snapshot_capture(image, helper, witnesses):
    """Pin the bounded paired-window copy loop and its sole memcpy call.

    Unlike the scalar calls, this callee stays in R9 across a short loop.
    Bind its actual literal and every argument/count/loop instruction rather
    than widening linked_call's permissible target-load gap.
    """
    target, _ = typed(image, '_memcpy', 2)
    loads = []
    for pc, op in function_ops(image, helper):
        if op >> 12 == 0xd:
            literal = ((pc+4) & ~3)+(op & 255)*4
            if struct.unpack('<I', image.read(literal, 4))[0] == target:
                loads.append(pc)
    if len(loads) != 1:
        raise ValueError('Requires one exact paired snapshot-copy callee load')
    c = Cursor(image, loads[0]-2, witnesses)
    c.literal(12, 0x20000000, 'snapshot source P2 alias')
    c.literal(9, target, 'snapshot copy callee')
    loop = c.pc
    for op in (0x50f4, 0xe10c, 0x52f2, 0xe60c, 0x201e, 0x6526):
        c.op(op, 'snapshot index, twelve-byte span and source entry')
    c.literal(1, typed(image, '_persistent_exit_windows', 1)[0], 'snapshot destination array')
    for op in (0x041a, 0x1f22, 0x25cb):
        c.op(op, 'snapshot destination index and uncached source')
    call = c.pc
    c.op(0x490b, 'snapshot exact memcpy call')
    c.op(0x341c, 'snapshot destination argument delay')
    for op in (0x51f4, 0xe001, 0x52f5, 0x7101, 0x3216):
        c.op(op, 'snapshot bounded pair-loop increment and limit')
    displacement = (loop-(c.pc+4))//2
    c.op(0x8d00 | (displacement & 255), 'snapshot bounded pair-loop edge')
    c.op(0x1f04, 'snapshot second window index')
    return call


def cached_prearm_call(image, relay, witnesses):
    """Exact cache-profile mapping call and hook-store schedule before prearm.

    The compiler schedules two stores between the callee load and call. Pin
    their actual typed targets and every instruction instead of admitting
    arbitrary memory operations in linked_call's scalar argument window.
    """
    target, _ = typed(image, '_native_crt_bootstrap_prearm', 2)
    found = []
    for pc, op in function_ops(image, relay):
        if op >> 12 != 0xd:
            continue
        literal = ((pc+4) & ~3)+(op & 255)*4
        if struct.unpack('<I', image.read(literal, 4))[0] != target:
            continue
        local = []
        c = Cursor(image, pc-10, local)
        try:
            c.literal(1, typed(image, '_native_disc_mapping_prepare', 2)[0], 'cached mapping callee')
            c.literal(8, typed(image, '_native_heap_hook', 1)[0], 'cached heap hook cell')
            c.op(0x410b, 'cached mapping exact call'); c.op(0x0009, 'cached mapping call delay')
            c.literal(1, image.symbol('_kui_vmu_observe_heap_hook'), 'cached heap hook target')
            c.literal(0, target, 'cached prearm callee')
            c.literal(2, image.symbol('_kui_vmu_observe_return_hook'), 'cached return hook target')
            c.op(0x2812, 'cached heap hook publication')
            c.literal(1, typed(image, '_native_terminal', 1)[0], 'cached terminal hook cell')
            call = c.pc
            c.op(0x400b, 'cached prearm exact call')
            c.op(0x2122, 'cached terminal hook publication delay')
        except ValueError:
            continue
        found.append((call, local))
    if len(found) != 1:
        raise ValueError('Requires exact cached mapping/hook/prearm instruction schedule')
    call, local = found[0]
    witnesses.extend(local)
    return call


def writes_gpr(op, register):
    """Conservative SH integer register-write predicate for retained-frame proof.

    Stores do not replace their base except predecrement; loads may also
    replace a postincrement source. Calls use the compiler's callee-saved
    register ABI. Unknown integer/system encodings conservatively refuse.
    """
    top, n, m, low = op >> 12, op >> 8 & 15, op >> 4 & 15, op & 15
    if top in (5, 6, 7, 9, 0xd, 0xe):
        return n == register or (top == 6 and low in (4, 5, 6) and m == register)
    if top == 1 or top in (0xa, 0xb):
        return False
    if top == 0xc:
        return op & 0xff00 == 0xc300  # TRAPA is not a callee-saved C call
    if top == 2:
        return n == register and low not in (0, 1, 2, 7, 8, 0xc, 0xe, 0xf)
    if top == 3:
        return n == register and low not in (0, 2, 3, 5, 6, 7, 0xd)
    if top == 4:
        return n == register or (low == 0xf and m == register)
    if top == 8:
        # Displacement byte/word loads target R0; branches do not write a GPR.
        return register == 0 and (op & 0xff00) in (0x8400, 0x8500)
    if top == 0:
        if op in (0x0009, 0x0008, 0x0018, 0x0019, 0x0028):
            return False
        if low in (4, 5, 6, 7):
            return False  # indexed stores and MUL.L do not update GPRs
        if low == 0xf:
            return n == register or m == register  # MAC.L postincrements both
        if low in (2, 0xa, 9, 0xc, 0xd, 0xe):
            return n == register
        return True
    return True


def audit_entry_return(image, witnesses):
    """Narrow compiled-C ABI witnesses; host tests cover refusal semantics.

    Keep the original 84-byte assembly interface unchanged. Prove the relay
    feeds its retained input frame into the noipa binder, that this happens
    before heap arming, and that the compiled binder publishes only PR word5
    after its exit veneer/captured-window update. No C function-body hash or
    source line number is used as authority.
    """
    helper = '_native_disc_entry_return_bind'
    relay = '_kui_disc_vmu_stage_relay'
    cached = '__native_resolution_cache_seed_start' in image.symbols
    before = (cached_prearm_call(image, relay, witnesses) if cached else
              linked_call(image, relay, '_native_crt_bootstrap_prearm', witnesses))
    if cached:
        image_sha = linked_call(image, relay, '_kui_sha256_digest', witnesses)
        extent = linked_call(image, relay, '_kui_vmu_native_program_extent', witnesses)
        mapping = linked_call(image, relay, '_native_disc_mapping_prepare', witnesses)
        if not image_sha < extent < mapping < before:
            raise ValueError('Disc mapping must follow the full-file SHA and program extent, before live prearm')
    bind = linked_call(image, relay, helper, witnesses)
    after = linked_call(image, relay, '_code_sha', witnesses)
    if not before < bind < after:
        raise ValueError('Entry PR binding must follow CRT prearm and precede heap arming')
    relay_ops = dict(function_ops(image, relay))
    if cached:
        # The smaller cache-profile relay retains the incoming R4 in R9.
        # Prove its entry copy precedes every call and no intervening opcode
        # writes R9, including a memory instruction's address update.
        begin, _ = typed(image, relay, 2)
        c = Cursor(image, begin, witnesses)
        c.op(0x2f86, 'cached relay saves r8')
        c.op(0x2f96, 'cached relay saves r9')
        retain = c.pc
        c.op(0x6943, 'retain original owner-frame argument')
        # This profile's prefix is contiguous code; its literal pools follow
        # the binder. Do not let a mutated PC-relative load label a later
        # executable instruction as data and hide a retained-register write.
        for pc in range(retain+2, bind+2, 2):
            op = struct.unpack('<H', image.read(pc, 2))[0]
            if op >> 12 in (9, 0xd):
                literal = (pc+4+(op & 255)*2 if op >> 12 == 9 else
                           ((pc+4) & ~3)+(op & 255)*4)
                size = 2 if op >> 12 == 9 else 4
                if literal < bind+4 and begin < literal+size:
                    raise ValueError('Cached relay literal overlaps retained-frame code prefix')
            if writes_gpr(op, 9):
                raise ValueError('Return binder loses the original R9 owner frame')
        if relay_ops.get(bind+2) != 0x6493:
            raise ValueError('Return binder does not retain/pass the original R9 owner frame')
    else:
        # Historical compiler profile retains original R4 at SP+12.
        saves = [(pc, op) for pc, op in relay_ops.items() if pc < bind and op & 0xff0f == 0x1f03]
        if len(saves) != 1 or saves[0][1] != 0x1f43 or relay_ops.get(bind+2) != 0x54f3:
            raise ValueError('Return binder does not receive the original saved owner frame')
        witnesses.append(('retain original owner-frame argument', saves[0][0], 2))
    witnesses.append(('pass exact original owner-frame argument', bind+2, 2))
    patch = linked_call(image, helper, '_readonly_patch_entry', witnesses)
    capture = snapshot_capture(image, helper, witnesses)
    ops = dict(function_ops(image, helper))
    stores = [(pc, op) for pc, op in ops.items() if pc > capture and op >> 12 == 1 and op & 15 == 5]
    if len(stores) != 1 or not patch < capture < stores[0][0]:
        raise ValueError('Entry PR store must follow exit publication and capture update')
    pc, op = stores[0]
    base, value = op >> 8 & 15, op >> 4 & 15
    prior = ops.get(pc-2, 0)
    source = prior >> 4 & 15
    if prior != 0x200b | base << 8 | source << 4:
        raise ValueError('Entry PR write must use an explicitly uncached owner-frame alias')
    load = ops.get(pc-4, 0)
    if load & 0xff00 != 0xd000 | base << 8:
        raise ValueError('Entry PR uncached alias lacks a controlling literal')
    literal = ((pc & ~3)+(load & 255)*4)
    if struct.unpack('<I', image.read(literal, 4))[0] != 0xa0000000:
        raise ValueError('Entry PR write must use P2')
    read = ops.get(pc+2, 0)
    dest = read >> 8 & 15
    if read != 0x5005 | dest << 8 | base << 4 or ops.get(pc+6) != 0x3000 | value << 8 | dest << 4:
        raise ValueError('Entry PR write must reread and compare the exact same word5')
    error = ops.get(pc+4, 0)
    if error & 0xf0ff != 0xe074:
        raise ValueError('Entry PR readback error must identify frame publication')
    edge = ops.get(pc+8, 0)
    if edge >> 8 != 0x8d:
        raise ValueError('Entry PR readback mismatch must refuse')
    displacement = edge & 255
    if displacement & 128:
        displacement -= 256
    success = pc+12+2*displacement
    failed = pc+12
    if success != pc+16:
        raise ValueError('Entry PR readback equality must skip only the refusal edge')
    error_store = ops.get(pc+10, 0)
    if error_store & 0xf00f != 0x2002 or error_store >> 4 & 15 != error >> 8 & 15:
        raise ValueError('Entry PR readback must publish its exact error detail')
    branch, zero = ops.get(failed, 0), ops.get(failed+2, 0)
    if branch >> 12 != 0xa or zero & 0xf0ff != 0xe000:
        raise ValueError('Entry PR failed readback must select false before returning')
    displacement = branch & 4095
    if displacement & 2048:
        displacement -= 4096
    result = failed+4+2*displacement
    if ops.get(result) != 0x6003 | (zero >> 8 & 15) << 4:
        raise ValueError('Entry PR failed readback must return the selected false result')
    for label, address, size in (
            ('entry PR P2 alias load', pc-4, 2), ('entry PR P2 alias', literal, 4),
            ('entry PR P2 selection', pc-2, 2), ('entry PR word5 store', pc, 2),
            ('entry PR word5 readback', pc+2, 2), ('entry PR readback error detail', pc+4, 2),
            ('entry PR target comparison', pc+6, 2), ('entry PR failed readback edge', pc+8, 2),
            ('entry PR readback error publication', pc+10, 2), ('entry PR refusal return edge', failed, 2),
            ('entry PR refusal selects false', failed+2, 2), ('entry PR refusal returns false', result, 2)):
        witnesses.append((label, address, size))


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
