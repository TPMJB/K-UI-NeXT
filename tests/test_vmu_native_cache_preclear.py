#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Require the real preclear companion before the typed cache-hit boundary."""
import copy
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from check_vmu_native_cache_gateway import CacheRoute, bind_preclear, preclear_window


class CachePreclear(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profiles = []
        for name in ('native-background-compact', 'native-background-compact-a1'):
            directory = ROOT / 'build' / name
            if (directory / 'native-preclear-proof.json').is_file():
                stage = read_elf(directory / 'stage.elf')
                cls.profiles.append((directory, stage, bind_preclear(stage, directory)))
        if not cls.profiles:
            raise unittest.SkipTest('Build a compact profile and its actual-original preclear companion')

    def copied(self, source, destination):
        for name in ('stage.elf', 'native-worker.elf', 'resident-sci.elf', 'resident-legacy.elf',
                     'native-preclear-proof.json'):
            shutil.copyfile(source / name, destination / name)

    def test_normal_return_and_both_terminal_outcomes(self):
        cases = 0
        for _, stage, binding in self.profiles:
            self.assertEqual(binding['window']['argument_count'], 0)
            for seed in (0, 1):
                for mask in (0, 15):
                    for mode in (0, 2, 4, 5, 6):
                        for normalized in (False, True):
                            for eager in (False, True):
                                for refusal in (0, 0x53, 0x54):
                                    cpu = CacheRoute(stage, seed, mask, True, mode=mode,
                                        normalized=normalized, eager=eager, preclear=binding,
                                        preclear_refusal=refusal).run()
                                    cpu.check()
                                    self.assertEqual(cpu.route, 'preclear-refused' if refusal else 'cached')
                                    self.assertEqual(cpu.calls.count('preclear'), 1)
                                    cases += 1
        self.assertEqual(cases, 240 * len(self.profiles))

    def test_cache_miss_and_eligibility_failures_never_reach_preclear(self):
        for _, stage, binding in self.profiles:
            for hit, invalid in ((False, 0), *((True, x) for x in range(1, 8))):
                cpu = CacheRoute(stage, 7, 15, hit, invalid, preclear=binding,
                                 preclear_refusal=0x53).run()
                cpu.check()
                self.assertNotIn('preclear', cpu.calls)
            cpu = CacheRoute(stage, 7, 15, True, mode=1).run()
            cpu.check()
            self.assertNotIn('preclear', cpu.calls)

    def test_directory_and_parsed_stage_must_agree(self):
        directory, stage, _ = self.profiles[0]
        with self.assertRaisesRegex(ValueError, 'companion directory'):
            bind_preclear(stage)
        with self.assertRaisesRegex(ValueError, 'verified companion'):
            CacheRoute(stage, 0, 0, True)
        forged = dict(stage[4])
        row = forged['_native_preclear_prepare']
        forged['_native_preclear_prepare'] = (row[0] + 2, *row[1:])
        with self.assertRaisesRegex(ValueError, 'parsed stage'):
            bind_preclear((*stage[:4], forged), directory)
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary)
            data = bytearray(stage[0])
            address, _, section = symbol_bytes(stage, '_kui_retail_stage_relay', 2)
            data[section[4] + address - section[3]] ^= 1
            (target / 'stage.elf').write_bytes(data)
            with self.assertRaisesRegex(ValueError, 'parsed stage'):
                bind_preclear(stage, target)

    def test_missing_stale_artifact_and_stale_source_companions_refuse(self):
        directory, stage, _ = self.profiles[0]
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary)
            self.copied(directory, target)
            path = target / 'native-preclear-proof.json'
            original = path.read_bytes()
            path.unlink()
            with self.assertRaisesRegex(ValueError, 'companion file'):
                bind_preclear(stage, target)
            for field in ('artifacts', 'proof_source_sha256'):
                value = json.loads(original)
                key = next(iter(value[field]))
                value[field][key] = '0' * 64
                path.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    bind_preclear(stage, target)

    def test_companion_cannot_change_during_validation(self):
        import check_native_preclear
        directory, stage, _ = self.profiles[0]
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary)
            self.copied(directory, target)
            validate = check_native_preclear.check
            def changed_after_real_validation(*args, **kwargs):
                result = validate(*args, **kwargs)
                path = target / 'native-preclear-proof.json'
                path.write_bytes(path.read_bytes() + b'\n')
                return result
            # The real validator still runs every acceptance gate. This unit
            # wrapper introduces only the intervening file replacement.
            with patch.object(check_native_preclear, 'check', changed_after_real_validation):
                with self.assertRaisesRegex(ValueError, 'changed while'):
                    bind_preclear(stage, target)

    def test_code_sha_type_and_digest_section_and_span_are_explicit(self):
        for _, stage, _ in self.profiles:
            for name, value in (
                    ('_code_sha', (stage[4]['_code_sha'][0], stage[4]['_code_sha'][1], 1,
                                   stage[4]['_code_sha'][3])),
                    ('_native_heap_code_sha', (*stage[4]['_native_heap_code_sha'][:3], len(stage[3]))),
                    ('_native_heap_code_sha', (0x8cdfffe0, *stage[4]['_native_heap_code_sha'][1:])),
                    ('_native_heap_code_sha', (stage[4]['__retail_stage_bss_end'][0] - 16,
                                               *stage[4]['_native_heap_code_sha'][1:]))):
                symbols = dict(stage[4]); symbols[name] = value
                with self.subTest(name=name, value=value):
                    with self.assertRaises(ValueError):
                        preclear_window((*stage[:4], symbols))

    def test_exact_window_and_all_three_typed_literals(self):
        for _, stage, binding in self.profiles:
            begin, original, _ = symbol_bytes(stage, '_kui_retail_stage_relay', 2)
            window = binding['window']
            offsets = [window['window_offset'] + 2 * x for x in range(7)]
            offsets += [address - begin for address in window['literal_addresses']]
            for offset in offsets:
                with self.subTest(profile=hex(begin), offset=offset):
                    changed = bytearray(original)
                    changed[offset] ^= 2
                    with self.assertRaises(ValueError):
                        CacheRoute(stage, 7, 15, True, preclear=binding, blob=bytes(changed))
            broken = copy.deepcopy(binding)
            broken['stage_sha256'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'binding differs'):
                CacheRoute(stage, 7, 15, True, preclear=broken)
            with self.assertRaisesRegex(ValueError, 'Unknown preclear'):
                CacheRoute(stage, 7, 15, True, preclear=binding, preclear_refusal=0x55)

    def test_actual_branch_cannot_skip_preclear_before_code_sha(self):
        for _, stage, binding in self.profiles:
            begin, original, _ = symbol_bytes(stage, '_kui_retail_stage_relay', 2)
            # Replace the cached-message JSR, whose delay is a legal NOP,
            # with a BRA directly to code_sha's argument/call sequence.
            at = binding['window']['window_offset'] - 4
            self.assertEqual(struct.unpack_from('<H', original, at)[0], 0x480b)
            self.assertEqual(struct.unpack_from('<H', original, at + 2)[0], 9)
            target = binding['window']['return_pr']
            displacement = (target - (begin + at + 4)) // 2
            self.assertTrue(-2048 <= displacement < 2048)
            changed = bytearray(original)
            struct.pack_into('<H', changed, at, 0xa000 | (displacement & 0xfff))
            cpu = CacheRoute(stage, 7, 15, True, preclear=binding, blob=bytes(changed))
            with self.assertRaisesRegex(AssertionError, 'Code SHA continuation'):
                cpu.run()


if __name__ == '__main__':
    unittest.main()
