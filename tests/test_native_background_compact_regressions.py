#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host-only compact Background regression and virtual-card routing matrix.

The focused compact fixture exercises the exact new worker/lease in both
runtime modes. Existing engine, loan and selector fixtures remain controls;
their passing does not establish console performance or game compatibility.
No SH compiler or target-native build is invoked by this runner.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run_group(name, commands, directory, environment):
    log = []
    passed = True
    for command in commands:
        log.append('$ ' + shlex.join(command) + '\n')
        run = subprocess.run(command, cwd=ROOT, env=environment,
                             text=True, capture_output=True)
        log.extend((run.stdout, run.stderr, 'exit=' + str(run.returncode) + '\n'))
        if run.returncode:
            passed = False
            break
    (directory / (name + '.log')).write_text(''.join(log))
    return dict(name=name, passed=passed)


def groups(directory, environment):
    python = sys.executable
    make = ['make', '--no-print-directory', 'CC=cc', 'HOST_CC=cc']

    def py(filename):
        return [python, 'tests/' + filename]

    def target(makefile, name, *targets):
        build = directory / name
        build.mkdir(exist_ok=True)
        return [*make, '-f', makefile, 'BUILD=' + str(build), *targets]

    codec = directory / 'card-route-codecs'
    codec.mkdir(exist_ok=True)
    profile = codec / 'template.bin'
    common = ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
              '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
              '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-Iinclude', '-I.deps/fatfs/source']
    compact_flags = common[1:] + ['-ffunction-sections', '-fdata-sections', '-Isrc/loader'] + [
        '-D' + name + '=1' for name in (
            'KUI_NATIVE_BACKGROUND_COMPACT', 'KUI_NATIVE_DUAL_READERS', 'KUI_NATIVE_READAHEAD',
            'KUI_VMU_NATIVE_BACKGROUND', 'KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION',
            'KUI_NATIVE_SPLIT_CONTROLLER_GENERAL', 'KUI_NATIVE_SPLIT_CONTROLLER_PREFETCH',
            'KUI_NATIVE_SPLIT_CONTROLLER_STREAM_GUARD', 'KUI_NATIVE_GENERAL_LEAN_LAYOUT',
            'KUI_VMU_NATIVE_CACHED_CODE', 'KUI_VMU_NATIVE_PERSISTENT_PROBE',
            'KUI_VMU_EXPERIMENTAL', 'KUI_NATIVE_IRQ_REARM', 'KUI_NATIVE_COOP_PUMP',
            'KUI_NATIVE_REQUEST_TIMING')]
    sources = ['src/core/vmu_game_card.c', 'src/core/vmu_persist_profile.c',
               'src/core/retail_image.c', 'src/core/hash.c', 'src/core/data.c']
    codec_commands = [[python, 'tools/vmu_persist_profile.py', '--template', str(profile)]]
    for name, extra, arguments in (
            ('test_vmu_game_card', [], [str(profile)]),
            ('test_vmu_game_assign', ['src/core/vmu_game_assign.c'], []),
            ('test_games_vmu_card', [], [str(profile)])):
        binary = codec / name
        codec_commands.extend(([*common, 'tests/' + name + '.c', *extra, *sources,
                                '-o', str(binary)], [str(binary), *arguments]))
    return (
        ('compact-on-off', [py('test_native_background_compact.py')]),
        ('actual-reader-cache-aux', [target('Makefile.vmu-native-readahead', 'reader',
            'readahead-cache-host-tests', 'readahead-aux-host-tests', 'readahead-low-host-tests')]),
        ('compact-reader-cache-aux', [target('Makefile.vmu-native-background-compact', 'compact-reader',
            'PASSIVE_HOST_FLAGS=' + shlex.join(compact_flags), 'readahead-cache-host-tests',
            'readahead-aux-host-tests', 'readahead-low-host-tests')]),
        ('legacy-split-prefetch', [target('Makefile.vmu-native-split-controller-prefetch',
            'split', 'split-controller-prefetch-host-tests')]),
        ('legacy-toggle-worker', [py('test_vmu_native_toggle.py')]),
        ('legacy-dual-selectors', [[*py('test_native_dual_readers.py'), '--host-only']]),
        ('actual-async-loan', [py('test_vmu_native_async_loan.py')]),
        ('background-control-bind', [py('test_vmu_native_background_gateway.py'),
            py('test_vmu_native_background_init.py'), py('test_vmu_native_background_bind.py')]),
        ('card-route-codecs', codec_commands),
        ('card-service-real-filesystems', [py('test_vmu_game_card_service.py')]),
        ('card-library-real-filesystems', [py('test_vmu_library.py')]),
        ('assignment-wizard', [py('test_vmu_wizard.py')]),
        ('game-card-status', [py('test_vmu_game_info.py')]),
        ('vm-emulation-mode', [py('test_vmu_emulation_settings.py'), py('test_vmu_native_mode.py')]),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log-dir', type=Path, help='Retain separate logs and host binaries here')
    parser.add_argument('--jobs', type=int, choices=(1, 2, 3, 4), default=3)
    args = parser.parse_args()
    environment = dict(os.environ, CC='cc', HOST_CC='cc',
                       ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    missing = [name for name in ('mkfs.fat', 'mkfs.exfat', 'fsck.fat', 'fsck.exfat')
               if not shutil.which(name, path=environment.get('PATH'))]
    if missing:
        parser.error('Real filesystem cases require these tools on PATH: ' + ', '.join(missing))
    if not (ROOT / 'tests/test_native_background_compact.py').is_file():
        parser.error('The focused compact On/Off fixture is missing; no replacement is counted as a pass')
    with tempfile.TemporaryDirectory(prefix='native-background-compact-regressions-') as temporary:
        directory = args.log_dir.resolve() if args.log_dir else Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        plan = groups(directory, environment)
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            pending = [executor.submit(run_group, name, commands, directory, environment)
                       for name, commands in plan]
            results = [future.result() for future in pending]
        report = dict(host_only=True, console_compatibility_claim=False, groups=results,
                      all_passed=all(result['passed'] for result in results))
        (directory / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
        for result in results:
            print(('PASS ' if result['passed'] else 'FAIL ') + result['name'])
            if not result['passed']:
                print((directory / (result['name'] + '.log')).read_text(), end='')
        if args.log_dir:
            print('Logs: ' + str(directory))
        return 0 if report['all_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
