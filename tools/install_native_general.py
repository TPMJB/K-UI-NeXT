#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install the general split controller, structural New reader and matching
Games UI, or restore the exact files they replaced.

Three files change: KUI/runtime.kui, KUI/apps/games/retail-native-dual.kui and
the new KUI/apps/games/retail-native-standalone.kui. Their exact previous bytes
(or absence) are kept in KUI/native-general/backup before anything is written,
and --restore puts back exactly those bytes. The prepared card, its profile
files, launch settings and the ordinary retail-boot.kui are only read.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

SCHEMA = 'kui.native.general.delivery.v1'
BACKUP_SCHEMA = 'kui.native.general.backup.v1'
STATE_SCHEMA = 'kui.native.general.state.v1'
FILES = {'gui': Path('KUI/runtime.kui'),
         'dual': Path('KUI/apps/games/retail-native-dual.kui'),
         'standalone': Path('KUI/apps/games/retail-native-standalone.kui')}
ORDINARY = Path('KUI/apps/games/retail-boot.kui')
PARENT = Path('KUI/native-general')
BACKUP = PARENT/'backup'
STATE = PARENT/'state.json'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return None if data is None else hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2)+'\n').encode()


def load_personalizer(package):
    spec = importlib.util.spec_from_file_location('general_personalizer', package/'personalize.py')
    require(spec and spec.loader, 'Missing package personalizer; extract the complete ZIP')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sync_directory(path):
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def publish(path, data):
    """Replace or remove one file so a reader sees the old or the new bytes."""
    if data is None:
        path.unlink(missing_ok=True)
    else:
        handle, temporary = tempfile.mkstemp(prefix='.'+path.name+'.', dir=path.parent)
        try:
            with os.fdopen(handle, 'wb') as output:
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, path)
        finally:
            Path(temporary).unlink(missing_ok=True)
    sync_directory(path.parent)


def optional(p, root, relative, maximum):
    path = root/relative
    if not path.parent.exists() and not path.parent.is_symlink():
        return None
    p.ordinary_path(path.parent, root, directory=True)
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


def load_package(package):
    package = Path(package).resolve(strict=True)
    p = load_personalizer(package)
    metadata = json.loads(p.read_regular(package/'general.json', package, maximum=16384))
    require(isinstance(metadata, dict) and metadata.get('schema') == SCHEMA,
            'This installer requires its own general candidate package')
    limit = p.MAX_BYTES+64
    gui = p.read_regular(package/'gui/runtime.kui', package, maximum=limit)
    standalone = p.read_regular(package/'standalone/retail-native-standalone.kui', package, maximum=limit)
    require(sha(gui) == metadata['gui']['sha256'] and p.verify(gui)['build'] == metadata['gui']['build'],
            'Packaged Games UI differs from its recorded build')
    require(sha(standalone) == metadata['standalone']['sha256'] and
            p.verify(standalone)['build'] == metadata['standalone']['build'],
            'Packaged standalone reader differs from its recorded build')
    module, manifest, info = p.generic_module(package/'dual')
    require(sha(module) == metadata['dual']['sha256'] and info['build'] == metadata['dual']['build'] and
            manifest['build'] == metadata['dual']['build'],
            'Packaged split-controller module differs from its recorded build')
    return p, metadata, dict(gui=gui, standalone=standalone, dual=(module, manifest, info)), limit


def protected_snapshot(p, root, selected, limit):
    """Files this command must only read."""
    paths = [ORDINARY]
    settings = root/'KUI/apps/games'
    p.ordinary_path(settings, root, directory=True)
    paths += [path.relative_to(root) for path in sorted(settings.glob('launch-settings-*.bin'))]
    if selected is not None:
        pair = selected['path'].parent
        paths += [selected['path'].relative_to(root)]
        paths += [(pair/name).relative_to(root) for name in ('identity.bin', 'card.vmd', 'card.journal')]
    return {str(path): sha(optional(p, root, path, limit)) for path in paths}


def read_backup(p, root, limit):
    record = json.loads(p.read_regular(root/BACKUP/'manifest.json', root, maximum=16384))
    require(isinstance(record, dict) and record.get('schema') == BACKUP_SCHEMA and
            set(record.get('files', {})) == set(FILES), 'Invalid general candidate backup')
    original = {}
    for name in FILES:
        item = record['files'][name]
        require(isinstance(item, dict) and set(item) == {'present', 'bytes', 'sha256'} and
                type(item['present']) is bool, 'Invalid general candidate backup record')
        copy = root/BACKUP/(name+'.kui')
        if item['present']:
            data = p.read_regular(copy, root, expected=item['bytes'])
            require(sha(data) == item['sha256'], 'Backup checksum differs: '+name)
            original[name] = data
        else:
            require(item['bytes'] == 0 and item['sha256'] is None and
                    not copy.exists() and not copy.is_symlink(), 'Absent-file backup is inconsistent')
            original[name] = None
    return record, original


def create_backup(p, root, current, limit):
    parent = ordinary_directory(p, root, PARENT)
    stage = Path(tempfile.mkdtemp(prefix='.backup-', dir=parent))
    try:
        record = dict(schema=BACKUP_SCHEMA, files={name: dict(
            present=data is not None, bytes=len(data) if data is not None else 0,
            sha256=sha(data)) for name, data in current.items()})
        for name, data in current.items():
            if data is not None:
                publish(stage/(name+'.kui'), data)
        publish(stage/'manifest.json', wire(record))
        require(not (root/BACKUP).exists() and not (root/BACKUP).is_symlink(),
                'Another general candidate backup appeared; nothing was installed')
        os.rename(stage, root/BACKUP)
        sync_directory(parent)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return read_backup(p, root, limit)


def apply(p, root, current, desired, state, protected, selected, limit):
    """Publish a pending receipt, replace files, verify, then finalize."""
    pending = dict(state, pending=True, allowed={name: list(dict.fromkeys(
        [sha(current[name]), sha(desired[name])])) for name in FILES})
    changed = [name for name in FILES if current[name] != desired[name]]
    published = []
    try:
        require(protected_snapshot(p, root, selected, limit) == protected,
                'Read-only files changed before publication')
        publish(root/STATE, wire(pending))
        for name in changed:
            require(optional(p, root, FILES[name], limit) == current[name], 'File changed before publication: '+name)
            published.append(name)
            publish(root/FILES[name], desired[name])
        require(all(optional(p, root, FILES[name], limit) == desired[name] for name in FILES),
                'Installed files differ on readback')
        require(protected_snapshot(p, root, selected, limit) == protected,
                'Read-only files changed during installation')
        publish(root/STATE, wire(dict(state, pending=False,
                                      allowed={name: [sha(desired[name])] for name in FILES})))
    except BaseException as error:
        for name in reversed(published):
            try:
                if optional(p, root, FILES[name], limit) == desired[name]:
                    publish(root/FILES[name], current[name])
            except (OSError, ValueError) as rollback_error:
                error.add_note('Rollback failed; run --restore: '+str(rollback_error))
        raise
    return [str(FILES[name]) for name in changed]


def run(root, package, restore=False, profile=None):
    root = Path(root).resolve(strict=True)
    p, metadata, payload, limit = load_package(package)
    for path in FILES.values():
        p.ordinary_path((root/path).parent, root, directory=True)
    current = {name: optional(p, root, path, limit) for name, path in FILES.items()}
    have_backup = (root/BACKUP).exists() or (root/BACKUP).is_symlink()
    state_raw = optional(p, root, STATE, 16384)
    state = json.loads(state_raw) if state_raw is not None else None
    require(state is None or (isinstance(state, dict) and state.get('schema') == STATE_SCHEMA and
            set(state.get('allowed', {})) == set(FILES)), 'Invalid general candidate receipt')
    require(have_backup or state is None, 'General candidate receipt has no backup; nothing was changed')
    if restore:
        require(have_backup, 'No general candidate backup exists; nothing was changed')
        _, original = read_backup(p, root, limit)
        for name in FILES:
            allowed = state['allowed'][name] if state is not None else [sha(original[name])]
            require(sha(current[name]) in allowed+[sha(original[name])],
                    'File changed outside this candidate; preserving it: '+str(FILES[name]))
        protected = protected_snapshot(p, root, None, limit)
        changed = apply(p, root, current, original, dict(schema=STATE_SCHEMA, build=metadata['build'],
                        installed=False), protected, None, limit)
        return dict(restored=True, changed=changed, backup=str(BACKUP))
    ordinary = optional(p, root, ORDINARY, limit)
    require(sha(ordinary) not in metadata['refuse_ordinary_sha256'],
            'The DOA2 pilot still replaces retail-boot.kui. From the extracted DOA2 ZIP run '
            '"python3 install.py <card> --variant restore" first; nothing was changed')
    selected = p.select_profile(root, profile)
    module, manifest, info = payload['dual']
    desired = dict(gui=payload['gui'], standalone=payload['standalone'],
                   dual=p.personalize_bytes(module, manifest, info, selected['wire'], runtime_mode=0))
    protected = protected_snapshot(p, root, selected, limit)
    if have_backup:
        _, original = read_backup(p, root, limit)
        for name in FILES:
            allowed = state['allowed'][name] if state is not None else [sha(original[name])]
            require(sha(current[name]) in allowed+[sha(original[name]), sha(desired[name])],
                    'File changed outside this candidate; preserving it: '+str(FILES[name]))
    else:
        require(current['standalone'] is None,
                'An unrecorded retail-native-standalone.kui exists; preserving it')
        _, original = create_backup(p, root, current, limit)
    changed = apply(p, root, current, desired, dict(schema=STATE_SCHEMA, build=metadata['build'],
                    installed=True), protected, selected, limit)
    return dict(installed=True, changed=changed, backup=str(BACKUP),
                profile=str(selected['path'].relative_to(root)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--profile', type=Path, help='Prepared profile, e.g. KUI/vmu-test/p0005/retail-persistent-profile-0005.bin')
    parser.add_argument('--restore', action='store_true', help='Put back the exact files this candidate replaced')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = run(args.sd_root, args.package, args.restore, args.profile)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'General candidate installation refused: '+str(error)+'\n')
    print(json.dumps(result, indent=2))
    if args.restore:
        print('Exact previous Games UI and modules restored. The 6be and DOA2 installers work again.')
    else:
        print('Installed. Use SCI / Background New (X) / CD music Off / RAM patches Off.')
        print('Games the virtual-A2 module admits keep virtual A2; New (X) games it refuses that have the')
        print('SDK SCI family use the standalone reader without virtual VMU. Background Off is unchanged.')
    print('Safely eject before booting.')


if __name__ == '__main__':
    main()
