#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise embedded collection delivery with bounded authored SH ELF fixtures."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_smash_collection_update as delivery
import update_package as update
from runtime_package import ADDRESS, ELF_HEADER, PROGRAM_HEADER, envelope
from smash_ce_profile import profile
from build_runtime_module_bundle import encode
from check_loader_layout import SH, SYM


def digest(data):
    return hashlib.sha256(data).hexdigest()


class CollectionDeliveryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='kui-collection-update-test-')
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.commit = '123456789abc' + '0' * 28
        self.output = self.base / 'test.kup'
        self.source = self.base / 'src/current.c'
        self.source.parent.mkdir()
        self.source.write_bytes(b'qualified source\n')
        self.report = dict(schema='kui.smash.collection-focused.v1', status='PASS',
            source_commit=self.commit, source_dirty=False, development_validation_only=False,
            source_sha256={'src/current.c': digest(self.source.read_bytes())},
            reports={'native_worker_bytes': 72 * 1024}, install_files={}, artifacts_sha256={})
        self.raws = {}
        for i, name in enumerate(delivery.STAGES):
            payload = b'authored stage\0\0'
            if i >= 2:
                kind = i - 1
                raw = b'helper body ' + delivery.HELPER_COMPONENT_BUILD.encode() + bytes(40)
                self.raws[kind] = raw
                self.report['reports']['CE_' + str(kind)] = dict(
                    helper_component_build=delivery.HELPER_COMPONENT_BUILD,
                    helper_qualified_source_commit=self.commit,
                    helper_body=dict(build=delivery.HELPER_COMPONENT_BUILD, kind=kind,
                        raw_sha256=digest(raw), authored_fixture_only=True))
                prepared = raw + bytes(49152 - len(raw))
                payload = delivery.HELPER_HEADER.pack(0x5349554b, 0x31484543, 1, 32,
                    kind, len(raw), 49152, 0) + raw + profile(raw, kind, embedded=True)
                payload += struct.pack('<11I', 0x32554d56, 0x44414f4c, 2, 44,
                    49152, zlib.crc32(prepared), 0, 0, 0, 0, 0)
            module, _ = self.module(str(i), payload)
            self.report['install_files']['KUI/' + name] = dict(path=str(module), sha256=digest(module.read_bytes()))
            if i >= 2:
                helper = module.parent / 'helper'
                helper.mkdir()
                (helper / 'helper.bin').write_bytes(raw)
                (helper / 'prepared.bin').write_bytes(prepared)
                (helper / 'helper.elf').write_bytes(b'qualified helper ELF fixture' + bytes([kind]))
                self.report['artifacts_sha256']['CE' + str(kind) + '/helper.elf'] = digest((helper / 'helper.elf').read_bytes())
                self.report['install_files']['KUI/' + delivery.HELPERS[kind]] = dict(
                    path=str(helper / 'prepared.bin'), sha256=digest(prepared))
        self.report_path = self.base / 'qualification.json'
        self.save_report()
        self.bundled = {name: Path(self.report['install_files']['KUI/' + name]['path']).read_bytes()
                        for name in delivery.STAGES}
        self.bundle = encode(self.bundled, self.commit[:12])
        self.gui(self.bundle)
        for patch in (mock.patch.object(delivery, 'ROOT', self.base),
                      mock.patch.object(delivery, 'git', side_effect=lambda *a: '' if a[0] == 'status' else self.commit),
                      mock.patch.object(delivery, 'validate_helper', side_effect=lambda raw, kind, build:
                          dict(build=delivery.HELPER_COMPONENT_BUILD, kind=kind,
                              raw_sha256=digest(raw), authored_fixture_only=True)
                          if raw == self.raws[kind] and build == delivery.HELPER_COMPONENT_BUILD
                          else (_ for _ in ()).throw(ValueError('fixture component bytes/build changed'))),
                      mock.patch.object(delivery, 'HELPER_PINS', {kind: dict(reference_sha256=digest(raw))
                          for kind, raw in self.raws.items()})):
            patch.start()
            self.addCleanup(patch.stop)

    def module(self, folder, payload, symbols=None):
        payload += bytes(-len(payload) % 4)
        path = self.base / folder
        path.mkdir(exist_ok=True)
        elf, module = path / 'entry.elf', path / 'module.kui'
        ident = b'\x7fELF\x01\x01\x01' + bytes(9)
        header = ELF_HEADER.pack(ident, 2, 42, 1, ADDRESS, ELF_HEADER.size,
            0, 0, ELF_HEADER.size, PROGRAM_HEADER.size, 1, 0, 0, 0)
        segment = PROGRAM_HEADER.pack(1, 256, ADDRESS, ADDRESS, len(payload), len(payload), 5, 4)
        data = header + segment + bytes(256 - len(header) - len(segment)) + payload
        if symbols:
            strings, records = bytearray(b'\0'), bytearray(SYM.size)
            for name, (address, size, kind) in symbols.items():
                name_offset = len(strings)
                strings.extend(name.encode() + b'\0')
                records.extend(SYM.pack(name_offset, address, size, 0x10 | kind, 0, 1))
            strings_at = len(data)
            data += strings
            data += bytes(-len(data) % 4)
            symbols_at = len(data)
            data += records
            sections_at = len(data)
            data += SH.pack(*([0] * 10))
            data += SH.pack(0, 1, 6, ADDRESS, 256, len(payload), 0, 0, 4, 0)
            data += SH.pack(0, 3, 0, 0, strings_at, len(strings), 0, 0, 1, 0)
            data += SH.pack(0, 2, 0, 0, symbols_at, len(records), 2, 1, 4, SYM.size)
            header = ELF_HEADER.pack(ident, 2, 42, 1, ADDRESS, ELF_HEADER.size,
                sections_at, 0, ELF_HEADER.size, PROGRAM_HEADER.size, 1, SH.size, 4, 0)
            data = header + data[ELF_HEADER.size:]
        elf.write_bytes(data)
        module.write_bytes(envelope(payload, len(payload), self.commit[:12]))
        elf.with_suffix('.compile.json').write_text(json.dumps(dict(commit=self.commit,
            source_dirty=False, elf_sha256=digest(data))))
        return module, elf

    def gui(self, bundle, overrides=None, literal_bounds=None):
        prefix = update.CAPABILITY + update.ENGINE + update.HANDOFF_HEADER + bytes(176)
        begin = ADDRESS + 16 + len(prefix)
        end = begin + len(bundle)
        # Two real SH PC-relative longword loads, with pools outside the
        # function extent: D001 -> +8, D102 -> +12.
        code = struct.pack('<4H2I', 0xd001, 0xd102, 0, 0, *(literal_bounds or (begin, end)))
        symbols = {'__kui_runtime_module_bundle_start': (begin, 0, 0),
            '__kui_runtime_module_bundle_end': (end, 0, 0),
            '_kui_runtime_module_bundle_read': (ADDRESS, 4, 2),
            '_kui_runtime_module_bundle_contains': (ADDRESS, 4, 2)}
        for name, value in (overrides or {}).items():
            if value is None:
                symbols.pop(name, None)
            else:
                symbols[name] = value
        self.runtime, self.runtime_elf = self.module('gui',
            code + prefix + bundle, symbols)

    def save_report(self):
        self.report_path.write_text(json.dumps(self.report))

    def build(self):
        return delivery.build(self.runtime, self.runtime_elf, self.report_path, self.output)

    def test_current_v2_delivers_real_stages_and_helpers_over_pinned_old_bank(self):
        result = self.build()
        info = update.verify(self.output)
        expected = {'runtime.kui', *delivery.STAGES, *delivery.HELPERS.values()}
        self.assertEqual({r['path'] for r in info['members']}, expected)
        self.assertEqual(len(info['members']), 7)
        self.assertEqual(info['format'], 2)
        self.assertFalse(info['full'])
        for row in info['members']:
            self.assertEqual(row['policy'], update.RAW_SOFTWARE if row['path'] in delivery.HELPERS.values() else update.SELF_KUI)
        self.assertEqual(result['embedded_stage_modules'], 4)
        self.assertEqual(result['explicit_stage_replacements'], 4)
        self.assertEqual(result['raw_helper_replacements'], 2)
        self.assertEqual(result['updater_consumer']['decoder'], 'current-source')
        self.assertEqual(result['updater_consumer']['format'], 2)
        self.assertEqual(len(result['updater_consumer']['source_sha256']), 64)
        self.assertFalse(result['external_root_helpers_required'])
        self.assertTrue(result['root_dispatcher_upgrade_required'])
        self.assertTrue(all(r['component_build'] == delivery.HELPER_COMPONENT_BUILD
                            and r['qualified_source_commit'] == self.commit
                            for r in result['embedded_helpers'].values()))
        self.assertNotEqual(delivery.HELPER_COMPONENT_BUILD, self.commit[:12])
        self.assertEqual(len(list(self.base.glob('*.kup'))), 1)

    def test_old_gui_cannot_downgrade_current_updater(self):
        # A structurally valid old GUI/bundle cannot enter a new test package.
        prefix = self.runtime.read_bytes()[64:]
        self.assertIn(update.ENGINE, prefix)
        self.gui(self.bundle)
        data = self.runtime.read_bytes()
        data = data[:64] + data[64:].replace(update.ENGINE, bytes(len(update.ENGINE)))
        payload = data[64:]
        # Rebuild matching ELF/receipt so the engine gate, not envelope identity,
        # is the reason for refusal.
        self.runtime, self.runtime_elf = self.module('old-gui', payload)
        # The missing GUI bundle symbols independently make it unqualified;
        # direct current producer admission must still reject its engine.
        with self.assertRaises(ValueError):
            update.member_info_v2('runtime.kui', self.runtime.read_bytes())
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse(self.output.exists())

    def test_gui_must_embed_exact_qualified_stage_bytes_and_build(self):
        name = delivery.STAGES[0]
        original = self.bundled[name]
        changed = original[64:-1] + bytes([original[-1] ^ 1])
        different = {**self.bundled, name: envelope(changed, len(changed), self.commit[:12])}
        self.gui(encode(different, self.commit[:12]))
        with self.assertRaises(ValueError):
            self.build()
        self.gui(self.bundle[:32] + b'abcdefabcdef' + self.bundle[44:])
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse(self.output.exists())

    def test_gui_codec_must_bind_defined_SH_ABI_symbols_and_literal_pools(self):
        cases = ({'__kui_runtime_module_bundle_start': (0, 0, 0)},
                 {'_kui_runtime_module_bundle_read': None},
                 {'___kui_runtime_module_bundle_start': (0, 0, 0)})
        for overrides in cases:
            with self.subTest(overrides=overrides):
                self.gui(self.bundle, overrides)
                with self.assertRaises(ValueError):
                    self.build()
        self.gui(self.bundle, literal_bounds=(0, 0))
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse(self.output.exists())

    def test_unqualified_source_or_worker_geometry_refused_before_output(self):
        for changes in ({'source_dirty': True}, {'source_commit': 'a' * 40},
                        {'development_validation_only': True}, {'reports': {'native_worker_bytes': 112 * 1024}}):
            with self.subTest(changes=changes):
                self.save_report()
                changed = {**self.report, **changes}
                self.report_path.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):
                    self.build()
                self.assertFalse(self.output.exists())

    def test_changed_stage_or_link_receipt_refused(self):
        stage = Path(self.report['install_files']['KUI/' + delivery.STAGES[0]]['path'])
        original = stage.read_bytes()
        stage.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        with self.assertRaises(ValueError):
            self.build()
        stage.write_bytes(original)
        receipt = stage.with_name('entry.compile.json')
        receipt.write_text(json.dumps(dict(commit=self.commit, source_dirty=True,
            elf_sha256=digest(stage.with_name('entry.elf').read_bytes()))))
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse(self.output.exists())

    def test_helper_component_identity_is_independent_and_exact(self):
        original = self.report['reports']['CE_1']
        for changed in ({}, {**original, 'helper_component_build': self.commit[:12]},
                        {**original, 'helper_qualified_source_commit': '0' * 40},
                        {**original, 'helper_body': {'wrong': 'proof'}}):
            with self.subTest(changed=changed):
                self.report['reports']['CE_1'] = changed
                self.save_report()
                with self.assertRaises(ValueError):
                    self.build()
                self.assertFalse(self.output.exists())
        self.report['reports']['CE_1'] = original
        self.save_report()
        with mock.patch.dict(delivery.HELPER_PINS, {1: dict(reference_sha256='0' * 64)}):
            with self.assertRaises(ValueError):
                self.build()
            self.assertFalse(self.output.exists())

    def test_changed_helper_elf_refused(self):
        helper = Path(self.report['install_files']['KUI/' + delivery.HELPERS[1]]['path']).parent
        (helper / 'helper.elf').write_bytes(b'changed linked helper')
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse(self.output.exists())

    def test_descriptor_and_owner_corruption_refused(self):
        raw = self.raws[1]
        header = delivery.HELPER_HEADER.pack(0x5349554b, 0x31484543, 1, 32, 1, len(raw), 49152, 0)
        padded = raw + bytes(49152 - len(raw))
        late = struct.pack('<11I', 0x32554d56, 0x44414f4c, 2, 44, 49152,
            zlib.crc32(padded), 0, 0, 0, 0, 0)
        good = header + raw + profile(raw, 1, embedded=True) + late
        self.assertEqual(delivery.embedded_helper(good, raw, 1)['profile_version'], 2)
        cases = (good + header + raw, b'\0' + good, good[:-1],
                 header + raw + profile(raw, 1) + late,
                 header + raw + profile(raw, 1, embedded=True) + late[:-4] + b'bad!')
        for payload in cases:
            with self.subTest(payload_bytes=len(payload)), self.assertRaises(ValueError):
                delivery.embedded_helper(payload, raw, 1)
        with self.assertRaises(ValueError):
            delivery.embedded_helper(good, raw[:-4] + b'bad!', 1)

    def test_existing_or_wrong_format_output_not_overwritten(self):
        self.output.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            self.build()
        self.assertEqual(self.output.read_bytes(), b'keep')
        with self.assertRaises(ValueError):
            delivery.build(self.runtime, self.runtime_elf, self.report_path, self.output.with_suffix('.zip'))


if __name__ == '__main__':
    unittest.main()
