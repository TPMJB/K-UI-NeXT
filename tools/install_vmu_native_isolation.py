#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Switch isolated native controls while preserving the GUI and prepared saves."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent/'tools'))
from runtime_package import MAX_BYTES, verify
from install_vmu_native_dual import temporary_bytes, sync_directory

SCHEMA = 'kui.native.isolation.delivery.v1'
GUI = Path('KUI/runtime.kui')
MODULES = {'reader-only': Path('KUI/apps/games/retail-native-dual.kui'),
           'vmu-only': Path('KUI/apps/games/retail-boot.kui')}
BACKUP_PARENT = Path('KUI/native-isolation')
BACKUP = BACKUP_PARENT/'backup'
STATE = BACKUP_PARENT/'state.json'
LIMIT = MAX_BYTES+64


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fingerprint(data):
    return None if data is None else sha(data)


def wire(value):
    return (json.dumps(value, indent=2)+'\n').encode()


def load_personalizer(package):
    spec = importlib.util.spec_from_file_location('isolation_personalizer', package/'personalize.py')
    require(spec and spec.loader, 'Missing package personalizer')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def optional_file(p, root, relative, maximum=LIMIT):
    path = root/relative
    return p.read_regular(path, root, maximum=maximum) if path.exists() or path.is_symlink() else None


def ordinary_directory(p, root, relative):
    current = root
    for component in relative.parts:
        p.ordinary_path(current, root, directory=True)
        current = current/component
        if not current.exists() and not current.is_symlink():
            current.mkdir()
            sync_directory(current.parent)
        p.ordinary_path(current, root, directory=True)
    return current


def publish(path, data):
    if data is None:
        path.unlink()
    else:
        temporary = temporary_bytes(path, data)
        try:
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)
    sync_directory(path.parent)


def protected_snapshot(p, root, selected=None):
    paths = [GUI]
    settings = root/'KUI/apps/games'
    p.ordinary_path(settings, root, directory=True)
    paths += [path.relative_to(root) for path in settings.glob('launch-settings-*.bin')]
    if selected is not None:
        pair = selected['path'].parent
        paths += [selected['path'].relative_to(root)]
        paths += [(pair/name).relative_to(root) for name in ('identity.bin', 'card.vmd', 'card.journal')]
    return {str(path): sha(p.read_regular(root/path, root, maximum=LIMIT)) for path in paths}


def read_backup(p, root, metadata):
    raw = p.read_regular(root/BACKUP/'manifest.json', root, maximum=16384)
    record = json.loads(raw)
    require(isinstance(record, dict) and record.get('schema') == 'kui.native.isolation.backup.v1' and
            record.get('gui_sha256') == metadata['gui']['sha256'] and
            record.get('package_build') in {metadata['build'], *metadata.get('compatible_packages', {})} and
            set(record.get('files', {})) == set(MODULES), 'Backup provenance differs from this isolation package')
    original = {}
    for name in MODULES:
        item = record['files'][name]
        require(isinstance(item, dict) and set(item) == {'present', 'bytes', 'sha256'} and
                type(item['present']) is bool, 'Invalid original-module backup record')
        if item['present']:
            require(type(item['bytes']) is int and 0 <= item['bytes'] <= LIMIT,
                    'Invalid original-module backup length')
            data = p.read_regular(root/BACKUP/(name+'.kui'), root, expected=item['bytes'])
            require(sha(data) == item['sha256'], 'Backup checksum differs: '+name)
            original[name] = data
        else:
            require(item['bytes'] == 0 and item['sha256'] is None and
                    not (root/BACKUP/(name+'.kui')).exists() and
                    not (root/BACKUP/(name+'.kui')).is_symlink(), 'Absent-module backup is inconsistent')
            original[name] = None
    return record, original


def verify_previous_module(p, data, name, build, metadata, backup):
    """Authorize earlier test code by its pinned generic bytes, not a receipt hash."""
    previous = metadata.get('compatible_packages', {}).get(build)
    require(isinstance(previous, dict) and previous.get('gui_sha256') == metadata['gui']['sha256'] and
            set(previous.get('variants', {})) == set(MODULES), 'Unknown earlier isolation package')
    item = previous['variants'][name]
    require(isinstance(item, dict) and set(item) == {'module_bytes', 'module_sha256', 'profile_module_offset'} and
            type(item['module_bytes']) is int and len(data) == item['module_bytes'] and
            type(item['profile_module_offset']) is int, 'Invalid earlier isolation module bounds')
    info = verify(data)
    offset, template = item['profile_module_offset'], p.template_profile()
    require(info['build'] == build and offset >= 64 and not offset & 3 and
            offset+len(template) <= len(data), 'Earlier isolation module build/profile span differs')
    profile = data[offset:offset+len(template)]
    require(profile != template and p.decode_profile(profile)['image_id'] == backup['profile_identity'],
            'Earlier isolation module belongs to a different prepared card')
    payload = bytearray(data[64:]);payload[offset-64:offset-64+len(template)] = template
    canonical = p.envelope(bytes(payload), info['memory_bytes'], info['build'])
    require(sha(canonical) == item['module_sha256'],
            'Earlier installed module differs from the pinned isolation code: '+name)


def create_backup(p, root, metadata, original, identity):
    parent = ordinary_directory(p, root, BACKUP_PARENT)
    stage = Path(tempfile.mkdtemp(prefix='.backup-', dir=parent))
    try:
        record = dict(schema='kui.native.isolation.backup.v1', package_build=metadata['build'],
                      gui_sha256=metadata['gui']['sha256'], profile_identity=identity.hex(),
                      files={name: dict(present=data is not None, bytes=len(data) if data is not None else 0,
                                        sha256=fingerprint(data)) for name, data in original.items()})
        for name, data in original.items():
            if data is not None:
                publish(stage/(name+'.kui'), data)
        publish(stage/'manifest.json', wire(record))
        require(not (root/BACKUP).exists() and not (root/BACKUP).is_symlink(),
                'Another isolation backup appeared; nothing was installed')
        os.rename(stage, root/BACKUP)
        sync_directory(parent)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return read_backup(p, root, metadata)


def validate_variant_metadata(metadata):
    """Build-only bypass metadata cannot silently select another diagnostic."""
    for name, entry in metadata['variants'].items():
        require(isinstance(entry, dict), 'Invalid isolation variant metadata')
        bypass = entry.get('maple_bypass', False)
        require(type(bypass) is bool, 'Maple-bypass metadata must be a boolean')
        delay = entry.get('maple_delay', False)
        require(type(delay) is bool, 'Maple-delay metadata must be a boolean')
        completion = entry.get('completion_delay', False)
        require(type(completion) is bool, 'Completion-delay metadata must be a boolean')
        submission = entry.get('submission_delay', False)
        require(type(submission) is bool, 'Submission-delay metadata must be a boolean')
        require(not(completion and submission),'Select one delayed gateway')
        require(not completion or delay, 'Completion-only delay requires Maple delay')
        require(not submission or delay, 'Submission-only delay requires Maple delay')
        if delay:
            require(bypass is True and entry.get('maple_delay_ticks') ==
                    dict(submit=0 if completion else 59, complete=0 if submission else 76),
                    'Maple delay requires the bypass control and exact selected tick budgets')
        if bypass:
            require(name == 'reader-only' and entry.get('prefetch_enabled') is False and
                    entry.get('controller_tick_off') is True and
                    entry.get('e1_wait_capture') is False,
                    'Maple bypass requires reader-only demand reads, controller TICK off, and no wait capture')


def install(root, package, variant, profile=None):
    require(variant in (*MODULES, 'restore'), 'Select reader-only, vmu-only or restore')
    root, package = Path(root).resolve(strict=True), Path(package).resolve(strict=True)
    p = load_personalizer(package)
    metadata = json.loads(p.read_regular(package/'isolation.json', package, maximum=16384))
    require(metadata.get('schema') == SCHEMA and set(metadata.get('variants', {})) == set(MODULES),
            'Invalid isolation package metadata')
    validate_variant_metadata(metadata)
    maple_bypass = metadata['variants'].get(variant, {}).get('maple_bypass', False)
    maple_delay = metadata['variants'].get(variant, {}).get('maple_delay', False)
    completion_delay = metadata['variants'].get(variant, {}).get('completion_delay', False)
    submission_delay = metadata['variants'].get(variant, {}).get('submission_delay', False)
    completion_receipt = (dict(submission_delay=True) if submission_delay else
                          dict(completion_delay=True) if completion_delay else {})
    gui = p.read_regular(root/GUI, root, maximum=LIMIT)
    require(sha(gui) == metadata['gui']['sha256'] and verify(gui)['build'] == metadata['gui']['build'],
            'This test requires the existing 8adf Games UI; it was not replaced. Restore/install that candidate first.')
    for path in MODULES.values():
        p.ordinary_path((root/path).parent, root, directory=True)
    current = {name: optional_file(p, root, path) for name, path in MODULES.items()}
    selected = p.select_profile(root, profile) if variant != 'restore' else None
    before_protected = protected_snapshot(p, root, selected)
    target_module = None
    if selected is not None:
        entry = metadata['variants'][variant]
        require(entry['directory'] == 'variants/'+variant and entry['destination'] == str(MODULES[variant]),
                'Variant routing differs from its fixed installation path')
        p.ordinary_path(package/entry['directory'], package, directory=True)
        module, manifest, info = p.generic_module(package/entry['directory'])
        require(info['build'] == metadata['build'] == manifest['build'] and
                sha(module) == entry['module_sha256'] and manifest['source_commit'] == metadata['source_commit'],
                'Variant module/source differs from isolation metadata')
        target_module = p.personalize_bytes(module, manifest, info, selected['wire'], runtime_mode=0)
    if (root/BACKUP).exists() or (root/BACKUP).is_symlink():
        backup, original = read_backup(p, root, metadata)
    else:
        require(variant != 'restore', 'No isolation backup exists; no files were changed')
        backup, original = create_backup(p, root, metadata, current, selected['identity'])
    if selected is not None:
        require(backup['profile_identity'] == selected['identity'].hex(),
                'Use the same prepared virtual card for both isolation controls')
    state_raw = optional_file(p, root, STATE, 16384)
    state = json.loads(state_raw) if state_raw is not None else None
    if state is not None:
        require(state.get('schema') == 'kui.native.isolation.state.v1' and
                state.get('build') in {metadata['build'], *metadata.get('compatible_packages', {})} and
                set(state.get('allowed', {})) == set(MODULES) and
                type(state.get('maple_bypass', False)) is bool and
                type(state.get('maple_delay', False)) is bool and
                type(state.get('completion_delay', False)) is bool and
                type(state.get('submission_delay', False)) is bool and
                not(state.get('completion_delay',False) and state.get('submission_delay',False)),
                'Invalid isolation recovery receipt')
    for name, data in current.items():
        allowed = state['allowed'][name] if state is not None else [fingerprint(original[name])]
        require(isinstance(allowed, list) and fingerprint(data) in allowed,
                'Installed module changed outside this test; preserving it: '+str(MODULES[name]))
        if state is not None and state['build'] != metadata['build'] and data != original[name]:
            require(data is not None, 'Earlier isolation module is missing')
            verify_previous_module(p, data, name, state['build'], metadata, backup)
    desired = dict(original)
    if variant != 'restore':
        desired[variant] = target_module
    changes = [(name, current[name], desired[name]) for name in MODULES if current[name] != desired[name]]
    receipt = dict(schema='kui.native.isolation.state.v1', build=metadata['build'], variant=variant,
                   pending=False, maple_bypass=maple_bypass, maple_delay=maple_delay,
                   allowed={name: [fingerprint(desired[name])] for name in MODULES},**completion_receipt)
    if not changes and state == receipt:
        require(protected_snapshot(p, root, selected) == before_protected, 'Read-only files changed during inspection')
        return dict(variant=variant, build=metadata['build'], already_installed=True, gui_unchanged=True,
                    maple_bypass=maple_bypass, maple_delay=maple_delay,
                    prepared_card_unchanged=True, changed_modules=[],
                    prefetch_enabled=metadata['variants'].get(variant, {}).get('prefetch_enabled'),
                    e1_wait_capture=metadata['variants'].get(variant, {}).get('e1_wait_capture', False),
                    **completion_receipt)
    pending = dict(receipt, pending=True, allowed={name: list(dict.fromkeys(
        [fingerprint(current[name]), fingerprint(desired[name])])) for name in MODULES})
    published = []
    try:
        require(protected_snapshot(p, root, selected) == before_protected, 'Read-only files changed before publication')
        publish(root/STATE, wire(pending))
        for name, before, after in changes:
            require(optional_file(p, root, MODULES[name]) == before, 'Module changed before publication: '+name)
            published.append((name, before, after))
            publish(root/MODULES[name], after)
        require(all(optional_file(p, root, MODULES[name]) == desired[name] for name in MODULES),
                'Installed isolation modules differ on readback')
        require(protected_snapshot(p, root, selected) == before_protected, 'Read-only files changed during installation')
        publish(root/STATE, wire(receipt))
    except BaseException as error:
        for name, before, after in reversed(published):
            try:
                if optional_file(p, root, MODULES[name]) == after:
                    publish(root/MODULES[name], before)
            except (OSError, ValueError) as rollback_error:
                error.add_note('Rollback failed; run --variant restore: '+str(rollback_error))
        # The pending receipt accepts the exact before/after states after an
        # interrupted publication; immutable baseline copies remain untouched.
        raise
    return dict(variant=variant, build=metadata['build'], gui_unchanged=True,
                maple_bypass=maple_bypass, maple_delay=maple_delay,
                prefetch_enabled=metadata['variants'].get(variant, {}).get('prefetch_enabled'),
                e1_wait_capture=metadata['variants'].get(variant, {}).get('e1_wait_capture', False),
                prepared_card_unchanged=True, changed_modules=[str(MODULES[name]) for name, _, _ in changes],
                backup=str(BACKUP), restored_exact_original_modules=variant == 'restore',**completion_receipt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--variant', required=True, choices=(*MODULES, 'restore'))
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = install(args.sd_root, args.package, args.variant, args.profile)
        print(json.dumps(result, indent=2))
        if args.variant == 'reader-only':
            print('Use SCI / Background New (X). Virtual A2 is OFF.')
            if result.get('maple_bypass'):
                print('Original SDK Maple submit/completion stays in place; controller TICK is OFF.')
            else:
                print('Reader support hooks remain active.')
            if result.get('prefetch_enabled') is False:
                print('Controller read-ahead is OFF; New demand reads and SDK SCI remain enabled.')
            if result.get('maple_delay'):
                if result.get('submission_delay'):
                    print('Timing control: real Maple C callbacks remain OFF; only submit waits59 TMU0 ticks.')
                    print('Completion retains the passing CEC no-call path; no completion delay helper runs.')
                elif result.get('completion_delay'):
                    print('Timing control: real Maple C callbacks remain OFF; only completion waits76 TMU0 ticks.')
                    print('Submit retains the passing CEC no-call path; no submit delay helper runs.')
                else:
                    print('Timing control: real Maple C callbacks remain OFF; read-only delays are 59/76 TMU0 ticks.')
                print('Check only the same Evolution transition. No capture gesture or new diagnostic photos are required.')
            if result.get('e1_wait_capture'):
                print('At the black-screen wait, hold both triggers + B for 30 controller polls, then release.')
                print('Photograph both short CPU/reader-state pages. NORMAL mode remains selected.')
        elif args.variant == 'vmu-only':
            print('Use SCI / Background Off (Standard). Virtual A2 is ON.')
            print('The ordinary Standard module is replaced until --variant restore is run.')
        else:
            print('Both original modules restored. Select New (X) again for the combined reader/A2 candidate.')
        print('GUI, prepared saves, profile and launch settings were not changed.')
        print('Safely eject before booting. Restore afterward with the same command and --variant restore.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Isolation installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
