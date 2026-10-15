#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused title/bank admission tests; no streaming timing simulation."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_armada_ce_excerpt as audit


def owner_fixture():
    unit = 'armada_ce_stream_player.c'
    base, limit, _, _ = audit.stream_layout(worms=True)
    payload, locals_, sizes = bytearray(), {}, {}
    for name, expected in audit.stream_owner_bodies(worms=True).items():
        payload.extend(bytes(-len(payload) % 4))
        start = base + len(payload)
        payload.extend(expected)
        if name.startswith('___'):
            sizes[name] = start, len(expected)
        else:
            locals_[unit, name] = start, len(expected)
    player, player_bytes = limit - 8192, 4100
    locals_[unit, '_player'] = player, player_bytes
    symbols = {name: address for name, (address, _) in sizes.items()}
    symbols.update({'__armada_bind_bss_begin': player,
                    '__armada_bind_bss_end': player + player_bytes})
    for name, relocations in audit.STREAM_OWNER_RELOCS.items():
        start, _ = locals_[unit, name]
        for offset, target_name, addend in relocations:
            target = sizes[target_name] if target_name.startswith('___') else locals_[unit, target_name]
            struct.pack_into('<I', payload, start - base + offset, target[0] + addend)
    return {'payload': payload, 'symbols': symbols}, locals_, sizes


class WormsAudit(unittest.TestCase):
    def test_layout_is_explicit_and_does_not_mutate_other_profiles(self):
        original = audit.stream_layout()
        self.assertEqual(audit.stream_layout(worms=True),
                         (0x8cec7000, 0x8cedf000, 0x8ce70000, 0x8cef0000))
        self.assertEqual(audit.stream_layout(), original)

    def test_owner_profile_requires_worms_globals_and_reserved_bank(self):
        image, locals_, sizes = owner_fixture()
        self.assertEqual(audit.audit_stream_owner_code(image, locals_, sizes, worms=True)
                         ['kernel_profile'], 'worms-us')
        with self.assertRaises(ValueError):
            audit.audit_stream_owner_code(image, locals_, sizes)
        for name, offset in (('_read_native', 72),
                             ('_kui_armada_ce_stream_owner_word', 348),
                             ('_kui_armada_ce_stream_owner_word', 376),
                             ('_kui_armada_ce_stream_owner_word', 496)):
            payload = bytearray(image['payload'])
            payload[locals_['armada_ce_stream_player.c', name][0] - 0x8cec7000 + offset] ^= 1
            with self.subTest(name=name, offset=offset), self.assertRaises(ValueError):
                audit.audit_stream_owner_code(dict(image, payload=payload), locals_, sizes, worms=True)

    def test_controller_footer_requires_worms_bank(self):
        base, limit, _, _ = audit.stream_layout(worms=True)
        end, bss_end = base + 64, base + 128
        footer = struct.pack('<8I', audit.STREAM_CONTRACT_MAGIC, 1, base,
                             end, end, bss_end, limit, base)
        image = {'payload': bytes(32) + footer, 'memory_end': bss_end,
                 'symbols': {'_kui_armada_ce_bind_probe': base,
                             '__armada_bind_binary_end': end,
                             '__armada_stream_contract': end - 32,
                             '__armada_bind_bss_begin': end,
                             '__armada_bind_bss_end': bss_end}}
        audit.audit_stream_contract(image, worms=True)
        with self.assertRaises(ValueError):
            audit.audit_stream_contract(image)
        for offset in (8, 24, 28):
            broken = bytearray(image['payload']); broken[32 + offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                audit.audit_stream_contract(dict(image, payload=broken), worms=True)

    def test_source_seed_requires_worms_pool_and_map(self):
        header = struct.pack('<16I', 0x32584543, 2, 0x8ce70040,
                             audit.PCM_BYTES, 0x8cedf000, 4096, 0, 352768, *([0] * 8))
        stage = {'payload': header + bytes(4096),
                 'symbols': {'_armada_excerpt_seed': audit.STAGE}}
        audit.audit_seed(stage, stream=True, worms=True)
        with self.assertRaises(ValueError):
            audit.audit_seed(stage, stream=True)
        for offset in (8, 16, 64):
            broken = bytearray(stage['payload']); broken[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                audit.audit_seed(dict(stage, payload=broken), stream=True, worms=True)

    def test_boot_profile_changes_only_reviewed_literals(self):
        key = 'stage', '_kui_retail_stage_relay'
        expected = bytearray(audit.STREAM_BOOT_BODIES[key])
        for offset, value in ((0x230, 0x8ce70000), (0x234, 0xac11de70),
                              (0x248, 0xacec7000), (0x334, 0xace70000),
                              (0x340, 0xacedf000), (0x348, 0xeb39c010),
                              (0x368, 0x8cec7000)):
            struct.pack_into('<I', expected, offset, value)
        for offset, _, _, _ in audit.BAM4_STAGE_LINKED_TARGETS:
            expected[offset:offset + 4] = bytes(4)
        bodies = audit.stream_boot_bodies(worms=True)
        self.assertEqual(bodies[key], expected)
        for other in bodies:
            if other != key:
                self.assertEqual(bodies[other], audit.STREAM_BOOT_BODIES[other])

    def test_title_flags_are_mutually_exclusive(self):
        for function in (audit.stream_owner_bodies, audit.stream_boot_bodies):
            with self.assertRaises(ValueError):
                function(bam4=True, worms=True)


if __name__ == '__main__':
    unittest.main()
