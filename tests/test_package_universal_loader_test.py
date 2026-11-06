#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify mixed accepted/current software through the real v2 producer/consumer."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import package_universal_loader_test as delivery
import update_package as update
from build_runtime_module_bundle import encode
from runtime_package import ADDRESS, envelope
from smash_ce_profile import profile
import test_smash_collection_update as fixture

COMMIT = '123456789abc' + '0' * 28


class UniversalDelivery(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='kui-universal-package-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.commit = COMMIT
        self.output = self.base / 'test.kup'
        self.evidence = self.base / 'evidence'
        self.accepted = self.base / 'accepted.zip'
        self.smash = {}
        for index, name in enumerate(delivery.STAGES):
            payload = b'accepted cc81 stage' + bytes(16)
            if index >= 2:
                kind = index - 1
                helper = bytes([kind]) + bytes(49151)
                self.smash['KUI/' + delivery.HELPERS[kind]] = helper
                payload += profile(helper, kind, embedded=True)
            self.smash['KUI/' + name] = envelope(payload + bytes(-len(payload) % 4),
                (len(payload) + 3) & ~3, delivery.ACCEPTED_BUILD)
        with zipfile.ZipFile(self.accepted, 'w') as archive:
            for name, body in self.smash.items():archive.writestr(name, body)
            archive.writestr('KUI/vmu-test/private-profile.bin', b'must never enter package')
        self.archive_sha = hashlib.sha256(self.accepted.read_bytes()).hexdigest()
        self.cold = {name: envelope(b'new cold reader bytes'.ljust(24, b'\0'), 24, COMMIT[:12])
                     for name in delivery.INSTALL_TARGETS}
        self.cold['evidence/global-loader-rollout.json'] = b'{"qualified_cold_roles":12}'
        self.cold_report = dict(status='PASS', source_commit=COMMIT, installed_files=sorted(delivery.INSTALL_TARGETS))
        self.gui()
        for selected in (patch.object(delivery, 'source_state', return_value=dict(commit=COMMIT, dirty=False)),
                         patch.object(delivery, 'ACCEPTED_ZIP_SHA256', self.archive_sha),
                         patch.object(delivery, 'collect', side_effect=lambda *a, **kw:(self.cold, self.cold_report))):
            selected.start()
            self.addCleanup(selected.stop)

    def gui(self, members=None, symbols_override=None):
        bundle = encode(members or {name: self.smash['KUI/' + name] for name in delivery.STAGES},
                        delivery.ACCEPTED_BUILD)
        prefix = update.CAPABILITY + update.ENGINE + update.HANDOFF_HEADER + bytes(176)
        begin = ADDRESS + 16 + len(prefix)
        end = begin + len(bundle)
        code = struct.pack('<4H2I', 0xd001, 0xd102, 0, 0, begin, end)
        symbols = {'__kui_runtime_module_bundle_start': (begin, 0, 0),
            '__kui_runtime_module_bundle_end': (end, 0, 0),
            '_kui_runtime_module_bundle_read': (ADDRESS, 4, 2),
            '_kui_runtime_module_bundle_contains': (ADDRESS, 4, 2)}
        symbols.update(symbols_override or {})
        self.runtime, self.elf = fixture.CollectionDeliveryTests.module(self, 'gui', code + prefix + bundle, symbols)

    def build(self):
        return delivery.build(self.runtime, self.elf, self.accepted, self.output, self.evidence)

    def test_twenty_one_real_replacements_preserve_ce_pilot_and_accepted_smash(self):
        report = self.build()
        checked = update.verify(self.output)
        self.assertFalse(checked['full'])
        self.assertEqual(checked['engine_minimum'], 3)
        self.assertEqual(len(checked['members']), 21)
        self.assertEqual({row['path'] for row in checked['members']},
                         {name[4:] for name in delivery.REPLACEMENTS})
        self.assertNotIn('apps/games/ce-probe.kui', {row['path'] for row in checked['members']})
        self.assertFalse(any(row['policy'] == update.REMOVE for row in checked['members']))
        self.assertEqual(report['package']['consumer']['decoder'], 'current-source')
        self.assertFalse(report['full_global_rollout'])
        self.assertFalse(report['personalized_card_profile_packaged'])
        actual = {p.relative_to(self.evidence / 'staged').as_posix(): p.read_bytes()
                  for p in (self.evidence / 'staged').rglob('*') if p.is_file()}
        self.assertEqual(set(actual), delivery.REPLACEMENTS)
        for name, body in self.smash.items():self.assertEqual(actual[name], body)
        self.assertEqual(actual['KUI/runtime.kui'], self.runtime.read_bytes())
        self.assertFalse((self.evidence / 'checked-test.kup').exists())
        self.assertEqual(json.loads((self.evidence / 'universal-loader-test.json').read_text()), report)

    def test_dirty_source_and_unpinned_archive_refuse_before_publication(self):
        with patch.object(delivery, 'source_state', return_value=dict(commit=COMMIT, dirty=True)):
            with self.assertRaisesRegex(ValueError, 'Commit the intended'):
                self.build()
        with patch.object(delivery, 'ACCEPTED_ZIP_SHA256', 'f' * 64):
            with self.assertRaisesRegex(ValueError, 'pinned cc81'):
                self.build()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.evidence.exists())

    def test_historical_external_helper_profile_cannot_replace_embedded_v2(self):
        for kind, helper_name in delivery.HELPERS.items():
            with self.subTest(kind=kind):
                changed = dict(self.smash)
                helper = changed['KUI/' + helper_name]
                child = 'KUI/' + delivery.STAGES[2 if kind == 1 else 3]
                payload = changed[child][64:]
                current, historical = profile(helper, kind, embedded=True), profile(helper, kind)
                self.assertEqual(payload.count(current), 1)
                payload = payload.replace(current, historical)
                changed[child] = envelope(payload, len(payload), delivery.ACCEPTED_BUILD)
                archive = self.base / ('historical-' + str(kind) + '.zip')
                with zipfile.ZipFile(archive, 'w') as target:
                    for name, body in changed.items():target.writestr(name, body)
                with patch.object(delivery, 'ACCEPTED_ZIP_SHA256', hashlib.sha256(archive.read_bytes()).hexdigest()):
                    with self.assertRaisesRegex(ValueError, 'child/helper ownership'):
                        delivery.accepted_smash(archive)

    def test_changed_accepted_gui_bundle_or_its_actual_bounds_refuse(self):
        members = {name: self.smash['KUI/' + name] for name in delivery.STAGES}
        name = delivery.STAGES[0]
        members[name] = envelope(b'another accepted-build-looking stage'.ljust(40, b'\0'), 40, delivery.ACCEPTED_BUILD)
        self.gui(members)
        with self.assertRaisesRegex(ValueError, 'exact four accepted'):
            self.build()
        self.gui(symbols_override={'__kui_runtime_module_bundle_start': (ADDRESS + 4, 0, 0)})
        with self.assertRaisesRegex(ValueError, 'complete linked bundle'):
            self.build()
        self.assertFalse(self.output.exists())

    def test_missing_cold_role_and_foreign_qualification_refuse(self):
        self.cold_report['source_commit'] = 'a' * 40
        with self.assertRaisesRegex(ValueError, 'Cold-reader qualification'):
            self.build()
        self.cold_report['source_commit'] = COMMIT
        self.cold.pop(delivery.CE_PROBE)
        with self.assertRaisesRegex(ValueError, 'Cold-reader qualification'):
            self.build()
        self.assertFalse(self.evidence.exists())

    def test_stale_gui_receipt_or_changed_source_cannot_produce_a_candidate(self):
        receipt = self.elf.with_suffix('.compile.json')
        data = json.loads(receipt.read_text())
        receipt.write_text(json.dumps(dict(data, source_dirty=True)))
        with self.assertRaisesRegex(ValueError, 'clean-source'):
            self.build()
        receipt.write_text(json.dumps(data))
        with patch.object(delivery, 'source_state', side_effect=[dict(commit=COMMIT, dirty=False),
                    dict(commit='a' * 40, dirty=False)]):
            with self.assertRaisesRegex(ValueError, 'Source changed'):
                self.build()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.evidence.exists())

    def test_producer_failure_never_publishes_success_or_staged_files(self):
        with patch.object(delivery, 'make_update', side_effect=ValueError('consumer refused')):
            with self.assertRaisesRegex(ValueError, 'consumer refused'):
                self.build()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.evidence.exists())


if __name__ == '__main__':
    unittest.main()
