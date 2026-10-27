#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Metadata checks plus optional tests against privately supplied owner files.

Set KUI_ARMADA_STARTUP_DIR or pass --startup-dir to run the source-dependent
cases. Owner bytes stay private; mutations use temporary copies or memory.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from armada_ce_maple_write_owner_audit import (
    CE_WAIT_ABI_METADATA, CE_WAIT_WINDOWS, COMPLETION_WINDOWS, IDENTITIES, SCI_WINDOWS, WINDOWS,
    ROMModuleView, Scalars, reviewed_completion_windows, reviewed_pe_windows, reviewed_sci_windows,
    reviewed_wait_abi, reviewed_wait_windows, source_audit,
)
from armada_ce_rom_audit import ROM
from armada_pe_audio_audit import PE

OWNER = Path(os.environ['KUI_ARMADA_STARTUP_DIR']) if os.environ.get('KUI_ARMADA_STARTUP_DIR') else None
METADATA = ROOT / 'docs/evidence/armada-ce-maple-write-owner.json'


class MetadataTests(unittest.TestCase):
    def test_checked_metadata_keeps_protocol_and_blockers_explicit(self):
        report = json.loads(METADATA.read_text())
        self.assertEqual(report['format'], 'armada-ce-maple-write-owner-contracts-v1')
        self.assertEqual(len(report['source_contracts']), 26)
        self.assertEqual(len(report['decoded_scalar_checks']), 85)
        wait=report['ce_wait_abi']
        self.assertEqual({key:wait[key] for key in CE_WAIT_ABI_METADATA}, CE_WAIT_ABI_METADATA)
        self.assertEqual(wait['caller_home_bytes'], 16)
        self.assertEqual(wait['callee_frame_bytes'], 8)
        self.assertEqual(wait['kernel_file_offset'], '0x00028f7c')
        self.assertEqual(wait['rom_storage_address'], '0x8c03877c')
        self.assertEqual(sorted(row['caller_sp_offset'] for row in wait['home_stores']), [0,4,8,12])
        self.assertEqual({row['source_register']:row['caller_sp_offset'] for row in wait['home_stores']},
                         {4:0,5:4,6:8,7:12})
        self.assertEqual(report['advertised_geometry'], {
            'normalized_function_definition': '0x000f4100',
            'dma_function_definition': '0x00410f00', 'block_bytes': 512,
            'read_phases': 1, 'write_phases': 4, 'write_phase_bytes': 128,
            'write_selector_phases': [0, 1, 2, 3], 'write_payload_words': 34,
            'write_request_bytes': 140, 'write_final_selector_phase': 4,
            'single_read_final_selector_phase': 0,
        })
        response = report['response_contract']
        self.assertEqual(response['write_phase_success']['receive_capacity_bytes'], 4)
        self.assertEqual(response['finalizer_success']['receive_capacity_bytes'], 8)
        self.assertFalse(response['deferred_commit_busy_is_transparent'])
        lifetime = report['pending_request_lifetime']
        self.assertFalse(lifetime['timeout_ioctl_cancels_original_request'])
        self.assertTrue(lifetime['successful_timeout_poke_repeats_wait'])
        self.assertFalse(lifetime['worker_critical_section_lease_proved'])
        epoch=report['completion_epoch_contract']
        self.assertTrue(epoch['status_query_before_consumer'])
        self.assertTrue(epoch['status_query_acknowledges_captured_raw_bits'])
        self.assertFalse(epoch['interrupt_done_clears_later_raw_status'])
        self.assertTrue(epoch['builder_zeros_receive_before_next_completion'])
        self.assertTrue(epoch['raw_completion_is_coalesced_status'])
        self.assertFalse(epoch['invalid_source_may_be_skipped'])
        self.assertIn('MDEN0',epoch['required_service_policy'])
        self.assertIn('interrupts unmasked',epoch['required_service_policy'])
        sci = report['sci_backpressure_contract']
        self.assertEqual(sci['request_commands'], [17, 38])
        self.assertEqual(sci['zero_request_token_error'], '0xc0000185')
        self.assertFalse(sci['zero_token_is_transparent_busy'])
        self.assertFalse(sci['completion_helper_reissues_zero_token_request'])
        self.assertTrue(sci['requires_loan_aware_gd_wait_or_deferral'])
        self.assertTrue(sci['requires_cdda_exclusion'])
        self.assertFalse(sci['hardware_storage_write_admitted'])
        windows = {row['id']: row for row in report['source_contracts']}
        self.assertEqual(windows['gd_read_submit_and_completion']['sha256'],
                         '1d4cded9d416a9d8a516727ebeacf8655dfa8d07eee010473e937a6125aa33ed')
        self.assertEqual(windows['gd_zero_token_is_io_error']['sha256'],
                         '91fd2f77cf9dca3a899038bd1f6a45e747a2d535715e87bed16ad9ea984f55b8')


class PrivateOwnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if OWNER is None:
            raise unittest.SkipTest('Set KUI_ARMADA_STARTUP_DIR or --startup-dir for private owner source tests')
        cls.actual = source_audit(OWNER)

    def pe(self):
        return PE(OWNER / 'WINCE/MAPLEDEV.DLL')

    def rom(self):
        return ROM(OWNER / '0WINCEOS.BIN')

    def test_exact_source_reproduces_all_checked_metadata(self):
        self.assertEqual(self.actual, json.loads(METADATA.read_text()))
        self.assertTrue(all(row['complete_stored_text_equal']
                            for row in self.actual['external_text_equivalence']))
        self.assertEqual([row['resolved_name'] for row in self.actual['resolved_coredll_imports']],
                         ['DeviceIoControl', 'WaitForSingleObject', 'GetLastError', 'GetTickCount',
                          'EnterCriticalSection', 'LeaveCriticalSection', 'KernelIoControl', 'WaitForSingleObject'])

    def test_every_exact_identity_and_size_refuses_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory)
            for relative, _, _ in IDENTITIES:
                target = copied / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(OWNER / relative, target)
            for relative, _, _ in IDENTITIES:
                target = copied / relative
                original = target.read_bytes()
                for kind in ('content', 'size'):
                    with self.subTest(file=relative, mutation=kind):
                        changed = bytearray(original)
                        if kind == 'content':
                            changed[len(changed) // 2] ^= 1
                        else:
                            changed.append(0)
                        target.write_bytes(changed)
                        with self.assertRaisesRegex(ValueError, 'exact-owner (SHA256|file size)'):
                            source_audit(copied)
                        target.write_bytes(original)

    def test_each_reviewed_mapledev_window_has_an_independent_hash_guard(self):
        for name, start, end, _, _ in WINDOWS:
            with self.subTest(window=name):
                pe = self.pe()
                changed = bytearray(pe.data)
                changed[pe.rva_offset(start + (end-start)//2)] ^= 1
                pe.data = bytes(changed)
                with self.assertRaisesRegex(ValueError, 'Reviewed write owner window ' + name):
                    reviewed_pe_windows(pe)

    def test_sci_and_completion_windows_have_independent_rom_hash_guards(self):
        for module, name, start, end, _, _, _ in SCI_WINDOWS+COMPLETION_WINDOWS:
            with self.subTest(window=name):
                rom = self.rom()
                at = rom.rva_address(rom.modules[module], start, end-start)
                changed = bytearray(rom.data)
                changed[rom.address_offset(at + (end-start)//2)] ^= 1
                rom.data = bytes(changed)
                if any(row[1]==name for row in SCI_WINDOWS):
                    with self.assertRaisesRegex(ValueError, 'Reviewed SCI owner window ' + name):
                        reviewed_sci_windows(rom)
                else:
                    with self.assertRaisesRegex(ValueError, 'Reviewed completion owner window ' + name):
                        reviewed_completion_windows(rom)

    def test_wait_windows_have_independent_rom_hash_guards(self):
        for name,start,end,_,_ in CE_WAIT_WINDOWS:
            with self.subTest(window=name):
                rom=self.rom();module=rom.modules['coredll.dll']
                at=rom.address_offset(rom.rva_address(module,start+(end-start)//2))
                changed=bytearray(rom.data);changed[at]^=1;rom.data=bytes(changed)
                with self.assertRaisesRegex(ValueError,'Reviewed CE wait owner window '+name):
                    reviewed_wait_windows(rom)

    def test_wait_export_mapping_and_exact_kernel_are_required(self):
        rom=self.rom();actual=reviewed_wait_abi(rom)
        self.assertEqual(actual['export_rva'],'0x0000877c')
        self.assertEqual(actual['kernel_file_offset'],'0x00028f7c')
        rom=self.rom();changed=bytearray(rom.data);changed[-1]^=1;rom.data=bytes(changed)
        with self.assertRaisesRegex(ValueError,'CE wait exact kernel hash'):
            reviewed_wait_abi(rom)
        for field,value in (('name','OtherWait'),('rva',0x8780),('rom_storage_address',0x8c01877c)):
            with self.subTest(export_field=field):
                rom=self.rom();original=rom.exports
                def exports(module):
                    return [dict(entry,**{field:value}) if entry['ordinal']==497 else entry
                            for entry in original(module)]
                rom.exports=exports
                with self.assertRaisesRegex(ValueError,'CE wait export'):
                    reviewed_wait_abi(rom)
        rom=self.rom();entries=rom.exports(rom.modules['coredll.dll'])
        rom.exports=lambda module:entries
        rom.modules['coredll.dll']['sections'][0]['data_address']-=0x1000
        with self.assertRaisesRegex(ValueError,'Reviewed CE wait owner storage address'):
            reviewed_wait_abi(rom)

    def test_wait_scalar_homes_refuse_register_offset_family_and_frame_changes(self):
        for kind in ('source','base','offset','family','push'):
            with self.subTest(mutation=kind):
                rom=self.rom();module=rom.modules['coredll.dll']
                at=0x877c if kind=='push' else 0x8782
                offset=rom.address_offset(rom.rva_address(module,at,2))
                word=struct.unpack_from('<H',rom.data,offset)[0]
                word^={'source':0x10,'base':0x100,'offset':1,'family':0x1000,'push':0x10}[kind]
                changed=bytearray(rom.data);struct.pack_into('<H',changed,offset,word);rom.data=bytes(changed)
                scalar=Scalars(ROMModuleView(rom,'coredll.dll'),'coredll.dll')
                with self.assertRaisesRegex(ValueError,'ce_wait_'):
                    if kind=='push':scalar.push('ce_wait_saved_r8',at,8)
                    else:scalar.long_store('ce_wait_home_r4',at,4,15,8)

    def test_scalar_decoder_refuses_capacity_register_and_family_changes(self):
        for kind in ('capacity', 'register', 'family'):
            with self.subTest(mutation=kind):
                pe = self.pe()
                at = pe.rva_offset(0x7f5a, 2)
                word = pe.u16(at)
                if kind == 'capacity':
                    word = (word & ~255) | 8
                elif kind == 'register':
                    word ^= 0x100
                else:
                    word ^= 0x1000
                changed = bytearray(pe.data)
                struct.pack_into('<H', changed, at, word)
                pe.data = bytes(changed)
                with self.assertRaisesRegex(ValueError, 'write_receive_capacity'):
                    Scalars(pe).immediate('write_receive_capacity', 0x7f5a, 3, 4)

    def test_scalar_decoder_refuses_retry_branch_and_finalizer_target_changes(self):
        pe = self.pe()
        at = pe.rva_offset(0x7cf2, 2)
        changed = bytearray(pe.data)
        changed[at] ^= 1
        pe.data = bytes(changed)
        with self.assertRaisesRegex(ValueError, 'whole_write_retry'):
            Scalars(pe).branch('whole_write_retry', 0x7cf2, 0x7cd4, 'false')
        pe = self.pe()
        word = pe.u16(pe.rva_offset(0x7fbe, 2))
        literal = ((0x7fbe+4) & ~3) + 4*(word & 255)
        changed = bytearray(pe.data)
        struct.pack_into('<I', changed, pe.rva_offset(literal, 4), pe.image_base + 0x42a4)
        pe.data = bytes(changed)
        with self.assertRaisesRegex(ValueError, 'write_finalizer'):
            Scalars(pe).literal('write_finalizer', 0x7fbe, 0x42a0, True)

    def test_rom_scalar_decoder_refuses_zero_token_error_and_token_flow_changes(self):
        for kind in ('error', 'token', 'branch'):
            with self.subTest(mutation=kind):
                rom = self.rom()
                module = rom.modules['wsegacd.dll']
                changed = bytearray(rom.data)
                rva = {'error': 0x1a08, 'token': 0x2dca, 'branch': 0x19c8}[kind]
                at = rom.address_offset(rom.rva_address(module, rva, 4 if kind == 'error' else 2))
                changed[at] ^= 1 if kind != 'token' else 0x10
                rom.data = bytes(changed)
                scalars = Scalars(ROMModuleView(rom, 'wsegacd.dll'), 'wsegacd.dll')
                with self.assertRaisesRegex(ValueError, 'gd_'):
                    if kind == 'error':
                        scalars.literal('gd_zero_token_error', 0x19ca, 0xc0000185)
                    elif kind == 'token':
                        scalars.register_move('gd_completion_token', 0x2dca, 10, 5)
                    else:
                        scalars.branch('gd_nonzero_token', 0x19c8, 0x19d0, 'false')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir', type=Path)
    args, remaining = parser.parse_known_args()
    if args.startup_dir is not None:
        OWNER = args.startup_dir
    unittest.main(argv=[sys.argv[0]] + remaining)
