#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject a mislabeled or calling submit path in completion-only proofs."""
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
    def test_completion_selection_requires_delay_before_artifact_access(self):
        with self.assertRaisesRegex(ValueError,'Completion delay requires Maple delay'):
            linked.run_variant(None,None,None,'reader_only',completion_delay=True)
        with self.assertRaisesRegex(ValueError,'Completion delay requires Maple delay'):
            stage.check(None,None,None,completion_delay=True)

    def test_submit_gateway_rejects_every_call_form(self):
        for op in (0x400b,0x430b,0x0003,0x0703,0xb000,0xb7ff):
            with self.subTest(op=hex(op)):
                cpu=linked.BypassWorker.__new__(linked.BypassWorker)
                cpu.bypass_running=True;cpu.completion_delay=True
                cpu.delay_active=None;cpu.delay_entries={};cpu.forbidden_maple_entries={}
                cpu.pc=0x8c100100;cpu.sd_entry=0x8c007000;cpu.tick_entry=None
                cpu.gateway_ranges={'submit':(cpu.pc,cpu.pc+2)}
                cpu.read=lambda address,count:op
                with self.assertRaisesRegex(ValueError,'submit gateway executed a helper call'):
                    cpu.step()

    def test_stage_marker_never_silently_selects_completion_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            build=Path(directory)
            for marker in (None,1,(0,0,0,0xfff1)):
                with self.subTest(marker=marker):
                    symbols={'__kui_native_maple_delay_isolation_stage':stage.ABSOLUTE_ONE}
                    if marker is not None:
                        symbols['__kui_native_maple_delay_complete_only_isolation_stage']=marker
                    with patch.object(stage,'read_elf',return_value=(None,None,None,None,symbols)):
                        with self.assertRaisesRegex(ValueError,'completion-only delay marker differs'):
                            stage.check(build,None,None,maple_delay=True,completion_delay=True)
            symbols={'__kui_native_maple_delay_isolation_stage':stage.ABSOLUTE_ONE,
                     '__kui_native_maple_delay_complete_only_isolation_stage':stage.ABSOLUTE_ONE}
            with patch.object(stage,'read_elf',return_value=(None,None,None,None,symbols)):
                with self.assertRaisesRegex(ValueError,'completion-only delay marker differs'):
                    stage.check(build,None,None,maple_delay=True)


if __name__=='__main__':unittest.main()
