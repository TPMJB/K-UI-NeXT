#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Archive merged source while installing only the retained clean CDDA binaries."""
import argparse
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile

from package_cdda_calibration import (
    ROOT, archive_name, build_config, git, object_id, read_file, sha, write_archive,
)
from package_cdda_toy_pilot import pilot_layout
from package_toy_shared_sci import launcher_gate
from runtime_package import flatten_elf, verify

OUTPUT_NAME = 'K-UI-Integrated-Checkpoint.zip'
CLEAN_BUNDLE_SHA = 'ffd6628beaf4c964111598d1e19d902363b387c74b4b018ae11e9afeb43e2aae'
CLEAN_MEMBER = 'pilot/15-toy-eight-block-ring.kui'
CLEAN_GAME_SHA = '161ea24d655b6531867c6704c57a57c1ee25668e534d2f56dc793a5cd73a80de'
CLEAN_COMMIT = '7b55156aafa26a74c0c3ef3f595a9756cba5c73d'
CLEAN_WORKER_SHA = 'f3b510df3034634e01e3dd911711f94551062534d6cbf4b635900a366f903d3a'
LAUNCHER_COMMIT = '6c4a9915ba334cdcb871b9ff055cd7864b88d0ab'
GAME_TARGET = 'KUI/apps/games/retail-boot.kui'
LAUNCHER_TARGET = 'KUI/runtime.kui'
HOST_LOG = 'docs/evidence/toy-shared-sci-host-tests.txt'


def external_bytes(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError('External input must be a regular file: ' + str(path))
    return path.read_bytes()


def trusted_clean(bundle):
    if sha(bundle) != CLEAN_BUNDLE_SHA:
        raise ValueError('Clean eight-block bundle differs from the retained delivery')
    with zipfile.ZipFile(io.BytesIO(bundle)) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or archive.testzip():
            raise ValueError('Clean bundle has duplicate or damaged members')
        for name in names:
            archive_name(name)
        game = archive.read(CLEAN_MEMBER)
        metadata = json.loads(archive.read('build.json'))
        if (sha(game) != CLEAN_GAME_SHA or verify(game)['build'] != CLEAN_COMMIT[:12] or
                metadata['source_commit'] != CLEAN_COMMIT):
            raise ValueError('Clean game byte/source identity changed')
        evidence = {name: archive.read(name) for name in names
                    if name.startswith('evidence/toy-pilot/')}
        evidence['build.json'] = archive.read('build.json')
        evidence['source-snapshot.tar'] = archive.read('source-snapshot.tar')
        if sha(evidence['source-snapshot.tar']) != metadata['source_snapshot_sha256']:
            raise ValueError('Clean game source snapshot identity changed')
    return game, metadata, evidence


def bundle_identity(path, data, commit):
    # A prerequisite-free bundle can restore the source without a remote or an
    # older local checkout. Verify the real bundle, not a filename or label.
    header = data.split(b'\n\n', 1)[0]
    if not header.startswith((b'# v2 git bundle\n', b'# v3 git bundle\n')):
        raise ValueError('Unrecognized Git bundle header')
    if any(line.startswith(b'-') for line in header.splitlines()):
        raise ValueError('Git bundle requires missing external history')
    heads = subprocess.check_output(['git', 'bundle', 'list-heads', str(path)],
                                    cwd=ROOT, text=True).splitlines()
    refs = {}
    for line in heads:
        value, separator, name = line.partition(' ')
        if not separator or not re.fullmatch(r'[0-9a-f]{40}', value) or name in refs:
            raise ValueError('Malformed or duplicate Git bundle head')
        refs[name] = value
    if commit not in refs.values():
        raise ValueError('Git bundle does not retain the exact merged checkpoint')
    result = subprocess.run(['git', 'bundle', 'verify', str(path)], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise ValueError('Git bundle verification failed: ' + result.stderr.strip())
    expected_tree = git('rev-parse', commit + '^{tree}')
    with tempfile.TemporaryDirectory(prefix='kui-checkpoint-restore-') as temporary:
        recovered = Path(temporary) / 'source.git'
        result = subprocess.run(['git', 'clone', '--mirror', '--quiet', str(path), str(recovered)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if result.returncode:
            raise ValueError('Git bundle cannot restore into an empty repository: ' + result.stderr.strip())
        result = subprocess.run(['git', '-C', str(recovered), 'fsck', '--full', '--strict'],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if result.returncode:
            raise ValueError('Recovered Git bundle fails full strict object verification: ' + result.stderr.strip())
        restored_commit = subprocess.check_output(['git', '-C', str(recovered), 'rev-parse',
                                                   '--verify', commit + '^{commit}'], text=True).strip()
        restored_tree = subprocess.check_output(['git', '-C', str(recovered), 'rev-parse',
                                                 '--verify', commit + '^{tree}'], text=True).strip()
        if restored_commit != commit or restored_tree != expected_tree:
            raise ValueError('Recovered Git bundle checkpoint/tree differs from merged source')
        for ancestor in (CLEAN_COMMIT, LAUNCHER_COMMIT):
            result = subprocess.run(['git', '-C', str(recovered), 'merge-base', '--is-ancestor',
                                     ancestor, commit], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode:
                raise ValueError('Recovered Git history omits retained source ancestry: ' + ancestor)
    return {'file': 'source-history.bundle', 'sha256': sha(data), 'bytes': len(data),
            'prerequisites': [], 'verified': True, 'refs': refs,
            'restored_into_empty_repository': True, 'fsck_full_strict': True,
            'restored_commit': restored_commit, 'restored_tree': restored_tree,
            'retained_source_ancestors_restored': [CLEAN_COMMIT, LAUNCHER_COMMIT],
            'checkpoint_refs': sorted(name for name, value in refs.items() if value == commit)}


def collect(commit, default_build_dir, bundle_path, clean_path, launcher_path,
            host_test_log=HOST_LOG):
    if git('status', '--porcelain') or git('rev-parse', 'HEAD') != commit:
        raise ValueError('Commit the exact reviewed merged source before packaging')
    tree = git('rev-parse', 'HEAD^{tree}')
    tracked = {name for name in subprocess.check_output(
        ['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0') if name}
    for name in tracked:
        archive_name(name)
    for ancestor in (CLEAN_COMMIT, LAUNCHER_COMMIT):
        if subprocess.run(['git', 'merge-base', '--is-ancestor', ancestor, commit],
                          cwd=ROOT, check=False).returncode:
            raise ValueError('Merged checkpoint does not contain retained source: ' + ancestor)
    makefile = read_file(ROOT / 'Makefile.toy_pilot').decode()
    defaults = {'GD_FIXED_STEP': '2', 'SHARED_SCI': '0', 'ASYNC_CDDA': '0'}
    for flag, value in defaults.items():
        if re.findall(r'^' + flag + r'\s*\?=\s*(\d+)\s*$', makefile, re.M) != [value]:
            raise ValueError('Toy default differs from the retained clean policy: ' + flag)

    files, inputs, external = {}, {}, {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    def remember(path):
        data = read_file(path)
        digest = sha(data)
        if path in inputs and inputs[path] != digest:
            raise ValueError('Build evidence changed while collecting')
        inputs[path] = digest
        return data

    def outside(path):
        path = Path(path).resolve()
        data = external_bytes(path)
        external[path] = sha(data)
        return data

    clean_bytes, launcher_bytes, history = (outside(path) for path in
                                          (clean_path, launcher_path, bundle_path))
    game, _, clean_evidence = trusted_clean(clean_bytes)
    launcher, licenses, launcher_evidence, launcher_source, gate = launcher_gate(launcher_bytes, game)
    history_info = bundle_identity(Path(bundle_path).resolve(), history, commit)
    add(GAME_TARGET, game)
    add(LAUNCHER_TARGET, launcher)
    add('source-history.bundle', history)
    for name, data in clean_evidence.items():
        add('evidence/retained-clean/' + name, data)
    for name, data in launcher_evidence.items():
        add('evidence/retained-launcher/' + name, data)
    add('evidence/retained-launcher/source-snapshot.tar', launcher_source)
    for name, data in licenses.items():
        add(name, data)
    for name in sorted(tracked):
        if name == 'LICENSE' or name == 'THIRD_PARTY.md' or name.startswith('LICENSES/'):
            data = remember(ROOT / name)
            if name in files:
                if files[name] != data:
                    raise ValueError('Retained and merged licence notices differ: ' + name)
            else:
                add(name, data)

    build = (ROOT / default_build_dir).resolve()
    if not build.is_relative_to(ROOT) or build == ROOT:
        raise ValueError('Default build evidence must be inside the source checkout')
    config_bytes = remember(build / 'build-config')
    config = build_config(build / 'build-config')
    required = {'BUILD': commit[:12], 'PROFILE': '15', 'PILOT': '1', 'LOW': '1',
                'SLOTS': '64', 'SCI': '1', 'SCI_PACED': '1', 'SCI_FAULT': '1',
                'SCI_REUSE_TDRE': '0', 'GD_FIXED_STEP': '2', 'SHARED_SCI': '0',
                'ASYNC_CDDA': '0', 'OPT': '-Os -fno-tree-scev-cprop'}
    if config != required:
        raise ValueError('Complete default build configuration differs from the clean policy')
    default_runtime = remember(build / 'retail-toy-pilot.kui')
    envelope = verify(default_runtime)
    payload, memory = flatten_elf(remember(build / 'entry.elf'))
    if (envelope['build'] != commit[:12] or default_runtime[64:] != payload or
            envelope['memory_bytes'] != memory or commit[:12].encode() + b'\0' not in payload):
        raise ValueError('Merged default source, runtime and linked ELF identity disagree')
    linked = pilot_layout(build, remember, gd_fixed_step=2)
    if linked['embedded_blobs']['worker']['sha256'] != CLEAN_WORKER_SHA:
        raise ValueError('Merged default worker differs from the retained clean eight-block worker')
    worker_stack = linked['worker_stack']
    worker_stack['margin'] = worker_stack['available_bytes'] - worker_stack['conservative_bytes']
    add('evidence/merged-default/build-config', config_bytes)
    add('evidence/merged-default/toy_pilot_resident_symbols.h',
        remember(build / 'toy_pilot_resident_symbols.h'))
    evidence_sha = {}
    for path in sorted(build.rglob('*')):
        if path.is_file() and path.suffix in ('.elf', '.map', '.su'):
            name = path.relative_to(build).as_posix()
            data = remember(path)
            if not data:
                raise ValueError('Empty merged default build evidence: ' + name)
            add('evidence/merged-default/' + name, data)
            evidence_sha[name] = sha(data)
    if any(not {name + '.elf', name + '.map'} <= set(evidence_sha)
           for name in ('entry', 'stage', 'resident-sci', 'worker')):
        raise ValueError('Missing linked default ELF/map evidence')

    from test_toy_pilot import SUITES
    suite_count = len(SUITES) + 4
    log_path = (ROOT / host_test_log).resolve()
    if not log_path.is_relative_to(ROOT):
        raise ValueError('Host regression log must be inside the merged source checkout')
    log = remember(log_path)
    footer = f'{suite_count} Toy pilot regression suites passed'.encode()
    if not log.rstrip().endswith(footer) or b'FAIL' in log:
        raise ValueError('Missing exact complete host regression success footer')
    add('evidence/merged-host-tests.txt', log)
    for name in sorted(tracked):
        if (name.startswith('docs/evidence/toy-') or
                name.startswith('docs/evidence/cdda-eight-block-') or
                name in {'docs/toy-shared-sci-test.md', 'docs/cdda-toy-eight-block-test.md'}):
            add(name, remember(ROOT / name))

    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', commit], cwd=ROOT)
    add('source-snapshot.tar', snapshot)
    add('source-state.txt', ('Unpublished merged local checkpoint; no remote publication claimed.\n'
        'Commit: ' + commit + '\nTree: ' + tree + '\n'
        'Installed game and launcher retain their original 7b/6c source identities.\n').encode())
    metadata = {'source_commit': commit, 'source_tree': tree, 'source_dirty': False,
        'source_publication': 'unpublished merged local checkpoint; full source and history included',
        'source_snapshot_sha256': sha(snapshot), 'source_history': history_info,
        'scope': 'Restore retained clean eight-block CDDA game runtime with fixed launcher; preserve merged opt-in source experiments',
        'new_video_fix': False, 'experimental_binary_installed': False,
        'installed_game': {**verify(game), 'file': GAME_TARGET, 'bytes': len(game),
            'sha256': sha(game), 'source_commit': CLEAN_COMMIT,
            'unchanged_retained_bytes': True, 'source_bundle_sha256': sha(clean_bytes),
            'source_bundle_member': CLEAN_MEMBER,
            'hardware_evidence_scope': 'Retained original console run and listening report; this combined installation has not been retested'},
        'installed_launcher': {**verify(launcher), 'file': LAUNCHER_TARGET, 'bytes': len(launcher),
            'sha256': sha(launcher), 'source_commit': LAUNCHER_COMMIT,
            'unchanged_retained_bytes': True, 'source_bundle_sha256': sha(launcher_bytes)},
        'launcher_layout_gate': gate,
        'merged_default_validation': {'configuration': config, 'runtime_installed': False,
            'runtime_sha256': sha(default_runtime), 'envelope': envelope,
            'linked_layout': linked, 'evidence_sha256': evidence_sha,
            'worker_identical_to_retained_clean': True},
        'host_regression_suites': suite_count, 'host_regression_log_sha256': sha(log),
        'async_CDDA_experiment': {'released': False, 'hardware_tested': False,
            'withheld_reason': 'Single-sector staging cannot sustain 75 sectors/s under unchanged safe worker cadence'},
        'game_driver_or_card_configuration_included': False}
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    add('README.md', f'''# K-UI integrated checkpoint

This package restores the retained working eight-block CDDA runtime. It does
not introduce a video fix. The installed bytes are unchanged: game
`7b55156aafa2`, launcher `6c4a9915ba33`.

With the Dreamcast powered off, copy the `KUI` folder onto the SD card. This
replaces only `/KUI/runtime.kui` and `/KUI/apps/games/retail-boot.kui`. Preserve
the existing card-path configuration, original GDI and original track files;
this ZIP supplies no game data or driver files. Safely eject and cold boot
using SCI. Preserve previous ZIPs and runtime backups.

The complete merged source checkpoint is `{commit}`. Its own default build
was checked with GD_FIXED_STEP=2, SHARED_SCI=0 and ASYNC_CDDA=0, and its worker
matches the retained clean worker. That validation build is not installed.
Experimental transport implementations remain opt-in source; no experimental
binary is installed by this ZIP. The single-sector async CDDA experiment is
unreleased because the unchanged safe worker cadence cannot sustain CDDA's
75-sector-per-second demand.

The source checkpoint and installed binary identities are separate and are
recorded in `build.json`. `source-snapshot.tar` contains the exact merged tree;
`source-history.bundle` retains complete Git history without external
prerequisites. No remote push is claimed. Exact retained binary source and
linked evidence are under `evidence/retained-clean` and
`evidence/retained-launcher`. The merged default build's ELF/map/stack evidence
and complete {suite_count}-suite host log are also included. The retained
launcher's actual C layout gate accepts the clean game package and rejects
11 malformed or wrong-file packages under ASan/UBSan. These checks do not
establish console video smoothness or a new combined-installation result.

`SHA256SUMS` covers every other ZIP member.
'''.encode())
    if {name for name in files if name.endswith('.kui')} != {GAME_TARGET, LAUNCHER_TARGET}:
        raise ValueError('Checkpoint must contain only the two retained installed runtime files')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD') != commit or
            git('rev-parse', 'HEAD^{tree}') != tree):
        raise ValueError('Merged source changed while collecting')
    for path, digest in inputs.items():
        if sha(read_file(path)) != digest:
            raise ValueError('Source/build evidence changed while collecting: ' + str(path))
    for path, digest in external.items():
        if sha(external_bytes(path)) != digest:
            raise ValueError('Retained bundle or history changed while collecting: ' + str(path))
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode())
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-commit', required=True, type=object_id)
    parser.add_argument('--default-build-dir', required=True, type=Path)
    parser.add_argument('--git-bundle', required=True, type=Path)
    parser.add_argument('--clean-bundle', required=True, type=Path)
    parser.add_argument('--launcher-bundle', required=True, type=Path)
    parser.add_argument('--host-test-log', type=Path, default=Path(HOST_LOG))
    args = parser.parse_args()
    try:
        if args.output.is_symlink() or args.output.exists() or args.output.name != OUTPUT_NAME:
            raise ValueError('Use a new ' + OUTPUT_NAME + ' path; preserve previous artifacts')
        write_archive(args.output.resolve(), collect(args.source_commit,
            args.default_build_dir, args.git_bundle, args.clean_bundle, args.launcher_bundle,
            args.host_test_log))
        print(json.dumps({'file': str(args.output.resolve()), 'bytes': args.output.stat().st_size,
            'sha256': sha(args.output.read_bytes()), 'source_commit': args.source_commit,
            'installed_game': CLEAN_COMMIT[:12], 'installed_launcher': LAUNCHER_COMMIT[:12]}, indent=2))
    except (OSError, ValueError, UnicodeError, KeyError, struct.error,
            subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
