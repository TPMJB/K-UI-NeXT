#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install or exactly restore only the Skies comparison's launch UI update."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

SCHEMA = 'kui.skies-launch-ui.delivery.v1'
BACKUP_SCHEMA = 'kui.skies-launch-ui.backup.v1'
STATE_SCHEMA = 'kui.skies-launch-ui.state.v1'
DESTINATION = Path('KUI/runtime.kui')
PARENT = Path('KUI/skies-launch-ui')
BACKUP = PARENT/'backup'
STATE = PARENT/'state.json'


def load_core(package):
    path = Path(package)/'tools/install_native_vmu_only.py'
    if not path.is_file() or path.is_symlink():
        raise ValueError('Missing ordinary packaged installer core')
    spec = importlib.util.spec_from_file_location('skies_launch_ui_core', path)
    if not spec or not spec.loader:
        raise ValueError('Cannot load packaged installer core')
    core = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    core.BACKUP_SCHEMA, core.STATE_SCHEMA = BACKUP_SCHEMA, STATE_SCHEMA
    core.PARENT, core.BACKUP, core.STATE, core.DESTINATION = PARENT, BACKUP, STATE, DESTINATION
    core.TRANSPORTS = {'gui': 1}
    return core


def load_candidate(core, package):
    p = core.load_personalizer(package)
    metadata = json.loads(p.read_regular(package/'gui.json', package, maximum=16384))
    core.require(isinstance(metadata, dict) and set(metadata) ==
        {'schema', 'source_commit', 'build', 'module_sha256'} and metadata['schema'] == SCHEMA and
        isinstance(metadata['source_commit'], str) and re.fullmatch('[0-9a-f]{40}', metadata['source_commit']) and
        metadata['build'] == metadata['source_commit'][:12] and core.digest(metadata['module_sha256']),
        'Invalid launch UI package manifest')
    candidate = p.read_regular(package/'gui/runtime.kui', package, maximum=core.LIMIT)
    info = p.verify(candidate)
    core.require(info['build'] == metadata['build'] and core.sha(candidate) == metadata['module_sha256'],
                 'Launch UI differs from its pinned source/runtime checksum')
    return p, metadata, candidate, core.sha(core.wire(metadata))


def install(root, package, restore=False):
    if type(restore) is not bool:
        raise ValueError('Restore must be a boolean')
    root, package = Path(root).resolve(strict=True), Path(package).resolve(strict=True)
    core = load_core(package)
    p, metadata, candidate, package_sha = load_candidate(core, package)
    p.ordinary_path(root/DESTINATION.parent, root, directory=True)
    core.directory_preflight(p, root, PARENT)
    core.directory_preflight(p, root, BACKUP)
    current = p.read_regular(root/DESTINATION, root, maximum=core.LIMIT)
    p.verify(current)
    have_backup = (root/BACKUP).exists() or (root/BACKUP).is_symlink()
    core.require(have_backup or core.optional(p, root, STATE) is None,
                 'Launch UI receipt has no exact runtime backup')
    core.require(have_backup or not restore, 'No launch UI backup exists; nothing was changed')
    original = core.read_backup(p, root, package_sha) if have_backup else current
    state = core.read_state(p, root, package_sha, original)
    core.require((state is None and current == original) or
        (state is not None and core.sha(current) in state['allowed'] and current in (original, candidate)),
        'Current runtime changed outside this UI test; preserving all files')
    target, active = (original, 'restore') if restore else (candidate, 'gui')
    final = core.make_state(package_sha, original, [core.sha(target)], active, False)
    result = dict(build=p.verify(target)['build'], candidate_build=metadata['build'],
                  destination=str(DESTINATION), module_sha256=core.sha(target),
                  restored=restore, already_installed=current == target)
    if current == target and state == final:
        return result
    core.require(p.read_regular(root/DESTINATION, root, maximum=core.LIMIT) == current,
                 'Current runtime changed during preflight')
    if not have_backup:
        core.create_backup(p, root, package_sha, original)
        core.require(core.read_backup(p, root, package_sha) == original, 'Runtime backup readback differs')
    pending = core.make_state(package_sha, original, [core.sha(current), core.sha(target)], active, True)
    core.publish(root/STATE, core.wire(pending))
    try:
        if current != target:
            core.require(p.read_regular(root/DESTINATION, root, maximum=core.LIMIT) == current,
                         'Current runtime changed before publication')
            core.publish(root/DESTINATION, target)
            core.require(p.read_regular(root/DESTINATION, root, maximum=core.LIMIT) == target,
                         'Installed runtime readback differs')
        core.publish(root/STATE, core.wire(final))
    except BaseException:
        now = p.read_regular(root/DESTINATION, root, maximum=core.LIMIT)
        if now == target and now != current:
            core.publish(root/DESTINATION, current)
        if p.read_regular(root/DESTINATION, root, maximum=core.LIMIT) == current:
            previous = state or core.make_state(package_sha, original, [core.sha(current)], 'restore', False)
            core.publish(root/STATE, core.wire(previous))
        raise
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--restore', action='store_true')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.sd_root, args.package, args.restore), indent=2))
        print('Exact previous UI restored.' if args.restore else 'Launch UI update installed.')
        print('Game loaders, cards and launch settings were not changed. Safely eject before booting.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Launch UI installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
