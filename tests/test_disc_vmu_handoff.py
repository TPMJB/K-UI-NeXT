#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fault injection against actual linked physical-disc VMU handoff ELFs."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from check_disc_vmu_handoff import audit_stage, audit_support, function_ops, typed
from test_vmu_native_heap_execution import SH, MASK, physical


def mutations(path, audit):
    original = path.read_bytes()
    image, witnesses = audit(original)
    unique = {(address, size) for _, address, size in witnesses}
    cases = 0
    for address, size in sorted(unique):
        data = bytearray(original)
        data[image.offset(address, size)] ^= 1
        try:
            audit(data)
        except (ValueError, struct.error):
            cases += 1
        else:
            raise AssertionError(f'Audit admitted changed linked ABI at {address:08x}')
    for data in (b'', original[:16], original[:len(original)//2]):
        try:
            audit(data)
        except (ValueError, struct.error):
            cases += 1
        else:
            raise AssertionError('Audit admitted truncated ELF')
    return cases


def frame_contract():
    # Independently track the frame offset, including the deferred SR slot.
    # The ELF audit pins the exact push/pop instructions to this ABI.
    frame = ['macl', 'mach', 'gbr', 'vbr', 'sr', 'pr']+[f'r{r}' for r in range(14, -1, -1)]
    sp = 0
    for name in ('macl', 'mach', 'gbr', 'vbr'):
        assert frame[sp] == name
        sp += 1
    sr_slot = sp
    sp += 1
    assert frame[sp] == 'pr'
    sp += 1
    for r in range(14, 0, -1):
        assert frame[sp] == f'r{r}'
        sp += 1
    assert sp*4-64 == sr_slot*4
    assert frame[sp] == 'r0' and (sp+1)*4 == 84


def paired_snapshot_mutations(path):
    """Keep the new paired-copy loop's ABI failures explicit and named."""
    original = path.read_bytes()
    image, witnesses = audit_stage(original)
    by_label = {label: (address, size) for label, address, size in witnesses}
    changes = (
        ('snapshot source P2 alias value', 0),
        ('snapshot copy callee load', 0xd800),  # wrong retained target register
        ('snapshot destination array value', 0),
        ('snapshot exact memcpy call', 0x480b),  # wrong target register
        ('snapshot bounded pair-loop edge', 0x0009),
        ('snapshot second window index', 0x1f14),  # caller value instead of index one
        ('entry PR word5 store', 0x11a6),  # wrong owner-frame word
        ('entry PR failed readback edge', 0x8f02),  # inverted readback condition
    )
    for label, value in changes:
        address, size = by_label[label]
        data = bytearray(original)
        struct.pack_into('<I' if size == 4 else '<H', data, image.offset(address, size), value)
        try:
            audit_stage(data)
        except (ValueError, struct.error):
            pass
        else:
            raise AssertionError('Audit admitted invalid paired-return ABI: '+label)
    # Updating both the store and its comparison can retain local readback
    # consistency while publishing the private-stage memcpy address as PR.
    # Its retained target register is never the selected game-owned entry.
    data = bytearray(original)
    store, _ = by_label['entry PR word5 store']
    call, _ = by_label['snapshot exact memcpy call']
    callee = struct.unpack_from('<H', original, image.offset(call, 2))[0] >> 8 & 15
    old_store = struct.unpack_from('<H', original, image.offset(store, 2))[0]
    old_compare = struct.unpack_from('<H', original, image.offset(store+6, 2))[0]
    struct.pack_into('<H', data, image.offset(store, 2), (old_store & ~0xf0) | callee << 4)
    struct.pack_into('<H', data, image.offset(store+6, 2), (old_compare & ~0xf00) | callee << 8)
    try:
        audit_stage(data)
    except (ValueError, struct.error):
        pass
    else:
        raise AssertionError('Audit admitted memcpy address as a self-consistent owner-frame PR')
    return len(changes)+1


def retained_snapshot_mutations(path):
    """The new register cursor must stay the typed entry array on every path."""
    original = path.read_bytes()
    image, witnesses = audit_stage(original)
    by_label = {label: (address, size) for label, address, size in witnesses}
    if 'snapshot retained entry-array source load' not in by_label:
        return 0
    source = by_label['snapshot retained entry-array source load'][0]
    loop = by_label['snapshot copy callee load'][0]+2
    pinned = {a for _, address, size in witnesses for a in range(address, address+size)}
    sites = [pc for pc, op in function_ops(image, '_native_disc_entry_return_bind')
             if source < pc < loop and op == 0xee00 and pc not in pinned]
    assert len(sites) == 1
    for opcode in (0xed00, 0x6d13, 0x60d6, 0x2d06, 0x00df, 0x40df,
                   0xdd00, 0x7d01, 0x3d0c, 0xc300):
        data = bytearray(original)
        struct.pack_into('<H', data, image.offset(sites[0], 2), opcode)
        try:
            audit_stage(data)
        except (ValueError, struct.error):
            pass
        else:
            raise AssertionError(f'Audit admitted snapshot R13 source clobber {opcode:04x}')
    # A PC-relative read cannot hide a reachable clobber as purported data.
    data = bytearray(original)
    literal = (sites[0]+4) & ~3
    struct.pack_into('<H', data, image.offset(sites[0], 2), 0xd000)
    struct.pack_into('<H', data, image.offset(literal, 2), 0xed00)
    try:
        audit_stage(data)
    except (ValueError, struct.error):
        pass
    else:
        raise AssertionError('Audit admitted snapshot R13 clobber hidden by a forged literal')
    return 11


class SnapshotCPU(SH):
    """Generic integer opcodes used by these two loops and linked memcpy."""
    def plain(self, op, pc):
        n, m = op >> 8 & 15, op >> 4 & 15
        if op & 0xf00f == 0x200e:  # MULU.W Rm,Rn -> MACL
            self.executing = pc; self.instructions += 1
            self.macl = (self.r[n] & 65535)*(self.r[m] & 65535)
        elif op & 0xf0ff == 0x001a:  # STS MACL,Rn
            self.executing = pc; self.instructions += 1; self.r[n] = self.macl
        elif op & 0xf0ff == 0x4010:  # DT Rn
            self.executing = pc; self.instructions += 1
            self.r[n] = (self.r[n]-1) & MASK; self.flag(self.r[n] == 0)
        elif op & 0xf00f == 0x2000:  # MOV.B Rm,@Rn
            self.executing = pc; self.instructions += 1; self.write(self.r[n], self.r[m], 1)
        elif op & 0xff00 == 0x8400:  # MOV.B @(disp,Rm),R0
            self.executing = pc; self.instructions += 1
            value = self.read(self.r[m]+(op & 15), 1)
            self.r[0] = (value-256 if value & 128 else value) & MASK
        else:
            super().plain(op, pc)


def snapshot_loop_execution(path):
    """Execute the actual audited loop and exact linked memcpy for both bounds."""
    image, witnesses = audit_stage(path.read_bytes())
    by_label = {label: (address, size) for label, address, size in witnesses}
    callee = by_label['snapshot copy callee load'][0]
    begin = callee-2; end = by_label['snapshot second window index'][0]+2
    source = typed(image, '_native_exits', 1)[0]+24
    destination = typed(image, '_persistent_exit_windows', 1)[0]
    target = typed(image, '_memcpy', 2)[0]
    retained = 'snapshot retained entry-array source load' in by_label
    cases = 0
    for limit in (1, 2):
        for alignment in (0, 2):
            for seed in range(8):
                cpu = SnapshotCPU(b''); cpu.macl = 0
                for section in image.sections:
                    if section[1] == 1 and section[2] & 2 and section[5]:
                        cpu.store_block(section[3], image.read(section[3], section[5]))
                stack = 0x8cff0000
                cpu.r[15] = stack; cpu.r[13] = source
                cpu.write(stack+8, source); cpu.write(stack+16, 0); cpu.write(stack+20, limit)
                entry = 0x8c018000+alignment
                pattern = bytes((i*37+seed*11+19) & 255 for i in range(24))
                cpu.store_block(entry, pattern); cpu.write(source, entry); cpu.write(source+4, entry+8)
                cpu.store_block(destination-4, bytes([0xa5])*56)
                first_writes = len(cpu.writes); calls = []; cpu.pc = begin
                while cpu.pc != end:
                    assert len(calls) <= 2
                    pc = cpu.pc; op = cpu.read(pc, 2)
                    if op & 0xf0ff == 0x400b:
                        actual = cpu.r[op >> 8 & 15]; assert actual == target
                        cpu.plain(cpu.read(pc+2, 2), pc+2)
                        arguments = cpu.r[4:7].copy(); i = len(calls)
                        assert arguments == [destination+12*i, (entry+8*i) | 0x20000000, 12]
                        result, _, _ = cpu.run(actual, arguments, stack)
                        assert result == arguments[0]
                        calls.append(arguments)
                        # The copy is actual linked C. Stronger caller-clobber
                        # values then ensure the loop uses no volatile residue.
                        for r in range(8): cpu.r[r] = (0x913579bd+seed*29+r*97) & MASK
                        cpu.macl = 0x13579bdf; cpu.sr ^= seed & 1
                        cpu.pr = pc+4; cpu.pc = pc+4
                    elif op >> 8 == 0x8d:
                        taken = bool(cpu.sr & 1)
                        cpu.plain(cpu.read(pc+2, 2), pc+2)
                        displacement = ((op & 255)^128)-128
                        cpu.pc = pc+4+2*displacement if taken else pc+4
                    else:
                        cpu.plain(op, pc); cpu.pc += 2
                assert len(calls) == limit
                assert cpu.block(destination, 12*limit) == b''.join(pattern[8*i:8*i+12] for i in range(limit))
                assert cpu.block(destination-4, 4) == bytes([0xa5])*4
                assert cpu.block(destination+12*limit, 4) == bytes([0xa5])*4
                assert cpu.block(entry, len(pattern)) == pattern
                cursor = cpu.r[13] if retained else cpu.read(stack+8)
                assert cursor == source+4*limit and cpu.read(stack+16) == 1
                for _, address, _, width in cpu.writes[first_writes:]:
                    assert (physical(destination) <= physical(address) and
                            physical(address)+width <= physical(destination)+12*limit) or (
                            stack-4 <= address < stack+24)
                cases += 1
    return cases


def cached_frame_mutations(path):
    """Clobbers of the retained frame must fail beyond the pinned call sites."""
    original = path.read_bytes()
    image, witnesses = audit_stage(original)
    if '__native_resolution_cache_seed_start' not in image.symbols:
        return 0
    pinned = {a for _, address, size in witnesses for a in range(address, address+size)}
    # Replace an otherwise unpinned SR read in the C relay. This specifically
    # exercises the retained-R9 liveness proof, not a fixed-call opcode match.
    sites = [pc for pc, op in function_ops(image, '_kui_disc_vmu_stage_relay')
             if op == 0x0002 and pc not in pinned]
    assert len(sites) == 1
    changes = (0xe900, 0x6913, 0x6096, 0x2906, 0x009f, 0x409f,
               0xd900, 0x7901, 0x390c, 0xc300)
    for opcode in changes:
        data = bytearray(original)
        struct.pack_into('<H', data, image.offset(sites[0], 2), opcode)
        try:
            audit_stage(data)
        except (ValueError, struct.error):
            pass
        else:
            raise AssertionError(f'Audit admitted retained-frame clobber {opcode:04x}')
    # A forged literal must not hide an executable R9 write from a scanner
    # that skips words merely because another instruction names them as data.
    data = bytearray(original)
    literal = (sites[0]+4) & ~3
    struct.pack_into('<H', data, image.offset(sites[0], 2), 0xd000)
    struct.pack_into('<H', data, image.offset(literal, 2), 0xe900)
    try:
        audit_stage(data)
    except (ValueError, struct.error):
        pass
    else:
        raise AssertionError('Audit admitted R9 clobber hidden by a forged literal')
    return len(changes)+1


def main():
    if len(sys.argv) != 3:
        raise SystemExit('Usage: test_disc_vmu_handoff.py <disc-stage.elf> <support.elf>')
    frame_contract()
    cases = (mutations(Path(sys.argv[1]), audit_stage)+mutations(Path(sys.argv[2]), audit_support)+
             paired_snapshot_mutations(Path(sys.argv[1]))+cached_frame_mutations(Path(sys.argv[1]))+
             retained_snapshot_mutations(Path(sys.argv[1])))
    copies = snapshot_loop_execution(Path(sys.argv[1]))
    print(f'PASS physical VMU linked handoff mutations: {cases} corruptions refused; '
          f'saved84-byte frame/late SR offset independently checked; {copies} actual snapshot/linked-memcpy executions')


if __name__ == '__main__':
    main()
