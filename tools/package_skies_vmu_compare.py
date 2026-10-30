#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the SCI Skies controller comparison, launch UI and read-only inventory."""
import argparse
import csv
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_native_vmu_only import audit
from install_skies_launch_ui import SCHEMA as GUI_SCHEMA
from install_skies_vmu_compare import SCHEMA
from package_native_vmu_only import ARTIFACTS, bind_profile, checkout_script, sha, wire
from package_vmu_native_dual_delivery import offline_source_recovery_check
from runtime_package import flatten_elf, verify

ROOT=Path(__file__).resolve().parents[1]
SPAN=0x1c000
MODES={'baseline':'split','full-submit':'full'}
GUI_ARTIFACTS=('kui-runtime.elf','kui-runtime.map','kui-runtime.compile.json',
               'runtime.kui','gui-prelaunch-crc-evidence.json')
GUI_SOURCE_EVIDENCE='docs/evidence/skies-standard-prelaunch-crc-2026-10-30.md'


def require(value,message):
    if not value:
        raise ValueError(message)


def git(root,*arguments):
    return subprocess.check_output(['git','-C',str(root),*arguments],text=True).strip()


def clean_source(root):
    root=Path(root).resolve(strict=True)
    require(not git(root,'status','--porcelain','--untracked-files=all'),
            'Commit source changes before packaging: '+str(root))
    return git(root,'rev-parse','HEAD'),git(root,'rev-parse','HEAD^{tree}')


def common_directory(root):
    value=Path(git(root,'rev-parse','--git-common-dir'))
    return (value if value.is_absolute() else Path(root)/value).resolve()


def gui_artifacts(ui_root):
    """Bind the GUI to its own clean source and complete linked load image."""
    ui_root=Path(ui_root).resolve(strict=True)
    commit,tree=clean_source(ui_root)
    build=ui_root/'build'
    files={name:(build/name).read_bytes() for name in GUI_ARTIFACTS}
    provenance=json.loads(files['kui-runtime.compile.json'])
    require(isinstance(provenance,dict) and provenance.get('commit')==commit and
            provenance.get('source_dirty') is False and
            provenance.get('elf_sha256')==sha(files['kui-runtime.elf']),
            'GUI ELF differs from its exact clean compile provenance')
    info=verify(files['runtime.kui'])
    payload,memory=flatten_elf(files['kui-runtime.elf'])
    require(info['build']==commit[:12] and files['runtime.kui'][64:]==payload and
            info['memory_bytes']==memory,
            'GUI runtime differs from its clean linked ELF/source build')
    evidence=json.loads(files['gui-prelaunch-crc-evidence.json'])
    require(isinstance(evidence,dict) and evidence.get('source_commit')==commit and
            evidence.get('elf_sha256')==sha(files['kui-runtime.elf']) and
            evidence.get('module_sha256')==sha(files['runtime.kui']),
            'GUI CRC evidence differs from the shipped source/ELF/runtime')
    source_evidence=(ui_root/GUI_SOURCE_EVIDENCE).read_bytes()
    require(subprocess.check_output(['git','-C',str(ui_root),'show','HEAD:'+GUI_SOURCE_EVIDENCE])==source_evidence,
            'GUI source evidence differs from its committed bytes')
    metadata=dict(schema=GUI_SCHEMA,source_commit=commit,build=commit[:12],
                  module_sha256=sha(files['runtime.kui']))
    result=dict(source_commit=commit,source_tree=tree,build=commit[:12],
        module_sha256=metadata['module_sha256'],elf_sha256=sha(files['kui-runtime.elf']),
        compile_provenance_sha256=sha(files['kui-runtime.compile.json']),
        evidence_sha256=sha(files['gui-prelaunch-crc-evidence.json']),
        artifact_sha256={name:sha(data) for name,data in files.items()},
        console_execution_verified=False)
    return files,source_evidence,metadata,result


def original_comparison(build,revision,original):
    """Report exact byte comparison after replacing only the build ID string.

    A mismatch is evidence of a different binary, not a packaging refusal or
    permission to declare the rebuilt baseline byte-identical to the old ZIP.
    """
    original=Path(original)
    if not original.is_file():
        return dict(available=False,normalized_binary_equal=False,
                    reason='Original accepted ZIP was not available for byte comparison')
    with zipfile.ZipFile(original) as archive:
        def one(suffix):
            names=[name for name in archive.namelist() if name==suffix or name.endswith('/'+suffix)]
            require(len(names)==1,'Original ZIP has no unique '+suffix)
            return archive.read(names[0])
        candidate=json.loads(one('CANDIDATE.json'))
        old_commit=candidate['source_commit']
        require(old_commit.startswith('4edbc56c295b'),'Comparison reference is not the accepted 4edbc56c295b ZIP')
        old_build=old_commit[:12].encode()
        rows={}
        for name in ('resident-sci.bin','stage.bin','native-worker.bin'):
            previous=one('artifacts/sci/'+name)
            current=(Path(build)/name).read_bytes()
            normalized=current.replace(revision.encode(),old_build)
            rows[name]=dict(original_bytes=len(previous),baseline_bytes=len(current),
                original_sha256=sha(previous),baseline_sha256=sha(current),
                replaced_build_strings=current.count(revision.encode()),
                original_build_strings=previous.count(old_build),
                normalized_binary_equal=normalized==previous,
                differing_bytes=sum(a!=b for a,b in zip(normalized,previous))
                    if len(normalized)==len(previous) else None)
    return dict(available=True,original_source_commit=old_commit,
        original_zip_sha256=sha(original.read_bytes()),
        normalization='Replace the new 12-byte ASCII build identifier with 4edbc56c295b only',
        normalized_binary_equal=all(row['normalized_binary_equal'] for row in rows.values()),files=rows)


def test_sheet():
    output=io.StringIO(newline='')
    writer=csv.writer(output)
    writer.writerow(('mode','prepared_profile','VMU_detected','old_save_loaded',
                     'freeze_scene','attempts_or_minutes','A_to_bootstrap1_seconds','notes'))
    for mode in MODES:
        writer.writerow((mode,'','','','','','',''))
    return output.getvalue().encode()


def source_guide(commit,gui_commit):
    return f'''# Exact source checkpoints

Loader source: `{commit}`. Launch UI source: `{gui_commit}`.

Both branches and their complete history are in `source/native-source-full.bundle`.
There are no prerequisite bundles. From the extracted package:

```sh
python3 source/checkout-source.py
git -C restored-source worktree add ../restored-gui {gui_commit}
```

The helper verifies the bundle checksum and recovers the exact loader source.
The GUI worktree is pinned to its separate source commit, without replacing the
loader checkout. Local source edits must be preserved before changing either.

With the existing `sh-elf` toolchain on PATH, the loader builds are:

```sh
make -C restored-source -f Makefile.vmu-native-vmu-only -j4 BUILD=build/skies-vmu-baseline VMU_TRANSPORT=1
make -C restored-source -f Makefile.vmu-native-vmu-only-full-submit -j4 BUILD=build/skies-vmu-full VMU_TRANSPORT=1
```

The GUI uses its existing `Makefile.dc` workflow and KallistiOS/toolchain
dependencies. Its exact ELF, compile provenance and CRC evidence are included in
`artifacts/gui/`. Both loader variants' linked files and audit reports are in
`artifacts/baseline/` and `artifacts/full-submit/`.

This is a diagnostic comparison. It does not establish that Skies' crash is fixed.
'''.encode()


def combined_checkout_script():
    """Retain the checked recovery helper, fetching the sibling GUI as well.

    A checkout recovered from an earlier loader-only package may not contain
    this GUI commit. Fetching only loader HEAD cannot obtain a sibling branch.
    """
    script=checkout_script()
    previous=b"['git','-C',str(dest),'fetch',str(bundle),'HEAD']"
    require(script.count(previous)==1,'Offline helper fetch contract changed')
    script=script.replace(previous,b"['git','-C',str(dest),'fetch',str(bundle),'HEAD',item['gui_source_ref']]")
    previous=b"if git('rev-parse',item['source_commit']+'^{tree}')!=item['source_tree']:\n"
    require(script.count(previous)==1,'Offline helper source-tree contract changed')
    extra=b"if git('rev-parse',item['gui_source_commit']+'^{tree}')!=item['gui_source_tree']:\n    raise SystemExit('GUI source tree mismatch')\n"
    return script.replace(previous,extra+previous)


def prior_loader_recovery_check(candidate,script,bundle_data):
    """Execute recovery into a clean repository fetched from loader HEAD only."""
    with tempfile.TemporaryDirectory(prefix='kui-skies-prior-loader-') as temporary:
        root=Path(temporary);source=root/'source';source.mkdir()
        (root/'CANDIDATE.json').write_bytes(wire(candidate))
        bundle=source/'native-source-full.bundle';bundle.write_bytes(bundle_data)
        entry=source/'checkout-source.py';entry.write_bytes(script)
        destination=root/'restored-source'
        subprocess.run(['git','init',str(destination)],check=True,capture_output=True)
        subprocess.run(['git','-C',str(destination),'fetch',str(bundle),'HEAD'],
                       check=True,capture_output=True)
        subprocess.run(['git','-C',str(destination),'checkout','--detach',candidate['source_commit']],
                       check=True,capture_output=True)
        gui_before=subprocess.run(['git','-C',str(destination),'cat-file','-e',
            candidate['gui_source_commit']+'^{commit}'],capture_output=True).returncode==0
        run=subprocess.run(['python3',str(entry)],cwd=root,text=True,capture_output=True)
        require(run.returncode==0,'Recovery into prior loader checkout failed: '+run.stderr)
        require(git(destination,'rev-parse','HEAD')==candidate['source_commit'] and
                git(destination,'rev-parse',candidate['gui_source_commit']+'^{tree}')==candidate['gui_source_tree'] and
                not git(destination,'status','--porcelain'),
                'Prior clean checkout did not recover both exact source trees')
    return dict(extracted_cli_runs=1,prior_loader_only_checkout=True,
                gui_commit_present_before_recovery=gui_before,
                exact_gui_tree_recovered=True,loader_head_and_clean_tree_preserved=True)


def package(baseline_build,full_build,ui_root,output=None,evidence=(),original=None):
    commit,tree=clean_source(ROOT)
    revision=commit[:12]
    ui_root=Path(ui_root).resolve(strict=True)
    gui_files,gui_source_evidence,gui_metadata,gui_report=gui_artifacts(ui_root)
    gui_commit=gui_report['source_commit']
    gui_ref=git(ui_root,'symbolic-ref','--quiet','HEAD')
    require(git(ROOT,'rev-parse',gui_commit+'^{tree}')==gui_report['source_tree'],
            'GUI source commit is unavailable in the shared source repository')
    require(common_directory(ui_root)==common_directory(ROOT),
            'GUI and loader must share the source repository bundled by this package')
    files={}
    metadata=dict(schema=SCHEMA,source_commit=commit,build=revision,lease_bytes=SPAN,variants={})
    candidate=dict(schema='kui.native.skies-vmu-compare.candidate.v1',source_commit=commit,
        source_tree=tree,build=revision,lease_bytes=SPAN,storage_transport='SCI',
        virtual_a2=True,background_reader_enabled=False,prefetch_enabled=False,
        console_execution_verified=False,skies_crash_fixed=False,
        gui_source_commit=gui_commit,gui_source_tree=gui_report['source_tree'],gui_source_ref=gui_ref,
        launch_ui=gui_report,variants={})
    builds={'baseline':Path(baseline_build).resolve(strict=True),
            'full-submit':Path(full_build).resolve(strict=True)}
    for name,build in builds.items():
        report=audit(build,transport=1,allow_dirty=False,self_test=True)
        require(report.get('schema')=='kui.native.vmu-only.linked.v1' and
                report.get('transport')==1 and report.get('lease_bytes')==SPAN and
                report.get('worker_span')==SPAN and report.get('controller_submit')==MODES[name] and
                report.get('background_reader_enabled') is False and report.get('prefetch_enabled') is False and
                report.get('console_execution_verified') is False and
                report.get('source_provenance',{}).get('commit')==commit and
                report['source_provenance'].get('source_dirty') is False,
                'Linked audit differs from the clean selected controller variant: '+name)
        report_wire=wire(report)
        module,manifest=bind_profile(build,commit)
        require(report.get('module_sha256')==sha(module),'Linked audit differs from the packaged module: '+name)
        prefix='variants/'+name
        files[prefix+'/base/KUI/apps/games/retail-boot.kui']=module
        files[prefix+'/base/native-persistent-personalization.json']=wire(manifest)
        metadata['variants'][name]=dict(directory=prefix,transport=1,module_sha256=sha(module))
        artifact_hashes={}
        for artifact in ARTIFACTS:
            data=(build/artifact).read_bytes()
            files['artifacts/'+name+'/'+artifact]=data
            artifact_hashes[artifact]=sha(data)
        for pattern in ('*.ci','*.su'):
            for artifact in sorted(build.rglob(pattern)):
                files['artifacts/'+name+'/'+artifact.relative_to(build).as_posix()]=artifact.read_bytes()
        for optional in ('vmu-only-config',):
            path=build/optional
            if path.is_file():
                files['artifacts/'+name+'/'+optional]=path.read_bytes()
        files['artifacts/'+name+'/vmu-only-audit.json']=report_wire
        candidate['variants'][name]=dict(controller_submit=MODES[name],transport=1,
            module_sha256=sha(module),profile_module_offset=manifest['profile_module_offset'],
            audit_sha256=sha(report_wire),artifact_sha256=artifact_hashes)
    original=original or ROOT.parent/'delivery/K-UI-VMU-Only-4edbc56c295b.zip'
    candidate['baseline_original_comparison']=original_comparison(builds['baseline'],revision,original)
    for name,data in gui_files.items():
        files['artifacts/gui/'+name]=data
    files['gui/runtime.kui']=gui_files['runtime.kui']
    files['gui.json']=wire(gui_metadata)
    files['evidence/skies-standard-prelaunch-crc-2026-10-30.md']=gui_source_evidence
    for path in sorted((ui_root/'build/gui-prelaunch-tests').glob('*.log')):
        require(path.is_file() and not path.is_symlink(),'GUI evidence log must be an ordinary file')
        files['evidence/gui-'+path.name]=path.read_bytes()
    for name,path in (('install.py','tools/install_skies_vmu_compare.py'),
                      ('install-launch-ui.py','tools/install_skies_launch_ui.py'),
                      ('inspect-cards.py','tools/inspect_native_vmu_cards.py'),
                      ('personalize.py','tools/personalize_vmu_native_persistent.py'),
                      ('tools/install_native_vmu_only.py','tools/install_native_vmu_only.py'),
                      ('tools/runtime_package.py','tools/runtime_package.py'),
                      ('tools/vmu_persist_profile.py','tools/vmu_persist_profile.py'),
                      ('tools/explain_vmu_image.py','tools/explain_vmu_image.py'),
                      ('README-FIRST.md','docs/native-skies-vmu-comparison.md'),
                      ('LICENSE','LICENSE'),('THIRD_PARTY.md','THIRD_PARTY.md')):
        files[name]=(ROOT/path).read_bytes()
    files['vmu-only.json']=wire(metadata)
    files['test-results.csv']=test_sheet()
    files['SOURCE.md']=source_guide(commit,gui_commit)
    for path in evidence:
        path=Path(path).resolve(strict=True)
        require(path.is_file() and not path.is_symlink(),'Evidence must be an ordinary file')
        target='evidence/'+path.name
        data=path.read_bytes()
        require(target not in files or files[target]==data,'Duplicate evidence filename: '+path.name)
        files[target]=data
    with tempfile.TemporaryDirectory(prefix='kui-skies-source-') as temporary:
        bundle=Path(temporary)/'native-source-full.bundle'
        subprocess.run(['git','-C',str(ROOT),'bundle','create',str(bundle),'HEAD','--all'],
                       check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        subprocess.run(['git','-C',str(ROOT),'bundle','verify',str(bundle)],
                       check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        heads=subprocess.check_output(['git','bundle','list-heads',str(bundle)],text=True)
        require(any(line.split()==[gui_commit,gui_ref] for line in heads.splitlines()),
                'Source bundle does not advertise the exact GUI source branch')
        bundle_data=bundle.read_bytes()
    candidate['source_full_bundle']=dict(path='source/native-source-full.bundle',
        bytes=len(bundle_data),sha256=sha(bundle_data),full_history=True,prerequisites=[])
    script=combined_checkout_script()
    candidate['offline_source_recovery']=offline_source_recovery_check(candidate,script,bundle_data)
    candidate['prior_loader_source_recovery']=prior_loader_recovery_check(candidate,script,bundle_data)
    require(clean_source(ROOT)==(commit,tree) and clean_source(ui_root)==(gui_commit,gui_report['source_tree']),
            'Loader or GUI source changed while packaging')
    files['source/native-source-full.bundle']=bundle_data
    files['source/checkout-source.py']=script
    files['CANDIDATE.json']=wire(candidate)
    files['SHA256SUMS.txt']=''.join(sha(data)+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    output=Path(output or ROOT.parent/'delivery'/('K-UI-Skies-VMU-Compare-'+revision+'.zip')).resolve()
    require(not output.is_relative_to(ROOT) and not output.is_relative_to(ui_root),
            'Save the deliverable outside both source worktrees')
    output.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(prefix='.skies-vmu-compare-',suffix='.zip',dir=output.parent)
    os.close(fd);temporary=Path(name)
    try:
        with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED) as archive:
            for name,data in sorted(files.items()):
                archive.writestr(name,data)
        with zipfile.ZipFile(temporary) as archive:
            require(len(archive.namelist())==len(files) and set(archive.namelist())==set(files) and
                    all(archive.read(name)==data for name,data in files.items()),
                    'ZIP round-trip byte verification failed')
        os.replace(temporary,output)
    finally:
        temporary.unlink(missing_ok=True)
    result=dict(zip=str(output),bytes=output.stat().st_size,sha256=sha(output.read_bytes()),
        source_commit=commit,gui_source_commit=gui_commit,build=revision,
        modes=candidate['variants'],baseline_original_comparison=candidate['baseline_original_comparison'],
        console_execution_verified=False,skies_crash_fixed=False)
    print(json.dumps(result,indent=2))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-build',type=Path,default=ROOT/'build/skies-vmu-baseline')
    parser.add_argument('--full-build',type=Path,default=ROOT/'build/skies-vmu-full')
    parser.add_argument('--ui-root',type=Path,default=ROOT.parent/'skies-launch-ui')
    parser.add_argument('--original',type=Path,default=ROOT.parent/'delivery/K-UI-VMU-Only-4edbc56c295b.zip')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    args=parser.parse_args()
    try:
        package(args.baseline_build,args.full_build,args.ui_root,args.output,args.evidence,args.original)
    except (OSError,ValueError,AssertionError,KeyError,TypeError,subprocess.CalledProcessError,zipfile.BadZipFile) as error:
        parser.exit(2,'Skies VMU comparison package refused: '+str(error)+'\n')


if __name__=='__main__':
    main()
