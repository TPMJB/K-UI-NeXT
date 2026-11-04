#!/usr/bin/env python3
"""Exercise the actual C serializer and reject incomplete/mismatched TXT files."""
import os
from pathlib import Path
import subprocess
import sys
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from analyze_toy_report import decode,BODY,SIZE

class TextReport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        out=ROOT/'build/toy-text-host';out.mkdir(parents=True,exist_ok=True)
        cls.exe=out/'test'
        subprocess.run(['cc','-D_GNU_SOURCE','-DKUI_TOY_DECISION_PROFILE=2',
            '-DKUI_TOY_PILOT_DATA_PIPELINE_PROFILE=2','-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE=2',
            '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE=4','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-pie','-no-pie','-Iinclude',
            'tests/test_toy_report_text.c','src/loader/toy_report_text.c','-o',str(cls.exe)],cwd=ROOT,check=True)
        cls.env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'}
        cls.text=subprocess.check_output([str(cls.exe),'--fixture'],cwd=ROOT,env=cls.env)
    @classmethod
    def file(cls,text=None,profile=2):
        text=cls.text if text is None else text
        footer=('KUI REPORT COMPLETE V1\nPROFILE=%08X\nBYTES=%08X\nCRC32=%08X\n'%
            (profile,len(text),zlib.crc32(text))).encode().ljust(512,b' ')
        return text.ljust(BODY,b' ')+footer
    def test_actual_c_chunks_and_immutability(self):
        subprocess.run([str(self.exe)],cwd=ROOT,env=self.env,check=True)
    def test_c_text_to_complete_file(self):
        result=decode(self.file())
        self.assertEqual(result['build'],'0123456789ab');self.assertEqual(result['profile'],'TC')
        self.assertEqual(len(result['all_raw_bins']),8);self.assertTrue(result['footer_crc_verified'])
    def test_stale_partial_and_corrupt(self):
        good=self.file()
        for bad in (good[:-1],good+b' ',good[:BODY]+b' '*512,b'X'+good[1:],
                    good[:BODY-1]+b'X'+good[BODY:],self.file(profile=1)):
            with self.assertRaises(ValueError):decode(bad)
    def test_even_checksummed_bad_schema_refused(self):
        for old,new in ((b'format=KUI_TOY_REPORT_V1',b'format=unknown'),
                        (b'build=0123456789ab',b'build=9739EB693B02'),
                        (b'radix=16',b'radix=10'),(b'report_fields=magic',b'report_fields=other')):
            with self.assertRaises(ValueError):decode(self.file(self.text.replace(old,new)))

if __name__=='__main__':unittest.main()
