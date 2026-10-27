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
DEMAND_BUILD = '3f51ded6973f'
WAIT_BUILD = '5fa97d6729c8'
BYPASS_BUILD = 'cec542aefa8a'
DELAY_BUILD = '1b2d79400abd'
CACHED_BUILD = '5e4663cdaeeb'
KNOWN_PREVIOUS = {
    PREVIOUS_BUILD: (PREVIOUS_ARCHIVE_SHA, PREVIOUS_METADATA_SHA),
    DEMAND_BUILD: ('2ff9b0c356b535bdf3fa1083b9c65d1fd8b8bf346170b8f2eae6ecd42e2b673f',
                   '7e79d4e257443211b5f028796a74b14846d5c6aea46a4a5b21643b5357a79443'),
    WAIT_BUILD: ('40fd60c6a7631f4373fb428472754a8cfce73bbc00b1c2789d188eadd1d14c4d',
                 '64b9b858409599c2a6380c0aa0d93647a0455c136c83ab2dace2085a0860a21c'),
    BYPASS_BUILD: ('c0515ebb1ab6a30d0d76c533b3d7f8e9799d7036d5f2e8a56fdaeccc8c658f5d',
                   'abc31c8ee2a5cc810ce70dc2bfebed2b712fd63d4b0e213255c464b8bf57964e'),
    DELAY_BUILD: ('ccceb1172ee21107b00a619e28bbc28245417c3074103821bb5492ffdc7414f8',
                  '605cd998ca41d27c6ca53834ce2d6389bd8f0ae3fede3cc3f020d37abf955ce7'),
    CACHED_BUILD: ('36c633cf930f3df7332c0ec5844987628942a053b82e38e8cc327629fd8ff2e9',
                   'ae3064403230ec391186f4207b4077c8e781901ab06019e840672321cb2f1af2'),
}


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


def bind_variant(name, build, proof, runtime_commit, demand_only=False, wait_capture=False,
                 maple_bypass=False, maple_delay=False, cached_metadata=False,completion_delay=False):
    require(not wait_capture or demand_only, 'Wait capture requires the demand-only control')
    require(not maple_bypass or (demand_only and not wait_capture),
            'Maple bypass requires demand-only and excludes wait capture')
    require(not maple_delay or maple_bypass, 'Maple delay requires Maple C bypass')
    require(not completion_delay or maple_delay,'Completion-only delay requires Maple delay')
    require(not cached_metadata or (demand_only and not wait_capture and not maple_bypass and not maple_delay),
            'Cached metadata requires demand-only real callbacks without other diagnostics')
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
    stem = ('metadata_cache' if cached_metadata else 'reader_only') if name == 'reader-only' else 'standard_vmu'
    for image, side in ((stage, 'stage'), (worker, 'worker')):
        require(image[4].get('__kui_native_'+stem+'_isolation_'+side) == ABSOLUTE_ONE and
                all('__kui_native_'+other+'_isolation_'+side not in image[4]
                    for other in {'reader_only','standard_vmu','metadata_cache'}-{stem}) and
                image[4].get('__kui_native_isolation_lease_bytes') == (393216, 0, 0, 0xfff1) and
                image[4].get('__kui_native_bottom_heap_'+side) == ABSOLUTE_ONE,
                'Variant lacks its exact isolated bottom-reservation capabilities: '+name)
        marker = '__kui_native_ra_tick_off_isolation_'+side
        require(image[4].get(marker) == ABSOLUTE_ONE if demand_only and name == 'reader-only' else
                marker not in image[4], 'Variant controller-prefetch marker differs: '+name)
        capture_marker = '__kui_native_e1_wait_capture_'+side
        require(image[4].get(capture_marker) == ABSOLUTE_ONE if wait_capture and name == 'reader-only' else
                capture_marker not in image[4], 'Variant focused-wait-capture marker differs: '+name)
        bypass_marker = '__kui_native_maple_bypass_isolation_'+side
        require(image[4].get(bypass_marker) == ABSOLUTE_ONE if maple_bypass and name == 'reader-only' else
                bypass_marker not in image[4], 'Variant Maple C-gateway bypass marker differs: '+name)
        delay_marker = '__kui_native_maple_delay_isolation_'+side
        require(image[4].get(delay_marker) == ABSOLUTE_ONE if maple_delay and name == 'reader-only' else
                delay_marker not in image[4], 'Variant Maple delay marker differs: '+name)
        complete_marker = '__kui_native_maple_delay_complete_only_isolation_'+side
        require(image[4].get(complete_marker)==ABSOLUTE_ONE if completion_delay and name=='reader-only' else
                complete_marker not in image[4], 'Variant completion-only delay marker differs: '+name)
    require(proof.get('controller_tick_off', False) is (demand_only and name == 'reader-only'),
            'Linked execution report controller-prefetch setting differs: '+name)
    require(proof.get('e1_wait_capture', False) is (wait_capture and name == 'reader-only'),
            'Linked execution report focused-wait-capture setting differs: '+name)
    require(proof.get('maple_bypass',False) is (maple_bypass and name=='reader-only'),
            'Linked execution report Maple C-gateway bypass setting differs: '+name)
    require(proof.get('maple_delay',False) is (maple_delay and name=='reader-only'),
            'Linked execution report Maple delay setting differs: '+name)
    require(proof.get('cached_metadata',False) is (cached_metadata and name=='reader-only'),
            'Linked execution report cached-metadata setting differs: '+name)
    require(proof.get('completion_delay',False) is (completion_delay and name=='reader-only'),
            'Linked execution report completion-only delay setting differs: '+name)
    if cached_metadata:
        require(proof.get('virtual_a2') is True and proof.get('maple_c_callbacks_enabled') is True,
                'Combined report lacks real VMU callbacks: '+name)
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
                  e1_wait_capture=wait_capture and name == 'reader-only',
                  maple_bypass=maple_bypass and name=='reader-only',
                  maple_delay=maple_delay and name=='reader-only',
                  maple_delay_ticks=dict(submit=0 if completion_delay else 59,complete=76)
                    if maple_delay and name=='reader-only' else None,
                  completion_delay=completion_delay and name=='reader-only',
                  maple_c_callbacks_enabled=not(maple_bypass and name=='reader-only'),
                  cached_metadata=cached_metadata and name=='reader-only',
                  public_variant='combined' if cached_metadata and name=='reader-only' else name,
                  virtual_a2=name=='vmu-only' or cached_metadata,
                  heap_reclaim_coherence_guards_retained=True,
                  canonical_disjoint_personalization=manifest)
    files['variant-layout.json'] = wire(layout)
    return files, manifest, layout


def previous_package(path, expected_build=PREVIOUS_BUILD):
    """Pin an earlier console test and its already pinned backup-origin chain."""
    archive_sha, metadata_sha = KNOWN_PREVIOUS[expected_build]
    require(sha(path.read_bytes()) == archive_sha, 'Previous archive differs from the delivered isolation pair')
    with zipfile.ZipFile(path) as archive:
        raw = archive.read('isolation.json')
        require(sha(raw) == metadata_sha, 'Previous package is not the pinned isolation pair')
        old = json.loads(raw)
        require(old['build'] == expected_build and old['gui'] == dict(build=GUI_BUILD, sha256=GUI_SHA),
                'Previous isolation pair has a different GUI/build')
        descriptors = {}
        for name in MODULES:
            module = archive.read('variants/'+name+'/base/KUI/apps/games/retail-boot.kui')
            template = template_profile()
            require(sha(module) == old['variants'][name]['module_sha256'] and
                    verify(module)['build'] == expected_build and module.count(template) == 1,
                    'Previous isolation module differs from its pinned delivery: '+name)
            descriptors[name] = dict(module_bytes=len(module), module_sha256=sha(module),
                                     profile_module_offset=module.index(template))
    return dict(gui_sha256=GUI_SHA, isolation_metadata_sha256=metadata_sha,
                variants=descriptors), raw


def passed_bypass_bridge(path, metadata, files):
    """Retain the exact passed modules with a metadata-only return bridge."""
    _, raw = previous_package(path, BYPASS_BUILD)
    old = json.loads(raw)
    descriptor = dict(gui_sha256=GUI_SHA, isolation_metadata_sha256=sha(wire(metadata)), variants={})
    for name in MODULES:
        prefix = 'variants/'+name+'/'
        manifest = json.loads(files[prefix+'base/native-persistent-personalization.json'])
        module = files[prefix+'base/KUI/apps/games/retail-boot.kui']
        offset = manifest['profile_module_offset']
        require(verify(module)['build'] == metadata['build'] and
                sha(module) == metadata['variants'][name]['module_sha256'] and
                module[offset:offset+4096] == template_profile(),
                'Passed-build bridge lacks canonical current-module/profile binding: '+name)
        descriptor['variants'][name] = dict(module_bytes=len(module), module_sha256=sha(module),
                                             profile_module_offset=offset)
    old['compatible_packages'] = {**old.get('compatible_packages', {}), metadata['build']: descriptor}
    bridge = {}
    with zipfile.ZipFile(path) as archive:
        for name in archive.namelist():
            if name in ('install.py', 'personalize.py') or name.startswith(('tools/', 'variants/', 'native-cache/')):
                bridge['passed-bypass/'+name] = archive.read(name)
    bridge['passed-bypass/isolation.json'] = wire(old)
    require({key:value for key,value in old.items() if key!='compatible_packages'} ==
            {key:value for key,value in json.loads(raw).items() if key!='compatible_packages'} and
            {key:value for key,value in old['compatible_packages'].items() if key!=metadata['build']} ==
            json.loads(raw).get('compatible_packages', {}),
            'Passed-build bridge changed more than the current-build compatibility descriptor')
    info = dict(passed_build=BYPASS_BUILD, passed_archive_sha256=KNOWN_PREVIOUS[BYPASS_BUILD][0],
                original_metadata_sha256=sha(raw), bridge_metadata_sha256=sha(wire(old)),
                current_metadata_sha256=sha(wire(metadata)), current_descriptor=descriptor,
                original_installer_personalizer_tools_and_variants_unchanged=True,
                source_bundle_duplicated=False,
                files_sha256={name:sha(data) for name,data in bridge.items()})
    return bridge, info


def extracted_install_check(files, previous_archive=None, origin_archive=None):
    sys.path.insert(0, str(ROOT/'tests'))
    from test_personalize_vmu_native_persistent import pair
    combined = json.loads(files['isolation.json']).get('cached_metadata',False)
    selected = 'combined' if combined else 'reader-only'
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
        for variant in (selected, selected, 'vmu-only', selected, 'restore', 'restore'):
            result = subprocess.run([*argv, '--variant', variant], cwd=root, text=True, capture_output=True)
            require(result.returncode == 0, 'Extracted isolation installer failed: '+result.stderr)
            after = {str(path.relative_to(card)): path.read_bytes() for path in card.rglob('*') if path.is_file()}
            require(all(after[key] == value for key, value in before.items() if key not in map(str, MODULES.values())),
                    'Isolation installer changed GUI, profile, settings or prepared saves')
            for name, path in MODULES.items():
                if ('reader-only' if variant=='combined' else variant) == name:
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
            previous_combined=json.loads((previous/'isolation.json').read_text()).get('cached_metadata',False)
            origin = root/'origin-package'
            if origin_archive is not None:
                with zipfile.ZipFile(origin_archive) as archive:
                    for name in archive.namelist():
                        if name in ('install.py', 'personalize.py', 'isolation.json') or name.startswith(('tools/', 'variants/')):
                            path = origin/name;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(archive.read(name))
            scenarios = [(False, name) for name in MODULES]
            if origin_archive is not None:
                scenarios += [(True, name) for name in MODULES]
            for inherited_backup, earlier_variant in scenarios:
                # Each temporary card scenario starts with the true pre-test baseline.
                shutil.rmtree(card/BACKUP_PARENT)
                if inherited_backup:
                    result = subprocess.run([sys.executable, str(origin/'install.py'), str(card),
                        '--profile', str(profile.relative_to(card)), '--variant', earlier_variant],
                        cwd=root, text=True, capture_output=True)
                    require(result.returncode == 0, 'Original995 installer fixture failed: '+result.stderr)
                    origin_baseline = {p.name: p.read_bytes() for p in (card/BACKUP_PARENT/'backup').iterdir()}
                previous_argv = [sys.executable, str(previous/'install.py'), str(card),
                                 '--profile', str(profile.relative_to(card)), '--variant',
                                 'combined' if previous_combined and earlier_variant=='reader-only' else earlier_variant]
                result = subprocess.run(previous_argv, cwd=root, text=True, capture_output=True)
                require(result.returncode == 0, 'Earlier pinned installer fixture failed: '+result.stderr)
                baseline = {p.name: p.read_bytes() for p in (card/BACKUP_PARENT/'backup').iterdir()}
                require(not inherited_backup or baseline == origin_baseline,
                        'Intermediate isolation migration rewrote the original995 backups')
                for variant in (selected, 'vmu-only', 'restore'):
                    result = subprocess.run([*argv, '--variant', variant], cwd=root, text=True, capture_output=True)
                    require(result.returncode == 0, 'Extracted pinned-package migration failed: '+result.stderr)
                    require({p.name: p.read_bytes() for p in (card/BACKUP_PARENT/'backup').iterdir()} == baseline,
                            'Migration rewrote the original995 immutable backups')
                after = {str(p.relative_to(card)): p.read_bytes() for p in card.rglob('*') if p.is_file()}
                require(all(after[key] == value for key, value in before.items()),
                        'Migration/restore changed original modules, GUI, prepared saves or settings')
        bridge_runs = 0
        if 'passed-bypass/install.py' in files:
            backup_before = {p.name:p.read_bytes() for p in (card/BACKUP_PARENT/'backup').iterdir()}
            new_metadata = json.loads(files['isolation.json'])
            sequence = ((False,selected),(True,'reader-only'),(False,selected),
                        (True,'reader-only'),(True,'restore'),(False,selected),(False,'restore'))
            for passed, variant in sequence:
                script = package/'passed-bypass/install.py' if passed else package/'install.py'
                result = subprocess.run([sys.executable,str(script),str(card),'--profile',
                    str(profile.relative_to(card)),'--variant',variant],cwd=root,text=True,capture_output=True)
                require(result.returncode==0,'Passed-build return bridge failed: '+result.stderr)
                bridge_runs += 1
                require({p.name:p.read_bytes() for p in (card/BACKUP_PARENT/'backup').iterdir()}==backup_before,
                        'Return bridge changed immutable original backups')
                after = {str(p.relative_to(card)):p.read_bytes() for p in card.rglob('*') if p.is_file()}
                require(all(after[key]==value for key,value in before.items()
                            if key not in map(str,MODULES.values())),
                        'Return bridge changed GUI, prepared profile/card/journal or launch settings')
                for name,destination in MODULES.items():
                    if ('reader-only' if variant=='combined' else variant) != name:
                        require(after[str(destination)]==before[str(destination)],
                                'Return bridge did not restore exact inactive/original module')
                        continue
                    prefix = ('passed-bypass/' if passed else '')+'variants/'+name+'/'
                    manifest = json.loads(files[prefix+'base/native-persistent-personalization.json'])
                    data = after[str(destination)];offset = manifest['profile_module_offset']
                    expected_build = BYPASS_BUILD if passed else new_metadata['build']
                    require(verify(data)['build']==expected_build and
                            data[offset:offset+4096]==profile.read_bytes() and
                            decode_mode(data[manifest['runtime_mode_module_offset']:
                                             manifest['runtime_mode_module_offset']+MODE_BYTES])==0,
                            'Return bridge selected the wrong build/profile/mode')
                    payload = bytearray(data[64:]);payload[offset-64:offset-64+4096] = template_profile()
                    canonical = envelope(bytes(payload),verify(data)['memory_bytes'],expected_build)
                    require(sha(canonical)==manifest['module_sha256'],
                            'Return bridge changed the pinned module beyond profile personalization')
    return dict(extracted_cli_runs=6, both_variants_switch_and_restore=True,
                passed_bypass_bridge_cli_runs=bridge_runs,
                bidirectional_delay_bypass_switch_and_restore=bridge_runs==7 and not combined,
                bidirectional_combined_bypass_switch_and_restore=bridge_runs==7 and combined,
                public_combined_variant_executed=combined,
                passed_bypass_bridge_keeps_exact_cec_modules=bridge_runs==7,
                previous_package_migration_cli_runs=8 if previous_archive is not None else 0,
                inherited995_origin_migration_cli_runs=10 if origin_archive is not None else 0,
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


def validate_maple_bypass_receipt(gateways,runtime_commit,reader_build):
    require(gateways.get('schema')=='kui.native.maple-bypass.linked-gateways.v1' and
            gateways.get('status')=='PASS' and gateways.get('source_commit')==runtime_commit and
            gateways.get('instruction_cases')==1152 and gateways.get('C_calls')==0 and
            gateways.get('exact_original_submit_replay') is True and
            gateways.get('inherited_registers_PR_SR_SP_preserved') is True and
            gateways.get('gateway_frames_and_stack_traffic_retained') is True and
            gateways.get('all_imasks_and_T') is True and
            gateways.get('mdst_store_counts')==dict(submit_mdst=576,complete_mdst=0) and
            gateways.get('assembler_comparison',{}).get('only_two_C_call_instructions_replaced') is True and
            len(gateways.get('mutation_refusals',[]))==2 and
            {item.get('mutation') for item in gateways['mutation_refusals']}==
                {'submit-C-call-restored','complete-C-call-restored'} and
            all(item.get('reason') for item in gateways['mutation_refusals']) and
            all(gateways.get('artifact_sha256',{}).get(name)==sha((reader_build/name).read_bytes())
                for name in ('stage.elf','native-worker.elf','native-worker.bin','native-worker.reloc')),
            'Compiled Maple bypass gateway receipt differs from final artifacts')


def validate_maple_delay_receipt(gateways,execution,runtime_commit,reader_build,completion_delay=False):
    ticks=dict(submit=0 if completion_delay else 59,complete=76)
    calls=dict(submit=0 if completion_delay else 8,complete=8)
    helper_count=103 if completion_delay else 206
    mutations=({'submit-original-C-restored','complete-call-omitted','complete-original-C-restored'}
        if completion_delay else {role+'-'+kind for role in ('submit','complete')
            for kind in ('call-omitted','original-C-restored')})
    require(execution.get('actual_delay_helper_executed') is True and
            execution.get('delay_calls')==calls and execution.get('delay_ticks')==ticks and
            execution.get('timer_writes')==0 and
            len(execution.get('delay_visits',[]))==(8 if completion_delay else 16) and
            all(v.get('counter_reads',0)==v.get('target_ticks',0)+2 and v.get('target_ticks',0)>0
                for v in execution['delay_visits']),
            'Original SDK replay did not execute the actual read-only delay helpers')
    require(gateways.get('schema')==('kui.native.maple-completion-delay.linked-gateways.v1'
                if completion_delay else 'kui.native.maple-delay.linked-gateways.v1') and
            gateways.get('status')=='PASS' and gateways.get('source_commit')==runtime_commit and
            gateways.get('instruction_cases',0)>=1152 and gateways.get('C_calls')==0 and
            gateways.get('delay_ticks')==ticks and
            gateways.get('actual_delay_helper_executed') is True and
            gateways.get('timer_writes')==0 and
            gateways.get('exact_original_submit_replay') is True and
            gateways.get('inherited_registers_PR_SR_SP_preserved') is True and
            gateways.get('gateway_frames_and_stack_traffic_retained') is True and
            gateways.get('all_imasks_and_T') is True and
            gateways.get('mdst_store_counts')==dict(submit_mdst=576,complete_mdst=0) and
            gateways.get('gateway_callee_model')=='adversarial ABI-compatible delay callee' and
            gateways.get('actual_helper_case_count')==len(gateways.get('delay_helper_cases',[]))==helper_count and
            sum(row.get('refused') is False for row in gateways['delay_helper_cases'])==(3 if completion_delay else 6) and
            sum(row.get('refused') is True for row in gateways['delay_helper_cases'])==(100 if completion_delay else 200) and
            gateways.get('flag_zero_assembly',{}).get('normal_equal') is True and
            gateways.get('flag_zero_assembly',{}).get('bypass_equal') is True and
            len(gateways.get('mutation_refusals',[]))==len(mutations) and
            {row.get('name') for row in gateways['mutation_refusals']}==mutations and
            all(row.get('reason') for row in gateways['mutation_refusals']) and
            all(gateways.get('artifact_sha256',{}).get(name)==sha((reader_build/name).read_bytes())
                for name in ('stage.elf','native-worker.elf','native-worker.bin','native-worker.reloc')),
            'Compiled Maple delay receipt differs from final artifacts')
    if completion_delay:
        comparison=gateways.get('submit_cec_equivalence',{})
        require(execution.get('completion_delay') is True and execution.get('submit_gateway_no_calls') is True and
                gateways.get('completion_delay') is True and
                all(v.get('kind')=='complete' and v.get('target_ticks')==76 for v in execution['delay_visits']) and
                comparison.get('prior_runtime')=='cec542aefa8ad907ed063a6b8b471d7151eef38c' and
                all(comparison.get(key) is True for key in ('object_body_and_relocations_equal',
                    'linked_body_equal_after_exact_relocations','no_submit_helper_call')),
                'Completion-only delay lacks exact CEC submit/no-helper proof')


def validate_maple_delay_guard(guard,runtime_commit,reader_build,
    schema='kui.native.evolution1.maple-delay-stage-guard.v1',label='Maple-delay'):
    positive=guard.get('positive_case',{});refusals=guard.get('refusal_cases',[])
    expected={'digest-byte-'+str(i) for i in range(32)} | {'image-bytes','load-address'} | {
        'non-normal-mode-'+str(i) for i in range(1,7)}
    require(guard.get('schema')==schema and
            guard.get('status')=='PASS' and guard.get('source_commit')==runtime_commit and
            guard.get('enabledmarker') is True and positive.get('mode')==0 and
            positive.get('admitted_before_copy') is True and positive.get('first_copy') is not None and
            positive.get('actual_memcmp_calls')==1 and positive.get('digest_byte_offsets_read')==list(range(32)) and
            guard.get('refusal_count')==len(refusals)==40 and {r.get('name') for r in refusals}==expected and
            all(r.get('refused_before_copy') is True and r.get('first_copy') is None for r in refusals) and
            guard.get('inputs',{}).get('startup',{}).get('sha256')==
                'ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35' and
            all(guard.get('artifact_sha256',{}).get(name)==sha((reader_build/name).read_bytes())
                for name in ('stage.elf','stage.bin','entry.elf','native-worker.elf',
                             'native-worker.bin','resident-sci.elf')),
            'Exact Evolution/NORMAL '+label+' stage guard differs from final artifacts')
    for path,digest in guard['source_sha256'].items():
        relative=Path(path)
        require(not relative.is_absolute() and '..' not in relative.parts and
                sha((ROOT/relative).read_bytes())==digest,label+' stage proof source changed: '+path)


def validate_cached_metadata_publication(report,runtime_commit,reader_build):
    publication=report.get('cache_publication',{})
    layout=report.get('metadata_layout',{})
    require(report.get('schema')=='kui.native.metadata-cache-publication.v1' and
            report.get('status')=='PASS' and report.get('source_commit')==runtime_commit and
            report.get('paired_markers') is True and
            isinstance(layout,dict) and set(layout)=={'config','live','lifetime','hooks'} and
            all(report.get(key) is True for key in ('whole_cache_line_isolation',
                'read_only_p1_views','no_p1_metadata_stores','coherent_live_witnesses')) and
            all(publication.get(key) is True for key in ('ocbi_only','no_ocbp','exact_range',
                'ready_after_all_invalidations','published_after_live_prepare')) and
            all(report.get('artifact_sha256',{}).get(name)==sha((reader_build/name).read_bytes())
                for name in ('entry.elf','stage.elf','native-worker.elf',
                             'native-worker.bin','native-worker.reloc')),
            'Metadata cache publication proof differs from final artifacts or cache contract')
    spans=[]
    for name,item in layout.items():
        require(isinstance(item,dict) and all(type(item.get(key)) is int and item[key]>0
                for key in ('object_bytes','padded_bytes','cache_lines')) and
                all(isinstance(item.get(key),str) for key in
                    ('canonical_p2','cached_p1','p2_begin','p2_end')),
                'Metadata cache layout lacks typed bounds: '+name)
        canonical,cached,begin,end=(int(item[key],16) for key in
                                  ('canonical_p2','cached_p1','p2_begin','p2_end'))
        require(0<=begin<end<=0xffffffff and canonical==begin and begin&0xe0000000==0xa0000000 and
                cached==(canonical&0x1fffffff|0x80000000) and not begin&31 and not end&31 and
                end-begin==item['padded_bytes']==32*item['cache_lines'] and
                0<=item['padded_bytes']-item['object_bytes']<32 and
                all(end<=first or begin>=last for first,last in spans),
                'Metadata cache objects do not own disjoint whole P1/P2 cache lines: '+name)
        spans.append((begin,end))
    sources=report.get('source_sha256',{})
    require({'src/loader/vmu_native_metadata_cache.inc','tools/check_native_metadata_cache.py'}<=set(sources),
            'Metadata cache publication proof omits the implementation or auditor source')
    for path,digest in sources.items():
        relative=Path(path)
        require(not relative.is_absolute() and '..' not in relative.parts and
                sha((ROOT/relative).read_bytes())==digest,'Metadata cache proof source changed: '+path)


def validate_cached_metadata_execution(report):
    packets=report.get('forced_packets',[])
    metadata={row.get('command'):row for row in packets if row.get('command') in (1,2,10)}
    reads=[row for row in packets if row.get('command')==11]
    stats=report.get('final_overlay',{})
    require(report.get('variant')=='metadata_cache' and report.get('cached_metadata') is True and
            report.get('virtual_a2') is True and report.get('maple_c_callbacks_enabled') is True and
            report.get('controller_tick_off') is True and report.get('tick_calls')==0 and
            report.get('maple_bypass') is False and report.get('maple_delay') is False and
            report.get('e1_wait_capture') is False and
            report.get('actual_maple_c_callbacks')==dict(submit=8,complete=8) and
            len(report.get('natural_rounds',[]))==8 and
            all(stats.get(key,0)>0 for key in ('advertisements','backend_calls','devinfo')) and
            set(metadata)=={1,2,10} and len(packets)==4 and
            all(int(metadata[command].get('reply','0'),16)&255==reply and
                metadata[command].get('overlay',{}).get('backend_calls',0)>0 and
                metadata[command].get('storage_boundary_reached') is False
                for command,reply in ((1,5),(2,6),(10,8))) and
            len(reads)==1 and reads[0].get('storage_boundary_reached') is True and
            reads[0].get('success_or_ack_substituted') is False and
            reads[0].get('rx_unpublished') is True and
            reads[0].get('calls',{}).get('actual_low_sd_loan_boundary')==1 and
            report.get('sd_loan_boundary',{}).get('reader')=='New (X)' and
            report.get('worker_helpers_substituted') is False and
            report.get('original_game_or_sdk_functions_substituted') is False,
            'Combined linked SDK proof lacks real callbacks, VMU metadata, or the New SD-loan boundary')


def validate_cached_metadata_traffic(report,runtime_commit,reader_build):
    require(report.get('schema')=='kui.native.metadata-cache-traffic.v1' and
            report.get('status')=='PASS' and report.get('source_commit')==runtime_commit and
            report.get('real_virtual_a2') is True and report.get('real_c_callbacks') is True and
            report.get('controller_tick_calls')==0 and report.get('targeted_cold_metadata_eviction') is True and
            len(report.get('rounds',[]))>=2 and
            all(report.get('artifact_sha256',{}).get(name)==sha((reader_build/name).read_bytes())
                for name in ('entry.elf','stage.elf','native-worker.elf',
                             'native-worker.bin','native-worker.reloc')),
            'Metadata cache traffic proof differs from final VMU/New artifacts')
    for row in report['rounds']:
        variants=row.get('variants',{})
        require(set(variants)=={'canonical_p2','warm_p1','cold_p1'} and
                all(row.get(key) is True for key in ('identical_live_read_order',
                    'identical_live_store_order','identical_sdk_vmu_packets')),
                'Metadata traffic proof lost its matching live SDK workload')
        baseline=variants['canonical_p2']
        for name in ('warm_p1','cold_p1'):
            candidate=variants[name]
            require(candidate.get('counts',{}).get('metadata_p2_reads',0)<
                        baseline.get('counts',{}).get('metadata_p2_reads',0) and
                    candidate.get('counts',{}).get('metadata_p1_reads',0)>0 and
                    all(candidate.get(key)==baseline.get(key) for key in
                        ('live_read_events','live_read_sha256','live_write_events',
                         'live_write_sha256','rx_sha256','tx_sha256')),
                    'Cached metadata traffic did not preserve the live SDK reads, stores and packets')
    sources=report.get('source_sha256',{})
    require('tools/check_native_metadata_traffic.py' in sources,
            'Metadata cache traffic proof omits its auditor source')
    for path,digest in sources.items():
        relative=Path(path)
        require(not relative.is_absolute() and '..' not in relative.parts and
                sha((ROOT/relative).read_bytes())==digest,'Metadata traffic proof source changed: '+path)


def package(args):
    require(not args.wait_capture or args.demand_only, '--wait-capture requires --demand-only')
    require(not args.maple_bypass or (args.demand_only and not args.wait_capture),
            '--maple-bypass requires --demand-only and excludes --wait-capture')
    require(not args.maple_delay or args.maple_bypass, '--maple-delay requires --maple-bypass')
    require(not args.completion_delay or args.maple_delay,'--completion-delay requires --maple-delay')
    require(not args.cached_metadata or (args.demand_only and not args.wait_capture and
                not args.maple_bypass and not args.maple_delay),
            '--cached-metadata requires --demand-only and excludes wait capture, bypass, and delay')
    if args.previous_package is None:
        filename = ('K-UI-Evolution-VMU-New-5e4663cdaeeb.zip' if args.completion_delay else
                    'K-UI-Evolution-Maple-Delay-1b2d79400abd.zip' if args.cached_metadata else
                    'K-UI-Evolution-Maple-Bypass-cec542aefa8a.zip' if args.maple_delay else
                    'K-UI-Evolution-Wait-Capture-5fa97d6729c8.zip' if args.maple_bypass else
                    'K-UI-Evolution-New-Demand-3f51ded6973f.zip' if args.wait_capture else
                    'K-UI-Evolution-Isolation-9956562f91d6.zip')
        args.previous_package = ROOT.parent/filename
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
    reader_proof = 'metadata_cache' if args.cached_metadata else 'reader_only'
    require(set(proofs) == {reader_proof, 'standard_vmu'} and len(linked['variants']) == 2,
            'Isolation execution report lacks both variants')
    gui = args.gui.read_bytes()
    require(sha(gui) == GUI_SHA and verify(gui)['build'] == GUI_BUILD, 'Wrong preserved 8adf Games UI')
    metadata = dict(schema='kui.native.isolation.delivery.v1', source_commit=runtime_commit,
                    runtime_source_commit=runtime_commit, delivery_source_commit=delivery_commit,
                    build=runtime_commit[:12], gui=dict(build=GUI_BUILD, sha256=GUI_SHA), variants={})
    if args.cached_metadata:
        metadata.update(cached_metadata=True,public_variant='combined')
    installer = 'install_vmu_native_cached_metadata.py' if args.cached_metadata else 'install_vmu_native_isolation.py'
    files = {'install.py': (ROOT/'tools'/installer).read_bytes(),
             'personalize.py': (ROOT/'tools/personalize_vmu_native_persistent.py').read_bytes(),
             'baseline/runtime.kui': gui, 'evidence/isolation-linked-sdk.json': linked_raw,
             'evidence/host-tests.log': args.host_log.read_bytes(),
             'LICENSE': (ROOT/'LICENSE').read_bytes(), 'THIRD_PARTY.md': (ROOT/'THIRD_PARTY.md').read_bytes()}
    if args.demand_only:
        prior_build = (CACHED_BUILD if args.completion_delay else DELAY_BUILD if args.cached_metadata else BYPASS_BUILD if args.maple_delay else WAIT_BUILD if args.maple_bypass else
                       DEMAND_BUILD if args.wait_capture else PREVIOUS_BUILD)
        compatible, previous_raw = previous_package(args.previous_package, prior_build)
        metadata['compatible_packages'] = {**json.loads(previous_raw).get('compatible_packages', {}),
                                           prior_build: compatible}
        files['evidence/previous-isolation.json'] = previous_raw
        if args.wait_capture or args.maple_bypass or args.cached_metadata:
            origin, origin_raw = previous_package(args.backup_origin_package)
            require(metadata['compatible_packages'].get(PREVIOUS_BUILD) == origin,
                    'Intermediate package backup origin differs from the pinned995 pair')
            files['evidence/backup-origin-isolation.json'] = origin_raw
    extra_proofs = EXTRA_PROOFS + (('standard-control-comparison.json',)
        if args.demand_only else ()) + (('tick-off-stack-mapping-negatives.json',)
        if args.demand_only and not args.wait_capture and not args.maple_bypass and not args.cached_metadata else ()) + (
        ('e1-wait-capture-collector.json', 'e1-wait-capture-image-guard.json',
         'original-frame-proof.json') if args.wait_capture else ()) + (
        ('maple-completion-delay-gateways.json','maple-completion-delay-stage-guard.json',
         'completion-only-independent-review.json') if args.completion_delay else
        ('maple-delay-gateways.json','maple-delay-stage-guard.json') if args.maple_delay else
        ('maple-bypass-gateways.json',) if args.maple_bypass else ()) + (
        ('metadata-cache-publication.json','metadata-cache-stage-guard.json',
         'metadata-cache-traffic.json') if args.cached_metadata else ())
    for filename in extra_proofs:
        data = (args.linked_report.parent/filename).read_bytes()
        json.loads(data)
        files['evidence/'+filename] = data
    if args.cached_metadata:
        validate_cached_metadata_execution(proofs[reader_proof])
        validate_cached_metadata_publication(json.loads(files['evidence/metadata-cache-publication.json']),
                                             runtime_commit,args.reader_build)
        validate_cached_metadata_traffic(json.loads(files['evidence/metadata-cache-traffic.json']),
                                         runtime_commit,args.reader_build)
        validate_maple_delay_guard(json.loads(files['evidence/metadata-cache-stage-guard.json']),
            runtime_commit,args.reader_build,'kui.native.evolution1.metadata-cache-stage-guard.v1','Metadata-cache')
        files['evidence/native-evolution1-cec542-console-pass-2026-10-26.json']=(
            ROOT/'docs/evidence/native-evolution1-cec542-console-pass-2026-10-26.json').read_bytes()
        files['evidence/native-evolution1-1b2d-delay-console-failure-2026-10-26.json']=(
            ROOT/'docs/evidence/native-evolution1-1b2d-delay-console-failure-2026-10-26.json').read_bytes()
    if args.wait_capture:
        collector = json.loads(files['evidence/e1-wait-capture-collector.json'])
        require(collector.get('schema') == 'kui.native.e1-wait-capture-collector.v1' and
                collector.get('status') == 'PASS' and collector.get('source_commit') == runtime_commit and
                collector.get('paired_markers') is True and
                collector.get('collector_layout') == dict(record_bytes=68, capture_bytes=728, phase_bytes=908) and
                collector.get('host_tests') == dict(checks=9442, status='PASS', undefined_behavior_sanitizer=True) and
                collector.get('normal_pre_gesture') == dict(polls=8, capture_attempts=0,
                    captured_contexts=0, capture_ready=0, controller_tick_calls=0, normal_diagnostics=0) and
                all(collector.get('artifact_sha256', {}).get(name) == sha((args.reader_build/name).read_bytes())
                    for name in ('native-worker.elf', 'native-worker.bin', 'native-worker.reloc', 'stage.elf')),
                'Focused capture collector/normal-path proof differs from final artifacts')
        frame = json.loads(files['evidence/original-frame-proof.json'])
        require(frame.get('schema') == 'kui.native.evolution1.original-wait-frame.v1' and
                frame.get('input', {}).get('bytes') == 2143652 and
                frame.get('input', {}).get('sha256') ==
                    'ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35' and
                frame.get('generic_frame_bytes') == 228 and
                frame.get('complete_offsets') == dict(R0=312, R4=328, R12=360, R13=364, R14=368,
                    ISR_SR=308, VBR=380, PR=388, SPC=392, SSR=396) and
                frame.get('case_counts') == dict(prologues=12, shared_prefix=18, polling=12, refusals=10) and
                all(row.get('refused') is True for row in frame.get('mutation_refusals', [])),
                'Original Evolution frame/poll proof differs from the focused collector contract')
        guard = json.loads(files['evidence/e1-wait-capture-image-guard.json'])
        positive = guard.get('positive_case', {})
        refusals = guard.get('refusal_cases', [])
        expected_refusals = {'digest-byte-'+str(i) for i in range(32)} | {'image-bytes', 'load-address'} | {
            'non-normal-mode-'+str(i) for i in range(1, 7)}
        require(guard.get('schema') == 'kui.native.evolution1.wait-capture-stage-guard.v1' and
                guard.get('status') == 'PASS' and guard.get('source_commit') == runtime_commit and
                guard.get('enabledmarker') is True and positive.get('mode') == 0 and
                positive.get('admitted_before_copy') is True and positive.get('first_copy') is not None and
                positive.get('actual_memcmp_calls') == 1 and positive.get('digest_byte_offsets_read') == list(range(32)) and
                guard.get('refusal_count') == len(refusals) == 40 and
                {row.get('name') for row in refusals} == expected_refusals and
                all(row.get('refused_before_copy') is True and row.get('first_copy') is None for row in refusals) and
                guard.get('inputs', {}).get('startup', {}).get('sha256') == frame['input']['sha256'] and
                all(guard.get('artifact_sha256', {}).get(name) == sha((args.reader_build/name).read_bytes())
                    for name in ('stage.elf', 'stage.bin', 'entry.elf', 'native-worker.elf',
                                 'native-worker.bin', 'resident-sci.elf')),
                'Exact Evolution/NORMAL stage guard proof differs from final artifacts')
        for report in (collector, frame, guard):
            for path, digest in report['source_sha256'].items():
                relative = Path(path)
                require(not relative.is_absolute() and '..' not in relative.parts and
                        sha((ROOT/relative).read_bytes()) == digest, 'Focused wait proof source changed: '+path)
    if args.maple_bypass:
        execution=proofs['reader_only'].get('maple_bypass_execution',{})
        require(execution.get('polls')==8 and execution.get('actual_patched_submit_executed') is True and
                execution.get('actual_completion_gateway_and_original_consumer_executed') is True and
                execution.get('maple_c_callbacks_executed')==execution.get('submitted')==
                execution.get('completed')==execution.get('pending')==0 and
                execution.get('assembly_frames_and_masking_retained') is True and
                execution.get('forced_virtual_packet_injection') is False and
                set(execution.get('gateway_instructions',{}))=={'submit','complete'} and
                all(value>0 for value in execution['gateway_instructions'].values()),
                'Bypass report did not execute the actual SDK through both compiled assembly gateways')
        gateways=json.loads(files['evidence/'+('maple-completion-delay-gateways.json' if args.completion_delay else
                                              'maple-delay-gateways.json' if args.maple_delay else
                                              'maple-bypass-gateways.json')])
        if args.maple_delay:
            validate_maple_delay_receipt(gateways,execution,runtime_commit,args.reader_build,args.completion_delay)
            if args.completion_delay:
                guard=json.loads(files['evidence/maple-completion-delay-stage-guard.json'])
                require(guard.get('completion_delay') is True,'Stage proof is not completion-only delay')
                validate_maple_delay_guard(guard,runtime_commit,args.reader_build,
                    'kui.native.evolution1.maple-completion-delay-stage-guard.v1','Maple completion-delay')
                review=json.loads(files['evidence/completion-only-independent-review.json'])
                require(review.get('schema')=='kui.native.maple-complete-delay.independent-review.v1' and
                        review.get('status')=='PASS' and review.get('source_commit')==runtime_commit and
                        all(review.get('artifact_sha256',{}).get(name)==sha((args.reader_build/name).read_bytes())
                            for name in ('entry.elf','stage.elf','native-worker.elf','native-worker.bin','native-worker.reloc')) and
                        all(review.get('review',{}).get(key) is True for key in
                            ('submit_has_no_call_or_timer_access',
                             'submit_original_mdst_replay_and_sr_stack_sequence_equal_passing_cec',
                             'completion_frame_mask_restore_consumer_sequence_equal_failed_1b',
                             'real_c_callbacks_bypassed','vmu_and_prefetch_tick_off')),
                        'Completion-only independent review differs from final artifacts')
                for path,digest in review['source_sha256'].items():
                    relative=Path(path)
                    require(not relative.is_absolute() and '..' not in relative.parts and
                            sha((ROOT/relative).read_bytes())==digest,'Completion review source changed: '+path)
            else:
                validate_maple_delay_guard(json.loads(files['evidence/maple-delay-stage-guard.json']),
                                           runtime_commit,args.reader_build)
        else:
            validate_maple_bypass_receipt(gateways,runtime_commit,args.reader_build)
        for path,digest in gateways['source_sha256'].items():
            relative=Path(path)
            require(not relative.is_absolute() and '..' not in relative.parts and
                    sha((ROOT/relative).read_bytes())==digest,'Maple gateway proof source changed: '+path)
        files['evidence/native-evolution1-5fa97-wait-capture-2026-10-26.json']=(
            ROOT/'docs/evidence/native-evolution1-5fa97-wait-capture-2026-10-26.json').read_bytes()
        if args.maple_delay:
            files['evidence/native-evolution1-cec542-console-pass-2026-10-26.json']=(
                ROOT/'docs/evidence/native-evolution1-cec542-console-pass-2026-10-26.json').read_bytes()
        if args.completion_delay:
            for filename in ('native-evolution1-1b2d-delay-console-failure-2026-10-26.json',
                             'native-evolution1-5e466-cache-console-failure-2026-10-26.json'):
                files['evidence/'+filename]=(ROOT/'docs/evidence'/filename).read_bytes()
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
    if args.cached_metadata:
        files['tools/install_vmu_native_isolation.py'] = (ROOT/'tools/install_vmu_native_isolation.py').read_bytes()
    for name, build, proof_name in (('reader-only', args.reader_build, reader_proof),
                                     ('vmu-only', args.vmu_build, 'standard_vmu')):
        proof = proofs[proof_name]
        require(proof['lease_bytes'] == 393216 and proof['worker_bytes'] == 131072 and proof['placement'] == 'bottom',
                'Isolation controls do not retain the matching memory reservation')
        variant_files, manifest, layout = bind_variant(name, build.resolve(), proof, runtime_commit,
                                                       args.demand_only, args.wait_capture,args.maple_bypass,args.maple_delay,
                                                       args.cached_metadata,args.completion_delay)
        files.update({'variants/'+name+'/'+path: data for path, data in variant_files.items()})
        metadata['variants'][name] = dict(directory='variants/'+name, destination=str(MODULES[name]),
            module_sha256=manifest['module_sha256'], virtual_a2=name == 'vmu-only' or args.cached_metadata,
            reader='New (X)' if name == 'reader-only' else 'Standard', lease_bytes=393216,
            support_hooks_retained=not(args.maple_bypass and name=='reader-only'),
            prefetch_enabled=name == 'reader-only' and not args.demand_only,
            controller_tick_off=args.demand_only and name == 'reader-only',
            e1_wait_capture=args.wait_capture and name == 'reader-only',
            maple_bypass=args.maple_bypass and name=='reader-only',
            maple_delay=args.maple_delay and name=='reader-only',
            maple_delay_ticks=dict(submit=0 if args.completion_delay else 59,complete=76)
                if args.maple_delay and name=='reader-only' else None,
            completion_delay=args.completion_delay and name=='reader-only',
            maple_c_callbacks_enabled=not(args.maple_bypass and name=='reader-only'),
            cached_metadata=args.cached_metadata and name=='reader-only',
            public_variant='combined' if args.cached_metadata and name=='reader-only' else name,
            heap_reclaim_coherence_guards_retained=True,
            linked_layout_sha256=sha(wire(layout)))
    files['isolation.json'] = wire(metadata)
    passed_recovery = None
    if args.maple_delay or args.cached_metadata:
        bridge,passed_recovery = passed_bypass_bridge(
            args.passed_bypass_package if args.cached_metadata or args.completion_delay else args.previous_package,metadata,files)
        files.update(bridge)
        files['evidence/passed-bypass-recovery.json'] = wire(passed_recovery)
    guide_path = ('docs/native-isolation-maple-completion-delay-test.md' if args.completion_delay else
                  'docs/native-isolation-cached-metadata-test.md' if args.cached_metadata else
                  'docs/native-isolation-maple-delay-test.md' if args.maple_delay else
                  'docs/native-isolation-maple-bypass-test.md' if args.maple_bypass else
                  'docs/native-isolation-wait-test.md' if args.wait_capture else
                  'docs/native-isolation-demand-test.md' if args.demand_only else 'docs/native-isolation-test.md')
    guide = (ROOT/guide_path).read_text().replace('@BUILD@', runtime_commit[:12]).replace(
        '@RUNTIME_COMMIT@', runtime_commit).replace('@DELIVERY_COMMIT@', delivery_commit).encode()
    files['README-FIRST.md'] = guide
    delivery_checks = extracted_install_check(files, args.previous_package if args.demand_only else None,
                                               args.backup_origin_package if args.wait_capture or args.maple_bypass or args.cached_metadata else None)
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
                     e1_wait_capture=args.wait_capture,
                     maple_bypass=args.maple_bypass,
                     maple_delay=args.maple_delay,
                     completion_delay=args.completion_delay,
                     cached_metadata=args.cached_metadata,
                     public_variant='combined' if args.cached_metadata else None,
                     passed_bypass_recovery=passed_recovery,
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
    parser.add_argument('--wait-capture', action='store_true',
                        help='Require the Evolution1 manual30-poll wait capture; needs --demand-only')
    parser.add_argument('--maple-bypass',action='store_true',
                        help='Require the diagnostic Maple C-call bypass; needs --demand-only and excludes wait capture')
    parser.add_argument('--maple-delay',action='store_true',
                        help='Require read-only 59/76 tick delay addback; needs --maple-bypass')
    parser.add_argument('--completion-delay',action='store_true',
                        help='Use exact CEC submit and only the76-tick completion helper; needs --maple-delay')
    parser.add_argument('--cached-metadata',action='store_true',
                        help='Require the combined real-callback VMU/New metadata-cache candidate; needs --demand-only')
    parser.add_argument('--previous-package', type=Path,
                        help='Pinned earlier archive; defaults to995,3f51 for capture,5fa97 for bypass,cec542 for delay,1b2d for cached metadata')
    parser.add_argument('--passed-bypass-package',type=Path,
                        default=ROOT.parent/'K-UI-Evolution-Maple-Bypass-cec542aefa8a.zip',
                        help='Pinned passing cec542 archive for the cached-metadata direct return bridge')
    parser.add_argument('--backup-origin-package', type=Path,
                        default=ROOT.parent/'K-UI-Evolution-Isolation-9956562f91d6.zip',
                        help='Pinned995 archive for inherited backup-chain extraction checks')
    args = parser.parse_args()
    try:
        print(json.dumps(package(args), indent=2))
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'Isolation package refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
