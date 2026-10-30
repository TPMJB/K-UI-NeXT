#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package three SCI 72 KiB read-policy comparisons without replacing the GUI."""
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
from install_skies_read_fix import CONTRACTS, SCHEMA
from package_native_vmu_only import ARTIFACTS, bind_profile, checkout_script, sha, wire
from package_skies_vmu_compare import clean_source
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
        report.get('worker_span') == geometry['lease_bytes'] and report.get('variant') == 'vmu-lite' and
        report.get('controller_submit') == 'split' and
        report.get('controller_submit_audit', {}).get('matching_immutable_markers') is True and
        report.get('sci_rx_policy') == geometry['sci_rx_policy'] and
        report.get('sci_rx_policy_audit', {}).get('matching_immutable_markers') is True and
        report.get('background_reader_enabled') is False and report.get('prefetch_enabled') is False and
        report.get('console_execution_verified') is False and
        report.get('source_provenance', {}).get('commit') == commit and
        report['source_provenance'].get('source_dirty') is False and
        report.get('runtime', {}).get('build') == commit[:12] and
        report.get('module_sha256') == module_sha256,
        'Linked SCI policy/source/geometry binding differs: '+mode)


def test_sheet():
    stream = io.StringIO(newline='');writer = csv.writer(stream)
    writer.writerow(('mode','profile','opening_intro','Alfonso_second_fight',
                     'save_point_reached','fault_message_and_fields','attempts_or_minutes','notes'))
    for mode in CONTRACTS:writer.writerow((mode,'','','','','','',''))
    return stream.getvalue().encode()


def source_guide(commit):
    return f'''# Exact loader source checkpoint

All three loader modes use clean source commit `{commit}`. Complete source
history is included with no prerequisite bundle. Recover it offline:

```sh
python3 source/checkout-source.py
```

With the existing SH-4 toolchain on PATH, build the SCI modes serially:

```sh
make -C restored-source -f Makefile.vmu-native-vmu-lite-paced -j1 BUILD=build/skies-read-paced VMU_TRANSPORT=1 VMU_SCI_RX_POLICY=paced
make -C restored-source -f Makefile.vmu-native-vmu-lite-paced -j1 BUILD=build/skies-read-pio VMU_TRANSPORT=1 VMU_SCI_RX_POLICY=pio
make -C restored-source -f Makefile.vmu-native-vmu-lite-paced -j1 BUILD=build/skies-read-unpaced VMU_TRANSPORT=1 VMU_SCI_RX_POLICY=unpaced
```

`artifacts/` contains the exact ELF files, binary images, maps, compile
provenance, emitted callgraph/stack files and linked audits. `evidence/` contains
host and integration reports supplied at packaging time. The linked audit binds
each policy to matching immutable stage/resident markers and its actual bus code.
This package contains no GUI candidate and does not update `KUI/runtime.kui`.
'''.encode()


def package(builds, output=None, evidence=()):
    require(set(builds) == set(CONTRACTS), 'Supply exactly paced, pio and unpaced builds')
    commit, tree = clean_source(ROOT);revision = commit[:12]
    metadata = dict(schema=SCHEMA, source_commit=commit, build=revision, variants={})
    candidate = dict(schema='kui.native.skies-read-fix.candidate.v1', source_commit=commit,
        source_tree=tree, build=revision, storage_transport='SCI', virtual_a2=True,
        gui_updated=False, background_reader_enabled=False, prefetch_enabled=False,
        console_execution_verified=False, skies_crash_fixed=False,
        check_progress_fix_in_all_modes=True, default_mode='paced', variants={})
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
        for pattern in ('*.ci','*.su'):
            for path in sorted(build.rglob(pattern)):
                files['artifacts/'+mode+'/'+path.relative_to(build).as_posix()] = path.read_bytes()
        for name in ('vmu-only-config','vmu-sci-rx-policy-config','retail-native-vmu-lite.kui',
                     'retail-native-vmu-lite-'+mode+'.kui'):
            path = build/name
            if path.is_file():files['artifacts/'+mode+'/'+name] = path.read_bytes()
        report_wire = wire(report);files['artifacts/'+mode+'/vmu-only-audit.json'] = report_wire
        candidate['variants'][mode] = dict(**geometry, module_sha256=sha(module),
            audit_sha256=sha(report_wire), artifact_sha256=hashes,
            profile_module_offset=manifest['profile_module_offset'])
    for name, source in (
        ('install.py','tools/install_skies_read_fix.py'),
        ('inspect-cards.py','tools/inspect_native_vmu_cards.py'),
        ('personalize.py','tools/personalize_vmu_native_persistent.py'),
        ('tools/install_native_vmu_only.py','tools/install_native_vmu_only.py'),
        ('tools/runtime_package.py','tools/runtime_package.py'),
        ('tools/vmu_persist_profile.py','tools/vmu_persist_profile.py'),
        ('tools/explain_vmu_image.py','tools/explain_vmu_image.py'),
        ('README-FIRST.md','docs/native-skies-read-fix.md'),('LICENSE','LICENSE'),('THIRD_PARTY.md','THIRD_PARTY.md')):
        files[name] = (ROOT/source).read_bytes()
    files['skies-read-fix.json'] = wire(metadata);files['SOURCE.md'] = source_guide(commit)
    files['test-results.csv'] = test_sheet()
    for path in evidence:
        path = Path(path).resolve(strict=True)
        require(path.is_file() and not path.is_symlink(), 'Evidence must be an ordinary file')
        target = 'evidence/'+path.name;data = path.read_bytes()
        require(target not in files or files[target] == data, 'Different evidence shares filename: '+path.name)
        files[target] = data
    with tempfile.TemporaryDirectory(prefix='kui-skies-read-source-') as temporary:
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
    output = Path(output or ROOT.parent/'delivery'/('K-UI-Skies-Read-Fix-'+revision+'.zip')).resolve()
    require(not output.is_relative_to(ROOT), 'Save package outside the source checkout')
    output.parent.mkdir(parents=True,exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.skies-read-fix-',suffix='.zip',dir=output.parent);os.close(fd)
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
        source_commit=commit,build=revision,modes=candidate['variants'],gui_updated=False,
        console_execution_verified=False)
    print(json.dumps(result,indent=2));return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--paced-build',type=Path,default=ROOT/'build/skies-read-paced')
    parser.add_argument('--pio-build',type=Path,default=ROOT/'build/skies-read-pio')
    parser.add_argument('--unpaced-build',type=Path,default=ROOT/'build/skies-read-unpaced')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    args = parser.parse_args()
    try:package({'paced':args.paced_build,'pio':args.pio_build,'unpaced':args.unpaced_build},args.output,args.evidence)
    except (OSError,ValueError,AssertionError,KeyError,TypeError,subprocess.CalledProcessError) as error:
        parser.exit(2,'Skies read comparison package refused: '+str(error)+'\n')


if __name__ == '__main__':main()
