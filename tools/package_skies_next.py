#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver one clean-source GUI and three audited SCI Skies VMU modes."""
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
from install_skies_next import CONTRACTS, SCHEMA
from install_skies_next_ui import SCHEMA as GUI_SCHEMA
from package_native_vmu_only import ARTIFACTS, bind_profile, checkout_script, sha, wire
from package_skies_vmu_compare import clean_source, gui_artifacts
from package_vmu_native_dual_delivery import offline_source_recovery_check

ROOT = Path(__file__).resolve().parents[1]


def require(value, message):
    if not value:
        raise ValueError(message)


def validate_audit(report, mode, commit, module_sha256):
    geometry = CONTRACTS[mode]
    require(report.get('schema') == 'kui.native.vmu-only.linked.v1' and
        report.get('transport') == geometry['transport'] and
        report.get('lease_bytes') == geometry['lease_bytes'] and
        report.get('worker_span') == geometry['lease_bytes'] and
        report.get('variant') == ('vmu-lite' if geometry['lease_bytes'] == 0x12000 else 'vmu-only') and
        report.get('controller_submit') == geometry['controller_submit'] and
        report.get('controller_submit_audit', {}).get('matching_immutable_markers') is True and
        report.get('background_reader_enabled') is False and report.get('prefetch_enabled') is False and
        report.get('console_execution_verified') is False and
        report.get('source_provenance', {}).get('commit') == commit and
        report['source_provenance'].get('source_dirty') is False and
        report.get('runtime', {}).get('build') == commit[:12] and
        report.get('module_sha256') == module_sha256,
        'Linked mode/source/geometry binding differs: '+mode)


def validate_gui_evidence(evidence, commit, files, logs):
    require(evidence.get('schema') == 'kui.native.standard.prelaunch-crc.evidence.v1' and
        evidence.get('source_commit') == commit and evidence.get('build') == commit[:12] and
        evidence.get('source_dirty') is False and evidence.get('all_runtime_objects_rebuilt_at_commit') is True and
        evidence.get('elf_sha256') == sha(files['kui-runtime.elf']) and
        evidence.get('module_sha256') == sha(files['runtime.kui']) and
        evidence.get('compile_provenance_sha256') == sha(files['kui-runtime.compile.json']),
        'GUI evidence is not the complete fresh clean-source proof')
    checks, scope = evidence.get('checks', {}), evidence.get('scope', {})
    for name in ('native_cache_asan_ubsan_checks', 'standard_fat32_exfat_cases', 'existing_dual_fat32_exfat_cases'):
        require(type(checks.get(name)) is int and checks[name] > 0, 'Missing successful GUI check count: '+name)
    for name in ('one_sector_io_preserved', 'global_vmu_off_saved_and_preserved',
                 'independent_exact_tail_crc_verified', 'report_and_manifest_crc_verified',
                 'partial_source_requires_complete_crc_fallback', 'cancellation_refuses_handoff',
                 'malformed_absent_duplicate_seed_keep_direct_crc', 'scif_keeps_direct_crc'):
        require(checks.get(name) is True, 'Missing successful GUI regression: '+name)
    require(scope.get('gui_only') is True and scope.get('new_buffer_allocations') == 0 and
        type(scope.get('new_buffer_allocations')) is int and scope.get('prefetch_cache_added') is False and
        scope.get('manifest_wire_changed') is False and scope.get('detached_loader_changed') is False and
        scope.get('runtime_vmu_changed') is False and
        scope.get('sha256_and_detached_crc_and_ownership_checks_retained') is True and
        scope.get('console_execution_verified') is False and scope.get('ingame_freeze_fixed') is False,
        'GUI evidence scope differs from the audited launch-only change')
    expected = evidence.get('log_sha256')
    require(isinstance(expected, dict) and set(expected) == set(logs) and
        all(sha(logs[name]) == digest for name, digest in expected.items()), 'GUI regression log bindings differ')


def test_sheet():
    stream = io.StringIO(newline='');writer = csv.writer(stream)
    writer.writerow(('mode','profile','VMU_detected','old_save_loaded','freeze_scene',
                     'attempts_or_minutes','A_to_bootstrap1_seconds','fault_message','physical_A1_or_extra_controller','notes'))
    for mode in CONTRACTS:writer.writerow((mode, '', '', '', '', '', '', '', '', ''))
    return stream.getvalue().encode()


def source_guide(commit):
    return f'''# Exact source checkpoint

GUI and all three loaders use clean source commit `{commit}`. Complete history
is included with no prerequisite bundle. Recover it offline from this folder:

```sh
python3 source/checkout-source.py
```

With the existing SH-4 toolchain on PATH, the three SCI builds are:

```sh
make -C restored-source -f Makefile.vmu-native-vmu-lite -j1 BUILD=build/skies-next-baseline VMU_TRANSPORT=1
make -C restored-source -f Makefile.vmu-native-vmu-lite-full-submit -j1 BUILD=build/skies-next-full VMU_TRANSPORT=1
make -C restored-source -f Makefile.vmu-native-vmu-only -j1 BUILD=build/skies-next-reference112 VMU_TRANSPORT=1
```

The GUI follows `Makefile.dc` with the existing KallistiOS/toolchain dependencies.
Exact ELF files, map files, compile provenance, emitted stack/callgraph files,
and linked audit reports are under `artifacts/`. Evidence is under `evidence/`.
The standalone VMU and broader background-reader paths remain separate.
'''.encode()


def package(builds, output=None, evidence=()):
    require(set(builds) == set(CONTRACTS), 'Supply exactly the three supported mode builds')
    commit, tree = clean_source(ROOT)
    revision = commit[:12]
    gui_files, gui_source_evidence, gui_metadata, gui_report = gui_artifacts(ROOT)
    require(gui_report['source_commit'] == commit and gui_report['source_tree'] == tree,
            'GUI and loaders must use the same clean source checkpoint')
    logs = {name: (ROOT/'build/gui-prelaunch-tests'/(name+'.log')).read_bytes()
            for name in ('native_cache', 'standard_filesystem', 'dual_filesystem')}
    validate_gui_evidence(json.loads(gui_files['gui-prelaunch-crc-evidence.json']), commit, gui_files, logs)
    gui_metadata = dict(gui_metadata, schema=GUI_SCHEMA)
    metadata = dict(schema=SCHEMA, source_commit=commit, build=revision, variants={})
    candidate = dict(schema='kui.native.skies-next.candidate.v2', source_commit=commit,
        source_tree=tree, build=revision, storage_transport='SCI', virtual_a2=True,
        background_reader_enabled=False, prefetch_enabled=False, console_execution_verified=False,
        skies_crash_fixed=False, launch_ui=gui_report, variants={},
        compatibility_limits=dict(compact_maple_chain_bytes=4096, compact_staged_virtual_reply_frames=1,
            longer_chain_behavior='visible refusal before storage or partial physical submission'))
    files = {}
    for mode, geometry in CONTRACTS.items():
        build = Path(builds[mode]).resolve(strict=True)
        report = audit(build, transport=1, allow_dirty=False, self_test=True)
        module, manifest = bind_profile(build, commit)
        validate_audit(report, mode, commit, sha(module))
        prefix = 'variants/'+mode
        files[prefix+'/base/KUI/apps/games/retail-boot.kui'] = module
        files[prefix+'/base/native-persistent-personalization.json'] = wire(manifest)
        metadata['variants'][mode] = dict(directory=prefix, **geometry, module_sha256=sha(module))
        hashes = {}
        for artifact in ARTIFACTS:
            data = (build/artifact).read_bytes();files['artifacts/'+mode+'/'+artifact] = data
            hashes[artifact] = sha(data)
        for pattern in ('*.ci', '*.su'):
            for path in sorted(build.rglob(pattern)):
                files['artifacts/'+mode+'/'+path.relative_to(build).as_posix()] = path.read_bytes()
        for name in ('vmu-only-config', 'retail-native-vmu-lite.kui'):
            path = build/name
            if path.is_file():files['artifacts/'+mode+'/'+name] = path.read_bytes()
        report_wire = wire(report)
        files['artifacts/'+mode+'/vmu-only-audit.json'] = report_wire
        candidate['variants'][mode] = dict(**geometry, module_sha256=sha(module),
            audit_sha256=sha(report_wire), artifact_sha256=hashes,
            profile_module_offset=manifest['profile_module_offset'])
    for name, data in gui_files.items():files['artifacts/gui/'+name] = data
    files['gui/runtime.kui'] = gui_files['runtime.kui'];files['gui.json'] = wire(gui_metadata)
    files['evidence/skies-standard-prelaunch-crc-2026-10-30.md'] = gui_source_evidence
    for name, data in logs.items():files['evidence/gui-'+name+'.log'] = data
    for name, source in (
        ('install.py','tools/install_skies_next.py'), ('install-ui.py','tools/install_skies_next_ui.py'),
        ('inspect-cards.py','tools/inspect_native_vmu_cards.py'),
        ('personalize.py','tools/personalize_vmu_native_persistent.py'),
        ('tools/install_native_vmu_only.py','tools/install_native_vmu_only.py'),
        ('tools/install_skies_launch_ui.py','tools/install_skies_launch_ui.py'),
        ('tools/runtime_package.py','tools/runtime_package.py'), ('tools/vmu_persist_profile.py','tools/vmu_persist_profile.py'),
        ('tools/explain_vmu_image.py','tools/explain_vmu_image.py'),
        ('README-FIRST.md','docs/native-skies-next.md'), ('LICENSE','LICENSE'), ('THIRD_PARTY.md','THIRD_PARTY.md')):
        files[name] = (ROOT/source).read_bytes()
    files['skies-next.json'] = wire(metadata);files['SOURCE.md'] = source_guide(commit)
    files['test-results.csv'] = test_sheet()
    for path in evidence:
        path = Path(path).resolve(strict=True)
        require(path.is_file() and not path.is_symlink(), 'Evidence must be an ordinary file')
        target = 'evidence/'+path.name;data = path.read_bytes()
        require(target not in files or files[target] == data, 'Different evidence shares filename: '+path.name)
        files[target] = data
    with tempfile.TemporaryDirectory(prefix='kui-skies-next-source-') as temporary:
        bundle = Path(temporary)/'native-source-full.bundle'
        subprocess.run(['git','bundle','create',str(bundle),'HEAD','--all'],cwd=ROOT,check=True,
                       stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        subprocess.run(['git','bundle','verify',str(bundle)],cwd=ROOT,check=True,
                       stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        bundle_data = bundle.read_bytes()
    candidate['source_full_bundle'] = dict(path='source/native-source-full.bundle',
        bytes=len(bundle_data),sha256=sha(bundle_data),full_history=True,prerequisites=[])
    script = checkout_script()
    candidate['offline_source_recovery'] = offline_source_recovery_check(candidate,script,bundle_data)
    require(clean_source(ROOT) == (commit,tree), 'Source changed while packaging')
    files['source/native-source-full.bundle'] = bundle_data;files['source/checkout-source.py'] = script
    files['CANDIDATE.json'] = wire(candidate)
    files['SHA256SUMS.txt'] = ''.join(sha(data)+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    output = Path(output or ROOT.parent/'delivery'/('K-UI-Skies-Next-'+revision+'.zip')).resolve()
    require(not output.is_relative_to(ROOT), 'Save package outside the source checkout')
    output.parent.mkdir(parents=True,exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.skies-next-',suffix='.zip',dir=output.parent);os.close(fd)
    temporary = Path(temporary)
    try:
        with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED) as archive:
            for name,data in sorted(files.items()):archive.writestr(name,data)
        with zipfile.ZipFile(temporary) as archive:
            require(len(archive.namelist()) == len(files) and set(archive.namelist()) == set(files) and
                all(archive.read(name) == data for name,data in files.items()), 'ZIP byte round-trip failed')
        os.replace(temporary,output)
    finally:temporary.unlink(missing_ok=True)
    result = dict(zip=str(output),bytes=output.stat().st_size,sha256=sha(output.read_bytes()),
        source_commit=commit,build=revision,modes=candidate['variants'],console_execution_verified=False)
    print(json.dumps(result,indent=2));return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-build',type=Path,default=ROOT/'build/skies-next-baseline')
    parser.add_argument('--full-build',type=Path,default=ROOT/'build/skies-next-full')
    parser.add_argument('--reference-build',type=Path,default=ROOT/'build/skies-next-reference112')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    args = parser.parse_args()
    try:package({'baseline':args.baseline_build,'full-submit':args.full_build,'reference-112':args.reference_build},args.output,args.evidence)
    except (OSError,ValueError,AssertionError,KeyError,TypeError,subprocess.CalledProcessError) as error:
        parser.exit(2,'Skies next package refused: '+str(error)+'\n')


if __name__ == '__main__':main()
