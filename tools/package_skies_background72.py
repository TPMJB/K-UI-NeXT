#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package matching compact Background/Standard 72 KiB loaders and their GUI."""
import argparse
import csv
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_native_vmu_only import audit as standalone_audit
from install_skies_background72 import CONTRACTS, SCHEMA
from install_skies_background72_ui import SCHEMA as GUI_SCHEMA
from package_native_vmu_only import ARTIFACTS, bind_profile, checkout_script, sha, wire
from package_skies_next import validate_gui_evidence
from package_skies_read_fix import validate_audit as validate_read_policy
from package_skies_vmu_compare import clean_source, gui_artifacts
from package_vmu_native_dual_delivery import offline_source_recovery_check

ROOT = Path(__file__).resolve().parents[1]
BACKGROUND_ARTIFACTS = ARTIFACTS + (
    'retail-native-background-compact.kui', 'retail-native-dual.kui',
    'resident-legacy.elf', 'resident-legacy.bin', 'resident-legacy.map',
    'native-ra-aux.elf', 'native-ra-aux.bin', 'native-ra-aux.map', 'native-ra-aux.reloc',
    'vmu_maple_observe_legacy_symbols.h')


def require(value, message):
    if not value:
        raise ValueError(message)


def validate_standalone(report, commit, module_sha256):
    validate_read_policy(report, 'paced', commit, module_sha256)


def expected_toggle_kernels():
    from check_native_compact_background_profile import STAGE_PINS, WORKER_PINS
    result = {}
    for label, pins in (('stage',STAGE_PINS),('worker',WORKER_PINS)):
        selected = pins.get('toggle')
        require(isinstance(selected,dict) and selected,
                'Compact Background toggle instruction review is incomplete: '+label)
        result[label] = {name:dict(bytes=count,sha256=digest) for name,(count,digest) in selected.items()}
    return result


def validate_background(report, commit, module_sha256):
    require(report.get('schema') == 'kui.native.background.compact.audit.v1' and
        report.get('source_provenance', {}).get('commit') == commit and
        report['source_provenance'].get('source_dirty') is False and
        report.get('runtime', {}).get('build') == commit[:12] and
        report.get('module_sha256') == module_sha256 and
        report.get('geometry', {}).get('worker_bytes') == 0x12000 and
        report['geometry'].get('lease_bytes') == 0x47000 and
        report['geometry'].get('cache_bytes') == 0x30000 and
        report['geometry'].get('worker_stack_bytes') == 4096 and
        report.get('transport') == 1 and
        report.get('console_execution_verified') is False,
        'Compact Background source/runtime/geometry binding differs')
    # The aggregate checker must execute every linked companion before it can
    # produce its report. These witnesses prevent a partial report being used
    # as the complete delivery proof.
    require(report.get('matching_immutable_component_markers') is True and
        report.get('background_reader_enabled') is True and report.get('prefetch_enabled') is True and
        report.get('controller_submit') == 'split' and report.get('receive_engine') == 'background-async-dma' and
        report.get('prefetch_mode') == 'auto' and report.get('standard_rx_feeder_absent') is True and
        report.get('emulated_vmu_toggle') is True and report.get('reader_only_retains_complete_lease') is True and
        report.get('exact_toggle_kernels') == expected_toggle_kernels(),
        'Compact Background immutable reader/VMU witnesses differ')
    full = report.get('proofs', {}).get('persistent', {})
    read_ahead = full.get('read_ahead', {})
    require(full.get('module_sha256') == module_sha256 and full.get('source_provenance') == report['source_provenance'] and
        full.get('runtime') == report['runtime'] and full.get('worker_span') == 0x12000 and
        full.get('worker_stack_bytes') == 4096 and full.get('stage_embedded_worker_verified') is True and
        full.get('maple_interception_installed') is True and full.get('scoped_sci_transactions') is True and
        full.get('console_execution_verified') is False and read_ahead.get('enabled') is True and
        read_ahead.get('paired_markers') is True and read_ahead.get('dual_reader', {}).get('enabled') is True and
        read_ahead['dual_reader'].get('compact_background', {}).get('matching_immutable_component_markers') is True,
        'Compact Background complete persistent/dual-reader proof differs')


def validate_compact_gui_evidence(evidence, commit, files, logs):
    names = {'native_cache','standard_filesystem','dual_filesystem','compact_background_filesystem'}
    require(set(logs) == names and evidence.get('log_sha256') ==
        {name:sha(data) for name,data in logs.items()}, 'GUI regression log bindings differ')
    count = evidence.get('checks',{}).get('compact_background_filesystem_cases')
    require(type(count) is int and count >= 46,
            'Missing actual compact Background GUI filesystem regressions')
    # Keep the complete historical GUI evidence validator intact. Its three
    # legacy log identities are a strict subset of this new four-log proof.
    prior = dict(evidence,log_sha256={name:digest for name,digest in evidence['log_sha256'].items()
                                    if name != 'compact_background_filesystem'})
    validate_gui_evidence(prior,commit,files,{name:data for name,data in logs.items()
                                          if name != 'compact_background_filesystem'})


def test_sheet():
    stream = io.StringIO(newline='');writer = csv.writer(stream)
    writer.writerow(('route','reader','Virtual_VMU','pair_id','loaded_saved_game','opening_intro',
                     'Alfonso_second_fight','safe_save','power_cycle_load','fault_message_and_fields','notes'))
    for row in (('Background','New (X)','On'),('Background','New (X)','Off'),
                ('Background optional','Legacy (Y)','On'),('Standard retained','Standard','module supplies A2')):
        writer.writerow((*row, '', '', '', '', '', '', '', ''))
    return stream.getvalue().encode()


def source_guide(commit):
    return f'''# Exact source checkpoint

GUI and both loaders use clean source commit `{commit}`. The full Git bundle
includes history with no prerequisites. Recover the source offline:

```sh
python3 source/checkout-source.py
```

With the existing SH-4 toolchain on PATH, rebuild the SCI loaders serially:

```sh
make -C restored-source -f Makefile.vmu-native-vmu-lite-paced -j1 BUILD=build/background72-standalone VMU_TRANSPORT=1 VMU_SCI_RX_POLICY=paced
make -C restored-source -f Makefile.vmu-native-background-compact -j1 BUILD=build/background72-combined
```

The matching GUI uses `Makefile.dc` with the existing KallistiOS dependencies.
`artifacts/` retains exact linked ELF/binary files, maps, compile provenance,
callgraph/stack files and audits. The Background aggregate proof includes the
complete compact lease, both reader engines, cache, relocations, stack bounds,
and typed GUI toggle capability. Its asynchronous stream engine remains
separate from the Standard paced receive-DMA path. `evidence/` holds the host
reports supplied when packaging. Console behavior is recorded separately.
'''.encode()


def package(standalone_build, background_build, output=None, evidence=()):
    from check_native_background_compact import audit as background_audit
    commit, tree = clean_source(ROOT);revision = commit[:12]
    gui_files, gui_source_evidence, gui_metadata, gui_report = gui_artifacts(ROOT)
    require(gui_report['source_commit'] == commit and gui_report['source_tree'] == tree,
            'GUI and both loaders must use the same clean source checkpoint')
    logs = {name:(ROOT/'build/gui-prelaunch-tests'/(name+'.log')).read_bytes()
            for name in ('native_cache','standard_filesystem','dual_filesystem','compact_background_filesystem')}
    validate_compact_gui_evidence(json.loads(gui_files['gui-prelaunch-crc-evidence.json']), commit, gui_files, logs)
    gui_metadata = dict(gui_metadata, schema=GUI_SCHEMA)
    metadata = dict(schema=SCHEMA, source_commit=commit, build=revision, variants={})
    candidate = dict(schema='kui.native.skies-background72.candidate.v1', source_commit=commit,
        source_tree=tree, build=revision, storage_transport='SCI', gui_updated=True,
        launch_ui=gui_report, console_execution_verified=False, skies_crash_fixed=False,
        recommended_install='background-only',
        background_vmu_preference_switchable=True, standalone_vmu_forced_on=True,
        background_vmu_off_retains_worker_lease_cache=True,
        installed_profile_does_not_override_background_assignment=True, variants={})
    files = {}
    for component, build_path in (('standalone',standalone_build),('background',background_build)):
        build = Path(build_path).resolve(strict=True)
        if component == 'standalone':
            report = standalone_audit(build, transport=1, allow_dirty=False, self_test=True)
            module, manifest = bind_profile(build, commit, 'retail-native-vmu-lite-paced.kui')
            validate_standalone(report, commit, sha(module))
            artifacts, audit_name = ARTIFACTS, 'vmu-only-audit.json'
        else:
            report = background_audit(build, allow_dirty=False, self_test=True)
            module, manifest = bind_profile(build, commit, 'retail-native-background-compact.kui')
            validate_background(report, commit, sha(module))
            require((build/'retail-native-dual.kui').read_bytes() == module,
                    'Compact Background dual-reader alias differs')
            artifacts, audit_name = BACKGROUND_ARTIFACTS, 'background-compact-audit.json'
        prefix = 'variants/'+component
        files[prefix+'/base/KUI/apps/games/retail-boot.kui'] = module
        files[prefix+'/base/native-persistent-personalization.json'] = wire(manifest)
        metadata['variants'][component] = dict(directory=prefix, **CONTRACTS[component], module_sha256=sha(module))
        hashes = {}
        for artifact in artifacts:
            data = (build/artifact).read_bytes();files['artifacts/'+component+'/'+artifact] = data
            hashes[artifact] = sha(data)
        for pattern in ('*.ci','*.su'):
            for path in sorted(build.rglob(pattern)):
                require(not path.is_symlink(), 'Callgraph/stack artifact must be ordinary')
                files['artifacts/'+component+'/'+path.relative_to(build).as_posix()] = path.read_bytes()
        for name in ('vmu-only-config','vmu-sci-rx-policy-config','retail-native-vmu-lite.kui',
                     'retail-native-vmu-lite-paced.kui','background-compact-config'):
            path = build/name
            if path.is_file():files['artifacts/'+component+'/'+name] = path.read_bytes()
        report_wire = wire(report);files['artifacts/'+component+'/'+audit_name] = report_wire
        candidate['variants'][component] = dict(**CONTRACTS[component], module_sha256=sha(module),
            audit_sha256=sha(report_wire), artifact_sha256=hashes,
            profile_module_offset=manifest['profile_module_offset'])
    for name, data in gui_files.items():files['artifacts/gui/'+name] = data
    files['gui/runtime.kui'] = gui_files['runtime.kui'];files['gui.json'] = wire(gui_metadata)
    files['evidence/skies-standard-prelaunch-crc-2026-10-30.md'] = gui_source_evidence
    for name,data in logs.items():files['evidence/gui-'+name+'.log'] = data
    for name, source in (
        ('install.py','tools/install_skies_background72.py'),
        ('install-ui.py','tools/install_skies_background72_ui.py'),
        ('inspect-cards.py','tools/inspect_native_vmu_cards.py'),
        ('personalize.py','tools/personalize_vmu_native_persistent.py'),
        ('tools/install_native_vmu_only.py','tools/install_native_vmu_only.py'),
        ('tools/install_skies_launch_ui.py','tools/install_skies_launch_ui.py'),
        ('tools/runtime_package.py','tools/runtime_package.py'),
        ('tools/vmu_persist_profile.py','tools/vmu_persist_profile.py'),
        ('tools/explain_vmu_image.py','tools/explain_vmu_image.py'),
        ('README-FIRST.md','docs/native-skies-background72.md'),('LICENSE','LICENSE'),('THIRD_PARTY.md','THIRD_PARTY.md')):
        files[name] = (ROOT/source).read_bytes()
    files['skies-background72.json'] = wire(metadata);files['SOURCE.md'] = source_guide(commit)
    files['test-results.csv'] = test_sheet()
    for path in evidence:
        path = Path(path).resolve(strict=True)
        require(path.is_file() and not path.is_symlink(), 'Evidence must be an ordinary file')
        target = 'evidence/'+path.name;data = path.read_bytes()
        require(target not in files or files[target] == data, 'Different evidence shares filename: '+path.name)
        files[target] = data
    with tempfile.TemporaryDirectory(prefix='kui-background72-source-') as temporary:
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
    output = Path(output or ROOT.parent/'delivery'/('K-UI-Skies-Background72-'+revision+'.zip')).resolve()
    require(not output.is_relative_to(ROOT), 'Save package outside the source checkout')
    output.parent.mkdir(parents=True,exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.skies-background72-',suffix='.zip',dir=output.parent);os.close(fd)
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
        source_commit=commit,build=revision,variants=candidate['variants'],gui_updated=True,
        console_execution_verified=False)
    print(json.dumps(result,indent=2));return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--standalone-build',type=Path,default=ROOT/'build/background72-standalone')
    parser.add_argument('--background-build',type=Path,default=ROOT/'build/background72-combined')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    args = parser.parse_args()
    try:package(args.standalone_build,args.background_build,args.output,args.evidence)
    except (OSError,ValueError,AssertionError,KeyError,TypeError,subprocess.CalledProcessError) as error:
        parser.exit(2,'Compact Background package refused: '+str(error)+'\n')


if __name__ == '__main__':main()
