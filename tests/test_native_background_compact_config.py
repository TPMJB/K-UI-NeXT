#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep the combined compact geometry distinct from standalone VMU-lite."""
from pathlib import Path
import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from native_runtime_layout import BACKGROUND_COMPACT,GENERAL,VMU_LITE,layout_for
from prove_vmu_native_evolution1_readahead_lease import geometry_defines
from check_native_background_compact import complete_callgraphs

ONE=(1,0,0,0xfff1)


class CompactBackgroundConfig(unittest.TestCase):
    def test_partial_compiler_graph_cannot_feed_the_stack_audit(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'stage.ci'
            path.write_text('graph: { node: { title: "quoted } \\\" {" } }\n')
            self.assertEqual(complete_callgraphs(temporary),
                             dict(reports=1,all_reports_complete=True))
            for data in ('','graph: { node: { title: "cut" }',
                         'graph: { node: { title: "unterminated } }',
                         'graph: { } } { }'):
                path.write_text(data)
                with self.assertRaises(ValueError):complete_callgraphs(temporary)

    def test_original_corpus_compiles_the_selected_production_geometry(self):
        definitions=geometry_defines(True,BACKGROUND_COMPACT)
        result=subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),
            '-std=c11','-Wall','-Wextra','-Werror','-Wpedantic',*definitions,
            '-I',str(ROOT/'include'),'-x','c','-fsyntax-only','-'],
            input='''#include "kui/vmu_native_background.h"
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES==0x12000,"worker");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==0x47000,"lease");
_Static_assert(KUI_NATIVE_RA_DATA_BYTES==0x30000,"full cache");
''',text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotIn('-DKUI_NATIVE_BACKGROUND_COMPACT=1',geometry_defines(True,GENERAL))
        with self.assertRaises(AssertionError):geometry_defines(False,BACKGROUND_COMPACT)
        result=subprocess.run([sys.executable,str(ROOT/'tools/audit_vmu_native_corpus.py'),
            '--diagnostic-layout','--compact-background-layout','--stage-end','0x8cf00000',
            '--output','unused.json'],text=True,capture_output=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('not allowed with argument',result.stderr)

    def test_independent_exact_geometry_oracle(self):
        g=BACKGROUND_COMPACT
        self.assertEqual((g.WORKER_BYTES,g.LEASE_BYTES,g.DATA_BYTES),(0x12000,0x47000,0x30000))
        self.assertEqual((g.MAP_OFFSET,g.REGION_OFFSET,g.AUX_OFFSET),(0x12000,0x12400,0x13000))
        self.assertEqual(g.AUX_OFFSET+g.AUX_SPAN,g.STACK_END)
        self.assertEqual(g.STACK_END,g.DATA_OFFSET)
        self.assertEqual(g.DATA_OFFSET+g.DATA_BYTES,g.LEASE_BYTES)
        roles=('stage','worker','resident','auxiliary')
        general=('__kui_native_general_lean_stage_layout','__kui_native_general_lean_worker_layout',
                 '__kui_native_general_lean_resident_layout','__kui_native_general_lean_auxiliary')
        for role,lean_marker in zip(roles,general):
            with self.subTest(role=role):
                marker='__kui_native_background_compact_'+role
                symbols={marker:ONE,lean_marker:ONE,'__kui_native_isolation_lease_bytes':(0x47000,0,0,0xfff1)}
                self.assertIs(layout_for(symbols),g)
                for name,value in ((marker,(2,0,0,0xfff1)),
                                   (lean_marker,(2,0,0,0xfff1)),
                                   ('__kui_native_isolation_lease_bytes',(0x51000,0,0,0xfff1)),
                                   ('__kui_native_vmu_only_worker',ONE)):
                    changed={**symbols,name:value}
                    with self.assertRaises(ValueError):
                        layout_for(changed)
                changed=dict(symbols);changed.pop(marker)
                with self.assertRaises(ValueError):
                    layout_for(changed)
        self.assertIs(layout_for({'__kui_native_general_lean_worker_layout':ONE,
            '__kui_native_isolation_lease_bytes':(0x51000,0,0,0xfff1)}),GENERAL)
        self.assertIs(layout_for({'__kui_native_vmu_only_worker':ONE,'__kui_native_vmu_lite_worker':ONE,
            '__kui_native_isolation_lease_bytes':(0x12000,0,0,0xfff1)}),VMU_LITE)

    def test_selected_flags_keep_both_background_engines_and_cache(self):
        result=subprocess.run(['make','-f','Makefile.vmu-native-background-compact',
            'BUILD=build/compact-background-selection-dryrun','-n','-B','all'],
            cwd=ROOT,text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        for expected in ('-DKUI_NATIVE_BACKGROUND_COMPACT=1','-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1',
            '-DKUI_NATIVE_DUAL_READERS=1','-DKUI_NATIVE_READAHEAD=1','-DKUI_VMU_NATIVE_BACKGROUND=1',
            '-DKUI_RETAIL_ASYNC=1','-DKUI_SCI_SD_NO_BLOCK=1','-DKUI_NATIVE_SPLIT_CONTROLLER_STREAM_GUARD=1',
            '-Tsrc/loader/vmu_native_background_compact.ld','--span-bytes 0x12000',
            'resident-legacy.elf','native-ra-aux.elf','retail-native-background-compact.kui'):
            self.assertIn(expected,result.stdout)
        for forbidden in ('-DKUI_NATIVE_VMU_ONLY=1','-DKUI_NATIVE_VMU_LITE=1',
                          '-DKUI_NATIVE_VMU_SCI_RX_PACED=1','-DKUI_SCI_SD_PIO_ONLY=1'):
            self.assertNotIn(forbidden,result.stdout)

    def test_same_build_recompiles_every_geometry_dependent_family(self):
        with tempfile.TemporaryDirectory(prefix='compact-background-switch-') as temporary:
            directory=Path(temporary);build=directory/'build'
            compiler=directory/'record_compiler.py';log=directory/'compiles.jsonl'
            compiler.write_text('''import json,pathlib,sys
arguments=sys.argv[1:]
output=pathlib.Path(arguments[arguments.index('-o')+1])
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(arguments))
with pathlib.Path(__file__).with_name('compiles.jsonl').open('a') as stream:
    stream.write(json.dumps(arguments)+'\\n')
''')
            objects=[build/path for path in (
                'native/src/core/vmu_native_heap.o','resident/src/loader/sci_sd_bus.o',
                'legacy/src/core/retail_gd.o','stage/src/core/hash.o',
                'worker/src/loader/vmu_maple_observe_worker.o','ra/src/core/native_readahead_cache.o')]

            def run(compact):
                makefile='Makefile.vmu-native-background-compact' if compact else 'Makefile.vmu-native-split-controller-stream-guard'
                command=['make','--no-print-directory','-f',makefile,f'BUILD={build}',
                         f'CC={sys.executable} {compiler}']
                for ignored in ('vmu_maple_observe_symbols.h','worker.bin','native-ra-aux.bin',
                                'native-ra-aux.reloc','vmu_maple_observe_legacy_symbols.h','resident-legacy.bin'):
                    command+=['-o',str(build/ignored)]
                result=subprocess.run([*command,*map(str,objects)],cwd=ROOT,text=True,capture_output=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                return [json.loads(line) for line in log.read_text().splitlines()]

            count=0
            for compact in (False,True,False,True):
                rows=run(compact)
                self.assertEqual(len(rows),count+len(objects),'Layout switch retained a stale object')
                for row in rows[count:]:
                    self.assertEqual('-DKUI_NATIVE_BACKGROUND_COMPACT=1' in row,compact)
                count=len(rows)
                self.assertEqual(len(run(compact)),count,'Unchanged layout rebuilt an object')


if __name__=='__main__':
    unittest.main()
