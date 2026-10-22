#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the audited read-only native A1 probe and exact source checkpoint."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from check_vmu_native_readonly import check


def package(build):
    root = Path(__file__).resolve().parents[1]
    build = Path(build)

    def git(*arguments):
        return subprocess.check_output(
            ['git', *arguments], cwd=root, text=True).strip()

    commit = git('rev-parse', 'HEAD')
    revision = git('rev-parse', '--short=12', 'HEAD')
    if git('status', '--porcelain'):
        raise ValueError('Commit source changes before packaging a reviewable build')
    report = check(build, self_test=True)
    # This binds the current module, linked stage and relocated worker before
    # assigning the source identity. A clean but stale build is refused too.
    if report['allocator_bootstrap_audit']['runtime']['build'] != revision:
        raise ValueError('Fresh build/source commit mismatch')
    report.update(
        source_commit=commit, source_build_id=revision,
        console_execution_verified=False, maple_interception_installed=True,
        virtual_replies_enabled=True, storage_writes_enabled=False,
        save_writes_enabled=False, target_port=0, target_unit='A1',
        pair_limit=3600, expected_phase=13,
        expected_proof={'device_info_or_all_info': 1, 'media_info': 1, 'block_reads': 1},
        compatibility_claim='Bounded diagnostic for the tested native SDK family only')

    branch = git('branch', '--show-current')
    source = (build / 'native-readonly-source.bundle').resolve()
    refs = ['HEAD', branch] if branch else ['HEAD']
    subprocess.run(['git', 'bundle', 'create', str(source), *refs], cwd=root, check=True)
    subprocess.run(['git', 'bundle', 'verify', str(source)], cwd=root, check=True,
                   stdout=subprocess.DEVNULL)
    if git('rev-parse', 'HEAD') != commit or git('status', '--porcelain'):
        raise ValueError('Source changed during auditing; rebuild before packaging')

    files = {
        'KUI/apps/games/retail-boot.kui': (build / 'retail-maple-observe.kui').read_bytes(),
        'README-FIRST.md': (root / 'docs/vmu-native-readonly-test.md').read_bytes(),
        'probe-report.json': (json.dumps(report, indent=2) + '\n').encode(),
        'LICENSE': (root / 'LICENSE').read_bytes(),
        'THIRD_PARTY.md': (root / 'THIRD_PARTY.md').read_bytes(),
        f'source/K-UI-native-readonly-{revision}.bundle': source.read_bytes(),
    }
    files['SHA256SUMS.txt'] = ''.join(
        hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
        for name, data in sorted(files.items())).encode()
    output = build / 'K-UI-Native-ReadOnly-Test.zip'
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(output) as archive:
        if set(archive.namelist()) != set(files) or any(
                archive.read(name) != data for name, data in files.items()):
            raise ValueError('ZIP byte verification failed')
    print(json.dumps({
        'zip': str(output), 'bytes': output.stat().st_size, 'commit': commit,
        'module_sha256': hashlib.sha256(files['KUI/apps/games/retail-boot.kui']).hexdigest(),
    }))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    try:
        package(parser.parse_args().build)
    except (OSError, ValueError, KeyError, AssertionError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'Native read-only package refused: {error}\n')
