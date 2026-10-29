#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep the terminal report out of transport/scheduling and unsupported builds."""
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


def flags():
    with tempfile.TemporaryDirectory(prefix='kui-stream-report-flags-') as temporary:
        directory=Path(temporary);output=directory/'flags'
        makefile=directory/'flags.mk'
        makefile.write_text('include '+str(ROOT/'Makefile.native-standalone-stream-report')+'\n'+
            'dump-stream-flags:\n\t$(file >'+str(output)+', $(RESIDENT_FLAGS))\n\t@true\n')
        subprocess.run(['make','--no-print-directory','-s','-f',str(makefile),'dump-stream-flags'],
                       cwd=ROOT,check=True)
        return [word for word in shlex.split(output.read_text()) if word.startswith(('-D','-I'))]


class StreamReportScopeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.enabled=flags()
        cls.disabled=[word for word in cls.enabled if word!='-DKUI_NATIVE_STREAM_REPORT=1']

    def preprocess(self,source,options):
        return subprocess.run(['cc','-E','-P',*options,source],cwd=ROOT,
                              text=True,capture_output=True)

    def test_transport_and_entry_sources_preprocess_identically(self):
        for source in ('src/loader/retail_async.c','src/loader/sci_stream.c',
                       'src/core/retail_cursor.c','src/loader/retail_resident.S'):
            with self.subTest(source=source):
                before=self.preprocess(source,self.disabled)
                after=self.preprocess(source,self.enabled)
                self.assertEqual(before.returncode,0,before.stderr)
                self.assertEqual(after.returncode,0,after.stderr)
                self.assertEqual(before.stdout,after.stdout)

    def test_standalone_scope_and_disabled_default(self):
        for options in (self.disabled,self.enabled):
            result=self.preprocess('src/loader/retail_resident.c',options)
            self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotIn('__kui_native_stream_report_resident',
                         self.preprocess('src/loader/retail_resident.c',self.disabled).stdout)

    def test_unsupported_profiles_refused(self):
        variants=(['-DKUI_NATIVE_RAM_MAP=1'],['-DKUI_RETAIL_CE=1'],
                  ['-UKUI_RETAIL_ASYNC'],['-UKUI_NATIVE_STANDALONE_READER'],
                  ['-DKUI_VMU_NATIVE_BACKGROUND=1'])
        for extra in variants:
            with self.subTest(extra=extra):
                result=self.preprocess('src/loader/retail_resident.c',self.enabled+extra)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('Native stream reporting requires the standalone native reader',result.stderr)


if __name__=='__main__':unittest.main()
