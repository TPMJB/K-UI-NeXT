#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package an audited generic native save test and its exact source checkpoint."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from check_vmu_native_persistent import check
from check_loader_layout import inspect_elf
from personalize_vmu_sd_proof import readonly_blob
from runtime_package import ADDRESS,verify
from vmu_persist_profile import template_profile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def package(build):
    root=Path(__file__).resolve().parents[1];build=Path(build)
    def git(*args):
        return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
    commit=git('rev-parse','HEAD');revision=commit[:12]
    if git('status','--porcelain'):
        raise ValueError('Commit source changes before packaging')
    report=check(build,self_test=True)
    source=report['source_provenance']
    if source['commit']!=commit or source['source_dirty'] or report['runtime']['build']!=revision:
        raise ValueError('Requires a fresh build from the current clean source commit')
    module=(build/'retail-maple-observe.kui').read_bytes()
    stage_data=(build/'stage.elf').read_bytes();stage=inspect_elf(stage_data,0x8ce00000,0x8cfe0000)
    entry=inspect_elf((build/'entry.elf').read_bytes(),ADDRESS,0x8c112000)
    template=template_profile()
    address,_=readonly_blob(stage_data,stage,'__vmu_sd_profile_blob_start',
        '__vmu_sd_profile_blob_end',len(template))
    offset=64+entry['symbols']['__retail_stage_blob_start']-ADDRESS+address-0x8ce00000
    if offset&3 or module[offset:offset+4096]!=template or module.find(template)!=offset or module.find(template,offset+1)!=-1:
        raise ValueError('Module does not contain exactly its typed immutable profile template')
    if verify(module)['build']!=revision:
        raise ValueError('Runtime module/source mismatch')
    manifest=dict(schema='kui.native.persistent.personalization.v1',source_commit=commit,build=revision,
        module_sha256=sha(module),module_bytes=len(module),profile_module_offset=offset,
        profile_bytes=4096,profile_sha256=sha(template),storage_writes_enabled=True)
    report.update(source_commit=commit,source_build_id=revision,console_execution_verified=False,
        generic_template_is_owner_profile=False,personalization_required=True,pair_limit=180000,
        profile=dict(embedded_address=f'0x{address:08x}',module_offset=offset,bytes=4096,
            template_sha256=sha(template)),
        compatibility_claim='Save/load experiment for the reviewed native SDK family')
    branch=git('branch','--show-current');bundle=(build/'native-persistent-source.bundle').resolve()
    subprocess.run(['git','bundle','create',str(bundle),'HEAD',*([branch] if branch else [])],cwd=root,check=True)
    subprocess.run(['git','bundle','verify',str(bundle)],cwd=root,check=True,stdout=subprocess.DEVNULL)
    if git('rev-parse','HEAD')!=commit or git('status','--porcelain'):
        raise ValueError('Source changed during auditing; rebuild before packaging')
    files={
        'base/KUI/apps/games/retail-boot.kui':module,
        'base/native-persistent-personalization.json':(json.dumps(manifest,indent=2)+'\n').encode(),
        'personalize.py':(root/'tools/personalize_vmu_native_persistent.py').read_bytes(),
        'tools/runtime_package.py':(root/'tools/runtime_package.py').read_bytes(),
        'tools/vmu_persist_profile.py':(root/'tools/vmu_persist_profile.py').read_bytes(),
        'README-FIRST.md':(root/'docs/vmu-native-persistent-test.md').read_bytes(),
        'probe-report.json':(json.dumps(report,indent=2)+'\n').encode(),
        'LICENSE':(root/'LICENSE').read_bytes(),
        'THIRD_PARTY.md':(root/'THIRD_PARTY.md').read_bytes(),
        f'source/K-UI-native-persistent-{revision}.bundle':bundle.read_bytes(),
    }
    files['SHA256SUMS.txt']=''.join(sha(data)+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    output=build/'K-UI-Native-Persistent-Test.zip'
    with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items():archive.writestr(name,data)
    with zipfile.ZipFile(output) as archive:
        if set(archive.namelist())!=set(files) or any(archive.read(name)!=data for name,data in files.items()):
            raise ValueError('ZIP byte verification failed')
    print(json.dumps(dict(zip=str(output),bytes=output.stat().st_size,commit=commit,
        module_sha256=sha(module),profile_module_offset=offset)))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('build',type=Path)
    try:package(parser.parse_args().build)
    except (OSError,ValueError,KeyError,AssertionError,subprocess.CalledProcessError) as error:
        parser.exit(2,f'Native persistent package refused: {error}\n')
