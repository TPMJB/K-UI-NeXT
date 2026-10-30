#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject ambiguous compact markers before any historical pin fallback."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import check_native_compact_background_profile as compact
import check_native_general_lean_profile as lean
from check_vmu_native_persistent import cached_submit_callback,retry_byte_postincrement_truth_cases,P2


def image(role):
    symbols={compact.MARK+role:compact.ONE,
             compact.GENERAL_MARKERS[role]:compact.ONE,
             '__kui_native_isolation_lease_bytes':(0x47000,0,0,0xfff1)}
    if role=='worker':
        symbols.update({compact.MARK+'worker_layout':compact.ONE,
                        '__kui_native_worker_span':(0x12000,0,0,0xfff1)})
    return (None,None,None,None,symbols)


class CompactProfile(unittest.TestCase):
    def test_retry_byte_postincrement_semantics_and_strict_aliases(self):
        self.assertEqual(retry_byte_postincrement_truth_cases(),4097)

    def test_exact_marked_roles_and_forged_markers(self):
        for role in compact.GENERAL_MARKERS:
            original=image(role)
            self.assertTrue(compact._profile(original,role))
            for name in (compact.MARK+role,compact.GENERAL_MARKERS[role],
                         '__kui_native_isolation_lease_bytes'):
                for value in (None,(2,0,0,0xfff1),(1,4,0,0xfff1),(1,0,0,1)):
                    symbols=dict(original[4])
                    if value is None:symbols.pop(name)
                    else:symbols[name]=value
                    with self.subTest(role=role,marker=name,value=value):
                        with self.assertRaises(ValueError):
                            compact._profile((*original[:4],symbols),role)

    def test_orphan_compact_geometry_cannot_be_historical(self):
        for role in compact.GENERAL_MARKERS:
            for symbols in ({'__kui_native_isolation_lease_bytes':(0x47000,0,0,0xfff1)},
                            {compact.MARK+'worker_layout':compact.ONE}):
                with self.assertRaises(ValueError):
                    compact._profile((None,None,None,None,symbols),role)
            self.assertFalse(compact._profile((None,None,None,None,{}),role))

    def test_worker_requires_both_exact_linker_contracts(self):
        original=image('worker')
        for name in (compact.MARK+'worker_layout','__kui_native_worker_span'):
            for value in (None,(0x1c000,0,0,0xfff1)):
                symbols=dict(original[4])
                if value is None:symbols.pop(name)
                else:symbols[name]=value
                with self.assertRaises(ValueError):
                    compact.worker_profile((*original[:4],symbols))
        for conflict in ('__kui_native_vmu_only_worker','__kui_native_vmu_lite_worker'):
            with self.assertRaises(ValueError):
                compact.worker_profile((*original[:4],{**original[4],conflict:compact.ONE}))

    def test_unreviewed_worker_role_never_borrows_general_pins(self):
        original=image('worker')
        for role in ('dual','live_boundary','prepared','read_ahead','toggle','witness'):
            self.assertIs(compact.worker_pins(original,role),compact.WORKER_PINS[role])
        with self.assertRaises(ValueError):
            compact.worker_pins(original,'unknown')

    def test_low_pins_select_the_actual_companion_role(self):
        worker=image('worker')
        resident=image('resident')
        resident[4].update({'__kui_vmu_native_background_resident':compact.ONE,
            **{name:(0x8c000100,4,2,1) for name in
               ('_kui_retail_release_100','_kui_retail_release_400','_kui_retail_release_600')}})
        for role in ('loan_quiesce','map_refusal'):
            for companion in (worker,resident):
                self.assertIs(compact.low_pins(companion,role),compact.LOW_PINS[role])
            self.assertIs(lean.low_pins(resident,role,{}),compact.LOW_PINS[role])
        for companion,marker in ((worker,compact.MARK+'worker'),
                                 (resident,compact.MARK+'resident')):
            symbols=dict(companion[4]);symbols.pop(marker)
            with self.assertRaises(ValueError):
                compact.low_pins((*companion[:4],symbols),'map_refusal')
        ambiguous=(*resident[:4],{**resident[4],compact.MARK+'worker':compact.ONE})
        with self.assertRaises(ValueError):
            compact.low_pins(ambiguous,'map_refusal')
        with self.assertRaises(ValueError):
            lean.low_pins(ambiguous,'map_refusal',{})

    def test_relocated_split_gateway_requires_compact_span(self):
        delta=0x00300000
        names=('__kui_native_general_lean_worker_layout',
               '__kui_native_split_controller_general_worker',
               '__kui_native_split_controller_isolation_worker',
               '__kui_native_general_unbounded_pairs_worker',
               '__kui_native_background_compact_worker_layout',
               '__kui_native_background_compact_worker')
        symbols={name:1+delta for name in names}
        symbols.update({'__kui_native_isolation_lease_bytes':0x47000+delta,
                        '__vmu_native_worker_stack_top':P2+0x12000+delta,
                        '_kui_vmu_native_passive_split_submit':0x8c1030c4+delta})
        self.assertEqual(cached_submit_callback(symbols,relocation_delta=delta),
                         '_kui_vmu_native_passive_split_submit')
        for name,value in (('__vmu_native_worker_stack_top',P2+0x1c000+delta),
                           ('__kui_native_isolation_lease_bytes',0x51000+delta),
                           ('__kui_native_background_compact_worker',2+delta),
                           ('__kui_native_vmu_lite_worker_layout',1+delta)):
            with self.assertRaises(ValueError):
                cached_submit_callback({**symbols,name:value},relocation_delta=delta)


if __name__=='__main__':unittest.main()
