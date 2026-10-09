#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual linked mutation fixtures for the separate DATA probe gate."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import toy_loader_data_probe_audit as audit
from toy_pilot_cache_audit import Linked

BUILD = ROOT / 'build/toy-data-probe'
BASELINE = ROOT / 'build/recovered-t-check'


def mutate_word(image, base, address, value, width=2):
    result = copy.deepcopy(image)
    payload = bytearray(result['payload'])
    at = audit.trace.physical(address) - base
    payload[at:at + width] = value.to_bytes(width, 'little')
    result['payload'] = bytes(payload)
    return result


class LinkedDataProbeAudit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (BUILD / 'worker.elf').is_file():
            raise unittest.SkipTest('Build DATA_PROBE=1 target before linked fixtures')
        cls.config, cls.images, cls.dis = audit.load_profile(BUILD)

    def code(self, image=None):
        return Linked(copy.deepcopy(image or self.images['worker']), audit.HIGH, self.dis['worker'])

    def change_instruction(self, code, at, opcode, mnemonic, operands):
        code.image = mutate_word(code.image, code.base, at, opcode)
        code.payload = code.image['payload']
        code.rows = [(address, opcode, mnemonic, operands) if address == at else row
                     for row in code.rows for address in (row[0],)]

    def literal_for(self, value, name):
        code = self.code()
        for at, opcode, mnemonic, _ in audit.trace.exact_function(code, name)[2]:
            if opcode & 0xF000 == 0xD000 and mnemonic == 'mov.l':
                literal, found = code.literal(at, opcode >> 8 & 15)
                if found == value:
                    return literal
        self.fail('Expected linked literal absent from ' + name)

    def test_complete_fresh_linked_profile(self):
        proof = audit.audit_data_probe(BUILD, BASELINE)
        self.assertEqual(proof['exports']['bytes'], 80)
        self.assertEqual(proof['exports']['boot_bytes'], 56)
        self.assertEqual(proof['retained_trace']['report_words'], 416)
        self.assertEqual(proof['DATA_probe']['report_words'], 192)
        self.assertEqual(len(proof['DATA_probe']['passive_callgraph']['MMIO_reads']), 5)
        self.assertTrue(proof['retained']['retained_audio_instructions_identical'])
        self.assertFalse(proof['hardware_behavior_verified'])

    def test_original_T_gate_still_refuses_probe(self):
        with self.assertRaises(ValueError):
            audit.trace.read_config(BUILD)

    def test_changed_profile_and_unreviewed_keys_rejected(self):
        for key, value in [('DATA_PROBE', '0'), ('LOADER_TRACE', '0'),
                           ('GD_FIXED_STEP', '3'), ('SHARED_SCI', '1')]:
            with self.subTest(key=key), tempfile.TemporaryDirectory(dir=ROOT / 'build') as area:
                config = dict(self.config)
                config[key] = value
                (Path(area) / 'build-config').write_text(''.join(f'{k}={v}\n' for k, v in config.items()))
                with self.assertRaises(ValueError):
                    audit.read_config(area)
        for extra in ('DATA_PROBE=1\n', 'UNREVIEWED_KEY=1\n'):
            with tempfile.TemporaryDirectory(dir=ROOT / 'build') as area:
                (Path(area) / 'build-config').write_text((BUILD / 'build-config').read_text() + extra)
                with self.assertRaises(ValueError):
                    audit.read_config(area)

    def test_allocated_low_object_size_change_rejected(self):
        # Bytes and addresses stay exact; an allocated low object cannot gain
        # space merely because non-allocated compiler metadata is excluded.
        images = copy.deepcopy(self.images)
        images['resident-sci']['symbol_sizes']['_card'] += 4
        with self.assertRaisesRegex(ValueError, 'changed retained low linked symbols or object sizes'):
            audit.audit_retained(BUILD, self.config, images, self.dis, BASELINE)

    def test_actual_nonallocated_LTO_metadata_rename_is_not_runtime_change(self):
        # The observed CI delta was zero-size _minic.c.<random> source anchors.
        # load_profile/inspect_elf has already excluded them from runtime symbols.
        images = copy.deepcopy(self.images)
        low = images['resident-sci']
        name = next(name for name, size in low['symbol_sizes'].items()
                    if name.startswith('_minic.c.') and not size and name not in low['symbols'])
        del low['symbol_sizes'][name]
        low['symbol_sizes'][name + '_fixture'] = 0
        proof = audit.audit_retained(BUILD, self.config, images, self.dis, BASELINE)
        self.assertTrue(proof['low_symbols_and_reservations_identical'])
        self.assertTrue(proof['low_binary_identical_except_build_string'])


    def reviewed_literal(self, name, offset):
        code = self.code()
        at = code.symbols[name] + offset
        opcode = code.half(at)
        return code.literal(at, opcode >> 8 & 15)[0:2]

    def test_reviewed_packet_and_display_bytes_cannot_change(self):
        # This covers the actual shared packet initializer, thirteen hardware
        # register offsets, hex digits, and every selected immutable string.
        for name, rules in audit.REVIEWED_RODATA_USES.items():
            for offset, (_, key) in rules.items():
                with self.subTest(name=name, offset=offset, object=key):
                    _, target = self.reviewed_literal(name, offset)
                    original = self.code().data(target, 1)[0]
                    changed = mutate_word(self.images['worker'], audit.HIGH,
                                          target, original ^ 1, 1)
                    with self.assertRaisesRegex(ValueError, 'Reviewed rodata bytes changed'):
                        audit.scoped_normalized_function(self.code(changed), name, self.config['BUILD'])

    def test_rodata_relocation_requires_actual_readonly_section_bounds(self):
        name, offset = '_fault', 0x48
        _, target = self.reviewed_literal(name, offset)
        image = copy.deepcopy(self.images['worker'])
        # Initialized bytes alone are insufficient: the actual ELF section
        # must contain the complete sixteen-byte packet.
        image['readonly_ranges'] = [(target, target + 15)]
        with self.assertRaisesRegex(ValueError, 'read-only ELF section'):
            audit.scoped_normalized_function(self.code(image), name, self.config['BUILD'])

    def test_reviewed_derived_stack_pointers_are_exact(self):
        for name, rules in audit.REVIEWED_STACK_USES.items():
            for offset in rules:
                with self.subTest(name=name, offset=offset):
                    literal, target = self.reviewed_literal(name, offset)
                    changed = mutate_word(self.images['worker'], audit.HIGH, literal, target + 4, 4)
                    with self.assertRaisesRegex(ValueError, 'derived stack literal changed'):
                        audit.scoped_normalized_function(self.code(changed), name, self.config['BUILD'])

    def test_unlisted_pointer_never_uses_scoped_relocation(self):
        # The shared packet exception cannot admit a shift inside worker
        # state: this independent literal retains T's exact object+offset.
        name = '_fault'
        code = self.code()
        literal, target = self.reviewed_literal(name, 0x0c)
        changed = mutate_word(self.images['worker'], audit.HIGH, literal, target + 4, 4)
        self.assertNotEqual(
            audit.scoped_normalized_function(code, name, self.config['BUILD']),
            audit.scoped_normalized_function(self.code(changed), name, self.config['BUILD']))

    def test_changed_opcode_cannot_hide_selected_literal_rule(self):
        name = '_fault'
        code = self.code()
        self.change_instruction(code, code.symbols[name] + 0x48, 0x0009, 'nop', '')
        with self.assertRaisesRegex(ValueError, 'Missing exact reviewed literal-use'):
            audit.scoped_normalized_function(code, name, self.config['BUILD'])

    def test_wrong_low_read_or_block_callback_rejected(self):
        low = self.images['resident-sci']
        for name, callback in [('_data_probe_read', '_read_sectors'),
                               ('_data_probe_payload', '_transfer_block')]:
            with self.subTest(callback=callback):
                at = self.literal_for(low['symbols'][callback], name)
                changed = mutate_word(self.images['worker'], audit.HIGH, at,
                                      low['symbols']['_kui_toy_pilot_read_raw'], 4)
                with self.assertRaisesRegex(ValueError, 'Changed reviewed'):
                    audit.trace.reviewed_identities(self.code(changed), audit.REVIEWED_DATA, 'DATA probe')

    def test_wrong_low_SR_or_card_address_rejected(self):
        low = self.images['resident-sci']
        for symbol in ('_kui_retail_hook_sr', '_card'):
            with self.subTest(symbol=symbol):
                at = self.literal_for(low['symbols'][symbol], '_kui_toy_loader_data_probe_begin')
                changed = mutate_word(self.images['worker'], audit.HIGH, at,
                                      low['symbols'][symbol] + 4, 4)
                with self.assertRaisesRegex(ValueError, 'Changed reviewed'):
                    audit.trace.reviewed_identities(self.code(changed), audit.REVIEWED_DATA, 'DATA probe')

    def test_actual_new_MMIO_store_rejected(self):
        code = self.code()
        at = next(at for at, opcode, _, _ in audit.trace.exact_function(code, '_data_probe_sample')[2]
                  if opcode == 0x6711)
        self.change_instruction(code, at, 0x2171, 'mov.w', 'r7,@r1')
        with self.assertRaisesRegex(ValueError, 'wrote outside'):
            audit.audit_probe_graph(code, self.images['resident-sci'])

    def test_unknown_timer_address_rejected(self):
        at = self.literal_for(0xFFC00000, '_data_probe_sample')
        changed = mutate_word(self.images['worker'], audit.HIGH, at, 0xFFC00004, 4)
        with self.assertRaisesRegex(ValueError, 'unreviewed MMIO'):
            audit.audit_probe_graph(self.code(changed), self.images['resident-sci'])

    def test_unexpected_SDK_callback_rejected_by_graph(self):
        at = self.literal_for(self.images['resident-sci']['symbols']['_transfer_block'], '_data_probe_payload')
        changed = mutate_word(self.images['worker'], audit.HIGH, at,
                              self.images['worker']['symbols']['_kui_toy_pilot_request'], 4)
        with self.assertRaisesRegex(ValueError, 'unknown/out-of-scope callback'):
            audit.audit_probe_graph(self.code(changed), self.images['resident-sci'])

    def test_P1_new_state_rejected(self):
        image = copy.deepcopy(self.images['worker'])
        image['symbols']['_data_probe_report'] -= audit.P2
        with self.assertRaises(ValueError):
            audit.trace.owned_bss(image, {'_data_probe_report': 768})

    def test_DATA_and_TRACE_state_overlap_rejected(self):
        image = copy.deepcopy(self.images['worker'])
        image['symbols']['_data_probe_report'] = image['symbols']['_report']
        with self.assertRaisesRegex(ValueError, 'overlap'):
            audit.audit_state(image)

    def test_changed_freeze_or_fpu_refuses_reviewed_identity(self):
        for opcode, mnemonic, operands in [(0x0009, 'nop', ''), (0xF000, 'fadd', 'fr0,fr0')]:
            code = self.code()
            at = code.symbols['_kui_toy_loader_data_probe_freeze']
            self.change_instruction(code, at, opcode, mnemonic, operands)
            with self.assertRaisesRegex(ValueError, 'Changed reviewed'):
                audit.trace.reviewed_identities(code, audit.REVIEWED_DATA, 'DATA probe')

    def test_probe_stack_evidence_cannot_understate_or_hide_frame(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as area:
            target = Path(area)
            for path in BUILD.rglob('*.su'):
                copy_path = target / path.relative_to(BUILD)
                copy_path.parent.mkdir(parents=True, exist_ok=True)
                copy_path.write_bytes(path.read_bytes())
            report = target / 'worker/src/loader/toy_loader_data_probe.su'
            original = report.read_text()
            actual = next(row['bytes'] for row in audit.stack_rows([report])
                          if row['function'] == 'data_probe_read')
            report.write_text(original.replace(f'data_probe_read\t{actual}\t', 'data_probe_read\t4\t'))
            with self.assertRaisesRegex(ValueError, 'actual linked frame'):
                audit.audit_probe_stacks(target, self.images['resident-sci'], self.images['worker'], self.dis['worker'], BASELINE)
            report.write_text(original + 'fixture:1:1:hidden_frame\t4096\tstatic\n')
            with self.assertRaises(ValueError):
                audit.audit_probe_stacks(target, self.images['resident-sci'], self.images['worker'], self.dis['worker'], BASELINE)


if __name__ == '__main__':
    unittest.main()
