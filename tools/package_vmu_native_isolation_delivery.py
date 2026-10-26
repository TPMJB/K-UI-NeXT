#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package two isolated native controls with exact backup/restore installation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

from check_vmu_native_worker_gateway import read_elf
from check_loader_layout import inspect_elf
from runtime_package import ADDRESS, envelope, flatten_elf, verify
from vmu_native_relocations import generate, validate_loads
from vmu_persist_profile import template_profile
from vmu_native_cache_seed import template_seed, SEED_BYTES, RECORD_BYTES, DETECTOR_EPOCH
from vmu_native_mode import template_mode, MODE_BYTES, decode_mode
from package_vmu_native_persistent import cache_catalog
from package_vmu_native_a2_delivery import archive_files, checksums
from package_vmu_native_dual_delivery import offline_source_recovery_check
from install_vmu_native_isolation import MODULES, GUI, BACKUP_PARENT

ROOT = Path(__file__).resolve().parents[1]
GUI_BUILD = '8adf1a039035'
GUI_SHA = '57b9584ba789b425c8924a63f955aeefa104b3bb0bf9a2fab74d02d320c91a22'
ABSOLUTE_ONE = (1, 0, 0, 0xfff1)
REQUIRED_ARTIFACTS = ('entry.elf', 'stage.elf', 'native-worker.elf', 'native-worker.bin',
                      'native-worker.reloc', 'resident-sci.elf', 'resident-sci.bin')
EXTRA_PROOFS = ('instruction-audit.json', 'sdk-reader.json', 'sdk-standard.json',
                'stack-reader.json', 'stack-standard.json',
                'reader-component-comparison.json', 'isolation-cold.json')
PREVIOUS_BUILD = '9956562f91d6'
PREVIOUS_METADATA_SHA = 'adf9dd97917140cc07e5027ae15fa1d85caa06cd83cb8e5d8db4551241e9f0cd'
PREVIOUS_ARCHIVE_SHA = 'cd6d51767788f4222fffd3c6299024bb8700f8407fe623d6664ef922876d5f29'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2)+'\n').encode()


def git(*arguments):
    return subprocess.check_output(['git', *arguments], cwd=ROOT, text=True).strip()


def embedded(image, begin, end, data, base, binary):
    first, last = image[4][begin][0], image[4][end][0]
    require(last-first == len(data) and binary[first-base:last-base] == data,
            'Embedded linked bytes differ: '+begin)
    return first


def bind_variant(name, build, proof, runtime_commit, demand_only=False):
    artifacts = {key: sha((build/key).read_bytes()) for key in REQUIRED_ARTIFACTS}
    require(all(proof['artifact_sha256'].get(key) == value for key, value in artifacts.items()),
            'Linked execution report differs from final artifacts: '+name)
    for key in ('stage.bin', 'resident-legacy.bin', 'native-ra-aux.bin', 'native-ra-aux.reloc'):
        if (build/key).is_file():
            artifacts[key] = sha((build/key).read_bytes())
    provenance = json.loads((build/'entry.compile.json').read_text())
    require(provenance == dict(commit=runtime_commit, source_dirty=False, elf_sha256=artifacts['entry.elf']),
            'Entry was not compiled from the clean runtime freeze: '+name)
    entry, stage, worker = (read_elf(build/(key+'.elf')) for key in ('entry', 'stage', 'native-worker'))
    payload, memory = flatten_elf(entry[0])
    module = envelope(payload, memory, runtime_commit[:12])
    filename = 'retail-native-dual.kui' if name == 'reader-only' else 'retail-native-isolation-standard.kui'
    require(module == (build/filename).read_bytes() and verify(module)['build'] == runtime_commit[:12],
            'Runtime envelope differs from its complete linked entry: '+name)
    stage_binary = (build/'stage.bin').read_bytes()
    require(inspect_elf(stage[0], 0x8ce00000, 0x8cfe0000)['payload'] == stage_binary,
            'Stage binary differs from its ELF: '+name)
    stage_entry = embedded(entry, '__retail_stage_blob_start', '__retail_stage_blob_end',
                           stage_binary, ADDRESS, payload)
    raw_worker, reloc = (build/'native-worker.bin').read_bytes(), (build/'native-worker.reloc').read_bytes()
    embedded(stage, '__native_worker_blob_begin', '__native_worker_blob_end', raw_worker, 0x8ce00000, stage_binary)
    embedded(stage, '__native_worker_reloc_begin', '__native_worker_reloc_end', reloc, 0x8ce00000, stage_binary)
    require(generate(worker[0], raw_worker, 0x8c100000, 0x20000) == reloc,
            'Worker relocation table differs from actual linked pointers: '+name)
    loads = validate_loads(worker[0], raw_worker, 0x8c100000, 0x20000)
    stem = 'reader_only' if name == 'reader-only' else 'standard_vmu'
    other = 'standard_vmu' if name == 'reader-only' else 'reader_only'
    for image, side in ((stage, 'stage'), (worker, 'worker')):
        require(image[4].get('__kui_native_'+stem+'_isolation_'+side) == ABSOLUTE_ONE and
                '__kui_native_'+other+'_isolation_'+side not in image[4] and
                image[4].get('__kui_native_isolation_lease_bytes') == (393216, 0, 0, 0xfff1) and
                image[4].get('__kui_native_bottom_heap_'+side) == ABSOLUTE_ONE,
                'Variant lacks its exact isolated bottom-reservation capabilities: '+name)
        marker = '__kui_native_ra_tick_off_isolation_'+side
        require(image[4].get(marker) == ABSOLUTE_ONE if demand_only and name == 'reader-only' else
                marker not in image[4], 'Variant controller-prefetch marker differs: '+name)
    require(proof.get('controller_tick_off', False) is (demand_only and name == 'reader-only'),
            'Linked execution report controller-prefetch setting differs: '+name)
    if name == 'reader-only':
        require(worker[4].get('__kui_native_readahead_worker') == ABSOLUTE_ONE and
                worker[4].get('__kui_native_dual_readers_worker') == ABSOLUTE_ONE,
                'Reader-only control lacks its unchanged New reader/cache')
        descriptor = bytes.fromhex('4b4452314455414c010000002000000001000000020000000000060000000000')
        require(module[64:].count(descriptor) == 1, 'Reader-only control lost the preserved GUI route descriptor')
    else:
        require(not any(key in worker[4] for key in ('__kui_native_readahead_worker',
                    '__kui_native_dual_readers_worker', '__kui_native_sdk_sci_route_worker')),
                'Standard VMU control still contains an asynchronous reader capability')
    bss_end = worker[4]['__vmu_native_worker_bss_end'][0]
    stack_low, stack_high = (worker[4][key][0] for key in
                            ('__vmu_native_worker_stack_bottom', '__vmu_native_worker_stack_top'))
    require(bss_end <= stack_low and stack_high-stack_low == 4096 and
            stack_high == 0xac120000, 'Worker BSS/stack escaped the fixed 128 KiB reservation')
    manifest = dict(schema='kui.native.persistent.personalization.v1', source_commit=runtime_commit,
                    build=runtime_commit[:12], module_sha256=sha(module), module_bytes=len(module),
                    storage_writes_enabled=True)
    spans = []
    for field, start, end, template in (
        ('profile', '__vmu_sd_profile_blob_start', '__vmu_sd_profile_blob_end', template_profile()),
        ('cache_seed', '__native_resolution_cache_seed_start', '__native_resolution_cache_seed_end', template_seed()),
        ('runtime_mode', '__native_runtime_mode_start', '__native_runtime_mode_end', template_mode())):
        first, last = stage[4][start][0], stage[4][end][0]
        offset = 64+stage_entry-ADDRESS+first-0x8ce00000
        require(last-first == len(template) and not offset & 3 and
                module[offset:offset+len(template)] == template and module.count(template) == 1,
                'Canonical template is not uniquely bound to its typed stage span: '+field)
        require(all(offset+len(template) <= a or offset >= b for a, b in spans),
                'Personalization spans overlap')
        spans.append((offset, offset+len(template)))
        manifest.update({field+'_module_offset': offset, field+'_bytes': len(template),
                         field+'_sha256': sha(template)})
    catalog, files = cache_catalog(ROOT, runtime_commit)
    manifest.update(cache_record_bytes=RECORD_BYTES, cache_detector_epoch=DETECTOR_EPOCH,
                    cache_catalog_path='native-cache/catalog.json', cache_catalog_sha256=sha(catalog),
                    runtime_mode_choices={'normal': 0, 'profile': 2, 'hooks-only': 4,
                                          'raw-chain': 5, 'evolution-trace': 6})
    files.update({'base/KUI/apps/games/retail-boot.kui': module,
                  'base/native-persistent-personalization.json': wire(manifest)})
    layout = dict(variant=name, runtime_source_commit=runtime_commit, artifact_sha256=artifacts,
                  module_sha256=sha(module), lease_bytes=393216, worker_bytes=131072,
                  bottom_reservation=True, relocation_table_matches_linked_elf=True,
                  physical_loads=len(loads), bss_stack_margin=stack_low-bss_end,
                  controller_tick_off=demand_only and name == 'reader-only',
                  prefetch_enabled=name == 'reader-only' and not demand_only,
                  canonical_disjoint_personalization=manifest)
    files['variant-layout.json'] = wire(layout)
    return files, manifest, layout


def previous_package(path):
    """Pin the one earlier console test whose immutable on-card backups we reuse."""
    require(sha(path.read_bytes()) == PREVIOUS_ARCHIVE_SHA, 'Previous archive differs from the delivered995 pair')
    with zipfile.ZipFile(path) as archive:
        raw = archive.read('isolation.json')
        require(sha(raw) == PREVIOUS_METADATA_SHA, 'Previous package is not the delivered995 isolation pair')
        old = json.loads(raw)
        require(old['build'] == PREVIOUS_BUILD and old['gui'] == dict(build=GUI_BUILD, sha256=GUI_SHA),
                'Previous isolation pair has a different GUI/build')
        descriptors = {}
        for name in MODULES:
            module = archive.read('variants/'+name+'/base/KUI/apps/games/retail-boot.kui')
            template = template_profile()
            require(sha(module) == old['variants'][name]['module_sha256'] and
                    verify(module)['build'] == PREVIOUS_BUILD and module.count(template) == 1,
                    'Previous isolation module differs from its pinned delivery: '+name)
            descriptors[name] = dict(module_bytes=len(module), module_sha256=sha(module),
                                     profile_module_offset=module.index(template))
    return dict(gui_sha256=GUI_SHA, isolation_metadata_sha256=PREVIOUS_METADATA_SHA,
                variants=descriptors), raw


def extracted_install_check(files, previous_archive=None):
    sys.path.insert(0, str(ROOT/'tests'))
    from test_personalize_vmu_native_persistent import pair
    with tempfile.TemporaryDirectory(prefix='kui-native-isolation-cli-') as directory:
        root = Path(directory)
        package, card = root/'package', root/'card'
        card.mkdir()
        for name, data in files.items():
            if name.startswith(('source/', 'evidence/')):
                continue
            path = package/name;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(data)
        profile = pair(card, 'p0005', number='0005')
        (card/GUI).parent.mkdir(exist_ok=True)
        (card/GUI).write_bytes(files['baseline/runtime.kui'])
        for name, path in MODULES.items():
            (card/path).parent.mkdir(parents=True, exist_ok=True)
            (card/path).write_bytes(('original '+name).encode())
        settings = card/'KUI/apps/games/launch-settings-a.bin';settings.write_bytes(b'preserved launch settings')
        before = {str(path.relative_to(card)): path.read_bytes() for path in card.rglob('*') if path.is_file()}
        argv = [sys.executable, str(package/'install.py'), str(card), '--profile', str(profile.relative_to(card))]
        for variant in ('reader-only', 'reader-only', 'vmu-only', 'reader-only', 'restore', 'restore'):
            result = subprocess.run([*argv, '--variant', variant], cwd=root, text=True, capture_output=True)
            require(result.returncode == 0, 'Extracted isolation installer failed: '+result.stderr)
            after = {str(path.relative_to(card)): path.read_bytes() for path in card.rglob('*') if path.is_file()}
            require(all(after[key] == value for key, value in before.items() if key not in map(str, MODULES.values())),
                    'Isolation installer changed GUI, profile, settings or prepared saves')
            for name, path in MODULES.items():
                if variant == name:
                    manifest = json.loads(files['variants/'+name+'/base/native-persistent-personalization.json'])
                    data = after[str(path)]
                    require(verify(data)['build'] == manifest['build'] and
                            data[manifest['profile_module_offset']:manifest['profile_module_offset']+4096] == profile.read_bytes() and
                            decode_mode(data[manifest['runtime_mode_module_offset']:manifest['runtime_mode_module_offset']+MODE_BYTES]) == 0,
                            'Installed isolated module lost its profile/NORMAL mode/build')
                else:
                    require(after[str(path)] == before[str(path)], 'Inactive module was not restored to its exact baseline')
            require(all(key in before or key.startswith(str(BACKUP_PARENT)+'/') for key in after),
                    'Unexpected isolation installer output path')
        if previous_archive is not None:
            previous = root/'previous-package'
            with zipfile.ZipFile(previous_archive) as archive:
                for name in archive.namelist():
                    if name in ('install.py', 'personalize.py', 'isolation.json') or name.startswith(('tools/', 'variants/')):
                        path = previous/name;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(archive.read(name))
            for earlier_variant in MODULES:
                # Each temporary card scenario starts with the true pre-test baseline.
                shutil.rmtree(card/BACKUP_PARENT)
                previous_argv = [sys.executable, str(previous/'install.py'), str(card),
                                 '--profile', str(profile.relative_to(card)), '--variant', earlier_variant]
                result = subprocess.run(previous_argv, cwd=root, text=True, capture_output=True)
                require(result.returncode == 0, 'Earlier995 installer fixture failed: '+result.stderr)
                baseline = {p.name: p.read_bytes() for p in (card/BACKUP_PARENT/'backup').iterdir()}
                for variant in ('reader-only', 'vmu-only', 'restore'):
                    result = subprocess.run([*argv, '--variant', variant], cwd=root, text=True, capture_output=True)
                    require(result.returncode == 0, 'Extracted995-to-demand migration failed: '+result.stderr)
                    require({p.name: p.read_bytes() for p in (card/BACKUP_PARENT/'backup').iterdir()} == baseline,
                            'Migration rewrote the original995 immutable backups')
                after = {str(p.relative_to(card)): p.read_bytes() for p in card.rglob('*') if p.is_file()}
                require(all(after[key] == value for key, value in before.items()),
                        'Migration/restore changed original modules, GUI, prepared saves or settings')
    return dict(extracted_cli_runs=6, both_variants_switch_and_restore=True,
                previous_package_migration_cli_runs=8 if previous_archive is not None else 0,
                previous_original_backup_unchanged=previous_archive is not None,
                inactive_module_restored=True, original_modules_restored=True,
                gui_profile_card_journal_and_launch_settings_unchanged=True, normal_mode_only=True)


def checkout_script():
    return b'''#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess
here=pathlib.Path(__file__).resolve().parent
root=here.parent if here.name=='source' else here
item=json.loads((root/'CANDIDATE.json').read_text())
full=here/'native-source-full.bundle'
info=item['source_full_bundle']
if full.stat().st_size!=info['bytes'] or hashlib.sha256(full.read_bytes()).hexdigest()!=info['sha256']:
    raise SystemExit('Full source bundle checksum mismatch')
dest=root/'restored-source'
def git(*a):return subprocess.check_output(['git','-C',str(dest),*a],text=True).strip()
if dest.is_symlink():raise SystemExit('Refusing a symlink source destination')
if dest.exists():
    top=subprocess.run(['git','-C',str(dest),'rev-parse','--show-toplevel'],text=True,capture_output=True)
    if top.returncode or pathlib.Path(top.stdout.strip()).resolve()!=dest:
        raise SystemExit('Destination is not a source repository root')
    if git('status','--porcelain'):raise SystemExit('Refusing a dirty source checkout')
    subprocess.run(['git','-C',str(dest),'fetch',str(full),'HEAD'],check=True)
else:subprocess.run(['git','clone','--no-checkout',str(full),str(dest)],check=True)
if git('rev-parse',item['source_commit']+'^{tree}')!=item['source_tree']:
    raise SystemExit('Source tree mismatch')
subprocess.run(['git','-C',str(dest),'checkout','--detach',item['source_commit']],check=True)
if git('status','--porcelain'):raise SystemExit('Restored source is not clean')
print('Verified offline isolation delivery source',item['source_commit'],dest)
print('Runtime build commit:',item['runtime_source_commit'])
'''


def package(args):
    require(not git('status', '--porcelain'), 'Commit delivery tools/tests/docs before packaging')
    delivery_commit, delivery_tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    linked_raw = args.linked_report.read_bytes();linked = json.loads(linked_raw)
    require(linked.get('schema') == 'kui.native.isolation-linked-sdk.v1' and linked.get('status') == 'PASS',
            'Requires the actual original-SDK isolation execution report')
    runtime_commit = linked['source_commit']
    require(len(runtime_commit) == 40 and linked['build'] == runtime_commit[:12] and
            git('merge-base', '--is-ancestor', runtime_commit, delivery_commit) == '',
            'Runtime freeze is not an ancestor of delivery source')
    changed = git('diff', '--name-only', runtime_commit, delivery_commit).splitlines()
    require(all(name.startswith(('tools/', 'tests/', 'docs/')) for name in changed),
            'Runtime source or Makefiles changed after the isolated builds')
    for path, digest in linked['source_sha256'].items():
        relative = Path(path)
        require(not relative.is_absolute() and '..' not in relative.parts and sha((ROOT/relative).read_bytes()) == digest,
                'Linked isolation auditor/source binding changed: '+path)
    proofs = {row['variant']: row for row in linked['variants']}
    require(set(proofs) == {'reader_only', 'standard_vmu'} and len(linked['variants']) == 2,
            'Isolation execution report lacks both variants')
    gui = args.gui.read_bytes()
    require(sha(gui) == GUI_SHA and verify(gui)['build'] == GUI_BUILD, 'Wrong preserved 8adf Games UI')
    metadata = dict(schema='kui.native.isolation.delivery.v1', source_commit=runtime_commit,
                    runtime_source_commit=runtime_commit, delivery_source_commit=delivery_commit,
                    build=runtime_commit[:12], gui=dict(build=GUI_BUILD, sha256=GUI_SHA), variants={})
    files = {'install.py': (ROOT/'tools/install_vmu_native_isolation.py').read_bytes(),
             'personalize.py': (ROOT/'tools/personalize_vmu_native_persistent.py').read_bytes(),
             'baseline/runtime.kui': gui, 'evidence/isolation-linked-sdk.json': linked_raw,
             'evidence/host-tests.log': args.host_log.read_bytes(),
             'LICENSE': (ROOT/'LICENSE').read_bytes(), 'THIRD_PARTY.md': (ROOT/'THIRD_PARTY.md').read_bytes()}
    if args.demand_only:
        compatible, previous_raw = previous_package(args.previous_package)
        metadata['compatible_packages'] = {PREVIOUS_BUILD: compatible}
        files['evidence/previous-isolation.json'] = previous_raw
    extra_proofs = EXTRA_PROOFS + (('standard-control-comparison.json',
        'tick-off-stack-mapping-negatives.json') if args.demand_only else ())
    for filename in extra_proofs:
        data = (args.linked_report.parent/filename).read_bytes()
        json.loads(data)
        files['evidence/'+filename] = data
    if args.demand_only:
        standard = json.loads(files['evidence/standard-control-comparison.json'])
        require(standard.get('old_build') == PREVIOUS_BUILD and
                standard.get('new_build') == runtime_commit[:12] and
                standard.get('relocation_records_equal') is True and
                len(standard.get('components', [])) == 8 and
                all(row.get('normalized_equal') is True and
                    row.get('new_sha256') == sha((args.vmu_build/row['name']).read_bytes())
                    for row in standard['components']),
                'Passing Standard control comparison differs from final components')
    comparison = json.loads(files['evidence/reader-component-comparison.json'])
    require(set(comparison) == {'resident-sci.bin', 'resident-legacy.bin', 'native-ra-aux.bin', 'native-ra-aux.reloc'} and
            all(item['identical_after_build_label_normalization'] is True and
                item['new_sha256'] == sha((args.reader_build/name).read_bytes())
                for name, item in comparison.items()), 'Reader preservation proof is detached from final components')
    for filename in ('runtime_package.py', 'vmu_persist_profile.py', 'vmu_native_cache_seed.py',
                     'vmu_native_mode.py', 'install_vmu_native_dual.py'):
        files['tools/'+filename] = (ROOT/'tools'/filename).read_bytes()
    for name, build, proof_name in (('reader-only', args.reader_build, 'reader_only'),
                                     ('vmu-only', args.vmu_build, 'standard_vmu')):
        proof = proofs[proof_name]
        require(proof['lease_bytes'] == 393216 and proof['worker_bytes'] == 131072 and proof['placement'] == 'bottom',
                'Isolation controls do not retain the matching memory reservation')
        variant_files, manifest, layout = bind_variant(name, build.resolve(), proof, runtime_commit, args.demand_only)
        files.update({'variants/'+name+'/'+path: data for path, data in variant_files.items()})
        metadata['variants'][name] = dict(directory='variants/'+name, destination=str(MODULES[name]),
            module_sha256=manifest['module_sha256'], virtual_a2=name == 'vmu-only',
            reader='New (X)' if name == 'reader-only' else 'Standard', lease_bytes=393216,
            support_hooks_retained=True, prefetch_enabled=name == 'reader-only' and not args.demand_only,
            controller_tick_off=args.demand_only and name == 'reader-only',
            linked_layout_sha256=sha(wire(layout)))
    files['isolation.json'] = wire(metadata)
    guide_path = 'docs/native-isolation-demand-test.md' if args.demand_only else 'docs/native-isolation-test.md'
    guide = (ROOT/guide_path).read_text().replace('@BUILD@', runtime_commit[:12]).replace(
        '@RUNTIME_COMMIT@', runtime_commit).replace('@DELIVERY_COMMIT@', delivery_commit).encode()
    files['README-FIRST.md'] = guide
    delivery_checks = extracted_install_check(files, args.previous_package if args.demand_only else None)
    with tempfile.TemporaryDirectory(prefix='kui-isolation-source-') as directory:
        bundle = Path(directory)/'native-source-full.bundle'
        subprocess.run(['git', 'bundle', 'create', str(bundle), 'HEAD'], cwd=ROOT, check=True, capture_output=True)
        subprocess.run(['git', 'bundle', 'verify', str(bundle)], cwd=ROOT, check=True, capture_output=True)
        full = bundle.read_bytes()
    full_info = dict(path='source/native-source-full.bundle', bytes=len(full), sha256=sha(full), prerequisites=[])
    candidate = dict(schema='kui.native.isolation.candidate.v1', source_commit=delivery_commit,
                     source_tree=delivery_tree, runtime_source_commit=runtime_commit,
                     runtime_build=runtime_commit[:12], source_full_bundle=full_info,
                     runtime_files_unchanged_after_freeze=True, console_compatibility_verified=False,
                     demand_only=args.demand_only,
                     preserved_gui_build=GUI_BUILD, preserved_gui_sha256=GUI_SHA,
                     evidence_sha256={name: sha(data) for name, data in files.items()
                                      if name.startswith('evidence/') or name.endswith('/variant-layout.json')})
    script = checkout_script()
    delivery_checks['offline_source_recovery'] = offline_source_recovery_check(candidate, script, full)
    files.update({'CANDIDATE.json': wire(candidate), 'delivery-checks.json': wire(delivery_checks),
                  'source/native-source-full.bundle': full, 'source/checkout-source.py': script,
                  'source/SOURCE-RECOVERY.md': ('Run `python3 source/checkout-source.py` from the extracted folder.\n'
                      'Delivery source: '+delivery_commit+'\nRuntime source: '+runtime_commit+'\n'
                      'Both commits are in the complete offline bundle.\n').encode()})
    files['SHA256SUMS.txt'] = checksums(files)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    require(archive_files(args.output) == files and args.output.stat().st_size < 50*1024*1024,
            'Final isolation archive differs or exceeds the delivery size bound')
    require(git('rev-parse', 'HEAD') == delivery_commit and not git('status', '--porcelain'),
            'Delivery source changed during packaging')
    return dict(zip=str(args.output.resolve()), bytes=args.output.stat().st_size,
                sha256=sha(args.output.read_bytes()), runtime_source_commit=runtime_commit,
                delivery_source_commit=delivery_commit, delivery_checks=delivery_checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reader-build', type=Path, required=True)
    parser.add_argument('--vmu-build', type=Path, required=True)
    parser.add_argument('--linked-report', type=Path, required=True)
    parser.add_argument('--gui', type=Path, required=True)
    parser.add_argument('--host-log', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--demand-only', action='store_true',
                        help='Require the New-demand/controller-prefetch-off control and its guide')
    parser.add_argument('--previous-package', type=Path,
                        default=ROOT.parent/'K-UI-Evolution-Isolation-9956562f91d6.zip',
                        help='Pinned earlier995 archive for original-backup compatibility')
    args = parser.parse_args()
    try:
        print(json.dumps(package(args), indent=2))
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'Isolation package refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
