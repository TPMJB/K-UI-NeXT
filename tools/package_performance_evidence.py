#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a copy-over console evidence suite from one clean source commit.

No console execution or improvement is inferred from successful host audits.
The baseline installation and each overlay contain their own explicit GUI.
"""
import argparse
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from package_fmv_tests import TOY_FLAGS, checked_module, check_toy, check_armada
from runtime_package import envelope, flatten_elf, verify
from performance_test_plan import make_cases, csv_bytes, jsonl_bytes, case_guide

ROOT = Path(__file__).resolve().parents[1]
BASE = '0045dd96937ee7485ecd4fe0432b28babea8646a'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def run(command, log):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('w') as output:
        result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise ValueError('Failed: ' + ' '.join(command) + '; inspect ' + str(log))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path)
    p.add_argument('--jobs', type=int, default=4)
    args = p.parse_args()
    if not 1 <= args.jobs <= 16:
        p.error('--jobs must be 1..16')
    if git('status', '--porcelain').strip():
        p.error('Commit the reviewed source before packaging')
    commit = git('rev-parse', 'HEAD').decode().strip()
    subprocess.run(['git', 'merge-base', '--is-ancestor', BASE, commit], cwd=ROOT, check=True)
    prefix = commit[:10]
    evidence = ROOT / 'build/performance-evidence'
    files, profiles = {}, {}

    def execute(command, name):
        log = evidence / (name + '.log')
        run(command, log)
        files['evidence/' + name + '.log'] = log.read_bytes()

    def add_profile(name, folder, target, module, **metadata):
        info = verify(module)
        member = folder + '/KUI/apps/games/' + target
        files[member] = module
        entry = profiles.setdefault(name, {'folder': folder, 'modules': [],
            'console_tested': False, 'prefetch': False, **metadata})
        entry['modules'].append({'member': member, 'build_id': info['build'],
            'sha256': sha(module), 'bytes': len(module)})

    # Meaningful production-loop/model tests, including invalid clocks and
    # candidate activation. These are correctness checks, not speed benchmarks.
    execute(['python3', 'tools/test_performance_evidence.py'], 'host-controls')
    execute(['python3', 'tests/test_analyze_performance_results.py'], 'results-parser')

    menus = {}
    for forced in (0, 1):
        # Existing DC object directory is shared by the GUI Makefile. Purge it
        # when changing route flags so no old object can select another module.
        shutil.rmtree(ROOT / 'build/dc', ignore_errors=True)
        build_id = prefix + ('f1' if forced else 'f0')
        execute(['make', '-f', 'Makefile.dc', 'KUI_FMV_TEST=1',
                 'KUI_PERF_NATIVE_STANDALONE=' + str(forced),
                 'BUILD_ID=' + build_id, '-j' + str(args.jobs), 'build/kui-runtime.elf'],
                'gui-' + str(forced))
        data, memory = flatten_elf((ROOT / 'build/kui-runtime.elf').read_bytes())
        record = json.loads((ROOT / 'build/kui-runtime.compile.json').read_text())
        if record.get('commit') != commit or record.get('source_dirty') is not False or \
           record.get('elf_sha256') != sha((ROOT / 'build/kui-runtime.elf').read_bytes()):
            raise ValueError('GUI compilation is not bound to clean source')
        menus[forced] = envelope(data, memory, build_id)
        verify(menus[forced])
        files['evidence/gui-' + str(forced) + '-compile.json'] = wire(record)

    for profile, code, folder, engaged, purpose in (
        (1, 'd0', '00-control', 'VISITS', 'AP demand control'),
        (2, 'd1', '01-toy-ready-yield', 'YIELDS', 'Return while an owned DMA receive is pending'),
        (3, 'd2', '02-toy-no-fingerprint', 'FP SKIPS', 'Remove only diagnostic first-sector fingerprints')):
        for cooked in (0, 1):
            build_id = prefix + code
            suffix = 'cooked' if cooked else 'raw'
            directory = ROOT / ('build/performance-toy-' + str(profile) + '-' + suffix)
            reference = ROOT / ('build/performance-toy-reference-' + str(profile) + '-' + suffix)
            flags = TOY_FLAGS + ['FMV_IDLE_REARM=0', 'TOY_DEMAND_TEST_PROFILE=' + str(profile),
                                 'BUILD_ID=' + build_id]
            if cooked:
                flags += ['COOKED_DATA_PROFILE=1']
            for selected, target in ((0, reference), (1, directory)):
                execute(['make', '-f', 'Makefile.toy_pilot', 'BUILD=' + str(target.relative_to(ROOT)),
                         'NATIVE_COMPAT=' + str(selected), *flags, '-j' + str(args.jobs), 'all'],
                        'toy-' + str(profile) + '-' + suffix + '-' + str(selected))
            proof = check_toy(directory, 'toy-' + suffix, reference)
            module = checked_module(directory, 'retail-toy-pilot.kui', build_id, commit)
            add_profile('T' + str(profile), folder, 'cdda-native-toy-' + suffix + '.kui', module,
                        purpose=purpose, engagement=engaged, family='native-CDDA',
                        virtual_vmu_supported=False, report_pages=5)
            files['evidence/toy-' + str(profile) + '-' + suffix + '-audit.json'] = wire(proof)
            files['evidence/toy-' + str(profile) + '-' + suffix + '-config.txt'] = (directory / 'build-config').read_bytes()
            print('Audited Toy T' + str(profile) + ' ' + suffix, flush=True)

    for profile, code, folder in ((1, 'e0', '00-control'), (2, 'e1', '03-armada-measured')):
        directory = ROOT / ('build/performance-armada-' + str(profile))
        shutil.rmtree(directory, ignore_errors=True)
        build_id = prefix + code
        execute(['make', '-f', 'Makefile.armada-stream', 'DEMAND_PROFILE=' + str(profile),
                 'BUILD_ID=' + build_id, '-j' + str(args.jobs), str(directory.relative_to(ROOT) / 'entry.elf')],
                'armada-' + str(profile))
        proof = check_armada(directory, stream=True)
        module = checked_module(directory, 'ce-cdda-armada.kui', build_id, commit)
        add_profile('C' + str(profile), folder, 'ce-cdda-armada.kui', module,
                    purpose='Existing DATA counters only' if profile == 1 else 'GD/IRQ aggregate observation',
                    engagement='GD calls / checked blocks', family='WinCE-CDDA',
                    virtual_vmu_supported=False, report_pages=5, timer_units='explicit per row; TMU modulo is a lower bound')
        files['evidence/armada-' + str(profile) + '-audit.json'] = wire(proof)
        execute(['python3', 'tools/check_ce_demand_profile.py', str(directory),
                 '--profile', str(profile), '--self-test'], 'armada-' + str(profile) + '-demand-audit')
        print('Audited Armada C' + str(profile), flush=True)

    for name, code, folder, capture, full in (
        ('N3', 'b4', '08-native-uninstrumented', 0, 0),
        ('N1', 'b0', '04-native-split-capture', 1, 0),
        ('N2', 'b1', '05-native-full-submit', 1, 1)):
        directory = ROOT / ('build/performance-native-' + name.lower())
        build_id = prefix + code
        execute(['make', '-f', 'Makefile.performance-native', 'BUILD=' + str(directory.relative_to(ROOT)),
                 'BUILD_ID=' + build_id, 'PERF_NATIVE_CAPTURE=' + str(capture),
                 'PERF_NATIVE_FULL_SUBMIT=' + str(full), '-j' + str(args.jobs), 'all'], 'native-' + name)
        module = checked_module(directory, 'retail-native-dual.kui', build_id, commit)
        payload = module[64:]
        if b'PREFETCH OFF / SPLIT CONTROLLER ALL\0' not in payload or \
           any(s in payload for s in (b'PREFETCH AUTO /', b'PREFETCH ON /')):
            raise ValueError('Native profile is not demand-only: ' + name)
        audit = ['python3', 'tools/check_performance_native.py', str(directory)]
        if name != 'N3':
            audit += ['--reference', str(ROOT / 'build/performance-native-n3')]
        execute(audit, 'native-' + name + '-audit')
        add_profile(name, folder, 'retail-native-dual.kui', module,
                    purpose='Full-submit control' if full else 'Split controller with capture' if capture else 'Uninstrumented split control',
                    family='native', virtual_vmu_supported=True, virtual_slot='A2',
                    worker_bytes=112 * 1024, reservation_bytes=324 * 1024,
                    capture=bool(capture), full_submit=bool(full), report_pages=4 if capture else None,
                    engagement='PERF captured polls' if capture else 'stage identity / terminal reader counters')
        if name == 'N3':
            files['00-control/KUI/apps/games/retail-native-dual.kui'] = module
        for filename in ('readonly-config', 'native-worker.map', 'native-worker.reloc', 'entry.compile.json'):
            path = directory / filename
            if path.exists():
                files['evidence/' + name + '-' + filename] = path.read_bytes()
        print('Audited native ' + name, flush=True)

    standalone = ROOT / 'build/performance-native-standalone'
    execute(['make', '-f', 'Makefile.native-standalone-stream-report', 'BUILD=' + str(standalone.relative_to(ROOT)),
             'BUILD_ID=' + prefix + 'b2', '-j' + str(args.jobs), 'all'], 'standalone')
    execute(['python3', 'tools/check_native_stream_report.py', str(standalone / 'resident-sci.elf'), '--self-test'],
            'standalone-report-audit')
    module = checked_module(standalone, 'retail-native-standalone.kui', prefix + 'b2', commit)
    add_profile('N0', '06-native-standalone', 'retail-native-standalone.kui', module,
                purpose='New demand without VMU worker/lease', family='native', virtual_vmu_supported=False,
                engagement='terminal SCI counters', forced_standalone_gui=True, worker_bytes=0)
    files['00-control/KUI/apps/games/retail-native-standalone.kui'] = module

    standard = ROOT / 'build/performance-native-standard'
    execute(['make', '-f', 'Makefile.vmu-native-vmu-lite-paced', 'BUILD=' + str(standard.relative_to(ROOT)),
             'BUILD_ID=' + prefix + 'b3', '-j' + str(args.jobs), 'all'], 'standard-vmu')
    module = checked_module(standard, 'retail-native-vmu-lite-paced.kui', prefix + 'b3', commit)
    add_profile('S1', '07-native-standard-vmu', 'retail-boot.kui', module,
                purpose='Paced Standard with virtual VMU', family='native', virtual_vmu_supported=True,
                virtual_slot='A2', worker_bytes=72 * 1024,
                engagement='stage identity / save readback', capture=False)
    files['00-control/KUI/apps/games/retail-boot.kui'] = module

    execute(['make', '-f', 'Makefile.dc', 'BUILD_ID=' + prefix + 'b5',
             '-j' + str(args.jobs), 'retail-physical'], 'standard-physical')
    physical = ROOT / 'build/retail-physical'
    module = checked_module(physical, 'retail-physical.kui', prefix + 'b5', commit)
    add_profile('S0', '00-control', 'retail-physical.kui', module,
                purpose='Ordinary Standard with VMU Off', family='native', virtual_vmu_supported=False,
                engagement='stage identity / terminal counters', worker_bytes=0)

    for folder in {p['folder'] for p in profiles.values()} | {'00-control'}:
        files[folder + '/KUI/runtime.kui'] = menus[1 if folder == '06-native-standalone' else 0]
    for filename in ('docs/performance-evidence-suite.md',):
        files['README-FIRST.md'] = (ROOT / filename).read_bytes()
    for filename in ('tools/analyze_performance_results.py', 'LICENSE', 'THIRD_PARTY.md'):
        files[filename] = (ROOT / filename).read_bytes()
    cases = make_cases(profiles)
    files['performance-results-template.csv'] = csv_bytes(cases)
    files['performance-results-template.jsonl'] = jsonl_bytes(cases)
    files['RUN-CHECKLIST.md'] = case_guide(cases).encode()
    files['source/K-UI-Performance-' + commit[:12] + '.tar.gz'] = gzip.compress(
        git('archive', '--format=tar', '--prefix=K-UI-Performance/', commit), mtime=0)
    files['source/performance-changes.patch'] = git('diff', '--binary', '--full-index', 'b7b148e4a158', commit)
    for filename in ('LICENSE', 'COPYING'):
        path = ROOT / '.deps/kos' / filename
        if path.exists():
            files['licenses/KallistiOS-' + filename] = path.read_bytes()
    sheet = io.StringIO(newline='')
    writer = csv.writer(sheet)
    writer.writerow(('profile', 'folder', 'module_build', 'purpose', 'engagement', 'prefetch', 'virtual_vmu_supported'))
    for name, info in profiles.items():
        writer.writerow((name, info['folder'], info['modules'][0]['build_id'], info['purpose'],
                         info['engagement'], 'Off', info['virtual_vmu_supported']))
    files['profile-map.csv'] = sheet.getvalue().encode()
    manifest = dict(schema='kui.performance.evidence-suite.v1', source_commit=commit,
        source_tree=git('rev-parse', 'HEAD^{tree}').decode().strip(), base_main=BASE,
        source_dirty=False, console_tested=False, profiles=profiles,
        planned_runs=len(cases),
        gui={str(n): dict(build_id=verify(b)['build'], sha256=sha(b)) for n, b in menus.items()},
        install='Merge 00-control/KUI once; then copy one selected numbered folder/KUI with console powered off',
        save_images_included=False, game_images_included=False,
        evidence_scope='Host correctness, linked capacity/instructions/stack and archive checks; no console performance claim')
    files['build.json'] = wire(manifest)
    files['SHA256SUMS'] = ''.join(sha(data) + '  ' + name + '\n' for name, data in sorted(files.items())).encode()
    if git('status', '--porcelain').strip() or git('rev-parse', 'HEAD').decode().strip() != commit:
        raise ValueError('Source changed during packaging')
    output = (args.output or ROOT.parent / ('K-UI-Performance-Evidence-' + commit[:12] + '-TEST.zip')).resolve()
    if output.is_relative_to(ROOT):
        raise ValueError('Save the ZIP outside the source checkout')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix='.performance-', suffix='.zip', dir=output.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() or len(archive.namelist()) != len(files) or \
               any(archive.read(n) != b for n, b in files.items()):
                raise ValueError('ZIP round-trip verification failed')
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    print(json.dumps(dict(path=str(output), bytes=output.stat().st_size,
                          sha256=sha(output.read_bytes()), source_commit=commit,
                          profiles=len(profiles), console_tested=False), indent=2))


if __name__ == '__main__':
    main()
