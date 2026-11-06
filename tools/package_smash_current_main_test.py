#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver only a GUI test .kup using Main's released 26-role wire profile.

The GUI's own optional-role catalog is not a package compatibility profile.
Keep the accepted runtime unchanged when repairing only its update container.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from runtime_package import envelope, flatten_elf, verify as verify_runtime
from update_package import INHERIT_ROOT, build_package, package_paths, verify

ROOT = Path(__file__).resolve().parents[1]
MAIN = '758f62c43d76ce4c8550be5789874313a9a5551d'
PROFILE = 'resources/updater-bindings/update-main26-catalog.inc'
PROFILE_SHA256 = 'a9d6b3bb7cc1ebc9259a697435d6b492cb590db2c82b60a92294f219b225e8ba'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def regular(path):
    path = Path(path).absolute()
    if path.is_symlink() or not path.is_file() or any(p.is_symlink() for p in path.parents):
        raise ValueError('Use a regular file: ' + path.name)
    return path


def main_profile():
    data = (ROOT / PROFILE).read_bytes()
    if sha(data) != PROFILE_SHA256:
        raise ValueError('Released Main package profile changed; requalify its consumers')
    paths = []
    for line in data.decode('ascii').splitlines():
        match = re.fullmatch(r'KUI_UPDATE_MEMBER\("([a-z0-9/.-]+)"\)', line)
        if not match:
            raise ValueError('Invalid Main package profile')
        paths.append(match[1])
    if len(paths) != 26:
        raise ValueError('Released package profile must contain 26 roles')
    return package_paths(paths)


def build(runtime, output, runtime_elf):
    runtime, elf = regular(runtime), regular(runtime_elf)
    output = Path(output).absolute()
    if output.suffix != '.kup':
        raise ValueError('Deliver the .kup directly; installation kits are not required')
    if output.exists() or output.is_symlink() or any(p.is_symlink() for p in output.parents):
        raise ValueError('Refusing to replace an existing delivery or use linked output paths')
    data = runtime.read_bytes()
    info = verify_runtime(data)
    elf_data = elf.read_bytes()
    payload, memory = flatten_elf(elf_data)
    if envelope(payload, memory, info['build']) != data:
        raise ValueError('Runtime bytes differ from the linked ELF')
    compiled = json.loads(regular(elf.with_suffix('.compile.json')).read_text())
    commit = compiled.get('commit', '')
    if (not re.fullmatch(r'[0-9a-f]{40}', commit) or commit[:12] != info['build'] or
            compiled.get('source_dirty') is not False or compiled.get('elf_sha256') != sha(elf_data)):
        raise ValueError('Runtime lacks matching clean-source compile provenance')
    paths = main_profile()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='kui-test-update-') as temporary:
        staged = Path(temporary) / 'KUI'
        staged.mkdir()
        (staged / 'runtime.kui').write_bytes(data)
        package_file = Path(temporary) / 'test.kup'
        package = build_package(staged, package_file, info['build'], 'testing',
                                inherit_missing=True, paths=paths)
        if verify(package_file) != package:
            raise ValueError('Produced update failed verification')
        if (len(package['members']) != 26 or package['members'][0]['policy'] != 0 or
                any(e['policy'] != INHERIT_ROOT for e in package['members'][1:])):
            raise ValueError('Test update must replace only the GUI and inherit the other 25 roles')
        result = package_file.read_bytes()
        with output.open('xb') as stream:
            stream.write(result)
    return dict(file=str(output), bytes=len(result), sha256=sha(result),
                runtime_build=info['build'], runtime_source_commit=commit,
                runtime_sha256=sha(data), main_profile_source=MAIN,
                manifest_entries=26, replacements=['runtime.kui'], inherited_roles=25,
                worker_replacements=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--runtime-elf', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(build(args.runtime, args.output, args.runtime_elf), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
