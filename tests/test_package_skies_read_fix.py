#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-policy packaging rejects relabelled modes and stale geometry/source."""
import copy
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import package_skies_read_fix as package
COMMIT='a'*40
HASH='b'*64


def linked(mode):
    return dict(schema='kui.native.vmu-only.linked.v1',transport=1,
        lease_bytes=73728,worker_span=73728,variant='vmu-lite',controller_submit='split',
        controller_submit_audit={'matching_immutable_markers':True},
        sci_rx_policy=mode,sci_rx_policy_audit={'matching_immutable_markers':True},
        background_reader_enabled=False,prefetch_enabled=False,console_execution_verified=False,
        source_provenance={'commit':COMMIT,'source_dirty':False},runtime={'build':COMMIT[:12]},module_sha256=HASH)


class ReadFixPackageBindings(unittest.TestCase):
    def test_each_policy_requires_72k_split_and_its_exact_receive_policy(self):
        for mode in package.CONTRACTS:package.validate_audit(linked(mode),mode,COMMIT,HASH)

    def test_relabelled_receive_policies_refuse(self):
        for mode in package.CONTRACTS:
            for other in package.CONTRACTS:
                if mode==other:continue
                with self.subTest(mode=mode,other=other):
                    with self.assertRaises(ValueError):package.validate_audit(linked(other),mode,COMMIT,HASH)

    def test_wrong_geometry_controller_transport_or_async_machinery_refuse(self):
        for field,value in (('lease_bytes',114688),('worker_span',114688),('variant','vmu-only'),
            ('controller_submit','full'),('transport',0),('background_reader_enabled',True),
            ('prefetch_enabled',True),('console_execution_verified',True),('module_sha256','c'*64)):
            report=linked('paced');report[field]=value
            with self.subTest(field=field):
                with self.assertRaises(ValueError):package.validate_audit(report,'paced',COMMIT,HASH)

    def test_stale_source_or_runtime_build_refuses(self):
        for key,value in (('commit','c'*40),('source_dirty',True)):
            report=linked('paced');report['source_provenance'][key]=value
            with self.assertRaises(ValueError):package.validate_audit(report,'paced',COMMIT,HASH)
        report=linked('paced');report['runtime']['build']='c'*12
        with self.assertRaises(ValueError):package.validate_audit(report,'paced',COMMIT,HASH)

    def test_missing_or_mixed_controller_markers_refuse(self):
        report=linked('paced');report['controller_submit_audit']['matching_immutable_markers']=False
        with self.assertRaises(ValueError):package.validate_audit(report,'paced',COMMIT,HASH)

    def test_missing_or_mixed_receive_policy_markers_refuse(self):
        report=linked('paced');report['sci_rx_policy_audit']['matching_immutable_markers']=False
        with self.assertRaises(ValueError):package.validate_audit(report,'paced',COMMIT,HASH)
        report=linked('paced');del report['sci_rx_policy_audit']
        with self.assertRaises(ValueError):package.validate_audit(report,'paced',COMMIT,HASH)


if __name__=='__main__':unittest.main()
