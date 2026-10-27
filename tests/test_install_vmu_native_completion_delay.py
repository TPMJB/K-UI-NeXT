#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The completion-only installer keeps exact restoration and typed mode flags."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import test_install_vmu_native_isolation as fixtures


class CompletionDelayInstallTests(unittest.TestCase):
    def setUp(self):
        self.fixture=fixtures.IsolationInstallTests();self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.entry=self.fixture.metadata['variants']['reader-only']
        self.entry.update(maple_bypass=True,maple_delay=True,completion_delay=True,
            maple_delay_ticks=dict(submit=0,complete=76),prefetch_enabled=False,
            controller_tick_off=True,e1_wait_capture=False)
        self.fixture.write_metadata()

    def test_install_idempotence_switch_and_exact_restore(self):
        result=self.fixture.run_variant('reader-only')
        self.assertIs(result['completion_delay'],True)
        state=json.loads((self.fixture.card/'KUI/native-isolation/state.json').read_text())
        self.assertIs(state['completion_delay'],True)
        self.assertTrue(self.fixture.run_variant('reader-only')['already_installed'])
        self.assertNotIn('completion_delay',self.fixture.run_variant('vmu-only'))
        self.assertTrue(self.fixture.run_variant('restore')['restored_exact_original_modules'])
        self.fixture.assert_protected()
        for path in fixtures.installer.MODULES.values():
            self.assertEqual((self.fixture.card/path).read_bytes(),self.fixture.original[str(path)])

    def test_completion_mode_and_ticks_are_typed_before_card_writes(self):
        valid=dict(self.entry);before=self.fixture.snapshot()
        changes=[dict(completion_delay=value) for value in (False,0,1,None,'true')]
        changes += [dict(maple_delay=False),dict(maple_bypass=False),
                    dict(maple_delay_ticks=dict(submit=59,complete=76)),
                    dict(maple_delay_ticks=dict(submit=0,complete=0))]
        for change in changes:
            with self.subTest(change=change):
                self.entry.clear();self.entry.update(valid,**change);self.fixture.write_metadata()
                with self.assertRaises(ValueError):self.fixture.run_variant('reader-only')
                self.assertEqual(self.fixture.snapshot(),before)

    def test_public_cli_reports_no_submit_helper_and_completion_budget(self):
        result=subprocess.run([sys.executable,str(self.fixture.package/'install.py'),str(self.fixture.card),
            '--profile',str(self.fixture.profile.relative_to(self.fixture.card)),'--variant','reader-only'],
            cwd=self.fixture.root,text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('only completion waits76 TMU0 ticks',result.stdout)
        self.assertIn('no submit delay helper runs',result.stdout)
        self.assertNotIn('59/76',result.stdout)
        self.fixture.assert_protected()


if __name__=='__main__':unittest.main()
