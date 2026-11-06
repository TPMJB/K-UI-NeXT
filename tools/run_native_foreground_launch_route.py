#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test the GUI-used Standard route helpers against a real foreground package.

This runs production manifest/classifier/path helpers, not the FatFs Games
frontend, target stage or hardware. The ordinary-physical positive is an
explicitly authored descriptor fixture; the foreground positive is untouched
payload from the supplied runtime envelope, validated with production C.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('tests/test_native_foreground_launch_route.c',
           'src/core/runtime_image.c', 'src/core/data.c')
FLAGS = ('-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
         '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
         '-fno-sanitize-recover=all', '-DKUI_VMU_GAME_CARDS=1', '-Iinclude')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, env=None):
    return subprocess.run(command, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def fatfs_path(explicit):
    if explicit:
        return explicit.resolve()
    local = ROOT / '.deps/fatfs/source'
    if (local / 'ff.h').is_file():
        return local
    common = run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'])
    if common.returncode:
        raise RuntimeError('Cannot locate the repository FatFs headers')
    return Path(common.stdout.strip()).parent / '.deps/fatfs/source'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', type=Path, required=True,
                        help='Actual foreground runtime envelope (.kui)')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/foreground-launch-route-host')
    parser.add_argument('--fatfs', type=Path, help='Existing FatFs header directory')
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    module = args.module.resolve(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    compiler_name = shutil.which(args.cc)
    if not compiler_name:
        raise RuntimeError('Compiler not found: ' + args.cc)
    compiler = Path(compiler_name).resolve(); fatfs = fatfs_path(args.fatfs)
    if not (fatfs / 'ff.h').is_file():
        raise RuntimeError('FatFs headers missing; supply --fatfs')
    flags = [*FLAGS, '-I' + str(fatfs)]
    gui = (ROOT / 'src/apps/games_retail.c').read_text()
    required = ('#include "kui/native_standard_route.h"',
                'kui_native_standard_package(emulation_enabled)',
                'kui_native_standard_module_find(',
                'kui_native_foreground_manifest_valid(map)')
    if not all(fragment in gui for fragment in required):
        raise RuntimeError('The Games frontend no longer calls the tested production helpers')
    dependencies = run([str(compiler), *flags, '-MM', *SOURCES])
    if dependencies.returncode:
        raise RuntimeError(dependencies.stdout)
    paths = {ROOT / name for name in SOURCES}
    paths.update((Path(__file__).resolve(), ROOT / 'src/apps/games_retail.c'))
    for line in dependencies.stdout.replace('\\\n', ' ').splitlines():
        if ':' in line:
            paths.update((ROOT / name).resolve() for name in shlex.split(line.split(':', 1)[1]))
    bound = {str(p.resolve()): sha(p) for p in sorted(paths)}
    module_hash = sha(module); compiler_hash = sha(compiler)
    receipt = {'schema': 'kui.native.foreground-launch-route-host.v1', 'status': 'FAIL',
               'source_root': str(ROOT), 'source_sha256': bound,
               'module': str(module), 'module_sha256': module_hash,
               'compiler': {'path': str(compiler), 'sha256': compiler_hash},
               'frontend_helper_call_sites_checked': list(required), 'modes': {},
               'limits': ['The actual foreground envelope header and payload CRC are checked by production C.',
                          'The classifier and path/manifest helpers are the same functions referenced by Games.',
                          'The ordinary PHYSICAL positive is an authored descriptor fixture, not a linked physical module.',
                          'Full FatFs Games frontend, target SH code and hardware are not executed.'],
               'full_frontend_executed': False, 'hardware_executed': False}
    logs = []
    try:
        for mode, sanitize in (('ubsan', 'undefined'), ('asan-ubsan', 'address,undefined')):
            exe = out / ('test-foreground-launch-route-' + mode)
            command = [str(compiler), *flags, '-fsanitize=' + sanitize,
                       *SOURCES, '-o', str(exe)]
            built = run(command); logs.append(mode + ' compile\n' + built.stdout)
            if built.returncode:
                raise RuntimeError('Compile failed:\n' + built.stdout)
            ran = run([str(exe), str(module)], dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
                                                   UBSAN_OPTIONS='halt_on_error=1'))
            logs.append(mode + ' execute\n' + ran.stdout)
            if ran.returncode:
                raise RuntimeError('Route test failed:\n' + ran.stdout)
            prefix = 'FOREGROUND_LAUNCH_ROUTE_JSON '
            rows = [line[len(prefix):] for line in ran.stdout.splitlines() if line.startswith(prefix)]
            if len(rows) != 1:
                raise RuntimeError('Missing unique actual test result')
            result = json.loads(rows[0])
            if not (result['checks'] > 0 and result['foreground_field_mutations'] > 0 and
                    result['manifest_refusals'] > 0 and result['actual_foreground_envelope'] and
                    not result['full_frontend_executed'] and not result['hardware_executed']):
                raise RuntimeError('Incomplete or overstated route test result')
            receipt['modes'][mode] = {'result': result, 'command': command,
                                     'execution_command': [str(exe), str(module)],
                                     'executable_sha256': sha(exe)}
        stable = bound == {name: sha(name) for name in bound}
        receipt['source_hashes_unchanged_after_tests'] = stable
        receipt['module_hash_unchanged_after_tests'] = sha(module) == module_hash
        if not stable or sha(module) != module_hash or sha(compiler) != compiler_hash:
            raise RuntimeError('Source, module or compiler changed during tests')
        if receipt['modes']['ubsan']['result'] != receipt['modes']['asan-ubsan']['result']:
            raise RuntimeError('Sanitizer modes exercised different results')
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        (out / 'foreground-launch-route.log').write_text('\n'.join(logs))
        (out / 'foreground-launch-route.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS foreground Standard launch route:', {
        mode: row['result'] for mode, row in receipt['modes'].items()})


if __name__ == '__main__':
    main()
