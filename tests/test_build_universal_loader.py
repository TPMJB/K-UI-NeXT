#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real rollout plan, production CE recipe and failure boundaries."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_universal_loader as builder

COMMIT = '0123456789abcdef0123456789abcdef01234567'


class UniversalBuild(unittest.TestCase):
    def test_plan_covers_real_collector_and_keeps_the_accepted_service_families_separate(self):
        selected = builder.plan(COMMIT, 3)
        covered = [role for step in selected['steps'] for role in step['roles']]
        self.assertEqual(len(covered), len(set(covered)))
        self.assertEqual(set(covered), set(builder.DEFAULT_BUILDS))
        commands = [step['command'] for step in selected['steps']]
        self.assertEqual(commands[2], ['make', '-f', 'Makefile.dc', '-j3',
                                      'BUILD_ID=' + COMMIT[:12], 'wince-cdda'])
        self.assertFalse(any('all' in command or 'tools/make_update.py' in command
                             or any('smash' in part or 'session-controls' in part for part in command)
                             for command in commands))
        self.assertFalse(selected['full_service_rollout_qualified'])
        self.assertFalse(selected['hardware_compatibility_claimed'])
        self.assertEqual(set(selected['separately_preserved_families']), {
            'standard_vmu', 'compact_background_a1_a2', 'physical_disc_vmu_a1_a2',
            'accepted_smash_collection'})

    def test_production_ce_aggregate_still_supplies_its_complete_isolated_configuration(self):
        # Evaluate the real make variable with an empty SDK rule include. This
        # checks the production recipe without a compiler or a native build.
        with tempfile.TemporaryDirectory() as temporary:
            (Path(temporary) / 'Makefile.rules').write_text('')
            rule = 'show-rollout-ce:\n\t$(info FLAGS=$(WINCE_CDDA_BUILD_FLAGS))\n\t@:\n'
            result = subprocess.check_output(['make', '--no-print-directory', '-s', '-rR',
                '-f', 'Makefile.dc', 'KOS_BASE=' + temporary,
                '--eval=' + rule, 'show-rollout-ce'], cwd=ROOT, text=True)
        line = next(line for line in result.splitlines() if line.startswith('FLAGS='))
        flags = dict(token.split('=', 1) for token in line[6:].split())
        self.assertEqual(flags, dict(KUI_RETAIL_LOW_RESIDENT='0', KUI_RETAIL_STARTUP_TRACE='0',
            KUI_RETAIL_SONIC_STACK_TEST='0', KUI_GENERAL_SCI_RESUME_FIX='0',
            KUI_CE_SCI_RESUME_FIX='1', KUI_RETAIL_RETURN_TRACE='0',
            KUI_RETAIL_NATIVE_DISC_CHECK='0', KUI_RETAIL_NATIVE_DRIVE_STATE='0'))

    def test_plan_runs_without_the_sdk_and_does_not_create_build_outputs(self):
        environment = {key: value for key, value in os.environ.items()
                       if key not in ('KOS_BASE', 'KOS_CC_BASE')}
        environment['PATH'] = '/usr/bin:/bin'
        result = subprocess.run([sys.executable, str(ROOT / 'tools/build_universal_loader.py'),
            'plan', '--jobs', '2'], cwd=ROOT, env=environment, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['schema'], 'kui.universal-loader-build-plan.v1')
        self.assertFalse(report['installable_update_created'])

    def test_bad_jobs_and_foreign_commit_refuse_before_build(self):
        for jobs in (0, 33):
            with self.assertRaisesRegex(ValueError, 'Jobs'):
                builder.plan(COMMIT, jobs)
        for commit in ('abc', 'G' * 40):
            with self.assertRaisesRegex(ValueError, 'complete source commit'):
                builder.plan(commit, 2)

    def test_build_stops_on_source_change_before_running_a_command(self):
        initial = dict(commit=COMMIT, source_dirty=False)
        with patch.object(builder.shutil, 'which', return_value='/sdk/sh-elf-gcc'), \
             patch.object(builder, 'source_state', return_value=dict(initial, source_dirty=True)), \
             patch.object(builder.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'Source changed'):
                builder.build(builder.plan(COMMIT, 2), initial, False)
            run.assert_not_called()

    def test_failed_step_has_no_success_receipt_and_does_not_run_later_steps(self):
        initial = dict(commit=COMMIT, source_dirty=False)
        selected = builder.plan(COMMIT, 2)
        with tempfile.TemporaryDirectory() as temporary, \
             patch.object(builder, 'ROOT', Path(temporary)), \
             patch.object(builder.shutil, 'which', return_value='/sdk/sh-elf-gcc'), \
             patch.object(builder, 'source_state', return_value=initial), \
             patch.object(builder.subprocess, 'run', return_value=subprocess.CompletedProcess([], 2)) as run:
            receipt = Path(temporary) / 'build/universal-loader-build.json'
            receipt.parent.mkdir()
            receipt.write_text('{"all_cold_build_steps_completed":true}')
            with self.assertRaisesRegex(ValueError, 'Build failed at ordinary'):
                builder.build(selected, initial, False)
            self.assertEqual(run.call_count, 1)
            self.assertFalse(receipt.exists())

    def test_check_requires_the_actual_gui_binding(self):
        with patch.object(builder, 'source_state', return_value=dict(commit=COMMIT, source_dirty=False)), \
             patch.object(sys, 'argv', ['build_universal_loader.py', 'check']), \
             contextlib.redirect_stderr(io.StringIO()) as stderr, \
             patch.object(builder, 'collect') as collect:
            with self.assertRaises(SystemExit) as caught:
                builder.main()
            self.assertEqual(caught.exception.code, 1)
            self.assertIn('--runtime-elf and --output', stderr.getvalue())
            collect.assert_not_called()


if __name__ == '__main__':
    unittest.main()
