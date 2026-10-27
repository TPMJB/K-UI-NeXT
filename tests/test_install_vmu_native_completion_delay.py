#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Single-gateway delay modes keep exact restoration and typed mode flags."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import test_install_vmu_native_isolation as fixtures


class CompletionDelayInstallTests(unittest.TestCase):
    mode='completion_delay'
    other='submission_delay'
    ticks=dict(submit=0,complete=76)
    budget_text='only completion waits76 TMU0 ticks'
    no_helper_text='no submit delay helper runs'

    def setUp(self):
        self.fixture=fixtures.IsolationInstallTests();self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.entry=self.fixture.metadata['variants']['reader-only']
        self.entry.update(maple_bypass=True,maple_delay=True,
            maple_delay_ticks=self.ticks,prefetch_enabled=False,
            controller_tick_off=True,e1_wait_capture=False)
        self.entry.update({self.mode:True,self.other:False})
        self.fixture.write_metadata()

    def test_install_idempotence_switch_and_exact_restore(self):
        result=self.fixture.run_variant('reader-only')
        self.assertIs(result[self.mode],True)
        state=json.loads((self.fixture.card/'KUI/native-isolation/state.json').read_text())
        self.assertIs(state[self.mode],True)
        self.assertTrue(self.fixture.run_variant('reader-only')['already_installed'])
        self.assertNotIn(self.mode,self.fixture.run_variant('vmu-only'))
        self.assertTrue(self.fixture.run_variant('restore')['restored_exact_original_modules'])
        self.fixture.assert_protected()
        for path in fixtures.installer.MODULES.values():
            self.assertEqual((self.fixture.card/path).read_bytes(),self.fixture.original[str(path)])

    def test_completion_mode_and_ticks_are_typed_before_card_writes(self):
        valid=dict(self.entry);before=self.fixture.snapshot()
        changes=[{self.mode:value} for value in (False,0,1,None,'true')]
        changes += [{self.other:True}]
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
        self.assertIn(self.budget_text,result.stdout)
        self.assertIn(self.no_helper_text,result.stdout)
        self.assertNotIn('59/76',result.stdout)
        self.fixture.assert_protected()


class SubmissionDelayInstallTests(CompletionDelayInstallTests):
    mode='submission_delay'
    other='completion_delay'
    ticks=dict(submit=59,complete=0)
    budget_text='only submit waits59 TMU0 ticks'
    no_helper_text='no completion delay helper runs'


class PostStartDelayInstallTests(SubmissionDelayInstallTests):
    budget_text='submit waits59 TMU0 ticks after the original MDST software start'

    def setUp(self):
        super().setUp()
        self.entry['post_start_delay']=True
        self.fixture.write_metadata()

    def test_post_start_flag_round_trip_and_old_receipt_compatibility(self):
        # A previous submission-only receipt has no post-start field.
        self.entry['post_start_delay']=False;self.fixture.write_metadata()
        self.assertNotIn('post_start_delay',self.fixture.run_variant('reader-only'))
        self.entry['post_start_delay']=True;self.fixture.write_metadata()
        result=self.fixture.run_variant('reader-only')
        self.assertIs(result['post_start_delay'],True)
        state=json.loads((self.fixture.card/'KUI/native-isolation/state.json').read_text())
        self.assertIs(state['post_start_delay'],True)
        self.assertIs(self.fixture.run_variant('reader-only')['post_start_delay'],True)
        self.assertNotIn('post_start_delay',self.fixture.run_variant('vmu-only'))
        self.assertNotIn('post_start_delay',self.fixture.run_variant('restore'))
        self.fixture.assert_protected()

    def test_post_start_metadata_and_recovery_receipt_are_typed(self):
        before=self.fixture.snapshot()
        for value in (None,0,1,'true'):
            with self.subTest(value=value):
                self.entry['post_start_delay']=value;self.fixture.write_metadata()
                with self.assertRaisesRegex(ValueError,'Post-start-delay metadata'):
                    self.fixture.run_variant('reader-only')
                self.assertEqual(self.fixture.snapshot(),before)
        self.entry['post_start_delay']=True;self.fixture.write_metadata()
        self.fixture.run_variant('reader-only')
        path=self.fixture.card/'KUI/native-isolation/state.json'
        valid=json.loads(path.read_text())
        for change in (dict(post_start_delay=1),dict(submission_delay=False),
                       dict(maple_delay=False),dict(maple_bypass=False)):
            with self.subTest(change=change):
                path.write_text(json.dumps(dict(valid,**change)))
                snapshot=self.fixture.snapshot()
                with self.assertRaisesRegex(ValueError,'recovery receipt'):
                    self.fixture.run_variant('restore')
                self.assertEqual(self.fixture.snapshot(),snapshot)
        path.write_text(json.dumps(valid))
        self.fixture.run_variant('restore');self.fixture.assert_protected()


class PostStartPackageSelectionTests(unittest.TestCase):
    def test_post_start_requires_explicit_submission_delay(self):
        argv=[sys.executable,str(ROOT/'tools/package_vmu_native_isolation_delivery.py')]
        for option in ('--reader-build','--vmu-build','--linked-report','--gui','--host-log','--output'):
            argv += [option,'unused']
        result=subprocess.run([*argv,'--demand-only','--maple-bypass','--maple-delay','--post-start-delay'],
                              cwd=ROOT,text=True,capture_output=True)
        self.assertEqual(result.returncode,2)
        self.assertIn('--post-start-delay requires --submission-delay',result.stderr)


if __name__=='__main__':unittest.main()
