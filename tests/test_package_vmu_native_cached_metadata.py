#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cached metadata delivery requires bound cache and real SDK execution proofs."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import package_vmu_native_isolation_delivery as delivery

COMMIT = 'a'*40
IMPLEMENTATION = 'src/loader/vmu_native_metadata_cache.inc'
AUDITOR = 'tools/check_native_metadata_cache.py'


def changed(report, path, value):
    result = copy.deepcopy(report)
    item = result
    for key in path[:-1]:
        item = item[key]
    item[path[-1]] = value
    return result


class CachedMetadataProofTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name)
        artifacts = {}
        for name in ('entry.elf', 'stage.elf', 'native-worker.elf',
                     'native-worker.bin', 'native-worker.reloc'):
            data = ('host fixture '+name).encode()
            (self.build/name).write_bytes(data)
            artifacts[name] = delivery.sha(data)
        layout={};address=0xac100000
        for name,size in (('config',2304),('live',456),('lifetime',616),('hooks',384)):
            padded=(size+31)&~31
            layout[name]=dict(canonical_p2=hex(address),cached_p1=hex(address-0x20000000),
                p2_begin=hex(address),p2_end=hex(address+padded),object_bytes=size,
                padded_bytes=padded,cache_lines=padded//32)
            address+=padded
        self.publication = dict(schema='kui.native.metadata-cache-publication.v1',
            status='PASS', source_commit=COMMIT, paired_markers=True,
            metadata_layout=layout, whole_cache_line_isolation=True,
            read_only_p1_views=True, no_p1_metadata_stores=True, coherent_live_witnesses=True,
            cache_publication=dict(ocbi_only=True, no_ocbp=True, exact_range=True,
                ready_after_all_invalidations=True, published_after_live_prepare=True),
            artifact_sha256=artifacts,
            source_sha256={name: delivery.sha((ROOT/name).read_bytes()) for name in (IMPLEMENTATION,AUDITOR)})
        self.execution = dict(variant='metadata_cache', cached_metadata=True, virtual_a2=True,
            maple_c_callbacks_enabled=True, controller_tick_off=True, tick_calls=0,
            maple_bypass=False, maple_delay=False, e1_wait_capture=False,
            actual_maple_c_callbacks=dict(submit=8, complete=8), natural_rounds=[{} for _ in range(8)],
            final_overlay=dict(advertisements=8, backend_calls=8, devinfo=8),
            forced_packets=[dict(command=command, reply=hex(reply), overlay=dict(backend_calls=1),
                                 storage_boundary_reached=False)
                            for command, reply in ((1, 5), (2, 6), (10, 8))]+[
                dict(command=11, storage_boundary_reached=True, success_or_ack_substituted=False,
                     rx_unpublished=True, calls=dict(actual_low_sd_loan_boundary=1))],
            sd_loan_boundary=dict(reader='New (X)'), worker_helpers_substituted=False,
            original_game_or_sdk_functions_substituted=False)

    def publication_check(self, report):
        delivery.validate_cached_metadata_publication(report, COMMIT, self.build)

    def test_bound_publication_and_execution_receipts_pass(self):
        self.publication_check(self.publication)
        delivery.validate_cached_metadata_execution(self.execution)

    def test_publication_requires_typed_cache_contract(self):
        paths = [(key,) for key in ('paired_markers', 'whole_cache_line_isolation',
            'read_only_p1_views', 'no_p1_metadata_stores', 'coherent_live_witnesses')]
        paths += [('cache_publication', key) for key in ('ocbi_only', 'no_ocbp', 'exact_range',
            'ready_after_all_invalidations', 'published_after_live_prepare')]
        for path in paths:
            for value in (False, 1, None):
                with self.subTest(path=path, value=value):
                    with self.assertRaisesRegex(ValueError, 'cache contract'):
                        self.publication_check(changed(self.publication, path, value))
        for value in (None, {}, 1, 'yes', []):
            with self.subTest(metadata_layout=value):
                with self.assertRaisesRegex(ValueError, 'cache contract'):
                    self.publication_check(changed(self.publication, ('metadata_layout',), value))
        for path,value in ((('metadata_layout','config','p2_end'),'0xac100001'),
                           (('metadata_layout','config','object_bytes'),True),
                           (('metadata_layout','config','cached_p1'),'0xac100000')):
            with self.subTest(path=path):
                with self.assertRaises(ValueError):
                    self.publication_check(changed(self.publication,path,value))

    def test_publication_is_bound_to_artifacts_and_implementation(self):
        cases = [changed(self.publication, ('source_commit',), 'b'*40),
                 changed(self.publication, ('artifact_sha256', 'stage.elf'), '0'*64),
                 changed(self.publication, ('source_sha256', IMPLEMENTATION), '0'*64),
                 changed(self.publication, ('source_sha256',), {}),
                 changed(self.publication, ('source_sha256',),
                         {**self.publication['source_sha256'], '../outside': '0'*64})]
        for report in cases:
            with self.subTest(report=report):
                with self.assertRaises(ValueError):
                    self.publication_check(report)
        (self.build/'native-worker.bin').write_bytes(b'changed after report')
        with self.assertRaisesRegex(ValueError, 'final artifacts'):
            self.publication_check(self.publication)

    def test_execution_requires_real_callbacks_and_virtual_metadata(self):
        mutations = [(('virtual_a2',), False), (('cached_metadata',), 1),
            (('maple_c_callbacks_enabled',), 1), (('controller_tick_off',), False),
            (('maple_bypass',), True), (('maple_delay',), True), (('tick_calls',), 1),
            (('actual_maple_c_callbacks', 'complete'), 0), (('natural_rounds',), [{}]*7),
            (('final_overlay', 'advertisements'), 0),
            (('forced_packets', 1, 'command'), 1),
            (('forced_packets', 1, 'reply'), '0x5'),
            (('forced_packets', 2, 'overlay', 'backend_calls'), 0),
            (('worker_helpers_substituted',), True),
            (('original_game_or_sdk_functions_substituted',), True)]
        for path, value in mutations:
            with self.subTest(path=path, value=value):
                with self.assertRaisesRegex(ValueError, 'real callbacks, VMU metadata'):
                    delivery.validate_cached_metadata_execution(changed(self.execution, path, value))

    def test_read_stops_at_real_new_storage_boundary_without_fake_reply(self):
        mutations = [(('forced_packets', 3, 'storage_boundary_reached'), False),
            (('forced_packets', 3, 'success_or_ack_substituted'), True),
            (('forced_packets', 3, 'rx_unpublished'), False),
            (('forced_packets', 3, 'calls', 'actual_low_sd_loan_boundary'), 0),
            (('sd_loan_boundary', 'reader'), 'Standard')]
        for path, value in mutations:
            with self.subTest(path=path, value=value):
                with self.assertRaisesRegex(ValueError, 'New SD-loan boundary'):
                    delivery.validate_cached_metadata_execution(changed(self.execution, path, value))


if __name__ == '__main__':
    unittest.main()
