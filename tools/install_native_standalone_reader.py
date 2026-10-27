#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install the guarded DOA2 standalone reader; retain the exact 6be session."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent/'tools'))
from runtime_package import MAX_BYTES, verify
from install_vmu_native_dual import temporary_bytes, sync_directory

SCHEMA = 'kui.native.standalone-reader.delivery.v1'
BACKUP_SCHEMA = 'kui.native.standalone-reader.backup.v1'
STATE_SCHEMA = 'kui.native.standalone-reader.state.v1'
BASELINE_BUILD = '6be76940ccc9'
BASELINE_METADATA_SHA = 'c82993927753fb93f6ce30d66933e1d4e92a0cf033b3072f39102bdca0853c44'
GUI_BUILD = '8adf1a039035'
GUI_SHA = '57b9584ba789b425c8924a63f955aeefa104b3bb0bf9a2fab74d02d320c91a22'
DESTINATION = Path('KUI/apps/games/retail-boot.kui')
DUAL = Path('KUI/apps/games/retail-native-dual.kui')
GUI = Path('KUI/runtime.kui')
ISOLATION = Path('KUI/native-isolation')
PARENT = Path('KUI/native-standalone-reader')
BACKUP = PARENT/'backup'
STATE = PARENT/'state.json'
MODULE_PATH = 'variants/doa2/retail-native-standalone.kui'
LIMIT = MAX_BYTES+64


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2)+'\n').encode()


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    require(spec and spec.loader, 'Missing baseline verification dependency')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_package(package):
    core = load_module(package/'tools/install_vmu_native_isolation.py', 'standalone_isolation_core')
    p = core.load_personalizer(package/'baseline')
    metadata = json.loads(p.read_regular(package/'standalone-reader.json', package, maximum=16384))
    require(isinstance(metadata, dict), 'Invalid standalone package metadata')
    runtime = metadata.get('runtime_source_commit', metadata.get('source_commit', ''))
    require(metadata.get('schema') == SCHEMA and metadata.get('variant') == 'doa2' and
            re.fullmatch('[0-9a-f]{40}', metadata.get('source_commit', '')) and
            re.fullmatch('[0-9a-f]{40}', runtime) and metadata.get('build') == runtime[:12] and
            metadata.get('destination') == str(DESTINATION) and
            metadata.get('gui') == dict(build=GUI_BUILD, sha256=GUI_SHA) and
            metadata.get('baseline') == dict(build=BASELINE_BUILD, isolation_metadata_sha256=BASELINE_METADATA_SHA),
            'Standalone package source, routing or baseline differs')
    require(metadata.get('standalone_reader') is True and metadata.get('virtual_vmu') is False and
            metadata.get('readahead') is False and metadata.get('demand_only') is True and
            metadata.get('reader') == 'New (X)' and
            metadata.get('ui_background_selection') == 'Off (Standard)' and
            metadata.get('engine') == 'New standalone demand' and
            type(metadata.get('heap_reservation_bytes')) is int and metadata['heap_reservation_bytes'] == 0,
            'Standalone package capabilities differ')
    item = metadata.get('module', {})
    require(isinstance(item, dict) and item.get('path') == MODULE_PATH and type(item.get('bytes')) is int and
            68 <= item['bytes'] <= LIMIT, 'Standalone module bounds differ')
    module = p.read_regular(package/MODULE_PATH, package, expected=item['bytes'])
    require(sha(module) == item.get('sha256') and verify(module)['build'] == metadata['build'],
            'Standalone module checksum or envelope differs')
    raw = p.read_regular(package/'baseline/isolation.json', package, maximum=16384)
    require(sha(raw) == BASELINE_METADATA_SHA, 'Baseline isolation metadata is not the pinned 6be delivery')
    baseline = json.loads(raw)
    require(isinstance(baseline, dict) and baseline.get('build') == BASELINE_BUILD and baseline.get('gui') == metadata['gui'] and
            baseline.get('split_controller') is True and baseline.get('cached_metadata') is False and
            baseline.get('public_variant') == 'combined' and
            set(baseline.get('variants', {})) == set(core.MODULES), 'Baseline is not the passing 6be pair')
    canonical = {}
    for name, destination in core.MODULES.items():
        entry = baseline['variants'][name]
        require(entry.get('directory') == 'variants/'+name and entry.get('destination') == str(destination),
                'Baseline module routing differs')
        data, manifest, info = p.generic_module(package/'baseline'/entry['directory'])
        layout_raw = p.read_regular(package/'baseline'/entry['directory']/'variant-layout.json', package, maximum=32768)
        layout = json.loads(layout_raw)
        require(info['build'] == manifest['build'] == BASELINE_BUILD and
                manifest['source_commit'] == baseline['source_commit'] and
                sha(data) == entry['module_sha256'] == layout.get('module_sha256') and
                sha(layout_raw) == entry.get('linked_layout_sha256'), 'Baseline module/manifest/layout binding differs')
        canonical[name] = (data, manifest, info)
    return core, p, metadata, module, baseline, canonical


def tree_snapshot(p, root, relative):
    base = p.ordinary_path(root/relative, root, directory=True)
    result = {}
    for path in sorted(base.rglob('*')):
        if path.is_dir() and not path.is_symlink():
            p.ordinary_path(path, root, directory=True)
        else:
            result[str(path.relative_to(root))] = sha(p.read_regular(path, root, maximum=LIMIT))
    return result


def controls_snapshot(p, root):
    result = tree_snapshot(p, root, ISOLATION)
    for path in (GUI, DUAL):
        result[str(path)] = sha(p.read_regular(root/path, root, maximum=LIMIT))
    return result


def validate_baseline(core, p, root, metadata, canonical, original_boot):
    gui = p.read_regular(root/GUI, root, maximum=LIMIT)
    require(sha(gui) == GUI_SHA and verify(gui)['build'] == GUI_BUILD, 'The installed GUI is not the pinned 8adf GUI')
    receipt = json.loads(p.read_regular(root/ISOLATION/'state.json', root, maximum=16384))
    require(isinstance(receipt, dict) and receipt.get('schema') == 'kui.native.isolation.state.v1' and
            receipt.get('build') == BASELINE_BUILD and receipt.get('variant') == 'reader-only' and
            receipt.get('pending') is False and receipt.get('maple_bypass') is False and
            receipt.get('maple_delay') is False and
            all(receipt.get(key, False) is False for key in ('completion_delay', 'submission_delay', 'post_start_delay')) and
            set(receipt.get('allowed', {})) == set(core.MODULES), 'Requires the completed active 6be combined isolation session')
    backup, originals = core.read_backup(p, root, metadata)
    require(originals['vmu-only'] is not None and original_boot == originals['vmu-only'],
            'Ordinary loader differs from the exact retained isolation original')
    verify(original_boot)
    current = p.read_regular(root/DUAL, root, maximum=LIMIT)
    generic, manifest, info = canonical['reader-only']
    offset = manifest['profile_module_offset']
    profile_wire = current[offset:offset+p.WIRE_BYTES]
    profile = p.decode_profile(profile_wire)
    require(profile_wire != p.template_profile() and profile['image_id'] == backup['profile_identity'] and
            verify(current)['build'] == BASELINE_BUILD and
            p.personalize_bytes(generic, manifest, info, profile_wire, runtime_mode=0) == current,
            'Active 6be module differs from its exact customized profile/NORMAL manifest')
    require(receipt['allowed'] == {'reader-only': [sha(current)], 'vmu-only': [sha(original_boot)]},
            'Isolation receipt does not authorize the exact current customized pair')
    pair = Path(profile['pair_path'].lstrip('/'))
    p.ordinary_path(root/pair, root, directory=True)
    matches = [path for path in sorted((root/pair).glob('retail-persistent-profile-*.bin'))
               if p.read_regular(path, root, expected=p.WIRE_BYTES) == profile_wire]
    require(matches, 'The customized 6be profile has no matching prepared profile file')
    p.inspect_profile(matches[0], root)
    return pair


def protected_snapshot(p, root, pair):
    result = controls_snapshot(p, root)
    result.update(tree_snapshot(p, root, pair))
    for path in sorted((root/'KUI/apps/games').glob('launch-settings-*.bin')):
        result[str(path.relative_to(root))] = sha(p.read_regular(path, root, maximum=LIMIT))
    return result


def publish(path, data):
    temporary = temporary_bytes(path, data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)
    sync_directory(path.parent)


def install(root, package, variant):
    require(variant in ('doa2', 'restore'), 'Select doa2 or restore')
    root, package = Path(root).resolve(strict=True), Path(package).resolve(strict=True)
    core, p, metadata, candidate, baseline, canonical = load_package(package)
    current = p.read_regular(root/DESTINATION, root, maximum=LIMIT)
    have_backup = (root/BACKUP).exists() or (root/BACKUP).is_symlink()
    have_state = (root/STATE).exists() or (root/STATE).is_symlink()
    require(have_backup or not have_state, 'Standalone receipt has no exact module backup')
    if have_backup:
        saved = json.loads(p.read_regular(root/BACKUP/'manifest.json', root, maximum=16384))
        original = p.read_regular(root/BACKUP/DESTINATION.name, root, maximum=LIMIT)
        require(isinstance(saved, dict) and saved.get('schema') == BACKUP_SCHEMA and saved.get('build') == metadata['build'] and
                saved.get('destination') == str(DESTINATION) and saved.get('candidate_sha256') == sha(candidate) and
                saved.get('original_sha256') == sha(original) and saved.get('original_bytes') == len(original),
                'Standalone exact module backup is corrupt or belongs to another package')
    else:
        require(variant != 'restore', 'No standalone backup exists; nothing was changed')
        original = current
    pair = validate_baseline(core, p, root, baseline, canonical, original)
    controls = controls_snapshot(p, root)
    if have_backup:
        require(saved.get('isolation_controls') == controls, 'The retained 6be session changed; preserving all files')
    if have_state:
        state = json.loads(p.read_regular(root/STATE, root, maximum=16384))
        require(isinstance(state, dict) and state.get('schema') == STATE_SCHEMA and state.get('build') == metadata['build'] and
                state.get('destination') == str(DESTINATION) and type(state.get('pending')) is bool and
                state.get('variant') in ('doa2', 'restore') and
                state.get('original_sha256') == sha(original) and state.get('candidate_sha256') == sha(candidate) and
                isinstance(state.get('allowed'), list) and state['allowed'] and
                set(state['allowed']) <= {sha(original), sha(candidate)} and sha(current) in state['allowed'] and
                (state['pending'] or state['allowed'] == [sha(candidate if state['variant'] == 'doa2' else original)]),
                'Installed ordinary loader or standalone receipt changed outside this test')
    else:
        state = None
        require(current == original, 'Standalone module replacement has no recovery receipt')
    protected = protected_snapshot(p, root, pair)
    desired = candidate if variant == 'doa2' else original
    receipt = dict(schema=STATE_SCHEMA, build=metadata['build'], variant=variant, pending=False,
                   destination=str(DESTINATION), original_sha256=sha(original), candidate_sha256=sha(candidate),
                   allowed=[sha(desired)])
    if current == desired and state == receipt:
        return dict(variant=variant, build=metadata['build'], already_installed=True, changed_modules=[],
                    isolation_session_unchanged=True, restored_exact_previous_module=variant == 'restore')
    if not have_backup:
        saved = dict(schema=BACKUP_SCHEMA, build=metadata['build'], destination=str(DESTINATION),
                     candidate_sha256=sha(candidate), original_sha256=sha(original), original_bytes=len(original),
                     isolation_controls=controls)
        parent = core.ordinary_directory(p, root, PARENT)
        stage = Path(tempfile.mkdtemp(prefix='.backup-', dir=parent))
        try:
            publish(stage/DESTINATION.name, original)
            publish(stage/'manifest.json', wire(saved))
            require(protected_snapshot(p, root, pair) == protected and
                    p.read_regular(root/DESTINATION, root, maximum=LIMIT) == current,
                    'Protected files changed before standalone backup publication')
            require(not (root/BACKUP).exists() and not (root/BACKUP).is_symlink(), 'Another standalone backup appeared')
            os.rename(stage, root/BACKUP)
            sync_directory(parent)
        finally:
            if stage.exists():
                shutil.rmtree(stage)
    pending = dict(receipt, pending=True, allowed=list(dict.fromkeys([sha(current), sha(desired)])))
    try:
        require(protected_snapshot(p, root, pair) == protected and
                p.read_regular(root/DESTINATION, root, maximum=LIMIT) == current,
                'Protected files changed before standalone publication')
        publish(root/STATE, wire(pending))
        if current != desired:
            publish(root/DESTINATION, desired)
        require(p.read_regular(root/DESTINATION, root, maximum=LIMIT) == desired and
                protected_snapshot(p, root, pair) == protected, 'Standalone readback or protected files differ')
        publish(root/STATE, wire(receipt))
    except BaseException as error:
        try:
            if current != desired and p.read_regular(root/DESTINATION, root, maximum=LIMIT) == desired:
                publish(root/DESTINATION, current)
        except (OSError, ValueError) as rollback_error:
            error.add_note('Rollback failed; use this standalone installer --variant restore: '+str(rollback_error))
        raise
    return dict(variant=variant, build=metadata['build'], already_installed=False,
                changed_modules=[str(DESTINATION)] if current != desired else [],
                isolation_session_unchanged=True, restored_exact_previous_module=variant == 'restore')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--variant', choices=('doa2', 'restore'), required=True)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.sd_root, args.package, args.variant), indent=2))
        if args.variant == 'doa2':
            print('Select SCI / Background Off (Standard): this module runs the New standalone demand reader internally.')
            print('Virtual VMU and read-ahead are OFF; no prepared profile or save files were changed.')
        else:
            print('The exact previous ordinary loader is restored; the 6be combined module remains unchanged.')
        print('Restore this standalone test before using an older isolation installer.')
        print('Safely eject before booting.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Standalone installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
