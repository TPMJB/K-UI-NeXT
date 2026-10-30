#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Switch the SCI Skies VMU comparison, preserving the previous VMU-only test."""
import argparse
import importlib.util
import json
from pathlib import Path

SCHEMA = 'kui.native.skies-vmu-compare.delivery.v1'
BACKUP_SCHEMA = 'kui.native.skies-vmu-compare.backup.v1'
STATE_SCHEMA = 'kui.native.skies-vmu-compare.state.v1'
PARENT = Path('KUI/native-vmu-only-skies')
BACKUP = PARENT/'backup'
STATE = PARENT/'state.json'
DESTINATION = Path('KUI/apps/games/retail-boot.kui')
MODES = {'baseline': 1, 'full-submit': 1}


def load_core(package):
    """Give the proven installer its own module object and receipt namespace."""
    path = Path(package)/'tools/install_native_vmu_only.py'
    if not path.is_file() or path.is_symlink():
        raise ValueError('Missing ordinary packaged VMU-only installer core')
    spec = importlib.util.spec_from_file_location('skies_vmu_compare_core', path)
    if not spec or not spec.loader:
        raise ValueError('Cannot load packaged VMU-only installer core')
    core = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    core.SCHEMA, core.BACKUP_SCHEMA, core.STATE_SCHEMA = SCHEMA, BACKUP_SCHEMA, STATE_SCHEMA
    core.PARENT, core.BACKUP, core.STATE, core.DESTINATION = PARENT, BACKUP, STATE, DESTINATION
    core.TRANSPORTS = dict(MODES)
    return core


def install(root, package, mode=None, profile=None, restore=False):
    if type(restore) is not bool or not ((restore and mode is None) or (not restore and mode in MODES)):
        raise ValueError('Select baseline, full-submit or restore')
    package = Path(package).resolve(strict=True)
    core = load_core(package)
    if not restore:
        # The shared installer checks the profile before creating its backup.
        # Recheck it at the final target publication as well, since backup and
        # pending-receipt syncs must not hide a changed profile/path/identity.
        selected = {}
        load_personalizer, publish = core.load_personalizer, core.publish
        def bound_personalizer(directory):
            p = load_personalizer(directory)
            select_profile = p.select_profile
            def capture_profile(*arguments, **keywords):
                item = select_profile(*arguments, **keywords)
                selected.update(personalizer=p, item=item)
                return item
            p.select_profile = capture_profile
            return p
        card_root = Path(root).resolve(strict=True)
        destination = card_root/DESTINATION
        attempted = False
        def guarded_publish(path, data):
            nonlocal attempted
            if path == destination and not attempted:
                p, item = selected['personalizer'], selected['item']
                core.require(p.read_regular(item['path'], card_root, expected=p.WIRE_BYTES) == item['wire'] and
                    p.read_regular(item['path'].parent/'identity.bin', card_root, expected=16) == item['identity'],
                    'Prepared profile/identity changed before loader publication')
                attempted = True
            return publish(path, data)
        core.load_personalizer, core.publish = bound_personalizer, guarded_publish
    result = core.install(root, package, mode, profile, restore)
    result['mode'] = result.pop('transport')
    result['storage_transport'] = 'SCI'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('--mode', choices=tuple(MODES))
    choice.add_argument('--restore', action='store_true')
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.sd_root, args.package, args.mode, args.profile, args.restore), indent=2))
        if args.restore:
            print('The exact ordinary loader from before this Skies comparison is restored.')
        else:
            print('Use SCI / Background Off (Standard) / CD music Off / RAM patches Off.')
            print('Set the saved Emulated VMU setting Off; this test module supplies virtual A2 itself.')
            print('Skies comparison mode: '+args.mode+'. Both modes use the same selected prepared card.')
        print('Safely eject before booting. Restore this comparison with the same installer and --restore.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Skies VMU comparison installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
