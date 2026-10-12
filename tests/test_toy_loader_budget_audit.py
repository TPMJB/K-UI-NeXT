#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed checks for the separate startup DATA budget admission gate."""
import copy
import hashlib
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import toy_loader_budget_audit as audit
from toy_pilot_cache_audit import Linked


class ConfigurationTests(unittest.TestCase):
    def config(self, profile=1):
        return {**audit.REQUIRED_CONFIG, 'BUILD': '012345abcdef', 'DATA_BUDGET_PROFILE': str(profile)}

    def write(self, config, extra=''):
        area = tempfile.TemporaryDirectory()
        path = Path(area.name)
        (path / 'build-config').write_text(''.join(f'{k}={v}\n' for k, v in config.items()) + extra)
        return area, path

    def test_four_exact_budget_profiles_and_units(self):
        for profile, (name, blocks, ticks) in audit.PROFILES.items():
            area, path = self.write(self.config(profile))
            with self.subTest(profile=profile), area:
                self.assertEqual(audit.read_config(path, profile)['DATA_BUDGET_PROFILE'], str(profile))
                budget = audit.budget_metadata(profile)
                self.assertEqual((budget['name'], budget['credit_limit'], budget['tick_limit']), (name, blocks, ticks))
                self.assertEqual(budget['credit_unit'], 'verified 512-byte physical card blocks')
                self.assertEqual(budget['tick_hz'], 781250)
                self.assertAlmostEqual(budget['admission_microseconds'], 1920 if ticks == 1500 else 7680)
                self.assertTrue(budget['one_step_may_cross_tick_limit'])

    def test_changed_audio_route_and_unreviewed_keys_refused(self):
        mutations = [('DATA_BUDGET_PROFILE', '0'), ('DATA_BUDGET_PROFILE', '5'),
            ('DATA_SCHEDULE_MODE', '1'), ('DATA_SCHEDULE_MODE', '3'),
            ('DATA_PAYLOAD_MODE', '1'), ('SHARED_SCI', '1'), ('ASYNC_CDDA', '1'),
            ('GD_FIXED_STEP', '1'), ('PRIVATE_P2', '0'), ('NATIVE_CACHE', '1'),
            ('SCI_REUSE_TDRE', '1'), ('LOADER_TRACE', '0'), ('DATA_PROBE', '0'),
            ('BUILD', 'abcdef'), ('UNREVIEWED', '1')]
        for key, value in mutations:
            config = self.config(); config[key] = value
            area, path = self.write(config)
            with self.subTest(key=key, value=value), area, self.assertRaises(ValueError):
                audit.read_config(path)

    def test_duplicate_missing_and_wrong_profile_refused(self):
        config = self.config(); config.pop('DATA_PROBE')
        area, path = self.write(config)
        with area, self.assertRaisesRegex(ValueError, 'missing/unreviewed'):
            audit.read_config(path)
        area, path = self.write(self.config(), 'DATA_BUDGET_PROFILE=1\n')
        with area, self.assertRaisesRegex(ValueError, 'Duplicate'):
            audit.read_config(path)
        area, path = self.write(self.config())
        with area, self.assertRaisesRegex(ValueError, 'differs'):
            audit.read_config(path, 2)

    def test_only_twelve_block_profiles_can_change_data_work(self):
        for profile in audit.PROFILES:
            self.assertEqual(audit.changed_functions(profile, 'resident-sci'), set())
            self.assertEqual(audit.changed_functions(profile, 'stage'), set())
            self.assertEqual('_data_work' in audit.changed_functions(profile, 'worker'), profile in (2, 4))
            self.assertNotIn('_kui_toy_pilot_sci_irq', audit.changed_functions(profile, 'worker'))
            self.assertNotIn('_dispatch_body', audit.changed_functions(profile, 'worker'))
            self.assertNotIn('_kui_toy_pilot_worker_initialize', audit.changed_functions(profile, 'worker'))


class SourceAndSchemaTests(unittest.TestCase):
    def test_all_guarded_schema_profiles(self):
        for profile in audit.PROFILES:
            with self.subTest(profile=profile):
                schema = audit.audit_budget_schema(profile)
                self.assertEqual((schema['version'], schema['words'], schema['record_words']), (2, 192, 18))
                self.assertEqual(schema['record_start'] + schema['records'] * schema['record_words'], schema['words'])
                self.assertEqual(schema['budget_stats_start'] + schema['budget_stats_words'], schema['record_start'])
                self.assertEqual(schema['TRACE_words'], 416)

    def test_resolved_schema_and_budget_constant_mutations_refused(self):
        for name, before, after in (
            ('include/kui/toy_loader_schedule_report.h', 'REPORT_WORDS 192u', 'REPORT_WORDS 208u'),
            ('include/kui/toy_loader_schedule_report.h', 'RECORD_WORDS 18u', 'RECORD_WORDS 17u'),
            ('include/kui/toy_loader_schedule_report.h', 'RECORDS 8u', 'RECORDS 9u'),
            ('src/loader/toy_pilot_sci.h', 'SERVICE_BLOCKS 12u', 'SERVICE_BLOCKS 13u'),
            ('src/loader/toy_pilot_sci.h', 'SERVICE_TICKS 6000u', 'SERVICE_TICKS 7000u'),
            ('src/loader/toy_pilot_sci.h', 'SERVICE_STEPS 1024u', 'SERVICE_STEPS 2048u')):
            with self.subTest(name=name, before=before), tempfile.TemporaryDirectory() as area:
                root = Path(area)
                shutil.copytree(ROOT / 'include', root / 'include')
                (root / 'src/loader').mkdir(parents=True)
                for source in (ROOT / 'src/loader').glob('*.h'):
                    shutil.copy2(source, root / 'src/loader' / source.name)
                target = root / name
                original = target.read_text(); self.assertIn(before, original)
                target.write_text(original.replace(before, after))
                with self.assertRaisesRegex(ValueError, 'schema constant'):
                    audit.audit_budget_schema(4, root)

    def test_candidate_source_fingerprints_do_not_grant_review(self):
        observed = audit.source_identities()
        self.assertEqual(set(observed), set(audit.REVIEW_SOURCE_FILES))
        with patch.dict(audit.REVIEWED_SOURCE_SHA256, {}, clear=True), self.assertRaisesRegex(ValueError, 'Missing independently reviewed'):
            audit.audit_sources()

    def test_reviewed_source_change_refused(self):
        with tempfile.TemporaryDirectory() as area:
            root = Path(area); (root / 'source.c').write_text('original\n')
            digest = hashlib.sha256(b'original\n').hexdigest()
            with patch.object(audit, 'REVIEW_SOURCE_FILES', ('source.c',)), \
                 patch.dict(audit.REVIEWED_SOURCE_SHA256, {'source.c': digest}, clear=True), \
                 patch.dict(audit.schedule.FROZEN_SOURCES, {}, clear=True):
                self.assertEqual(audit.audit_sources(root)['reviewed_budget_sha256'], {'source.c': digest})
                (root / 'source.c').write_text('changed\n')
                with self.assertRaisesRegex(ValueError, 'Changed independently reviewed'):
                    audit.audit_sources(root)

    def test_frozen_CDDA_source_change_refused(self):
        with tempfile.TemporaryDirectory() as area:
            root = Path(area); (root / 'budget.c').write_text('budget\n'); (root / 'audio.c').write_text('changed\n')
            digest = hashlib.sha256(b'budget\n').hexdigest()
            with patch.object(audit, 'REVIEW_SOURCE_FILES', ('budget.c',)), \
                 patch.dict(audit.REVIEWED_SOURCE_SHA256, {'budget.c': digest}, clear=True), \
                 patch.dict(audit.schedule.FROZEN_SOURCES, {'audio.c': hashlib.sha256(b'original\n').hexdigest()}, clear=True), \
                 self.assertRaisesRegex(ValueError, 'frozen low reader/CDDA'):
                audit.audit_sources(root)

    def test_empty_review_tables_always_refuse_admission(self):
        for profile in audit.PROFILES:
            with self.subTest(profile=profile), patch.dict(audit.REVIEWED, {profile: {}}), \
                 self.assertRaisesRegex(ValueError, 'Missing independently reviewed'):
                audit.reviewed_shapes({'DATA_BUDGET_PROFILE': str(profile)}, {}, {}, {})

    def test_actual_private_report_geometry_and_last_record_bound(self):
        image = {'symbols': {'__toy_pilot_bss_begin': audit.HIGH + audit.P2,
                            '__toy_pilot_bss_end': audit.HIGH_LIMIT + audit.P2}, 'symbol_sizes': {}}
        at = audit.HIGH + audit.P2
        for name, size in (('_schedule_report', 768), ('_schedule_visit', 56),
                           ('_report', 1664), ('_trace_words', 1664), ('_request', 96)):
            image['symbols'][name] = at; image['symbol_sizes'][name] = size; at += size
        self.assertEqual(audit.audit_report_geometry(image)['last_legal_record_word'], 191)
        for name in image['symbol_sizes']:
            changed = copy.deepcopy(image); changed['symbol_sizes'][name] += 4
            with self.subTest(object=name), self.assertRaisesRegex(ValueError, 'geometry'):
                audit.audit_report_geometry(changed)
        changed = copy.deepcopy(image); changed['symbols']['_schedule_report'] -= audit.P2
        with self.assertRaisesRegex(ValueError, 'geometry'):
            audit.audit_report_geometry(changed)


class TargetLinkedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('sh-elf-objdump'):
            raise unittest.SkipTest('Target objdump required for actual emitted mutation fixtures')
        cls.paths = {profile: ROOT / 'build' / ('budget-' + name.lower())
                     for profile, (name, _, _) in audit.PROFILES.items()}
        if not all((path / 'retail-toy-pilot.kui').is_file() for path in cls.paths.values()):
            raise unittest.SkipTest('Build all four final DATA budget profiles before linked fixtures')
        cls.profiles = {profile: audit.load_profile(path, profile) for profile, path in cls.paths.items()}

    def test_candidate_mode_is_always_unadmitted_and_cannot_fill_tables(self):
        before = copy.deepcopy((audit.REVIEWED, audit.REVIEWED_OBJECTS, audit.REVIEWED_STACK_ROWS, audit.REVIEWED_RUNTIME_SHA256))
        for profile, path in self.paths.items():
            with self.subTest(profile=profile):
                candidate = audit.candidate(path, profile=profile)
                self.assertFalse(candidate['admitted'])
                self.assertTrue(candidate['independent_review_required'])
                self.assertEqual(candidate['retained']['baseline_runtime']['sha256'], audit.Z_RUNTIME_SHA256)
        self.assertEqual(before, (audit.REVIEWED, audit.REVIEWED_OBJECTS, audit.REVIEWED_STACK_ROWS, audit.REVIEWED_RUNTIME_SHA256))

    def test_final_candidates_require_independent_runtime_review(self):
        for profile, path in self.paths.items():
            with self.subTest(profile=profile):
                if not audit.REVIEWED_RUNTIME_SHA256[profile]:
                    with self.assertRaisesRegex(ValueError, 'independently reviewed immutable'):
                        audit.audit_budget(path, profile=profile)
                else:
                    proof = audit.audit_budget(path, profile=profile)
                    self.assertTrue(proof['admitted'])
                    self.assertEqual(proof['runtime']['sha256'], audit.REVIEWED_RUNTIME_SHA256[profile])
                    self.assertEqual(proof['schedule_schema']['words'], 192)
                    self.assertFalse(proof['hardware_behavior_verified'])
                    self.assertLessEqual(proof['stacks']['conservative_sums']['high_GD']['bytes'], 4000)

    def test_changed_runtime_pin_refused(self):
        for profile, path in self.paths.items():
            with self.subTest(profile=profile), patch.dict(audit.REVIEWED_RUNTIME_SHA256, {profile: '0' * 64}), \
                 self.assertRaisesRegex(ValueError, 'immutable artifact'):
                audit.audit_budget(path, profile=profile)

    def test_exact_vector_base_and_binary_protocol_relocations_preserve_identity(self):
        old_config, old_images, old_dis, _ = audit.reference_z()
        before = Linked(old_images['worker'], audit.HIGH, old_dis['worker'])
        for profile, (config, images, dis) in self.profiles.items():
            after = Linked(images['worker'], audit.HIGH, dis['worker'])
            for name in ('_hook', '_unhook', '_kui_retail_gd_dispatch'):
                with self.subTest(profile=profile, function=name):
                    self.assertEqual(audit.function_identity(before, name, old_config['BUILD']),
                                     audit.function_identity(after, name, config['BUILD']))

    def test_scoped_vector_target_and_register_mutations_refused(self):
        config, images, dis = self.profiles[1]
        code = Linked(images['worker'], audit.HIGH, dis['worker'])
        for name, offset, register in (('_hook', 4, 7), ('_unhook', 16, 1)):
            begin, _, _ = audit.schedule.bounded_function(code, name)
            pool = code.literal(begin + offset, register)[0]
            for mutation in ('target', 'register'):
                changed = copy.deepcopy(images['worker']); payload = bytearray(changed['payload'])
                if mutation == 'target':
                    struct.pack_into('<I', payload, pool - audit.HIGH, changed['symbols']['_kui_toy_pilot_sci_vectors'] - 0x100 + 32)
                else:
                    payload[begin + offset - audit.HIGH + 1] ^= 1
                changed['payload'] = bytes(payload)
                with self.subTest(function=name, mutation=mutation), self.assertRaises(ValueError):
                    audit.function_identity(Linked(changed, audit.HIGH, dis['worker']), name, config['BUILD'])

    def test_binary_GET_VERS_contents_cannot_change(self):
        config, images, dis = self.profiles[1]
        code = Linked(images['worker'], audit.HIGH, dis['worker'])
        begin, _, _ = audit.schedule.bounded_function(code, '_kui_retail_gd_dispatch')
        value = code.literal(begin + 0x712, 5)[1]
        protocol = bytes.fromhex('4744432056657273696f6e20312e313020313939392d30332d33310200')
        for offset in (0, len(protocol) - 2, len(protocol) - 1):
            changed = copy.deepcopy(images['worker']); payload = bytearray(changed['payload'])
            payload[value - audit.HIGH + offset] ^= 1; changed['payload'] = bytes(payload)
            with self.subTest(byte_offset=offset), self.assertRaisesRegex(ValueError, 'protocol bytes'):
                audit.function_identity(Linked(changed, audit.HIGH, dis['worker']), '_kui_retail_gd_dispatch', config['BUILD'])
        changed = copy.deepcopy(images['worker']); changed['readonly_ranges'] = []
        with self.assertRaisesRegex(ValueError, 'protocol bytes'):
            audit.function_identity(Linked(changed, audit.HIGH, dis['worker']), '_kui_retail_gd_dispatch', config['BUILD'])
        changed = copy.deepcopy(images['worker']); payload = bytearray(changed['payload'])
        payload[begin + 0x712 - audit.HIGH + 1] ^= 1; changed['payload'] = bytes(payload)
        with self.assertRaises(ValueError):
            audit.function_identity(Linked(changed, audit.HIGH, dis['worker']), '_kui_retail_gd_dispatch', config['BUILD'])

    def test_each_reviewed_body_instruction_mutation_refused(self):
        for profile, (config, images, dis) in self.profiles.items():
            if not audit.REVIEWED[profile]:
                continue
            retained = audit.compare_retained(self.paths[profile], config, images, dis)
            for image_name, names in audit.REVIEWED[profile].items():
                for name in names:
                    changed = copy.deepcopy(images)
                    base = {'resident-sci': audit.LOW, 'worker': audit.HIGH, 'stage': audit.STAGE}[image_name]
                    at = audit.physical(changed[image_name]['symbols'][name]) - base
                    payload = bytearray(changed[image_name]['payload']); payload[at] ^= 1
                    changed[image_name]['payload'] = bytes(payload)
                    with self.subTest(profile=profile, image=image_name, function=name), self.assertRaises(ValueError):
                        audit.reviewed_shapes(config, changed, dis, retained)

    def test_retained_low_reader_audio_and_installer_mutations_refused(self):
        profile = 1; config, images, dis = self.profiles[profile]
        names = {'resident-sci': ('_read_sectors', '_kui_toy_pilot_read_raw'),
                 'worker': ('_kui_toy_pilot_worker_initialize',),
                 'stage': ('_kui_toy_pilot_stage_install',)}
        for image_name, symbols in names.items():
            for name in symbols:
                self.assertIn(name, images[image_name]['symbols'])
                changed = copy.deepcopy(images)
                base = {'resident-sci': audit.LOW, 'worker': audit.HIGH, 'stage': audit.STAGE}[image_name]
                at = audit.physical(changed[image_name]['symbols'][name]) - base
                payload = bytearray(changed[image_name]['payload']); payload[at] ^= 1
                changed[image_name]['payload'] = bytes(payload)
                with self.subTest(image=image_name, function=name), self.assertRaises(ValueError):
                    audit.compare_retained(self.paths[profile], config, changed, dis)

    def test_exact_report_and_engine_object_size_mutations_refused(self):
        for profile, (_, images, _) in self.profiles.items():
            expected = audit.REVIEWED_OBJECTS[profile].get('worker')
            if not expected:
                continue
            for name in ('_e', '_schedule_report', '_schedule_visit'):
                changed = copy.deepcopy(images['worker']); changed['symbol_sizes'][name] += 4
                with self.subTest(profile=profile, object=name), self.assertRaisesRegex(ValueError, 'size/ownership'):
                    audit.schedule.audit_owned_objects(changed, expected, '__toy_pilot', audit.HIGH, audit.HIGH_LIMIT)

    def test_IRQ_guard_and_alias_mutations_refused(self):
        for profile, (_, images, _) in self.profiles.items():
            for symbol, delta in [('__toy_sci_irq_stack_top', 32), ('__toy_sci_irq_stack_bottom', -audit.P2)]:
                changed = copy.deepcopy(images['worker']); changed['symbols'][symbol] += delta
                with self.subTest(profile=profile, symbol=symbol), self.assertRaises(ValueError):
                    audit.schedule.audit_startup_irq(self.paths[profile], changed, 2)

    def test_TRACE_cannot_call_integer_division_or_storage(self):
        _, images, dis = self.profiles[1]
        code = Linked(images['worker'], audit.HIGH, dis['worker'])
        audit.audit_budget_trace_graph(code)
        _, _, rows = audit.schedule.bounded_function(code, '_kui_toy_loader_trace_end')
        pool = next(code.literal(at, opcode >> 8 & 15)[0] for at, opcode, mnemonic, _ in rows
                    if opcode & 0xF000 == 0xD000 and mnemonic == 'mov.l' and
                    code.literal(at, opcode >> 8 & 15)[1] == code.symbols['_add'])
        for target in ('___udivsi3', '_kui_toy_pilot_sci_service'):
            changed = copy.deepcopy(images['worker']); payload = bytearray(changed['payload'])
            struct.pack_into('<I', payload, pool - audit.HIGH, changed['symbols'][target])
            changed['payload'] = bytes(payload)
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, 'unknown/out-of-scope'):
                audit.audit_budget_trace_graph(Linked(changed, audit.HIGH, dis['worker']))


if __name__ == '__main__':
    unittest.main()
