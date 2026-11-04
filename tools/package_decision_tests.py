#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package five Toy/Armada decision profiles without replacing native loaders or GUI."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile

from build_native_cdda_compat import TOY
from check_native_cdda_compat import check_directory as check_toy
from check_armada_ce_excerpt import check as check_armada
from package_fmv_tests import checked_module
from runtime_package import verify
from check_decision_baselines import compare_bytes
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from check_toy_decision import check as check_toy_decision

ROOT = Path(__file__).resolve().parents[1]
BASE = '0045dd96937ee7485ecd4fe0432b28babea8646a'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def toy_observer_pair(clean, observed, clean_id, observed_id):
    """Only the initialized enable word and display identity may differ."""
    a, b = read_elf(clean / 'worker.elf'), read_elf(observed / 'worker.elf')
    shape = lambda elf: [(p[0], *p[2:]) for p in elf[2]]
    if shape(a) != shape(b):
        raise ValueError('Toy B/C worker load geometry differs')
    av, ad, _ = symbol_bytes(a, '_decision_enabled', 1)
    bv, bd, _ = symbol_bytes(b, '_decision_enabled', 1)
    if av != bv or ad != struct.pack('<I', 1) or bd != struct.pack('<I', 2):
        raise ValueError('Toy B/C enable word differs from the admitted runtime switch')
    def masked(directory, value):
        data = bytearray((directory / 'worker.bin').read_bytes())
        at = (value & 0x1fffffff) - 0x0cfd0000
        if not 0 <= at <= len(data)-4:
            raise ValueError('Toy observer switch is outside its loaded worker')
        data[at:at+4] = bytes(4)
        return bytes(data)
    worker = compare_bytes('Toy B/C identical observer code', masked(clean, av),
        masked(observed, bv), clean_id, observed_id)
    low = compare_bytes('Toy B/C unchanged low transport',
        (clean / 'resident-sci.bin').read_bytes(),
        (observed / 'resident-sci.bin').read_bytes(), clean_id, observed_id)
    return dict(worker=worker, low_transport=low, same_load_geometry=True,
        permitted_runtime_switch_address=hex(av), runtime_switch_values=[1,2])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--pristine-root', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error('--jobs must be 1..16')
    if git('status', '--porcelain').strip():
        parser.error('Commit the reviewed source before packaging')
    commit = git('rev-parse', 'HEAD').decode().strip()
    subprocess.run(['git', 'merge-base', '--is-ancestor', BASE, commit], cwd=ROOT, check=True)
    prefix = commit[:10]
    evidence = ROOT / 'build/decision-evidence'
    files, profiles = {}, {}

    def execute(command, name, cwd=ROOT):
        log = evidence / (name + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('w') as stream:
            result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise ValueError('Failed: ' + ' '.join(command) + '; inspect ' + str(log))
        files['evidence/' + log.name] = log.read_bytes()

    def add(profile, folder, target, data, **metadata):
        verified = verify(data)
        member = folder + '/KUI/apps/games/' + target
        files[member] = data
        info = profiles.setdefault(profile, dict(folder=folder, console_tested=False,
            virtual_vmu_supported=False, **metadata, modules=[]))
        info['modules'].append(dict(member=member, build_id=verified['build'],
            bytes=len(data), sha256=sha(data)))

    # Host checks exercise real collector state transitions, clock failures and
    # frozen reports. They are not console throughput or compatibility evidence.
    execute(['python3', 'tests/test_toy_decision_observer.py'], 'toy-observer-host')
    execute(['python3', 'tests/test_ce_decision_window.py'], 'ce-window-host')
    execute(['python3', 'tests/test_decision_baselines.py'], 'baseline-checker-host')

    for selected, code, folder, purpose in (
        (0, '10', '00-control', 'Preserved AP playback and diagnostic path'),
        (1, '11', '01-toy-clean', 'Remove legacy diagnostic work; AP schedule unchanged'),
        (2, '12', '02-toy-observer', 'Same clean path plus rolling decision observer')):
        build_id = prefix + code
        for cooked in (False, True):
            geometry = 'cooked' if cooked else 'raw'
            directory = ROOT / ('build/decision-toy-' + str(selected) + '-' + geometry)
            reference = ROOT / ('build/decision-toy-ref-' + str(selected) + '-' + geometry)
            flags = list(TOY) + ['BUILD_ID=' + build_id,
                'TOY_DECISION_PROFILE=' + str(selected),
                'FMV_TEST=' + str(int(selected != 0)), 'FMV_IDLE_REARM=0',
                'TOY_DEMAND_TEST_PROFILE=0', 'WORD_REVERSE_PROFILE=0']
            if cooked:
                flags += ['COOKED_DATA_PROFILE=1']
            if selected == 0:
                execute(['make', '-f', 'Makefile.toy_pilot',
                    'BUILD=build/pristine-toy-' + geometry, 'NATIVE_COMPAT=1',
                    *flags, '-j' + str(args.jobs), 'all'],
                    'pristine-toy-' + geometry, cwd=args.pristine_root)
            for compat, path in ((0, reference), (1, directory)):
                execute(['make', '-f', 'Makefile.toy_pilot',
                    'BUILD=' + str(path.relative_to(ROOT)), 'NATIVE_COMPAT=' + str(compat),
                    *flags, '-j' + str(args.jobs), 'all'],
                    'toy-' + str(selected) + '-' + geometry + '-compat' + str(compat))
            audit = check_toy(directory, 'toy-' + geometry, reference)
            files['evidence/toy-' + str(selected) + '-' + geometry + '-admission.json'] = wire(audit)
            files['evidence/toy-' + str(selected) + '-' + geometry + '-config.txt'] = (directory / 'build-config').read_bytes()
            data = checked_module(directory, 'retail-toy-pilot.kui', build_id, commit)
            add(('TA', 'TB', 'TC')[selected], folder, 'cdda-native-toy-' + geometry + '.kui', data,
                purpose=purpose, background=False, cd_music=True, prefetch=False,
                image_formats='Use existing raw OR cooked; do not compare formats',
                gameplay_schedule_changed=False, diagnostic_profile=selected)
        print('Built Toy ' + ('TA', 'TB', 'TC')[selected], flush=True)

    for geometry in ('raw', 'cooked'):
        clean = ROOT / ('build/decision-toy-1-' + geometry)
        observed = ROOT / ('build/decision-toy-2-' + geometry)
        proof = dict(binary_pair=toy_observer_pair(clean, observed, prefix+'11', prefix+'12'),
            profile_and_stack=check_toy_decision(clean, observed))
        files['evidence/toy-' + geometry + '-observer-pair.json'] = wire(proof)

    for selected, code, profile, folder in (
        (0, '20', 'CA', '00-control'), (3, '21', 'CB', '03-armada-observer')):
        directory = ROOT / ('build/armada-ce-stream' if selected == 0 else 'build/performance-armada-3')
        build_id = prefix + code
        if selected == 0:
            execute(['make', '-f', 'Makefile.armada-stream', 'DEMAND_PROFILE=0',
                'FMV_TEST=0', 'FMV_MUTE=0', 'BUILD_ID=' + build_id,
                '-j' + str(args.jobs), 'build/armada-ce-stream/entry.elf'],
                'pristine-armada', cwd=args.pristine_root)
        execute(['make', '-f', 'Makefile.armada-stream', 'DEMAND_PROFILE=' + str(selected),
            'FMV_TEST=0', 'FMV_MUTE=0', 'BUILD_ID=' + build_id, '-j' + str(args.jobs),
            str(directory.relative_to(ROOT) / 'entry.elf')], 'armada-' + profile)
        audit = check_armada(directory, stream=True)
        files['evidence/armada-' + profile + '-admission.json'] = wire(audit)
        if selected == 3:
            execute(['python3', 'tools/check_ce_demand_profile.py', str(directory),
                '--profile', '3', '--self-test'], 'armada-linked-observer')
        data = checked_module(directory, 'ce-cdda-armada.kui', build_id, commit)
        add(profile, folder, 'ce-cdda-armada.kui', data,
            purpose='Preserved CE path' if selected == 0 else 'Recent-window DATA/interrupt observation',
            background=True, cd_music=True, prefetch=False,
            gameplay_schedule_changed=False, demand_profile=selected,
            native_sound_sysintr_measurement=False)
        print('Built Armada ' + profile, flush=True)

    execute(['python3', 'tools/check_decision_baselines.py',
        '--pristine-root', str(args.pristine_root.resolve()),
        '--candidate-root', str(ROOT),
        '--output', str(evidence / 'baseline-proof.json')],
        'baseline-proof')
    files['evidence/baseline-proof.json'] = (evidence / 'baseline-proof.json').read_bytes()

    files['README.md'] = (ROOT / 'docs/decision-tests.md').read_bytes()
    files['source/K-UI-Decision-' + commit[:12] + '.tar.gz'] = gzip.compress(
        git('archive', '--format=tar', '--prefix=K-UI-Decision/', commit), mtime=0)
    files['source/changes-from-withdrawn-suite.patch'] = git('diff', 'dfd33ffc4843', commit)
    files['build.json'] = wire(dict(source_commit=commit, source_tree=git('rev-parse', 'HEAD^{tree}').decode().strip(),
        source_dirty=False, console_tested=False, initial_console_runs=5,
        accepted_pre_test_source=BASE,
        withdrawn_package='K-UI-Performance-Evidence-dfd33ffc4843-TEST.zip',
        profiles=profiles,
        native_loader_files_replaced=False, menu_replaced=False,
        compiler=subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0]))
    allowed={'cdda-native-toy-raw.kui','cdda-native-toy-cooked.kui','ce-cdda-armada.kui'}
    installed=[name for name in files if '/KUI/' in name]
    if any('/KUI/apps/games/' not in name or name.rsplit('/',1)[1] not in allowed
           for name in installed) or set(profiles)!={'TA','TB','TC','CA','CB'}:
        raise ValueError('Decision package must contain only its five Toy/Armada profiles')
    files['SHA256SUMS'] = ''.join(sha(value) + '  ' + name + '\n'
        for name, value in sorted(files.items())).encode()
    if git('status', '--porcelain').strip():
        raise ValueError('Source changed during packaging')
    output = args.output or ROOT.parent / ('K-UI-Decision-Tests-' + commit[:12] + '.zip')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, value in sorted(files.items()):
            archive.writestr(name, value)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None or any(archive.read(k) != v for k, v in files.items()):
            raise ValueError('ZIP roundtrip validation failed')
    print(json.dumps(dict(path=str(output.resolve()), bytes=output.stat().st_size,
        sha256=sha(output.read_bytes()), source_commit=commit), indent=2))


if __name__ == '__main__':
    main()

