#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutation fixtures for the separate actual-linked V/W/X admission gate."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import toy_loader_sci_next_audit as audit
from toy_pilot_cache_audit import Linked

BASELINE = ROOT / 'build/toy-u-reference'
BUILDS = {i: ROOT / ('build/sci-next-' + label) for i, label in enumerate('vwx')}


def mutate_word(image, base, address, value, width=2):
    changed = copy.deepcopy(image)
    payload = bytearray(changed['payload'])
    offset = audit.trace.physical(address) - base
    payload[offset:offset + width] = value.to_bytes(width, 'little')
    changed['payload'] = bytes(payload)
    return changed


class SciNextSchemaAudit(unittest.TestCase):
    def test_exact_report_schema(self):
        self.assertEqual(audit.audit_schema()['words'], 192)

    def test_report_version_size_and_field_order_refused(self):
        header = ROOT / 'include/kui/toy_loader_data_probe.h'
        original = header.read_text()
        mutations = [
            ('VERSION 2u', 'VERSION 1u'),
            ('WORDS 192u', 'WORDS 208u'),
            ('dma_delta_invalid,dma_counter_wraps', 'dma_counter_wraps,dma_delta_invalid'),
            ('dma_started,dma_payload_ok', 'dma_payload_ok,dma_started'),
            ('feature_flags,payload_mode', 'payload_mode,feature_flags'),
            ('dma_payload_body,pio_payload_body', 'pio_payload_body,dma_payload_body'),
            ('UINT32_C(0x4c445031)', 'UINT32_C(0x4c445032)'),
            ('DMA_ATTRIBUTION UINT32_C(1)', 'DMA_ATTRIBUTION UINT32_C(2)'),
        ]
        for before, after in mutations:
            with self.subTest(before=before), tempfile.TemporaryDirectory() as area:
                target = Path(area) / 'include/kui/toy_loader_data_probe.h'
                target.parent.mkdir(parents=True)
                self.assertIn(before, original)
                target.write_text(original.replace(before, after))
                with self.assertRaises(ValueError):
                    audit.audit_schema(area)

    def test_frozen_low_and_audio_sources(self):
        self.assertTrue(audit.audit_frozen_sources()['all_frozen_sources_identical_to_U'])
        with tempfile.TemporaryDirectory() as area:
            for name in audit.FROZEN_SOURCES:
                target = Path(area) / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / name).read_bytes())
            for name in ('src/loader/sci_sd_bus.c', 'src/loader/toy_pilot_worker.c'):
                with self.subTest(name=name):
                    target = Path(area) / name
                    original = target.read_bytes()
                    target.write_bytes(original + b'\n/* unreviewed source */\n')
                    with self.assertRaisesRegex(ValueError, 'frozen low/CDDA source'):
                        audit.audit_frozen_sources(area)
                    target.write_bytes(original)


class SciNextLinkedAudit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not all((path / 'worker.elf').is_file() for path in BUILDS.values()):
            raise unittest.SkipTest('Build all three SCI-next target profiles before linked fixtures')
        cls.profiles = {mode: audit.load_profile(path, mode) for mode, path in BUILDS.items()}

    def code(self, mode, image=None):
        _, images, dis = self.profiles[mode]
        return Linked(copy.deepcopy(image or images['worker']), audit.HIGH, dis['worker'])

    def test_complete_actual_profiles(self):
        for mode, build in BUILDS.items():
            with self.subTest(mode=mode):
                proof = audit.audit_sci_next(build, BASELINE, mode)
                self.assertEqual(proof['profile'], 'toy-loader-sci-next-R')
                self.assertEqual(proof['DATA_probe']['version'], 2)
                self.assertEqual(proof['DATA_probe']['report_words'], 192)
                self.assertEqual(proof['retained']['baseline_build'], audit.U_BUILD)
                self.assertTrue(proof['retained']['retained_audio_instructions_identical'])
                self.assertTrue(proof['retained']['low_binary_identical_except_build_string'])
                self.assertFalse(proof['hardware_behavior_verified'])

    def test_old_U_gate_refuses_new_modes(self):
        for mode, path in BUILDS.items():
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                audit.u.read_config(path)

    def test_candidate_review_never_grants_admission(self):
        evidence = audit.review_candidate(BUILDS[1], BASELINE, 1)
        self.assertFalse(evidence['admitted'])
        self.assertTrue(evidence['independent_review_required'])

    def test_unreviewed_configuration_and_wrong_mode_refused(self):
        config = self.profiles[0][0]
        for key, value in [('DATA_PROBE', '0'), ('SCI_REUSE_TDRE', '1'),
                           ('GD_FIXED_STEP', '3'), ('DATA_PAYLOAD_MODE', '3')]:
            with self.subTest(key=key), tempfile.TemporaryDirectory() as area:
                candidate = dict(config)
                candidate[key] = value
                (Path(area) / 'build-config').write_text(''.join(f'{k}={v}\n' for k, v in candidate.items()))
                with self.assertRaises(ValueError):
                    audit.read_config(area)
        for mode in (1, 2):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                audit.read_config(BUILDS[mode], 0)
        for extra in ('DATA_PROBE=1\n', 'UNREVIEWED=1\n'):
            with tempfile.TemporaryDirectory() as area:
                (Path(area) / 'build-config').write_text((BUILDS[0] / 'build-config').read_text() + extra)
                with self.assertRaises(ValueError):
                    audit.read_config(area)

    def test_each_reviewed_function_actual_instruction_mutation_refused(self):
        for mode in BUILDS:
            for name in audit.REVIEWED[mode]:
                with self.subTest(mode=mode, function=name):
                    code = self.code(mode)
                    at = code.symbols[name]
                    opcode = code.half(at) ^ 1
                    code.image = mutate_word(code.image, audit.HIGH, at, opcode)
                    code.payload = code.image['payload']
                    code.rows = [(address, opcode, mnemonic, operands) if address == at else row
                                 for row in code.rows for address, _, mnemonic, operands in (row,)]
                    with self.assertRaisesRegex(ValueError, 'Changed reviewed|Reviewed literal'):
                        audit.trace.reviewed_identities(code, audit.REVIEWED[mode], 'SCI-next')

    def test_low_diagnostic_image_and_callback_objects_typed(self):
        for name, size in (('_diagnostic', 60), ('_image', 512), ('_card', 76)):
            with self.subTest(name=name):
                _, images, _ = self.profiles[1]
                low = copy.deepcopy(images['resident-sci'])
                low['symbol_sizes'][name] = size
                with self.assertRaisesRegex(ValueError, 'typed low P2 binding'):
                    audit.audit_bindings(BUILDS[1], low, self.code(1), 1)

    def test_changed_generated_field_address_refused(self):
        for mode in BUILDS:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as area:
                path = Path(area)
                (path / 'resident-sci.elf').write_bytes((BUILDS[mode] / 'resident-sci.elf').read_bytes())
                original = (BUILDS[mode] / 'toy_pilot_resident_symbols.h').read_text()
                import re
                bad = re.sub(r'(LOW_PROBE_DIAGNOSTIC UINT32_C\(0x)[0-9a-f]{8}', r'\g<1>ac006000', original)
                self.assertNotEqual(bad, original)
                (path / 'toy_pilot_resident_symbols.h').write_text(bad)
                with self.assertRaisesRegex(ValueError, 'generated header'):
                    audit.audit_bindings(path, self.profiles[mode][1]['resident-sci'], self.code(mode), mode)

    def test_low_object_size_change_refused_even_if_bytes_unchanged(self):
        config, images, dis = self.profiles[0]
        changed = copy.deepcopy(images)
        changed['resident-sci']['symbol_sizes']['_diagnostic'] += 4
        with self.assertRaisesRegex(ValueError, 'retained low symbols/object sizes'):
            audit.audit_retained(BUILDS[0], config, changed, dis, BASELINE)

    def test_reference_runtime_bytes_cannot_be_substituted(self):
        config, images, _ = audit.load_profile(BASELINE, reference=True)
        with tempfile.TemporaryDirectory() as area:
            target = Path(area)
            runtime = bytearray((BASELINE / 'retail-toy-pilot.kui').read_bytes())
            runtime[-1] ^= 1
            (target / 'retail-toy-pilot.kui').write_bytes(runtime)
            with self.assertRaisesRegex(ValueError, 'exact published U runtime'):
                audit.bind_runtime(target, config, images, audit.U_RUNTIME_SHA)

    def test_probe_graph_refuses_new_timer_write(self):
        for mode in BUILDS:
            with self.subTest(mode=mode):
                code = self.code(mode)
                rows = audit.exact_function(code, '_data_probe_sample')[2]
                row = next(row for row in rows if row[1] & 0xF00F == 0x6001)
                at, old, _, _ = row
                source, destination = old >> 4 & 15, old >> 8 & 15
                opcode = 0x2001 | source << 8 | destination << 4
                code.image = mutate_word(code.image, audit.HIGH, at, opcode)
                code.payload = code.image['payload']
                code.rows = [(address, opcode, 'mov.w', f'r{destination},@r{source}') if address == at else original
                             for original in code.rows for address in (original[0],)]
                with self.assertRaisesRegex(ValueError, 'outside owned/guest main RAM'):
                    audit.audit_probe_graph(code, self.profiles[mode][1]['resident-sci'], mode)

    def test_private_pio_scratch_alignment_and_size_refused(self):
        worker = self.profiles[2][1]['worker']
        for field, value in (('symbols', worker['symbols']['_payload_pio_scratch'] + 1),
                             ('symbol_sizes', 512)):
            with self.subTest(field=field):
                bad = copy.deepcopy(worker)
                bad[field]['_payload_pio_scratch'] = value
                with self.assertRaises(ValueError):
                    audit.audit_state(bad, 2)

    def test_understated_or_extra_helper_stack_evidence_refused(self):
        for mode in (1, 2):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as area:
                target = Path(area)
                for path in (BUILDS[mode] / 'worker').rglob('*.su'):
                    copy_path = target / path.relative_to(BUILDS[mode])
                    copy_path.parent.mkdir(parents=True, exist_ok=True)
                    copy_path.write_bytes(path.read_bytes())
                report = target / 'worker/src/loader/toy_loader_payload_control.su'
                original = report.read_text()
                rows = audit.static_rows(report)
                actual = next(row['bytes'] for row in rows if row['function'] == 'kui_toy_loader_payload_call')
                report.write_text(original.replace(f'kui_toy_loader_payload_call\t{actual}\t', 'kui_toy_loader_payload_call\t4\t'))
                with self.assertRaisesRegex(ValueError, 'actual linked frame'):
                    audit.audit_frames(target, self.profiles[mode][1]['worker'], self.profiles[mode][2]['worker'], mode)
                report.write_text(original + 'fixture:1:1:hidden_helper\t4096\tstatic\n')
                with self.assertRaises(ValueError):
                    audit.audit_frames(target, self.profiles[mode][1]['worker'], self.profiles[mode][2]['worker'], mode)


    def copy_stack_reports(self, mode, target):
        for group in ('worker', 'stage', 'sci/lto'):
            for path in (BUILDS[mode] / group).rglob('*.su'):
                copy_path = target / path.relative_to(BUILDS[mode])
                copy_path.parent.mkdir(parents=True, exist_ok=True)
                copy_path.write_bytes(path.read_bytes())

    def test_retained_stack_reports_missing_empty_or_understated_refused(self):
        names = ('worker/src/loader/toy_pilot_worker.su',
                 'worker/src/loader/toy_loader_trace.su',
                 'stage/src/loader/sci_sd_bus.su',
                 'sci/lto/resident-sci.elf.ltrans0.ltrans.su')
        for mode in BUILDS:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as area:
                target = Path(area)
                self.copy_stack_reports(mode, target)
                self.assertTrue(audit.audit_stack_completeness(target, BASELINE)
                                ['all_retained_reports_present_and_nonempty'])
                for name in names:
                    report = target / name
                    original = report.read_bytes()
                    with self.subTest(report=name, fault='missing'):
                        report.unlink()
                        with self.assertRaisesRegex(ValueError, 'stack report set'):
                            audit.audit_stack_completeness(target, BASELINE)
                    with self.subTest(report=name, fault='empty'):
                        report.write_bytes(b'')
                        with self.assertRaisesRegex(ValueError, 'Missing or empty'):
                            audit.audit_stack_completeness(target, BASELINE)
                    with self.subTest(report=name, fault='understated'):
                        rows = original.decode().splitlines()
                        fields = rows[0].split('\t')
                        fields[1] = str(max(0, int(fields[1]) - 4)) if int(fields[1]) else '4'
                        rows[0] = '\t'.join(fields)
                        report.write_text('\n'.join(rows) + '\n')
                        with self.assertRaisesRegex(ValueError, 'Changed retained'):
                            audit.audit_stack_completeness(target, BASELINE)
                    report.write_bytes(original)
                extra = target / 'worker/src/loader/hidden_frame.su'
                extra.write_text('fixture:1:1:hidden_frame\t0\tstatic\n')
                with self.assertRaisesRegex(ValueError, 'stack report set'):
                    audit.audit_stack_completeness(target, BASELINE)

    def test_V_absent_zero_frame_helpers_only(self):
        with tempfile.TemporaryDirectory() as area:
            target = Path(area)
            self.copy_stack_reports(0, target)
            proof = audit.audit_frames(target, self.profiles[0][1]['worker'],
                                       self.profiles[0][2]['worker'], 0)
            self.assertEqual(proof['frames']['kui_toy_loader_payload_call'], 0)
            report = target / 'worker/src/loader/toy_loader_payload_control.su'
            original = report.read_text()
            report.write_text(original.replace('kui_toy_loader_payload_call\t0\t',
                                                'kui_toy_loader_payload_call\t4\t'))
            with self.assertRaisesRegex(ValueError, 'linked dropped'):
                audit.audit_frames(target, self.profiles[0][1]['worker'],
                                   self.profiles[0][2]['worker'], 0)
            report.write_text(original)
            worker = copy.deepcopy(self.profiles[0][1]['worker'])
            worker['symbols']['_kui_toy_loader_payload_call'] = audit.HIGH + 128
            with self.assertRaisesRegex(ValueError, 'linked dropped'):
                audit.audit_frames(target, worker, self.profiles[0][2]['worker'], 0)
            report.write_bytes(b'')
            with self.assertRaisesRegex(ValueError, 'Missing or empty'):
                audit.audit_frames(target, self.profiles[0][1]['worker'],
                                   self.profiles[0][2]['worker'], 0)

    def replace_code_word(self, code, address, value, mnemonic=None, operands=None):
        code.image = mutate_word(code.image, audit.HIGH, address, value)
        code.payload = code.image['payload']
        code.rows = [(at, value, mnemonic or old_mnemonic,
                      old_operands if operands is None else operands) if at == address else row
                     for row in code.rows for at, _, old_mnemonic, old_operands in (row,)]
        return code

    def test_cached_helper_wrong_line_count_post_purge_or_CCR_binding_refused(self):
        code = self.code(1)
        rows = audit.exact_function(code, '_kui_toy_loader_payload_call')[2]
        line_count = next(row for row in rows if row[1] & 0xF0FF == 0xE010)
        post_purge = [row for row in rows if row[2] == 'ocbp'][1]
        for row, value in ((line_count, line_count[1] + 1), (post_purge, 0x0009)):
            with self.subTest(address=hex(row[0])):
                bad = self.replace_code_word(self.code(1), row[0], value)
                with self.assertRaisesRegex(ValueError, 'Changed reviewed'):
                    audit.audit_helper(bad, self.profiles[1][1]['resident-sci'], 1)
        ccr_load = next(row for row in rows if row[1] & 0xF000 == 0xD000 and
                        code.literal(row[0], row[1] >> 8 & 15)[1] == 0xff00001c)
        ccr_literal = code.literal(ccr_load[0], ccr_load[1] >> 8 & 15)[0]
        bad = self.code(1, mutate_word(code.image, audit.HIGH, ccr_literal, 0xff000020, 4))
        with self.assertRaisesRegex(ValueError, 'Changed reviewed'):
            audit.audit_helper(bad, self.profiles[1][1]['resident-sci'], 1)

    def test_programmed_helper_aligned_scratch_copy_on_failure_or_wrong_image_refused(self):
        code = self.code(2)
        rows = audit.exact_function(code, '_kui_toy_loader_payload_call')[2]
        scratch = code.symbols['_payload_pio_scratch']
        block = self.profiles[2][1]['resident-sci']['symbols']['_image'] + 32
        for old, new in ((scratch + 1, scratch), (block, block + 32)):
            with self.subTest(binding=hex(old)):
                load = next(row for row in rows if row[1] & 0xF000 == 0xD000 and
                            code.literal(row[0], row[1] >> 8 & 15)[1] == old)
                literal = code.literal(load[0], load[1] >> 8 & 15)[0]
                bad = self.code(2, mutate_word(code.image, audit.HIGH, literal, new, 4))
                with self.assertRaisesRegex(ValueError, 'Changed reviewed'):
                    audit.audit_helper(bad, self.profiles[2][1]['resident-sci'], 2)
        original_call = next(i for i, row in enumerate(rows) if row[2] == 'jsr')
        success_gate = next(row for row in rows[original_call + 2:] if row[2] in ('bt', 'bf', 'bt.s', 'bf.s'))
        bad = self.replace_code_word(self.code(2), success_gate[0], success_gate[1] ^ 0x0200)
        with self.assertRaisesRegex(ValueError, 'Changed reviewed'):
            audit.audit_helper(bad, self.profiles[2][1]['resident-sci'], 2)

    def test_graph_refuses_control_register_write_and_out_of_scope_cache_op(self):
        for mode in BUILDS:
            code = self.code(mode)
            row = audit.exact_function(code, '_data_probe_sample')[2][0]
            for value, mnemonic, operands in ((0x400e, 'ldc', 'r0,sr'),
                                               (0x00a3, 'ocbp', '@r0')):
                with self.subTest(mode=mode, mnemonic=mnemonic):
                    bad = self.replace_code_word(self.code(mode), row[0], value, mnemonic, operands)
                    with self.assertRaisesRegex(ValueError, 'interrupt/control registers|cache operation'):
                        audit.audit_probe_graph(bad, self.profiles[mode][1]['resident-sci'], mode)


if __name__ == '__main__':
    unittest.main()
