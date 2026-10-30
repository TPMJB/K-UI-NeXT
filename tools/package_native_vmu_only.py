#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package both audited compact VMU-only transports and complete offline source."""
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

from check_loader_layout import inspect_elf
from check_native_vmu_only import audit
from package_vmu_native_dual_delivery import offline_source_recovery_check
from personalize_vmu_sd_proof import readonly_blob
from runtime_package import ADDRESS, verify
from vmu_persist_profile import template_profile

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 'kui.native.vmu-only.delivery.v1'
SPAN = 0x1c000
TRANSPORTS = {'sci': 1, 'scif': 0}
ARTIFACTS = ('retail-maple-observe.kui', 'retail-native-vmu-only.kui',
             'entry.elf', 'entry.map', 'entry.compile.json', 'stage.elf', 'stage.bin', 'stage.map',
             'resident-sci.elf', 'resident-sci.bin', 'resident-sci.map',
             'native-worker.elf', 'native-worker.bin', 'native-worker.map', 'native-worker.reloc',
             'vmu-sd-profile.bin', 'readonly-config', 'vmu_maple_observe_symbols.h')


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2)+'\n').encode()


def git(*arguments):
    return subprocess.check_output(['git', *arguments], cwd=ROOT, text=True).strip()


def bind_profile(build, commit):
    """Derive the only personalized span from its exact read-only linked owner."""
    module = (build/'retail-maple-observe.kui').read_bytes()
    require((build/'retail-native-vmu-only.kui').read_bytes() == module,
            'VMU-only published alias differs from the audited runtime')
    stage_data = (build/'stage.elf').read_bytes()
    stage = inspect_elf(stage_data, 0x8ce00000, 0x8cfe0000)
    entry = inspect_elf((build/'entry.elf').read_bytes(), ADDRESS, 0x8c112000)
    template = template_profile()
    address, _ = readonly_blob(stage_data, stage, '__vmu_sd_profile_blob_start',
                               '__vmu_sd_profile_blob_end', len(template))
    stage_address, _ = readonly_blob((build/'entry.elf').read_bytes(), entry,
        '__retail_stage_blob_start', '__retail_stage_blob_end', len((build/'stage.bin').read_bytes()))
    offset = 64+stage_address-ADDRESS+address-0x8ce00000
    require(offset >= 64 and not offset&3 and module[offset:offset+len(template)] == template and
            module.find(template) == offset and module.find(template, offset+1) == -1 and
            (build/'vmu-sd-profile.bin').read_bytes() == template,
            'Runtime does not contain exactly its typed canonical profile template')
    require(verify(module)['build'] == commit[:12], 'Runtime build differs from source checkpoint')
    # Cache discovery remains cold and the immutable runtime mode stays NORMAL.
    # This manifest exposes no cache-seed or diagnostic mode mutation capability.
    manifest = dict(schema='kui.native.persistent.personalization.v1', source_commit=commit,
        build=commit[:12], module_sha256=sha(module), module_bytes=len(module),
        profile_module_offset=offset, profile_bytes=len(template),
        profile_sha256=sha(template), storage_writes_enabled=True)
    return module, manifest


def checkout_script():
    return b'''#!/usr/bin/env python3
"""Recover this exact source checkpoint without a network connection."""
import hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent
root=here.parent
item=json.loads((root/'CANDIDATE.json').read_text())
bundle=here/'native-source-full.bundle'
info=item['source_full_bundle']
if bundle.stat().st_size!=info['bytes'] or hashlib.sha256(bundle.read_bytes()).hexdigest()!=info['sha256']:
    raise SystemExit('Full source bundle checksum mismatch')
dest=root/'restored-source'
def git(*a):return subprocess.check_output(['git','-C',str(dest),*a],text=True).strip()
if dest.is_symlink():raise SystemExit('Refusing a symlink source destination')
if dest.exists():
    top=subprocess.run(['git','-C',str(dest),'rev-parse','--show-toplevel'],text=True,capture_output=True)
    if top.returncode or pathlib.Path(top.stdout.strip()).resolve()!=dest:
        raise SystemExit('Destination is not a source repository root')
    if git('status','--porcelain'):raise SystemExit('Refusing a dirty source checkout')
    subprocess.run(['git','-C',str(dest),'fetch',str(bundle),'HEAD'],check=True)
else:
    subprocess.run(['git','clone','--no-checkout',str(bundle),str(dest)],check=True)
if git('rev-parse',item['source_commit']+'^{tree}')!=item['source_tree']:
    raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
if git('status','--porcelain'):raise SystemExit('Restored source is not clean')
print('Verified offline VMU-only source',item['source_commit'],dest)
'''


def test_matrix():
    stream = io.StringIO(newline='')
    writer = csv.writer(stream)
    writer.writerow(('transport', 'order', 'game', 'test', 'result', 'notes'))
    cases = (
        ('Evolution 1', 'Load old save; pass Sofdec transition; save; power off; reload'),
        ('Rayman 2', 'Load; play/stream; save; continue; relaunch/load'),
        ('Record of Lodoss War', 'Load; play/stream; save; continue; relaunch/load'),
        ('Sonic Adventure 2', 'FMV; gameplay; save/load; physical A1/controller passthrough'),
        ('Power Stone 2', 'FMV; match; save/load; physical A1/controller passthrough'),
        ('Grandia II', 'FMV; gameplay; save/load'),
        ('Skies of Arcadia (optional)', 'Load; gameplay; save/load'),
    )
    for name in TRANSPORTS:
        for order, (game, test) in enumerate(cases, 1):
            writer.writerow((name.upper(), order, game, test, '', ''))
    return stream.getvalue().encode()


def source_guide(commit):
    return f'''# Complete source checkpoint

Runtime source commit: `{commit}`.

This ZIP contains a full Git bundle with source history and no prerequisites.
No network access is required to recover it. From the extracted ZIP folder:

```sh
python3 source/checkout-source.py
```

The script verifies the bundle checksum, recovers the exact commit into
`restored-source/`, and checks its complete source tree. It refuses an existing
dirty checkout, a symlink destination, or an unrelated directory.

With the existing `sh-elf` toolchain on PATH, rebuild the two transports:

```sh
make -C restored-source -f Makefile.vmu-native-vmu-only -j4 BUILD=build/native-vmu-only-sci VMU_TRANSPORT=1
make -C restored-source -f Makefile.vmu-native-vmu-only -j4 BUILD=build/native-vmu-only-scif VMU_TRANSPORT=0
```

Run `tools/check_native_vmu_only.py` with the corresponding build directory and
`--transport sci` or `--transport scif`. The archived linked audit reports and
compiled ELF files are under `artifacts/sci/` and `artifacts/scif/`.
'''.encode()


def package(sci_build, scif_build, output=None, evidence=()):
    commit, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    require(not git('status', '--porcelain'), 'Commit source changes before packaging')
    revision = commit[:12]
    files = {}
    metadata = dict(schema=SCHEMA, source_commit=commit, build=revision, lease_bytes=SPAN, variants={})
    candidate = dict(schema='kui.native.vmu-only.candidate.v1', source_commit=commit,
        source_tree=tree, build=revision, lease_bytes=SPAN, virtual_a2=True,
        background_reader_enabled=False, prefetch_enabled=False, console_execution_verified=False,
        variants={})
    builds = {'sci': Path(sci_build).resolve(strict=True), 'scif': Path(scif_build).resolve(strict=True)}
    for name, build in builds.items():
        report = audit(build, transport=TRANSPORTS[name], allow_dirty=False, self_test=True)
        require(report.get('schema') == 'kui.native.vmu-only.linked.v1' and
                report.get('transport') == TRANSPORTS[name] and report.get('lease_bytes') == SPAN and
                report.get('worker_span') == SPAN and report.get('background_reader_enabled') is False and
                report.get('prefetch_enabled') is False and report.get('console_execution_verified') is False and
                report.get('source_provenance', {}).get('commit') == commit and
                report['source_provenance'].get('source_dirty') is False,
                'Linked audit differs from this clean compact runtime: '+name)
        report_wire = wire(report)
        (build/'vmu-only-audit.json').write_bytes(report_wire)
        module, manifest = bind_profile(build, commit)
        require(report.get('module_sha256') == sha(module), 'Linked audit module binding differs: '+name)
        prefix = 'variants/'+name
        files[prefix+'/base/KUI/apps/games/retail-boot.kui'] = module
        files[prefix+'/base/native-persistent-personalization.json'] = wire(manifest)
        metadata['variants'][name] = dict(directory=prefix, transport=TRANSPORTS[name], module_sha256=sha(module))
        artifact_hashes = {}
        for artifact in ARTIFACTS:
            data = (build/artifact).read_bytes()
            files['artifacts/'+name+'/'+artifact] = data
            artifact_hashes[artifact] = sha(data)
        for artifact in sorted(build.rglob('*.ci')):
            relative = artifact.relative_to(build).as_posix()
            files['artifacts/'+name+'/'+relative] = artifact.read_bytes()
        for artifact in sorted(build.rglob('*.su')):
            relative = artifact.relative_to(build).as_posix()
            files['artifacts/'+name+'/'+relative] = artifact.read_bytes()
        files['artifacts/'+name+'/vmu-only-audit.json'] = report_wire
        candidate['variants'][name] = dict(transport=TRANSPORTS[name], module_sha256=sha(module),
            audit_sha256=sha(report_wire), artifact_sha256=artifact_hashes,
            profile_module_offset=manifest['profile_module_offset'])
    for name, path in (('install.py', 'tools/install_native_vmu_only.py'),
                       ('personalize.py', 'tools/personalize_vmu_native_persistent.py'),
                       ('tools/runtime_package.py', 'tools/runtime_package.py'),
                       ('tools/vmu_persist_profile.py', 'tools/vmu_persist_profile.py'),
                       ('README-FIRST.md', 'docs/native-vmu-only-test.md'),
                       ('LICENSE', 'LICENSE'), ('THIRD_PARTY.md', 'THIRD_PARTY.md')):
        files[name] = (ROOT/path).read_bytes()
    files['vmu-only.json'] = wire(metadata)
    files['test-results.csv'] = test_matrix()
    files['SOURCE.md'] = source_guide(commit)
    for source in evidence:
        source = Path(source).resolve(strict=True)
        require(source.is_file() and not source.is_symlink(), 'Evidence must be an ordinary file')
        target = 'evidence/'+source.name
        require(target not in files, 'Duplicate evidence filename: '+source.name)
        files[target] = source.read_bytes()
    with tempfile.TemporaryDirectory(prefix='kui-vmu-only-source-') as temporary:
        bundle = Path(temporary)/'native-source-full.bundle'
        subprocess.run(['git', 'bundle', 'create', str(bundle), 'HEAD', '--all'], cwd=ROOT, check=True)
        subprocess.run(['git', 'bundle', 'verify', str(bundle)], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        bundle_data = bundle.read_bytes()
    candidate['source_full_bundle'] = dict(path='source/native-source-full.bundle',
        bytes=len(bundle_data), sha256=sha(bundle_data), full_history=True, prerequisites=[])
    script = checkout_script()
    candidate['offline_source_recovery'] = offline_source_recovery_check(candidate, script, bundle_data)
    require(git('rev-parse', 'HEAD') == commit and git('rev-parse', 'HEAD^{tree}') == tree and
            not git('status', '--porcelain'), 'Source changed while packaging')
    files['source/native-source-full.bundle'] = bundle_data
    files['source/checkout-source.py'] = script
    files['CANDIDATE.json'] = wire(candidate)
    files['SHA256SUMS.txt'] = ''.join(sha(data)+'  '+name+'\n'
        for name, data in sorted(files.items())).encode()
    if output is None:
        output = ROOT.parent/'delivery'/('K-UI-VMU-Only-'+revision+'.zip')
    output = Path(output).resolve()
    require(not output.is_relative_to(ROOT), 'Save the deliverable outside the source checkout')
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.vmu-only-package-', suffix='.zip', dir=output.parent)
    os.close(fd)
    temporary = Path(temporary)
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(temporary) as archive:
            require(set(archive.namelist()) == set(files) and len(archive.namelist()) == len(files) and
                    all(archive.read(name) == data for name, data in files.items()),
                    'ZIP round-trip byte verification failed')
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    result = dict(zip=str(output), bytes=output.stat().st_size, sha256=sha(output.read_bytes()),
                  source_commit=commit, build=revision, variants=candidate['variants'],
                  console_execution_verified=False)
    print(json.dumps(result, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sci-build', type=Path, default=ROOT/'build/native-vmu-only-sci')
    parser.add_argument('--scif-build', type=Path, default=ROOT/'build/native-vmu-only-scif')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--evidence', type=Path, action='append', default=[])
    args = parser.parse_args()
    try:
        package(args.sci_build, args.scif_build, args.output, args.evidence)
    except (OSError, ValueError, AssertionError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'VMU-only package refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
