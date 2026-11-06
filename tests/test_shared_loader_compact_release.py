#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep the normal release on the current compact Background contract."""
import copy
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_shared_loader_performance import compact_background_binding, REQUIRED
from native_runtime_layout import BACKGROUND_COMPACT
from package_universal_performance_test import FUNCTIONAL_TARGETS, require_functional_targets

ONE=(1,0,0,0xfff1)


def components(slot=2):
    rows=[]
    for role,lean in (
        ('stage','__kui_native_general_lean_stage_layout'),
        ('worker','__kui_native_general_lean_worker_layout'),
        ('resident','__kui_native_general_lean_resident_layout'),
        ('resident','__kui_native_general_lean_resident_layout'),
        ('auxiliary','__kui_native_general_lean_auxiliary')):
        symbols={'__kui_native_background_compact_'+role:ONE,lean:ONE,
                 '__kui_native_isolation_lease_bytes':(0x47000,0,0,0xfff1)}
        if role=='worker':
            symbols['__kui_native_background_compact_worker_layout']=ONE
        if role in ('stage','worker'):
            symbols['__kui_native_virtual_slot_'+role]=(slot,0,0,0xfff1)
        rows.append((b'',None,[],[],symbols))
    return rows


class CompactRelease(unittest.TestCase):
    def test_both_slots_keep_complete_cache_and_both_readers(self):
        for slot in (1,2):
            proof=compact_background_binding(*components(slot),slot)
            self.assertEqual((proof['worker_bytes'],proof['lease_bytes'],proof['cache_bytes'],
                              proof['worker_stack_bytes']),
                             (73728,290816,196608,3072))
            self.assertEqual(proof['readers'],['new_x','legacy_y'])
            self.assertEqual(proof['virtual_slot'],slot)
            self.assertFalse(proof['optional_session_controls'])
            self.assertEqual(proof['component_markers']['components'],5)

    def test_historical_full_general_cannot_be_the_default(self):
        images=components()
        for image in images:
            symbols=image[4]
            for name in tuple(symbols):
                if name.startswith('__kui_native_background_compact_'):del symbols[name]
            symbols['__kui_native_isolation_lease_bytes']=(0x51000,0,0,0xfff1)
        with self.assertRaises(ValueError):compact_background_binding(*images,2)

    def test_every_component_requires_its_exact_compact_contract(self):
        roles=('stage','worker','resident','resident','auxiliary')
        for index,role in enumerate(roles):
            for name,value in (
                ('__kui_native_background_compact_'+role,None),
                ('__kui_native_background_compact_'+role,(2,0,0,0xfff1)),
                ('__kui_native_isolation_lease_bytes',(0x51000,0,0,0xfff1)),
                ('__kui_native_session_controls_worker',ONE),
                ('__native_session_descriptor',(0x8ce01000,18432,1,2)),
                ('_kui_retail_native_session_control',(0x8c004400,16,2,1))):
                with self.subTest(component=index,name=name,value=value):
                    images=copy.deepcopy(components())
                    if value is None:del images[index][4][name]
                    else:images[index][4][name]=value
                    with self.assertRaises(ValueError):compact_background_binding(*images,2)

    def test_stage_and_worker_slots_must_match_the_install_path(self):
        for requested in (0,1,3):
            with self.assertRaises(ValueError):compact_background_binding(*components(),requested)
        for index,role in ((0,'stage'),(1,'worker')):
            images=components()
            images[index][4]['__kui_native_virtual_slot_'+role]=(1,0,0,0xfff1)
            with self.assertRaises(ValueError):compact_background_binding(*images,2)

    def test_worker_and_cache_oracle_cannot_silently_expand_or_shrink(self):
        for name,value in (('WORKER_BYTES',0x1c000),('LEASE_BYTES',0x51000),
                           ('DATA_BYTES',0x20000),('WORKER_STACK_BYTES',0x800)):
            with self.subTest(name=name),mock.patch.object(BACKGROUND_COMPACT,name,value):
                with self.assertRaises(ValueError):compact_background_binding(*components(),2)

    def test_exact_eight_file_cohort_and_no_optional_larger_module(self):
        expected={
            'KUI/runtime.kui','KUI/apps/games/retail-boot.kui',
            'KUI/apps/games/retail-native-dual.kui','KUI/apps/games/retail-native-dual-a1.kui',
            'KUI/apps/disc/native-disc-vmu.kui','KUI/apps/disc/native-disc-vmu-a1.kui',
            'KUI/apps/games/retail-smash-menu.kui','KUI/apps/games/retail-smash-native-menu.kui',
        }
        self.assertEqual(FUNCTIONAL_TARGETS,expected)
        self.assertEqual(set(require_functional_targets({name:b'' for name in expected})),expected)
        self.assertEqual(set(require_functional_targets({name:b'' for name in expected},focused=True)),expected)
        optional={name:b'' for name in expected}
        optional['Optional-timing-stop/KUI/apps/games/retail-smash-menu.kui']=b''
        require_functional_targets(optional)
        with self.assertRaises(ValueError):require_functional_targets(optional,focused=True)
        for name in ('KUI/apps/games/retail-native-session.kui',
                     'KUI/apps/games/retail-native-session-a1.kui',
                     'OPTIONAL-CONTROLS/KUI/apps/games/retail-native-dual.kui'):
            with self.subTest(name=name):
                files={target:b'' for target in expected};files[name]=b''
                with self.assertRaises(ValueError):require_functional_targets(files)
        files={target:b'' for target in expected}
        files.pop('KUI/apps/games/retail-native-dual.kui')
        files['KUI/apps/games/retail-native-session.kui']=b''
        with self.assertRaises(ValueError):require_functional_targets(files)

    def test_required_families_do_not_force_historical_general_or_session(self):
        self.assertEqual(REQUIRED,{'standard','background_a2','background_a1','disc_a2','disc_a1'})


if __name__=='__main__':unittest.main()
