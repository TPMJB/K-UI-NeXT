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
from vmu_native_cache_seed import template_seed,record_metadata,DETECTOR_EPOCH
from vmu_native_mode import template_mode,MODE_BYTES


def sha(data):
    return hashlib.sha256(data).hexdigest()


def cache_catalog(root,commit):
    """Package reviewed metadata fixtures only; never copy external game bytes."""
    root=Path(root)
    evidence=json.loads((root/'docs/evidence/native-cache-catalog-inputs.json').read_bytes())
    if (evidence.get('schema')!='kui.native.resolution-cache.inputs.v1' or
        evidence.get('detector_epoch')!=DETECTOR_EPOCH or
        {entry.get('slug') for entry in evidence.get('inputs',[])}!={'ps2','skies','sa2','grandia2','evolution1'} or
        len(evidence['inputs'])!=5):
        raise ValueError('Requires the five reviewed native cache fixtures')
    files={};games=[]
    for entry in evidence['inputs']:
        slug=entry['slug'];fixture=f'tests/fixtures/native-cache/{slug}.nvr'
        if entry['record_fixture']!=fixture:raise ValueError('Unexpected cache fixture path')
        record=(root/fixture).read_bytes()
        metadata=record_metadata(record,entry['record_digest'])
        for field in ('record_bytes','record_sha256','image_sha256','image_bytes','load_address'):
            if entry[field]!=metadata[field]:raise ValueError('Cache fixture/evidence mismatch: '+field)
        path=f'native-cache/{slug}.nvr';files[path]=record
        games.append(dict(slug=slug,title=entry['title'],record_path=path,
            **{key:metadata[key] for key in ('record_sha256','record_digest','image_sha256','image_bytes','load_address')}))
    catalog=dict(schema='kui.native.resolution-cache.catalog.v1',source_commit=commit,
        detector_epoch=DETECTOR_EPOCH,games=games)
    wire=(json.dumps(catalog,indent=2)+'\n').encode()
    files['native-cache/catalog.json']=wire
    return wire,files


def package(build,guide='broader'):
    root=Path(__file__).resolve().parents[1];build=Path(build)
    def git(*args):
        return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
    commit=git('rev-parse','HEAD');revision=commit[:12]
    if git('status','--porcelain'):
        raise ValueError('Commit source changes before packaging')
    report=check(build,self_test=True)
    cached=report['worker_load_policy']['cached_code']
    seed=report.get('resolution_cache_seed',{})
    compat=seed.get('enabled',False)
    flavor='compat' if compat else ('performance' if cached else 'persistent')
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
    extra={}
    if compat:
        seed_offset=seed['seed_module_offset'];seed_template=template_seed()
        if (seed['seed_bytes']!=len(seed_template) or
            module[seed_offset:seed_offset+len(seed_template)]!=seed_template or
            seed['empty_template_sha256']!=sha(seed_template) or
            not (seed_offset+len(seed_template)<=offset or seed_offset>=offset+4096)):
            raise ValueError('Cache seed differs from the audited disjoint canonical template')
        catalog,extra=cache_catalog(root,commit)
        manifest.update(cache_seed_module_offset=seed_offset,cache_seed_bytes=len(seed_template),
            cache_seed_sha256=sha(seed_template),cache_record_bytes=1024,
            cache_detector_epoch=DETECTOR_EPOCH,cache_catalog_path='native-cache/catalog.json',
            cache_catalog_sha256=sha(catalog))
        extra['tools/vmu_native_cache_seed.py']=(root/'tools/vmu_native_cache_seed.py').read_bytes()
        for name in ('vmu-feature-comparison.md','vmu-native-resolution-cache.md',
                     'vmu-native-launch-cache.md','vmu-native-runtime-cost.md',
                     'native-heap-poll-identity.md','vmu-native-latency-test.md',
                     'vmu-native-compat-test.md','vmu-native-optimization-test.md',
                     'vmu-performance-design-review-2026-10-22.md','vmu-next-stages.md',
                     'vmu-native-broader-test.md','vmu-native-evolution-context-test.md',
                     'vmu-native-evolution-fault-test.md'):
            extra['docs/'+name]=(root/'docs'/name).read_bytes()
        for name in ('native-sa2-latency-2026-10-22.json','native-optimization-sh-2026-10-22.json',
                     'native-performance-acceptance-2026-10-23.json','native-uniform-root-family-2026-10-23.json',
                     'native-recv-ownership-2026-10-23.json','native-older-sdk-2026-10-23.json',
                     'native-broader-compatibility-2026-10-23.json','native-evolution1-layout-2026-10-23.json',
                     'native-allocator-family-2026-10-23.json','native-evolution-context-2026-10-23.json',
                     'native-evolution-fault-2026-10-23.json'):
            extra['docs/evidence/'+name]=(root/'docs/evidence'/name).read_bytes()
        extra['docs/native-sdk-family-corpus.json']=(root/'docs/evidence/native-sdk-family-corpus-2026-10-22.json').read_bytes()
        extra['docs/native-installer-stack-regression.json']=(root/'docs/evidence/native-sa2-installer-stack-2026-10-22.json').read_bytes()
        extra['TEST-RESULTS.csv']=(
            'game,build,profile,cache_message,bootloader_seconds,startup_after_bootloader_seconds,fmv_audio,gameplay,save,poweroff_reload,controllers,notes\n'
            'Grandia II,,,,,,,,,,,\nEvolution,,,,,,,,,,,\nSkies of Arcadia,,,,,,,,,,,\nSonic Adventure 2,,,,,,,,,,,\nPower Stone 2,,,,,,,,,,,\n').encode()
    mode=report.get('runtime_mode',{})
    if mode.get('enabled',False):
        mode_offset=mode['module_offset'];mode_template=template_mode()
        if (mode['bytes']!=MODE_BYTES or mode['template_sha256']!=sha(mode_template) or
            mode_offset<64 or mode_offset&3 or mode_offset+MODE_BYTES>len(module) or
            module[mode_offset:mode_offset+MODE_BYTES]!=mode_template or
            not (mode_offset+MODE_BYTES<=offset or offset+4096<=mode_offset) or
            (compat and not (mode_offset+MODE_BYTES<=seed_offset or seed_offset+len(seed_template)<=mode_offset))):
            raise ValueError('Runtime mode differs from its audited disjoint canonical template')
        manifest.update(runtime_mode_module_offset=mode_offset,runtime_mode_bytes=MODE_BYTES,
            runtime_mode_sha256=sha(mode_template))
        extra['tools/vmu_native_mode.py']=(root/'tools/vmu_native_mode.py').read_bytes()
    report.update(source_commit=commit,source_build_id=revision,console_execution_verified=False,
        generic_template_is_owner_profile=False,personalization_required=True,pair_limit=180000,
        profile=dict(embedded_address=f'0x{address:08x}',module_offset=offset,bytes=4096,
            template_sha256=sha(template)),
        compatibility_claim=('Evolution black-screen failure-path diagnostic; no console fix established'
            if guide=='evolution-fault' else
            'Evolution first-transfer failure-witness diagnostic; no console fix established'
            if guide=='evolution-context' else 'Save/load experiment for the reviewed native SDK family'))
    branch=git('branch','--show-current');bundle=(build/f'native-{flavor}-source.bundle').resolve()
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
        'README-FIRST.md':(root/(f'docs/vmu-native-{guide}-test.md' if compat else
                                f'docs/vmu-native-{flavor}-test.md')).read_bytes(),
        'probe-report.json':(json.dumps(report,indent=2)+'\n').encode(),
        'LICENSE':(root/'LICENSE').read_bytes(),
        'THIRD_PARTY.md':(root/'THIRD_PARTY.md').read_bytes(),
        f'source/K-UI-native-{flavor}-{revision}.bundle':bundle.read_bytes(),
    }
    files.update(extra)
    files['SHA256SUMS.txt']=''.join(sha(data)+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    output=build/('K-UI-Native-Compatibility-Test.zip' if compat else
        ('K-UI-Native-Performance-Test.zip' if cached else 'K-UI-Native-Persistent-Test.zip'))
    with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items():archive.writestr(name,data)
    with zipfile.ZipFile(output) as archive:
        if set(archive.namelist())!=set(files) or any(archive.read(name)!=data for name,data in files.items()):
            raise ValueError('ZIP byte verification failed')
    print(json.dumps(dict(zip=str(output),bytes=output.stat().st_size,commit=commit,
        module_sha256=sha(module),profile_module_offset=offset)))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('build',type=Path)
    parser.add_argument('--guide',choices=('broader','evolution-context','evolution-fault'),default='broader')
    try:
        args=parser.parse_args();package(args.build,args.guide)
    except (OSError,ValueError,KeyError,AssertionError,subprocess.CalledProcessError) as error:
        parser.exit(2,f'Native persistent package refused: {error}\n')
