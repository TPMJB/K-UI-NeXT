#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compose the actual global loader rollout and unified Smash next test."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from global_loader_rollout import DEFAULT_BUILDS, collect
from check_shared_loader_performance import receipt, runtime_standard_manifest_binding
from check_vmu_native_worker_gateway import read_elf
from package_smash_native_menu_test import optimized_runtime_proof
from runtime_package import envelope, flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
PRESERVED_PACKAGE_SHA = '23c3a4387c44980682b643f62d732e60eb4037dc307822b7d8a75f0a009d94ed'
PRESERVED = ('KUI/apps/games/retail-boot.kui', 'KUI/apps/games/retail-native-dual.kui',
             'KUI/apps/games/retail-native-dual-a1.kui', 'KUI/apps/disc/native-disc-vmu.kui',
             'KUI/apps/disc/native-disc-vmu-a1.kui')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def require(value, message):
    if not value:
        raise ValueError(message)


def preserved(path, header):
    require(sha(path.read_bytes()) == PRESERVED_PACKAGE_SHA, 'Preserved cohort package changed')
    files = {}
    with zipfile.ZipFile(path) as archive:
        manifest = json.loads(archive.read('evidence/standard-build-manifest.json'))
        require(sha(header.read_bytes()) == manifest['header_sha256'], 'Preserved Standard header changed')
        for name in PRESERVED:
            data = archive.read(name)
            require(verify(data)['build'] == 'dbadd024e2ff', 'Preserved cohort build changed')
            files[name] = data
        require(sha(files[PRESERVED[0]]) == manifest['module_sha256'], 'Standard module differs from its manifest')
        payload = files[PRESERVED[0]][64:]
        at, count = manifest['profile_payload_offset'], manifest['profile_bytes']
        require(sha(payload[:at] + bytes(count) + payload[at + count:]) == manifest['normalized_payload_sha256'],
                'Preserved Standard normalized code changed')
        for name in ('compact_release_regressions.json', 'standard_safety.json',
                     'background_a1_safety.json', 'background_a2_safety.json',
                     'disc_a1_safety.json', 'disc_a2_safety.json'):
            files['evidence/preserved-dbadd/' + name] = archive.read('evidence/' + name)
    files['evidence/preserved-standard-manifest.h'] = header.read_bytes()
    files['evidence/preserved-standard-manifest.json'] = wire(manifest)
    files['evidence/preserved-cohort.json'] = wire(dict(package=path.name, package_sha256=PRESERVED_PACKAGE_SHA,
        unchanged_sha256={name: sha(files[name]) for name in PRESERVED},
        worker_KiB=72, combined_reservation_KiB=284, combined_cache_KiB=192,
        source_commit='dbadd024e2ff2b61f0052ead008b6a4a987d41f7', rebuilt=False))
    return files


def source_current(report, name):
    require(report.get('status') == 'PASS', name + ' did not pass')
    hashes = report.get('source_sha256', {})
    require(hashes and all(sha((ROOT / path).read_bytes()) == digest for path, digest in hashes.items()),
            name + ' does not match current source')


def package(args):
    require(not subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT).strip(),
            'Final package requires committed source')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    files, rollout = collect(DEFAULT_BUILDS, commit, runtime_elf=args.runtime)
    files.update(preserved(args.preserved_package, args.standard_header))
    runtime_receipt = receipt(args.runtime, commit, False)
    files['evidence/runtime-compile.json'] = wire(runtime_receipt)
    files['evidence/runtime-optimization.json'] = wire(optimized_runtime_proof(args.runtime))
    standard_manifest = json.loads(files['evidence/preserved-standard-manifest.json'])
    files['evidence/runtime-standard-binding.json'] = wire(runtime_standard_manifest_binding(
        read_elf(args.runtime, allow_tls=True), standard_manifest['normalized_payload_sha256']))
    payload, memory = flatten_elf(args.runtime.read_bytes())
    files['KUI/runtime.kui'] = envelope(payload, memory, commit[:12])
    # The caller supplies the independently qualified, source-bound Smash
    # report and its explicit delivered paths. Never discover install files
    # by recursively copying a build directory.
    smash = json.loads(args.smash_report.read_text())
    source_current(smash, 'Smash linked qualification')
    require(smash.get('source_commit') == commit and smash.get('source_dirty') is False and
            not smash.get('development_validation_only', False),
            'Smash qualification is not this clean source cohort')
    paths = smash.get('install_files', {})
    expected = {'KUI/apps/games/retail-smash-menu.kui', 'KUI/apps/games/retail-smash-native-menu.kui',
                'KUI/apps/games/retail-smash-vc2.kui', 'KUI/apps/games/retail-smash-swirl.kui',
                'KUI/apps/games/smash-vc2-vmu-helper.bin', 'KUI/apps/games/smash-swirl-vmu-helper.bin'}
    require(set(paths) == expected, 'Smash install file set is incomplete')
    for target, record in paths.items():
        data = Path(record['path']).read_bytes()
        require(sha(data) == record['sha256'], 'Smash qualified artifact changed: ' + target)
        if target.endswith('.kui'):
            require(verify(data)['build'] == commit[:12], 'Smash module build differs from source')
        files[target] = data
    files['evidence/smash-linked.json'] = args.smash_report.read_bytes()
    policy = json.loads(args.policy_report.read_text())
    source_current(policy, 'Global launch policy')
    files['evidence/global-launch-policy.json'] = args.policy_report.read_bytes()
    require(len(policy['profiles']) >= 25, 'Global policy lost normal loader families')
    require(len([name for name in files if name.startswith('KUI/')]) == 27,
            'Global/Smash installation coverage changed')
    for path in args.evidence:
        target = 'evidence/' + path.name
        require(target not in files, 'Duplicate evidence name: ' + target)
        files[target] = path.read_bytes()
    files['LICENSE.txt'] = (ROOT / 'LICENSE').read_bytes()
    for path in sorted((ROOT / 'LICENSES').iterdir()):
        if path.is_file():
            files['LICENSES/' + path.name] = path.read_bytes()
    files['GLOBAL-STARTUP.txt'] = (ROOT / 'docs/global-launch-policy.md').read_bytes()
    files['README-FIRST.txt'] = f'''K-UI GLOBAL STARTUP + SMASH PACK NEXT TEST
Source: {commit}

Copy the KUI folder over the KUI folder on the SD card. Keep your existing
settings, game images and VMU data. This is a manual loader test package;
it is not the withdrawn full-cohort offline updater package.

This ZIP includes the previously omitted ordinary physical, CE, compressed,
standalone and CDDA loader routes. Their shared cold stage uses automatic
batched reads and fast verification with every SD block CRC retained.
The existing Standard 72 KiB and compact Background 72 KiB/284 KiB modules
are authenticated, unchanged copies of the dbadd cohort. The new GUI retains
the latest reconciled offline updater source and its release safeguards.

NEXT TEST
1. Use your existing settings to time launch button -> bootstrap for Sonic
   Adventure or Skies of Arcadia. This checks the common policy; there is
   no new per-game speed option to enable.
2. For Sega Smash Pack, use Standard / Background OFF / virtual VMU ON.
   Launch normally through the original collection menu. Check Shining
   Force -> menu -> Phantasy Star II, including loading the existing saves.
   Record launch -> bootstrap and selection -> usable game screen separately.
3. Select Sega Swirl and Virtua Cop 2 from that same package/menu. Their
   matched CE helpers and shared card are now prepared without swapping
   retail-physical.kui. Check gameplay and the existing save/high-score path.

Native Shining Force/Phantasy Star II menu return is retained. CE return
remains a diagnostic: a blue STOP page marked SCER/UNAVAILABLE captures
the actual reset context. Photograph that page if encountered. It does not
mean CE return is fixed. Do not enable Background/prefetch for Smash Pack.

HOST RESULTS AND LIMITS
Current checks cover global policy, linked cache/CRC/stack behavior, exact
loader identities, retained CDDA playback configuration, original Smash
transfers, one shared card/history preparation, six-file write/readback and
failure publication. Console elapsed times have not yet been measured.
Other Genesis score/save classification follows these integration results.
Quake III Arena is unchanged and is not part of this test.

The source bundle includes this commit and reconciled updater history.
Newly built modules and preserved modules have separate provenance in evidence.
'''.encode()
    with tempfile.TemporaryDirectory(prefix='global-smash-source-') as temporary:
        bundle = Path(temporary) / 'K-UI-source.bundle'
        subprocess.run(['git', 'bundle', 'create', str(bundle), 'HEAD',
                        'updater-preserve/offline-updater-20261104'], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL)
        files['source/K-UI-source.bundle'] = bundle.read_bytes()
    files['BUILD.json'] = wire(dict(source_commit=commit, source_dirty=False,
        installed_files={name: sha(data) for name, data in files.items() if name.startswith('KUI/')},
        global_rollout_roles=sorted(rollout['profiles']), preserved_roles=list(PRESERVED),
        hardware_timing_measured=False, CE_menu_return_qualified=False,
        full_updater_release_enabled=False))
    files['SHA256SUMS.txt'] = ''.join(sha(data) + '  ' + name + '\n' for name, data in sorted(files.items())).encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix('.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(temporary) as archive:
        require(archive.testzip() is None, 'ZIP content CRC failed')
    temporary.replace(args.output)
    print(json.dumps(dict(status='PASS', path=str(args.output), source_commit=commit,
        bytes=args.output.stat().st_size, sha256=sha(args.output.read_bytes()), installed_files=27)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--preserved-package', type=Path, required=True)
    parser.add_argument('--standard-header', type=Path, required=True)
    parser.add_argument('--smash-report', type=Path, required=True)
    parser.add_argument('--policy-report', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    package(parser.parse_args())
