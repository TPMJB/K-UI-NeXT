#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the shared-core VMU test as a small, verified copy-over KUI tree.

Source/history live in the named Git commit. The ZIP contains install files,
the test guide and explicit validation evidence, without private game inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from package_native_compat_test import background_module, standalone_module
from package_physical_disc_test import collect_vmu_build, provenance
from runtime_package import envelope, flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]


def require(ok, message):
    if not ok:
        raise ValueError(message)


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def package(args):
    require(not git('status', '--porcelain'), 'Commit source changes before packaging')
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    build = commit[:12]
    gui_elf = args.gui_elf.read_bytes()
    provenance(args.gui_elf.with_suffix('.compile.json'), gui_elf, commit, True)
    payload, memory = flatten_elf(gui_elf)
    for directory in (args.background_build, args.background_a1_build, args.standalone_build):
        provenance(directory/'entry.compile.json', (directory/'entry.elf').read_bytes(), commit, True)
    disc, _ = collect_vmu_build(args.disc_vmu_build, build, commit)
    files = {
        'KUI/runtime.kui': envelope(payload, memory, build),
        'KUI/apps/games/retail-native-dual.kui': background_module(args.background_build, build, 'A2'),
        'KUI/apps/games/retail-native-dual-a1.kui': background_module(args.background_a1_build, build, 'A1'),
        'KUI/apps/games/retail-native-standalone.kui': standalone_module(args.standalone_build, build),
        'KUI/apps/disc/native-disc-vmu.kui': disc,
    }
    for data in files.values():
        require(verify(data)['build'] == build, 'Install component build differs from source')
    installed = {name: dict(bytes=len(data), sha256=sha(data)) for name, data in files.items()}
    files['README-FIRST.md'] = (ROOT/'docs/universal-vmu-test.md').read_bytes().replace(b'@BUILD@', build.encode())
    files['details/REVIEW.md'] = (ROOT/'docs/universal-vmu-review-2026-11-01.md').read_bytes()
    files['LICENSE'] = (ROOT/'LICENSE').read_bytes()
    files['THIRD_PARTY.md'] = (ROOT/'THIRD_PARTY.md').read_bytes()
    for source in args.evidence:
        require(source.is_file() and not source.is_symlink(), 'Evidence must be an ordinary file')
        name = 'details/evidence/'+source.name
        require(name not in files, 'Duplicate evidence filename')
        files[name] = source.read_bytes()
    files['details/BUILD.json'] = (json.dumps(dict(schema='kui.universal-vmu.test.v1',
        source_commit=commit, source_tree=tree, build=build,
        source_url='https://github.com/TPMJB/K-UI-NeXT/tree/'+commit,
        install_files=installed, console_execution_verified=False), indent=2)+'\n').encode()
    files['SHA256SUMS.txt'] = ''.join(sha(data)+'  '+name+'\n' for name, data in sorted(files.items())).encode()
    output = args.output.resolve()
    require(not output.is_relative_to(ROOT), 'Save the ZIP outside the source checkout')
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.universal-vmu-', suffix='.zip', dir=output.parent)
    os.close(fd)
    temporary = Path(name)
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(temporary) as archive:
            require(set(archive.namelist()) == set(files) and
                    all(archive.read(name) == data for name, data in files.items()),
                    'ZIP byte verification failed')
        require(git('rev-parse', 'HEAD') == commit and not git('status', '--porcelain'),
                'Source changed while packaging')
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    print(json.dumps(dict(zip=str(output), bytes=output.stat().st_size,
        sha256=sha(output.read_bytes()), build=build, install_files=installed), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('gui-elf', 'background-build', 'background-a1-build', 'standalone-build', 'disc-vmu-build', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    args = parser.parse_args()
    try:
        package(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'Universal VMU test package refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
