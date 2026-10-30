#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Physical Standard descriptor ABI, profile exclusion and linked binding refusals."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import check_retail_physical as tool


def fixture():
    data=bytes(32)+tool.CAPABILITY+bytes(32)
    section=(0,1,2,tool.STAGE_ADDRESS,0,len(data),0,0,4,0)
    stage=(data,None,None,[section],{tool.SYMBOL:(tool.STAGE_ADDRESS+32,32,1,0),tool.MARKER:(1,0,0,0xfff1)})
    module=bytes(64+tool.STAGE_BLOB_OFFSET)+data
    return stage,module


class PhysicalStandard(unittest.TestCase):
    def test_actual_descriptor_header_with_host_sanitizers(self):
        with tempfile.TemporaryDirectory(prefix='physical-capability-') as directory:
            binary=Path(directory)/'test'
            subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-Wall','-Wextra','-Werror',
                '-Wpedantic','-fsanitize=address,undefined','-fno-pie','-no-pie','-Iinclude',
                'tests/test_native_physical_capability.c','-o',str(binary)],cwd=ROOT,check=True)
            subprocess.run([str(binary)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))

    def test_linked_typed_descriptor_binds_exact_readonly_owner_and_module_offset(self):
        stage,module=fixture();proof=tool.typed_descriptor(stage,module)
        self.assertEqual(proof['module_offset'],64+tool.STAGE_BLOB_OFFSET+32)
        self.assertTrue(proof['allocated_read_only'])
        for index in range(32):
            changed=bytearray(stage[0]);changed[32+index]^=1
            with self.assertRaises(ValueError):tool.typed_descriptor((bytes(changed),*stage[1:]),module)
        with self.assertRaises(ValueError):tool.typed_descriptor(stage,module+tool.CAPABILITY)
        with self.assertRaises(ValueError):tool.typed_descriptor(stage,bytes(len(module)))

    def test_writable_descriptor_and_wrong_source_marker_refuse(self):
        stage,module=fixture();sections=[(*stage[3][0][:2],3,*stage[3][0][3:])]
        with self.assertRaises(ValueError):tool.typed_descriptor((*stage[:3],sections,stage[4]),module)
        symbols=dict(stage[4]);symbols[tool.MARKER]=(2,0,0,0xfff1)
        with self.assertRaises(ValueError):tool.typed_descriptor((*stage[:4],symbols),module)

    def test_vmu_worker_profile_mode_cache_and_candidate_symbols_refuse(self):
        stage,module=fixture();tool.no_virtual_runtime({'stage':stage},module)
        for symbol in ('_kui_vmu_game_candidates_list','__kui_native_background_compact_stage',
                       '_native_persist_storage_init','__vmu_sd_profile_blob_start'):
            symbols=dict(stage[4]);symbols[symbol]=(0,0,0,0xfff1)
            with self.assertRaises(ValueError):tool.no_virtual_runtime({'stage':(*stage[:4],symbols)},module)
        for magic in (b'KUIVMUP2',b'KUINVM01',b'KUINVS01'):
            with self.assertRaises(ValueError):tool.no_virtual_runtime({'stage':stage},module+magic)

    def test_reviewed_linked_guard_accepts_only_reader_zero_and_three_transports(self):
        for reader in (*range(8),0xffffffff):
            for transport in (*range(8),0xffffffff):
                self.assertEqual(tool.execute_guard(tool.GUARD,reader,transport),int(reader==0 and transport<3))
        for offset in range(len(tool.GUARD)):
            changed=bytearray(tool.GUARD);changed[offset]^=1
            with self.assertRaises(ValueError):tool.execute_guard(bytes(changed),0,1)

    def test_physical_stage_optin_rejects_vmu_and_special_profiles(self):
        command=[*shlex.split(os.environ.get('CC','cc')),'-E','-P','-Iinclude','-Isrc/loader',
                 '-DKUI_RETAIL_PHYSICAL_CAPABILITY=1','src/loader/retail_stage.c']
        ordinary=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
        self.assertEqual(ordinary.returncode,0,ordinary.stderr)
        self.assertIn('kui_native_physical_capability',ordinary.stdout)
        self.assertIn('PHYSICAL STANDARD ONLY',ordinary.stdout)
        for define in ('KUI_RETAIL_CE=1','KUI_RETAIL_ZSO=1','KUI_NATIVE_VMU_ONLY=1',
                       'KUI_VMU_NATIVE_BACKGROUND=1','KUI_NATIVE_DUAL_READERS=1'):
            run=subprocess.run([*command[:-1],'-D'+define,command[-1]],cwd=ROOT,text=True,capture_output=True)
            self.assertNotEqual(run.returncode,0,define)
            self.assertIn('Physical Standard capability',run.stderr)
        normal=subprocess.run([item for item in command if item!='-DKUI_RETAIL_PHYSICAL_CAPABILITY=1'],
                              cwd=ROOT,text=True,capture_output=True)
        self.assertEqual(normal.returncode,0,normal.stderr)
        self.assertNotIn('kui_native_physical_capability',normal.stdout)
        self.assertNotIn('PHYSICAL STANDARD ONLY',normal.stdout)


if __name__=='__main__':unittest.main()
