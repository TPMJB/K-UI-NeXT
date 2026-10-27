#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package one audited, privately prepared Armada CE writable VMU experiment."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from check_armada_ce_vmu_a2_package import check as native_check
from check_loader_layout import inspect_elf, region
from retail_package import inspect_retail
from runtime_package import flatten_elf, envelope, verify
from vmu_persist_profile import decode_profile, template_profile

ROOT=Path(__file__).resolve().parents[1]
INSTALL='KUI/apps/games/ce-probe.kui'
BASELINE_BUILD='af18576dba18'
BASELINE_MODULE_SHA='58ef7fc2b09fbc55735a4c84c8d21456943a8b2f6017445b0196d3101449c5d2'

def digest(data):
    return hashlib.sha256(data).hexdigest()

def require(ok,why):
    if not ok:raise ValueError(why)

def git(*args):
    return subprocess.check_output(['git',*args],cwd=ROOT)

def selected_profile(wire):
    profile=decode_profile(wire)
    require(profile['pair_path']=='/KUI/vmu-test/p0001',
            'This console guide selects only the existing p0001 prepared pair')
    require(wire!=template_profile(),'The public CI template is not a private card preparation')
    return profile

def build(args):
    directory=args.directory.resolve()
    commit=git('rev-parse','HEAD').decode().strip()
    require(not git('status','--porcelain'),'Commit source before making a console package')
    stamp=commit[:12]
    source_stamp_bytes=(directory/'entry.compile.json').read_bytes()
    source_stamp=json.loads(source_stamp_bytes)
    require(source_stamp['commit']==commit and source_stamp['source_dirty'] is False,
            'Native build is not stamped with this clean source commit')
    elf=(directory/'entry.elf').read_bytes()
    require(source_stamp['elf_sha256']==digest(elf),'Built entry changed after its compile stamp')
    native=native_check(directory)
    adapter=native['runtime_a2_adapter']
    require(native.get('write_runtime') and native.get('sd_io') and
            adapter.get('write_commands_enabled') and not adapter.get('read_only_backend'),
            'A source-specific writable runtime audit is required')
    require(adapter['diagnostic_mode']==0,'Publication must be enabled')
    require(adapter['automatic_terminal_capture'],'Writable console test requires automatic fault capture')
    wire=args.profile.read_bytes()
    profile=selected_profile(wire)
    stage=inspect_elf((directory/'stage.elf').read_bytes(),0x8ce10000,0x8cfc4000)
    start=stage['symbols'].get('__vmu_sd_profile_blob_start',0)
    end=stage['symbols'].get('__vmu_sd_profile_blob_end',0)
    require(start%4==0 and end-start==4096 and
            region(stage['payload'],start-0x8ce10000,4096,'embedded private profile')==wire,
            'Built stage does not contain the explicitly selected private P2 profile')
    payload,memory=flatten_elf(elf)
    module=envelope(payload,memory,stamp)
    info=verify(module)
    inspect_retail(module,ce=True)
    with zipfile.ZipFile(args.baseline) as prior:
        require(prior.testzip() is None,'Baseline ZIP CRC failed')
        baseline=prior.read(INSTALL)
        require(digest(baseline)==BASELINE_MODULE_SHA and
                verify(baseline)['build']==BASELINE_BUILD,'Wrong accepted baseline module')
    files={
        INSTALL:module,
        'restore-baseline/'+INSTALL:baseline,
        'README-FIRST.md':(ROOT/'docs/armada-ce-vmu-write-console-test.md').read_bytes(),
        'LICENSE':(ROOT/'LICENSE').read_bytes(),
        'source/K-UI-Armada-CE-VMU-Write-'+stamp+'.tar.gz':
            gzip.compress(git('archive','--format=tar','--prefix=K-UI-Armada-CE-VMU-Write/',commit),mtime=0),
        'evidence/native-audit.json':(json.dumps(native,indent=2)+'\n').encode(),
        'evidence/module-envelope.json':(json.dumps(info,indent=2)+'\n').encode(),
        'native-proof/entry.compile.json':source_stamp_bytes,
    }
    for path in [ROOT/'THIRD_PARTY.md',*sorted((ROOT/'LICENSES').glob('*'))]:
        if path.is_file():files[path.relative_to(ROOT).as_posix()]=path.read_bytes()
    for name in ('armada-ce-vmu-write-integration.md','armada-ce-vmu-runtime-stage.md',
                 'armada-ce-vmu-write-layout.md'):
        files['docs/'+name]=(ROOT/'docs'/name).read_bytes()
    for name in ('armada-ce-maple-write-owner.json','armada-af18576-cached-readonly-console.json',
                 'armada-ce-vmu-runtime-stage.json','armada-ce-vmu-write-runtime.json',
                 'armada-ce-vmu-loading-freeze.json','armada-ce-vmu-ce-wait-abi.json',
                 'armada-ce-vmu-dma-replay.json'):
        files['evidence/'+name]=(ROOT/'docs/evidence'/name).read_bytes()
    for specification in args.evidence:
        require('=' in specification,'Evidence must be NAME=PATH')
        name,path=specification.split('=',1)
        require(name and '/' not in name and '\\' not in name and name not in ('.','..'),
                'Evidence name must be a simple filename')
        path=Path(path)
        require(path.suffix in ('.json','.txt','.log') and path.stat().st_size<=4*1024*1024,
                'Evidence must be bounded JSON/text')
        require('evidence/'+name not in files,'Duplicate evidence')
        files['evidence/'+name]=path.read_bytes()
    # Keep the actual linked proof and its static call graphs. The personalized
    # stage includes this user's P2 identity; it is never put in source/Git.
    for name in ('entry.elf','hook.elf','stage.elf','resident-scia.elf','bind-probe.elf'):
        files['native-proof/'+name]=(directory/name).read_bytes()
    for extension in ('*.ci','*.su'):
        for path in directory.rglob(extension):
            files['native-proof/'+path.relative_to(directory).as_posix()]=path.read_bytes()
    build_info={
        'source_commit':commit,'build':stamp,'source_dirty':False,
        'hardware_tested':False,'physical_save_load_recovery_verified':False,
        'virtual_slot':'A2','prepared_pair':profile['pair_path'],
        'private_profile_sha256':digest(wire),'sd_transport':'SCI','reader':'ASYNC',
        'module_sha256':digest(module),'module_bytes':len(module),
        'accepted_baseline_build':BASELINE_BUILD,'baseline_module_sha256':digest(baseline),
        'installed_files':[INSTALL],'universal_hook_detection':False,'physical_gd_boot':False,
        'scope':'Exact original US Armada CE; prepared existing card, durable write experiment',
        'privacy':'Module and native stage proof contain this owner\'s SD preparation identity; source does not',
        'members':{name:{'bytes':len(data),'sha256':digest(data)} for name,data in files.items()}
    }
    files['BUILD.json']=(json.dumps(build_info,indent=2)+'\n').encode()
    files['SHA256SUMS.txt']=''.join(digest(data)+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    require(git('rev-parse','HEAD').decode().strip()==commit and
            not git('status','--porcelain'),'Source changed while making the console package')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    require(not args.output.exists(),'Output already exists; choose a new artifact name')
    with zipfile.ZipFile(args.output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for name,data in files.items():archive.writestr(name,data)
    with zipfile.ZipFile(args.output) as archive:
        require(archive.testzip() is None,'Output ZIP CRC failed')
        require(archive.read(INSTALL)==module and
                archive.read('restore-baseline/'+INSTALL)==baseline,'Output modules differ')
    print(json.dumps({'file':str(args.output.resolve()),'bytes':args.output.stat().st_size,
        'build':stamp,'module_sha256':digest(module),'prepared_pair':profile['pair_path'],
        'hardware_tested':False,'verified':True}))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--profile',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--evidence',action='append',default=[])
    args=parser.parse_args()
    try:build(args)
    except (OSError,ValueError,KeyError,zipfile.BadZipFile) as error:
        parser.exit(1,'Writable CE package refused: '+str(error)+'\n')

if __name__=='__main__':main()
