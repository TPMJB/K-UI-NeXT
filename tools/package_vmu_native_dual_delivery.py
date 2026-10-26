#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver one matching Games UI and generalized native X/Y virtual-A2 module."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

from package_vmu_native_a2_delivery import archive_files, checksums, smoke
from runtime_package import envelope, flatten_elf, verify
from vmu_native_mode import decode_mode
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from check_native_controller_failure_witness import witness_companion

ROOT = Path(__file__).resolve().parents[1]
PREREQUISITE = '74958b3878279fc916199a7cd330f7f17b036138'
PUBLIC_FOLDER = 'native-game-startup'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def offline_source_recovery_check(candidate, script, bundle):
    """Run the shipped offline recovery, including refusal before mutation."""
    with tempfile.TemporaryDirectory(prefix='kui-native-recovery-cli-') as temporary:
        root=Path(temporary);source=root/'source';source.mkdir()
        (root/'CANDIDATE.json').write_bytes(wire(candidate))
        path=source/'native-source-full.bundle';path.write_bytes(bundle)
        entry=source/'checkout-source.py';entry.write_bytes(script)
        argv=[sys.executable,str(entry)]
        for _ in range(2):
            run=subprocess.run(argv,cwd=root,text=True,capture_output=True)
            require(run.returncode==0,'Shipped offline source recovery failed: '+run.stderr)
            restored=root/'restored-source'
            actual=subprocess.check_output(['git','-C',str(restored),'rev-parse','HEAD^{tree}'],text=True).strip()
            require(actual==candidate['source_tree'] and not subprocess.check_output(
                ['git','-C',str(restored),'status','--porcelain'],text=True),
                'Shipped offline recovery did not produce the exact clean tree')
        sentinel=restored/'README.md';before=sentinel.read_bytes();sentinel.write_bytes(before+b'\nlocal edit\n')
        refused=subprocess.run(argv,cwd=root,text=True,capture_output=True)
        require(refused.returncode!=0 and 'dirty' in refused.stderr.lower() and
                sentinel.read_bytes()==before+b'\nlocal edit\n',
                'Offline source recovery did not preserve a dirty checkout')
        sentinel.write_bytes(before)
        with path.open('r+b') as damaged:
            damaged.seek(-1,2);last=damaged.read(1);damaged.seek(-1,2);damaged.write(bytes([last[0]^1]))
        refused=subprocess.run(argv,cwd=root,text=True,capture_output=True)
        require(refused.returncode!=0 and 'checksum mismatch' in refused.stderr.lower(),
                'Offline source recovery accepted a changed full bundle')
        path.write_bytes(bundle)
        saved=root/'saved-source';restored.rename(saved);restored.mkdir()
        subprocess.run(['git','init',str(root)],check=True,capture_output=True)
        (root/'.git/info/exclude').write_text('*\n')
        subprocess.run(['git','-C',str(root),'-c','user.name=Recovery check',
                        '-c','user.email=recovery@example.invalid','commit','--allow-empty',
                        '-m','Parent repository sentinel'],check=True,capture_output=True)
        parent_head=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
        for symlink in (False,True):
            if symlink:
                restored.rmdir();restored.symlink_to(saved,target_is_directory=True)
            refused=subprocess.run(argv,cwd=root,text=True,capture_output=True)
            require(refused.returncode!=0 and
                    ('repository root' in refused.stderr.lower() if not symlink else
                     'symlink' in refused.stderr.lower()) and
                    subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()==parent_head and
                    not (root/'.git/FETCH_HEAD').exists() and
                    not subprocess.check_output(['git','-C',str(root),'status','--porcelain'],text=True),
                    'Offline recovery did not refuse an unrelated destination before parent repository mutation')
    return dict(extracted_cli_runs=6,fresh_offline_clone=True,existing_clean_checkout=True,
                dirty_checkout_preserved=True,changed_bundle_refused=True,exact_source_tree_verified=True,
                non_repository_destination_refused=True,symlink_destination_refused=True,
                clean_parent_repository_preserved=True)


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def extracted_resource_probe_check(files):
    names=('kui-rayman-resource-probe.py','kui-doa2-filesystem-probe.py',
           'kui-native-startup-bundle.py')
    with tempfile.TemporaryDirectory(prefix='kui-rayman-probe-cli-') as temporary:
        root=Path(temporary)
        for name in names:(root/name).write_bytes(files[name])
        argv=[sys.executable,str(root/names[0])]
        help_run=subprocess.run([*argv,'--help'],cwd=root,text=True,capture_output=True)
        require(help_run.returncode==0 and '--gdi' in help_run.stdout and 'FIX.DAT' in help_run.stdout,
                'Extracted optional Rayman probe help/imports failed')
        refused=subprocess.run([*argv,'--gdi'],cwd=root,text=True,capture_output=True)
        require(refused.returncode==2 and '--gdi needs the full path to a .gdi file' in refused.stderr,
                'Extracted optional Rayman probe accepted a valueless GDI selector')
    return dict(extracted_cli_cases=2,all_three_required_scripts_together=True,
                source_payloads_read=False,script_sha256={name:sha(files[name]) for name in names})


def owner_stack_literal(body, address, expected):
    """Decode the unique physical entry-SP lower/upper bound comparison."""
    matches = []
    for compare_at in range(4, len(body) - 8, 2):
        first, second, lower, fail, upper_load, upper, fail_high = struct.unpack_from('<7H', body, compare_at - 4)
        # SH scheduling can exchange the independent literal load and AND.
        if first == 0x2139 and second & 0xff00 == 0xd000:
            load, at = second, compare_at - 2
        elif second == 0x2139 and first & 0xff00 == 0xd000:
            load, at = first, compare_at - 4
        else:
            continue
        if (lower != 0x3102 or fail & 0xff00 != 0x8b00 or upper_load & 0xff00 != 0xd000 or
                upper != 0x3106 or fail_high & 0xff00 != 0x8900):
            continue
        literal = ((address + at + 4) & ~3) + (load & 255) * 4 - address
        high = ((address + compare_at + 8) & ~3) + (upper_load & 255) * 4 - address
        if not (0 <= literal <= len(body) - 4 and 0 <= high <= len(body) - 4):
            continue
        if struct.unpack_from('<I', body, literal)[0] == expected and struct.unpack_from('<I', body, high)[0] == 0x0c00f400:
            matches.append(literal)
    require(len(matches) == 1 and body.count(struct.pack('<I', expected)) == 1,
            'Owner-stack lower bound is not one uniquely decoded hook literal')
    return matches[0]


def normalize_owner_stack_delta(old, new, hook_offset, hook_address, hook_bytes):
    """Allow one reviewed literal at its decoded hook offset, never a global replacement."""
    require(hook_offset >= 0 and hook_bytes >= 28 and hook_offset <= len(old) - hook_bytes and
            len(old) == len(new), 'Owner-stack hook leaves its fixed reader image')
    old_body, new_body = old[hook_offset:hook_offset + hook_bytes], new[hook_offset:hook_offset + hook_bytes]
    old_at = owner_stack_literal(old_body, hook_address, 0x0c00c100)
    new_at = owner_stack_literal(new_body, hook_address, 0x0c008100)
    require(old_at == new_at and not (hook_address + old_at) & 3,
            'Owner-stack lower-bound literal moved or is unaligned')
    at = hook_offset + old_at
    normalized = new[:at] + struct.pack('<I', 0x0c00c100) + new[at + 4:]
    require(normalized == old, 'Reader changed beyond its single decoded owner-stack lower bound')
    return normalized, dict(binary_offset=at, hook_offset=old_at, old_physical_low='0x0c00c100',
                            new_physical_low='0x0c008100', physical_high='0x0c00f400', changed_literals=1)


def verify_reader_component(name, old, new, baseline_build, revision, baseline_dir, build):
    normalized = new.replace(revision.encode(), baseline_build.encode())
    require(new.count(revision.encode()) == old.count(baseline_build.encode()),
            'Reader build-label count differs: ' + name)
    if name not in ('resident-sci.bin', 'resident-legacy.bin'):
        require(normalized == old, 'Approved auxiliary component changed: ' + name)
        return None
    old_elf, new_elf = read_elf(baseline_dir / name.replace('.bin', '.elf')), read_elf(build / name.replace('.bin', '.elf'))
    base = 0x8c004000
    for image, payload in ((old_elf, old), (new_elf, new)):
        loads = [p for p in image[2] if p[0] == 1]
        require(len(loads) == 1 and loads[0][2] == loads[0][3] == base and
                loads[0][4] == len(payload) and
                image[0][loads[0][1]:loads[0][1] + len(payload)] == payload,
                'Low-reader binary differs from its complete linked ELF load: ' + name)
    require(new_elf[4].get('__kui_native_owner_stack_ip_hook') == (1, 0, 0, 0xfff1),
            'Missing exact reviewed owner-stack hook marker: ' + name)
    old_address, old_body, old_section = symbol_bytes(old_elf, '_kui_vmu_observe_heap_hook', 2)
    new_address, new_body, new_section = symbol_bytes(new_elf, '_kui_vmu_observe_heap_hook', 2)
    require(old_address == new_address and len(old_body) == len(new_body) and
            old_section[2] & 7 == new_section[2] & 7 == 6,
            'Owner-stack hook address, extent or executable ownership changed: ' + name)
    _, delta = normalize_owner_stack_delta(old, normalized, old_address - base, old_address, len(old_body))
    return dict(hook_address=hex(old_address), hook_bytes=len(old_body),
                old_hook_sha256=sha(old_body), current_hook_sha256=sha(new_body), **delta)


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
    stack_guard = report.get('temporary_installer_stack', {})
    require(stack_guard.get('owner_stack_boundary_execution_cases', 0) > 0 and
            stack_guard.get('owner_stack_physical_bounds') == dict(low='0x0c008100', high='0x0c00f400') and
            stack_guard.get('inherited_52byte_frame_above_retained_low_stack') is True and
            stack_guard.get('rayman_original_startup_sp_rejected_by_old_bound_cases', 0) > 0,
            'Actual SH owner-stack boundary and inherited-frame execution proof is required')
    for name in ('resident-sci.bin', 'resident-legacy.bin', 'native-ra-aux.bin', 'native-ra-aux.reloc'):
        old = (args.baseline.parent/name).read_bytes()
        new = (args.build/name).read_bytes()
        copies = baseline[64:].count(old)
        require(old and copies > 0 and module[64:].count(new) == copies,
                'Reader component copies differ from their verified baseline module: '+name)
        delta = verify_reader_component(name, old, new, baseline_info['build'], revision,
                                        args.baseline.parent, args.build)
        unchanged_components[name] = dict(baseline_sha256=sha(old), current_sha256=sha(new), embedded_copies=copies)
        if delta is not None:
            unchanged_components[name]['reviewed_owner_stack_hook_delta'] = delta
    runtime_comparison = dict(baseline_source_commit='af9e01c8a2710eb44e24867bbe2f593727fbb844',
                              baseline_module_sha256=sha(baseline), unchanged_reader_components=unchanged_components,
                              low_readers_and_auxiliary_identical_except_reviewed_owner_stack_bound_and_build_label=True,
                              low_reader_delta='One decoded owner-stack lower-bound literal in each low hook; scheduling and producer policy unchanged',
                              worker_change='Exact original-tail or arena-bottom lease identity with existing lifetime/free-overlap checks; guarded timer callbacks and terminal-only PROFILE stack/video observer; standard Visual Memory product-class reply retains storage-only capabilities',
                              cold_stage_change='Six typed callbacks selected from the actual reader; original allocator reserves the bottom chunk and restores the original upper allocation boundary; verified private-pool split budgets the exact physical lease cost',
                              games_ui_change='New reader selected by default; explicit reader choices retained')
    ra, sdk = report['read_ahead'], report['sdk_sci_route']
    require(ra.get('dual_reader_candidate') is True and
            ra.get('dual_reader', {}).get('enabled') is True and
            sdk.get('structural_profile') is True and sdk.get('generic_family_table', {}).get('enabled'),
            'Both structural SDK and actual dual-engine proofs are required')
    from check_native_dual_readers import low_callback_companion
    dual=ra['dual_reader']
    require(low_callback_companion(dual.get('artifact_sha256',{}),dual.get('selected_low_callbacks')),
            'Actual selected-reader cold callback execution is required')
    witness = ra.get('controller_failure_witness')
    require(report.get('prepared_lifetime_admission', {}).get('controller_failure_witness') == witness and
            witness_companion(read_elf(args.build/'native-worker.elf'), ra),
            'Matching actual linked controller/identity/terminal capability witness proof is required')
    require(witness.get('owned_timer_callbacks', {}).get('enabled') is True and
            report.get('prepared_lifetime_admission', {}).get('live_boundary_call_abi', {}).get('enabled') is True,
            'Actual owned-timer callback and live-boundary driver ABI proofs are required')
    from check_native_raw_owner_capture import require_companion as raw_owner_companion
    require(raw_owner_companion(read_elf(args.build/'native-worker.elf'),
                report.get('prepared_lifetime_admission',{}).get('raw_owner_terminal')),
            'Matching actual terminal-only owner observer proof is required')
    require(sha((args.build/'native-ra-aux.bin').read_bytes()) ==
            '61ab1f513910fce19665d742ad872bea92ee69afe1af6030d09ecb04f96d0974' and
            (ROOT/'src/loader/native_readahead_aux.c').read_bytes() ==
            subprocess.check_output(['git', 'show', 'af401496961afdcc27f57cb0ba63bd1a8414ce67:src/loader/native_readahead_aux.c'], cwd=ROOT),
            'Approved PIO-pair auxiliary policy changed')
    joint_wire, heap_wire = args.joint.read_bytes(), args.heap.read_bytes()
    joint, heap = json.loads(joint_wire), json.loads(heap_wire)
    from check_native_bottom_heap import require_companion as bottom_heap_companion
    from check_native_followup_profile import bottom_stage_profile, bottom_worker_profile
    bottom_stage=read_elf(args.build/'stage.elf')
    bottom_worker=read_elf(args.build/'native-worker.elf')
    require(bottom_stage_profile(bottom_stage) and bottom_worker_profile(bottom_worker) and
            bottom_heap_companion(bottom_stage,bottom_worker,report.get('bottom_heap_reservation'),heap),
            'Delivery requires the exact bottom cold/worker/original-corpus companion')
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
                  'kui-rayman-resource-probe.py': (ROOT/'tools/kui-rayman-resource-probe.py').read_bytes(),
                  'RAYMAN-PROBE.md': (ROOT/'tools/kui-rayman-resource-probe-README.md').read_bytes(),
                  'DOA2-PROBE.md': (ROOT/'tools/kui-doa2-filesystem-probe-README.md').read_bytes(),
                  'evidence/final-joint-corpus.json': joint_wire,
                  'evidence/final-original-heap.json': heap_wire,
                  'evidence/host-tests.log': args.host_log.read_bytes(),
                  'evidence/package-audit.log': args.package_log.read_bytes()})
    for path in sorted((ROOT/'docs/evidence').glob('native-*2026-10-2[56].json')):
        if any(word in path.name for word in ('structural', 'joint', 'pio-pair-observed', 'new-structural', 'console-followup', 'followup-review', 'terminal-dispatch', 'startup-source-pass', 'card-preflight', 'new-default', 'raw-owner', 'selected-low', 'dual-low-callback', 'doa2-smaller', 'doa2-fixed', 'crazy-taxi-source-admission', 'bottom-heap', 'private-pool', 'vmu-product-class')):
            files['evidence/'+path.name] = path.read_bytes()
    candidate = dict(schema='kui.native.universal-readers.candidate.v1', source_commit=commit,
                     source_tree=tree, build=revision, module_sha256=sha(module),
                     runtime_sha256=sha(gui), new_reader='X', legacy_reader='Y',
                     virtual_slot='A2', source_bundle_prerequisite=PREREQUISITE,
                     source_images_analyzed=26, joint_software_candidates=joint['joint_candidate_slugs'],
                     title_allowlist=False, approved_reader_policy_preserved=True,
                     console_compatibility_verified=False, reader_runtime_comparison=runtime_comparison,
                     shared_card_preflight=json.loads((ROOT/'docs/evidence/native-shared-card-preflight-2026-10-25.json').read_text()),
                     game_startup_followup=json.loads((ROOT/'docs/evidence/native-game-startup-followup-review-2026-10-25.json').read_text()),
                     console_observations=json.loads((ROOT/'docs/evidence/native-universal-console-followup-2026-10-25.json').read_text())['console_observations'],
                     console_followup_f241=json.loads((ROOT/'docs/evidence/native-console-followup-f241-2026-10-25.json').read_text()),
                     console_followup_c29=json.loads((ROOT/'docs/evidence/native-console-followup-c29-2026-10-26.json').read_text()),
                     bottom_heap_followup=json.loads((ROOT/'docs/evidence/native-bottom-heap-reservation-2026-10-26.json').read_text()),
                     private_pool_capacity=json.loads((ROOT/'docs/evidence/native-private-pool-capacity-2026-10-26.json').read_text()),
                     vmu_product_class=json.loads((ROOT/'docs/evidence/native-vmu-product-class-2026-10-26.json').read_text()),
                     console_followup_cd3318=json.loads((ROOT/'docs/evidence/native-console-followup-cd3318-new-2026-10-26.json').read_text()),
                     new_default_followup=json.loads((ROOT/'docs/evidence/native-new-default-followup-review-2026-10-26.json').read_text()),
                     raw_owner_terminal_followup=json.loads((ROOT/'docs/evidence/native-raw-owner-terminal-review-2026-10-26.json').read_text()),
                     additional_source_input_reviews=dict(
                         scope='Additional original-input analysis only; the 26-input corpus and eight software candidates are unchanged. No additional game admission or console compatibility is claimed.',
                         crazy_taxi=json.loads((ROOT/'docs/evidence/native-crazy-taxi-source-admission-review-2026-10-26.json').read_text()),
                         doa2_fixed_arena=json.loads((ROOT/'docs/evidence/native-doa2-fixed-arena-counterexamples-2026-10-26.json').read_text())))
    delivery = dict(source_commit=commit, generic_installer=host_installer,
                    combined_installer=extracted_install_check(files),
                    optional_rayman_probe=extracted_resource_probe_check(files),
                    same_clean_UI_and_module_source=True, no_retail_or_save_payloads=True,
                    final_corpus_artifacts_bound=True, reader_runtime_comparison=runtime_comparison,
                    console_execution_verified=False)
    args.public.mkdir(parents=True, exist_ok=True)
    bundle = args.public/'native-source-update.bundle'
    subprocess.run(['git', 'bundle', 'create', str(bundle.resolve()), 'HEAD', '^'+PREREQUISITE], cwd=ROOT, check=True)
    subprocess.run(['git', 'bundle', 'verify', str(bundle.resolve())], cwd=ROOT, check=True, capture_output=True)
    full_bundle = args.public/'native-source-full.bundle'
    subprocess.run(['git','bundle','create',str(full_bundle.resolve()),'HEAD'],cwd=ROOT,check=True)
    subprocess.run(['git','bundle','verify',str(full_bundle.resolve())],cwd=ROOT,check=True,capture_output=True)
    full_wire=full_bundle.read_bytes()
    # An independent empty clone proves this bundle has no prerequisite on
    # another workspace, public branch, network connection or local object.
    with tempfile.TemporaryDirectory(prefix='kui-native-offline-source-') as temporary:
        restored=Path(temporary)/'restored-source'
        subprocess.run(['git','clone','--no-checkout',str(full_bundle.resolve()),str(restored)],
                       check=True,capture_output=True)
        subprocess.run(['git','-C',str(restored),'checkout','--detach',commit],check=True,capture_output=True)
        restored_tree=subprocess.check_output(['git','-C',str(restored),'rev-parse','HEAD^{tree}'],text=True).strip()
        require(restored_tree==tree and not subprocess.check_output(
            ['git','-C',str(restored),'status','--porcelain'],text=True),
            'Full source bundle did not restore the exact clean source tree offline')
    full_info=dict(path='source/native-source-full.bundle',bytes=len(full_wire),sha256=sha(full_wire),
                   prerequisites=[],clean_offline_clone_and_tree_verified=True)
    candidate['source_full_bundle']=full_info
    candidate['source_recovery_default']='self-contained offline full-history bundle'
    delivery['source_full_bundle']=full_info
    patch = subprocess.check_output(['git', 'diff', '--binary', PREREQUISITE, 'HEAD'], cwd=ROOT)
    recovery = f'''# Source recovery

Source {commit}, tree {tree}.

The preferred recovery is self-contained and needs no network or earlier
checkout. From the extracted ZIP root:

```sh
git clone source/native-source-full.bundle restored-source
git -C restored-source checkout --detach {commit}
git -C restored-source rev-parse 'HEAD^{{tree}}'
```

The printed tree must equal `{tree}`. Alternatively run
`python3 source/checkout-source.py`; it verifies the bundle SHA256 and tree
and refuses a dirty existing checkout. The full bundle was independently
cloned and its clean tree verified during packaging.

Full bundle: {len(full_wire)} bytes, SHA256 {sha(full_wire)}.

The smaller source/native-source-update.bundle is also retained for an
existing checkout containing prerequisite {PREREQUISITE}. Fetch that local
bundle and check out {commit}, refusing a dirty checkout first. It is an
optional incremental path, not a prerequisite for the full bundle.
'''.encode()
    checkout = f'''#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent
root=here.parent if here.name=='source' else here
item=json.loads((root/'CANDIDATE.json').read_text())
full=here/'native-source-full.bundle'
info=item['source_full_bundle']
if full.stat().st_size!=info['bytes'] or hashlib.sha256(full.read_bytes()).hexdigest()!=info['sha256']:
    raise SystemExit('Full source bundle checksum mismatch')
dest=root/'restored-source'
def git(*a):return subprocess.check_output(['git','-C',str(dest),*a],text=True).strip()
if dest.is_symlink():raise SystemExit('Refusing a symlink source destination')
if dest.exists():
    top=subprocess.run(['git','-C',str(dest),'rev-parse','--show-toplevel'],text=True,capture_output=True)
    if top.returncode or pathlib.Path(top.stdout.strip()).resolve()!=dest:
        raise SystemExit('Destination is not a source repository root')
    if git('status','--porcelain'):raise SystemExit('Refusing a dirty source checkout')
    subprocess.run(['git','-C',str(dest),'fetch',str(full),'HEAD'],check=True)
else:
    subprocess.run(['git','clone','--no-checkout',str(full),str(dest)],check=True)
if git('rev-parse',item['source_commit']+'^{{tree}}')!=item['source_tree']:
    raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
if git('status','--porcelain'):raise SystemExit('Restored source is not clean')
print('Verified offline native X/Y source',item['source_commit'],dest)
'''.encode()
    files.update({'CANDIDATE.json': wire(candidate), 'delivery-checks.json': wire(delivery),
                  'source/native-source-update.bundle': bundle.read_bytes(),
                  'source/native-source-full.bundle': full_wire,
                  'source/checkout-source.py': checkout,
                  'source/source-change.patch': patch, 'source/SOURCE-RECOVERY.md': recovery})
    delivery['offline_source_recovery']=offline_source_recovery_check(candidate,checkout,full_wire)
    files['delivery-checks.json']=wire(delivery)
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
    public['native-source-full.bundle'] = full_wire
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
