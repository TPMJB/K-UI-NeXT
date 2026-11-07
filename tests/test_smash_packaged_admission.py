#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production GUI admission against all actual modules from a shipped ZIP.

The runtime-file and original-disc proof are explicit read-only seams. The
production GUI selector, layout/capability checks and all typed seed finders
execute on the exact packaged module payloads, including compiler literals.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from runtime_package import verify
from smash_ce_profile import profile

MODULES=('retail-smash-menu.kui','retail-smash-native-menu.kui','retail-smash-vc2.kui','retail-smash-swirl.kui')
SOURCES=('tests/test_smash_packaged_admission.py','tests/test_smash_menu_collection_prepare.c',
    'tests/test_smash_ce_core.c','tests/test_smash_ce_core.py',
    'Makefile.smash-menu-ui-tests','src/apps/games_retail.c',
    'src/apps/smash_menu_collection_prepare.inc','src/apps/smash_menu_capsule_prepare.inc',
    'src/core/smash_menu_collection.c','src/core/smash_ce_child.c',
    'src/core/smash_menu_loader.c','src/core/smash_menu_return.c','src/core/smash_native_menu.c',
    'src/core/smash_ce_profile.c','src/core/smash_ce_helper.c','include/kui/smash_ce_helper.h','include/kui/native_standard_route.h',
    'include/kui/native_standalone_foreground.h','include/kui/smash_menu_loader_capability.h',
    'include/kui/smash_menu_collection.h','include/kui/smash_ce_child.h',
    'include/kui/smash_ce_profile.h','tools/runtime_package.py','tools/smash_ce_profile.py')
def sha(data):return hashlib.sha256(data).hexdigest()
def audit(package,runtime=None):
    package=Path(package);actual={};headers={};helper_owners={};baseline={}
    with zipfile.ZipFile(package) as archive:
        for name in archive.namelist():
            if name.startswith('KUI/') and not name.endswith('/'):
                data=archive.read(name);baseline[name]=dict(sha256=sha(data),bytes=len(data))
        for name in MODULES:
            path='KUI/apps/games/'+name;actual[path]=archive.read(path);headers[path]=verify(actual[path])
        for kind,label in ((1,'vc2'),(2,'swirl')):
            helper_path='KUI/apps/games/smash-'+label+'-vmu-helper.bin'
            helper=archive.read(helper_path);assert len(helper)==49152
            actual[helper_path]=helper
            owner=profile(helper,kind,embedded=True);module=actual['KUI/apps/games/retail-smash-'+label+'.kui'][64:]
            assert module.count(owner)==1,'Exact packaged helper does not match its module owner'
            damaged=bytearray(helper);damaged[0]^=1
            assert profile(damaged,kind,embedded=True) not in module,'Changed helper must not match the module'
            helper_owners[label]=dict(helper_sha256=sha(helper),owner_sha256=sha(owner),
                exact_packaged_pair=True,changed_helper_refused=True)
    assert len({v['build'] for v in headers.values()})==1,'Four child/menu modules must share one build'
    core=subprocess.run([sys.executable,str(ROOT/'tests/test_smash_ce_core.py')],cwd=ROOT,
        text=True,capture_output=True)
    if core.returncode:raise AssertionError(core.stdout+core.stderr)
    with tempfile.TemporaryDirectory(prefix='smash-packaged-admission-') as temporary:
        directory=Path(temporary)
        for name in MODULES:(directory/name).write_bytes(actual['KUI/apps/games/'+name])
        command=['make','-f','Makefile.smash-menu-ui-tests','BUILD='+str(directory/'host'),
            'MENU_LOADER='+str(directory/MODULES[0]),'MENU_NATIVE='+str(directory/MODULES[1]),
            'MENU_VC2='+str(directory/MODULES[2]),'MENU_SWIRL='+str(directory/MODULES[3])]
        completed=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
        if completed.returncode:raise AssertionError(completed.stdout+completed.stderr)
        for expected in ('Actual packaged Smash menu loader: production source-bound selection OK',
            'Actual packaged native child: production module admission OK',
            'Actual packaged VC2 child: production module admission and refusals OK',
            'Actual packaged Swirl child: production module admission and refusals OK'):
            assert expected in completed.stdout,'Missing actual packaged admission case: '+expected
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip())
    current_runtime=None
    if runtime is not None:
        runtime=Path(runtime);wire=runtime.read_bytes();header=verify(wire)
        assert header['build']==commit[:12],'Corrected GUI runtime must match the tested source commit'
        current_runtime=dict(path=str(runtime.resolve()),sha256=sha(wire),bytes=len(wire),header=header)
    return dict(schema='kui.smash.packaged-admission.v1',status='PASS',source_commit=commit,source_dirty=dirty,
        package_path=str(package.resolve()),package_sha256=sha(package.read_bytes()),package_bytes=package.stat().st_size,
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCES},
        baseline_install_files=baseline,
        preserved_files={name:value for name,value in baseline.items() if name!='KUI/runtime.kui'},
        runtime=current_runtime,
        module_headers=headers,actual_outer_production_GUI_probe=True,
        actual_child_production_module_readers=True,helper_owners=helper_owners,
        wrong_version_size_and_nonblank_seeds_refused=True,
        host_sanitizers='ASan/UBSan',core_stdout=core.stdout,stdout=completed.stdout,
        limits=['Exact shipped bytes execute the production GUI selector, child admission and core finders.',
            'Runtime-file I/O and original-disc proof use read-only seams; this is not console/game execution.'])
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,help='Bind the corrected GUI runtime after the clean final build')
    parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
    result=audit(args.package,args.runtime);args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'status':'PASS','report':str(args.report),'actual_modules':len(MODULES),'matched_helpers':2}))
if __name__=='__main__':main()
