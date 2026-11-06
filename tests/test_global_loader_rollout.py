#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guard complete global role coverage and exact linked module binding."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from global_loader_rollout import DEFAULT_BUILDS, INSTALL_TARGETS, collect, linked_module
from test_image_loader_layout import executable
from runtime_package import ADDRESS, verify


class GlobalRollout(unittest.TestCase):
    def test_complete_twelve_role_fifteen_file_install_contract(self):
        self.assertEqual(len(DEFAULT_BUILDS), 12)
        self.assertEqual(len(INSTALL_TARGETS), 15)
        for name in ('cdda-toy-raw', 'cdda-toy-cooked', 'retail-cdda-mdk2',
                     'cdda-native-toy-raw', 'cdda-native-toy-cooked', 'cdda-native-mdk2'):
            self.assertIn('KUI/apps/games/' + name + '.kui', INSTALL_TARGETS)
        self.assertNotIn('KUI/apps/games/retail-boot.kui', INSTALL_TARGETS)
        self.assertFalse(any('silver' in name or 'vmu-toy' in name for name in INSTALL_TARGETS))
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            collect({k:v for k,v in DEFAULT_BUILDS.items() if k != 'ce'}, '1'*40)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            collect({**DEFAULT_BUILDS, 'old_foreground':Path('irrelevant')}, '1'*40)

    def test_exact_entry_stage_and_clean_receipt_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            commit = '0123456789abcdef0123456789abcdef01234567'
            stage = b'\x09\0\x09\0' * 8
            (directory / 'stage.bin').write_bytes(stage)
            (directory / 'stage.elf').write_bytes(executable(0x8ce00000, stage, len(stage), {}))
            entry = directory / 'entry.elf'
            entry.write_bytes(executable(ADDRESS, b'\x09\0\x09\0' * 2048 + stage, 0x2000 + len(stage), {}))
            receipt = dict(commit=commit, source_dirty=False,
                           elf_sha256=hashlib.sha256(entry.read_bytes()).hexdigest())
            path = entry.with_suffix('.compile.json')
            path.write_text(json.dumps(receipt))
            data, record = linked_module(directory, commit, False)
            self.assertEqual(verify(data)['build'], commit[:12])
            self.assertEqual(record['stage_bin_sha256'], hashlib.sha256(stage).hexdigest())
            for delta in (dict(commit='f'*40), dict(source_dirty=True), dict(elf_sha256='0'*64)):
                path.write_text(json.dumps({**receipt, **delta}))
                with self.assertRaises(ValueError):linked_module(directory, commit, False)
            path.write_text(json.dumps(receipt))
            (directory / 'stage.bin').write_bytes(stage[:-1] + b'\x01')
            with self.assertRaisesRegex(ValueError, 'exact stage'):linked_module(directory, commit, False)
            (directory / 'stage.bin').write_bytes(stage)
            (directory / 'stage.elf').write_bytes(executable(0x8ce00000, stage[:-1] + b'\x01', len(stage), {}))
            with self.assertRaisesRegex(ValueError, 'differs from its ELF'):linked_module(directory, commit, False)


if __name__ == '__main__':unittest.main()
