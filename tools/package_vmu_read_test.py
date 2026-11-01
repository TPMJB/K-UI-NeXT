#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the exact GUI-only VMU reader test as a copy-over KUI folder."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from package_native_vmu_only import checkout_script
from package_physical_disc_test import ROOT, clean_source, ordinary, require, sha, wire
from package_vmu_native_dual_delivery import offline_source_recovery_check
from runtime_package import envelope, flatten_elf, verify


def package(args):
    commit, tree = clean_source()
    build = commit[:12]
    elf = ordinary(args.elf)
    proof = ordinary(args.elf.with_suffix('.compile.json'))
    require(json.loads(proof) == dict(commit=commit, source_dirty=False, elf_sha256=sha(elf)),
            'GUI provenance differs from this exact clean checkpoint')
    payload, memory = flatten_elf(elf)
    runtime = envelope(payload, memory, build)
    require(verify(runtime)['build'] == build, 'Runtime envelope differs from source')
    files = {
        'KUI/runtime.kui': runtime,
        'README-FIRST.md': ordinary(ROOT/'docs/black-vmu-import-test.md').replace(b'@BUILD@', build.encode()),
        'LICENSE': ordinary(ROOT/'LICENSE'),
        'THIRD_PARTY.md': ordinary(ROOT/'THIRD_PARTY.md'),
        'artifacts/kui-runtime.elf': elf,
        'artifacts/kui-runtime.map': ordinary(args.elf.with_suffix('.map')),
        'artifacts/kui-runtime.compile.json': proof,
    }
    require(args.evidence, 'Supply the targeted host and console-build reports')
    for path in args.evidence:
        name = 'evidence/'+path.name
        require(name not in files, 'Duplicate evidence filename')
        files[name] = ordinary(path)
    with tempfile.TemporaryDirectory(prefix='kui-vmu-read-source-') as temporary:
        bundle_path = Path(temporary)/'source.bundle'
        subprocess.run(['git', '-C', str(ROOT), 'bundle', 'create', str(bundle_path), 'HEAD'],
                       check=True, capture_output=True)
        bundle = ordinary(bundle_path)
    candidate = dict(schema='kui.physical-vmu-read.test.v1', build=build,
        source_commit=commit, source_tree=tree, install_files=['KUI/runtime.kui'],
        runtime_sha256=sha(runtime), game_modules_replaced=False,
        physical_import_writes=False, hardware_recovery_verified=False,
        source_full_bundle=dict(path='source/native-source-full.bundle', bytes=len(bundle),
                                sha256=sha(bundle), full_history=True, prerequisites=[]))
    script = checkout_script().replace(b'Verified offline VMU-only source', b'Verified offline VMU reader source')
    candidate['offline_source_recovery'] = offline_source_recovery_check(candidate, script, bundle)
    files['source/native-source-full.bundle'] = bundle
    files['source/checkout-source.py'] = script
    files['CANDIDATE.json'] = wire(candidate)
    files['SOURCE.md'] = (f'# Exact GUI source\n\nCommit: `{commit}`. Tree: `{tree}`.\n\n'
        'From this extracted folder, run `python3 source/checkout-source.py` to recover\n'
        'the exact source without a network connection. This command is for source\n'
        'recovery only; installation is the KUI-folder copy described in README-FIRST.md.\n').encode()
    files['checksums.json'] = wire({name: sha(data) for name, data in sorted(files.items())})
    require(clean_source() == (commit, tree), 'Source changed while packaging')
    name = 'K-UI-Black-VMU-Import-Test-'+build
    output = args.output.resolve()
    require(not output.is_relative_to(ROOT) and not output.exists(), 'Choose a new output outside source')
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.vmu-read-', suffix='.zip', dir=output.parent)
    os.close(descriptor)
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path, data in sorted(files.items()):
                item = zipfile.ZipInfo(name+'/'+path, (1980, 1, 1, 0, 0, 0))
                item.create_system = 3
                item.external_attr = 0o100644 << 16
                archive.writestr(item, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        require(Path(temporary).stat().st_size < 50*1024*1024, 'Delivery exceeds 50 MiB')
        with zipfile.ZipFile(temporary) as archive:
            require(len(archive.namelist()) == len(files) and all(
                archive.read(name+'/'+path) == data for path, data in files.items()), 'ZIP round-trip differs')
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return dict(zip=str(output), bytes=output.stat().st_size, sha256=sha(output.read_bytes()),
                source_commit=commit, build=build, install_files=['KUI/runtime.kui'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT/'build/kui-runtime.elf')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    print(json.dumps(package(parser.parse_args()), indent=2))
