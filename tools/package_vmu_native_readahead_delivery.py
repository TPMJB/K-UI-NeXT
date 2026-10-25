#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Trim an audited PIO read-ahead package and preserve its exact source history."""
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
PUBLIC_FOLDER = 'read-ahead-pio'
PUBLIC_BRANCH = 'checkpoint/native-readahead-pio-20261025'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def package(args):
    require(not git('status', '--porcelain'), 'Commit source before delivery')
    commit = git('rev-parse', 'HEAD')
    tree = git('rev-parse', 'HEAD^{tree}')
    build_id = commit[:12]
    build = args.build.resolve()
    files = archive_files(build / 'K-UI-Native-Compatibility-Test.zip')
    report = json.loads(files['probe-report.json'])
    manifest = json.loads(files['base/native-persistent-personalization.json'])
    module = files['base/KUI/apps/games/retail-boot.kui']
    require(report['source_commit'] == manifest['source_commit'] == commit and
            report['runtime']['build'] == manifest['build'] == build_id and
            not report['source_provenance']['source_dirty'], 'Stale source or module')
    require(module == (build / 'retail-maple-observe.kui').read_bytes() and
            sha(module) == manifest['module_sha256'] == report['module_sha256'],
            'Packaged module differs from audited build')
    require(report['read_ahead']['pio_candidate'] and
            all(report['read_ahead']['pio_producer'][name] for name in
                ('enabled', 'actual_linked_producer_executed', 'tick_only',
                 'one_fresh_block_per_tick', 'no_optional_dma_or_new_commands',
                 'producer_only_timing')), 'Missing complete linked PIO proof')
    require(report['read_ahead']['geometry']['public_bytes'] == 656 and
            report['read_ahead']['diagnostics']['actual_diagnostic_c_executed'] and
            report['read_ahead']['cold_binder']['actual_auxiliary_c_initializer_executed'] and
            report['sdk_sci_route']['paired_sdk_sci_markers'] and
            report['sdk_sci_route']['cold_binder_instruction_modelled'] and
            report['loader_timeline']['enabled'] and
            report['irq_rearm_reader']['reader_sci_level'] == 1,
            'Missing paired diagnostic, cold, timeline or SDK proof')
    offset = manifest['profile_module_offset']
    require(module[offset:offset+4096] == template_profile(), 'Noncanonical owner profile')
    installer = smoke(files, 'Background')
    args.public.mkdir(parents=True, exist_ok=True)
    bundle = args.public / 'native-source-update.bundle'
    subprocess.run(['git', 'bundle', 'create', str(bundle.resolve()), 'HEAD',
                    '^' + PREREQUISITE], cwd=ROOT, check=True)
    subprocess.run(['git', 'bundle', 'verify', str(bundle.resolve())],
                   cwd=ROOT, check=True, capture_output=True)
    patch = subprocess.check_output(['git', 'diff', '--binary', PREREQUISITE, 'HEAD'], cwd=ROOT)
    recovery = f'''# Exact source and history

Source `{commit}`, tree `{tree}`. The bundle preserves original commit identities
since prerequisite `{PREREQUISITE}`. Complete prerequisite history is retained
in the public checkpoint.

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
print('Verified PIO read-ahead source',item['source_commit'],dest)
'''.encode()
    candidate = dict(schema='kui.native.read-ahead.pio-candidate.v1', source_commit=commit,
                     source_tree=tree, build=build_id, module_sha256=sha(module),
                     module_bytes=len(module), source_bundle_prerequisite=PREREQUISITE,
                     maximum_optional_blocks_per_heartbeat=1, demand_dma_unchanged=True,
                     console_performance_verified=False, targeted_capture_seconds=25)
    delivery = dict(schema='kui.native.read-ahead.pio-delivery.v1', source_commit=commit,
                    source_tree=tree, extracted_installer=installer,
                    generic_profile_matches_canonical_template=True,
                    prepared_owner_card_files_included=False, save_payload_included=False,
                    retail_executable_or_disc_tracks_included=False,
                    user_photos_or_video_included=False, linked_artifact_audit_passed=True,
                    console_performance_verified=False)
    evidence = (ROOT / 'docs/evidence/native-read-ahead-pio-2026-10-25.json').read_bytes()
    observed = (ROOT / 'docs/evidence/native-read-ahead-second-overrun-2026-10-25.json').read_bytes()
    selected = {name: data for name, data in files.items()
                if name.startswith(('base/', 'tools/', 'native-cache/')) or name in
                ('personalize.py', 'README-FIRST.md', 'LICENSE', 'THIRD_PARTY.md', 'probe-report.json')}
    extras = {'CANDIDATE.json': wire(candidate), 'delivery-checks.json': wire(delivery),
              'source/native-source-update.bundle': bundle.read_bytes(),
              'source/source-change.patch': patch, 'source/SOURCE-RECOVERY.md': recovery,
              'evidence/TEST-EVIDENCE.json': evidence, 'evidence/63369-observed.json': observed,
              'evidence/linked-read-ahead.json': wire(report['read_ahead']),
              'evidence/host-tests.log': args.host_log.read_bytes(),
              'evidence/package-audit.log': args.package_log.read_bytes(),
              'evidence/display-host-test.c': args.display_test.read_bytes(),
              'evidence/low-resident-comparison.json': args.resident_comparison.read_bytes(),
              'build/native-ra-aux.bin': (build / 'native-ra-aux.bin').read_bytes(),
              'build/native-ra-aux.reloc': (build / 'native-ra-aux.reloc').read_bytes()}
    selected.update(extras)
    selected['SHA256SUMS.txt'] = checksums(selected)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(selected.items()):
            archive.writestr(name, data)
    require(archive_files(args.output) == selected, 'Final ZIP differs')
    public = {name: data for name, data in extras.items() if '/' not in name}
    public.update({'README.md': files['README-FIRST.md'], 'SOURCE-RECOVERY.md': recovery,
                   'checkout-source.py': checkout, 'source-change.patch': patch,
                   'TEST-EVIDENCE.json': evidence, 'CONSOLE-RESULTS.json': observed,
                   'probe-report.json': files['probe-report.json'],
                   'linked-read-ahead.json': wire(report['read_ahead']),
                   'manifest.json': files['base/native-persistent-personalization.json'],
                   'retail-boot.kui': module, 'host-tests.log': args.host_log.read_bytes(),
                   'package-audit.log': args.package_log.read_bytes(),
                   'display-host-test.c': args.display_test.read_bytes(),
                   'low-resident-comparison.json': args.resident_comparison.read_bytes(),
                   'native-ra-aux.bin': (build / 'native-ra-aux.bin').read_bytes(),
                   'native-ra-aux.reloc': (build / 'native-ra-aux.reloc').read_bytes(),
                   args.output.name: args.output.read_bytes()})
    for name, data in public.items():
        (args.public / name).write_bytes(data)
    current = {p.name: p.read_bytes() for p in args.public.iterdir()
               if p.is_file() and p.name != 'SHA256SUMS.txt'}
    (args.public / 'SHA256SUMS.txt').write_bytes(checksums(current))
    require(git('rev-parse', 'HEAD') == commit and not git('status', '--porcelain'),
            'Source changed during delivery')
    return dict(zip=str(args.output.resolve()), bytes=args.output.stat().st_size,
                sha256=sha(args.output.read_bytes()), build=build_id,
                source_commit=commit, source_tree=tree, module_sha256=sha(module),
                installer_cases=installer['extracted_cli_cases'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'output', 'public', 'host-log', 'package-log',
                 'display-test', 'resident-comparison'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(package(args), indent=2))
    except (OSError, ValueError, KeyError, AssertionError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'PIO delivery refused: {error}\n')
