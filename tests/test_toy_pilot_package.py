#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse stale or mixed geometry/ABI/transport metadata in the block package."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import package_cdda_toy_pilot as package


class BlockPackageAdmission(unittest.TestCase):
    def test_geometry_and_retained_capture_proof(self):
        source = (ROOT / 'include/kui/toy_pilot_ring.h').read_bytes()
        admitted = package.ring_contract(source)
        self.assertEqual((admitted['KUI_TOY_RING_BLOCKS'], admitted['KUI_TOY_RING_BLOCK']),
                         (8, 4096))
        self.assertEqual((admitted['KUI_TOY_RING_FRAMES'], admitted['KUI_TOY_RING_HALF']),
                         (32768, 16384))
        for before, after in ((b'4096u', b'16384u'), (b'16384u', b'4096u'),
                              (b'39063u', b'78126u'), (b'3907u', b'1u'),
                              (b'64u', b'128u')):
            with self.subTest(change=(before, after)), self.assertRaises(ValueError):
                package.ring_contract(source.replace(before, after, 1))
        with self.assertRaises(ValueError):
            package.ring_contract(source + b'\n#define KUI_TOY_RING_BLOCKS 8u\n')

    def test_snapshot_geometry_rejects_old_and_mixed_abi(self):
        source = (ROOT / 'include/kui/toy_pilot.h').read_bytes()
        admitted = package.pilot_contract(source)
        self.assertEqual(admitted['KUI_TOY_PILOT_API'], 8)
        self.assertEqual(admitted['KUI_TOY_PILOT_SOUND_BYTES'], 131072)
        for before, after in ((b'PILOT_API 8u', b'PILOT_API 7u'),
                              (b'PILOT_BLOCKS 8u', b'PILOT_BLOCKS 2u'),
                              (b'PILOT_RING_MONO_BYTES 65536u', b'PILOT_RING_MONO_BYTES 16384u'),
                              (b'PILOT_RAW_SECTORS 1u', b'PILOT_RAW_SECTORS 8u'),
                              (b'PILOT_BANK_FRAMES KUI_TOY_PILOT_BLOCK_FRAMES',
                               b'PILOT_BANK_FRAMES 16384u')):
            with self.subTest(change=(before, after)), self.assertRaises(ValueError):
                package.pilot_contract(source.replace(before, after, 1))
        legend = package.snapshot_legend()
        self.assertEqual(len(legend), 7)
        self.assertEqual(sum(map(len, legend)), 112)
        self.assertEqual(legend[6][4:6], ['reserve_last_bank', 'reserve_last_bank_filled'])
        self.assertEqual(legend[6][-2:], ['reserve_last_window_ticks', 'reserve_last_window_calls'])

    def test_isolated_test_refuses_transport_or_data_step_changes(self):
        self.assertEqual(package.OUTPUT_NAME, 'K-UI-Toy-Video-Streaming-Test.zip')
        self.assertEqual(package.GD_RUNTIME_FILE, 'pilot/15-toy-gd-three-sector.kui')
        self.assertEqual(package.README_SOURCE, 'docs/cdda-toy-video-test.md')
        for parameters in ({'gd_fixed_step': 0}, {'gd_fixed_step': 2}, {'sci_build_dir': 'unused'}):
            with self.subTest(parameters=parameters), self.assertRaises(ValueError):
                package.collect_bundle('0' * 40, None, **parameters)
        self.assertEqual(package.selected_checklist(b'exact reviewed checklist', 3),
                         b'exact reviewed checklist')
        with self.assertRaises(ValueError):
            package.selected_checklist(b'exact reviewed checklist', 0)


if __name__ == '__main__':
    unittest.main()
