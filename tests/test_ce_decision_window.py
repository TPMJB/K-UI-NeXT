#!/usr/bin/env python3
"""Focused CE decision observer: actual collector and rolling-window contracts."""
import unittest
import os
import re
import subprocess
from pathlib import Path
from test_retail_demand_diagnostic import DemandDiagnosticTests
from test_retail_demand_window import DemandWindowTests

class AcceptedPathProjectionTests(unittest.TestCase):
    def test_ce_history_retained_in_reference_and_observer(self):
        root=Path(__file__).resolve().parents[1]
        base=[os.environ.get('CC','cc'),'-E','-P','-Iinclude','-Isrc/loader',
            '-DKUI_RETAIL_CE=1','-DKUI_RETAIL_ASYNC=1','-DKUI_ARMADA_CE_STREAM=1',
            '-DKUI_ARMADA_CE_AUDIO_OBSERVE=1','-DKUI_ARMADA_CE_EXCERPT=1',
            '-DKUI_RETAIL_TRANSPORT=1','-DKUI_RETAIL_GD_ASYNC=1',
            '-DKUI_RETAIL_MANIFEST_SLOTS=64','-DKUI_SCI_SD_NO_BLOCK=1']
        functions=[]
        for profile in (0,3):
            source=subprocess.check_output([*base,f'-DKUI_RETAIL_DEMAND_DIAGNOSTIC={profile}',
                'src/loader/retail_resident.c'],cwd=root,text=True)
            found=re.search(r'static int ce_record\([^)]*\)\s*\{([^}]*)\}',source)
            self.assertIsNotNone(found)
            functions.append(found.group(0))
        self.assertEqual(functions[0],functions[1])
        self.assertIn('ce_calls[ce_count++&3u]',functions[0])
        self.assertIn('call[3]=r6;',functions[0])

if __name__=='__main__':
    unittest.main()
