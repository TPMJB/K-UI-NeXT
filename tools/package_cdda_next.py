#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle the proven baseline and three new SCI CDDA experiments."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
BASE = '057f0e13c0c9e4e4e6f7c3365a47002467470277'
MARKER = bytes.fromhex('4b554953424f4f540100000003000000fcffffff')
PROFILES = ((1, 'controls'), (2, 'soak'), (3, 'stress'))


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--published-tree', required=True,
                        help='Tree SHA independently verified on the published commit')
    parser.add_argument('--baseline-package', required=True, type=Path)
    args = parser.parse_args()
    if git('status', '--porcelain'):
        parser.error('Commit reviewed source before packaging')
    commit = args.source_commit
    if len(commit) != 40 or any(c not in '0123456789abcdef' for c in commit):
        parser.error('Invalid published source commit')
    source_tree = git('rev-parse', 'HEAD^{tree}')
    if args.published_tree != source_tree:
        parser.error('Published source tree differs from the archived checkpoint')
    files = {}
    runtimes = []
    for profile, name in PROFILES:
        build = ROOT / ('build/cdda-' + name)
        config = dict(line.split('=', 1) for line in
                      (build / 'build-config').read_text().splitlines())
        if config.get('BUILD') != commit[:12] or config.get('PROFILE') != str(profile):
            parser.error('Wrong build configuration for ' + name)
        runtime = (build / 'cdda-harness.kui').read_bytes()
        info = verify(runtime)
        elf = (build / 'cdda-harness.elf').read_bytes()
        payload, memory = flatten_elf(elf)
        if (info['build'] != commit[:12] or runtime[64:] != payload or
                memory != 0x200000 or payload.count(MARKER) != 1):
            parser.error('Envelope/source/layout mismatch for ' + name)
        filename = f'runtimes/{profile:02d}-{name}.kui'
        files[filename] = runtime
        if profile == 1:
            files['KUI/runtime.kui'] = runtime
        runtimes.append({'profile': profile, 'name': name, 'file': filename,
                         'build': info['build'], 'hardware_tested': False,
                         'runtime_sha256': sha(runtime), 'elf_sha256': sha(elf),
                         'memory_footprint_bytes': memory})
        files[f'evidence/{name}/cdda-harness.elf'] = elf
        files[f'evidence/{name}/cdda-harness.map'] = (build / 'cdda-harness.map').read_bytes()
        for path in build.rglob('*.su'):
            files[f'evidence/{name}/stack/' + str(path.relative_to(build))] = path.read_bytes()
    baseline_bytes = args.baseline_package.read_bytes()
    if sha(baseline_bytes) != '5d030225e4f8d3ece36518f6c77d4314720b4af0c5766de1d6282667d9a9b8eb':
        parser.error('Baseline archive differs from the tested package')
    with zipfile.ZipFile(args.baseline_package) as baseline:
        tested = baseline.read('KUI/runtime.kui')
        baseline_metadata = json.loads(baseline.read('build.json'))
    if verify(tested)['build'] != '9020d5101c7e':
        parser.error('Baseline build identity mismatch')
    if baseline_metadata['source_tree'] != 'e9049e95c7ca349741d16acf8703b76a94a56aec':
        parser.error('Tested source tree differs from its published equivalent')
    files['runtimes/00-tested-baseline.kui'] = tested
    runtimes.insert(0, {'profile': 0, 'name': 'tested-baseline',
                       'file': 'runtimes/00-tested-baseline.kui',
                       'build': '9020d5101c7e', 'runtime_sha256': sha(tested),
                       'source_commit': 'ec59022adfab9a12583c58153c139517adabc798',
                       'local_checkpoint': baseline_metadata['source_commit'],
                       'source_tree': baseline_metadata['source_tree'],
                       'hardware_counter_tested': True, 'listening_report_received': False,
                       'completed_stages': 5, 'failures': 0,
                       'worst_refill_us': 79408, 'maximum_service_gap_us': 7445,
                       'minimum_refill_margin_us': 106145,
                       'checked_card_blocks': 20032, 'observed_stack_bytes': 5184})
    fixture = ROOT / 'build/cdda/fixture'
    for name in ('stereo', 'stress'):
        data = (fixture / (name + ('.raw' if name == 'stereo' else '.bin'))).read_bytes()
        meta = json.loads((fixture / (name + '.json')).read_text())
        if len(data) != meta['bytes'] or sha(data) != meta['sha256']:
            parser.error('Fixture mismatch: ' + name)
        files['KUI/tests/cdda/' + meta['file']] = data
        files['fixtures/' + name + '.json'] = (json.dumps(meta, indent=2) + '\n').encode()
    metadata = {'source_commit': commit, 'source_tree': source_tree,
                'local_checkpoint': git('rev-parse', 'HEAD'), 'source_dirty': False,
                'source_url': 'https://github.com/TPMJB/K-UI-NeXT/tree/' + commit,
                'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
                'scope': 'Detached SCI homebrew; no retail game CDDA integration',
                'default_profile': 'controls', 'runtimes': runtimes,
                'sample_included': False, 'sample_path': '/KUI/tests/cdda/track14.raw',
                'expected_sample_sha256': 'ae3d955fc817f433b4c5273581398215e3be5cf2cfd1dee2ec62aaaa677cf338',
                'sound_ring_bytes': 65536, 'private_stack_bytes': 65536}
    files['build.json'] = (json.dumps(metadata, indent=2) + '\n').encode()
    files['README.md'] = (ROOT / 'docs/cdda-next-test.md').read_bytes()
    # Keep the evidence documents' original relative links usable outside the
    # source snapshot as well as in a normal repository checkout.
    files['cdda-next-test.md'] = files['README.md']
    files['cdda-harness-test.md'] = (ROOT / 'docs/cdda-harness-test.md').read_bytes()
    files['source-url.txt'] = (metadata['source_url'] + '\n').encode()
    files['source-snapshot.tar'] = subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT)
    files['source-patch.mbox'] = subprocess.check_output(['git', 'format-patch', BASE + '..HEAD', '--stdout'], cwd=ROOT)
    files['base-source-url.txt'] = b'https://github.com/TPMJB/K-UI-NeXT/tree/4be17b8d5383a6065e922f53362224405ed9d1e8\n'
    for path in (ROOT / 'docs/evidence').glob('cdda-*.md'):
        files['evidence/' + path.name] = path.read_bytes()
    for path in (ROOT / 'LICENSES').iterdir():
        if path.is_file():
            files['LICENSES/' + path.name] = path.read_bytes()
    for name in ('LICENSE', 'THIRD_PARTY.md'):
        files[name] = (ROOT / name).read_bytes()
    # THIRD_PARTY.md describes the wider source snapshot too. Retain the
    # catalogue notice at its normal distribution path even though this
    # standalone executable does not link the known-dump databases.
    files['LICENSES/known-dumps-README.txt'] = (ROOT / 'data/known-dumps/README.txt').read_bytes()
    for name in ('ff.c', 'ff.h', 'ffconf.h', 'ffunicode.c', 'diskio.h'):
        files['source-fatfs/' + name] = (ROOT / 'build/cdda-controls/fatfs' / name).read_bytes()
    files['SHA256SUMS'] = ''.join(sha(data) + '  ' + name + '\n'
                                for name, data in sorted(files.items())).encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() or any(archive.read(name) != data for name, data in files.items()):
            parser.error('ZIP verification failed')
    print(json.dumps({'file': str(args.output.resolve()), 'bytes': args.output.stat().st_size,
                      'build': commit[:12], 'sha256': sha(args.output.read_bytes())}, indent=2))


if __name__ == '__main__':
    main()
