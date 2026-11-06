#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver one new GUI while retaining the exact installed ab1 loader cohort."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

from check_shared_loader_performance import receipt, runtime_standard_manifest_binding
from check_vmu_native_worker_gateway import read_elf
from native_cdda_manifest import runtime_binding as cdda_runtime_binding
from package_smash_native_menu_test import optimized_runtime_proof
from runtime_package import envelope, flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
BASELINE_SHA256 = '38f0170b0ce41c5a84d436dadebacf8ccc2d42f72b5dee9616eea0cdec1cb24c'
BASELINE_BYTES = 39264198
BASELINE_COMMIT = 'ab1b145b33b4b4cf252f61d7f5cfb06a55ac5f87'
GUI_PATH = 'KUI/runtime.kui'
MANIFEST_PATHS = ('evidence/preserved-standard-manifest.h',
    'evidence/preserved-standard-manifest.json', 'evidence/native-cdda-manifest.h',
    'evidence/native-cdda-manifest.json')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def require(value, message):
    if not value:
        raise ValueError(message)


def baseline(path):
    data = path.read_bytes()
    require(len(data) == BASELINE_BYTES and sha(data) == BASELINE_SHA256,
            'Baseline must be the exact delivered ab1 package')
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        require(len(names) == len(set(names)) and archive.testzip() is None,
                'Baseline ZIP has duplicate names or damaged content')
        installed = {name: archive.read(name) for name in names if name.startswith('KUI/')}
        require(len(installed) == 27 and GUI_PATH in installed,
                'Baseline installation inventory changed')
        build = json.loads(archive.read('BUILD.json'))
        require(build['source_commit'] == BASELINE_COMMIT and build['source_dirty'] is False,
                'Baseline source identity changed')
        hashes = {name: sha(body) for name, body in installed.items()}
        require(build['installed_files'] == hashes, 'Baseline installation hashes differ from its manifest')
        for name, body in installed.items():
            if name.endswith('.kui'):
                verify(body)
        manifests = {name: archive.read(name) for name in MANIFEST_PATHS}
    standard = json.loads(manifests[MANIFEST_PATHS[1]])
    require(sha(manifests[MANIFEST_PATHS[0]]) == standard['header_sha256'],
            'ZIP-backed Standard header differs from its manifest')
    module = installed['KUI/apps/games/retail-boot.kui']
    require(sha(module) == standard['module_sha256'], 'ZIP-backed Standard module changed')
    payload = module[64:]
    at, count = standard['profile_payload_offset'], standard['profile_bytes']
    require(0 <= at < at + count <= len(payload) and
            sha(payload[:at] + bytes(count) + payload[at + count:]) == standard['normalized_payload_sha256'],
            'ZIP-backed Standard normalized code differs from its manifest')
    cdda = json.loads(manifests[MANIFEST_PATHS[3]])
    require(sha(manifests[MANIFEST_PATHS[2]]) == cdda['header_sha256'],
            'ZIP-backed CDDA header differs from its manifest')
    for profile in cdda['profiles'].values():
        require(len(profile['aliases']) == 2, 'ZIP-backed CDDA alias set changed')
        for alias in profile['aliases']:
            body = installed[alias]
            info = verify(body)
            require(sha(body) == profile['module_sha256'] and
                    sha(body[64:]) == profile['payload_sha256'] and
                    info['build'] == profile['build'] and info['crc32'] == profile['crc32'] and
                    info['payload_bytes'] == profile['payload_bytes'],
                    'ZIP-backed CDDA module differs from its manifest')
    return installed, manifests, standard, cdda


def current_report(path, commit, name, *, runtime_elf_sha256=None):
    report = json.loads(path.read_text())
    require(report.get('status') == 'PASS' and report.get('source_commit') == commit and
            report.get('source_dirty') is False and not report.get('development_validation_only', False),
            name + ' is not this clean source cohort')
    sources = report.get('source_sha256', {})
    require(sources, name + ' has no source binding')
    for relative, digest in sources.items():
        source = (ROOT / relative).resolve()
        require(source.is_relative_to(ROOT) and sha(source.read_bytes()) == digest,
                name + ' source changed: ' + relative)
    if runtime_elf_sha256 is not None:
        require(report.get('runtime_elf_sha256') == runtime_elf_sha256,
                name + ' does not bind the delivered GUI ELF')
    return report


def admitted_baseline(report, installed, runtime):
    require(report.get('schema') == 'kui.smash.packaged-admission.v1' and
            report.get('package_sha256') == BASELINE_SHA256 and
            report.get('package_bytes') == BASELINE_BYTES,
            'GUI admission does not bind the exact installed ab1 ZIP')
    expected = {name: dict(sha256=sha(body), bytes=len(body)) for name, body in installed.items()}
    preserved = {name: value for name, value in expected.items() if name != GUI_PATH}
    require(report.get('baseline_install_files') == expected and report.get('preserved_files') == preserved,
            'GUI admission does not preserve every installed module/helper')
    actual = report.get('runtime', {})
    require(actual.get('sha256') == sha(runtime) and actual.get('bytes') == len(runtime) and
            actual.get('header') == verify(runtime), 'GUI admission used another runtime envelope')
    require(report.get('actual_outer_production_GUI_probe') is True and
            report.get('actual_child_production_module_readers') is True,
            'GUI admission omitted the actual installed collection/child readers')
    return preserved


def package(args):
    require(args.output.suffix == '.zip', 'Use a ZIP output filename')
    require(not subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT).strip(),
            'Commit the final GUI source before packaging')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    installed, manifests, standard, cdda = baseline(args.baseline_package)
    provenance = receipt(args.runtime, commit, False)
    optimization = optimized_runtime_proof(args.runtime)
    require(all(re.search(r'(?:^|\s)-O2(?:\s|$)', producer)
                for producer in optimization['actual_linked_producers']),
            'The delivered GUI must contain the actual optimized O2 build')
    payload, memory = flatten_elf(args.runtime.read_bytes())
    runtime = envelope(payload, memory, commit[:12])
    runtime_sha = sha(args.runtime.read_bytes())
    admission = current_report(args.admission_report, commit, 'ZIP-backed GUI admission')
    preserved = admitted_baseline(admission, installed, runtime)
    current_report(args.performance_report, commit, 'GUI launch preparation proof', runtime_elf_sha256=runtime_sha)
    current_report(args.runtime_crc_report, commit, 'Linked GUI CRC proof', runtime_elf_sha256=runtime_sha)
    standard_binding = runtime_standard_manifest_binding(read_elf(args.runtime, allow_tls=True),
        standard['normalized_payload_sha256'])
    cdda_binding = cdda_runtime_binding(args.runtime, cdda)
    files = {GUI_PATH: runtime, **manifests,
        'evidence/runtime-compile.json': wire(provenance),
        'evidence/runtime-optimization.json': wire(optimization),
        'evidence/runtime-standard-binding.json': wire(standard_binding),
        'evidence/runtime-cdda-binding.json': wire(cdda_binding),
        'evidence/gui-packaged-admission.json': args.admission_report.read_bytes(),
        'evidence/gui-preparation-performance.json': args.performance_report.read_bytes(),
        'evidence/runtime-linked-crc.json': args.runtime_crc_report.read_bytes()}
    for path in args.evidence:
        target = 'evidence/' + path.name
        require(target not in files, 'Duplicate evidence name: ' + target)
        files[target] = path.read_bytes()
    files['evidence/preserved-installation.json'] = wire(dict(package_sha256=BASELINE_SHA256,
        package_bytes=BASELINE_BYTES, source_commit=BASELINE_COMMIT,
        baseline_install_files={name: dict(sha256=sha(body), bytes=len(body)) for name, body in installed.items()},
        preserved_files=preserved, modules_or_helpers_rebuilt=False, installed_module_files=26))
    files['README-FIRST.txt'] = f'''K-UI LAUNCH PREPARATION FIX
GUI source: {commit}

Copy the KUI folder onto the existing SD card and replace runtime.kui.
This overlay requires the installed ab1b145b33b4 Global Startup + Smash test.
Only the GUI changes. All26 installed loader/helper files, game images,
settings and VMU data remain in place. No loader rebuild or replacement is
required. Confirm GUI build {commit[:12]} after restarting. If Updates selects
another GUI bank, select its original/root version to run this copied GUI.
The current updater, restart and rollback features are retained.

The changes reduce repeated launch preparation work and correct the GUI's
admission of the already installed Smash collection. Use your existing game
settings. Sega Smash Pack still requires Standard / Background OFF / VMU ON.
Its CE return remains the existing terminal diagnostic; it is unfinished.

Try the launch that was slow or refused. Timing is written automatically to
/KUI/apps/games/launch-timing.txt. Startup/version-selection logs are appended
to /KUI/startup.txt, with the previous bounded log in /KUI/startup-previous.txt.
These text files capture the relevant GUI stages without a video recording.
Startup reports begin in the GUI; earlier CD bootstrap/firmware output is
outside their scope. A report write failure does not prevent startup.

Host proofs and exact installed-file identities are included in evidence.
Console elapsed times and universal game compatibility are not established.
This is a manual GUI overlay; no new offline-update package is supplied.
'''.encode()
    files['LICENSE.txt'] = (ROOT / 'LICENSE').read_bytes()
    for path in sorted((ROOT / 'LICENSES').iterdir()):
        if path.is_file():
            files['LICENSES/' + path.name] = path.read_bytes()
    with tempfile.TemporaryDirectory(prefix='launch-preparation-source-') as temporary:
        bundle = Path(temporary) / 'K-UI-source.bundle'
        subprocess.run(['git', 'bundle', 'create', str(bundle), 'HEAD',
            'updater-preserve/offline-updater-20261104'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        files['source/K-UI-source.bundle'] = bundle.read_bytes()
    require({name for name in files if name.startswith('KUI/')} == {GUI_PATH},
            'GUI-only overlay unexpectedly includes loader/helper files')
    files['BUILD.json'] = wire(dict(schema='kui.launch-preparation.gui-overlay.v1', source_commit=commit,
        source_dirty=False, installed_files={GUI_PATH: sha(runtime)}, baseline_package_sha256=BASELINE_SHA256,
        preserved_files=preserved, preserved_module_helper_count=26, loader_rebuilds=False,
        hardware_timing_measured=False, CE_menu_return_qualified=False, full_updater_release_enabled=False))
    files['SHA256SUMS.txt'] = ''.join(sha(body) + '  ' + name + '\n'
        for name, body in sorted(files.items())).encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix('.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, body in sorted(files.items()):
            archive.writestr(name, body)
    with zipfile.ZipFile(temporary) as archive:
        require(archive.testzip() is None, 'GUI overlay ZIP content CRC failed')
        require([name for name in archive.namelist() if name.startswith('KUI/')] == [GUI_PATH],
                'GUI overlay install inventory changed')
    temporary.replace(args.output)
    return dict(status='PASS', path=str(args.output), source_commit=commit,
        bytes=args.output.stat().st_size, sha256=sha(args.output.read_bytes()),
        installed_files=[GUI_PATH], preserved_module_helpers=26)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True, help='Final clean GUI ELF')
    parser.add_argument('--baseline-package', type=Path, required=True)
    parser.add_argument('--admission-report', type=Path, required=True)
    parser.add_argument('--performance-report', type=Path, required=True)
    parser.add_argument('--runtime-crc-report', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    print(json.dumps(package(parser.parse_args())))
