#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver one matching Games UI and generalized native X/Y virtual-A2 module."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

from package_vmu_native_a2_delivery import archive_files, checksums, smoke
from runtime_package import envelope, flatten_elf, verify
from vmu_native_mode import decode_mode
from check_vmu_native_worker_gateway import read_elf
from check_native_controller_failure_witness import witness_companion

ROOT = Path(__file__).resolve().parents[1]
PREREQUISITE = '74958b3878279fc916199a7cd330f7f17b036138'
PUBLIC_FOLDER = 'native-startup-followup'
PUBLIC_BRANCH = 'checkpoint/native-startup-followup-20261025'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def extracted_install_check(files):
    sys.path.insert(0, str(ROOT/'tests'))
    from test_personalize_vmu_native_persistent import pair
    with tempfile.TemporaryDirectory(prefix='kui-native-dual-delivery-') as directory:
        root = Path(directory)
        package, card = root/'package', root/'card'
        card.mkdir()
        for name, data in files.items():
            if name.startswith(('source/', 'evidence/')):
                continue
            target = package/name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        profile = pair(card, 'p0005', number='0005')
        ordinary = card/'KUI/apps/games/retail-boot.kui'
        ordinary.parent.mkdir(parents=True, exist_ok=True)
        ordinary.write_bytes(b'previous ordinary loader')
        (card/'KUI/runtime.kui').write_bytes(b'previous UI')
        settings = ordinary.parent/'launch-settings-a.bin'
        settings.write_bytes(b'preserved image launch choices')
        before = {p.relative_to(card).as_posix(): p.read_bytes()
                  for p in card.rglob('*') if p.is_file()}
        argv = [sys.executable, str(package/'install.py'), str(card), '--profile',
                str(profile.relative_to(card)), '--profile-runtime']
        for index in range(2):
            result = subprocess.run(argv, cwd=root, text=True, capture_output=True)
            require(result.returncode == 0, result.stderr)
            after = {p.relative_to(card).as_posix(): p.read_bytes()
                     for p in card.rglob('*') if p.is_file()}
            changed = {'KUI/runtime.kui', 'KUI/apps/games/retail-native-dual.kui'}
            require(all(after[name] == data for name, data in before.items() if name not in changed),
                    'Candidate installer changed ordinary loader, settings or prepared card')
            require(set(after) - set(before) <= changed, 'Unexpected installer output')
            if index:
                require(after == first, 'Repeated extracted installation differs')
            first = after
        require(verify(after['KUI/runtime.kui'])['build'] ==
                verify(after['KUI/apps/games/retail-native-dual.kui'])['build'],
                'Installed UI and reader builds differ')
        # Exercise the real diagnostic mode at the separate dual destination,
        # then return to the standard capture setup without changing saves.
        result = subprocess.run(argv[:-1]+['--evolution-trace'], cwd=root, text=True, capture_output=True)
        require(result.returncode == 0, result.stderr)
        traced = {p.relative_to(card).as_posix(): p.read_bytes()
                  for p in card.rglob('*') if p.is_file()}
        mode_offset = json.loads(files['base/native-persistent-personalization.json'])['runtime_mode_module_offset']
        require(decode_mode(traced['KUI/apps/games/retail-native-dual.kui'][mode_offset:mode_offset+32]) == 6,
                'Evolution trace mode did not reach the dual module')
        require(set(traced) == set(first) and all(traced[name] == data for name, data in first.items()
                    if name != 'KUI/apps/games/retail-native-dual.kui'),
                'Trace installation changed files outside its module')
        result = subprocess.run(argv, cwd=root, text=True, capture_output=True)
        require(result.returncode == 0 and
                {p.relative_to(card).as_posix(): p.read_bytes()
                 for p in card.rglob('*') if p.is_file()} == first,
                'Returning from Evolution trace to profile changed installed/prepared bytes')
        return dict(extracted_cli_runs=4, evolution_trace_and_profile_roundtrip=True,
                    prepared_card_and_settings_preserved=True,
                    ordinary_loader_preserved=True, idempotent_bytes=True)



def package(args):
    require(not git('status', '--porcelain'), 'Commit source before delivery')
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    revision = commit[:12]
    original = archive_files(args.build/'K-UI-Native-Compatibility-Test.zip')
    report = json.loads(original['probe-report.json'])
    manifest = json.loads(original['base/native-persistent-personalization.json'])
    module = original['base/KUI/apps/games/retail-boot.kui']
    require(report['source_commit'] == manifest['source_commit'] == commit and
            manifest['build'] == report['runtime']['build'] == revision and
            not report['source_provenance']['source_dirty'] and
            sha(module) == manifest['module_sha256'] == report['module_sha256'] and
            module == (args.build/'retail-native-dual.kui').read_bytes(),
            'Stale native module or proof')
    baseline = args.baseline.read_bytes()
    baseline_info = verify(baseline)
    require(sha(baseline) == '7be0a290ec675438a565f1c3d1aac6fde18520b88056c0513961d96f9bc24f48' and
            baseline_info['build'] == 'af9e01c8a271', 'Wrong approved comparison module')
    unchanged_components = {}
    for name in ('resident-sci.bin', 'resident-legacy.bin', 'native-ra-aux.bin', 'native-ra-aux.reloc'):
        old = (args.baseline.parent/name).read_bytes()
        new = (args.build/name).read_bytes()
        copies = baseline[64:].count(old)
        require(old and copies > 0 and module[64:].count(new) == copies,
                'Reader component copies differ from their verified baseline module: '+name)
        require(new.replace(revision.encode(), baseline_info['build'].encode()) == old,
                'Startup follow-up changed reader component beyond its visible build label: '+name)
        unchanged_components[name] = dict(baseline_sha256=sha(old), current_sha256=sha(new), embedded_copies=copies)
    runtime_comparison = dict(baseline_source_commit='af9e01c8a2710eb44e24867bbe2f593727fbb844',
                              baseline_module_sha256=sha(baseline), unchanged_reader_components=unchanged_components,
                              low_readers_and_auxiliary_identical_except_visible_build_label=True,
                              worker_change='Typed Lodoss terminal-dispatch capability, precise VMU guard witnesses and compact diagnostic captions')
    ra, sdk = report['read_ahead'], report['sdk_sci_route']
    require(ra.get('dual_reader_candidate') is True and
            ra.get('dual_reader', {}).get('enabled') is True and
            sdk.get('structural_profile') is True and sdk.get('generic_family_table', {}).get('enabled'),
            'Both structural SDK and actual dual-engine proofs are required')
    witness = ra.get('controller_failure_witness')
    require(report.get('prepared_lifetime_admission', {}).get('controller_failure_witness') == witness and
            witness_companion(read_elf(args.build/'native-worker.elf'), ra),
            'Matching actual linked controller/identity/terminal capability witness proof is required')
    require(sha((args.build/'native-ra-aux.bin').read_bytes()) ==
            '61ab1f513910fce19665d742ad872bea92ee69afe1af6030d09ecb04f96d0974' and
            (ROOT/'src/loader/native_readahead_aux.c').read_bytes() ==
            subprocess.check_output(['git', 'show', 'af401496961afdcc27f57cb0ba63bd1a8414ce67:src/loader/native_readahead_aux.c'], cwd=ROOT),
            'Approved PIO-pair auxiliary policy changed')
    joint_wire, heap_wire = args.joint.read_bytes(), args.heap.read_bytes()
    joint, heap = json.loads(joint_wire), json.loads(heap_wire)
    require(joint['joint_host_candidates'] == 8 and len(joint['joint_candidate_slugs']) == 8 and
            len(heap['results']) == 8 and heap['lease_bytes'] == 393216,
            'Complete joint original-code and enlarged-lease proof is required')
    artifact_names = {'stage.elf', 'native-worker.elf', 'native-worker.bin', 'native-ra-aux.bin'}
    for proof in (joint, heap):
        require(set(proof['artifacts']) == artifact_names, 'Missing final linked artifact bindings')
        require(all(sha((args.build/name).read_bytes()) == digest
                    for name, digest in proof['artifacts'].items()),
                'Corpus proof is stale for the final linked artifacts')
    require(joint['proof_sha256']['original_heap'] == sha(heap_wire), 'Joint heap proof hash differs')
    admitted = [row for row in joint['results'] if row['joint_host_candidate']]
    admitted_slugs = {row['slug'] for row in admitted}
    admitted_hashes = {row['effective_image_sha256'] for row in admitted}
    require(len(admitted) == len(admitted_slugs) == len(admitted_hashes) == 8 and
            admitted_slugs == set(joint['joint_candidate_slugs']) and
            {(row['slug'], row['image_sha256']) for row in heap['results']} ==
            {(row['slug'], row['effective_image_sha256']) for row in admitted} and
            all(row['retained_header_and_entire_lease_preserved_during_allocator_operations'] is True
                and row['lease_bytes'] == 393216 for row in heap['results']),
            'Joint candidate identities and original allocator lifetime proofs differ')
    owners = sdk.get('original_sdk_dispatch_by_owner', {})
    require(admitted_hashes == set(owners) and
            all(proof.get('owner_digest') == digest and
                proof.get('production_extent_and_structural_source_live_models_checked') is True
                for digest, proof in owners.items()),
            'Missing actual original SDK callback proof for a joint candidate')
    provenance = json.loads(args.runtime.with_suffix('.compile.json').read_bytes())
    elf = args.runtime.read_bytes()
    require(provenance == dict(commit=commit, source_dirty=False, elf_sha256=sha(elf)),
            'UI ELF does not have the same clean source provenance')
    payload, memory = flatten_elf(elf)
    gui = envelope(payload, memory, revision)
    runtime_info = dict(schema='kui.native.dual-runtime.v1', build=revision,
                        runtime_sha256=sha(gui), runtime_bytes=len(gui), module_sha256=sha(module))
    host_installer = smoke(original, 'Background')
    files = {name: data for name, data in original.items()
             if name.startswith(('base/', 'tools/', 'native-cache/')) or name in
             ('personalize.py', 'probe-report.json', 'LICENSE', 'THIRD_PARTY.md')}
    guide = (ROOT/'docs/native-dual-reader-test.md').read_text().replace('@BUILD@', revision).encode()
    files.update({'install.py': (ROOT/'tools/install_vmu_native_dual.py').read_bytes(),
                  'base/KUI/runtime.kui': gui, 'base/native-dual-runtime.json': wire(runtime_info),
                  'README-FIRST.md': guide, 'TESTS.md': guide,
                  'RESULTS.csv': (ROOT/'docs/native-dual-reader-results.csv').read_bytes(),
                  'kui-doa2-filesystem-probe.py': (ROOT/'tools/kui-doa2-filesystem-probe.py').read_bytes(),
                  'kui-native-startup-bundle.py': (ROOT/'tools/kui-native-startup-bundle.py').read_bytes(),
                  'DOA2-PROBE.md': (ROOT/'tools/kui-doa2-filesystem-probe-README.md').read_bytes(),
                  'evidence/final-joint-corpus.json': joint_wire,
                  'evidence/final-original-heap.json': heap_wire,
                  'evidence/host-tests.log': args.host_log.read_bytes(),
                  'evidence/package-audit.log': args.package_log.read_bytes()})
    for path in sorted((ROOT/'docs/evidence').glob('native-*2026-10-25.json')):
        if any(word in path.name for word in ('structural', 'joint', 'pio-pair-observed', 'new-structural', 'console-followup', 'followup-review', 'terminal-dispatch', 'startup-source-pass')):
            files['evidence/'+path.name] = path.read_bytes()
    candidate = dict(schema='kui.native.universal-readers.candidate.v1', source_commit=commit,
                     source_tree=tree, build=revision, module_sha256=sha(module),
                     runtime_sha256=sha(gui), new_reader='X', legacy_reader='Y',
                     virtual_slot='A2', source_bundle_prerequisite=PREREQUISITE,
                     source_images_analyzed=26, joint_software_candidates=joint['joint_candidate_slugs'],
                     title_allowlist=False, approved_reader_policy_preserved=True,
                     console_compatibility_verified=False, reader_runtime_comparison=runtime_comparison,
                     console_observations=json.loads((ROOT/'docs/evidence/native-universal-console-followup-2026-10-25.json').read_text())['console_observations'])
    delivery = dict(source_commit=commit, generic_installer=host_installer,
                    combined_installer=extracted_install_check(files),
                    same_clean_UI_and_module_source=True, no_retail_or_save_payloads=True,
                    final_corpus_artifacts_bound=True, reader_runtime_comparison=runtime_comparison,
                    console_execution_verified=False)
    args.public.mkdir(parents=True, exist_ok=True)
    bundle = args.public/'native-source-update.bundle'
    subprocess.run(['git', 'bundle', 'create', str(bundle.resolve()), 'HEAD', '^'+PREREQUISITE], cwd=ROOT, check=True)
    subprocess.run(['git', 'bundle', 'verify', str(bundle.resolve())], cwd=ROOT, check=True, capture_output=True)
    patch = subprocess.check_output(['git', 'diff', '--binary', PREREQUISITE, 'HEAD'], cwd=ROOT)
    recovery = f'''# Source recovery

Source {commit}, tree {tree}. Clone checkpoint branch {PUBLIC_BRANCH} from
https://github.com/TPMJB/K-UI-NeXT and run python3 {PUBLIC_FOLDER}/checkout-source.py.
The bundle retains original history after prerequisite {PREREQUISITE}.
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
print('Verified native X/Y source',item['source_commit'],dest)
'''.encode()
    files.update({'CANDIDATE.json': wire(candidate), 'delivery-checks.json': wire(delivery),
                  'source/native-source-update.bundle': bundle.read_bytes(),
                  'source/source-change.patch': patch, 'source/SOURCE-RECOVERY.md': recovery})
    files['SHA256SUMS.txt'] = checksums(files)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    require(archive_files(args.output) == files, 'Final archive bytes differ')
    public = {'README.md': guide, 'RESULTS.csv': files['RESULTS.csv'],
              'CANDIDATE.json': wire(candidate), 'delivery-checks.json': wire(delivery),
              'SOURCE-RECOVERY.md': recovery, 'checkout-source.py': checkout,
              'source-change.patch': patch, 'runtime.kui': gui, 'retail-native-dual.kui': module,
              'manifest.json': original['base/native-persistent-personalization.json'],
              'probe-report.json': original['probe-report.json'],
              'final-joint-corpus.json': joint_wire, 'final-original-heap.json': heap_wire,
              'host-tests.log': args.host_log.read_bytes(), 'package-audit.log': args.package_log.read_bytes(),
              args.output.name: args.output.read_bytes()}
    for name, data in public.items():
        (args.public/name).write_bytes(data)
    public['native-source-update.bundle'] = bundle.read_bytes()
    require({p.name for p in args.public.iterdir() if p.is_file()} <= set(public)|{'SHA256SUMS.txt'},
            'Unexpected publication-directory file')
    (args.public/'SHA256SUMS.txt').write_bytes(checksums(public))
    require(git('rev-parse', 'HEAD') == commit and not git('status', '--porcelain'), 'Source changed during delivery')
    return dict(zip=str(args.output.resolve()), bytes=args.output.stat().st_size,
                sha256=sha(args.output.read_bytes()), build=revision, source_commit=commit,
                source_tree=tree, module_sha256=sha(module), runtime_sha256=sha(gui),
                generic_installer_cases=host_installer['extracted_cli_cases'],
                combined_installer_runs=delivery['combined_installer']['extracted_cli_runs'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'runtime', 'joint', 'heap', 'output', 'public', 'host-log', 'package-log', 'baseline'):
        parser.add_argument('--'+name, type=Path, required=True)
    try:
        print(json.dumps(package(parser.parse_args()), indent=2))
    except (OSError, ValueError, KeyError, AssertionError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'Native X/Y delivery refused: {error}\n')
