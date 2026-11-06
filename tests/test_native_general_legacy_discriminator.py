#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check actual General Legacy selection without weakening New refusals."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf
from check_native_general_sdk_profile import profile
from check_vmu_native_map_refusal import wide_guard_pins,signature
from check_native_general_lean_profile import LOW_PINS

LEGACY=('_kui_retail_irq_entry','_kui_retail_vector_interrupt','_kui_retail_rehook',
        '_kui_retail_release_100','_kui_retail_release_400','_kui_retail_release_600')
NEW=('__kui_native_readahead_resident','__kui_native_sdk_sci_route_resident',
     '__kui_native_sdk_sci_structural_resident','_kui_native_ra_reader_ops',
     '_kui_retail_sdk_sci_entry','_kui_retail_native_session_control')


def audit(builds):
    cases=[]
    for build in map(Path,builds):
        legacy=read_elf(build/'resident-legacy.elf');new=read_elf(build/'resident-sci.elf')
        expected='session' if '_kui_retail_native_session_control' in new[4] else 'ordinary'
        assert profile(legacy,'low') is None and profile(new,'low')==expected
        assert wide_guard_pins(legacy)==LOW_PINS['map_refusal']
        assert wide_guard_pins(new)['_native_background_map']!=LOW_PINS['map_refusal']['_native_background_map']
        mutations=[]
        for name in LEGACY:
            changed=copy.deepcopy(legacy);del changed[4][name];mutations.append(('missing '+name,changed))
            row=legacy[4][name];address,count,kind,owner=row
            for label,value in (
                ('scalar',0),('short tuple',row[:3]),('list',list(row)),
                ('boolean owner',(address,count,kind,True)),('string owner',(address,count,kind,str(owner))),
                ('negative owner',(address,count,kind,-1)),('unowned',(address,count,kind,len(legacy[3]))),
                ('data owner',(address,count,kind,0)),('function type',(address,count,2,owner)),
                ('sized',(address,2,kind,owner)),('unaligned',(address+1,count,kind,owner)),
                ('low range',(0x8c003ffe,count,kind,owner)),('high range',(0x8c007ba0,count,kind,owner))):
                changed=copy.deepcopy(legacy);changed[4][name]=value;mutations.append((label+' '+name,changed))
        changed=copy.deepcopy(legacy);changed[4][LEGACY[1]]=changed[4][LEGACY[0]];mutations.append(('aliased entries',changed))
        for marker in ('__kui_native_general_lean_resident_layout','__kui_vmu_native_background_resident'):
            changed=copy.deepcopy(legacy);changed[4][marker]=(0,0,0,0xfff1);mutations.append(('bad '+marker,changed))
        for name in NEW:
            changed=copy.deepcopy(legacy);changed[4][name]=(1,0,0,0xfff1);mutations.append(('orphan New '+name,changed))
        for name in NEW[:3]:
            changed=copy.deepcopy(new);del changed[4][name];mutations.append(('New removed '+name,changed))
            changed=copy.deepcopy(new);changed[4][name]=(0,0,0,0xfff1);mutations.append(('New bad '+name,changed))
        changed=copy.deepcopy(new)
        for name in NEW:changed[4].pop(name,None)
        mutations.append(('New stripped all New capabilities',changed))
        rejected=[]
        for label,image in mutations:
            try:
                profile(image,'low')
            except (ValueError,AssertionError,KeyError,TypeError):rejected.append(label)
            else:raise ValueError('Legacy discriminator accepted '+label)
        guard_refusals=0
        if expected=='session':
            # This old absence-only case can leave the SDK selector at None.
            # It still must not admit a New body under the Legacy guard pins.
            changed=copy.deepcopy(new);del changed[4]['_kui_retail_native_session_control']
            disassembly=subprocess.check_output([str(ROOT/'.deps/sh-elf/bin/sh-elf-objdump'),
                '-d',str(build/'resident-sci.elf')],text=True)
            try:
                guards=wide_guard_pins(changed)
                for name,pin in guards.items():
                    if signature(changed,disassembly,name)[:2]!=pin:
                        raise ValueError('New body differs from independently pinned Legacy guard')
            except (ValueError,AssertionError,KeyError,TypeError):guard_refusals+=1
            else:raise ValueError('New session control deletion downgraded the linked guard family')
        cases.append(dict(build=str(build),new_profile=expected,legacy_profile=None,
            independent_legacy_guard_pins=True,mutation_rejections=len(rejected),
            session_control_deletion_linked_guard_refusals=guard_refusals,
            rejected=rejected,artifacts_sha256={name:hashlib.sha256((build/name).read_bytes()).hexdigest()
                for name in ('resident-sci.elf','resident-legacy.elf')}))
    return dict(schema='kui.native.general.legacy-discriminator.v1',status='PASS',cases=cases,
        source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
            ('tools/check_native_general_sdk_profile.py','tests/test_native_general_legacy_discriminator.py')},
        limits='Selector metadata refusals only; independent linked Legacy guards and full native models remain required.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('builds',nargs='+',type=Path)
    print(json.dumps(audit(parser.parse_args().builds),indent=2))
