#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect the launcher update and unchanged GD3 test in ready-to-copy paths."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from check_retail_launcher_package import check
from runtime_package import envelope, flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
VIDEO_SHA = '0de59dcb06ae86d5c2564a762e0b8fc5e5ec7fedb783a63c0dacdaae8b146bd7'
VIDEO_MEMBER = 'pilot/15-toy-gd-three-sector.kui'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video-bundle', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if git('status', '--porcelain'):
        raise ValueError('Commit the reviewed source before packaging')
    commit = git('rev-parse', 'HEAD').decode().strip()
    original = args.video_bundle.read_bytes()
    if sha(original) != VIDEO_SHA:
        raise ValueError('Video input differs from the preserved audited bundle')
    elf = (ROOT / 'build/kui-runtime.elf').read_bytes()
    record = json.loads((ROOT / 'build/kui-runtime.compile.json').read_text())
    if record != {'commit': commit, 'source_dirty': False, 'elf_sha256': sha(elf)}:
        raise ValueError('Launcher link/source identity mismatch')
    payload, memory = flatten_elf(elf)
    launcher = envelope(payload, memory, commit[:12])
    if commit[:12].encode() + b'\0' not in payload:
        raise ValueError('Launcher visible identity is missing from linked payload')
    files = {
        'KUI/runtime.kui': launcher,
        'README.md': (ROOT / 'docs/launcher-video-install-fix.md').read_bytes(),
        'source-snapshot.tar': git('archive', '--format=tar', commit),
        'evidence/launcher.elf': elf,
        'evidence/launcher.map': (ROOT / 'build/kui-runtime.map').read_bytes(),
        'evidence/launcher.compile.json': (ROOT / 'build/kui-runtime.compile.json').read_bytes(),
    }
    with zipfile.ZipFile(args.video_bundle) as old:
        video = old.read(VIDEO_MEMBER)
        files['KUI/apps/games/retail-boot.kui'] = video
        files['evidence/video-build.json'] = old.read('build.json')
        files['evidence/video-source-snapshot.tar'] = old.read('source-snapshot.tar')
        files['evidence/video-test-instructions.md'] = old.read('README.md')
        for name in old.namelist():
            if name.startswith(('evidence/toy-pilot/', 'LICENSES/')) or name in ('LICENSE', 'THIRD_PARTY.md'):
                files[name] = old.read(name)
    video_info = check(video, launcher)
    if video_info['build'] != 'd4d18c11ffeb' or len(video) != 61716:
        raise ValueError('Unexpected video file identity')
    for name in ('launcher-logo-rendering-2026-10-08.md',
                 'launcher-logo-comparison-2026-10-08.png',
                 'launcher-package-refusal-2026-10-08.md'):
        files['evidence/' + name] = (ROOT / 'docs/evidence' / name).read_bytes()
    for name in ('launcher-build.log', 'launcher-checks.log'):
        files['evidence/' + name] = (ROOT / 'build' / name).read_bytes()
    newlib = ROOT / '.deps/kos/utils/kos-chain/newlib-4.6.0.20260123'
    for name in ('COPYING.NEWLIB', 'COPYING.LIBGLOSS'):
        files['LICENSES/' + name] = (newlib / name).read_bytes()
    files['LICENSES/GCC-Runtime-Exception.txt'] = \
        (ROOT / 'LICENSES/GCC-Runtime-Exception.txt').read_bytes()
    files['LICENSES/KOS-AUTHORS'] = (ROOT / '.deps/kos/AUTHORS').read_bytes()
    for path in sorted((ROOT / '.deps/kos/doc/license').glob('LICENSE.*')):
        files['LICENSES/KOS/' + path.name] = path.read_bytes()
    metadata = {
        'launcher': {**verify(launcher), 'commit': commit, 'file': 'KUI/runtime.kui',
                     'sha256': sha(launcher), 'hardware_tested': False},
        'video': {**video_info, 'file': 'KUI/apps/games/retail-boot.kui',
                  'sha256': sha(video), 'unchanged_from_previous_delivery': True,
                  'original_bundle_sha256': VIDEO_SHA, 'hardware_tested': False,
                  'worker_unchanged_from_clean_cdda_baseline': True},
        'clean_cdda_baseline': '7b55156aafa2',
        'validation': {'production_C_layout_gate': True, 'malformed_rejection_cases': 11,
                       'launcher_as_game_package_rejected': True},
    }
    files['build.json'] = (json.dumps(metadata, indent=2) + '\n').encode()
    files['SHA256SUMS'] = ''.join(f'{sha(data)}  {name}\n' for name, data in sorted(files.items())).encode()
    if {name for name in files if name.endswith('.kui')} != \
            {'KUI/runtime.kui', 'KUI/apps/games/retail-boot.kui'}:
        raise ValueError('Unexpected installable files')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for name, data in sorted(files.items()):
            out.writestr(name, data)
    with zipfile.ZipFile(args.output) as out:
        if out.testzip() is not None or set(out.namelist()) != set(files):
            raise ValueError('Final ZIP integrity failure')
        for name, expected in files.items():
            if out.read(name) != expected:
                raise ValueError('Final member differs: ' + name)
    print(json.dumps({'file': str(args.output), 'bytes': args.output.stat().st_size,
                      'sha256': sha(args.output.read_bytes()), 'builds': metadata}, indent=2))


if __name__ == '__main__':
    main()
