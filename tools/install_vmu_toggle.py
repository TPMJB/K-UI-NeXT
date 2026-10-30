#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install a matching VMU toggle UI/module pair, preserving all virtual cards.

Only the GUI and combined native module change. The exact previous pair is
checkpointed; interrupted installs/restores resume from their recorded hashes.
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
import tempfile
import zlib

SCHEMA = 'kui.vmu.toggle.delivery.v1'
BACKUP_SCHEMA = 'kui.vmu.toggle.backup.v1'
STATE_SCHEMA = 'kui.vmu.toggle.state.v1'
FILES = {'gui': Path('KUI/runtime.kui'), 'dual': Path('KUI/apps/games/retail-native-dual.kui')}
PARENT = Path('KUI/vmu-toggle')
MAX_BYTES = 4 * 1024 * 1024 + 64
REQUIRED_DUAL_BUILD = '34dc7b5e15ee'
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
    found = []
    start = 64
    while True:
        at = data.find(CAPABILITY, start)
        if at < 0:
            break
        start = at + 1
        if at % 4 == 0:
            found.append(at)
    return len(found) == 1


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
    spec = importlib.util.spec_from_file_location('toggle_profile_codec', path)
    require(spec and spec.loader, 'Cannot load the packaged profile codec')
    codec = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(codec)
    return codec


def package_data(package):
    package = Path(package).absolute()
    ordinary(package, package, directory=True)
    metadata = json.loads(read(package/'toggle.json', package, 16384))
    require(isinstance(metadata, dict) and metadata.get('schema') == SCHEMA and
            isinstance(metadata.get('build'), str) and re.fullmatch('[0-9a-f]{12}', metadata['build']) and
            metadata.get('required_dual_build') == REQUIRED_DUAL_BUILD and
            all(digest(metadata.get(name)) for name in ('runtime_sha256', 'dual_sha256', 'profile_codec_sha256')),
            'Invalid VMU toggle package record')
    gui = read(package/'gui/runtime.kui', package)
    dual = read(package/'dual/retail-native-dual.kui', package)
    require(verify(gui) == verify(dual) == metadata['build'] and
            sha(gui) == metadata['runtime_sha256'] and sha(dual) == metadata['dual_sha256'],
            'Packaged UI/module differs from its source build')
    codec = load_codec(package, metadata)
    at, _ = profile_slot(dual, codec)
    require(dual[at:at+codec.WIRE_BYTES] == codec.template_profile() and
            dual[mode_slot(dual):mode_slot(dual)+len(MODE)] == MODE and capability(dual),
            'New module is not the generic normal-mode VMU toggle module')
    return metadata, gui, dual, codec


def personalize(generic, previous, codec):
    build = verify(previous)
    require(build == REQUIRED_DUAL_BUILD or capability(previous),
            'Install the lean General 34dc7b5e15ee combined native reader first')
    previous_mode = mode_slot(previous)
    require(previous[previous_mode:previous_mode+len(MODE)] == MODE,
            'The installed native module is not in normal mode')
    source, decoded = profile_slot(previous, codec)
    template = codec.decode_profile(codec.template_profile())
    require(decoded['cid'] != template['cid'], 'The installed module has no personalized card profile')
    result = bytearray(generic)
    target, _ = profile_slot(generic, codec)
    result[target:target+codec.WIRE_BYTES] = previous[source:source+codec.WIRE_BYTES]
    result[mode_slot(result):mode_slot(result)+len(MODE)] = MODE
    struct.pack_into('<I', result, 32, zlib.crc32(result[64:]))
    struct.pack_into('<I', result, 60, zlib.crc32(result[:60]))
    verify(result)
    require(result[target:target+codec.WIRE_BYTES] == previous[source:source+codec.WIRE_BYTES],
            'The installed profile was not preserved exactly')
    return bytes(result)


def current_files(root):
    return {name: read(root/relative, root) for name, relative in FILES.items()}


def backup_read(root, checkpoint, metadata):
    backup = json.loads(read(checkpoint/'backup.json', root, 16384))
    require(isinstance(backup, dict) and set(backup) == {
        'schema', 'build', 'package', 'original', 'desired'} and backup['schema'] == BACKUP_SCHEMA and
        backup['build'] == metadata['build'] and backup['package'] == {
            'gui': metadata['runtime_sha256'], 'dual': metadata['dual_sha256']} and
        isinstance(backup['original'], dict) and set(backup['original']) == set(FILES) and
        isinstance(backup['desired'], dict) and set(backup['desired']) == set(FILES) and
        all(digest(backup['original'][name]) and digest(backup['desired'][name]) for name in FILES),
        'Invalid VMU toggle checkpoint')
    original = {name: read(checkpoint/(name+'-before.kui'), root) for name in FILES}
    for name, value in original.items():
        verify(value)
        require(sha(value) == backup['original'][name], 'Original checkpoint differs: '+name)
    return backup, original


def backup_create(root, checkpoint, metadata, current, desired):
    parent = mkdirs(root, checkpoint.parent.relative_to(root))
    stage = Path(tempfile.mkdtemp(prefix='.checkpoint-', dir=parent))
    try:
        backup = dict(schema=BACKUP_SCHEMA, build=metadata['build'],
                      package={'gui': metadata['runtime_sha256'], 'dual': metadata['dual_sha256']},
                      original={name: sha(value) for name, value in current.items()},
                      desired={name: sha(value) for name, value in desired.items()})
        for name, value in current.items():
            publish(stage/(name+'-before.kui'), value, root)
        publish(stage/'backup.json', wire(backup), root)
        require(not checkpoint.exists() and not checkpoint.is_symlink(), 'Toggle checkpoint appeared concurrently')
        require(current_files(root) == current, 'Installed files changed before checkpoint publication')
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
    state = json.loads(raw)
    require(isinstance(state, dict) and set(state) == {
        'schema', 'build', 'original', 'desired', 'pending', 'installed', 'allowed'} and
        state['schema'] == STATE_SCHEMA and state['build'] == metadata['build'] and
        state['original'] == backup['original'] and state['desired'] == backup['desired'] and
        type(state['pending']) is bool and type(state['installed']) is bool and
        isinstance(state['allowed'], dict) and set(state['allowed']) == set(FILES),
        'Invalid VMU toggle transaction receipt')
    for name in FILES:
        values = state['allowed'][name]
        require(isinstance(values, list) and len(values) in (1, 2) and len(set(values)) == len(values) and
                set(values) <= {backup['original'][name], backup['desired'][name]},
                'Invalid allowed VMU toggle hashes')
        if not state['pending']:
            require(values == [backup['desired' if state['installed'] else 'original'][name]],
                    'Final VMU toggle receipt is inconsistent')
    return raw, state


def apply(root, checkpoint, metadata, backup, state_raw, current, desired, installed):
    require(current_files(root) == current and optional(checkpoint/'state.json', root) == state_raw,
            'Installed files or receipt changed before publication')
    base = dict(schema=STATE_SCHEMA, build=metadata['build'], original=backup['original'],
                desired=backup['desired'], installed=installed)
    allowed = {name: list(dict.fromkeys((sha(current[name]), sha(desired[name])))) for name in FILES}
    changed = [name for name in FILES if desired[name] != current[name]]
    stage = Path(tempfile.mkdtemp(prefix='.transaction-', dir=checkpoint))
    try:
        # Stage and verify both complete files before announcing a pending pair.
        for name in FILES:
            publish(stage/(name+'.kui'), desired[name], root)
        require(current_files(root) == current and optional(checkpoint/'state.json', root) == state_raw,
                'Installed files changed before transaction publication')
        publish(checkpoint/'state.json', wire(dict(base, pending=True, allowed=allowed)), root)
        for name in FILES:
            require(current_files(root) == current, 'Installed files changed before '+name+' replacement')
            if name in changed:
                publish(root/FILES[name], read(stage/(name+'.kui'), root), root)
                current[name] = desired[name]
        require(current_files(root) == desired, 'Installed pair differs after publication')
        publish(checkpoint/'state.json', wire(dict(base, pending=False,
                allowed={name: [sha(desired[name])] for name in FILES})), root)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return [str(FILES[name]) for name in changed]


def run(root, package, restore=False):
    root = Path(root).absolute()
    require('..' not in root.parts, 'Card root must not contain parent traversal')
    ordinary(root, root, directory=True)
    metadata, gui, generic, codec = package_data(package)
    current = current_files(root)
    for value in current.values():
        verify(value)
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
            raise ValueError('Another VMU toggle installer is running') from error
        checkpoint = root/PARENT/'checkpoints'/metadata['build']
        if checkpoint.exists() or checkpoint.is_symlink():
            ordinary(checkpoint, root, directory=True)
            backup, original = backup_read(root, checkpoint, metadata)
        else:
            require(not restore, 'No checkpoint exists for this VMU toggle build')
            desired = {'gui': gui, 'dual': personalize(generic, current['dual'], codec)}
            backup, original = backup_create(root, checkpoint, metadata, current, desired)
        desired = {'gui': gui, 'dual': personalize(generic, original['dual'], codec)}
        require({name: sha(value) for name, value in desired.items()} == backup['desired'],
                'Desired pair differs from the recorded package/profile')
        state_raw, state = state_read(root, checkpoint, metadata, backup)
        allowed = state['allowed'] if state else {name: [backup['original'][name]] for name in FILES}
        require(all(sha(current[name]) in allowed[name] for name in FILES),
                'Files changed outside this VMU toggle update; preserving them')
        target = original if restore else desired
        changed = apply(root, checkpoint, metadata, backup, state_raw, current, target, not restore)
        return dict(restored=restore, installed=not restore, changed=changed,
                    checkpoint=str(checkpoint.relative_to(root)), build=metadata['build'])
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--restore', action='store_true', help='Restore the exact UI and module replaced by this build')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = run(args.sd_root, args.package, args.restore)
    except (OSError, ValueError, KeyError, TypeError, UnicodeError, struct.error) as error:
        parser.exit(2, 'VMU toggle installation refused: '+str(error)+'\n')
    print(json.dumps(result, indent=2))
    print('Previous UI/module restored.' if args.restore else 'Installed. Home > VMU tools > Emulated VMU: On / Off.')
    print('Safely eject the card before booting the Dreamcast.')


if __name__ == '__main__':
    main()


