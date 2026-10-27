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

    def test_undelayed_gateway_rejects_every_call_form(self):
        for side,role in (('completion','submit'),('submission','complete')):
            for op in (0x400b,0x430b,0x0003,0x0703,0xb000,0xb7ff):
                with self.subTest(side=side,op=hex(op)):
                    cpu=linked.BypassWorker.__new__(linked.BypassWorker)
                    cpu.bypass_running=True;cpu.completion_delay=side=='completion'
                    cpu.submission_delay=side=='submission'
                    cpu.delay_active=None;cpu.delay_entries={};cpu.forbidden_maple_entries={}
                    cpu.pc=0x8c100100;cpu.sd_entry=0x8c007000;cpu.tick_entry=None
                    cpu.gateway_ranges={role:(cpu.pc,cpu.pc+2)}
                    cpu.read=lambda address,count:op
                    with self.assertRaisesRegex(ValueError,role+' gateway executed a helper call'):
                        cpu.step()

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


if __name__=='__main__':unittest.main()
