#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package a failure-only Worms WinCE primary-map diagnostic update."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from package_armada_excerpt import check, verify_build
from package_wince_ce_stream import audit_stream, write_zip
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
BASE = 'a7c85a8a2a85804e9c9639b2ae62e2c5ed48d3a7'
BASE_ZIP_SHA = '9e867bb8bf93eebbd30f1f77494fe5d4e95000beb2f001f35193513659bbd7e7'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--evidence', action='append', default=[], metavar='NAME=PATH')
    args = parser.parse_args()
    commit = git('rev-parse', 'HEAD').decode().strip()
    check(not git('status', '--porcelain'), 'Commit intended diagnostic source first')
    subprocess.run(['git', 'merge-base', '--is-ancestor', BASE, commit], cwd=ROOT, check=True)
    check(sha(args.baseline.read_bytes()) == BASE_ZIP_SHA, 'Unexpected experimental baseline ZIP')
    files = {}
    with zipfile.ZipFile(args.baseline) as old:
        check(old.testzip() is None, 'Baseline ZIP CRC failure')
        for row in old.read('SHA256SUMS').decode().splitlines():
            expected, name = row.split('  ', 1)
            check(sha(old.read(name)) == expected, 'Baseline member changed: ' + name)
        for name in old.namelist():
            if name in ('LICENSE', 'THIRD_PARTY.md') or name.startswith(('LICENSES/', 'source/dependencies/')):
                files[name] = old.read(name)
    directory = ROOT / 'build/worms-ce-stream'
    elf, package = directory / 'entry.elf', directory / 'ce-cdda-worms.kui'
    record, record_bytes = verify_build(elf, commit)
    data = package.read_bytes()
    envelope = verify(data)
    payload, memory = flatten_elf(elf.read_bytes())
    check(data[64:] == payload and envelope['memory_bytes'] == memory and
          envelope['build'] == commit[:12], 'Diagnostic ELF/envelope/source mismatch')
    seed, audit = audit_stream(directory, data)
    check(audit['title_profile'] == 'worms-us' and audit['stream_profile'] is True and
          audit['distributed_pcm_bytes'] == 0 and seed['wire_all_zero'] is True,
          'Wrong Worms stream package or embedded owner data')
    member = 'KUI/apps/games/ce-cdda-worms.kui'
    files[member] = data
    files['evidence/worms-compile.json'] = record_bytes
    files['evidence/worms-layout-and-stack.json'] = encoded(audit)
    files['source/K-UI-Worms-Map-Diagnostic-' + commit[:12] + '.tar.gz'] = gzip.compress(
        git('archive', '--format=tar', '--prefix=K-UI-Worms-Map-Diagnostic/', commit), mtime=0)
    files['source/primary-map-diagnostic.patch'] = git('diff', '--binary', BASE, commit)
    files['docs/wince-map-diagnostic.md'] = (ROOT / 'docs/wince-map-diagnostic.md').read_bytes()
    for item in args.evidence:
        name, path = item.split('=', 1)
        check(name and '/' not in name and '\\' not in name, 'Evidence must use a basename')
        files['evidence/' + name] = Path(path).read_bytes()
    files['build.json'] = encoded({
        'source_commit': commit, 'source_dirty': False, 'base_commit': BASE,
        'baseline_zip_sha256': BASE_ZIP_SHA, 'installed_files': [member],
        'runtime_menu_replaced': False, 'primary_map_diagnostic_only': True,
        'map_validation_weakened': False, 'playback_pacing_retuned': False,
        'root_cause_confirmed': False, 'hardware_tested': False,
        'envelope': envelope, 'compile': record, 'seed': seed,
        'package_sha256': sha(data)})
    files['README.md'] = (f'# Worms WinCE launch-map diagnostic\n\nBuild **{commit[:12]}**.\n\n'
        'Merge the KUI folder over the previously installed experimental update with the console off. '
        'This replaces only ce-cdda-worms.kui; the menu and other game loaders stay as installed.\n\n'
        'Repeat the same 2048-byte Worms image with Background loading On and CD music On. '
        'If launch stops, send one clear photograph of the entire screen. It now reports the '
        'map pointer, exact failed checks, stored/calculated/source CRCs and map flags/counts.\n\n'
        'The reported failure has not reproduced in the real-package preparation test, using '
        'the supplied kernel with a real SHA and actual entry relocation instructions. This '
        'diagnostic does not claim a confirmed fix. Validation remains strict, storage remains '
        'read only, and audio scheduling/copies are unchanged. Source, patch, audits and '
        'dependency sources/licenses are included. No game/kernel/audio payload is distributed.\n').encode()
    check(not git('status', '--porcelain'), 'Diagnostic source changed during packaging')
    write_zip(args.output, files, installed_files=[member])
    print(json.dumps({'path': str(args.output.resolve()), 'build': commit[:12],
        'bytes': args.output.stat().st_size, 'sha256': sha(args.output.read_bytes()),
        'installed_files': [member], 'root_cause_confirmed': False}, indent=2))


if __name__ == '__main__':
    main()
