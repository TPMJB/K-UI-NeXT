#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the audited one-pair native Maple probe and exact source checkpoint."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile
from check_vmu_native_passive import check

def package(build):
    root=Path(__file__).resolve().parents[1]
    revision=subprocess.check_output(['git','rev-parse','--short=12','HEAD'],cwd=root,text=True).strip()
    if subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip():
        raise ValueError('Commit source changes before packaging a reviewable build')
    report=check(build,self_test=True)
    # The audit checks the module against the linked entry and embedded stage.
    if report['allocator_bootstrap_audit']['runtime']['build']!=revision:
        raise ValueError('Fresh build/source commit mismatch')
    report.update(source_commit=revision,console_execution_verified=False,
        maple_interception_installed=True,virtual_replies_enabled=False,
        storage_writes_enabled=False,expected_phase=11)
    branch=subprocess.check_output(['git','branch','--show-current'],cwd=root,text=True).strip()
    source=(build/'native-passive-source.bundle').resolve()
    subprocess.run(['git','bundle','create',str(source),'HEAD',branch],cwd=root,check=True)
    subprocess.run(['git','bundle','verify',str(source)],cwd=root,check=True,stdout=subprocess.DEVNULL)
    files={'KUI/apps/games/retail-boot.kui':(build/'retail-maple-observe.kui').read_bytes(),
        'README-FIRST.md':(root/'docs/vmu-native-passive-test.md').read_bytes(),
        'probe-report.json':(json.dumps(report,indent=2)+'\n').encode(),
        'LICENSE':(root/'LICENSE').read_bytes(),
        'THIRD_PARTY.md':(root/'THIRD_PARTY.md').read_bytes(),
        f'source/K-UI-native-passive-{revision}.bundle':source.read_bytes()}
    files['SHA256SUMS.txt']=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n'
        for name,data in sorted(files.items())).encode()
    output=build/'K-UI-Native-Passive-Test.zip'
    with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as z:
        for name,data in files.items():z.writestr(name,data)
    with zipfile.ZipFile(output) as z:
        if set(z.namelist())!=set(files) or any(z.read(name)!=data for name,data in files.items()):
            raise ValueError('ZIP byte verification failed')
    print(json.dumps({'zip':str(output),'bytes':output.stat().st_size,'commit':revision,
        'module_sha256':hashlib.sha256(files['KUI/apps/games/retail-boot.kui']).hexdigest()}))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build',type=Path)
    package(parser.parse_args().build)
