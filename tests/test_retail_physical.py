#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Physical Standard descriptor ABI, profile exclusion and linked binding refusals."""
import os
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
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

    def test_only_bound_exact_cold_crc_names_are_stage_only(self):
        stage,module=fixture();symbols=dict(stage[4])
        symbols.update({name:(1,0,0,0xfff1) for name in tool.COLD_CRC_SYMBOLS})
        cold=(*stage[:4],symbols)
        with self.assertRaises(ValueError):tool.no_virtual_runtime({'stage':cold},module)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'stage.elf';path.write_bytes(b'other stage')
            with self.assertRaises(ValueError):tool.cold_crc_scope(cold,path)
        with patch.object(tool,'cold_crc_scope',return_value={'bound':True}) as proof:
            self.assertEqual(tool.no_virtual_runtime({'stage':cold},module,stage_path='actual'),{'bound':True})
            proof.assert_called_once_with(cold,'actual')
        for owner in ('entry','resident-sci','resident-scia'):
            with patch.object(tool,'cold_crc_scope',return_value={'bound':True}):
                with self.assertRaises(ValueError):tool.no_virtual_runtime({'stage':cold,owner:cold},module)
        for name in ('_kui_native_stage_cached_crc32_extra','__kui_native_cached_boot_crc_extra',
                     '_kui_vmu_worker','_native_runtime_mode','__kui_native_cached_discovery'):
            extra=dict(symbols);extra[name]=(1,0,0,0xfff1)
            with patch.object(tool,'cold_crc_scope',return_value={'bound':True}):
                with self.assertRaises(ValueError):tool.no_virtual_runtime({'stage':(*stage[:4],extra)},module)
        sys.path.insert(0,str(ROOT/'tests'))
        import test_native_stage_cached_crc as checker
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'stage.elf';path.write_bytes(stage[0])
            for result in ({'status':'REFUSED','elf_sha256':tool.sha(stage[0])},
                           {'status':'PASS','elf_sha256':'0'*64}):
                with patch.object(checker,'audit',return_value=result):
                    with self.assertRaises(ValueError):tool.cold_crc_scope(cold,path)

    def test_clean_compile_receipts_allow_only_explicit_auditor_edits(self):
        stage,_=fixture();commit='f8807cb1656137cc5fad7854802d685ea168736f'
        changed=['tools/check_retail_physical.py','tests/test_retail_physical.py']
        def git(command,**_):
            if command[1]=='rev-parse':return commit+'\n'
            if command[1]=='status':return b' M tools/check_retail_physical.py\n'
            if command[1]=='diff':return b'\0'.join(p.encode() for p in changed)+b'\0'
            if command[1]=='ls-files':return b''
            self.fail('Unexpected git command')
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);receipt=dict(commit=commit,source_dirty=False,elf_sha256=tool.sha(stage[0]))
            (p/'entry.compile.json').write_text(json.dumps(receipt))
            (p/'plain-layout-config').write_text('LOW=1 TRACE=0 SONIC=0 BUILD='+commit[:12])
            with patch.object(tool.subprocess,'check_output',side_effect=git):
                before=(p/'entry.compile.json').read_bytes()
                result=tool.source_provenance(p,{'entry':stage},False)
                self.assertFalse(result['source_dirty'])
                self.assertEqual(result['audit_only_worktree']['changed_paths'],sorted(changed))
                self.assertEqual((p/'entry.compile.json').read_bytes(),before)
                with self.assertRaises(ValueError):tool.source_provenance(p,{'entry':stage},False,permit_audit_only=False)
                for bad in ('src/loader/retail_stage.c','Makefile.dc','docs/other.md','new-unknown.py'):
                    changed.append(bad)
                    with self.assertRaises(ValueError):tool.source_provenance(p,{'entry':stage},False)
                    changed.pop()
                receipt['elf_sha256']='0'*64;(p/'entry.compile.json').write_text(json.dumps(receipt))
                with self.assertRaises(ValueError):tool.source_provenance(p,{'entry':stage},False)

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
