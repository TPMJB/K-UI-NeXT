#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and package isolated Toy/Armada FMV console comparisons from a clean commit."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

from check_native_cdda_compat import check_directory as check_toy
from check_armada_ce_excerpt import check as check_armada
from runtime_package import envelope, flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
TOY_FLAGS = ['PRIVATE_P2=1', 'LOADER_TRACE=1', 'DATA_PROBE=1',
             'DATA_SCHEDULE_MODE=2', 'DATA_BUDGET_PROFILE=4',
             'AD_FOLLOWUP_PROFILE=2', 'DATA_TUNING_PROFILE=1',
             'STARTUP_IRQ_PROFILE=1', 'DATA_PIPELINE_PROFILE=2',
             'WORD_REVERSE_PROFILE=1', 'FMV_TEST=1']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()


def run(command, log):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('w') as output:
        result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError('Failed: ' + ' '.join(command) + '; inspect ' + str(log))


def checked_module(directory, name, build_id, commit):
    elf = directory / 'entry.elf'
    record = json.loads(elf.with_suffix('.compile.json').read_text())
    if record != {'commit': commit, 'source_dirty': False, 'elf_sha256': sha(elf.read_bytes())}:
        raise ValueError('Unclean or mismatched compilation: ' + str(directory))
    payload, memory = flatten_elf(elf.read_bytes())
    data = envelope(payload, memory, build_id)
    info = verify(data)
    if info['build'] != build_id or build_id.encode() + b'\0' not in payload:
        raise ValueError('Envelope/display build mismatch: ' + str(directory))
    (directory / name).write_bytes(data)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if git('status', '--porcelain').strip():
        raise SystemExit('Commit the intended source before packaging console tests')
    commit = git('rev-parse', 'HEAD').decode().strip()
    stamp = commit[:12]
    build = ROOT / 'build/fmv-evidence'
    files = {}
    run(['python3', 'tools/test_fmv_controls.py'], build / 'host-controls.log')
    files['evidence/host-controls.log'] = (build / 'host-controls.log').read_bytes()
    # Start the GUI from clean objects so no cached object retains an earlier build ID.
    shutil.rmtree(ROOT / 'build/dc', ignore_errors=True)
    run(['make', '-f', 'Makefile.dc', 'KUI_FMV_TEST=1', 'BUILD_ID=' + stamp,
         '-j4', 'build/kui-runtime.elf'], build / 'menu-build.log')
    payload, memory = flatten_elf((ROOT / 'build/kui-runtime.elf').read_bytes())
    menu = envelope(payload, memory, stamp)
    verify(menu)
    files['01-control/KUI/runtime.kui'] = menu
    profiles = {}
    for candidate in (0, 1):
        build_id = commit[:10] + ('c1' if candidate else 'c0')
        for cooked in (False, True):
            profile = 'toy-cooked' if cooked else 'toy-raw'
            title = 'toy-' + ('candidate' if candidate else 'control') + ('-cooked' if cooked else '-raw')
            directory = ROOT / ('build/fmv-' + title)
            reference = ROOT / ('build/fmv-reference-' + title)
            flags = TOY_FLAGS + ['FMV_IDLE_REARM=' + str(candidate), 'BUILD_ID=' + build_id]
            if cooked:
                flags += ['COOKED_DATA_PROFILE=1']
            for selected, target in ((0, reference), (1, directory)):
                run(['make', '-f', 'Makefile.toy_pilot', 'BUILD=' + str(target.relative_to(ROOT)),
                     'NATIVE_COMPAT=' + str(selected), *flags, '-j1', 'all'],
                    build / (title + ('-reference' if not selected else '') + '.log'))
            proof = check_toy(directory, profile, reference)
            data = checked_module(directory, 'retail-toy-pilot.kui', build_id, commit)
            folder = '02-toy-idle-background' if candidate else '01-control'
            member = folder + '/KUI/apps/games/cdda-native-' + profile + '.kui'
            files[member] = data
            files['evidence/' + title + '.json'] = json_bytes(proof)
            files['evidence/' + title + '-config.txt'] = (directory / 'build-config').read_bytes()
            profiles[title] = {'member': member, 'build_id': build_id,
                               'sha256': sha(data), 'console_tested': False}
            print('Built and audited ' + title, flush=True)
    for muted in (0, 1):
        build_id = commit[:10] + ('a1' if muted else 'a0')
        directory = ROOT / ('build/fmv-armada-' + str(muted))
        shutil.rmtree(directory, ignore_errors=True)
        run(['make', '-f', 'Makefile.armada-stream', 'FMV_TEST=1', 'FMV_MUTE=' + str(muted),
             'BUILD_ID=' + build_id, '-j1', str(directory.relative_to(ROOT) / 'entry.elf')],
            build / ('armada-' + str(muted) + '.log'))
        proof = check_armada(directory, stream=True)
        data = checked_module(directory, 'ce-cdda-armada.kui', build_id, commit)
        folder = '03-armada-cdda-silent' if muted else '01-control'
        member = folder + '/KUI/apps/games/ce-cdda-armada.kui'
        files[member] = data
        files['evidence/armada-' + str(muted) + '.json'] = json_bytes(proof)
        profiles['armada-' + ('silent' if muted else 'control')] = {
            'member': member, 'build_id': build_id, 'sha256': sha(data), 'console_tested': False}
        print('Built and audited Armada ' + ('silent' if muted else 'control'), flush=True)
    # The candidate changes high-level ownership only; low transport bytes
    # must be identical apart from their explicit display/build identifier.
    parity = {}
    for cooked in ('raw', 'cooked'):
        a = (ROOT / ('build/fmv-toy-control-' + cooked) / 'resident-sci.bin').read_bytes()
        b = (ROOT / ('build/fmv-toy-candidate-' + cooked) / 'resident-sci.bin').read_bytes()
        a = a.replace((commit[:10] + 'c0').encode(), b'000000000000')
        b = b.replace((commit[:10] + 'c1').encode(), b'000000000000')
        if a != b:
            raise ValueError('Toy low reader changed beyond build ID')
        parity['toy-' + cooked] = sha(a)
    for transport in ('scif', 'sci', 'ide', 'scia'):
        a = (ROOT / 'build/fmv-armada-0' / ('resident-' + transport + '.bin')).read_bytes()
        b = (ROOT / 'build/fmv-armada-1' / ('resident-' + transport + '.bin')).read_bytes()
        a = a.replace((commit[:10] + 'a0').encode(), b'000000000000')
        b = b.replace((commit[:10] + 'a1').encode(), b'000000000000')
        if a != b:
            raise ValueError('Armada low reader changed beyond build ID: ' + transport)
        parity['armada-' + transport] = sha(a)
    files['README.md'] = (ROOT / 'docs/fmv-toy-armada-tests.md').read_bytes()
    files['test-results.csv'] = b'run,game,image_format,video,audio,report_recording,rearm_count,notes\n'
    files['source/K-UI-FMV-' + stamp + '.tar.gz'] = gzip.compress(
        git('archive', '--format=tar', '--prefix=K-UI-FMV/', commit), mtime=0)
    for name in ('LICENSE', 'COPYING'):
        path = ROOT / '.deps/kos' / name
        if path.is_file():
            files['licenses/KallistiOS-' + name] = path.read_bytes()
    files['build.json'] = json_bytes({'source_commit': commit,
        'source_tree': git('rev-parse', 'HEAD^{tree}').decode().strip(),
        'source_dirty': False, 'hardware_tested': False, 'profiles': profiles,
        'menu': {'build_id': stamp, 'sha256': sha(menu)},
        'low_reader_parity_after_build_id_normalization': parity,
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'base_main': '0045dd96937ee7485ecd4fe0432b28babea8646a'})
    files['SHA256SUMS'] = ''.join(sha(data) + '  ' + name + '\n'
        for name, data in sorted(files.items())).encode()
    if git('status', '--porcelain').strip():
        raise ValueError('Source changed during build')
    output = args.output or ROOT.parent / ('K-UI-FMV-Toy-Armada-' + stamp + '-TEST.zip')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('Archive CRC validation failed')
        for name, data in files.items():
            if archive.read(name) != data:
                raise ValueError('Archive content mismatch: ' + name)
    print(json.dumps({'path': str(output.resolve()), 'bytes': output.stat().st_size,
                      'sha256': sha(output.read_bytes()), 'commit': commit}, indent=2))


if __name__ == '__main__':
    main()
