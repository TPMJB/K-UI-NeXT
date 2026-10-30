#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package a native compatibility test: GUI, compact Background and standalone New.

The three runtimes must carry this clean checkpoint's build identifier. Audit
reports and host logs are attached as evidence; the package records which
checks ran and which did not, and makes no console claim.
"""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_vmu_native_worker_gateway import read_elf
from install_native_compat_test import SCHEMA, TARGETS
from native_runtime_layout import BACKGROUND_COMPACT, layout_for
from package_native_vmu_only import checkout_script
from package_vmu_native_dual_delivery import offline_source_recovery_check
from runtime_package import verify

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = {
    'background': ('stage.elf', 'stage.map', 'native-worker.elf', 'native-worker.map', 'native-worker.reloc',
                   'resident-sci.elf', 'resident-sci.map', 'resident-legacy.elf', 'resident-legacy.map',
                   'native-ra-aux.elf', 'native-ra-aux.map', 'entry.elf', 'entry.map', 'entry.compile.json'),
    'standalone': ('stage.elf', 'stage.map', 'resident-sci.elf', 'resident-sci.map',
                   'entry.elf', 'entry.map', 'entry.compile.json'),
}


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2, sort_keys=True)+'\n').encode()


def git(*arguments):
    return subprocess.check_output(['git', *arguments], cwd=ROOT, text=True).strip()


def runtime(path, build):
    data = Path(path).read_bytes()
    require(verify(data)['build'] == build, 'Runtime build differs from this checkpoint: '+str(path))
    return data


def background_module(build, revision):
    module = runtime(build/'retail-native-background-compact.kui', revision)
    require((build/'retail-native-dual.kui').read_bytes() == module,
            'Compact Background dual-reader alias differs')
    stage, worker = read_elf(build/'stage.elf'), read_elf(build/'native-worker.elf')
    require(layout_for(worker[4]) is BACKGROUND_COMPACT and layout_for(stage[4]) is BACKGROUND_COMPACT,
            'Background build is not the exact compact 284 KiB profile')
    return module


def standalone_module(build, revision):
    module = runtime(build/'retail-native-standalone.kui', revision)
    stage = read_elf(build/'stage.elf')
    require(stage[4].get('__kui_native_standalone_reader_stage') == (1, 0, 0, 0xfff1),
            'Standalone build lacks its reader-only stage marker')
    return module


def test_sheet():
    stream = io.StringIO(newline='')
    writer = csv.writer(stream)
    writer.writerow(('game', 'reader', 'virtual_vmu', 'boots', 'gameplay', 'loading_stutter',
                     'memory_card_seen', 'save', 'power_cycle_load', 'failure_screen', 'notes'))
    for row in (('Sonic Adventure', 'Background New (X)', 'standalone, none'),
                ('The House of the Dead 2', 'Background New (X)', 'standalone, none'),
                ('Time Stalkers', 'Background New (X)', 'standalone, none'),
                ('Elemental Gimmick Gear', 'Background New (X)', 'standalone, none'),
                ('Sword of the Berserk', 'Background New (X)', 'On'),
                ('Crazy Taxi', 'Background New (X)', 'standalone, none'),
                ('Zombie Revenge', 'Background New (X)', 'standalone, none'),
                ('Regression: a game that already worked', 'Background New (X)', 'On')):
        writer.writerow((*row, '', '', '', '', '', '', '', ''))
    return stream.getvalue().encode()


def package(background_build, standalone_build, gui, output=None, evidence=(), verification=None):
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    require(not git('status', '--porcelain'), 'Commit source changes before packaging')
    revision = commit[:12]
    background_build = Path(background_build).resolve(strict=True)
    standalone_build = Path(standalone_build).resolve(strict=True)
    modules = {'gui': runtime(gui, revision), 'background': background_module(background_build, revision),
               'standalone': standalone_module(standalone_build, revision)}
    paths = {'gui': 'gui/runtime.kui', 'background': 'modules/retail-native-dual.kui',
             'standalone': 'modules/retail-native-standalone.kui'}
    require(set(modules) == set(TARGETS), 'Package components differ from the installer targets')
    files = {paths[name]: data for name, data in modules.items()}
    metadata = dict(schema=SCHEMA, source_commit=commit, build=revision,
                    files={name: dict(path=paths[name], sha256=sha(data)) for name, data in modules.items()})
    files['compat-test.json'] = wire(metadata)
    for name, build in (('background', background_build), ('standalone', standalone_build)):
        for artifact in ARTIFACTS[name]:
            files['artifacts/'+name+'/'+artifact] = (build/artifact).read_bytes()
    for source in evidence:
        source = Path(source).resolve(strict=True)
        require(source.is_file() and not source.is_symlink(), 'Evidence must be an ordinary file')
        target = 'evidence/'+source.name
        require(target not in files, 'Duplicate evidence filename: '+source.name)
        files[target] = source.read_bytes()
    for name, source in (('install.py', 'tools/install_native_compat_test.py'),
                         ('README-FIRST.md', 'docs/native-compat-test.md'),
                         ('ZR-FAMILY.md', 'docs/native-zr-family.md'),
                         ('LICENSE', 'LICENSE'), ('THIRD_PARTY.md', 'THIRD_PARTY.md')):
        files[name] = (ROOT/source).read_bytes()
    files['test-results.csv'] = test_sheet()
    candidate = dict(schema='kui.native.compat-test.candidate.v1', source_commit=commit, source_tree=tree,
                     build=revision, console_execution_verified=False,
                     targets={name: dict(destination=str(TARGETS[name]), sha256=sha(data))
                              for name, data in modules.items()},
                     verification=verification or {})
    with tempfile.TemporaryDirectory(prefix='kui-compat-test-source-') as temporary:
        bundle = Path(temporary)/'native-source-full.bundle'
        subprocess.run(['git', 'bundle', 'create', str(bundle), 'HEAD', '--all'], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        subprocess.run(['git', 'bundle', 'verify', str(bundle)], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        bundle_data = bundle.read_bytes()
    candidate['source_full_bundle'] = dict(path='source/native-source-full.bundle', bytes=len(bundle_data),
                                           sha256=sha(bundle_data), full_history=True, prerequisites=[])
    script = checkout_script()
    candidate['offline_source_recovery'] = offline_source_recovery_check(candidate, script, bundle_data)
    require(git('rev-parse', 'HEAD') == commit and not git('status', '--porcelain'), 'Source changed while packaging')
    files['source/native-source-full.bundle'] = bundle_data
    files['source/checkout-source.py'] = script
    files['CANDIDATE.json'] = wire(candidate)
    files['SHA256SUMS.txt'] = ''.join(sha(data)+'  '+name+'\n' for name, data in sorted(files.items())).encode()
    output = Path(output or ROOT.parent/'delivery'/('K-UI-Compat-Test-'+revision+'.zip')).resolve()
    require(not output.is_relative_to(ROOT), 'Save the package outside the source checkout')
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.compat-test-', suffix='.zip', dir=output.parent)
    os.close(fd)
    temporary = Path(temporary)
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(temporary) as archive:
            require(set(archive.namelist()) == set(files) and
                    all(archive.read(name) == data for name, data in files.items()),
                    'ZIP round-trip byte verification failed')
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    result = dict(zip=str(output), bytes=output.stat().st_size, sha256=sha(output.read_bytes()),
                  source_commit=commit, targets=candidate['targets'], console_execution_verified=False)
    print(json.dumps(result, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--background-build', type=Path, required=True)
    parser.add_argument('--standalone-build', type=Path, required=True)
    parser.add_argument('--gui', type=Path, required=True, help='runtime.kui built from this checkpoint')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    parser.add_argument('--verification', type=Path, help='JSON summary of the checks that ran')
    args = parser.parse_args()
    try:
        verification = json.loads(args.verification.read_text()) if args.verification else None
        package(args.background_build, args.standalone_build, args.gui, args.output, args.evidence, verification)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'Compatibility test package refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
