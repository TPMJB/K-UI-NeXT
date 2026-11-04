#!/usr/bin/env python3
"""Real bounded report ownership and terminal sink, independent sparse media."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ReportFileTests(unittest.TestCase):
    def test_actual_admission_and_sink(self):
        with tempfile.TemporaryDirectory(prefix='kui-report-file-') as tmp:
            for fixture in ('exfat','fat32'):
                output=Path(tmp)/fixture
                command=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                    '-ffunction-sections','-fdata-sections','-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-Iinclude','-Isrc/loader',f'tests/test_report_file_admission_{fixture}.c',
                    'src/core/report_file_admission.c','src/core/report_file.c',
                    'src/core/vmu_fs_admission.c','src/core/vmu_persist_profile.c','src/core/hash.c',
                    '-Wl,--gc-sections','-o',str(output)]
                subprocess.run(command,cwd=ROOT,check=True,capture_output=True,text=True)
                env=dict(os.environ);env['ASAN_OPTIONS']='detect_leaks=0'
                result=subprocess.run([str(output)],cwd=ROOT,env=env,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('contracts OK',result.stdout)
if __name__=='__main__':unittest.main()
