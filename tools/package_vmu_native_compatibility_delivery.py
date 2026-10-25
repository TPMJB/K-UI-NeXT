#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver audited PIO-pair and generic A2 builds with one test worksheet."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from package_vmu_native_a2_delivery import archive_files, checksums, smoke
from vmu_persist_profile import template_profile

ROOT = Path(__file__).resolve().parents[1]
PREREQUISITE = '74958b3878279fc916199a7cd330f7f17b036138'
OWNERS = {
    'ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35',
    '52e00d2b3e9db00016b1f6d782cf8a2141617f3d00bd628200de757fd2e11605',
}
PUBLIC_FOLDER = 'native-reader-compatibility'
PUBLIC_BRANCH = 'checkpoint/native-reader-compatibility-20261025'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def package(args):
    require(not git('status', '--porcelain'), 'Commit source before delivery')
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    revision = commit[:12]
    parts, manifests, reports, installer_checks = {}, {}, {}, {}
    for label, build, installer_label in (
            ('reader-pair', args.pair, 'Background'),
            ('native-a2', args.standard, 'Standard')):
        part = archive_files(build / 'K-UI-Native-Compatibility-Test.zip')
        report = json.loads(part['probe-report.json'])
        manifest = json.loads(part['base/native-persistent-personalization.json'])
        module = part['base/KUI/apps/games/retail-boot.kui']
        require(report['source_commit'] == manifest['source_commit'] == commit and
                report['runtime']['build'] == manifest['build'] == revision and
                not report['source_provenance']['source_dirty'], 'Stale ' + label)
        require(module == (build / 'retail-maple-observe.kui').read_bytes() and
                sha(module) == manifest['module_sha256'] == report['module_sha256'],
                'Packaged module differs from audited ' + label)
        offset = manifest['profile_module_offset']
        require(module[offset:offset+4096] == template_profile(), 'Owner data in template')
        installer_checks[label] = smoke(part, installer_label)
        parts[label], manifests[label], reports[label] = part, manifest, report

    pair = reports['reader-pair']
    route, read_ahead = pair['sdk_sci_route'], pair['read_ahead']
    producer = read_ahead['pio_producer']
    require(route.get('multi_owner') is True and
            set(route.get('original_sdk_dispatch_by_owner', {})) == OWNERS,
            'Both original SDK owner dispatch proofs are required')
    require(read_ahead['pio_pair_candidate'] and
            all(producer.get(name) is True for name in (
                'actual_linked_producer_executed', 'tick_only',
                'no_optional_dma_or_new_commands', 'second_block_timing_admitted',
                'all_admission_guards_rechecked_between_blocks')) and
            producer['maximum_blocks_per_visit'] == 2 and
            producer['soft_window_ticks'] == 2344 and
            producer['reserve_ticks'] == 1200 and
            producer['second_limit_ticks'] == 1144 and
            pair['irq_rearm_reader']['reader_sci_level'] == 1,
            'Successful pair policy or level1 routing changed')
    require(not reports['native-a2'].get('read_ahead', {}).get('enabled') and
            not reports['native-a2'].get('sdk_sci_route', {}).get('enabled'),
            'Generic A2 must retain its separately audited Standard reader')
    lease_wire = args.lease_proof.read_bytes()
    lease = json.loads(lease_wire)
    require(lease['geometry']['readahead_lease_bytes'] == 393216 and
            all(sha((args.pair / name).read_bytes()) == digest
                for name, digest in lease['artifacts'].items()),
            'Original allocator proof is not bound to these final linked artifacts')
    enlarged = [row for row in lease['original_sh'] if row['lease_bytes'] == 393216]
    require({row['slug'] for row in enlarged} == {'evolution1', 'evolution2'} and
            len(enlarged) == 2 and all(
                row['stage_preserved'] and row['installer_stack_preserved'] and
                row['retained_header_and_entire_lease_preserved_during_allocator_operations'] and
                row['production_live_allocator_proof']['lease_self_release_refused']
                for row in enlarged), 'Both enlarged original allocator proofs are required')
    require({(row['lease_bytes'], row['cached_code']) for row in lease['live_worker']} ==
            {(n, cached) for n in (196608, 393216) for cached in (False, True)} and
            all(row['asan_ubsan_passed'] for row in lease['live_worker']),
            'Cached and uncached transfer/lease proofs are required')

    previous_module = (args.previous_pair / 'retail-maple-observe.kui').read_bytes()
    require(sha(previous_module) ==
            'f67bd688d0111733f8213bc3082387c6e4bde1c0ad41043219197df6967ac94c',
            'Previous successful d0aac module identity differs')
    comparison = {}
    for name in ('resident-sci.bin', 'native-worker.bin',
                 'native-ra-aux.bin', 'native-ra-aux.reloc'):
        before = (args.previous_pair / name).read_bytes()
        after = (args.pair / name).read_bytes()
        if name in ('resident-sci.bin', 'native-worker.bin'):
            require(before in previous_module and
                    before.count(b'd0aac421584c') == after.count(revision.encode()) == 1,
                    'Previous runtime body or unique build label differs: ' + name)
            same = after.replace(revision.encode(), b'd0aac421584c') == before
        else:
            same = before == after
        require(same, 'Successful runtime policy bytes changed: ' + name)
        comparison[name] = dict(previous_sha256=sha(before), candidate_sha256=sha(after),
                                bytes=len(after), identical_except_build_label=True,
                                build_label_normalized=name in
                                ('resident-sci.bin', 'native-worker.bin'))

    args.public.mkdir(parents=True, exist_ok=True)
    bundle = args.public / 'native-source-update.bundle'
    subprocess.run(['git', 'bundle', 'create', str(bundle.resolve()), 'HEAD',
                    '^' + PREREQUISITE], cwd=ROOT, check=True)
    subprocess.run(['git', 'bundle', 'verify', str(bundle.resolve())],
                   cwd=ROOT, check=True, capture_output=True)
    patch = subprocess.check_output(['git', 'diff', '--binary', PREREQUISITE, 'HEAD'], cwd=ROOT)
    worksheet = (ROOT / 'docs/native-reader-compatibility-test-sheet.md').read_text()
    worksheet = worksheet.replace('@BUILD@', revision).encode()
    results = (ROOT / 'docs/native-reader-compatibility-results.csv').read_bytes()
    candidate = dict(schema='kui.native.reader-compatibility.candidate.v1',
                     source_commit=commit, source_tree=tree, build=revision,
                     source_bundle_prerequisite=PREREQUISITE,
                     variants={label: dict(module_sha256=manifest['module_sha256'],
                         module_bytes=manifest['module_bytes'],
                         console_verified=False) for label, manifest in manifests.items()},
                     reader_pair_admitted_games=['evolution1', 'evolution2'],
                     generic_a2_is_separate_standard_reader=True,
                     previous_successful_build='d0aac421584c',
                     full_cycle_captures_required=True,
                     console_compatibility_verified=False)
    recovery = f'''# Exact source and history

Source `{commit}`, tree `{tree}`. Complete checkpoint history is retained in
`native-source-update.bundle` after prerequisite `{PREREQUISITE}`.

```sh
git clone --single-branch --branch {PUBLIC_BRANCH} https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 {PUBLIC_FOLDER}/checkout-source.py
```
'''.encode()
    checkout = f'''#!/usr/bin/env python3
import json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
item=json.loads((root/'{PUBLIC_FOLDER}/CANDIDATE.json').read_text())
dest=root/'restored-source'
if not dest.exists():subprocess.run([sys.executable,str(root/'restore.py')],cwd=root,check=True)
def git(*a):return subprocess.check_output(['git','-C',str(dest),*a],text=True).strip()
if git('status','--porcelain'):raise SystemExit('Refusing a dirty source checkout')
subprocess.run(['git','-C',str(dest),'fetch',str(root/'{PUBLIC_FOLDER}/native-source-update.bundle'),'HEAD'],check=True)
if git('rev-parse',item['source_commit']+'^{{tree}}')!=item['source_tree']:raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
print('Verified compatibility candidate source',item['source_commit'],dest)
'''.encode()
    delivery = dict(schema='kui.native.reader-compatibility.delivery.v1',
                    source_commit=commit, build=revision, installers=installer_checks,
                    linked_artifacts_audited=True, canonical_profile_templates=True,
                    prepared_pairs_or_saves_included=False, retail_bytes_included=False,
                    user_photos_or_video_included=False, console_verified=False,
                    successful_runtime_comparison=comparison)
    selected = {'README-FIRST.md': worksheet, 'TESTS.md': worksheet, 'RESULTS.csv': results,
                'CANDIDATE.json': wire(candidate), 'delivery-checks.json': wire(delivery),
                'source/native-source-update.bundle': bundle.read_bytes(),
                'source/source-change.patch': patch, 'source/SOURCE-RECOVERY.md': recovery,
                'evidence/host-tests.log': args.host_log.read_bytes(),
                'evidence/package-audits.log': args.package_log.read_bytes(),
                'evidence/final-original-allocator-lease-proof.json': lease_wire}
    for label, part in parts.items():
        inner = {name: data for name, data in part.items()
                 if name.startswith(('base/', 'tools/', 'native-cache/')) or name in
                 ('personalize.py', 'collect-native-startup.py', 'README-FIRST.md',
                  'LICENSE', 'THIRD_PARTY.md', 'probe-report.json')}
        if label == 'reader-pair':
            title = 'Evolution 1/2 SCI Background PIO-pair candidate'
            selection = ' --game evolution1'
            scope = ('Use only the admitted Evolution 1 and Evolution 2 images. '
                     'For Evolution 2 use --game evolution2. Set SCI, Background On, '
                     'Normal, Auto encoding, CD music Off and RAM patches Off.')
        else:
            title = 'Generic native SCI Standard / virtual-A2 candidate'
            selection = ''
            scope = ('Omitting --game requests fresh SDK-family discovery. '
                     'Set SCI, Background Off, Auto encoding, CD music Off and RAM '
                     'patches Off. This module does not test the PIO-pair reader.')
        inner['README-FIRST.md'] = f'''# {title}

Build **{revision}**. Follow the numbered tests in [../TESTS.md](../TESTS.md)
and record results in [../RESULTS.csv](../RESULTS.csv). Keep this directory intact.

From this directory:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin{selection} --profile-runtime
```

{scope}

Leave physical A2 empty. Keep --profile-runtime installed for all runs. Long
play uses no capture gesture; make the separately required diagnostic run
afterward. See the top-level worksheet for the correct report-cycle heading.
'''.encode()
        inner['SHA256SUMS.txt'] = checksums(inner)
        selected.update({label + '/' + name: data for name, data in inner.items()})
    for path in sorted((ROOT / 'docs/evidence').glob('native-read-ahead-pio*2026-10-25.json')):
        selected['evidence/' + path.name] = path.read_bytes()
    selected['SHA256SUMS.txt'] = checksums(selected)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(selected.items()):
            archive.writestr(name, data)
    require(archive_files(args.output) == selected, 'Final ZIP comparison differs')
    public = {'README.md': worksheet, 'RESULTS.csv': results,
              'CANDIDATE.json': wire(candidate), 'delivery-checks.json': wire(delivery),
              'SOURCE-RECOVERY.md': recovery, 'checkout-source.py': checkout,
              'source-change.patch': patch, args.output.name: args.output.read_bytes(),
              'host-tests.log': args.host_log.read_bytes(),
              'package-audits.log': args.package_log.read_bytes(),
              'final-original-allocator-lease-proof.json': lease_wire}
    for label, manifest in manifests.items():
        public[label + '-manifest.json'] = wire(manifest)
        public[label + '-probe-report.json'] = parts[label]['probe-report.json']
        public[label + '-retail-boot.kui'] = parts[label]['base/KUI/apps/games/retail-boot.kui']
    for name, data in public.items():
        (args.public / name).write_bytes(data)
    current = {**public, 'native-source-update.bundle': bundle.read_bytes()}
    actual = {p.name for p in args.public.iterdir() if p.is_file()}
    require(actual <= set(current) | {'SHA256SUMS.txt'},
            'Publication directory contains files outside this candidate inventory')
    (args.public / 'SHA256SUMS.txt').write_bytes(checksums(current))
    require(git('rev-parse', 'HEAD') == commit and not git('status', '--porcelain'),
            'Source changed during delivery')
    return dict(zip=str(args.output.resolve()), bytes=args.output.stat().st_size,
                sha256=sha(args.output.read_bytes()), build=revision,
                source_commit=commit, source_tree=tree,
                modules={label: manifest['module_sha256'] for label, manifest in manifests.items()},
                installer_cases={label: check['extracted_cli_cases']
                                 for label, check in installer_checks.items()})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pair', 'standard', 'previous-pair', 'lease-proof', 'output', 'public', 'host-log', 'package-log'):
        parser.add_argument('--' + name, type=Path, required=True)
    try:
        print(json.dumps(package(parser.parse_args()), indent=2))
    except (OSError, ValueError, KeyError, AssertionError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'Compatibility delivery refused: {error}\n')
