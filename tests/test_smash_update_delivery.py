#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real standalone producer with a bounded authored ELF fixture."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_smash_current_main_test as delivery
import update_package as update
from runtime_package import ADDRESS, ELF_HEADER, PROGRAM_HEADER, envelope


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='kui-main-profile-test-')
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.runtime = self.base / 'runtime.kui'
        self.elf = self.base / 'runtime.elf'
        self.output = self.base / 'test.kup'
        self.commit = '123456789abc' + '0' * 28
        payload = update.CAPABILITY + update.HANDOFF_HEADER + bytes(176)
        ident = b'\x7fELF\x01\x01\x01' + bytes(9)
        header = ELF_HEADER.pack(ident, 2, 42, 1, ADDRESS, ELF_HEADER.size,
                                 0, 0, ELF_HEADER.size, PROGRAM_HEADER.size, 1, 0, 0, 0)
        segment = PROGRAM_HEADER.pack(1, 256, ADDRESS, ADDRESS, len(payload), len(payload), 5, 4)
        data = header + segment + bytes(256 - len(header) - len(segment)) + payload
        self.elf.write_bytes(data)
        self.runtime.write_bytes(envelope(payload, len(payload), self.commit[:12]))
        self.receipt = dict(commit=self.commit, source_dirty=False,
                            elf_sha256=hashlib.sha256(data).hexdigest())
        self.elf.with_suffix('.compile.json').write_text(json.dumps(self.receipt))

    def test_uses_main26_regardless_of_local_gui_catalog_and_keeps_payload(self):
        with mock.patch.object(update, 'catalog', side_effect=AssertionError('local GUI catalog used')):
            result = delivery.build(self.runtime, self.output, self.elf)
        info = update.verify(self.output)
        self.assertEqual(len(info['members']), 26)
        self.assertEqual(info['members'][0]['policy'], 0)
        self.assertTrue(all(e['policy'] == update.INHERIT_ROOT for e in info['members'][1:]))
        self.assertEqual(self.output.read_bytes()[128 + 26 * 128:], self.runtime.read_bytes())
        self.assertEqual(result['runtime_source_commit'], self.commit)
        self.assertEqual(result['worker_replacements'], 0)
        self.assertEqual(set(p.name for p in self.base.iterdir()),
                         {'runtime.kui', 'runtime.elf', 'runtime.compile.json', 'test.kup'})

    def test_profile_is_pinned_independently_of_current_source_catalog(self):
        profile = self.base / delivery.PROFILE
        profile.parent.mkdir(parents=True)
        profile.write_text('KUI_UPDATE_MEMBER("runtime.kui")\n')
        with mock.patch.object(delivery, 'ROOT', self.base), self.assertRaises(ValueError):
            delivery.build(self.runtime, self.output, self.elf)
        self.assertFalse(self.output.exists())

    def test_refuses_kit_and_existing_delivery(self):
        with self.assertRaises(ValueError):
            delivery.build(self.runtime, self.output.with_suffix('.zip'), self.elf)
        self.output.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            delivery.build(self.runtime, self.output, self.elf)
        self.assertEqual(self.output.read_bytes(), b'keep')

    def test_changed_runtime_refused(self):
        original = self.runtime.read_bytes()
        self.runtime.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        with self.assertRaises(ValueError):
            delivery.build(self.runtime, self.output, self.elf)
        self.assertFalse(self.output.exists())

    def test_dirty_or_unrelated_compile_receipt_refused(self):
        for changes in ({'source_dirty': True}, {'elf_sha256': '0' * 64}, {'commit': 'a' * 40}):
            with self.subTest(changes=changes):
                self.elf.with_suffix('.compile.json').write_text(json.dumps({**self.receipt, **changes}))
                with self.assertRaises(ValueError):
                    delivery.build(self.runtime, self.output, self.elf)
                self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
