#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind SCI receive-policy selectors and exercise incremental mode switches."""
from pathlib import Path
import json
import os
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import check_native_vmu_only as audit


class SCIRxPolicy(unittest.TestCase):
    def test_marker_selection_and_mutations(self):
        for value,name in enumerate(('unpaced','paced','pio')):
            with self.subTest(policy=name):
                stage={'__kui_native_vmu_sci_rx_policy_stage':(value,0,0,0xfff1)}
                low={'__kui_native_vmu_sci_rx_policy_resident':(value,0,0,0xfff1)}
                if name=='paced':
                    low.update(__kui_native_vmu_sci_rx_paced_bus=audit.ONE,
                        __kui_native_vmu_sci_rx_paced_max_polls=(10000,0,0,0xfff1))
                image=lambda symbols:(None,None,None,None,symbols)
                body=struct.pack('<3IH',0xffa00018,0xffa00040,0xffe00010,10000)
                with patch.object(audit,'symbol_bytes',return_value=(0x8c004000,body,(0,0,6))):
                    result=audit.sci_rx_policy(image(stage),image(low),1)
                self.assertEqual(result['policy'],name)
                self.assertTrue(result['matching_immutable_markers'])
                self.assertEqual(result['mutation_rejections'],6)
                if name=='paced':
                    self.assertEqual(result['credit_max_polls_per_byte'],10000)
                    self.assertEqual(result['tdre_wait_max_polls_per_byte'],10000)
                    self.assertFalse(result['instruction_loop_bound_modeled'])
                # A label cannot select a paced bus lacking the actual guard.
                if name=='paced':
                    for marker in ('__kui_native_vmu_sci_rx_paced_bus',
                                   '__kui_native_vmu_sci_rx_paced_max_polls'):
                        changed=dict(low);changed.pop(marker)
                        with self.assertRaises(ValueError):
                            audit.sci_rx_policy(image(stage),image(changed),1)
                elif name=='pio':
                    changed=dict(low);changed['_dma_block']=(0x8c004000,100,2,1)
                    with self.assertRaises(ValueError):
                        audit.sci_rx_policy(image(stage),image(changed),1)

    def test_compile_time_policy_guards(self):
        values={'KUI_NATIVE_VMU_ONLY':1,'KUI_NATIVE_VMU_LITE':1,
                'KUI_NATIVE_VMU_ONLY_TRANSPORT':1,'KUI_NATIVE_VMU_SCI_RX_PACED':1,
                'KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION':1,
                'KUI_NATIVE_SPLIT_CONTROLLER_GENERAL':1,'KUI_NATIVE_GENERAL_LEAN_LAYOUT':1,
                'KUI_VMU_NATIVE_PERSISTENT_PROBE':1,'KUI_VMU_NATIVE_CACHED_CODE':1}
        changes=({}, {'KUI_NATIVE_VMU_SCI_RX_PACED':2},
                 {'KUI_NATIVE_VMU_ONLY':0},{'KUI_NATIVE_VMU_LITE':0},
                 {'KUI_NATIVE_VMU_ONLY_TRANSPORT':0},{'KUI_SCI_SD_PIO_ONLY':1},
                 {'KUI_RETAIL_TOY_PILOT':1},{'KUI_RETAIL_ASYNC':1},{'KUI_RETAIL_CE':1})
        for change in changes:
            with self.subTest(change=change):
                result=subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),
                    '-std=c11','-Werror','-Iinclude',
                    *(f'-D{k}={v}' for k,v in {**values,**change}.items()),
                    '-x','c','-','-fsyntax-only'],cwd=ROOT,text=True,capture_output=True,
                    input='#include "kui/native_isolation.h"\n')
                self.assertEqual(result.returncode==0,not change,result.stderr)

    def test_shared_build_policy_switch_recompiles_all_families(self):
        with tempfile.TemporaryDirectory(prefix='vmu-rx-policy-switch-') as temporary:
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
            objects=[build/path for path in ('native/src/core/vmu_native_heap.o',
                'resident/src/loader/sci_sd_bus.o','stage/src/core/hash.o',
                'worker/src/loader/vmu_maple_observe_worker.o')]

            def run(mode):
                result=subprocess.run(['make','--no-print-directory','-f',
                    'Makefile.vmu-native-vmu-lite'+('' if mode=='base' else '-'+mode),
                    f'BUILD={build}',f'CC={sys.executable} {compiler}',
                    '-o',str(build/'vmu_maple_observe_symbols.h'),
                    '-o',str(build/'worker.bin'),*map(str,objects)],
                    cwd=ROOT,text=True,capture_output=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                return [json.loads(line) for line in log.read_text().splitlines()]

            count=0
            for mode in ('base','paced','pio','unpaced','paced','base'):
                rows=run(mode)
                self.assertEqual(len(rows),count+len(objects),'Policy retained a stale object')
                for row in rows[count:]:
                    native='/native/' in row[row.index('-o')+1]
                    self.assertEqual('-DKUI_NATIVE_VMU_SCI_RX_PACED=1' in row,
                                     mode=='paced' and not native)
                    self.assertEqual('-DKUI_SCI_SD_PIO_ONLY=1' in row,
                                     mode=='pio' and not native)
                count=len(rows)
                self.assertEqual(len(run(mode)),count,'Unchanged policy rebuilt its objects')

    def test_wrappers_reject_other_transports_and_policies(self):
        for arguments in (('VMU_TRANSPORT=0',),('VMU_SCI_RX_POLICY=invalid',),
                          ('VMU_ONLY_FULL_SUBMIT=1',)):
            result=subprocess.run(['make','-f','Makefile.vmu-native-vmu-lite-paced',
                '-n','all',*arguments],cwd=ROOT,text=True,capture_output=True)
            self.assertNotEqual(result.returncode,0,result.stdout)


if __name__=='__main__':
    unittest.main()
