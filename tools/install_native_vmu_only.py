#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install a compact native virtual A2 service, or restore the exact loader."""
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

SCHEMA = 'kui.native.vmu-only.delivery.v1'
BACKUP_SCHEMA = 'kui.native.vmu-only.backup.v1'
STATE_SCHEMA = 'kui.native.vmu-only.state.v1'
DESTINATION = Path('KUI/apps/games/retail-boot.kui')
PARENT = Path('KUI/native-vmu-only')
BACKUP = PARENT/'backup'
STATE = PARENT/'state.json'
LEASE_BYTES = 0x1c000
TRANSPORTS = {'sci': 1, 'scif': 0}
LIMIT = 16*1024*1024+64


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, sort_keys=True, indent=2)+'\n').encode()


def digest(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def publish(path, data):
    """Atomically publish exact bytes and make the directory entry durable."""
    fd, name = tempfile.mkstemp(prefix='.vmu-only-', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        require(temporary.read_bytes() == data, 'Temporary file readback differs')
        os.replace(temporary, path)
        sync_directory(path.parent)
    finally:
        temporary.unlink(missing_ok=True)


def load_personalizer(package):
    sys.path.insert(0, str(package/'tools'))
    source = package/'personalize.py'
    require(source.is_file() and not source.is_symlink(), 'Missing ordinary package personalizer')
    spec = importlib.util.spec_from_file_location('vmu_only_personalizer', source)
    require(spec and spec.loader, 'Cannot load package personalizer')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_package(package):
    package = Path(package).resolve(strict=True)
    p = load_personalizer(package)
    metadata = json.loads(p.read_regular(package/'vmu-only.json', package, maximum=16384))
    require(isinstance(metadata, dict) and set(metadata) ==
            {'schema', 'source_commit', 'build', 'lease_bytes', 'variants'} and
            metadata['schema'] == SCHEMA and metadata['lease_bytes'] == LEASE_BYTES and
            type(metadata['lease_bytes']) is int and
            isinstance(metadata['source_commit'], str) and
            re.fullmatch('[0-9a-f]{40}', metadata['source_commit']) and
            metadata['build'] == metadata['source_commit'][:12] and
            isinstance(metadata['variants'], dict) and set(metadata['variants']) == set(TRANSPORTS),
            'Requires the complete compact VMU-only package')
    variants = {}
    for name, transport in TRANSPORTS.items():
        entry = metadata['variants'][name]
        require(isinstance(entry, dict) and set(entry) == {'directory', 'transport', 'module_sha256'} and
                entry['directory'] == 'variants/'+name and type(entry['transport']) is int and
                entry['transport'] == transport and digest(entry['module_sha256']),
                'Invalid VMU-only variant: '+name)
        directory = package/entry['directory']
        p.ordinary_path(directory, package, directory=True)
        module, manifest, info = p.generic_module(directory)
        require(manifest['source_commit'] == metadata['source_commit'] and
                info['build'] == metadata['build'] and sha(module) == entry['module_sha256'],
                'Variant differs from its pinned source/module: '+name)
        variants[name] = module, manifest, info
    return p, metadata, variants, sha(wire(metadata))


def directory_preflight(p, root, relative):
    """Validate existing ancestors before creating any backup/receipt file."""
    current = root
    for part in relative.parts:
        current = current/part
        if current.exists() or current.is_symlink():
            p.ordinary_path(current, root, directory=True)
        else:
            break


def create_directory(p, root, relative):
    current = root
    for part in relative.parts:
        current = current/part
        if not current.exists() and not current.is_symlink():
            current.mkdir()
            sync_directory(current.parent)
        p.ordinary_path(current, root, directory=True)
    return current


def optional(p, root, relative, maximum=16384):
    path = root/relative
    return p.read_regular(path, root, maximum=maximum) if path.exists() or path.is_symlink() else None


def read_backup(p, root, package_sha):
    record = json.loads(p.read_regular(root/BACKUP/'manifest.json', root, maximum=16384))
    require(isinstance(record, dict) and set(record) ==
            {'schema', 'package_sha256', 'destination', 'original_bytes', 'original_sha256'} and
            record['schema'] == BACKUP_SCHEMA and record['package_sha256'] == package_sha and
            record['destination'] == str(DESTINATION) and
            type(record['original_bytes']) is int and 68 <= record['original_bytes'] <= LIMIT and
            digest(record['original_sha256']), 'Backup provenance differs from this package')
    original = p.read_regular(root/BACKUP/'original.kui', root, expected=record['original_bytes'])
    require(sha(original) == record['original_sha256'], 'Original loader backup checksum differs')
    p.verify(original)
    return original


def create_backup(p, root, package_sha, original):
    parent = create_directory(p, root, PARENT)
    staging = Path(tempfile.mkdtemp(prefix='.backup-', dir=parent))
    try:
        record = dict(schema=BACKUP_SCHEMA, package_sha256=package_sha,
                      destination=str(DESTINATION), original_bytes=len(original), original_sha256=sha(original))
        publish(staging/'original.kui', original)
        publish(staging/'manifest.json', wire(record))
        require(not (root/BACKUP).exists() and not (root/BACKUP).is_symlink(),
                'Another VMU-only backup appeared; preserving the current loader')
        os.rename(staging, root/BACKUP)
        sync_directory(parent)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def canonical_candidate(p, data, variants):
    """A receipt hash alone cannot authorize unrelated replacement code."""
    for module, manifest, info in variants.values():
        if len(data) != len(module):
            continue
        offset = manifest['profile_module_offset']
        try:
            owner = data[offset:offset+p.WIRE_BYTES]
            if p.personalize_bytes(module, manifest, info, owner, runtime_mode=0) == data:
                return True
        except (ValueError, KeyError, TypeError):
            pass
    return False


def make_state(package_sha, original, allowed, active, pending):
    return dict(schema=STATE_SCHEMA, package_sha256=package_sha, original_sha256=sha(original),
                allowed=sorted(set(allowed)), active=active, pending=pending)


def read_state(p, root, package_sha, original):
    raw = optional(p, root, STATE)
    if raw is None:
        return None
    state = json.loads(raw)
    require(isinstance(state, dict) and set(state) ==
            {'schema', 'package_sha256', 'original_sha256', 'allowed', 'active', 'pending'} and
            state['schema'] == STATE_SCHEMA and state['package_sha256'] == package_sha and
            state['original_sha256'] == sha(original) and state['active'] in (*TRANSPORTS, 'restore') and
            type(state['pending']) is bool and isinstance(state['allowed'], list) and
            1 <= len(state['allowed']) <= 2 and all(digest(item) for item in state['allowed']) and
            len(set(state['allowed'])) == len(state['allowed']), 'Invalid VMU-only installation receipt')
    return state


def install(root, package, transport=None, profile=None, restore=False):
    require(type(restore) is bool and ((restore and transport is None) or
            (not restore and transport in TRANSPORTS)), 'Select SCI, SCIF or restore')
    root, package = Path(root).resolve(strict=True), Path(package).resolve(strict=True)
    p, metadata, variants, package_sha = load_package(package)
    p.ordinary_path(root/DESTINATION.parent, root, directory=True)
    directory_preflight(p, root, PARENT)
    directory_preflight(p, root, BACKUP)
    current = p.read_regular(root/DESTINATION, root, maximum=LIMIT)
    p.verify(current)
    have_backup = (root/BACKUP).exists() or (root/BACKUP).is_symlink()
    require(have_backup or optional(p, root, STATE) is None, 'VMU-only receipt has no exact loader backup')
    require(have_backup or not restore, 'No VMU-only backup exists; nothing was changed')
    original = read_backup(p, root, package_sha) if have_backup else current
    state = read_state(p, root, package_sha, original)
    require((state is None and current == original) or
            (state is not None and sha(current) in state['allowed'] and
             (current == original or canonical_candidate(p, current, variants))),
            'Current ordinary loader changed outside this test; preserving all files')
    selected = None
    if restore:
        target, active = original, 'restore'
    else:
        selected = p.select_profile(root, profile)
        module, manifest, info = variants[transport]
        target = p.personalize_bytes(module, manifest, info, selected['wire'], runtime_mode=0)
        active = transport
    final = make_state(package_sha, original, [sha(target)], active, False)
    result = dict(build=metadata['build'], transport=active, lease_bytes=LEASE_BYTES,
                  destination=str(DESTINATION), module_sha256=sha(target), already_installed=current == target)
    if selected:
        result.update(profile=str(selected['path'].relative_to(root)), pair_path=selected['summary']['pair_path'])
    if current == target and state == final:
        return result
    # Complete all package/profile/current-module checks before any first backup.
    require(p.read_regular(root/DESTINATION, root, maximum=LIMIT) == current,
            'Ordinary loader changed during preflight')
    if selected:
        require(p.read_regular(selected['path'], root, expected=p.WIRE_BYTES) == selected['wire'] and
                p.read_regular(selected['path'].parent/'identity.bin', root, expected=16) == selected['identity'],
                'Prepared profile/identity changed during preflight')
    if not have_backup:
        create_backup(p, root, package_sha, original)
        require(read_backup(p, root, package_sha) == original, 'Backup readback differs')
    pending = make_state(package_sha, original, [sha(current), sha(target)], active, True)
    publish(root/STATE, wire(pending))
    try:
        if current != target:
            require(p.read_regular(root/DESTINATION, root, maximum=LIMIT) == current,
                    'Ordinary loader changed before publication')
            publish(root/DESTINATION, target)
            require(p.read_regular(root/DESTINATION, root, maximum=LIMIT) == target,
                    'Installed loader readback differs')
        publish(root/STATE, wire(final))
    except BaseException:
        # Keep both exact states admissible until publication/rollback completes.
        # If rollback itself fails, the pending receipt makes the next invocation
        # recover either the previous invocation bytes or this package's candidate.
        now = p.read_regular(root/DESTINATION, root, maximum=LIMIT)
        if now == target and now != current:
            publish(root/DESTINATION, current)
        if p.read_regular(root/DESTINATION, root, maximum=LIMIT) == current:
            previous = state or make_state(package_sha, original, [sha(current)], 'restore', False)
            publish(root/STATE, wire(previous))
        raise
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('--transport', choices=tuple(TRANSPORTS))
    choice.add_argument('--restore', action='store_true')
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = install(args.sd_root, args.package, args.transport, args.profile, args.restore)
        print(json.dumps(result, indent=2))
        print('Exact original loader restored.' if args.restore else
              'Use '+args.transport.upper()+' / Background Off (Standard) / CD music Off / RAM patches Off. Virtual A2 is ON.')
        print('Restore your previous saved Emulated VMU setting.' if args.restore else
              'Set the saved Emulated VMU setting Off for this test; this module supplies virtual A2 itself.')
        print('Safely eject before booting. Restore with this installer and --restore.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'VMU-only installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
