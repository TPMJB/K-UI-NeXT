#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compact package bindings reject stale/mixed source, geometry and reader proofs."""
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import package_skies_background72 as package
from test_package_skies_read_fix import linked
COMMIT='a'*40
HASH='b'*64
TOGGLE={'stage':{'_stage_toggle':dict(bytes=16,sha256='c'*64)},
        'worker':{'_worker_toggle':dict(bytes=32,sha256='d'*64)}}


def compact():
    return dict(schema='kui.native.background.compact.audit.v1',
        source_provenance={'commit':COMMIT,'source_dirty':False},runtime={'build':COMMIT[:12]},
        module_sha256=HASH,transport=1,geometry={'worker_bytes':73728,'lease_bytes':290816,'cache_bytes':196608,'worker_stack_bytes':4096},
        console_execution_verified=False,matching_immutable_component_markers=True,
        background_reader_enabled=True,prefetch_enabled=True,controller_submit='split',
        receive_engine='background-async-dma',prefetch_mode='auto',standard_rx_feeder_absent=True,
        emulated_vmu_toggle=True,reader_only_retains_complete_lease=True,exact_toggle_kernels=copy.deepcopy(TOGGLE),
        proofs={'persistent':dict(module_sha256=HASH,
            source_provenance={'commit':COMMIT,'source_dirty':False},runtime={'build':COMMIT[:12]},
            worker_span=73728,worker_stack_bytes=4096,stage_embedded_worker_verified=True,
            maple_interception_installed=True,scoped_sci_transactions=True,console_execution_verified=False,
            read_ahead={'enabled':True,'paired_markers':True,'dual_reader':{'enabled':True,
                'compact_background':{'matching_immutable_component_markers':True}}})})


class Background72PackageBindings(unittest.TestCase):
    def setUp(self):
        self.expected = patch.object(package,'expected_toggle_kernels',return_value=copy.deepcopy(TOGGLE))
        self.expected.start();self.addCleanup(self.expected.stop)

    def test_standalone_requires_same_paced_72k_split_proof(self):
        package.validate_standalone(linked('paced'),COMMIT,HASH)
        for policy in ('pio','unpaced'):
            with self.assertRaises(ValueError):package.validate_standalone(linked(policy),COMMIT,HASH)

    def test_background_requires_complete_compact_async_geometry(self):
        package.validate_background(compact(),COMMIT,HASH)
        for name,value in (('worker_bytes',114688),('lease_bytes',331776),('cache_bytes',65536)):
            report=compact();report['geometry'][name]=value
            with self.subTest(name=name):
                with self.assertRaises(ValueError):package.validate_background(report,COMMIT,HASH)

    def test_renamed_standalone_or_disabled_background_proof_refuses(self):
        for name,value in (('schema','kui.native.vmu-only.linked.v1'),('matching_immutable_component_markers',False),
            ('background_reader_enabled',False),('prefetch_enabled',False),('controller_submit','full'),
            ('receive_engine','standard-paced-dma'),('standard_rx_feeder_absent',False),
            ('emulated_vmu_toggle',False),('reader_only_retains_complete_lease',False),
            ('prefetch_mode','eager'),('transport',0),('console_execution_verified',True)):
            report=compact();report[name]=value
            with self.subTest(name=name):
                with self.assertRaises(ValueError):package.validate_background(report,COMMIT,HASH)

    def test_source_build_or_module_drift_refuses(self):
        for section,key,value in (('source_provenance','commit','c'*40),('source_provenance','source_dirty',True),
                                  ('runtime','build','c'*12)):
            report=compact();report[section][key]=value
            with self.assertRaises(ValueError):package.validate_background(report,COMMIT,HASH)
        report=compact();report['module_sha256']='c'*64
        with self.assertRaises(ValueError):package.validate_background(report,COMMIT,HASH)

    def test_partial_persistent_or_dual_companion_refuses(self):
        report=compact();report['proofs']['persistent'].pop('scoped_sci_transactions')
        with self.assertRaises(ValueError):package.validate_background(report,COMMIT,HASH)
        report=compact();report['proofs']['persistent']['read_ahead']['dual_reader']['enabled']=False
        with self.assertRaises(ValueError):package.validate_background(report,COMMIT,HASH)

    def test_missing_or_changed_exact_toggle_kernel_refuses(self):
        for role in ('stage','worker'):
            report=compact();report['exact_toggle_kernels'][role]={}
            with self.assertRaises(ValueError):package.validate_background(report,COMMIT,HASH)
            report=compact();row=next(iter(report['exact_toggle_kernels'][role].values()));row['sha256']='e'*64
            with self.assertRaises(ValueError):package.validate_background(report,COMMIT,HASH)

    def test_geometry_metadata_does_not_label_background_as_standard_receive(self):
        self.assertEqual(package.CONTRACTS['background']['receive_engine'],'background-async-dma')
        self.assertEqual(package.CONTRACTS['standalone']['receive_engine'],'standard-paced-dma')
        self.assertNotIn('sci_rx_policy',package.CONTRACTS['background'])
        self.assertEqual(package.CONTRACTS['background']['worker_bytes'],package.CONTRACTS['standalone']['worker_bytes'])

    def test_gui_compact_proof_binds_extra_log_and_actual_filesystem_case_count(self):
        logs={name:name.encode() for name in ('native_cache','standard_filesystem','dual_filesystem','compact_background_filesystem')}
        evidence=dict(log_sha256={name:package.sha(data) for name,data in logs.items()},
                      checks={'compact_background_filesystem_cases':46})
        with patch.object(package,'validate_gui_evidence') as prior:
            package.validate_compact_gui_evidence(evidence,COMMIT,{},logs)
            self.assertEqual(set(prior.call_args.args[3]),{'native_cache','standard_filesystem','dual_filesystem'})
            self.assertEqual(set(prior.call_args.args[0]['log_sha256']),set(prior.call_args.args[3]))
        for count in (None,True,0,45):
            changed=copy.deepcopy(evidence);changed['checks']['compact_background_filesystem_cases']=count
            with self.assertRaises(ValueError):package.validate_compact_gui_evidence(changed,COMMIT,{},logs)
        changed=copy.deepcopy(evidence);changed['log_sha256']['compact_background_filesystem']='e'*64
        with self.assertRaises(ValueError):package.validate_compact_gui_evidence(changed,COMMIT,{},logs)

    def test_guide_has_both_separate_serial_builds_and_no_gui_as_loader(self):
        source=package.source_guide(COMMIT).decode()
        self.assertIn('Makefile.vmu-native-vmu-lite-paced -j1',source)
        self.assertIn('Makefile.vmu-native-background-compact -j1',source)
        self.assertEqual(source.count('make -C restored-source'),2)
        self.assertIn('Background',source)


if __name__=='__main__':unittest.main()
