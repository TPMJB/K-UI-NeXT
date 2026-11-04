#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Result forms carry real expected identities, never simulated observations."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PLAN = load('performance_test_plan')
RESULTS = load('analyze_performance_results')


def profiles(cooked=True):
    result = {}
    for name, suffix in PLAN.SUFFIX.items():
        module = 'cdda-native-toy-raw.kui' if name.startswith('T') else name.lower() + '.kui'
        result[name] = {'folder': 'test-' + name, 'modules': [
            {'member': 'test-' + name + '/KUI/apps/games/' + module,
             'build_id': '123456789a' + suffix}],
            'virtual_vmu_supported': name in ('N1', 'N2', 'N3', 'S1'),
            'virtual_slot': 'A2'}
        if name.startswith('T') and cooked:
            result[name]['modules'].append({
                'member': 'test-' + name + '/KUI/apps/games/cdda-native-toy-cooked.kui',
                'build_id': '123456789a' + suffix})
    return result


def modeled_observations(rows):
    """Host fixture only: exercise comparison contracts without console claims."""
    result = deepcopy(rows)
    for row in result:
        row.update(revision=row['game'] + '-test-revision', save_seed='same-logical-save',
                   start_marker=row['scene'] + '-start', end_marker=row['scene'] + '-end',
                   video_mode='VGA60', build_id=row['expected_build_id'])
        for evidence in row['engagement'].values():
            evidence['observed'] = 1
        for clock in row['clocks'].values():
            clock.update(valid_samples=1, bad_calls=0, bad_intervals=0, saturated=0)
        for metric in row['metrics'].values():
            metric['value'] = 100
    return result


class PerformancePlanTests(unittest.TestCase):
    def test_purposeful_matrix_and_only_supplied_formats(self):
        rows = PLAN.make_cases(profiles())
        self.assertEqual(len(rows), 35)
        self.assertEqual(len({row['case_id'] for row in rows}), 35)
        self.assertEqual([row['stage'] for row in rows], sorted(row['stage'] for row in rows))
        cooked = [row for row in rows if row['format'] == 'cooked']
        self.assertEqual([(row['profile'], row['game']) for row in cooked], [('T1', 'Toy Commander')])
        self.assertEqual(len(PLAN.make_cases(profiles(cooked=False))), 34)
        quiet = [row for row in rows if row['game'] == 'Power Stone 2' and row['scene'] == 'quiet']
        self.assertEqual(len(quiet), 10)  # Disc plus every legal nine-profile condition.
        self.assertEqual({(row['profile'], row['vmu']) for row in quiet if row['media'] == 'sd'},
                         {('N0', 'off'), ('N1', 'off'), ('N1', 'on'), ('N2', 'off'),
                          ('N2', 'on'), ('N3', 'off'), ('N3', 'on'), ('S0', 'off'), ('S1', 'on')})

    def test_expected_ids_come_from_actual_selected_module_but_observations_are_empty(self):
        source = profiles()
        rows = PLAN.make_cases(source)
        for row in rows:
            with self.subTest(case=row['case_id']):
                self.assertIsNone(row['build_id'])
                for key in ('revision', 'save_seed', 'video_mode', 'start_marker', 'end_marker'):
                    self.assertEqual(row[key], 'UNKNOWN')
                self.assertTrue(all(value is None for value in row['observations'].values()))
                self.assertTrue(all(item['observed'] is None for item in row['engagement'].values()))
                self.assertTrue(all(metric['value'] is None for metric in row['metrics'].values()))
                self.assertTrue(all(value is None for clock in row['clocks'].values() for value in clock.values()))
                if row['media'] == 'sd':
                    selected = next(module for module in source[row['profile']]['modules']
                                    if module['member'] == row['module_member'])
                    self.assertEqual(row['expected_build_id'], selected['build_id'])
                    self.assertEqual(row['storage'], 'SCI')
                    self.assertEqual(row['prefetch'], 'off')
        self.assertTrue(all(not review['eligible'] for review in RESULTS.analyze(rows)))

    def test_identity_mismatch_or_mixed_source_manifest_is_rejected(self):
        for replacement in ('123456789ad1', '123456789ag0', 'short'):
            bad = profiles()
            bad['T1']['modules'][0]['build_id'] = replacement
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                PLAN.make_cases(bad)
        bad = profiles()
        bad['N0']['modules'][0]['build_id'] = 'aaaaaaaaaab2'
        with self.assertRaisesRegex(ValueError, 'source build prefix'):
            PLAN.make_cases(bad)
        with self.assertRaisesRegex(ValueError, 'Missing required'):
            PLAN.make_cases({})
        bad = profiles()
        bad['T1']['modules'].append(dict(bad['T1']['modules'][0]))
        with self.assertRaisesRegex(ValueError, 'one module'):
            PLAN.make_cases(bad)

    def test_no_unsupported_vmu_or_movie_capture_is_planned(self):
        for row in PLAN.make_cases(profiles()):
            if row['profile'] in ('T1', 'T2', 'T3', 'C1', 'C2', 'N0', 'S0'):
                self.assertEqual(row['vmu'], 'off')
            if row['profile'] == 'S1':
                self.assertEqual((row['reader'], row['vmu'], row['vmu_slot']), ('Standard', 'on', 'A2'))
            if row['vmu'] == 'on':
                self.assertEqual(row['vmu_slot'], 'A2')
                self.assertIn('vmu', row['required_activity'])
            else:
                self.assertNotIn('vmu', row['required_activity'])
            if row['scene'] == 'movie' and row['profile'] in PLAN.NATIVE:
                self.assertEqual(row['capture_mode'], 'phone-unarmed')
                self.assertTrue(row['observation_only'])
                self.assertEqual(row['clocks'], {})
            if row['capture_mode'] == 'perf-quiet':
                self.assertIn(row['scene'], ('quiet', 'save-reboot'))
                self.assertIn(row['profile'], ('N1', 'N2'))

    def test_every_declared_pair_matches_workload_and_preserves_real_candidate_gate(self):
        rows = modeled_observations(PLAN.make_cases(profiles()))
        reviews = RESULTS.analyze(rows)
        for row, review in zip(rows, reviews):
            with self.subTest(case=row['case_id']):
                if row['observation_only']:
                    self.assertFalse(review['eligible'])
                    self.assertTrue(any('observation-only' in issue for issue in review['issues']))
                    self.assertEqual(review['comparison']['status'], 'refused')
                else:
                    self.assertTrue(review['eligible'], review['issues'])
                    if row.get('reference_case'):
                        self.assertEqual(review['comparison']['status'], 'matched', review['comparison'])
                if row.get('contrast') == 'profile':
                    self.assertIn('candidate', review['activity'])
                    row['engagement']['candidate']['observed'] = 0
                    self.assertFalse(RESULTS.assess(row)['eligible'])

    def test_csv_and_jsonl_roundtrip_keep_blank_measurements_blank(self):
        rows = PLAN.make_cases(profiles())
        with tempfile.TemporaryDirectory() as temporary:
            csv_path = Path(temporary) / 'results.csv'
            jsonl_path = Path(temporary) / 'results.jsonl'
            csv_path.write_bytes(PLAN.csv_bytes(rows))
            jsonl_path.write_bytes(PLAN.jsonl_bytes(rows))
            for path in (csv_path, jsonl_path):
                parsed = RESULTS.read_rows(path)
                self.assertEqual(len(parsed), 35)
                review = RESULTS.analyze(parsed)
                self.assertTrue(all(not row['eligible'] for row in review))
                for row in parsed:
                    self.assertIsNone(row.get('build_id'))
                    self.assertTrue(all(proof.get('observed') is None for proof in row.get('engagement', {}).values()))
            self.assertEqual(json.loads(jsonl_path.read_text().splitlines()[0])['build_id'], None)

    def test_observation_only_cannot_be_made_eligible_by_filling_every_counter(self):
        rows = modeled_observations(PLAN.make_cases(profiles()))
        movie = next(row for row in rows if row['profile'] == 'N2' and row['scene'] == 'movie')
        movie['engagement']['candidate'] = {'counter': 'not a real movie capture', 'observed': 100}
        self.assertFalse(RESULTS.assess(movie)['eligible'])
        invalid = dict(movie, observation_only='true')
        self.assertIn('observation_only must be boolean', RESULTS.assess(invalid)['issues'])
        with self.assertRaisesRegex(RESULTS.ResultError, 'true or false'):
            RESULTS.expand_csv({'observation_only': 'yes'})

    def test_quiet_demand_zero_is_not_manufactured_as_an_unengaged_loader(self):
        rows = modeled_observations(PLAN.make_cases(profiles()))
        quiet = next(row for row in rows if row['profile'] == 'N1' and row['scene'] == 'quiet' and row['vmu'] == 'off')
        quiet['metrics']['data_irq_blocks']['value'] = 0
        quiet['metrics']['data_call_blocks']['value'] = 0
        self.assertTrue(RESULTS.assess(quiet)['eligible'])
        quiet['engagement']['loader']['observed'] = 0
        self.assertFalse(RESULTS.assess(quiet)['eligible'])

    def test_case_guide_lists_ids_and_actual_counter_legend_without_console_claim(self):
        guide = PLAN.case_guide(PLAN.make_cases(profiles()))
        self.assertIn('unmeasured', guide)
        self.assertIn('123456789ab2', guide)
        self.assertIn('SD0 = DMA, PIO, CMD18 starts, kept', guide)
        self.assertIn('SDK cookie or menu selection alone', guide)
        self.assertIn('no L+R+B in a movie', guide)

    def test_fixed_storage_source_cannot_drift_inside_a_profile_pair(self):
        rows = modeled_observations(PLAN.make_cases(profiles()))
        target = next(row for row in rows if row['profile'] == 'T2' and row['scene'] == 'intro1')
        target['storage'] = 'SCIF'
        review = next(result for result in RESULTS.analyze(rows) if result['case_id'] == target['case_id'])
        self.assertEqual(review['comparison']['status'], 'refused')
        self.assertIn('storage', review['comparison']['reason'])

    def test_an_unfilled_reported_saturation_flag_cannot_be_assumed_zero(self):
        row = next(row for row in PLAN.make_cases(profiles()) if row['profile'] == 'T1')
        clock = row['clocks']['phase0']
        clock.update(valid_samples=1, bad_calls=0, bad_intervals=0)
        self.assertTrue(RESULTS.clock_status(clock).startswith('unavailable'))
        clock['saturated'] = 0
        self.assertEqual(RESULTS.clock_status(clock), 'valid')


if __name__ == '__main__':
    unittest.main()
