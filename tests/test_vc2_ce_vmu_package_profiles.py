#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Adversarial mode and seed binding tests, independent of console acceptance."""
import struct
import sys
from pathlib import Path
import unittest
import zlib

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from check_vc2_ce_vmu_package import (persistent_profile,persistent_call_homes,
    persistent_source_literals,audit_gd_wait_gate,audit_gd_wait_low,
    comparison_profile,GD_WAIT_P2,GD_WAIT_TEMPLATE,PERSIST_MARKER,WRITE_PROBE_MARKER)
from package_vc2_ce_vmu_persist import paired_helper
from vmu_persist_profile import template_profile


class ProfileTests(unittest.TestCase):
    def test_comparison_selector_is_typed_immutable_and_supported(self):
        address=0x8cfc5000
        section=(0,1,6,address,0,4,0,0,4,0)
        symbols={'_kui_armada_ce_maple_diagnostic_mode':(address,4,1,0)}
        for mode in (0,1,3,4):
            image=(struct.pack('<I',mode),None,[(1,0,address,address,4,4,5,4)],[section],symbols)
            report=comparison_profile(image,True)
            self.assertEqual(report,dict(mode=mode,virtual_card_enabled=mode==0,
                sampler_enabled=mode not in (3,4),dma_fence_enabled=mode not in (3,4),
                maple_hook_installed=mode!=4,maple_text_extended=mode!=4,
                kernel_ramend_reserved=True,runtime_storage_enabled=mode==0))
        for mode in (2,5,0xffffffff):
            image=(struct.pack('<I',mode),None,[(1,0,address,address,4,4,5,4)],[section],symbols)
            with self.assertRaises(ValueError):comparison_profile(image,True)
        for mode in (3,4):
            image=(struct.pack('<I',mode),None,[(1,0,address,address,4,4,5,4)],[section],symbols)
            with self.assertRaises(ValueError):comparison_profile(image,False)
        for record in ((address,8,1,0),(address,4,2,0),(address+1,4,1,0),(address,4,1,9)):
            bad={**symbols,'_kui_armada_ce_maple_diagnostic_mode':record}
            with self.assertRaises(ValueError):comparison_profile((*image[:4],bad),True)
        writable=(0,1,7,address,0,4,0,0,4,0)
        with self.assertRaises(ValueError):comparison_profile((*image[:3],[writable],symbols),True)

    def symbols(self):
        names=('_kui_armada_ce_vmu_runtime_bind','_kui_armada_ce_vmu_runtime_handle',
            '_kui_armada_ce_vmu_runtime_work','_kui_armada_ce_maple_runtime_wait',
            '_kui_armada_ce_maple_native_wait','_kui_vc2_ce_vmu_loan_begin',
            '_kui_vc2_ce_vmu_loan_end','_kui_vmu_store_write',
            '_runtime_cached_read','_runtime_cached_commit')
        out={name:(0x8cfc5000+4*i,4,2,1) for i,name in enumerate(names)}
        out[PERSIST_MARKER]=(1,0,0,0xfff1)
        out['__kui_maple_write_runtime']=(1,0,0,0xfff1)
        return out

    def test_distinct_persistent_mode_and_complete_runtime_are_required(self):
        self.assertFalse(persistent_profile({}))
        self.assertTrue(persistent_profile(self.symbols()))
        for name in self.symbols():
            with self.subTest(missing=name):
                symbols=self.symbols();del symbols[name]
                with self.assertRaises(ValueError):persistent_profile(symbols)
        for value,index in ((0,0xfff1),(1,1),(2,0xfff1)):
            symbols=self.symbols();symbols[PERSIST_MARKER]=(value,0,0,index)
            with self.assertRaises(ValueError):persistent_profile(symbols)
        symbols=self.symbols();symbols[WRITE_PROBE_MARKER]=(1,0,0,0xfff1)
        with self.assertRaises(ValueError):persistent_profile(symbols)

    def module(self,prepared):
        cap=struct.pack('<8I',0x3146474b,0x574f4c46,1,32,0,48,0,3)
        seed=struct.pack('<11I',0x32554d56,0x44414f4c,2,44,49152,
            zlib.crc32(prepared),0,0,0,0,0)
        return bytes(64)+cap+seed+template_profile()

    def test_seed_binds_capability_version_helper_and_launch_template(self):
        prepared=bytes(49152);module=self.module(prepared)
        result=paired_helper(module,prepared)
        self.assertEqual(result['capsule_blocks'],99)
        self.assertEqual(result['dedicated_product'],'KUIVC2TEST')
        self.assertTrue(result['physical_identity_bound_at_launch'])
        for at in (64+28,64+32+8,64+32+16,64+32+20,64+32+24):
            with self.subTest(offset=at):
                changed=bytearray(module);changed[at]^=1
                with self.assertRaises(ValueError):paired_helper(bytes(changed),prepared)
        with self.assertRaises(ValueError):paired_helper(module,prepared[:-1])
        with self.assertRaises(ValueError):paired_helper(module,bytes([1])+prepared[1:])
        with self.assertRaises(ValueError):paired_helper(module+template_profile(),prepared)
        with self.assertRaises(ValueError):paired_helper(module[:-1],prepared)

    def call_image(self):
        begin=0xacfc4000;data=bytearray(80)
        targets=('_kui_armada_ce_maple_traffic_run','_kui_armada_ce_vmu_runtime_work',
                 '_kui_armada_ce_maple_runtime_wait','_kui_armada_ce_maple_traffic_run')
        addresses={name:0x8cfc5000+0x100*i for i,name in enumerate(dict.fromkeys(targets))}
        symbols={name:(value,4,2,1) for name,value in addresses.items()}
        for i,target in enumerate(targets):
            at=12*i;literal=64+4*i
            displacement=(literal-((at+4)&~3))//4
            struct.pack_into('<5H',data,at,0xd000|displacement,0x7ff0,0x400b,0x0009,0x7f10)
            struct.pack_into('<I',data,literal,addresses[target])
        symbols['_kui_armada_ce_maple_hook']=(begin,0,0,1)
        symbols['__kui_maple_traffic_shim_end']=(begin+len(data),0,0,1)
        return bytes(data),None,[(1,0,begin,begin-0x20000000,len(data),len(data),5,4)],None,symbols

    def test_all_four_c_calls_require_homes_and_exact_target_literals(self):
        image=self.call_image();report=persistent_call_homes(image)
        self.assertEqual(len(report['calls']),4)
        self.assertEqual(report['proof_frame_and_homes_bytes'],40)
        for at in (2,8,14,20,26,32,38,44,64,68,72,76):
            with self.subTest(offset=at):
                changed=bytearray(image[0]);changed[at]^=1
                bad=(bytes(changed),*image[1:])
                with self.assertRaises(ValueError):persistent_call_homes(bad)

    def test_linked_profile_rejects_other_kernel_kdata_and_api_literals(self):
        selected=struct.pack('<2I',0x8c1d5890,0x8c1d5894)
        self.assertTrue(persistent_source_literals(selected)['stale_armada_kdata_and_api_literals_absent'])
        for value in (0x8c145890,0x8c145894,0xac145890,0xac145894,0x8c14e6e0,
                      0x01dd67c6,0x01dd6314):
            with self.subTest(stale=hex(value)):
                with self.assertRaises(ValueError):persistent_source_literals(selected+struct.pack('<I',value))
        with self.assertRaises(ValueError):persistent_source_literals(selected[:4])

    def test_gd_wait_gate_binds_complete_authored_body_padding_and_callbacks(self):
        begin=GD_WAIT_P2-32;data=bytearray(32)+bytearray(GD_WAIT_TEMPLATE)
        symbols={'_kui_vc2_ce_vmu_gd_wait':(GD_WAIT_P2,len(GD_WAIT_TEMPLATE),2,1),
            '__vc2_maple_bridge_blob_end':(begin,0,0,1),
            '__kui_maple_hook_binary_end':(GD_WAIT_P2+len(GD_WAIT_TEMPLATE),0,0,1)}
        for offset,name,address in ((0xb4,'_kui_vc2_ce_vmu_gd_wait_admit',0x8cfc8000),
                                    (0xb8,'_kui_vc2_ce_vmu_gd_wait_fault',0x8cfc8100)):
            symbols[name]=(address,4,2,1);struct.pack_into('<I',data,32+offset,address)
        image=(bytes(data),None,[(1,0,begin,begin-0x20000000,len(data),len(data),5,4)],None,symbols)
        sections={'.gd_wait':(0,1,6,begin,0,len(data))}
        self.assertFalse(audit_gd_wait_gate(image,sections)['request_token_synthesized'])
        for at in range(len(data)):
            with self.subTest(offset=at):
                changed=bytearray(data);changed[at]^=1
                with self.assertRaises(ValueError):audit_gd_wait_gate((bytes(changed),*image[1:]),sections)

    def test_low_loan_dispatch_binds_flag_and_exact_retry_targets(self):
        begin=0x8c006000;busy=begin+16;retry=begin-64;data=bytearray(44)
        struct.pack_into('<10H',data,0,0xe202,0x3120,0x8b04,0xd206,0xd306,0xd107,
            0x412b,0x0009,0x6073,0x8800)
        struct.pack_into('<3I',data,32,retry,busy,GD_WAIT_P2)
        symbols={'__kui_vc2_gd_retry_before_trace':(retry,0,0,1),
                 '__kui_vc2_gd_legacy_busy':(busy,0,0,1)}
        image=(bytes(data),None,[(1,0,begin,begin,len(data),len(data),5,4)],None,symbols)
        self.assertTrue(audit_gd_wait_low(image)['dispatch_bytes_pinned'])
        for at in list(range(20))+list(range(32,44)):
            with self.subTest(offset=at):
                changed=bytearray(data);changed[at]^=1
                with self.assertRaises(ValueError):audit_gd_wait_low((bytes(changed),*image[1:]))


if __name__=='__main__':unittest.main()
