#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused admission tests for the BAM4 native-owner compiled-body profile."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_armada_ce_excerpt as audit


def fixture(bam4):
    unit = 'armada_ce_stream_player.c'
    payload, locals_, sizes = bytearray(), {}, {}
    for name, expected in audit.stream_owner_bodies(bam4=bam4).items():
        payload.extend(bytes(-len(payload) % 4))
        start = audit.STREAM_BASE + len(payload)
        payload.extend(expected)
        if name.startswith('___'):
            sizes[name] = start, len(expected)
        else:
            locals_[unit, name] = start, len(expected)
    player, player_bytes = 0x8cfbd000, 4100
    locals_[unit, '_player'] = player, player_bytes
    symbols = {name: address for name, (address, _) in sizes.items()}
    symbols.update({'__armada_bind_bss_begin': player,
                    '__armada_bind_bss_end': player + player_bytes})
    for name, relocations in audit.STREAM_OWNER_RELOCS.items():
        start, _ = locals_[unit, name]
        for offset, target_name, addend in relocations:
            target = sizes[target_name] if target_name.startswith('___') else locals_[unit, target_name]
            struct.pack_into('<I', payload, start - audit.STREAM_BASE + offset, target[0] + addend)
    return {'payload': payload, 'symbols': symbols}, locals_, sizes


class Bam4OwnerAudit(unittest.TestCase):
    def test_default_pins_unchanged(self):
        self.assertEqual(audit.stream_owner_bodies(), audit.STREAM_OWNER_BODIES)

    def test_only_reviewed_literal_words_change(self):
        changed = {(name, at) for name, code in audit.stream_owner_bodies(bam4=True).items()
                   for at in range(len(code)) if code[at] != audit.STREAM_OWNER_BODIES[name][at]}
        allowed = {(name, word + byte) for name, words in {
            '_read_native': (72,), '_kui_armada_ce_stream_owner_word': (348, 376, 496)
        }.items() for word in words for byte in range(4)}
        self.assertTrue(changed)
        self.assertLessEqual(changed, allowed)

    def test_boot_profile_keeps_instructions_and_selects_exact_romhdr(self):
        self.assertEqual(audit.stream_boot_bodies(), audit.STREAM_BOOT_BODIES)
        bam4 = audit.stream_boot_bodies(bam4=True)
        key = 'stage', '_kui_retail_stage_relay'
        for body in bam4:
            if body != key:
                self.assertEqual(bam4[body], audit.STREAM_BOOT_BODIES[body])
        self.assertEqual(bam4[key][0x234:0x238], struct.pack('<I', 0xac1ce19c))
        expected = bytearray(audit.STREAM_BOOT_BODIES[key])
        expected[0x234:0x238] = struct.pack('<I', 0xac1ce19c)
        for offset, _, _, _ in audit.BAM4_STAGE_LINKED_TARGETS:
            expected[offset:offset + 4] = bytes(4)
        self.assertEqual(bam4[key], expected)

    def test_boot_linked_literals_require_named_function_or_exact_message(self):
        payload, symbols, code = bytearray(4096), {}, bytearray(912)
        position = 1024
        for offset, _, symbol, message in audit.BAM4_STAGE_LINKED_TARGETS:
            target = audit.STAGE + position
            if symbol:
                symbols[symbol] = target
                position += 4
            else:
                encoded = message.encode('ascii') + b'\0'
                payload[position:position + len(encoded)] = encoded
                position += len(encoded)
            struct.pack_into('<I', code, offset, target)
        image = {'payload': payload, 'symbols': symbols}
        audit.audit_bam4_stage_literals(bytearray(code), image)
        for offset, _, _, _ in audit.BAM4_STAGE_LINKED_TARGETS:
            broken = bytearray(code)
            value = struct.unpack_from('<I', broken, offset)[0]
            struct.pack_into('<I', broken, offset, value + 1)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                audit.audit_bam4_stage_literals(broken, image)

    def test_each_profile_accepts_its_own_full_bodies(self):
        for bam4 in (False, True):
            with self.subTest(bam4=bam4):
                proof = audit.audit_stream_owner_code(*fixture(bam4), bam4=bam4)
                self.assertEqual(proof['kernel_profile'], 'bam4-us' if bam4 else 'armada-us')

    def test_profiles_are_not_interchangeable(self):
        for bam4 in (False, True):
            with self.subTest(bam4=bam4), self.assertRaises(ValueError):
                audit.audit_stream_owner_code(*fixture(bam4), bam4=not bam4)

    def test_bam4_still_rejects_instruction_and_each_global_corruption(self):
        original, locals_, sizes = fixture(True)
        points = [('_read_native', 0), ('_read_native', 72),
                  ('_kui_armada_ce_stream_owner_word', 348),
                  ('_kui_armada_ce_stream_owner_word', 376),
                  ('_kui_armada_ce_stream_owner_word', 496)]
        for name, offset in points:
            with self.subTest(name=name, offset=offset):
                payload = bytearray(original['payload'])
                payload[locals_['armada_ce_stream_player.c', name][0] - audit.STREAM_BASE + offset] ^= 1
                with self.assertRaises(ValueError):
                    audit.audit_stream_owner_code(dict(original, payload=payload), locals_, sizes, bam4=True)


if __name__ == '__main__':
    unittest.main()
