#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the guarded DOA2 standalone New demand-reader experiment."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

from check_loader_layout import inspect_elf
from check_vmu_native_worker_gateway import read_elf
from runtime_package import ADDRESS, envelope, flatten_elf, verify
from package_vmu_native_a2_delivery import archive_files, checksums
from package_vmu_native_dual_delivery import offline_source_recovery_check

ROOT = Path(__file__).resolve().parents[1]
BASELINE_BUILD = '6be76940ccc9'
BASELINE_ZIP_SHA = 'c277be813459b5aae3fd90aec8896c4e168a3ba25c3a84b19f85e20736d86f2d'
BASELINE_METADATA_SHA = 'c82993927753fb93f6ce30d66933e1d4e92a0cf033b3072f39102bdca0853c44'
GUI_SHA = '57b9584ba789b425c8924a63f955aeefa104b3bb0bf9a2fab74d02d320c91a22'
ONE = (1, 0, 0, 0xfff1)
DESTINATION = 'KUI/apps/games/retail-boot.kui'
MODULE = 'variants/doa2/retail-native-standalone.kui'
ARTIFACTS = ('entry.elf', 'stage.elf', 'stage.bin', 'resident-sci.elf', 'resident-sci.bin')


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2)+'\n').encode()


def git(*arguments):
    return subprocess.check_output(['git', *arguments], cwd=ROOT, text=True).strip()


def embedded(image, begin, end, binary, base, container):
    first, last = image[4][begin][0], image[4][end][0]
    require(last-first == len(binary) and container[first-base:last-base] == binary,
            'Compiled embedded image differs: '+begin)


def bind_build(build, runtime_commit):
    hashes = {name: sha((build/name).read_bytes()) for name in ARTIFACTS}
    stamp = json.loads((build/'entry.compile.json').read_text())
    require(stamp == dict(commit=runtime_commit, source_dirty=False,
                          elf_sha256=hashes['entry.elf']),
            'Entry compile provenance differs from the clean runtime freeze')
    entry, stage, resident = (read_elf(build/(name+'.elf')) for name in
                              ('entry', 'stage', 'resident-sci'))
    payload, memory = flatten_elf(entry[0])
    module = envelope(payload, memory, runtime_commit[:12])
    require(module == (build/'retail-standalone-doa2.kui').read_bytes() and
            verify(module)['build'] == runtime_commit[:12],
            'Standalone module differs from its complete linked entry')
    stage_bytes, low_bytes = ((build/name).read_bytes() for name in ('stage.bin', 'resident-sci.bin'))
    require(inspect_elf(stage[0], 0x8ce00000, 0x8cfe0000)['payload'] == stage_bytes,
            'Standalone stage binary differs from its ELF')
    require(inspect_elf(resident[0], 0x8c004000, 0x8c007d00)['payload'] == low_bytes,
            'Standalone low resident binary differs from its ELF')
    embedded(entry, '__retail_stage_blob_start', '__retail_stage_blob_end',
             stage_bytes, ADDRESS, payload)
    embedded(stage, '__retail_resident_sci_blob_start', '__retail_resident_sci_blob_end',
             low_bytes, 0x8ce00000, stage_bytes)
    require(stage[4].get('__kui_native_standalone_reader_stage') == ONE and
            stage[4].get('__kui_native_standalone_doa2_stage') == ONE and
            resident[4].get('__kui_native_standalone_reader_resident') == ONE,
            'Standalone DOA2 stage/resident markers are not paired')
    forbidden = ('__kui_native_dual_reader_capability', '_native_heap_lease',
                 '_kui_vmu_observe_sd_lease', '_kui_vmu_native_passive_submit',
                 '_kui_vmu_native_passive_complete')
    require(not any(name in image[4] for name in forbidden for image in (stage, resident)) and
            not any('native_ra_' in name or 'native_async_loan' in name
                    for name in resident[4]),
            'Standalone experiment contains a VMU, read-ahead or heap-reservation capability')
    descriptor = bytes.fromhex('4b4452314455414c010000002000000001000000020000000000060000000000')
    require(descriptor not in module,
            'Standalone module falsely advertises the historical dual-reader heap lease')
    require(resident[4]['_standalone_reader_region'][1] == 0x760 and
            resident[4]['__retail_resident_bss_end'][0] <= resident[4]['__retail_hook_stack_bottom'][0],
            'Standalone reader region or low resident bounds differ')
    return module, hashes


def validate_execution(report, build, runtime_commit):
    require(report.get('schema') == 'kui.native.standalone-reader.execution.v1' and
            report.get('status') == 'PASS' and report.get('source_commit') == runtime_commit and
            report.get('launchable') is True and report.get('standalone_reader') is True and
            report.get('virtual_vmu') is False and report.get('readahead') is False and
            report.get('heap_reservation_bytes') == 0,
            'A final launchable standalone execution proof is required; fit-only evidence is insufficient')
    artifacts = report.get('artifact_sha256', {})
    require(all(artifacts.get(name) == sha((build/name).read_bytes()) for name in
                ('entry.elf', 'stage.elf', 'resident-sci.elf')),
            'Standalone execution proof differs from the final artifacts')
    sources = report.get('source_sha256', {})
    require(isinstance(sources, dict) and sources,
            'Standalone execution proof omits source bindings')
    for name, digest in sources.items():
        path = Path(name)
        require(not path.is_absolute() and '..' not in path.parts and
                sha((ROOT/path).read_bytes()) == digest,
                'Standalone proof source changed: '+name)


def baseline_files(path):
    require(sha(path.read_bytes()) == BASELINE_ZIP_SHA, 'Passing 6be archive checksum differs')
    old = archive_files(path)
    require(sha(old['isolation.json']) == BASELINE_METADATA_SHA and
            json.loads(old['isolation.json'])['build'] == BASELINE_BUILD and
            sha(old['baseline/runtime.kui']) == GUI_SHA,
            'Passing 6be metadata or unchanged GUI binding differs')
    result = {'baseline/'+name: data for name, data in old.items()
              if name in ('isolation.json', 'personalize.py', 'install.py') or name.startswith('variants/')}
    dependencies = ('runtime_package.py', 'vmu_persist_profile.py', 'vmu_native_mode.py',
                    'vmu_native_cache_seed.py', 'install_vmu_native_dual.py',
                    'install_vmu_native_isolation.py')
    for name in dependencies:
        result['tools/'+name] = old['tools/'+name]
    return result, old['baseline/runtime.kui']


def checkout_script():
    return b'''#!/usr/bin/env python3
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
else:subprocess.run(['git','clone','--no-checkout',str(bundle),str(dest)],check=True)
if git('rev-parse',item['source_commit']+'^{tree}')!=item['source_tree']:
    raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
if git('status','--porcelain'):raise SystemExit('Restored source is not clean')
print('Verified offline standalone source',item['source_commit'],dest)
print('Runtime source:',item['runtime_source_commit'])
'''


def guide(runtime_commit, delivery_commit):
    return f'''# DOA2 — New standalone demand reader

This targeted test runs the New demand engine with virtual VMU and read-ahead off.
It reserves no game heap. It admits only the reviewed DOA2 image on SCI.
Evolution's passing 6be combined module remains installed and unchanged.

Extract this ZIP, then run from the extracted folder:

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant doa2
```

In K-UI select your usual DOA2 image, SCI, **Background Off (Standard)**,
CD music off, and RAM patch off. For this experiment that menu selection loads
the ordinary module containing the **New standalone demand engine**. The game's
launch banner identifies the standalone reader. No prepared VMU profile is used.

Boot to the title screen and enter one fight. Report whether it boots and whether
the opening/fight-start audio or loading changes. Photograph an automatic error
page if one appears. This is a new console test; software proofs do not establish
hardware compatibility or speed.

Restore the exact prior ordinary module before using any earlier isolation installer:

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

Then Background New (X) still selects the untouched passing 6be combined module
for Evolution. The separate standalone backup leaves the original isolation
backup/receipt, prepared card, profile, GUI and launch settings unchanged.
Safely eject after installing or restoring.

Runtime source: `{runtime_commit}`. Delivery source: `{delivery_commit}`.
Run `python3 source/checkout-source.py` to restore the complete source history offline.
'''.encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--runtime-commit', required=True)
    parser.add_argument('--execution-proof', type=Path, required=True)
    parser.add_argument('--proof-dir', type=Path, required=True)
    parser.add_argument('--baseline-package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(not git('status', '--porcelain'), 'Commit final standalone delivery sources before packaging')
    delivery_commit, delivery_tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    runtime_commit = git('rev-parse', args.runtime_commit+'^{commit}')
    ancestry = subprocess.run(['git', 'merge-base', '--is-ancestor', runtime_commit, delivery_commit],
                              cwd=ROOT, capture_output=True)
    require(ancestry.returncode == 0, 'The full delivery history must contain the runtime freeze')
    build = args.build.resolve()
    module, artifact_hashes = bind_build(build, runtime_commit)
    proof = json.loads(args.execution_proof.read_text())
    validate_execution(proof, build, runtime_commit)
    files, gui = baseline_files(args.baseline_package)
    metadata = dict(schema='kui.native.standalone-reader.delivery.v1', source_commit=delivery_commit,
        runtime_source_commit=runtime_commit, build=runtime_commit[:12], variant='doa2',
        destination=DESTINATION, module=dict(path=MODULE, bytes=len(module), sha256=sha(module)),
        gui=dict(build='8adf1a039035', sha256=GUI_SHA),
        baseline=dict(build=BASELINE_BUILD, isolation_metadata_sha256=BASELINE_METADATA_SHA),
        standalone_reader=True, virtual_vmu=False, readahead=False, demand_only=True,
        reader='New (X)', ui_background_selection='Off (Standard)', engine='New standalone demand',
        heap_reservation_bytes=0)
    files.update({'standalone-reader.json': wire(metadata), MODULE: module,
                  'install.py': (ROOT/'tools/install_native_standalone_reader.py').read_bytes(),
                  'README-FIRST.md': guide(runtime_commit, delivery_commit),
                  'LICENSE': (ROOT/'LICENSE').read_bytes(),
                  'THIRD_PARTY.md': (ROOT/'THIRD_PARTY.md').read_bytes()})
    for name in (*ARTIFACTS, 'entry.compile.json'):
        files['compiled/'+name] = (build/name).read_bytes()
    for path in sorted(args.proof_dir.iterdir()):
        if path.is_file() and path.suffix in ('.json', '.log', '.txt'):
            files['evidence/'+path.name] = path.read_bytes()
    files['evidence/standalone-reader-execution.json'] = args.execution_proof.read_bytes()
    evidence = ROOT/'docs/evidence/native-evolution1-6be769-split-console-pass-2026-10-26.json'
    if evidence.is_file():
        files['evidence/'+evidence.name] = evidence.read_bytes()
    sys.path.insert(0, str(ROOT/'tests'))
    from test_install_native_standalone_reader import extracted_install_check
    checks = extracted_install_check(files, gui)
    with tempfile.TemporaryDirectory(prefix='kui-standalone-source-') as directory:
        path = Path(directory)/'native-source-full.bundle'
        subprocess.run(['git', 'bundle', 'create', str(path), 'HEAD'], cwd=ROOT, check=True, capture_output=True)
        subprocess.run(['git', 'bundle', 'verify', str(path)], cwd=ROOT, check=True, capture_output=True)
        bundle = path.read_bytes()
    candidate = dict(schema='kui.native.standalone-reader.candidate.v1', source_commit=delivery_commit,
        source_tree=delivery_tree, runtime_source_commit=runtime_commit, runtime_build=runtime_commit[:12],
        source_full_bundle=dict(path='source/native-source-full.bundle', bytes=len(bundle),
                                sha256=sha(bundle), prerequisites=[]),
        artifact_sha256=artifact_hashes, module_sha256=sha(module),
        console_compatibility_verified=False, standalone_reader=True, virtual_vmu=False,
        readahead=False, heap_reservation_bytes=0, preserved_gui_sha256=GUI_SHA,
        baseline_archive_sha256=BASELINE_ZIP_SHA,
        evidence_sha256={name: sha(data) for name, data in files.items() if name.startswith('evidence/')})
    script = checkout_script()
    checks['offline_source_recovery'] = offline_source_recovery_check(candidate, script, bundle)
    files.update({'CANDIDATE.json': wire(candidate), 'delivery-checks.json': wire(checks),
                  'source/native-source-full.bundle': bundle, 'source/checkout-source.py': script,
                  'source/SOURCE-RECOVERY.md': b'Run python3 source/checkout-source.py from the extracted folder.\n'})
    files['SHA256SUMS.txt'] = checksums(files)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    require(archive_files(args.output) == files, 'Final standalone archive checksum verification failed')
    require(git('rev-parse', 'HEAD') == delivery_commit and not git('status', '--porcelain'),
            'Standalone delivery source changed during packaging')
    print(json.dumps(dict(zip=str(args.output.resolve()), bytes=args.output.stat().st_size,
        sha256=sha(args.output.read_bytes()), runtime_source_commit=runtime_commit,
        delivery_source_commit=delivery_commit, delivery_checks=checks), indent=2))


if __name__ == '__main__':
    main()
