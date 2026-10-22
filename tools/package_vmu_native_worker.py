#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit/package the terminal-only relocated native VMU worker proof."""
import argparse,gzip,hashlib,json,subprocess,zipfile
from pathlib import Path
from check_vmu_native_heap_probe import inspect
from check_vmu_native_worker_gateway import check

def package(build):
    root=Path(__file__).resolve().parents[1]
    revision=subprocess.check_output(['git','rev-parse','--short=12','HEAD'],cwd=root,text=True).strip()
    if subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip():
        raise ValueError('Commit source changes before making a reviewable worker bundle')
    base=inspect(build,self_test=True,objdump='sh-elf-objdump')
    worker=check(build)
    if base['runtime']['build']!=revision:raise ValueError('Fresh build/source commit mismatch')
    base['legacy_worker_or_storage_gateway_linked']=base.pop('vmuwriter_or_mapleworker_linked')
    report={'schema':'kui.native.worker.probe.artifact.v1','source_commit':revision,
            'module_sha256':base['module_sha256'],'runtime':base['runtime'],
            'allocator_bootstrap_audit':base,'worker_execution_audit':worker,
            'maple_interception_installed':False,'storage_writes_enabled':False,
            'console_execution_verified':False,'expected_phase':10,'expected_blank_image_crc':'3d826179'}
    files={'KUI/apps/games/retail-boot.kui':(build/'retail-maple-observe.kui').read_bytes(),
           'install.py':(root/'tools/install_vmu_native_heap_probe.py').read_bytes(),
           'README-FIRST.md':(root/'docs/vmu-native-worker-test.md').read_bytes(),
           'probe-report.json':(json.dumps(report,indent=2)+'\n').encode(),
           'LICENSE':(root/'LICENSE').read_bytes(),'THIRD_PARTY.md':(root/'THIRD_PARTY.md').read_bytes()}
    tar=subprocess.check_output(['git','archive','--format=tar',f'--prefix=K-UI-native-worker-{revision}/','HEAD'],cwd=root)
    files[f'source/K-UI-native-worker-{revision}.tar.gz']=gzip.compress(tar,compresslevel=6,mtime=0)
    files['SHA256SUMS.txt']=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    output=build/'K-UI-Native-Worker-Test.zip'
    with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as z:
        for name,data in files.items():z.writestr(name,data)
    with zipfile.ZipFile(output) as z:
        if set(z.namelist())!=set(files) or any(z.read(name)!=data for name,data in files.items()):raise ValueError('ZIP verification failed')
    print(json.dumps({'zip':str(output),'bytes':output.stat().st_size,'commit':revision,'module_sha256':report['module_sha256']}))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);args=p.parse_args();package(args.build)
if __name__=='__main__':main()
