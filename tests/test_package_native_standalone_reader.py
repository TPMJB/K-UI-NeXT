#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse fit-only, stale and incorrectly described standalone evidence."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import package_native_standalone_reader as package


class ExecutionBinding(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.build = Path(self.directory.name)
        self.commit = '1'*40
        artifacts = {}
        for name in ('entry.elf', 'stage.elf', 'resident-sci.elf'):
            data = name.encode()
            (self.build/name).write_bytes(data)
            artifacts[name] = package.sha(data)
        source = 'tools/package_native_standalone_reader.py'
        self.report = dict(schema='kui.native.standalone-reader.execution.v1', status='PASS',
            source_commit=self.commit, launchable=True, standalone_reader=True,
            virtual_vmu=False, readahead=False, heap_reservation_bytes=0,
            artifact_sha256=artifacts,
            source_sha256={source: package.sha((ROOT/source).read_bytes())})

    def test_bound_evidence_accepts(self):
        package.validate_execution(self.report, self.build, self.commit)

    def test_fit_only_never_authorizes_module(self):
        report = copy.deepcopy(self.report)
        report.update(schema='kui.native.standalone-reader-fit.v1', launchable=False)
        with self.assertRaisesRegex(ValueError, 'fit-only'):
            package.validate_execution(report, self.build, self.commit)

    def test_stale_artifact_refuses(self):
        (self.build/'resident-sci.elf').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'final artifacts'):
            package.validate_execution(self.report, self.build, self.commit)

    def test_changed_source_refuses(self):
        self.report['source_sha256']['tools/package_native_standalone_reader.py'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'source changed'):
            package.validate_execution(self.report, self.build, self.commit)

    def test_false_runtime_properties_refuse(self):
        for key, value in (('source_commit', '2'*40), ('launchable', False),
                           ('standalone_reader', False), ('virtual_vmu', True),
                           ('readahead', True), ('heap_reservation_bytes', 393216)):
            with self.subTest(key=key):
                report = {**self.report, key: value}
                with self.assertRaises(ValueError):
                    package.validate_execution(report, self.build, self.commit)


if __name__ == '__main__':
    unittest.main()
