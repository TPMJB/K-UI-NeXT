#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject mislabeled controls or calls through an undelayed Maple gateway."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import check_native_isolation_linked as linked
import check_native_wait_stage_guard as stage


class CompletionDelayModeTests(unittest.TestCase):
    def test_side_selection_requires_delay_before_artifact_access(self):
        for side in ('completion','submission'):
            with self.subTest(side=side):
                options={side+'_delay':True}
                expected=side.capitalize()+' delay requires Maple delay'
                with self.assertRaisesRegex(ValueError,expected):
                    linked.run_variant(None,None,None,'reader_only',**options)
                with self.assertRaisesRegex(ValueError,expected):
                    stage.check(None,None,None,**options)

    def test_side_selections_are_mutually_exclusive_before_artifact_access(self):
        with self.assertRaisesRegex(ValueError,'Select only one Maple delay side'):
            linked.run_variant(None,None,None,'reader_only',completion_delay=True,submission_delay=True)
        with self.assertRaisesRegex(ValueError,'Select only one Maple delay side'):
            stage.check(None,None,None,completion_delay=True,submission_delay=True)

    def test_post_start_selection_requires_submission_before_artifact_access(self):
        with self.assertRaisesRegex(ValueError,'Post-start delay requires submission delay'):
            linked.run_variant(None,None,None,'reader_only',post_start_delay=True)
        with self.assertRaisesRegex(ValueError,'Post-start delay requires submission delay'):
            stage.check(None,None,None,post_start_delay=True)

    def test_undelayed_gateway_rejects_every_call_form(self):
        for side,role in (('completion','submit'),('submission','complete')):
            for op in (0x400b,0x430b,0x0003,0x0703,0xb000,0xb7ff):
                with self.subTest(side=side,op=hex(op)):
                    cpu=linked.BypassWorker.__new__(linked.BypassWorker)
                    cpu.bypass_running=True;cpu.completion_delay=side=='completion'
                    cpu.submission_delay=side=='submission'
                    cpu.post_start_delay=False;cpu.post_start_current=None
                    cpu.delay_active=None;cpu.delay_entries={};cpu.forbidden_maple_entries={}
                    cpu.pc=0x8c100100;cpu.sd_entry=0x8c007000;cpu.tick_entry=None
                    cpu.gateway_ranges={role:(cpu.pc,cpu.pc+2)}
                    cpu.read=lambda address,count:op
                    with self.assertRaisesRegex(ValueError,role+' gateway executed a helper call'):
                        cpu.step()

    def test_post_start_first_clock_requires_one_actual_mdst_write(self):
        for writes in ([],[{},{}],[{}]):
            with self.subTest(writes=len(writes)):
                cpu=linked.BypassWorker.__new__(linked.BypassWorker)
                cpu.post_start_delay=True;cpu.pc=0x8c100100;cpu.instructions=123
                cpu.post_start_current={'mdst_writes':writes,'first_clock_read':None}
                if len(writes)!=1:
                    with self.assertRaisesRegex(ValueError,'exactly one MDST write'):
                        cpu.post_start_clock_read(0xffc00000)
                else:
                    cpu.post_start_clock_read(0xffc00000)
                    self.assertEqual(cpu.post_start_current['mdst_writes_before_first_clock_read'],1)
                    self.assertEqual(cpu.post_start_current['first_clock_read']['address'],'0xffc00000')

    def test_pending_priority4_stays_blocked_until_sdk_state_is_published(self):
        cpu=linked.BypassWorker.__new__(linked.BypassWorker)
        cpu.post_start_delay=True;cpu.pc=0x8c100100;cpu.instructions=123
        cpu.post_start_current=dict(pending_bit12=True,pending_instruction_samples=0,
            min_imask=15,premature_dispatches=0,first_publication=None)
        cpu.post_start_tail_state=lambda:dict(fc0=1,lockbyte=128)
        cpu.sr=0x60000060
        cpu.post_start_pending_observe()
        self.assertEqual(cpu.post_start_current['min_imask'],6)
        self.assertEqual(cpu.post_start_current['premature_dispatches'],0)
        cpu.sr=0x60000030
        with self.assertRaisesRegex(ValueError,'before SDK tail publication'):
            cpu.post_start_pending_observe()
        self.assertEqual(cpu.post_start_current['premature_dispatches'],1)
        cpu.post_start_tail_state=lambda:dict(fc0=0,lockbyte=0)
        cpu.post_start_pending_observe()
        self.assertEqual(cpu.post_start_current['first_publication']['fc0'],0)

    def test_stage_marker_never_silently_selects_single_side_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            build=Path(directory)
            for side,stem in (('completion','complete'),('submission','submit')):
                key='__kui_native_maple_delay_'+stem+'_only_isolation_stage'
                for marker in (None,1,(0,0,0,0xfff1)):
                    with self.subTest(side=side,marker=marker):
                        symbols={'__kui_native_maple_delay_isolation_stage':stage.ABSOLUTE_ONE}
                        if marker is not None:symbols[key]=marker
                        with patch.object(stage,'read_elf',return_value=(None,None,None,None,symbols)):
                            with self.assertRaisesRegex(ValueError,side+'-only delay marker differs'):
                                stage.check(build,None,None,maple_delay=True,**{side+'_delay':True})
                symbols={'__kui_native_maple_delay_isolation_stage':stage.ABSOLUTE_ONE,key:stage.ABSOLUTE_ONE}
                with patch.object(stage,'read_elf',return_value=(None,None,None,None,symbols)):
                    with self.assertRaisesRegex(ValueError,side+'-only delay marker differs'):
                        stage.check(build,None,None,maple_delay=True)

    def test_stage_post_start_marker_requires_explicit_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            build=Path(directory)
            symbols={'__kui_native_maple_delay_isolation_stage':stage.ABSOLUTE_ONE,
                     '__kui_native_maple_delay_submit_only_isolation_stage':stage.ABSOLUTE_ONE}
            key='__kui_native_maple_delay_post_start_isolation_stage'
            for marker in (None,1,(0,0,0,0xfff1)):
                with self.subTest(marker=marker):
                    selected=dict(symbols)
                    if marker is not None:selected[key]=marker
                    with patch.object(stage,'read_elf',return_value=(None,None,None,None,selected)):
                        with self.assertRaisesRegex(ValueError,'post-start delay marker differs'):
                            stage.check(build,None,None,maple_delay=True,submission_delay=True,post_start_delay=True)
            symbols[key]=stage.ABSOLUTE_ONE
            with patch.object(stage,'read_elf',return_value=(None,None,None,None,symbols)):
                with self.assertRaisesRegex(ValueError,'post-start delay marker differs'):
                    stage.check(build,None,None,maple_delay=True,submission_delay=True)


if __name__=='__main__':unittest.main()
