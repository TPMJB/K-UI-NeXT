#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject apparent console wins without identity, activity, or matched scenes."""
import contextlib
from copy import deepcopy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'performance_results', ROOT / 'tools/analyze_performance_results.py')
RESULTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESULTS)


def baseline():
    return {
        'schema_version': 1, 'case_id': 'toy-intro-control-r1',
        'game': 'Toy Commander', 'revision': 'original-disc-revision-a',
        'workload_id': 'toy-first-intro', 'scene': 'first intro',
        'start_marker': 'first picture after logo',
        'end_marker': 'first main menu picture', 'save_seed': 'clean-slot-a1',
        'video_mode': 'VGA60', 'repeat': 1, 'media': 'sd', 'format': 'raw',
        'reader': 'native', 'profile': 'demand-control',
        'expected_profile': 'demand-control', 'build_id': 'abcdef0123c0',
        'expected_build_id': 'abcdef0123c0', 'prefetch': 'off',
        'background': 'on', 'vmu': 'off', 'vmu_slot': 'none', 'cdda': 'on',
        'engagement': {'loader': {'counter': 'DATA REQUESTS',
                                  'observed': 'hex:00000010'}},
        'clocks': {'phase0': {'valid_samples': 'hex:00000010',
                              'bad_calls': 0, 'bad_intervals': 0}},
        'metrics': {
            'elapsed_ms': {'value': 'dec:60000', 'unit': 'ms', 'source': 'phone'},
            'call_max_ms': {'value': 'hex:0000000A', 'unit': 'ms',
                            'clock': 'phase0'},
            'data_bytes': {'value': 'hex:00002000', 'unit': 'bytes'},
        },
    }


def candidate(control=None):
    row = deepcopy(control or baseline())
    row.update(case_id='toy-intro-candidate-r1', profile='demand-candidate',
               expected_profile='demand-candidate', build_id='abcdef0123c1',
               expected_build_id='abcdef0123c1',
               reference_case='toy-intro-control-r1', contrast='profile')
    row['engagement']['candidate'] = {
        'counter': 'CANDIDATE ENGAGED', 'observed': 'hex:00000001'}
    row['metrics']['elapsed_ms']['value'] = 'dec:57000'
    return row


class TypedValueTests(unittest.TestCase):
    def test_decimal_and_hex_require_an_explicit_choice(self):
        self.assertEqual(RESULTS.number('hex:00000010'), 16)
        self.assertEqual(RESULTS.number('0x00000010'), 16)
        self.assertEqual(RESULTS.number('dec:00000010'), 10)
        self.assertEqual(RESULTS.number('00000010', 16), 16)
        self.assertEqual(RESULTS.number('00000010', 10), 10)
        self.assertEqual(RESULTS.number(10), 10)
        self.assertEqual(RESULTS.number('dec:10.5'), 10.5)
        for value in ('00000010', 'DEADBEEF', '1e3'):
            with self.subTest(value=value), self.assertRaises(RESULTS.ResultError):
                RESULTS.number(value)

    def test_missing_is_distinct_from_zero(self):
        for value in (None, '', '-', 'n/a', 'unavailable', 'UNKNOWN'):
            self.assertIsNone(RESULTS.number(value))
        self.assertEqual(RESULTS.number('hex:00000000'), 0)

    def test_malformed_or_nonfinite_values_are_rejected(self):
        for value in ('hex:', '0x-1', 'hex:GG', 'dec:-1', 'dec:nan',
                      -1, True, float('nan'), float('inf'), 'dec:' + '9' * 400 + '.0'):
            with self.subTest(value=value), self.assertRaises(RESULTS.ResultError):
                RESULTS.number(value)
        for value in ('dec:1.5', 1.0, 'hex:100000000'):
            with self.subTest(value=value), self.assertRaises(RESULTS.ResultError):
                RESULTS.count(value)

    def test_uint32_max_is_not_globally_treated_as_missing(self):
        self.assertEqual(RESULTS.count('hex:FFFFFFFF'), 0xffffffff)
        row = baseline()
        row['metrics']['counter'] = {'value': 'hex:FFFFFFFF', 'unit': 'count'}
        measured = RESULTS.assess(row)
        self.assertEqual(measured['metrics']['counter']['status'], 'available')
        row['metrics']['counter']['unavailable_values'] = ['hex:FFFFFFFF']
        missing = RESULTS.assess(row)
        self.assertEqual(missing['metrics']['counter']['status'], 'unavailable (sentinel)')


class EvidenceTests(unittest.TestCase):
    def test_unavailable_and_invalid_clocks_do_not_compare_as_zero_ms(self):
        cases = (
            ({}, 'unavailable'),
            ({'valid_samples': 0, 'bad_calls': 0, 'bad_intervals': 0},
             'unavailable (zero valid samples)'),
            ({'valid_samples': 3, 'bad_calls': 1, 'bad_intervals': 0}, 'invalid'),
            ({'valid_samples': 3, 'bad_calls': 0, 'bad_intervals': 1}, 'invalid'),
            ({'valid_samples': 3, 'bad_calls': 0, 'bad_intervals': 0,
              'saturated': 1}, 'invalid'),
        )
        for clock, expected in cases:
            with self.subTest(clock=clock):
                control = baseline()
                row = candidate()
                row['clocks']['phase0'] = clock
                row['metrics']['call_max_ms']['value'] = 0
                review = RESULTS.analyze([control, row])[1]
                self.assertEqual(review['clocks']['phase0'], expected)
                self.assertTrue(review['metrics']['call_max_ms']['status'].startswith('unavailable'))
                self.assertNotIn('call_max_ms', review['comparison']['metrics'])
                self.assertIn('elapsed_ms', review['comparison']['metrics'])

    def test_report_time_requires_clock_but_phone_elapsed_does_not(self):
        row = baseline()
        row['clocks'] = {}
        result = RESULTS.assess(row)
        self.assertEqual(result['metrics']['elapsed_ms']['status'], 'available')
        self.assertTrue(result['metrics']['call_max_ms']['status'].startswith('unavailable'))

    def test_toy_clock_epoch_evidence_requires_every_stability_counter(self):
        valid = {'valid_samples': 'hex:00000010', 'clock_epoch': 0,
                 'unmatched_ends': 0, 'nested_begins': 0}
        self.assertEqual(RESULTS.clock_status(valid), 'valid')
        for field in ('clock_epoch', 'unmatched_ends', 'nested_begins'):
            with self.subTest(field=field):
                invalid = dict(valid, **{field: 1})
                self.assertEqual(RESULTS.clock_status(invalid), 'invalid')
                missing = dict(valid)
                del missing[field]
                self.assertEqual(RESULTS.clock_status(missing), 'unavailable')
        self.assertEqual(RESULTS.clock_status(dict(valid, valid_samples=0)),
                         'unavailable (zero valid samples)')
        self.assertEqual(RESULTS.clock_status(dict(valid, valid_samples=None)), 'unavailable')
        # A partial primary family cannot be filled using an unrelated family.
        self.assertEqual(RESULTS.clock_status(dict(valid, bad_calls=0)), 'unavailable')

    def test_exact_identity_is_required_before_attributing_a_profile_change(self):
        for field in ('build_id', 'profile'):
            with self.subTest(field=field):
                row = candidate()
                row[field] = 'another-variant'
                result = RESULTS.analyze([baseline(), row])[1]
                self.assertFalse(result['eligible'])
                self.assertEqual(result['comparison']['status'], 'refused')
        row = baseline()
        del row['expected_build_id']
        self.assertFalse(RESULTS.assess(row)['eligible'])

    def test_extra_instability_flags_cannot_be_hidden_by_a_valid_primary_family(self):
        clock = {'valid_samples': 8, 'bad_calls': 0, 'bad_intervals': 0,
                 'clock_epoch': 1, 'unmatched_ends': 0, 'nested_begins': 0}
        self.assertEqual(RESULTS.clock_status(clock), 'invalid')

    def test_cycle_times_need_clock_evidence_while_logical_counters_remain_usable(self):
        control = baseline()
        row = candidate()
        for item in (control, row):
            item['metrics']['gd_cycles_max'] = {
                'value': 'hex:00000040', 'unit': 'cycles', 'clock': 'phase0'}
        row['clocks']['phase0']['bad_calls'] = 1
        review = RESULTS.analyze([control, row])[1]
        self.assertTrue(review['eligible'])
        self.assertTrue(review['metrics']['gd_cycles_max']['status'].startswith('unavailable'))
        self.assertNotIn('gd_cycles_max', review['comparison']['metrics'])
        self.assertIn('data_bytes', review['comparison']['metrics'])

    def test_sd_identity_must_use_all_twelve_displayed_hex_characters(self):
        for identity in ('abcdef0123', 'abcdef0123c01', 'abcdef0123g0', '0xabcdef0123c0'):
            with self.subTest(identity=identity):
                row = baseline()
                row.update(build_id=identity, expected_build_id=identity)
                result = RESULTS.assess(row)
                self.assertFalse(result['eligible'])
                self.assertTrue(any('12 hexadecimal' in issue for issue in result['issues']))
        row = baseline()
        row['build_id'] = row['build_id'].upper()
        self.assertTrue(RESULTS.assess(row)['eligible'])

    def test_loader_activity_cannot_be_missing_zero_or_have_zero_threshold(self):
        for evidence in ({}, {'counter': 'DATA CALLS', 'observed': 0},
                         {'counter': 'DATA CALLS', 'observed': 10, 'minimum': 0}):
            with self.subTest(evidence=evidence):
                row = baseline()
                row['engagement']['loader'] = evidence
                result = RESULTS.assess(row)
                self.assertFalse(result['eligible'])
                self.assertNotEqual(result['activity']['loader'], 'engaged')

    def test_candidate_and_vmu_require_their_own_activation_evidence(self):
        row = candidate()
        row['engagement']['candidate']['observed'] = 0
        result = RESULTS.analyze([baseline(), row])[1]
        self.assertEqual(result['activity']['candidate'], 'unengaged')
        self.assertEqual(result['comparison']['status'], 'refused')
        row = baseline()
        row.update(vmu='on', vmu_slot='A2')
        result = RESULTS.assess(row)
        self.assertEqual(result['activity']['vmu'], 'unavailable')
        row['engagement']['vmu'] = {'counter': 'VMU HANDLED', 'observed': 1}
        self.assertTrue(RESULTS.assess(row)['eligible'])

    def test_active_vmu_requires_a_known_slot(self):
        for mode in ('on', 'physical'):
            for slot in (None, 'none', 'A0', 'a2', 'A3', 'unknown'):
                with self.subTest(mode=mode, slot=slot):
                    row = baseline()
                    row.update(vmu=mode, vmu_slot=slot)
                    row['engagement']['vmu'] = {'counter': 'VMU HANDLED', 'observed': 1}
                    result = RESULTS.assess(row)
                    self.assertFalse(result['eligible'])
                    self.assertIn('active VMU requires an explicit vmu_slot', result['issues'])

    def test_cdda_scene_requires_cdda_counter_only_when_explicitly_requested(self):
        row = baseline()
        row['required_activity'] = ['cdda']
        result = RESULTS.assess(row)
        self.assertEqual(result['activity']['cdda'], 'unavailable')
        row['engagement']['cdda'] = {'counter': 'RAW BYTES', 'observed': 'hex:00008000'}
        self.assertTrue(RESULTS.assess(row)['eligible'])


class MatchingTests(unittest.TestCase):
    def test_profile_pair_reports_the_measured_delta(self):
        result = RESULTS.analyze([baseline(), candidate()])[1]
        self.assertTrue(result['eligible'])
        self.assertEqual(result['comparison']['status'], 'matched')
        elapsed = result['comparison']['metrics']['elapsed_ms']
        self.assertEqual(elapsed['delta'], -3000)
        self.assertEqual(elapsed['change_pct'], -5.0)

    def test_different_workload_or_second_changed_setting_refuses_comparison(self):
        for field in (*RESULTS.WORKLOAD, 'prefetch', 'background', 'vmu',
                      'cdda', 'format', 'reader', 'media', 'vmu_slot'):
            with self.subTest(field=field):
                row = candidate()
                if field == 'repeat':
                    row[field] = 2
                elif field in ('prefetch', 'background', 'cdda'):
                    row[field] = 'on' if row[field] == 'off' else 'off'
                elif field == 'vmu':
                    row.update(vmu='on', vmu_slot='A2')
                    row['engagement']['vmu'] = {'counter': 'VMU HANDLED', 'observed': 1}
                else:
                    row[field] = 'different-' + str(row[field])
                result = RESULTS.analyze([baseline(), row])[1]
                self.assertEqual(result['comparison']['status'], 'refused')
                self.assertIn(field, result['comparison']['reason'])

    def test_vmu_pair_changes_only_vmu(self):
        control = baseline()
        row = deepcopy(control)
        row.update(case_id='toy-vmu-on-r1', reference_case=control['case_id'],
                   contrast='vmu', vmu='on', vmu_slot='A2')
        row['engagement']['vmu'] = {'counter': 'VMU HANDLED', 'observed': 20}
        result = RESULTS.analyze([control, row])[1]
        self.assertEqual(result['comparison']['status'], 'matched')
        self.assertEqual(result['comparison']['changed'], ['vmu', 'vmu_slot'])

    def test_whole_system_can_compare_disc_physical_vmu_with_sd_virtual_vmu(self):
        disc = baseline()
        disc.update(case_id='toy-disc-r1', media='disc', format='disc',
                    reader='drive', profile='physical-disc', background='n/a',
                    vmu='physical', vmu_slot='A1')
        for field in ('build_id', 'expected_build_id', 'expected_profile', 'engagement'):
            disc.pop(field)
        sd = baseline()
        sd.update(case_id='toy-sd-vmu-r1', reference_case=disc['case_id'],
                  contrast='system', vmu='on', vmu_slot='A2')
        sd['engagement']['vmu'] = {'counter': 'VMU HANDLED', 'observed': 20}
        result = RESULTS.analyze([disc, sd])[1]
        self.assertEqual(result['comparison']['status'], 'matched')
        self.assertIn('vmu', result['comparison']['changed'])
        self.assertIn('media', result['comparison']['changed'])
        self.assertIn('vmu_slot', result['comparison']['changed'])

    def test_system_label_cannot_bypass_profile_activation_with_two_sd_runs(self):
        row = candidate()
        row['contrast'] = 'system'
        row['engagement'].pop('candidate')
        result = RESULTS.analyze([baseline(), row])[1]
        self.assertEqual(result['comparison']['status'], 'refused')
        self.assertEqual(result['comparison']['reason'], 'system contrast requires a disc/SD pair')

    def test_unknown_media_is_ineligible_even_when_both_rows_agree(self):
        for media in ('usb', 'SD', 'unknown'):
            with self.subTest(media=media):
                row = baseline()
                row['media'] = media
                result = RESULTS.assess(row)
                self.assertFalse(result['eligible'])
                self.assertIn('media must be sd or disc', result['issues'])

    def test_format_pair_keeps_profile_and_settings_fixed(self):
        control = baseline()
        row = deepcopy(control)
        row.update(case_id='toy-cooked-r1', reference_case=control['case_id'],
                   contrast='format', format='cooked')
        result = RESULTS.analyze([control, row])[1]
        self.assertEqual(result['comparison']['status'], 'matched')

    def test_reader_contrast_allows_abi_profile_and_background_but_keeps_other_settings(self):
        control = baseline()
        row = candidate()
        row.update(contrast='reader', reader='new', background='off')
        result = RESULTS.analyze([control, row])[1]
        self.assertEqual(result['comparison']['status'], 'matched')
        self.assertEqual(result['comparison']['changed'], ['background', 'profile', 'reader'])
        for field, changed in (('prefetch', 'on'), ('vmu', 'physical'),
                               ('cdda', 'off'), ('format', 'cooked')):
            with self.subTest(field=field):
                incompatible = deepcopy(row)
                incompatible[field] = changed
                result = RESULTS.analyze([control, incompatible])[1]
                self.assertEqual(result['comparison']['status'], 'refused')
                self.assertIn(field, result['comparison']['reason'])

    def test_reader_label_cannot_bypass_activation_for_a_profile_only_change(self):
        row = candidate()
        row['contrast'] = 'reader'
        row['engagement'].pop('candidate')
        result = RESULTS.analyze([baseline(), row])[1]
        self.assertEqual(result['comparison']['status'], 'refused')
        self.assertIn('reader', result['comparison']['reason'])

    def test_units_or_measurement_sources_are_not_mixed(self):
        row = candidate()
        row['metrics']['elapsed_ms']['source'] = 'report'
        row['metrics']['elapsed_ms']['clock'] = 'phase0'
        row['metrics']['data_bytes']['unit'] = 'sectors'
        result = RESULTS.analyze([baseline(), row])[1]
        metrics = result['comparison']['metrics']
        self.assertNotIn('elapsed_ms', metrics)
        self.assertNotIn('data_bytes', metrics)

    def test_zero_reference_has_no_percentage_claim(self):
        control = baseline()
        control['metrics']['elapsed_ms']['value'] = 0
        result = RESULTS.analyze([control, candidate()])[1]
        self.assertIsNone(result['comparison']['metrics']['elapsed_ms']['change_pct'])
        self.assertIn('zero reference', RESULTS.markdown([result]))

    def test_duplicate_and_absent_or_self_references_are_rejected(self):
        with self.assertRaisesRegex(RESULTS.ResultError, 'duplicate case_id'):
            RESULTS.analyze([baseline(), baseline()])
        row = candidate()
        self.assertEqual(RESULTS.analyze([row])[0]['comparison']['reason'], 'missing reference_case')
        row['reference_case'] = row['case_id']
        self.assertEqual(RESULTS.analyze([row])[0]['comparison']['reason'], 'self reference')
        with self.assertRaisesRegex(RESULTS.ResultError, 'no result rows'):
            RESULTS.analyze([])


class FileInputTests(unittest.TestCase):
    def test_csv_dotted_columns_preserve_hex_and_expand_required_activity(self):
        row = RESULTS.expand_csv({
            'schema_version': '1', 'repeat': '2', 'radix': '16',
            'required_activity': 'loader;cdda',
            'engagement.loader.counter': 'DATA REQUESTS',
            'engagement.loader.observed': '00000010',
            'metrics.elapsed_ms.value': 'dec:57000',
            'metrics.elapsed_ms.unit': 'ms', 'metrics.elapsed_ms.source': 'phone',
        })
        self.assertEqual(row['repeat'], 2)
        self.assertEqual(row['required_activity'], ['loader', 'cdda'])
        self.assertEqual(RESULTS.count(row['engagement']['loader']['observed'], row['radix']), 16)
        self.assertEqual(RESULTS.number(row['metrics']['elapsed_ms']['value'], row['radix']), 57000)

    def test_bad_csv_shape_and_jsonl_line_are_not_silently_dropped(self):
        with self.assertRaisesRegex(RESULTS.ResultError, 'more cells'):
            RESULTS.expand_csv({'case_id': 'one', None: ['extra']})
        with self.assertRaisesRegex(RESULTS.ResultError, 'conflicting'):
            RESULTS.expand_csv({'metrics': 'scalar', 'metrics.elapsed.value': 'dec:1'})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'reports.jsonl'
            path.write_text(json.dumps(baseline()) + '\n{broken}\n')
            with self.assertRaisesRegex(RESULTS.ResultError, 'JSONL line 2'):
                RESULTS.read_rows(path)

    def test_cli_reads_a_real_jsonl_pair_and_returns_nonzero_for_unengaged_variant(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'reports.jsonl'
            row = candidate()
            path.write_text('\n'.join(json.dumps(item) for item in (baseline(), row)))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                status = RESULTS.main([str(path), '--json'])
            self.assertEqual(status, 0)
            parsed = json.loads(output.getvalue())
            self.assertEqual(parsed['results'][1]['comparison']['status'], 'matched')
            row['engagement']['candidate']['observed'] = 0
            path.write_text('\n'.join(json.dumps(item) for item in (baseline(), row)))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(RESULTS.main([str(path)]), 1)


if __name__ == '__main__':
    unittest.main()
