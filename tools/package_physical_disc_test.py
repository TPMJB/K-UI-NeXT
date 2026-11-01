#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the independent physical-disc boot baseline as a copy-over KUI folder."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from package_native_vmu_only import checkout_script
from package_vmu_native_dual_delivery import offline_source_recovery_check
from runtime_package import envelope, flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
BASE = '7cddad936d5d28b2a061b5e2636daf1f93fe12e1'
TARGETS = ('KUI/runtime.kui', 'KUI/apps/disc/native-disc.kui')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def clean_source():
    require(not git('status', '--porcelain', '--untracked-files=all'),
            'Commit source changes before packaging')
    return git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')


def ordinary(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), 'Missing ordinary artifact: ' + str(path))
    return path.read_bytes()


def provenance(path, elf, commit, required=False):
    path = Path(path)
    if not path.exists() and not required:
        return None
    data = ordinary(path)
    require(json.loads(data) == dict(commit=commit, source_dirty=False, elf_sha256=sha(elf)),
            'Linked provenance differs from this clean checkpoint: ' + str(path))
    return data


def source_guide(commit, base):
    return f'''# Exact physical-disc test source

Clean source commit: `{commit}`. Diff base: `{base}`.

The full Git bundle needs no network or prerequisite bundle. Recover its exact
source tree from the extracted package folder with:

```sh
python3 source/checkout-source.py
```

With the pinned KallistiOS and SH-4 dependencies configured, rebuild with:

```sh
make -C restored-source -f Makefile.dc build/kui-runtime.elf -j2
make -C restored-source -f Makefile.disc-native -j1
```

`source/source.diff` is the complete binary-safe change from the stated base.
`artifacts/` contains the exact linked ELF files, maps, runtime envelopes,
compile records and emitted stack/callgraph files that were available.
`evidence/` contains only reports explicitly supplied during packaging. Packaging
does not rerun the host suites. Their results do not establish console behavior.
This delivery installs no emulated VMU and contains no game executable or disc
image. DreamShell code and firmware binaries are not used by this implementation.
'''.encode()


def package(args):
    commit, tree = clean_source()
    base = git('rev-parse', args.base + '^{commit}')
    subprocess.run(['git', '-C', str(ROOT), 'merge-base', '--is-ancestor', base, commit], check=True)
    build = commit[:12]
    gui_elf = ordinary(args.gui_elf)
    payload, memory = flatten_elf(gui_elf)
    gui = envelope(payload, memory, build)
    require(verify(gui)['build'] == build, 'GUI envelope build differs')
    gui_proof = provenance(args.gui_elf.with_suffix('.compile.json'), gui_elf, commit, True)
    stage_build = args.disc_build.resolve(strict=True)
    module = ordinary(stage_build / 'native-disc.kui')
    require(verify(module)['build'] == build, 'Disc module build differs from HEAD')
    entry_elf = ordinary(stage_build / 'entry.elf')
    entry_payload, entry_memory = flatten_elf(entry_elf)
    info = verify(module)
    require(module[64:] == entry_payload and info['memory_bytes'] == entry_memory,
            'Disc module differs from its exact entry ELF')
    files = {TARGETS[0]: gui, TARGETS[1]: module,
             'README-FIRST.md': ordinary(ROOT / 'docs/physical-disc-test.md').replace(b'@BUILD@', build.encode()),
             'physical-disc-vmu-plan.md': ordinary(ROOT / 'docs/physical-disc-vmu-plan.md'),
             'SOURCE.md': source_guide(commit, base),
             'LICENSE': ordinary(ROOT / 'LICENSE'), 'THIRD_PARTY.md': ordinary(ROOT / 'THIRD_PARTY.md'),
             'artifacts/gui/kui-runtime.elf': gui_elf,
             'artifacts/gui/kui-runtime.map': ordinary(args.gui_elf.with_suffix('.map')),
             'artifacts/gui/kui-runtime.compile.json': gui_proof}
    for name in ('entry.elf', 'stage.elf', 'entry.map', 'stage.map', 'stage.bin'):
        files['artifacts/disc/' + name] = ordinary(stage_build / name)
    require(entry_payload[0x2000:] == files['artifacts/disc/stage.bin'],
            'Disc entry does not contain the exact supplied stage binary')
    for name in ('entry', 'stage'):
        proof = provenance(stage_build / (name + '.compile.json'),
                           files['artifacts/disc/' + name + '.elf'], commit, True)
        if proof is not None:
            files['artifacts/disc/' + name + '.compile.json'] = proof
    for folder, prefix in ((stage_build, 'artifacts/disc/'), (args.gui_objects, 'artifacts/gui/objects/')):
        for pattern in ('*.su', '*.ci'):
            for path in sorted(folder.rglob(pattern)):
                files[prefix + path.relative_to(folder).as_posix()] = ordinary(path)
    require(args.evidence, 'Supply the targeted test/audit reports with --evidence')
    for path in args.evidence:
        target = 'evidence/' + path.name
        require(target not in files, 'Duplicate evidence filename: ' + path.name)
        files[target] = ordinary(path)
    files['source/source.diff'] = subprocess.check_output(
        ['git', '-C', str(ROOT), 'diff', '--binary', base, commit])
    with tempfile.TemporaryDirectory(prefix='kui-disc-source-') as directory:
        bundle = Path(directory) / 'native-source-full.bundle'
        subprocess.run(['git', '-C', str(ROOT), '-c', 'pack.threads=1', 'bundle', 'create',
                        str(bundle), 'HEAD', '--all'], check=True, capture_output=True)
        subprocess.run(['git', '-C', str(ROOT), 'bundle', 'verify', str(bundle)], check=True, capture_output=True)
        source = bundle.read_bytes()
    candidate = dict(schema='kui.physical-disc.test.v1', build=build, source_commit=commit,
        source_tree=tree, source_diff_base=base, install_files={name: sha(files[name]) for name in TARGETS},
        virtual_vmu_enabled=False, virtual_card_lookup=False, sd_save_writes=False,
        permanent_resident=False, bios_gd_replacement=False, console_execution_verified=False,
        native_gd_only=True, mode1_data_only=True, windows_ce_supported=False, cd_supported=False,
        check_executable_limit_bytes=12 * 1024 * 1024, direct_executable_limit_bytes=4 * 1024 * 1024,
        foreign_region_boot_verified=False, independent_sd_owner_wired=False,
        source_full_bundle=dict(path='source/native-source-full.bundle', bytes=len(source),
                                sha256=sha(source), full_history=True, prerequisites=[]))
    script = checkout_script().replace(b'Verified offline VMU-only source', b'Verified offline physical-disc source')
    candidate['offline_source_recovery'] = offline_source_recovery_check(candidate, script, source)
    files['source/native-source-full.bundle'] = source
    files['source/checkout-source.py'] = script
    files['CANDIDATE.json'] = files['manifest.json'] = wire(candidate)
    files['checksums.json'] = wire({name: sha(data) for name, data in sorted(files.items())})
    require(clean_source() == (commit, tree), 'Source changed while packaging')
    name = 'K-UI-Physical-Disc-Test-' + build
    output = (args.output or ROOT.parent / 'delivery' / (name + '.zip')).resolve()
    require(not output.is_relative_to(ROOT), 'Save delivery outside the source checkout')
    require(not output.exists(), 'Output already exists: ' + str(output))
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.physical-disc-', suffix='.zip', dir=output.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path, data in sorted(files.items()):
                item = zipfile.ZipInfo(name + '/' + path, (1980, 1, 1, 0, 0, 0))
                item.create_system = 3
                item.external_attr = 0o100644 << 16
                item.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(item, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        with zipfile.ZipFile(temporary) as archive:
            require(len(archive.namelist()) == len(files) and all(
                archive.read(name + '/' + path) == data for path, data in files.items()), 'ZIP round-trip differs')
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)
    result = dict(zip=str(output), bytes=output.stat().st_size, sha256=sha(output.read_bytes()),
                  source_commit=commit, build=build, install_files=list(TARGETS), virtual_vmu_enabled=False)
    print(json.dumps(result, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui-elf', type=Path, default=ROOT / 'build/kui-runtime.elf')
    parser.add_argument('--gui-objects', type=Path, default=ROOT / 'build/dc')
    parser.add_argument('--disc-build', type=Path, default=ROOT / 'build/disc-native')
    parser.add_argument('--base', default=BASE)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    args = parser.parse_args()
    try:
        package(args)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'Physical-disc package refused: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
