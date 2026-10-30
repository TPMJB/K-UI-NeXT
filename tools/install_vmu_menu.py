#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install a GUI-only update, or restore its exact predecessor.

Only KUI/runtime.kui is replaced. Native game modules, VMU images/journals,
profiles, settings and the General installer's backup/receipt are untouched.
"""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import sys
import tempfile
import zlib

SCHEMA = 'kui.vmu.menu.delivery.v1'
BACKUP_SCHEMA = 'kui.vmu.menu.backup.v1'
STATE_SCHEMA = 'kui.vmu.menu.state.v1'
RUNTIME = Path('KUI/runtime.kui')
DUAL = Path('KUI/apps/games/retail-native-dual.kui')
PARENT = Path('KUI/vmu-menu')
MAX_BYTES = 4 * 1024 * 1024 + 64
STANDALONE = Path('KUI/apps/games/retail-native-standalone.kui')
ORDINARY = Path('KUI/apps/games/retail-boot.kui')
NATIVE_FILES = {'dual': DUAL, 'standalone': STANDALONE, 'ordinary': ORDINARY}
CAPABILITY = struct.pack('<8I', 0x5649554b, 0x3130544d, 1, 32, 7, 0x51000, 0x30000, 1)
MODE = b'KUINVM01'+struct.pack('<3I', 1, 32, 0)+bytes(12)
CODEC = 'tools/vmu_persist_profile.py'


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
    """The same version-1 envelope checks used by runtime_package.py."""
    require(68 <= len(data) <= MAX_BYTES and data[:8] == b'KUIRUN1\0', 'Invalid runtime envelope')
    version, header, size, address, entry, memory, crc, flags = struct.unpack_from('<8I', data, 8)
    require(version == 1 and header == 64 and address == entry == 0x8c010000 and
            flags == 0 and data[52:60] == bytes(8) and
            re.fullmatch(rb'[0-9a-f]{12}', data[40:52]) and
            zlib.crc32(data[:60]) == struct.unpack_from('<I', data, 60)[0],
            'Runtime header is damaged or unsupported')
    require(size >= 4 and size % 4 == 0 and len(data) == 64 + size and
            size <= memory <= 8 * 1024 * 1024 and memory % 4 == 0 and
            zlib.crc32(data[64:]) == crc, 'Runtime length or payload checksum differs')
    return data[40:52].decode('ascii')


def ordinary(path, root, directory=False):
    path, root = Path(path).absolute(), Path(root).absolute()
    try:
        parts = path.relative_to(root).parts
    except ValueError as error:
        raise ValueError('Path leaves the selected card/package') from error
    require('..' not in parts and stat.S_ISDIR(root.lstat().st_mode),
            'Root must be an ordinary directory')
    current = root
    for index, part in enumerate(parts):
        current /= part
        mode = current.lstat().st_mode
        require(stat.S_ISDIR(mode) if directory or index + 1 < len(parts) else stat.S_ISREG(mode),
                'Symlink or special file refused: '+str(current))
    return path


def read(path, root, maximum=MAX_BYTES):
    ordinary(path, root)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as source:
        before = os.fstat(source.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size <= maximum,
                'Oversized or non-regular file: '+str(path))
        data = source.read(maximum + 1)
        after = os.fstat(source.fileno())
    require(len(data) == before.st_size == after.st_size and
            before.st_mtime_ns == after.st_mtime_ns and before.st_ino == after.st_ino,
            'File changed while reading: '+str(path))
    return data


def optional(path, root, maximum=16384):
    ordinary(path.parent, root, directory=True)
    return read(path, root, maximum) if path.exists() or path.is_symlink() else None


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def mkdirs(root, relative):
    current = root
    for part in relative.parts:
        require(part not in ('', '.', '..'), 'Invalid output directory')
        ordinary(current, root, directory=True)
        current /= part
        if not current.exists() and not current.is_symlink():
            current.mkdir()
            sync_directory(current.parent)
        ordinary(current, root, directory=True)
    return current


def publish(path, data, root):
    ordinary(path.parent, root, directory=True)
    if path.exists() or path.is_symlink():
        ordinary(path, root)
    fd, name = tempfile.mkstemp(prefix='.'+path.name+'.', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        require(read(temporary, root) == data, 'Temporary file differs on readback')
        ordinary(path.parent, root, directory=True)
        if path.exists() or path.is_symlink():
            ordinary(path, root)
        os.replace(temporary, path)
        sync_directory(path.parent)
        require(read(path, root) == data, 'Published file differs on readback')
    finally:
        temporary.unlink(missing_ok=True)


def capability(data):
    positions = [at for at in range(64, len(data)-len(CAPABILITY)+1, 4)
                 if data[at:at+len(CAPABILITY)] == CAPABILITY]
    return len(positions) == 1


def mode_slot(data):
    found = []
    start = 64
    while True:
        at = data.find(MODE[:8], start)
        if at < 0:
            break
        start = at + 1
        value = data[at:at+len(MODE)]
        if at % 4 == 0 and len(value) == len(MODE) and value[:16] == MODE[:16] and not any(value[20:]) and \
                struct.unpack_from('<I', value, 16)[0] in range(8):
            found.append(at)
    require(len(found) == 1, 'Missing or ambiguous native runtime mode')
    return found[0]


def profile_slot(data, codec):
    found = []
    start = 64
    while True:
        at = data.find(b'KUIVMUP2', start)
        if at < 0:
            break
        start = at + 1
        if at % 4:
            continue
        try:
            decoded = codec.decode_profile(data[at:at+codec.WIRE_BYTES])
        except (ValueError, KeyError, TypeError, UnicodeError, struct.error):
            continue
        found.append((at, decoded))
    require(len(found) == 1, 'Missing or ambiguous persistent card profile')
    return found[0]


def load_codec(package, metadata):
    path = package/CODEC
    source = read(path, package, 131072)
    require(sha(source) == metadata.get('profile_codec_sha256'), 'Packaged profile codec differs')
    spec = importlib.util.spec_from_file_location('menu_profile_codec', path)
    require(spec and spec.loader, 'Cannot load the packaged profile codec')
    codec = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(codec)
    return codec


def package_data(package):
    package = Path(package).absolute()
    ordinary(package, package, directory=True)
    record = json.loads(read(package/'menu.json', package, 16384))
    require(isinstance(record, dict) and record.get('schema') == SCHEMA and
            isinstance(record.get('build'), str) and re.fullmatch('[0-9a-f]{12}', record['build']) and
            record.get('native_runtime_unchanged') is True and
            all(digest(record.get(name)) for name in ('runtime_sha256', 'profile_codec_sha256')),
            'Invalid VMU menu package record')
    data = read(package/'gui/runtime.kui', package)
    require(verify(data) == record['build'] and sha(data) == record['runtime_sha256'],
            'Packaged UI differs from its source build')
    return package, record, data, load_codec(package, record)


def native_files(root):
    return {name: read(root/path, root) if name == 'dual' else optional(root/path, root, MAX_BYTES)
            for name, path in NATIVE_FILES.items()}


def native_hashes(native):
    return {name: sha(value) if value is not None else None for name, value in native.items()}


def admit_native(dual, codec):
    build = verify(dual)
    require(capability(dual), 'Install the VMU toggle-capable combined native module first; this update preserves it')
    at = mode_slot(dual)
    require(dual[at:at+len(MODE)] == MODE, 'The installed combined native module is not in normal mode')
    _, decoded = profile_slot(dual, codec)
    require(decoded['cid'] != codec.decode_profile(codec.template_profile())['cid'],
            'The installed native module has no personalized card profile')
    return build


def backup_read(root, checkpoint, metadata):
    record = json.loads(read(checkpoint/'backup.json', root, 16384))
    require(isinstance(record, dict) and set(record) == {
        'schema', 'build', 'runtime_sha256', 'original_sha256', 'original_bytes', 'native_sha256'} and
        record['schema'] == BACKUP_SCHEMA and record['build'] == metadata['build'] and
        record['runtime_sha256'] == metadata['runtime_sha256'] and
        digest(record['original_sha256']) and isinstance(record['native_sha256'], dict) and
        set(record['native_sha256']) == set(NATIVE_FILES) and
        digest(record['native_sha256']['dual']) and
        all(value is None or digest(value) for value in record['native_sha256'].values()) and
        type(record['original_bytes']) is int and 68 <= record['original_bytes'] <= MAX_BYTES,
        'Invalid VMU menu checkpoint')
    original = read(checkpoint/'runtime-before.kui', root)
    require(len(original) == record['original_bytes'] and sha(original) == record['original_sha256'],
            'Original runtime checkpoint differs')
    verify(original)
    return record, original


def backup_create(root, checkpoint, metadata, current, native):
    parent = mkdirs(root, checkpoint.parent.relative_to(root))
    stage = Path(tempfile.mkdtemp(prefix='.checkpoint-', dir=parent))
    try:
        record = dict(schema=BACKUP_SCHEMA, build=metadata['build'],
                      runtime_sha256=metadata['runtime_sha256'], original_sha256=sha(current),
                      original_bytes=len(current), native_sha256=native_hashes(native))
        publish(stage/'runtime-before.kui', current, root)
        publish(stage/'backup.json', wire(record), root)
        require(not checkpoint.exists() and not checkpoint.is_symlink(), 'Menu checkpoint appeared concurrently')
        require(read(root/RUNTIME, root) == current and native_files(root) == native,
                'Installed files changed before checkpoint publication')
        os.rename(stage, checkpoint)
        sync_directory(parent)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return backup_read(root, checkpoint, metadata)


def state_read(root, checkpoint, metadata, backup):
    raw = optional(checkpoint/'state.json', root)
    if raw is None:
        return None, None
    value = json.loads(raw)
    require(isinstance(value, dict) and set(value) == {
        'schema', 'build', 'runtime_sha256', 'original_sha256', 'pending', 'installed', 'allowed'} and
        value['schema'] == STATE_SCHEMA and value['build'] == metadata['build'] and
        value['runtime_sha256'] == metadata['runtime_sha256'] and
        value['original_sha256'] == backup['original_sha256'] and
        type(value['pending']) is bool and type(value['installed']) is bool and
        isinstance(value['allowed'], list) and len(value['allowed']) in (1, 2) and
        len(set(value['allowed'])) == len(value['allowed']) and
        set(value['allowed']) <= {backup['original_sha256'], metadata['runtime_sha256']},
        'Invalid VMU menu transaction receipt')
    if not value['pending']:
        require(value['allowed'] == [metadata['runtime_sha256'] if value['installed'] else backup['original_sha256']],
                'Final VMU menu receipt is inconsistent')
    return raw, value


def apply(root, checkpoint, metadata, backup, state_raw, current, desired, native, installed):
    require(read(root/RUNTIME, root) == current and native_files(root) == native and
            optional(checkpoint/'state.json', root) == state_raw,
            'Installed files or receipt changed before publication')
    base = dict(schema=STATE_SCHEMA, build=metadata['build'], runtime_sha256=metadata['runtime_sha256'],
                original_sha256=backup['original_sha256'], installed=installed)
    pending = dict(base, pending=True, allowed=list(dict.fromkeys((sha(current), sha(desired)))))
    publish(checkpoint/'state.json', wire(pending), root)
    require(read(root/RUNTIME, root) == current and native_files(root) == native,
            'Installed files changed before runtime replacement')
    if desired != current:
        publish(root/RUNTIME, desired, root)
    require(read(root/RUNTIME, root) == desired and native_files(root) == native,
            'Runtime or protected native modules changed during installation; preserve this package and check the card')
    publish(checkpoint/'state.json', wire(dict(base, pending=False, allowed=[sha(desired)])), root)
    return desired != current


def run(root, package, restore=False):
    root = Path(root).absolute()
    require('..' not in root.parts, 'Card root must not contain parent traversal')
    ordinary(root, root, directory=True)
    _, metadata, payload, codec = package_data(package)
    ordinary((root/RUNTIME).parent, root, directory=True)
    ordinary((root/DUAL).parent, root, directory=True)
    current, native = read(root/RUNTIME, root), native_files(root)
    verify(current)
    native_build = admit_native(native['dual'], codec)
    parent = mkdirs(root, PARENT)
    lock_path = parent/'installer.lock'
    if lock_path.exists() or lock_path.is_symlink():
        ordinary(lock_path, root)
    fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        require(stat.S_ISREG(os.fstat(fd).st_mode), 'Installer lock is not an ordinary file')
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError('Another VMU menu installer is running') from error
        checkpoint = root/PARENT/'checkpoints'/metadata['build']
        if checkpoint.exists() or checkpoint.is_symlink():
            ordinary(checkpoint, root, directory=True)
            backup, original = backup_read(root, checkpoint, metadata)
        else:
            require(not restore, 'No checkpoint exists for this VMU menu build')
            backup, original = backup_create(root, checkpoint, metadata, current, native)
        require(native_hashes(native) == backup['native_sha256'],
                'Native modules changed outside this menu update; preserving them')
        state_raw, state = state_read(root, checkpoint, metadata, backup)
        allowed = state['allowed'] if state else [backup['original_sha256']]
        require(sha(current) in allowed, 'Runtime changed outside this menu update; preserving it')
        desired = original if restore else payload
        if desired == current and state and not state['pending'] and state['installed'] == (not restore):
            changed = False
            require(read(root/RUNTIME, root) == current and native_files(root) == native and
                    optional(checkpoint/'state.json', root) == state_raw,
                    'Runtime, native modules or receipt changed before completion')
        else:
            changed = apply(root, checkpoint, metadata, backup, state_raw, current, desired, native, not restore)
        return dict(restored=restore, installed=not restore, changed=[str(RUNTIME)] if changed else [],
                    checkpoint=str(checkpoint.relative_to(root)), build=metadata['build'],
                    native_module_build=native_build, native_sha256=native_hashes(native))
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--restore', action='store_true', help='Restore the exact UI replaced by this build')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = run(args.sd_root, args.package, args.restore)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError, struct.error) as error:
        parser.exit(2, 'VMU menu installation refused: '+str(error)+'\n')
    print(json.dumps(result, indent=2))
    print('Previous UI restored.' if args.restore else 'GUI update installed.')
    print('Safely eject the card before booting the Dreamcast.')


if __name__ == '__main__':
    main()
