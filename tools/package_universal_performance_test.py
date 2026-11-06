#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver shared launch/VMU defaults with complete family and cohort gates."""
import argparse
from concurrent.futures import ProcessPoolExecutor
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from package_smash_native_menu_test import INSTALL, optimized_runtime_proof
from package_vc2_ce_vmu_late_test import ROOT, git, installed_module, sha, wire
from runtime_package import envelope, flatten_elf, verify

CONFIRMED = '2c53ceafadf51d10b2aedeeb4e33ed8b5a6ca0c0'
CONFIRMED_SHA = 'acfa1c691ba55bb927051176af8f8bdb046be6981566637fe070721ca4f12ed1'
BACKGROUND_TARGETS = {
    'background_a2': 'KUI/apps/games/retail-native-dual.kui',
    'background_a1': 'KUI/apps/games/retail-native-dual-a1.kui',
}
FUNCTIONAL_TARGETS = frozenset({
    'KUI/runtime.kui', 'KUI/apps/games/retail-boot.kui',
    *BACKGROUND_TARGETS.values(),
    'KUI/apps/disc/native-disc-vmu.kui', 'KUI/apps/disc/native-disc-vmu-a1.kui',
    'KUI/apps/games/retail-smash-menu.kui', 'KUI/apps/games/retail-smash-native-menu.kui',
})


def require(ok, message):
    if not ok:
        raise ValueError(message)


def require_functional_targets(files, *, focused=False):
    installed={name for name in files if name.startswith('KUI/')}
    require(installed==FUNCTIONAL_TARGETS,
            'The compact default update must install exactly its eight matched files')
    allowed=FUNCTIONAL_TARGETS if focused else FUNCTIONAL_TARGETS | {prefix + name for prefix in
        ('Optional-timing-stop/', 'Restore-Smash-engine-2c53/') for name in
        ('KUI/apps/games/retail-smash-menu.kui', 'KUI/apps/games/retail-smash-native-menu.kui')}
    require(all(not name.endswith('.kui') or name in allowed for name in files),
            'Release contains a noncanonical installable module')
    return sorted(installed)


def checked_command(arguments):
    run = subprocess.run(arguments, cwd=ROOT, capture_output=True, text=True, check=True)
    return dict(command=arguments, stdout=run.stdout.strip(), stderr=run.stderr.strip())


def native_safety(role, directory, *, focused=False, reference=None):
    # The adversarial sweeps have already exercised the independently pinned
    # bodies. Re-admit the final linked cohort and execute its normal models;
    # rebuilding the exact build label does not require repeating those sweeps.
    try:
        if focused:
            require(role.startswith('background_') and reference is not None,
                    'Focused compact qualification needs its independent reference')
            from check_native_compact_delivery import audit
            report = audit(directory,reference=reference,slot=1 if role.endswith('_a1') else 2)
        elif role == 'standard':
            from check_native_vmu_only import audit
            report = audit(directory, transport=1, self_test=False)
        else:
            from check_native_background_compact import audit
            report = audit(directory, self_test=False)
    except Exception as error:
        print(json.dumps(dict(check=role, status='FAIL', error=str(error))), flush=True)
        raise
    print(json.dumps(dict(check=role, status='PASS')), flush=True)
    return report


def menu_safety(outer, child, runtime, runtime_elf, gin, *, focused=False):
    from check_smash_menu_loader import audit
    role = 'smash_' + outer.name
    try:
        if focused:
            from check_focused_loader_delivery import smash_binding
            report = smash_binding(outer,child,git('rev-parse','HEAD').decode().strip())
        else:
            report = audit(outer, child, runtime=runtime, runtime_elf=runtime_elf, gin=gin)
    except Exception as error:
        print(json.dumps(dict(check=role, status='FAIL', error=str(error))), flush=True)
        raise
    print(json.dumps(dict(check=role, status='PASS')), flush=True)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for role, folder in (
        ('standard', 'universal-standard-sci'),
        ('background-a2', 'universal-background-a2'), ('background-a1', 'universal-background-a1'),
        ('disc-a2', 'universal-disc-a2'), ('disc-a1', 'universal-disc-a1'),
    ):
        p.add_argument('--' + role, type=Path, default=ROOT / 'build' / folder)
    p.add_argument('--outer-build', type=Path, default=ROOT / 'build/smash-menu-fast')
    p.add_argument('--child-build', type=Path, default=ROOT / 'build/smash-native-fast')
    p.add_argument('--timing-outer', type=Path, default=ROOT / 'build/smash-menu-timing')
    p.add_argument('--timing-child', type=Path, default=ROOT / 'build/smash-native-timing')
    p.add_argument('--runtime-elf', type=Path, default=ROOT / 'build/kui-runtime.elf')
    p.add_argument('--standard-manifest', type=Path, default=ROOT / 'build/universal-native-standard-manifest.json')
    p.add_argument('--history-report', type=Path)
    p.add_argument('--checksum-report', type=Path)
    p.add_argument('--focused',action='store_true',
        help='Next-test qualification; record historical suites as not rerun')
    p.add_argument('--compact-reference-a1',type=Path)
    p.add_argument('--compact-reference-a2',type=Path)
    p.add_argument('--qualified-components',type=Path)
    p.add_argument('--qualified-baseline-archive',type=Path)
    p.add_argument('--cold-stage-report',type=Path,action='append',default=[])
    p.add_argument('--confirmed', type=Path, required=True)
    p.add_argument('--gin', type=Path, required=True)
    p.add_argument('--main', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    require(not git('status', '--porcelain'), 'Commit final source before packaging')
    commit = git('rev-parse', 'HEAD').decode().strip()
    base = git('rev-parse', a.main).decode().strip()
    for parent in (base, CONFIRMED):
        subprocess.run(['git', 'merge-base', '--is-ancestor', parent, commit], cwd=ROOT, check=True)
    require(a.output.suffix == '.zip' and not a.output.resolve().is_relative_to(ROOT), 'Save the ZIP outside the checkout')
    require(sha(a.confirmed.read_bytes()) == CONFIRMED_SHA, 'Confirmed restore differs from the original delivery')
    files = {'LICENSE.txt': (ROOT / 'LICENSE').read_bytes()}
    proofs = {}
    qualified=None
    if a.focused:
        require(all((a.compact_reference_a1,a.compact_reference_a2,a.qualified_components,
                     a.qualified_baseline_archive)),
                'Focused qualification requires independent compact and prior945 component references')
        from check_focused_loader_delivery import baseline_records,compare_components,COMPONENTS
        qualified,baseline_evidence=baseline_records(a.qualified_baseline_archive)
        for name,body in baseline_evidence.items():files['evidence/baseline-945/'+name+'.json']=body
        for role,names in COMPONENTS.items():
            for name in names:
                body=(a.qualified_components/role/name).read_bytes()
                require(sha(body)==qualified[role][name],'945 reference changed before packaging')
                files['evidence/reference-945/'+role+'/'+name]=body
        for slot,reference in ((1,a.compact_reference_a1),(2,a.compact_reference_a2)):
            files['evidence/reference-compact-a'+str(slot)+'/native-worker.elf']=reference.read_bytes()
    payload, memory = flatten_elf(a.runtime_elf.read_bytes())
    runtime = envelope(payload, memory, commit[:12])
    runtime_path = a.outer_build / 'runtime.kui'
    require(runtime_path.read_bytes() == runtime, 'Prepared runtime differs from the final ELF')
    data, provenance = installed_module(runtime_path, a.runtime_elf, commit)
    files['KUI/runtime.kui'] = data
    proofs['runtime_compile'] = provenance
    proofs['runtime_optimization'] = optimized_runtime_proof(a.runtime_elf)

    # Recognition is generated while building software, never learned from
    # an arbitrary installed module. Bind the actual linked GUI constant too.
    from native_standard_manifest import generate, ALIAS
    from test_native_standard_manifest import audit as manifest_audit
    header, manifest = generate(a.standard)
    recorded = json.loads(a.standard_manifest.read_text())
    require(all(recorded.get(k) == v for k, v in manifest.items()) and
            recorded.get('header_sha256') == sha(header.encode()), 'Stale generated Standard manifest')
    from check_shared_loader_performance import runtime_standard_manifest_binding
    proofs['runtime_standard_manifest_binding'] = runtime_standard_manifest_binding(
        read_elf(a.runtime_elf, allow_tls=True), manifest['normalized_payload_sha256'])
    proofs['standard_cohort_admission'] = manifest_audit(a.standard)
    files['evidence/standard-build-manifest.json'] = wire(recorded)

    # Derive the only launch-mutable profile from its exact typed linked owner.
    # A module compiled with private card data must never become a template.
    from package_native_vmu_only import bind_profile
    from check_shared_loader_performance import compact_background_binding
    for role, build, name in (
        ('standard', a.standard, 'apps/games/retail-boot.kui'),
        ('background_a2', a.background_a2, 'apps/games/retail-native-dual.kui'),
        ('background_a1', a.background_a1, 'apps/games/retail-native-dual-a1.kui'),
    ):
        filename = ALIAS if role == 'standard' else 'retail-native-background-compact.kui'
        module, receipt = installed_module(build / filename, build / 'entry.elf', commit)
        bound, personalization = bind_profile(build, commit, filename)
        require(module == bound, 'Installed native module differs from its canonical profile binding')
        proofs[role + '_canonical_profile'] = personalization
        if role != 'standard':
            slot = 1 if role.endswith('_a1') else 2
            proofs[role + '_compact_geometry'] = compact_background_binding(
                *(read_elf(build/(part+'.elf')) for part in
                  ('stage','native-worker','resident-sci','resident-legacy','native-ra-aux')),slot)
        files['KUI/' + name] = module
        proofs[role + '_compile'] = receipt

    # The next test reuses authenticated unchanged components and executes the
    # changed cold-stage models. The historical suites remain separately named.
    pool = ProcessPoolExecutor(max_workers=5)
    pending={}
    for role,build in (('standard',a.standard),('background_a2',a.background_a2),('background_a1',a.background_a1)):
        if a.focused and role=='standard':
            from check_focused_loader_delivery import native_embedding
            proofs['standard_safety']=dict(
                unchanged_components=compare_components(role,build,a.qualified_components,qualified[role],commit),
                current_embedding=native_embedding(build,commit),
                full_historical_standard_audit_executed=False)
        else:
            reference=a.compact_reference_a1 if role.endswith('_a1') else a.compact_reference_a2
            pending[role+'_safety']=pool.submit(native_safety,role,build,focused=a.focused,reference=reference)
    menu_builds=[('functional',a.outer_build,a.child_build)]
    if not a.focused:menu_builds.append(('timing',a.timing_outer,a.timing_child))
    menu_pending={label:pool.submit(menu_safety,outer,child,runtime_path,a.runtime_elf,a.gin,focused=a.focused)
                  for label,outer,child in menu_builds}
    if a.focused:
        for role,build in (('smash_outer',a.outer_build),('smash_child',a.child_build)):
            proofs[role+'_unchanged_components']=compare_components(
                role,build,a.qualified_components,qualified[role],commit)

    from package_physical_disc_test import collect_vmu_build
    from check_disc_vmu_stack import check as disc_stack
    for role, build, name, slot in (
        ('disc_a2', a.disc_a2, 'apps/disc/native-disc-vmu.kui', 2),
        ('disc_a1', a.disc_a1, 'apps/disc/native-disc-vmu-a1.kui', 1),
    ):
        module, bound = collect_vmu_build(build, commit[:12], commit)
        stage, worker = read_elf(build / 'disc-stage.elf'), read_elf(build / 'native-worker.elf')
        require(stage[4].get('__kui_disc_vmu_virtual_slot') == (slot, 0, 0, 0xfff1) and
                worker[4].get('__kui_native_physical_disc_virtual_slot') == (slot, 0, 0, 0xfff1),
                'Disc stage/worker slot mismatch')
        if a.focused:
            from check_focused_loader_delivery import native_embedding
            proofs[role+'_safety']=dict(
                unchanged_components=compare_components(role,build,a.qualified_components,qualified[role],commit),
                current_embedding=native_embedding(build,commit,disc=True),
                full_historical_disc_stack_and_handoff_suites_executed=False,
                artifacts_sha256={n:sha(v if isinstance(v,bytes) else wire(v)) for n,v in bound.items()})
        else:
            commands = [
                ['python3','tools/check_disc_vmu_handoff.py',str(build/'disc-stage.elf'),'--support',str(build/'support/support.elf')],
                ['python3','tests/test_disc_vmu_handoff.py',str(build/'disc-stage.elf'),str(build/'support/support.elf')],
                ['python3','tests/test_disc_vmu_stack.py',str(build)],
            ]
            proofs[role+'_safety']=dict(stack=disc_stack(build),checks=[checked_command(c) for c in commands],
                artifacts_sha256={n:sha(v if isinstance(v,bytes) else wire(v)) for n,v in bound.items()})
        files['KUI/' + name] = module

    for label,outer,child in menu_builds:
        report = menu_pending[label].result()
        require(report['status'] == 'PASS', 'Smash family functional audit failed')
        if label == 'timing':
            require(report['timing_diagnostic']['status'] == 'PASS' and
                    report['timing_diagnostic'].get('original_video_seed'), 'Timing profile lacks its original-video proof')
        else:
            require(not report['timing_diagnostic']['enabled'], 'Functional default must not intentionally stop')
        proofs['smash_' + label] = report
        prefix = 'KUI/' if label == 'functional' else 'Optional-timing-stop/KUI/'
        for name, module, elf in (
            (INSTALL[1], outer / 'retail-smash-menu.kui', outer / 'entry.elf'),
            (INSTALL[2], child / 'retail-smash-native-menu.kui', child / 'entry.elf'),
        ):
            body, receipt = installed_module(module, elf, commit)
            owner = 'outer' if name == INSTALL[1] else 'child'
            require(sha(body) == report['artifacts_sha256'][owner + '/' + module.name],
                    'Installed Smash module differs from its completed family audit')
            files[prefix + name] = body
            proofs[label + '_' + Path(name).stem + '_compile'] = receipt

    if a.focused:
        from check_focused_loader_delivery import cold_performance
        require(len(a.cold_stage_report)<=1,'Use one complete current cold-stage report')
        proofs['focused_cold_load']=cold_performance(a.outer_build,a.child_build,
            a.gin.parent/'MDE_US.BIN',a.cold_stage_report[0] if a.cold_stage_report else None)
    from check_shared_loader_performance import audit as shared_audit
    builds = dict(standard=a.standard, background_a2=a.background_a2, background_a1=a.background_a1,
                  disc_a2=a.disc_a2, disc_a1=a.disc_a1, smash_outer=a.outer_build, smash_child=a.child_build)
    proofs['shared_performance'] = shared_audit(builds, a.runtime_elf, history_report=a.history_report,
                                               checksum_report=a.checksum_report,focused=a.focused)
    require(proofs['shared_performance']['status'] == 'PASS', 'A loader family lost its shared defaults')
    require(sha(files['KUI/runtime.kui']) == proofs['shared_performance']['runtime']['module_sha256'],
            'Installed runtime differs from the completed shared cohort audit')
    for role, name in (
        ('standard', 'apps/games/retail-boot.kui'),
        ('background_a2', 'apps/games/retail-native-dual.kui'),
        ('background_a1', 'apps/games/retail-native-dual-a1.kui'),
        ('disc_a2', 'apps/disc/native-disc-vmu.kui'),
        ('disc_a1', 'apps/disc/native-disc-vmu-a1.kui'),
        ('smash_outer', INSTALL[1]), ('smash_child', INSTALL[2]),
    ):
        require(sha(files['KUI/' + name]) == proofs['shared_performance']['cohort'][role]['module_sha256'],
                'Installed module differs from the completed shared cohort audit: ' + role)
    from test_smash_launch_timing import audit as ui_audit
    proofs['ui_report'] = ui_audit()
    proofs['compact_release_regressions'] = checked_command(
        ['python3','tests/test_shared_loader_compact_release.py'])
    proofs.update({role: future.result() for role, future in pending.items()})
    pool.shutdown()
    proofs['qualification_scope']=dict(focused_next_test=a.focused,
        full_historical_native_suites_reexecuted=not a.focused,
        full_historical_smash_GIN_suite_reexecuted=not a.focused,
        new_shared_stage_instruction_models_executed=True,
        exact_embedding_geometry_and_component_qualification_executed=True,
        console_execution_verified=False)
    for name, proof in proofs.items():
        files['evidence/' + name + '.json'] = wire(proof)

    if not a.focused:
        with zipfile.ZipFile(a.confirmed) as z:
            require(z.testzip() is None, 'Confirmed restore ZIP failed CRC')
            for name in INSTALL[1:]:
                old = z.read('KUI/' + name)
                require(verify(old)['build'] == CONFIRMED[:12], 'Mixed confirmed restore')
                files['Restore-Smash-engine-2c53/KUI/' + name] = old
    files['source/change-from-confirmed-2c53.patch'] = git('diff', '--binary', CONFIRMED, commit)
    files['source/change-from-main.patch'] = git('diff', '--binary', base, commit)
    files['source/source.tar.gz'] = subprocess.check_output(['git', 'archive', '--format=tar.gz', '--prefix=K-UI/', commit], cwd=ROOT)
    files['details/shared-regressions.md'] = (ROOT / 'docs/shared-loader-vmu-regressions.md').read_bytes()
    files['details/shared-policy.md'] = (ROOT / 'docs/shared-loader-performance-policy.md').read_bytes()
    files['details/history.md'] = (ROOT / 'docs/vmu-history.md').read_bytes()
    require_functional_targets(files,focused=a.focused)
    files['BUILD.json'] = wire(dict(schema='kui.universal-launch-performance.v2', commit=commit,
        base=base, hardware_verified=False, console_speedup_measured=False,
        shared_families=list(builds), background_profile='compact72',
        background_reader_routes=dict(new_x='retail-native-dual[-a1].kui',
            legacy_y='retail-native-dual[-a1].kui'),
        installed_file_count=len(FUNCTIONAL_TARGETS), background_worker_bytes=0x12000,
        background_lease_bytes=0x47000, background_cache_bytes=0x30000,
        background_worker_stack_bytes=0xc00, optional_session_controls_installed=False,
        qualification_mode='focused-next-test' if a.focused else 'historical-family-audits',
        full_historical_suites_reexecuted=not a.focused,optional_timing_and_restore_modules_included=not a.focused,
        default_intentional_stop=False, original_game_bytes_included=False,
        install={name: dict(bytes=len(body), sha256=sha(body)) for name, body in files.items() if name.startswith('KUI/')},
        confirmed_smash_restore=CONFIRMED))
    files['README-FIRST.txt'] = f'''K-UI shared launch / VMU performance test {commit[:12]}

INSTALL
Copy all eight files in the TOP-LEVEL KUI folder over the SD installation,
replacing their existing copies.
Keep the current game images and virtual cards. This is the functional build:
it continues into games and does not stop for photographs.

These are common-path changes: bounded image and history reads, same-card
session preparation, fresh FAT windows, and inherited immutable-proof cache
defaults. Every SD block CRC, full source SHA/CRC, current card ownership check
and durable write/SYNC acknowledgement remain required. Standard, compact
Background A1/A2 and physical-disc A1/A2 modules are one checked cohort.
Both New/X and Legacy/Y use retail-native-dual[-a1].kui: 72 KiB worker,
284 KiB complete lease and the full 192 KiB read cache. This restores the
current compact default; historical General and optional session-control
modules are not installed. Old retail-native-session[-a1].kui files left by
the superseded update are ignored by the corrected GUI route.
Native SDK families and WinCE remain distinct compatibility backends; these
changes do not claim that every untested game or new SDK family is supported.

CHECK
Launch Smash Pack with the same settings used by the confirmed build. Choose
Shining Force, then use ABXY+Start and choose Phantasy Star II. Confirm both
cards/saves and report the pre-bootstrap and selection-to-game waits.
KUI/apps/games/smash-launch-timing.txt records the preparation phases, exact
installed-module fingerprints and cache outcomes. Keep the file from each run.
The previous measured action-to-ready total was 24,918 ms, including 14,300 ms
history and 4,729 ms executable read/hash. New console elapsed time is unmeasured.

OPTIONAL DIAGNOSTIC
Only if native selection remains slow, Optional-timing-stop/KUI swaps the two
Smash modules for a deliberately held timing report before game entry. Its
video state now comes from K-UI before the original menu changes framebuffers.
Photograph the labelled TIMING screen, then power off. Copy this ZIP's top-level
KUI folder again to restore functional play. The diagnostic omits the later
game/SGA-loading tail; report whether the wait occurs before reaching its page.

RESTORE
Restore-Smash-engine-2c53/KUI contains the two byte-identical prior Smash
engine modules confirmed to retain the VMU across menu returns. It keeps the
current GUI and shared modules together. An older GUI runtime requires its
matching generic modules; do not combine that runtime with this build's new
Standard module. Existing private card data is not included or replaced.

Automated checks establish the shared algorithms, exact build cohort, storage
semantics and linked ownership/stack behavior. Console performance and gameplay
remain hardware checks. Source contains the complete current tree and patches
from confirmed {CONFIRMED} and main {base}.
'''.encode()
    if a.focused:
        guide=files['README-FIRST.txt'].decode()
        start=guide.index('OPTIONAL DIAGNOSTIC\n');end=guide.index('Automated checks establish',start)
        guide=guide[:start]+'''QUALIFICATION SCOPE
This is a focused next test. The current compact worker is checked against an
independently rebuilt pre-shared source reference. Unchanged Standard, disc and
Smash runtime components are bound to the prior complete945 reports. Current
entry/stage embeddings, geometry, slots, relocations and clean source receipts
are checked, and the changed shared cold cache/FAT/CRC models execute again.
The long historical native/original-corpus/GIN suites were not rerun for this
next test. Their old reports are labelled baseline-945, not current execution.
This ZIP has one KUI install folder and no optional held-screen or larger
Background modules. Actual console speed, gameplay and persistence still need
the hardware check above.

'''+guide[end:]
        files['README-FIRST.txt']=guide.encode()
    files['SHA256SUMS.txt'] = ''.join(sha(body) + '  ' + name + '\n' for name, body in sorted(files.items())).encode()
    require(not git('status', '--porcelain') and git('rev-parse', 'HEAD').decode().strip() == commit, 'Source changed during checks')
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, body in files.items():
            z.writestr(name, body)
    with zipfile.ZipFile(a.output) as z, tempfile.TemporaryDirectory(prefix='kui-shared-readback-') as folder:
        require(z.testzip() is None, 'Delivery ZIP CRC failed')
        z.extractall(folder)
        require(all((Path(folder) / n).read_bytes() == b for n, b in files.items()), 'Extracted bytes differ')
    print(json.dumps(dict(status='PASS', file=str(a.output), bytes=a.output.stat().st_size,
        sha256=sha(a.output.read_bytes()), commit=commit, extract_verified=True, hardware_verified=False)))


if __name__ == '__main__':
    main()
