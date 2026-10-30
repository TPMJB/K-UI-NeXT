#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install or restore a native compatibility test: GUI, Background and standalone New.

Only these three files change. Cards, journals, profiles, assignments, settings,
the Standard loaders and other packages' backups are preserved. The first
install keeps an exact backup of each target (or records that it was absent);
restore returns exactly those bytes, then retires the record. A target changed
by anything else since installation is refused before any write. A record that
another compatibility package left after completing its own restore is retired
too, but only while every target still holds exactly the originals it recorded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import tempfile
import zlib

SCHEMA = 'kui.native.compat-test.delivery.v1'
BACKUP_SCHEMA = 'kui.native.compat-test.backup.v1'
STATE_SCHEMA = 'kui.native.compat-test.state.v1'
PARENT = Path('KUI/native-compat-test')
BACKUP = PARENT/'backup'
STATE = PARENT/'state.json'
TARGETS = {
    'gui': Path('KUI/runtime.kui'),
    'background': Path('KUI/apps/games/retail-native-dual.kui'),
    'standalone': Path('KUI/apps/games/retail-native-standalone.kui'),
}
# The GUI and Background loader must already exist; standalone New may not.
REQUIRED = {'gui', 'background'}
MAX_BYTES = 4*1024*1024+64


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2, sort_keys=True)+'\n').encode()


def digest(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def verify(data):
    """The version-1 runtime envelope, as runtime_package.py checks it."""
    require(68 <= len(data) <= MAX_BYTES and data[:8] == b'KUIRUN1\0', 'Invalid runtime envelope')
    version, header, size, address, entry, memory, crc, flags = struct.unpack_from('<8I', data, 8)
    require(version == 1 and header == 64 and address == entry == 0x8c010000 and not flags and
            data[52:60] == bytes(8) and re.fullmatch(rb'[0-9a-f]{12}', data[40:52]) and
            zlib.crc32(data[:60]) == struct.unpack_from('<I', data, 60)[0],
            'Runtime header is damaged or unsupported')
    require(4 <= size and not size % 4 and len(data) == 64+size and size <= memory <= 8*1024*1024 and
            not memory % 4 and zlib.crc32(data[64:]) == crc, 'Runtime length or payload checksum differs')
    return data[40:52].decode('ascii')


def ordinary(path, root, directory=False):
    """Every component below root is an ordinary directory, the last one as requested."""
    path, root = Path(path).absolute(), Path(root).absolute()
    try:
        parts = path.relative_to(root).parts
    except ValueError as error:
        raise ValueError('Path leaves the selected card or package') from error
    require('..' not in parts and stat.S_ISDIR(root.lstat().st_mode), 'Root must be an ordinary directory')
    current = root
    for index, part in enumerate(parts):
        current /= part
        mode = current.lstat().st_mode
        require(stat.S_ISDIR(mode) if directory or index+1 < len(parts) else stat.S_ISREG(mode),
                'Symlink or special file refused: '+str(current))
    return path


def read(path, root, maximum=MAX_BYTES):
    ordinary(path, root)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as source:
        before = os.fstat(source.fileno())
        require(before.st_size <= maximum, 'Oversized file: '+str(path))
        data = source.read(maximum+1)
        after = os.fstat(source.fileno())
    require(len(data) == before.st_size == after.st_size and before.st_mtime_ns == after.st_mtime_ns,
            'File changed while reading: '+str(path))
    return data


def maybe_read(path, root, maximum=MAX_BYTES):
    """Bytes of an ordinary file, or None when it and possibly its parents are absent."""
    current = Path(root)
    for part in Path(path).relative_to(root).parts[:-1]:
        current /= part
        if not current.exists() and not current.is_symlink():
            return None
        ordinary(current, root, directory=True)
    return read(path, root, maximum) if path.exists() or path.is_symlink() else None


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def publish(path, data):
    """Atomically publish exact bytes (or remove the file) and make it durable."""
    if data is None:
        if path.exists():
            path.unlink()
            sync_directory(path.parent)
        return
    fd, name = tempfile.mkstemp(prefix='.compat-test-', dir=path.parent)
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


def mkdirs(root, relative):
    current = Path(root)
    for part in relative.parts:
        current /= part
        if not current.exists() and not current.is_symlink():
            current.mkdir()
            sync_directory(current.parent)
        ordinary(current, root, directory=True)
    return current


def load_package(package):
    package = Path(package).resolve(strict=True)
    metadata = json.loads(read(package/'compat-test.json', package, 16384))
    require(isinstance(metadata, dict) and set(metadata) == {'schema', 'source_commit', 'build', 'files'} and
            metadata['schema'] == SCHEMA and isinstance(metadata['source_commit'], str) and
            re.fullmatch('[0-9a-f]{40}', metadata['source_commit']) and
            metadata['build'] == metadata['source_commit'][:12] and
            isinstance(metadata['files'], dict) and set(metadata['files']) == set(TARGETS),
            'Requires the complete native compatibility test package')
    modules = {}
    for name in TARGETS:
        entry = metadata['files'][name]
        require(isinstance(entry, dict) and set(entry) == {'path', 'sha256'} and digest(entry['sha256']) and
                isinstance(entry['path'], str) and not Path(entry['path']).is_absolute() and
                '..' not in Path(entry['path']).parts, 'Invalid package entry: '+name)
        data = read(package/entry['path'], package)
        require(sha(data) == entry['sha256'] and verify(data) == metadata['build'],
                'Package file differs from its pinned build: '+name)
        modules[name] = data
    return metadata, modules, sha(wire(metadata))


def read_backup(root, package_sha):
    record = json.loads(read(root/BACKUP/'manifest.json', root, 16384))
    require(isinstance(record, dict) and set(record) == {'schema', 'package_sha256', 'originals'} and
            record['schema'] == BACKUP_SCHEMA and isinstance(record['originals'], dict) and
            set(record['originals']) == set(TARGETS), 'Backup provenance is malformed')
    require(record['package_sha256'] == package_sha,
            'Another compatibility test package is installed; restore it with its own installer first')
    originals = {}
    for name, entry in record['originals'].items():
        if entry is None:
            require(name not in REQUIRED, 'Backup omits a required original: '+name)
            originals[name] = None
            continue
        require(isinstance(entry, dict) and set(entry) == {'bytes', 'sha256'} and digest(entry['sha256']) and
                type(entry['bytes']) is int, 'Backup entry is malformed: '+name)
        data = read(root/BACKUP/(name+'.kui'), root)
        require(len(data) == entry['bytes'] and sha(data) == entry['sha256'],
                'Original backup checksum differs: '+name)
        verify(data)
        originals[name] = data
    return originals


def create_backup(root, package_sha, originals):
    parent = mkdirs(root, PARENT)
    staging = Path(tempfile.mkdtemp(prefix='.backup-', dir=parent))
    try:
        record = dict(schema=BACKUP_SCHEMA, package_sha256=package_sha, originals={})
        for name, data in originals.items():
            if data is None:
                record['originals'][name] = None
            else:
                publish(staging/(name+'.kui'), data)
                record['originals'][name] = dict(bytes=len(data), sha256=sha(data))
        publish(staging/'manifest.json', wire(record))
        require(not (root/BACKUP).exists() and not (root/BACKUP).is_symlink(),
                'Another compatibility test backup appeared; nothing was changed')
        os.rename(staging, root/BACKUP)
        sync_directory(parent)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def read_state(root, package_sha):
    raw = maybe_read(root/STATE, root, 16384)
    if raw is None:
        return None
    state = json.loads(raw)
    require(isinstance(state, dict) and set(state) == {'schema', 'package_sha256', 'active', 'pending'} and
            state['schema'] == STATE_SCHEMA and state['package_sha256'] == package_sha and
            state['active'] in ('installed', 'restored') and type(state['pending']) is bool,
            'Compatibility test receipt is malformed')
    return state


def retire(root):
    """Remove the whole record with one rename, so no installer sees half of it."""
    parent = root/PARENT
    for index in range(100):
        retired = parent.with_name('.'+parent.name+'-retired-%d' % index)
        if not retired.exists() and not retired.is_symlink():
            break
    else:
        raise ValueError('Too many retired compatibility test records; nothing was changed')
    os.rename(parent, retired)
    sync_directory(parent.parent)
    # Nothing reads a retired record; a leftover after an interruption is inert.
    shutil.rmtree(retired, ignore_errors=True)


def finished_record(root, package_sha):
    """Originals of another package's record whose own restore completed."""
    record = json.loads(read(root/BACKUP/'manifest.json', root, 16384))
    owner = record.get('package_sha256') if isinstance(record, dict) else None
    if not digest(owner) or owner == package_sha:
        return None
    state = read_state(root, owner)
    if state is None or state['pending'] or state['active'] != 'restored':
        return None
    return read_backup(root, owner)


def install(root, package, restore=False):
    require(type(restore) is bool, 'Restore must be a boolean')
    root = Path(root).resolve(strict=True)
    metadata, modules, package_sha = load_package(package)
    current = {}
    for name, target in TARGETS.items():
        ordinary(root/target.parent, root, directory=True)
        data = maybe_read(root/target, root)
        require(data is not None or name not in REQUIRED, 'Missing current file to replace: '+str(target))
        if data is not None:
            verify(data)
        current[name] = data
    for relative in (PARENT, BACKUP):
        path = root/relative
        if path.exists() or path.is_symlink():
            ordinary(path, root, directory=True)
    have_backup = (root/BACKUP).exists()
    theirs = finished_record(root, package_sha) if have_backup and not restore else None
    if theirs is not None:
        # Its restore completed and every target still holds its exact
        # originals, so retiring its record loses nothing.
        require(all(current[name] == theirs[name] for name in TARGETS),
                'Another compatibility test package is installed; restore it with its own installer first')
        retire(root)
        have_backup = False
    require(have_backup or maybe_read(root/STATE, root, 16384) is None,
            'Compatibility test receipt has no exact backup; nothing was changed')
    require(have_backup or not restore, 'No compatibility test backup exists; nothing was changed')
    originals = read_backup(root, package_sha) if have_backup else dict(current)
    state = read_state(root, package_sha) if have_backup else None
    # Each target may hold only its exact original or this package's file.
    for name in TARGETS:
        require(current[name] in (originals[name], modules[name]),
                'Changed outside this test since installation; preserving it: '+str(TARGETS[name]))
    final = originals if restore else modules
    result = dict(build=metadata['build'], restored=restore, files={
        name: dict(destination=str(TARGETS[name]), sha256=sha(final[name]) if final[name] else None,
                   already=current[name] == final[name]) for name in TARGETS})
    if all(current[name] == final[name] for name in TARGETS) and state is not None and not state['pending'] and \
            state['active'] == ('restored' if restore else 'installed'):
        if restore:
            retire(root)
        return result
    if not have_backup:
        create_backup(root, package_sha, originals)
        require(read_backup(root, package_sha) == originals, 'Backup readback differs')
    publish(root/STATE, wire(dict(schema=STATE_SCHEMA, package_sha256=package_sha,
                                  active='restored' if restore else 'installed', pending=True)))
    for name, target in TARGETS.items():
        if current[name] != final[name]:
            require(maybe_read(root/target, root) == current[name], 'File changed during installation: '+str(target))
            publish(root/target, final[name])
            require(maybe_read(root/target, root) == final[name], 'Installed file readback differs: '+str(target))
    publish(root/STATE, wire(dict(schema=STATE_SCHEMA, package_sha256=package_sha,
                                  active='restored' if restore else 'installed', pending=False)))
    if restore:
        retire(root)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--restore', action='store_true')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.sd_root, args.package, args.restore), indent=2))
        print('Exact previous GUI and loaders restored.' if args.restore else
              'Compatibility test GUI, Background and standalone New installed.')
        print('Cards, saves, settings and Standard loaders are unchanged. Safely eject before booting.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Compatibility test installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
