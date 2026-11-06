#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Adversarial mutations of the actual linked texture-call count binding."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_vc2_texture_probe import audit_low_entry, payload_at
from check_vmu_native_worker_gateway import read_elf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resident', type=Path,
                        default=ROOT / 'build/vc2-texture-probe/resident-sci.elf')
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    image = read_elf(args.resident)
    report = audit_low_entry(image)
    pre, post = report['calls']
    pre_load, pre_call = int(pre['literal_load'], 16), int(pre['call'], 16)
    post_load, post_call = int(post['literal_load'], 16), int(post['call'], 16)
    save = int(pre['incoming_count_save'], 16)
    # This focused test intentionally binds the current emitted scheduling:
    # POST's original-count move is immediately before its entry literal.
    assert struct.unpack('<H', payload_at(image, post_load - 2, 2))[0] == 0x66B3
    marker_op = struct.unpack('<H', payload_at(image, pre_load + 2, 2))[0]
    assert marker_op >> 12 == 13 and marker_op >> 8 & 15 == 6
    marker = ((pre_load + 6) & ~3) + 4 * (marker_op & 255)
    mutations = [
        ('incoming_count_saved_from_wrong_argument', save, '<H', 0x6B53),
        ('saved_count_overwritten', save + 2, '<H', 0xEB01),
        ('post_count_uses_wrong_register', post_load - 2, '<H', 0x66A3),
        ('pre_marker_wrong_bit', marker, '<I', 0x40000000),
        ('pre_slot_ands_instead_of_ors', pre_call + 2, '<H', 0x26B9),
        ('post_slot_rewrites_count', post_call + 2, '<H', 0x66A3),
        ('post_window_rewrites_count', post_load + 2, '<H', 0x66A3),
    ]
    rejected = []
    for label, address, encoding, value in mutations:
        size = struct.calcsize(encoding)
        loads = [p for p in image[2] if p[0] == 1 and
                 p[2] <= address < address + size <= p[2] + p[4]]
        assert len(loads) == 1
        changed = bytearray(image[0])
        struct.pack_into(encoding, changed, loads[0][1] + address - loads[0][2], value)
        try:
            audit_low_entry((bytes(changed), *image[1:]))
        except ValueError:
            rejected.append(label)
        else:
            raise AssertionError('accepted count-binding mutation: ' + label)
    result = dict(status='PASS', schema='kui.vc2.low-count-binding.v1',
                  resident=str(args.resident),
                  elf_sha256=hashlib.sha256(image[0]).hexdigest(),
                  actual_calls=report['calls'], mutation_rejections=rejected,
                  hardware_executed=False,
                  limits=['Checks static emitted count provenance and adversarial mutations.',
                          'No payload, device, MMU, cache or console execution is modeled.'])
    encoded = json.dumps(result, indent=2) + '\n'
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(encoded)
    print(encoded, end='')


if __name__ == '__main__':
    main()
