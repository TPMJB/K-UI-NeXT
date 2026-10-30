#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject swapped geometries, stale source bindings and incomplete GUI proofs."""
import copy
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import package_skies_next as package
COMMIT='a'*40
HASH='b'*64


def linked(mode):
    geometry=package.CONTRACTS[mode]
    return dict(schema='kui.native.vmu-only.linked.v1',transport=1,
        lease_bytes=geometry['lease_bytes'],worker_span=geometry['lease_bytes'],
        variant='vmu-lite' if geometry['lease_bytes']==0x12000 else 'vmu-only',controller_submit=geometry['controller_submit'],
        controller_submit_audit={'matching_immutable_markers':True},
        background_reader_enabled=False,prefetch_enabled=False,console_execution_verified=False,
        source_provenance={'commit':COMMIT,'source_dirty':False},runtime={'build':COMMIT[:12]},module_sha256=HASH)


def gui_fixture():
    files={name:('test '+name).encode() for name in ('kui-runtime.elf','runtime.kui','kui-runtime.compile.json')}
    logs={name:(name+' checks pass').encode() for name in ('native_cache','standard_filesystem','dual_filesystem')}
    checks={name:True for name in ('one_sector_io_preserved','global_vmu_off_saved_and_preserved',
        'independent_exact_tail_crc_verified','report_and_manifest_crc_verified',
        'partial_source_requires_complete_crc_fallback','cancellation_refuses_handoff',
        'malformed_absent_duplicate_seed_keep_direct_crc','scif_keeps_direct_crc')}
    checks.update(native_cache_asan_ubsan_checks=39135,standard_fat32_exfat_cases=20,existing_dual_fat32_exfat_cases=46)
    evidence=dict(schema='kui.native.standard.prelaunch-crc.evidence.v1',source_commit=COMMIT,
        build=COMMIT[:12],source_dirty=False,all_runtime_objects_rebuilt_at_commit=True,
        elf_sha256=package.sha(files['kui-runtime.elf']),module_sha256=package.sha(files['runtime.kui']),
        compile_provenance_sha256=package.sha(files['kui-runtime.compile.json']),checks=checks,
        log_sha256={name:package.sha(data) for name,data in logs.items()},
        scope=dict(gui_only=True,new_buffer_allocations=0,prefetch_cache_added=False,
            manifest_wire_changed=False,detached_loader_changed=False,runtime_vmu_changed=False,
            sha256_and_detached_crc_and_ownership_checks_retained=True,
            console_execution_verified=False,ingame_freeze_fixed=False))
    return evidence,files,logs


class NextPackageBindings(unittest.TestCase):
    def test_each_exact_mode_geometry_is_accepted(self):
        for mode in package.CONTRACTS:package.validate_audit(linked(mode),mode,COMMIT,HASH)
    def test_relabelled_72k_or_wrong_controller_rejected(self):
        for mode,report in (('baseline',linked('reference-112')),('reference-112',linked('baseline')),
                            ('baseline',linked('full-submit')),('full-submit',linked('baseline'))):
            with self.subTest(mode=mode):
                with self.assertRaises(ValueError):package.validate_audit(report,mode,COMMIT,HASH)
    def test_dirty_stale_unbound_or_async_audits_refuse(self):
        for field,value in (('prefetch_enabled',True),('background_reader_enabled',True),
            ('worker_span',0x1c000),('module_sha256','c'*64),('console_execution_verified',True)):
            report=linked('baseline');report[field]=value
            with self.subTest(field=field):
                with self.assertRaises(ValueError):package.validate_audit(report,'baseline',COMMIT,HASH)
        for field,value in (('commit','c'*40),('source_dirty',True)):
            report=linked('baseline');report['source_provenance'][field]=value
            with self.assertRaises(ValueError):package.validate_audit(report,'baseline',COMMIT,HASH)
        report=linked('baseline');report['controller_submit_audit']['matching_immutable_markers']=False
        with self.assertRaises(ValueError):package.validate_audit(report,'baseline',COMMIT,HASH)
    def test_complete_current_gui_proof_is_accepted(self):
        evidence,files,logs=gui_fixture();package.validate_gui_evidence(evidence,COMMIT,files,logs)
    def test_gui_hashes_alone_do_not_authorize_missing_failed_checks(self):
        evidence,files,logs=gui_fixture()
        for field,value in (('source_dirty',True),('all_runtime_objects_rebuilt_at_commit',False),
                            ('schema','other'),('build','c'*12)):
            changed=copy.deepcopy(evidence);changed[field]=value
            with self.assertRaises(ValueError):package.validate_gui_evidence(changed,COMMIT,files,logs)
        changed=copy.deepcopy(evidence);changed['checks']['scif_keeps_direct_crc']=False
        with self.assertRaises(ValueError):package.validate_gui_evidence(changed,COMMIT,files,logs)
        changed=copy.deepcopy(evidence);changed['checks']['standard_fat32_exfat_cases']=0
        with self.assertRaises(ValueError):package.validate_gui_evidence(changed,COMMIT,files,logs)
        changed=copy.deepcopy(evidence);changed['scope']['sha256_and_detached_crc_and_ownership_checks_retained']=False
        with self.assertRaises(ValueError):package.validate_gui_evidence(changed,COMMIT,files,logs)
    def test_changed_or_missing_gui_log_is_rejected(self):
        evidence,files,logs=gui_fixture();logs['native_cache']+=b'changed'
        with self.assertRaises(ValueError):package.validate_gui_evidence(evidence,COMMIT,files,logs)
        evidence,files,logs=gui_fixture();del logs['dual_filesystem']
        with self.assertRaises(ValueError):package.validate_gui_evidence(evidence,COMMIT,files,logs)


if __name__=='__main__':unittest.main()
