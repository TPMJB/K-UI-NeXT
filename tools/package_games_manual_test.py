#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Publish an exact Games-only manual-copy test after complete qualification.

No update package, activation controls or user files are created. A later
metadata-only producer commit never relabels the actual compiled binary source.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = next(p for p in Path(__file__).resolve().parents if (p/'src/apps/games_retail.c').is_file())
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
from global_loader_rollout import DEFAULT_BUILDS, INSTALL_TARGETS, wire
from package_universal_loader_test import accepted_smash, SMASH_TARGETS, CE_PROBE, ACCEPTED_BUILD
from package_smash_collection_update import linked_member, linked_bundle, STAGES
from build_runtime_module_bundle import locate, decode
from runtime_package import verify
from check_shared_loader_performance import module, runtime_standard_manifest_binding
from native_cdda_manifest import PROFILES as CDDA_PROFILES, runtime_binding
from package_native_vmu_only import bind_profile
from package_physical_disc_test import collect_vmu_build
from check_focused_loader_delivery import native_embedding
from check_native_compact_delivery import component_equivalence
from check_vmu_native_worker_gateway import read_elf
from test_native_stage_discovery_scope import audit as discovery_audit
from test_smash_packaged_admission import audit as smash_admission

BINARY_BASELINE = '7fca3e37f40557066116e88ab817e91ef7dd50d3'
WORKERS = {
    'standard':'d81b8efd84431ed25314afc4bd9af06bc79bd418fccd27077c697d4149c22049',
    'background_a2':'d7ea9f9c661f06aa2ebf0f805e4edaab87e89f4f217ece64d9355dfd304a1ed1',
    'background_a1':'4cc655430dc591be1d6f18b343d22ff822e9a2d73ad55e6a44f723181c2b3f86',
    'disc_a2':'d8eedf0f71a4b2bd0b3b1d8bca50bd22fbea5e0cf848d642beb43ea3ea271e43',
    'disc_a1':'4271f10890c79efb9e82efdacf63d6788592a5506591360f0c3541487f509cb3',
}
SERVICES = {
    'standard':'KUI/apps/games/retail-boot.kui',
    'background_a2':'KUI/apps/games/retail-native-dual.kui',
    'background_a1':'KUI/apps/games/retail-native-dual-a1.kui',
    'disc_a2':'KUI/apps/disc/native-disc-vmu.kui',
    'disc_a1':'KUI/apps/disc/native-disc-vmu-a1.kui',
}
TARGETS = frozenset((INSTALL_TARGETS-{CE_PROBE}) | set(SERVICES.values()) | SMASH_TARGETS |
                    {'KUI/runtime.kui','KUI/boot.kui'})
RESOURCE_NAMES = ('preserved-standard-manifest.h','preserved-standard-manifest.json',
                  'native-cdda-manifest.h','native-cdda-manifest.json')


def require(ok, message):
    if not ok: raise ValueError(message)


def sha(data): return hashlib.sha256(data).hexdigest()


def git(*args): return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()


class Inputs:
    def __init__(self): self.hashes={}; self.trees={}
    def read(self,path):
        path=Path(path).absolute()
        require(path.is_file() and not path.is_symlink() and not any(p.is_symlink() for p in path.parents),
                'Input must be an ordinary file: '+str(path))
        data=path.read_bytes(); digest=sha(data)
        require(str(path) not in self.hashes or self.hashes[str(path)]==digest,
                'Input changed during qualification: '+str(path))
        self.hashes[str(path)]=digest; return data
    def tree(self,path):
        path=Path(path).absolute();require(path.is_dir() and not path.is_symlink(),'Missing input directory: '+str(path))
        self.trees[str(path)]={str(p) for p in path.rglob('*') if p.is_file()}
        for p in sorted(path.rglob('*')):
            require(not p.is_symlink(),'Symlink in qualified input tree: '+str(p))
            if p.is_file(): self.read(p)
    def recheck(self):
        for name,files in self.trees.items():
            require({str(p) for p in Path(name).rglob('*') if p.is_file()}==files,'Qualified input tree changed: '+name)
        for name,digest in self.hashes.items():
            path=Path(name)
            require(path.is_file() and not path.is_symlink() and sha(path.read_bytes())==digest,
                    'Input changed before publication: '+name)


def source_state(binary, resources):
    head=git('rev-parse','HEAD'); tree=git('rev-parse','HEAD^{tree}')
    require(not git('status','--porcelain'),'Final manual delivery requires clean source')
    require(re.fullmatch('[0-9a-f]{40}',binary or ''),'Use the complete actual binary source commit')
    subprocess.run(['git','merge-base','--is-ancestor',binary,head],cwd=ROOT,check=True)
    changed=git('diff','--name-only',binary,head).splitlines()
    allowed={'resources/updater-bindings/'+name for name in RESOURCE_NAMES}
    require(all(name in allowed or name.startswith(('tools/','tests/','docs/')) or
                name in ('AGENTS.md','README.md','CHANGELOG.md') for name in changed),
            'Production source or recipes changed after the binary build')
    for name,data in resources.items():
        require((ROOT/'resources/updater-bindings'/name).read_bytes()==data,
                'Default resource differs from actual qualified GUI input: '+name)
    return dict(tooling_commit=head,tooling_tree=tree,binary_source_commit=binary,
                binary_source_tree=git('rev-parse',binary+'^{tree}'))


def inventory(files):
    require(len(TARGETS)==28 and set(files)==TARGETS,'Manual Games delivery must contain exactly28 software files')
    require(files['KUI/boot.kui']==files['KUI/runtime.kui'],'Physical boot/runtime GUI aliases differ')
    for name,data in files.items():
        p=PurePosixPath(name)
        require(str(p)==name and not p.is_absolute() and '..' not in p.parts and '\\' not in name,
                'Unsafe software member')
        if name.endswith('.kui'): verify(data)
        else: require(name in SMASH_TARGETS and name.endswith('-helper.bin') and len(data)==49152,
                      'Unexpected raw or user data member')
    return {name:dict(bytes=len(data),sha256=sha(data),**({'runtime':verify(data)} if name.endswith('.kui') else {'kind':'pinned accepted raw helper'})) for name,data in sorted(files.items())}


def archive_matches(path,files):
    require(set(files)==TARGETS|{'README-FIRST.txt','BUILD.json','LICENSE.txt'},'Manual archive must contain28 software files and exactly3 release documents')
    with zipfile.ZipFile(path) as z:
        require(len(z.namelist())==len(files) and set(z.namelist())==set(files),
                'Manual archive contains missing, duplicate or unauthorized members')
        require(z.testzip() is None,'Manual archive CRC failed')
        require(all(z.read(name)==body for name,body in files.items()),'Manual archive software changed')


def artifact(directory,name):
    relative=PurePosixPath(name)
    require(str(relative)==name and not relative.is_absolute() and '..' not in relative.parts and '\\' not in name,
            'Unsafe qualification artifact path')
    return directory.joinpath(*relative.parts)


def family_report(role,directory,path,body,commit,inputs):
    report=json.loads(inputs.read(path));require(report.get('status')=='PASS','Family safety did not pass: '+role)
    if role.startswith('disc_'):
        require(report.get('source_commit')==commit and report.get('source_dirty') is False and
                report.get('module_sha256')==sha(body) and all(report.get(k) is True for k in
                ('entry_stage_embedding_checked','worker_support_relocations_checked','current_clean_component_receipts_checked')),
                'Physical-disc safety is stale/incomplete: '+role)
        hashes=report.get('artifacts_sha256',{})
        require({'artifacts/disc-vmu/'+n for n in ('disc-entry.elf','disc-stage.elf','native-worker.elf','support/support.elf')}<=set(hashes), 'Disc safety omitted an owner')
        for name,digest in hashes.items():
            prefix='artifacts/disc-vmu/';require(name.startswith(prefix),'Unknown disc safety owner')
            require(sha(inputs.read(artifact(directory,name[len(prefix):])))==digest,'Disc safety artifact changed')
    else:
        provenance=report.get('source_provenance',{})
        require(provenance.get('commit')==commit and provenance.get('source_dirty') is False and
                provenance.get('elf_sha256')==sha(inputs.read(directory/'entry.elf')),'Family safety source is stale: '+role)
        if role=='standard':
            require(report.get('schema')=='kui.native.vmu-only.linked.v1' and report.get('module_sha256')==sha(body) and
                    report.get('lease_bytes')==73728 and report.get('sci_rx_policy')=='paced',
                    'Standard safety/geometry mismatch')
            native_embedding(directory,commit)
        else:
            require(report.get('schema')=='kui.compact.next-test.qualification.v1' and
                    report.get('canonical_profile',{}).get('module_sha256')==sha(body) and
                    report.get('exact_entry_stage_worker_dual_reader_auxiliary_embed_chain') is True,
                    'Compact safety is stale/incomplete')
            geometry=report.get('geometry',{})
            require([geometry.get(k) for k in ('worker_bytes','lease_bytes','cache_bytes','worker_stack_bytes')]==
                    [73728,290816,196608,3072],'Compact reservation changed')
            hashes=report.get('artifact_sha256',{})
            require(set(hashes)=={x+'.elf' for x in ('entry','stage','native-worker','resident-sci','resident-legacy','native-ra-aux')},
                    'Compact safety omitted an owner')
            for name,digest in hashes.items():require(sha(inputs.read(artifact(directory,name)))==digest,'Compact safety artifact changed')
    for name,digest in report.get('source_sha256',{}).items():
        require(not Path(name).is_absolute() and '..' not in Path(name).parts and
                sha(inputs.read(ROOT/name))==digest,'Family audit source changed')
    return report


def qualify(args):
    inputs=Inputs(); inputs.read(__file__)
    coldroot=Path(args.cold_rollout);inputs.tree(coldroot)
    cold=json.loads(inputs.read(coldroot/'evidence/global-loader-rollout.json'))
    commit=args.binary_source_commit
    require(cold.get('status')=='PASS' and cold.get('source_commit')==commit and cold.get('source_dirty') is False and
            set(cold.get('installed_files',[]))==INSTALL_TARGETS and set(cold.get('profiles',{}))==set(DEFAULT_BUILDS),'Missing/stale complete cold rollout')
    files={name:inputs.read(coldroot/name) for name in INSTALL_TARGETS}
    for data in files.values(): verify(data)
    require(cold.get('installed_sha256')=={name:sha(data) for name,data in sorted(files.items())},'Cold rollout bytes changed')
    for role,row in cold['profiles'].items():
        proof=row['compile']['provenance']
        require(proof.get('commit')==commit and proof.get('source_dirty') is False,'Cold role source differs: '+role)
        receipt=json.loads(inputs.read(coldroot/('evidence/global-loader-rollout/'+role+'-entry.compile.json')))
        require(receipt==proof,'Cold role receipt changed: '+role)
    standard_header=inputs.read(args.standard_header);standard_manifest=inputs.read(args.standard_manifest)
    cdda_header=inputs.read(coldroot/'evidence/native-cdda-manifest.h')
    cdda_manifest=inputs.read(coldroot/'evidence/native-cdda-manifest.json')
    resources=dict(zip(RESOURCE_NAMES,(standard_header,standard_manifest,cdda_header,cdda_manifest)))
    initial=source_state(commit,resources)
    for name in resources: inputs.read(ROOT/'resources/updater-bindings'/name)
    runtime=Path(args.runtime);elf=Path(args.runtime_elf)
    for path in (runtime,elf,elf.with_suffix('.compile.json'),args.accepted_smash,args.shared_report):inputs.read(path)
    gui,gui_link=linked_member(runtime,elf,commit)
    preserved=accepted_smash(args.accepted_smash)
    offset,bundle=locate(gui[64:]);decoded=decode(bundle)
    require(bundle[32:44]==ACCEPTED_BUILD.encode() and set(decoded)==set(STAGES) and
            all(decoded[name]==preserved['KUI/'+name] for name in STAGES),'GUI does not contain exact accepted Smash bytes')
    bundle_proof=linked_bundle(elf,gui[64:],offset,bundle,gui_link['elf_sha256'])
    shared=json.loads(inputs.read(args.shared_report))
    require(shared.get('status')=='PASS' and shared.get('commit')==commit and shared.get('allow_dirty') is False and
            set(shared.get('cohort',{}))==set(SERVICES) and shared.get('runtime',{}).get('elf_sha256')==sha(inputs.read(elf)),
            'Shared native qualification is stale or incomplete')
    require(bool(shared.get('proof_python_sha256')),'Shared qualification omitted its model source fingerprints')
    for name,digest in shared['proof_python_sha256'].items():
        require(not Path(name).is_absolute() and '..' not in Path(name).parts and
                sha(inputs.read(ROOT/name))==digest,'Shared model source changed')
    family,workers,discovery,profiles={}, {}, {}, {}
    builds={role:Path(getattr(args,role)) for role in SERVICES}
    for role,directory in builds.items():
        inputs.tree(directory)
        _,bound=module(directory,role,commit,False)
        row=shared['cohort'][role]
        require(row.get('module_sha256')==bound['module_sha256'] and
                all(sha(inputs.read(artifact(directory,name)))==digest for name,digest in row['artifacts_sha256'].items()),
                'Shared native artifact changed: '+role)
        if role.startswith('disc_'):
            body,_=collect_vmu_build(directory,commit[:12],commit)
        else:
            alias='retail-native-vmu-lite-paced.kui' if role=='standard' else 'retail-native-background-compact.kui'
            body,profiles[role]=bind_profile(directory,commit,alias)
        require(sha(body)==bound['module_sha256'],'Service module binding differs')
        files[SERVICES[role]]=body
        safety=Path(args.safety_dir)/(role.replace('_','-')+'-safety.json' if not role.startswith('disc_') else role+'-safety.json')
        family[role]=family_report(role,directory,safety,body,commit,inputs)
        reference=Path(args.worker_reference_dir)/role/'native-worker.elf';inputs.read(reference)
        workers[role]=component_equivalence(read_elf(directory/'native-worker.elf'),read_elf(reference),
            reference_sha256=WORKERS[role],old_build=BINARY_BASELINE[:12],new_build=commit[:12])
        if not role.startswith('disc_'):discovery[role]=discovery_audit(directory/'stage.elf')
    manifest=json.loads(standard_manifest)
    require(manifest.get('source_commit')==commit and manifest.get('source_dirty') is False and
            manifest.get('module_sha256')==sha(files[SERVICES['standard']]) and
            manifest.get('header_sha256')==sha(standard_header),'Standard manifest is stale')
    payload=files[SERVICES['standard']][64:];slot=manifest['profile_payload_offset']
    require(slot==profiles['standard']['profile_module_offset']-64,'Standard mutable profile owner changed')
    require(set(manifest.get('artifacts_sha256',{}))=={'entry.elf','stage.elf','native-worker.elf','resident-sci.elf','retail-native-vmu-lite-paced.kui'}, 'Standard manifest omitted an owner')
    for name,digest in manifest['artifacts_sha256'].items():
        require(sha(inputs.read(artifact(builds['standard'],name)))==digest,'Standard manifest artifact changed')
    admission=json.loads(inputs.read(Path(args.safety_dir)/'standard-cohort-admission.json'))
    require(admission.get('status')=='PASS' and admission.get('manifest',{}).get('module_sha256')==sha(files[SERVICES['standard']]) and
            admission['manifest'].get('normalized_payload_sha256')==manifest.get('normalized_payload_sha256') and
            admission.get('current_module_only') is True and admission.get('no_installed_module_self_authorization') is True,
            'Standard production admission report is stale/incomplete')
    for name,digest in admission.get('source_sha256',{}).items():
        require(not Path(name).is_absolute() and '..' not in Path(name).parts and sha(inputs.read(ROOT/name))==digest,'Standard production admission source changed')
    require(0<=slot<=len(payload)-4096 and manifest.get('normalized_payload_sha256')==
            sha(payload[:slot]+bytes(4096)+payload[slot+4096:]),'Standard normalized identity changed')
    standard_binding=runtime_standard_manifest_binding(read_elf(elf,allow_tls=True),manifest['normalized_payload_sha256'])
    cdda=json.loads(cdda_manifest)
    require(cdda.get('source_commit')==commit and cdda.get('source_dirty') is False and
            cdda.get('header_sha256')==sha(cdda_header),'Native CDDA manifest is stale')
    require(set(cdda.get('profiles',{}))==set(CDDA_PROFILES),'Native CDDA manifest omitted a profile')
    for profile,(_,_,old,new) in CDDA_PROFILES.items():
        body=files['KUI/apps/games/'+old];require(body==files['KUI/apps/games/'+new],'Native CDDA aliases differ')
        header=verify(body);row=cdda['profiles'][profile]
        require(row.get('build')==header['build']==commit[:12] and row.get('payload_bytes')==header['payload_bytes'] and
                row.get('crc32')==header['crc32'] and row.get('payload_sha256')==sha(body[64:]) and row.get('module_sha256')==sha(body),
                'Native CDDA manifest does not bind actual module bytes: '+profile)
    cdda_binding=runtime_binding(elf,cdda)
    require(cold.get('runtime_native_CDDA_binding')==cdda_binding,'Cold rollout does not bind this actual GUI CDDA table')
    files.pop(CE_PROBE);files.update(preserved);files['KUI/runtime.kui']=files['KUI/boot.kui']=gui
    install=inventory(files);inputs.recheck();require(source_state(commit,resources)==initial,'Source changed during qualification')
    return files,dict(schema='kui.games-manual-test.v1',status='PASS',**initial,GUI=gui_link,
        source_url='https://github.com/TacitusVox/K-UI-NeXT/tree/'+commit,
        tooling_source_url='https://github.com/TacitusVox/K-UI-NeXT/tree/'+initial['tooling_commit'],
        accepted_Smash=dict(build=ACCEPTED_BUILD,exact_six_files=True,exact_embedded_four=True,linked_bundle=bundle_proof),
        Standard_GUI_binding=standard_binding,Standard_production_admission=admission,native_CDDA_GUI_binding=cdda_binding,
        worker_reference_commit=BINARY_BASELINE,worker_preservation=workers,new_discovery_scopes=discovery,
        family_safety=family,install=install,software_file_count=28,KUP_created=False,
        activation_records_written=False,user_files_packaged=False,personalized_ce_probe_packaged=False,
        inherited_root_ce_probe_required_for_ordinary_CE=True,hardware_timing_measured=False),inputs,resources


def guide(report):
    return ('Games-only manual-copy test\n\nCopy the supplied KUI folder over the SD installation, replacing only its28 files. '
        'Keep existing banks, games, VMUs, saves and settings. No KUP is included.\n\n'
        'Before booting this manual test, move KUI/system/active0.bin and KUI/system/active1.bin '
        '(whichever exist) into a separate backup folder outside KUI/system. Keep both files; do not delete them. '
        'This uses the existing no-selection ROOT behavior so a saved software bank cannot mask the copied files. '
        'It remains effective across restart/return without timing a controller button. Restore those exact files '
        'to their original paths to reselect the previous bank. The ZIP never writes activation records. '
        'Verify the reported GUI build '+report['binary_source_commit'][:12]+' and the final reader path/build/CRC/bytes '
        'in the Games launch timing or last-launch report before comparing game loading times.\n\n'
        'The archive deliberately omits ce-probe.kui to preserve the existing personalized Armada adapter. '
        'Ordinary WinCE inherits whatever compatible module is already at physical KUI/apps/games/ce-probe.kui; '
        'that route does not receive the new cold-stage changes in this test. A CE module present only in a '
        'saved bank is unavailable while testing copied ROOT files. The four supplied CE/CDDA backends are '
        'separately rebuilt and verified.\n\n'
        'Accepted Smash files remain exact. Standard retains72KiB; Background retains284KiB including192KiB cache; physical-disc VMU retains80KiB. Service geometry is unchanged. Compare Games preparation and bootstrap '
        'separately with the same VMU/reader settings. No console speedup is asserted.\n\n'
        'Exact binary source: '+report['source_url']+'\nSource tree: '+report['binary_source_tree']+'\n').encode()


def release_files(software,report,license_bytes):
    inventory(software)
    require(b'GNU GENERAL PUBLIC LICENSE' in license_bytes and b'Version 3' in license_bytes,
            'Missing project GPL license')
    compact={key:report[key] for key in ('tooling_commit','tooling_tree','binary_source_commit','binary_source_tree','source_url')}
    compact.update(schema='kui.games-manual-build.v1',status='PASS',software_file_count=28,
        install=inventory(software),KUP_created=False,activation_records_written=False,user_files_packaged=False,
        inherited_root_ce_probe_required_for_ordinary_CE=True,hardware_timing_measured=False,
        qualification_sha256={name:sha(wire(report.get(name,{}))) for name in
            ('family_safety','worker_preservation','new_discovery_scopes','Standard_GUI_binding','native_CDDA_GUI_binding','accepted_Smash')})
    return dict(software,**{'README-FIRST.txt':guide(report),'BUILD.json':wire(compact),'LICENSE.txt':license_bytes})


def publish(args):
    output,evidence=Path(args.output).absolute(),Path(args.evidence_dir).absolute()
    for path in (output,evidence):
        require(not path.exists() and not path.is_symlink() and not any(p.is_symlink() for p in path.parents),
                'Choose new output/evidence paths; existing files are never overwritten')
    require(output.suffix=='.zip' and not output.is_relative_to(evidence) and not evidence.is_relative_to(output),
            'Manual output/evidence must be separate new paths')
    files,report,inputs,resources=qualify(args)
    license_bytes=inputs.read(ROOT/'LICENSE')
    report['full_input_sha256']=dict(sorted(inputs.hashes.items()))
    released=release_files(files,report,license_bytes)
    output.parent.mkdir(parents=True,exist_ok=True);evidence.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.kui-games-manual-',dir=output.parent) as temporary:
        candidate=Path(temporary)/'candidate.zip'
        with zipfile.ZipFile(candidate,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for name,body in sorted(released.items()):
                item=zipfile.ZipInfo(name,(2026,11,6,0,0,0));item.compress_type=zipfile.ZIP_DEFLATED
                item.external_attr=0o100644<<16;z.writestr(item,body)
        archive_matches(candidate,released)
        packaged=smash_admission(candidate)
        require(packaged.get('status')=='PASS' and not packaged.get('source_dirty'),'Actual packaged Smash admission failed')
        # That helper labels the current toolingHEAD; require the actual GUI
        # source binding above rather than treating a metadata commit as a retag.
        report['actual_packaged_Smash_admission']=packaged
        report.update(zip_sha256=sha(candidate.read_bytes()),zip_bytes=candidate.stat().st_size,
                      full_input_sha256=dict(sorted(inputs.hashes.items())))
        inputs.recheck();require(source_state(args.binary_source_commit,resources)==
            {k:report[k] for k in ('tooling_commit','tooling_tree','binary_source_commit','binary_source_tree')},
            'Source changed before publication')
        require(not output.exists() and not evidence.exists(),'Output appeared during qualification')
        with tempfile.TemporaryDirectory(prefix='.kui-games-evidence-',dir=evidence.parent) as staging:
            stage=Path(staging)/'evidence';stage.mkdir()
            (stage/'QUALIFICATION.json').write_bytes(wire(report))
            stage.rename(evidence)
        # Hardlink publishes atomically and refuses an output created meanwhile.
        os.link(candidate,output)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('runtime','runtime-elf','accepted-smash','cold-rollout','standard-header','standard-manifest',
                 'shared-report','safety-dir','worker-reference-dir','output','evidence-dir'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--binary-source-commit',required=True)
    for role in SERVICES:parser.add_argument('--'+role.replace('_','-'),type=Path,required=True)
    args=parser.parse_args()
    try:
        result=publish(args)
        print(json.dumps(dict(status='PASS',output=str(args.output),software_files=28,sha256=result['zip_sha256'])))
    except (ValueError,OSError,zipfile.BadZipFile,subprocess.CalledProcessError) as error:parser.exit(1,str(error)+'\n')


if __name__=='__main__':main()
