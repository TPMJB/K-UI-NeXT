#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Negative linked fixtures for the separate Toy loader trace admission gate.

Run after building the exact trace target.  No fixtures authorize an altered
baseline profile or replace linked evidence with a source-text assertion.
"""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import toy_loader_trace_audit as audit
from package_cdda_toy_pilot import inspect_worker
from toy_pilot_cache_audit import Linked

BUILD = ROOT / 'build/toy-loader-trace'


def mutate_word(image, base, address, value, width=2):
    result = copy.deepcopy(image)
    payload = bytearray(result['payload'])
    at = audit.physical(address) - base
    payload[at:at + width] = value.to_bytes(width, 'little')
    result['payload'] = bytes(payload)
    return result


class LinkedTraceAudit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (BUILD / 'worker.elf').is_file():
            raise unittest.SkipTest('Build exact LOADER_TRACE=1 target before linked negative fixtures')
        cls.config, cls.images, cls.dis = audit.load_profile(BUILD)

    def code(self, name='worker'):
        return Linked(copy.deepcopy(self.images[name]),
                      {'worker': audit.HIGH, 'stage': audit.STAGE,
                       'resident-sci': audit.LOW}[name], self.dis[name])

    def change_instruction(self, code, address, opcode, mnemonic, operands):
        code.image = mutate_word(code.image, code.base, address, opcode)
        code.payload = code.image['payload']
        code.rows = [(at, opcode, mnemonic, operands) if at == address else row
                     for row in code.rows for at in (row[0],)]

    def test_complete_fresh_linked_profile(self):
        proof = audit.audit_loader_trace(BUILD)
        self.assertEqual(proof['exports']['bytes'], 80)
        self.assertEqual(proof['exports']['boot_bytes'], 56)
        self.assertEqual(proof['trace']['report_words'], 416)
        self.assertEqual(len(proof['trace']['passive_callgraph']['MMIO_reads']), 9)
        self.assertFalse(proof['hardware_behavior_verified'])
        self.assertTrue(proof['stacks']['audio_sum_includes_whole_low_raw_callback_and_callees'])

    def test_trace_is_rejected_by_original_76_byte_baseline_gate(self):
        with self.assertRaises(ValueError):
            inspect_worker((BUILD / 'worker.elf').read_bytes())

    def test_profile_changes_and_duplicate_flags_rejected(self):
        for key, value in [('LOADER_TRACE', '0'), ('NATIVE_CACHE', '1'),
                           ('GD_FIXED_STEP', '3'), ('SYNTHETIC_SOURCE', '1')]:
            with self.subTest(key=key), tempfile.TemporaryDirectory(dir=ROOT / 'build') as area:
                cfg = dict(self.config)
                cfg[key] = value
                (Path(area) / 'build-config').write_text(''.join(f'{k}={v}\n' for k, v in cfg.items()))
                with self.assertRaises(ValueError):
                    audit.read_config(area)
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as area:
            text = (BUILD / 'build-config').read_text() + 'LOADER_TRACE=1\n'
            (Path(area) / 'build-config').write_text(text)
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                audit.read_config(area)
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as area:
            text = (BUILD / 'build-config').read_text() + 'UNREVIEWED_SWITCH=1\n'
            (Path(area) / 'build-config').write_text(text)
            with self.assertRaisesRegex(ValueError, 'unreviewed key'):
                audit.read_config(area)

    def test_export_callback_and_abi_mutations_rejected(self):
        for index, value in [(2, 76), (17, self.images['worker']['symbols']['_kui_toy_pilot_gd_dispatch']),
                             (19, audit.LOW + 0x100)]:
            with self.subTest(index=index):
                changed = mutate_word(self.images['worker'], audit.HIGH,
                                      audit.HIGH + index * 4, value, 4)
                with self.assertRaises(ValueError):
                    audit.audit_exports(self.images['resident-sci'], changed)

    def test_p1_trace_state_and_gd_stack_bounds_rejected(self):
        worker = copy.deepcopy(self.images['worker'])
        worker['symbols']['_report'] -= audit.P2
        with self.assertRaises(ValueError):
            audit.owned_bss(worker, {'_report': 1664})
        for name, delta in [('__toy_pilot_gd_stack_top', 32),
                            ('__toy_pilot_gd_stack_bottom', -audit.P2)]:
            worker = copy.deepcopy(self.images['worker'])
            worker['symbols'][name] += delta
            with self.assertRaises(ValueError):
                audit.audit_exports(self.images['resident-sci'], worker)

    def test_bridge_fifth_argument_and_terminal_mask_rejected(self):
        worker = self.images['worker']
        gd = worker['symbols']['_kui_toy_pilot_gd_bridge']
        terminal = worker['symbols']['_kui_toy_loader_trace_terminal_bridge']
        code = self.code()
        mask_address = code.literal(terminal + 2, 2)[0]
        for at, value, width in [(gd, 0x0009, 2), (gd + 22, 0x0009, 2),
                                 (mask_address, 0xF0, 4), (terminal + 46, 0x0009, 2)]:
            with self.subTest(at=hex(at)):
                changed = mutate_word(worker, audit.HIGH, at, value, width)
                with self.assertRaises(ValueError):
                    audit.audit_bridges(changed, self.dis['worker'])

    def test_gd_stack_guard_fill_removed_rejected(self):
        code = self.code()
        start = code.find('_kui_toy_pilot_worker_initialize',
                          (None, 0x1126, None, None, 0x3236, 0x3128, 0x7103,
                           0x4109, 0x7101, 0x8B00, 0xE101, 0x4110, 0x8B33))
        store = start + 28 + 0x33 * 2
        changed = mutate_word(self.images['worker'], audit.HIGH, store, 0x0009)
        with self.assertRaises(ValueError):
            audit.audit_bridges(changed, self.dis['worker'])

    def test_wrong_low_publication_span_and_terminal_args_rejected(self):
        stage = self.code('stage')
        at = stage.find('_kui_toy_pilot_stage_install',
                        (0x518D, None, None, 0x141A, 0x5184, 0x1418, 0x5188, 0x1419))
        endpoint = stage.literal(at + 4, 5)[0]
        boot = self.images['resident-sci']['symbols']['_kui_toy_pilot_boot_control']
        changed = mutate_word(self.images['stage'], audit.STAGE, endpoint, boot + 52, 4)
        with self.assertRaises(ValueError):
            audit.audit_boot_publication(self.images['resident-sci'], self.images['worker'],
                                        changed, self.dis['resident-sci'], self.dis['stage'])
        low = self.code('resident-sci')
        entry = low.find('_kui_retail_menu_return', (0x511D, None, None, None, 0x410B, 0xE700))
        changed = mutate_word(self.images['resident-sci'], audit.LOW, entry + 10, 0xE704)
        with self.assertRaises(ValueError):
            audit.audit_boot_publication(changed, self.images['worker'], self.images['stage'],
                                        self.dis['resident-sci'], self.dis['stage'])

    def test_actual_MMIO_store_rejected_by_controlflow_walk(self):
        code = self.code()
        _, _, rows = audit.exact_function(code, '_clock_sample')
        at = next(row[0] for row in rows if row[1] == 0x6312)
        # Replace the actual TCOR0 read with a store using the same literal.
        self.change_instruction(code, at, 0x2132, 'mov.l', 'r3,@r1')
        with self.assertRaisesRegex(ValueError, 'wrote outside'):
            audit.audit_trace_graph(code)

    def test_unknown_timer_address_rejected(self):
        code = self.code()
        begin = code.symbols['_clock_sample']
        at = code.literal(begin + 2, 1)[0]
        changed = mutate_word(code.image, audit.HIGH, at, 0xFFC00004, 4)
        with self.assertRaisesRegex(ValueError, 'unreviewed MMIO'):
            audit.audit_trace_graph(Linked(changed, audit.HIGH, self.dis['worker']))

    def test_unknown_callback_rejected(self):
        code = self.code()
        call = next(row[0] for row in audit.exact_function(code, '_clock_sample')[2] if row[2] == 'jsr')
        at = code.literal(call - 4, 1)[0]
        changed = mutate_word(code.image, audit.HIGH, at, code.symbols['_kui_toy_pilot_request'], 4)
        with self.assertRaisesRegex(ValueError, 'unknown/out-of-scope callback'):
            audit.audit_trace_graph(Linked(changed, audit.HIGH, self.dis['worker']))

    def test_changed_store_and_fpu_fail_reviewed_identity(self):
        for opcode, mnemonic, operands in [(0x2142, 'mov.l', 'r4,@r1'),
                                           (0xF000, 'fadd', 'fr0,fr0')]:
            code = self.code()
            at = code.symbols['_kui_toy_loader_trace_word_count']
            self.change_instruction(code, at, opcode, mnemonic, operands)
            with self.assertRaisesRegex(ValueError, 'Changed reviewed'):
                audit.reviewed_identities(code, audit.REVIEWED_TRACE, 'trace')

    def test_trace_stack_evidence_cannot_understate_actual_frame(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as area:
            target = Path(area)
            for path in BUILD.rglob('*.su'):
                if path.is_relative_to(BUILD / 'sci/lto') or path.is_relative_to(BUILD / 'worker') or path.is_relative_to(BUILD / 'stage'):
                    copy_path = target / path.relative_to(BUILD)
                    copy_path.parent.mkdir(parents=True, exist_ok=True)
                    copy_path.write_bytes(path.read_bytes())
            report = target / 'worker/src/loader/toy_loader_trace.su'
            original = report.read_text()
            report.write_text(original.replace('kui_toy_loader_trace_end\t104\t',
                                               'kui_toy_loader_trace_end\t4\t'))
            with self.assertRaisesRegex(ValueError, 'actual linked frame'):
                audit.audit_stacks(target, self.images['resident-sci'], self.images['worker'], self.dis['worker'])
            report.write_text(original + 'fixture:1:1:overflow\t8192\tstatic\n')
            with self.assertRaises(ValueError):
                audit.audit_stacks(target, self.images['resident-sci'], self.images['worker'], self.dis['worker'])


if __name__ == '__main__':
    unittest.main()
