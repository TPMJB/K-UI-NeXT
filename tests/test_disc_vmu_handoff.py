#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fault injection against actual linked physical-disc VMU handoff ELFs."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from check_disc_vmu_handoff import audit_stage, audit_support, function_ops


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
    return len(changes)


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
             paired_snapshot_mutations(Path(sys.argv[1]))+cached_frame_mutations(Path(sys.argv[1])))
    print(f'PASS physical VMU linked handoff mutations: {cases} corruptions refused; '
          'saved84-byte frame/late SR offset independently checked')


if __name__ == '__main__':
    main()
