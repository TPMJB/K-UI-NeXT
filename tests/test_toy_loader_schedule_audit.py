#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed fixtures for the separate startup scheduling admission gate."""
import copy
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import shutil

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import toy_loader_schedule_audit as audit
from toy_pilot_cache_audit import Linked


def fixture(value=0xAC006800, *, address=0x8C004000, owned=0xAC006800,
            literal_offset=12, string=b'FIXTURE\0', opcode=0xD002):
    payload = bytearray(96)
    opcode = (opcode & 0xFF00) | ((literal_offset - 4) // 4)
    struct.pack_into('<HHH', payload, 0, opcode, 0x000B, 0x0009)
    struct.pack_into('<I', payload, literal_offset, value)
    payload[48:48 + len(string)] = string
    image = {'payload': bytes(payload), 'symbols': {'_fn': address, '_owned': owned},
             'symbol_sizes': {'_fn': 6, '_owned': 16},
             'readonly_ranges': [(address + 48, address + 96)]}
    code = Linked(image, address)
    code.rows = [(address, opcode, 'mov.l', 'literal,r0'),
                 (address + 2, 0x000B, 'rts', ''), (address + 4, 0x0009, 'nop', '')]
    code.literals = {address + literal_offset}
    return code


class ConfigurationTests(unittest.TestCase):
    def config(self, mode=1):
        return {**audit.REQUIRED_CONFIG, 'BUILD': '012345abcdef', 'DATA_SCHEDULE_MODE': str(mode)}

    def write(self, config, extra=''):
        temporary = tempfile.TemporaryDirectory()
        path = Path(temporary.name)
        (path / 'build-config').write_text(''.join(f'{key}={value}\n' for key, value in config.items()) + extra)
        return temporary, path

    def test_exact_modes_and_frozen_audio(self):
        for mode in (1, 2, 3):
            with self.subTest(mode=mode):
                temporary, path = self.write(self.config(mode))
                with temporary:
                    self.assertEqual(audit.read_config(path, mode)['DATA_SCHEDULE_MODE'], str(mode))

    def test_changed_or_unknown_config_refused(self):
        mutations = [('DATA_SCHEDULE_MODE', '0'), ('DATA_SCHEDULE_MODE', '4'),
                     ('DATA_PAYLOAD_MODE', '1'), ('SHARED_SCI', '1'), ('ASYNC_CDDA', '1'),
                     ('GD_FIXED_STEP', '1'), ('PRIVATE_P2', '0'), ('NATIVE_CACHE', '1'),
                     ('SCI_REUSE_TDRE', '1'), ('BUILD', 'abcdef'), ('UNREVIEWED', '1')]
        for key, value in mutations:
            with self.subTest(key=key, value=value):
                config = self.config(); config[key] = value
                temporary, path = self.write(config)
                with temporary, self.assertRaises(ValueError):
                    audit.read_config(path)

    def test_duplicate_missing_and_wrong_mode_refused(self):
        config = self.config(); config.pop('DATA_PROBE')
        temporary, path = self.write(config)
        with temporary, self.assertRaises(ValueError):
            audit.read_config(path)
        temporary, path = self.write(self.config(), 'DATA_PROBE=1\n')
        with temporary, self.assertRaisesRegex(ValueError, 'Duplicate'):
            audit.read_config(path)
        temporary, path = self.write(self.config())
        with temporary, self.assertRaisesRegex(ValueError, 'differs'):
            audit.read_config(path, 2)


class InstructionIdentityTests(unittest.TestCase):
    def identity(self, code, build='012345abcdef'):
        return audit.function_identity(code, '_fn', build)

    def test_owned_literal_and_pc_pool_relocation_preserve_identity(self):
        first = fixture()
        second = fixture(0xAC006840, address=0x8C004080, owned=0xAC006840, literal_offset=16)
        self.assertEqual(self.identity(first), self.identity(second))

    def test_object_interior_offset_is_part_of_identity(self):
        self.assertNotEqual(self.identity(fixture(0xAC006801)), self.identity(fixture(0xAC006802)))

    def test_scalar_mmio_literal_change_refused(self):
        self.assertNotEqual(self.identity(fixture(0xFFD8000C)), self.identity(fixture(0xFFD80008)))

    def test_opcode_register_change_is_part_of_identity(self):
        first = fixture()
        second = fixture(opcode=0xD102)
        self.assertNotEqual(self.identity(first), self.identity(second))

    def test_actual_bytes_cannot_hide_behind_stale_disassembly(self):
        code = fixture()
        payload = bytearray(code.payload); payload[2] ^= 1
        code.payload = code.image['payload'] = bytes(payload)
        with self.assertRaisesRegex(ValueError, 'actual linked bytes|actual linked instruction'):
            self.identity(code)

    def test_anonymous_readonly_string_content_is_part_of_identity(self):
        first = fixture(0x8C004030, string=b'FIRST\0')
        moved = fixture(0x8C0040B0, address=0x8C004080, string=b'FIRST\0')
        changed = fixture(0x8C004030, string=b'OTHER\0')
        self.assertEqual(self.identity(first), self.identity(moved))
        self.assertNotEqual(self.identity(first), self.identity(changed))

    def test_build_string_is_only_exact_reviewed_build(self):
        first = fixture(0x8C004030, string=b'BUILD 012345abcdef\0')
        second = fixture(0x8C004030, string=b'BUILD abcdef012345\0')
        self.assertEqual(self.identity(first), self.identity(second, 'abcdef012345'))
        self.assertNotEqual(self.identity(first), self.identity(second))

    def test_anonymous_binary_rodata_does_not_gain_permissive_alias(self):
        first = fixture(0x8C004030, string=b'\0\xff\xff\0')
        moved = fixture(0x8C0040B0, address=0x8C004080, string=b'\0\xff\xff\0')
        self.assertNotEqual(self.identity(first), self.identity(moved))


class OwnershipAndReviewTests(unittest.TestCase):
    def image(self):
        return {'symbols': {'__toy_pilot_bss_begin': 0xACFD4000, '__toy_pilot_bss_end': 0xACFD5000,
                            '_report': 0xACFD4040, '_state': 0xACFD40C0},
                'symbol_sizes': {'_report': 128, '_state': 64}}

    def audit(self, image, expected=None):
        return audit.audit_owned_objects(image, expected or {'_report': 128, '_state': 64},
                                         '__toy_pilot', audit.HIGH, audit.HIGH_LIMIT)

    def test_exact_owned_object_sizes_and_overlap(self):
        self.assertEqual(self.audit(self.image())['_state']['bytes'], 64)
        for field, name, value in [('symbols', '_state', 0x8CFD40C0),
                                    ('symbols', '_state', 0xACFD5000),
                                    ('symbols', '_state', 0xACFD4080),
                                    ('symbol_sizes', '_state', 60)]:
            changed = self.image(); changed[field][name] = value
            with self.subTest(field=field, name=name, value=value), self.assertRaises(ValueError):
                self.audit(changed)

    def test_empty_review_tables_always_refuse_hardware_admission(self):
        for mode in (1, 2, 3):
            with self.subTest(mode=mode), patch.dict(audit.REVIEWED,{mode:{}}), self.assertRaisesRegex(ValueError, 'Missing independently reviewed'):
                audit.reviewed_shapes({'DATA_SCHEDULE_MODE': str(mode)}, {}, {}, {})

    def test_empty_missing_and_malformed_stack_reports_refused(self):
        with tempfile.TemporaryDirectory() as area:
            directory = Path(area)
            with self.assertRaises(ValueError):
                audit.stack_inventory(directory)
            target = directory / 'worker/src/loader/probe.su'; target.parent.mkdir(parents=True)
            for content in ('', 'file:1:1:helper\t16\tdynamic\n', 'file:1:1:helper\tx\tstatic\n'):
                target.write_text(content)
                with self.subTest(content=content), self.assertRaises(ValueError):
                    audit.stack_inventory(directory)
            target.write_text('file:1:1:helper\t16\tstatic\n')
            self.assertEqual(audit.stack_inventory(directory)['worker/src/loader/probe.su'], [['helper', 16]])

    def test_chronology_schema_size_and_record_count_refused(self):
        self.assertEqual(audit.audit_schedule_schema()['words'], 176)
        original = (ROOT / 'include/kui/toy_loader_schedule_report.h').read_text()
        for before, after in (('WORDS 176u', 'WORDS 192u'), ('RECORDS 8u', 'RECORDS 9u')):
            with self.subTest(before=before), tempfile.TemporaryDirectory() as area:
                target = Path(area) / 'include/kui/toy_loader_schedule_report.h'
                target.parent.mkdir(parents=True)
                target.write_text(original.replace(before, after))
                with self.assertRaisesRegex(ValueError, 'schema constant'):
                    audit.audit_schedule_schema(area)


class TargetLinkedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('sh-elf-objdump'):
            raise unittest.SkipTest('Target objdump required for actual emitted mutation fixtures')
        cls.paths={1:ROOT/'build/schedule-y',2:ROOT/'build/schedule-z',3:ROOT/'build/schedule-b'}
        if not all((p/'worker.elf').is_file() for p in cls.paths.values()):
            raise unittest.SkipTest('Build all three final scheduling profiles before linked fixtures')
        cls.profiles={mode:audit.load_profile(p,mode) for mode,p in cls.paths.items()}

    def test_all_final_hardware_candidates_pass_with_immutable_runtime(self):
        for mode,path in self.paths.items():
            with self.subTest(mode=mode):
                proof=audit.audit_schedule(path,mode=mode)
                self.assertTrue(proof['admitted'])
                self.assertEqual(proof['runtime']['sha256'],audit.REVIEWED_RUNTIME_SHA256[mode])
                self.assertFalse(proof['hardware_behavior_verified'])
                self.assertLessEqual(proof['stacks']['conservative_sums']['high_GD']['bytes'],4000)

    def test_changed_runtime_is_not_admitted(self):
        for mode in self.paths:
            with self.subTest(mode=mode), patch.dict(audit.REVIEWED_RUNTIME_SHA256,{mode:'0'*64}), self.assertRaisesRegex(ValueError,'immutable artifact'):
                audit.audit_schedule(self.paths[mode],mode=mode)

    def test_each_new_or_changed_instruction_refuses_reviewed_identity(self):
        for mode,(config,images,dis) in self.profiles.items():
            retained=audit.compare_retained(self.paths[mode],config,images,dis,audit.DEFAULT_BASELINE)
            for image_name,names in audit.REVIEWED[mode].items():
                for name in names:
                    changed=copy.deepcopy(images)
                    base={'resident-sci':audit.LOW,'worker':audit.HIGH,'stage':audit.STAGE}[image_name]
                    offset=audit.physical(changed[image_name]['symbols'][name])-base
                    payload=bytearray(changed[image_name]['payload']);payload[offset]^=1
                    changed[image_name]['payload']=bytes(payload)
                    with self.subTest(mode=mode,image=image_name,function=name), self.assertRaises(ValueError):
                        audit.reviewed_shapes(config,changed,dis,retained)

    def test_actual_GD_bridge_guard_and_register_mutations_refused(self):
        for mode,(_,images,dis) in self.profiles.items():
            for name,offset in [('_kui_toy_pilot_gd_bridge',0),('_kui_toy_loader_trace_terminal_bridge',6)]:
                changed=copy.deepcopy(images['worker']);at=changed['symbols'][name]-audit.HIGH+offset
                payload=bytearray(changed['payload']);payload[at]^=1;changed['payload']=bytes(payload)
                with self.subTest(mode=mode,name=name), self.assertRaises(ValueError):
                    audit.audit_schedule_bridges(changed,dis['worker'],mode)

    def test_IRQ_stack_alias_and_runtime_guard_cannot_change(self):
        _,images,_=self.profiles[2]
        for symbol,delta in [('__toy_sci_irq_stack_top',32),('__toy_sci_irq_stack_bottom',-0x20000000)]:
            changed=copy.deepcopy(images['worker']);changed['symbols'][symbol]+=delta
            with self.subTest(symbol=symbol), self.assertRaises(ValueError):
                audit.audit_startup_irq(self.paths[2],changed,2)

    def test_typed_low_callback_tuple_object_size_change_refused(self):
        for mode,(config,images,dis) in self.profiles.items():
            changed=copy.deepcopy(images);changed['resident-sci']['symbol_sizes']['_image']+=4
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError,'private object size changed'):
                audit.compare_retained(self.paths[mode],config,changed,dis,audit.DEFAULT_BASELINE)

    def test_understated_or_missing_stack_inventory_refused(self):
        for mode,path in self.paths.items():
            inventory=audit.stack_inventory(path)
            signatures=audit.stack_signatures(inventory)
            self.assertEqual(signatures,audit.REVIEWED_STACK_ROWS[mode])
            key=next(iter(inventory));changed=copy.deepcopy(inventory);changed[key][0][1]+=4
            self.assertNotEqual(audit.stack_signatures(changed),audit.REVIEWED_STACK_ROWS[mode])
            del changed[key]
            self.assertNotEqual(audit.stack_signatures(changed),audit.REVIEWED_STACK_ROWS[mode])

    def test_legacy_gate_refuses_scheduling_configuration(self):
        for mode,path in self.paths.items():
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                audit.previous.read_config(path)


if __name__ == '__main__':
    unittest.main()
