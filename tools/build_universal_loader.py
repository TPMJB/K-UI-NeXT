#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and qualify the shared ordinary/WinCE/CDDA loader rollout.

This is the cold-reader portion of the universal-loader integration. Accepted
Standard, compact Background, physical-disc VMU and Smash components have their
own preservation and qualification gates; this command does not replace them
or generate an installable update by itself.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from global_loader_rollout import CE, DEFAULT_BUILDS, collect, wire, write_rollout
from native_cdda_manifest import PROFILES

NATIVE_FLAGS = (
    'KUI_RETAIL_LOW_RESIDENT=1', 'KUI_RETAIL_STARTUP_TRACE=0',
    'KUI_RETAIL_SONIC_STACK_TEST=0', 'KUI_RETAIL_RETURN_TRACE=0',
    'KUI_GENERAL_SCI_RESUME_FIX=1', 'KUI_RETAIL_NATIVE_DISC_CHECK=1',
    'KUI_RETAIL_NATIVE_DRIVE_STATE=1', 'KUI_CE_SCI_RESUME_FIX=1',
)
PRESERVED_FAMILIES = ('standard_vmu', 'compact_background_a1_a2',
                      'physical_disc_vmu_a1_a2', 'accepted_smash_collection')
MANIFEST_HEADER = ROOT / 'build/universal-native-cdda-manifest.h'
MANIFEST_REPORT = ROOT / 'build/universal-native-cdda-manifest.json'


def source_state():
    return dict(commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                                               text=True).strip(),
                source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)))


def plan(commit, jobs, allow_dirty=False):
    if len(commit) != 40 or any(c not in '0123456789abcdef' for c in commit):
        raise ValueError('Expected a complete source commit')
    if not 1 <= jobs <= 32:
        raise ValueError('Jobs must be between 1 and 32')
    build_id = 'BUILD_ID=' + commit[:12]
    steps = [dict(name='ordinary', roles=['physical', 'ce', 'zso', 'lzo'], command=[
        'make', '-f', 'Makefile.dc', '-j' + str(jobs), build_id, *NATIVE_FLAGS,
        'retail-physical', 'build/retail-ce/entry.elf',
        'build/retail-zso/entry.elf', 'build/retail-lzo/entry.elf']),
        dict(name='standalone', roles=['standalone'], command=[
            'make', '-f', 'Makefile.native-standalone-general', '-j' + str(jobs), build_id,
            'build/native-standalone-general/retail-native-standalone.kui'])]
    # The production aggregate owns the CE-specific flags. Calling wrappers
    # alone can lose the accepted CE resume/layout settings or inherit native
    # diagnostics; do not duplicate its flag vector here.
    steps.append(dict(name='ce_cdda', roles=['ce_' + profile for profile in CE], command=[
        'make', '-f', 'Makefile.dc', '-j' + str(jobs), build_id, 'wince-cdda']))
    steps.append(dict(name='native_cdda', roles=[
        'native_' + profile.replace('-', '_') for profile in PROFILES], command=[
        sys.executable, 'tools/build_native_cdda_compat.py', '--jobs', str(jobs),
        *(['--allow-dirty'] if allow_dirty else [])]))
    covered = [role for step in steps for role in step['roles']]
    if len(covered) != len(set(covered)) or set(covered) != set(DEFAULT_BUILDS):
        raise ValueError('Build plan does not cover every collected cold-reader role exactly once')
    steps.append(dict(name='native_cdda_manifest', roles=[], command=[
        sys.executable, 'tools/native_cdda_manifest.py',
        '--header', str(MANIFEST_HEADER.relative_to(ROOT)),
        '--report', str(MANIFEST_REPORT.relative_to(ROOT)),
        *(['--allow-dirty'] if allow_dirty else [])]))
    return dict(schema='kui.universal-loader-build-plan.v1', source_commit=commit,
        cold_reader_roles=sorted(covered), steps=steps,
        separately_preserved_families=list(PRESERVED_FAMILIES),
        native_CDDA_GUI_manifest=str(MANIFEST_HEADER.relative_to(ROOT)),
        installable_update_created=False, full_service_rollout_qualified=False,
        hardware_compatibility_claimed=False)


def build(selected, initial, allow_dirty):
    if not shutil.which('sh-elf-gcc'):
        raise ValueError('Source the configured Dreamcast SDK environment before building')
    destination = ROOT / 'build/universal-loader-build.json'
    destination.unlink(missing_ok=True)
    results = []
    for step in selected['steps']:
        current = source_state()
        if current['commit'] != initial['commit'] or (current['source_dirty'] and not allow_dirty):
            raise ValueError('Source changed during the build; no coherent rollout is qualified')
        log = ROOT / ('build/universal-loader-' + step['name'] + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('w') as output:
            result = subprocess.run(step['command'], cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            raise ValueError('Build failed at ' + step['name'] + '; inspect ' + str(log))
        results.append(dict(name=step['name'], log=str(log.relative_to(ROOT)), returncode=0))
        print('Built ' + step['name'], flush=True)
    current = source_state()
    if current['commit'] != initial['commit'] or (current['source_dirty'] and not allow_dirty):
        raise ValueError('Source changed during the build; no coherent rollout is qualified')
    report = dict(selected, source_dirty=initial['source_dirty'] or current['source_dirty'],
                  completed_steps=results, all_cold_build_steps_completed=True,
                  actual_GUI_admission_checked=False)
    destination.write_bytes(wire(report))
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('plan', 'build', 'check'))
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--allow-dirty', action='store_true', help='Local development only')
    parser.add_argument('--runtime-elf', type=Path,
                        help='The actual GUI linked with the generated native CDDA manifest')
    parser.add_argument('--output', type=Path, help='New or empty checked cold-rollout directory')
    args = parser.parse_args()
    try:
        initial = source_state()
        selected = plan(initial['commit'], args.jobs, args.allow_dirty)
        if args.action == 'plan':
            print(json.dumps(dict(selected, source_dirty=initial['source_dirty']), indent=2))
            return
        if initial['source_dirty'] and not args.allow_dirty:
            raise ValueError('Commit the intended source before building or qualifying deliverables')
        if args.action == 'build':
            build(selected, initial, args.allow_dirty)
            return
        if args.runtime_elf is None or args.output is None:
            raise ValueError('check requires --runtime-elf and --output for actual GUI binding')
        files, report = collect(DEFAULT_BUILDS, initial['commit'], allow_dirty=args.allow_dirty,
                                runtime_elf=args.runtime_elf)
        output = write_rollout(files, args.output)
        print(json.dumps(dict(status='PASS', output=str(output),
            installed_cold_reader_files=report['installed_files'],
            separately_preserved_families=list(PRESERVED_FAMILIES),
            installable_update_created=False, full_service_rollout_qualified=False)))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
