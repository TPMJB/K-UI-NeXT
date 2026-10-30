#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install one audited SCI Skies mode, preserving all earlier test receipts."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

SCHEMA = 'kui.native.skies-next.delivery.v2'
BACKUP_SCHEMA = 'kui.native.skies-next.backup.v2'
STATE_SCHEMA = 'kui.native.skies-next.state.v2'
PARENT = Path('KUI/skies-next-loader')
BACKUP, STATE = PARENT/'backup', PARENT/'state.json'
DESTINATION = Path('KUI/apps/games/retail-boot.kui')
CONTRACTS = {
    'baseline': dict(transport=1, lease_bytes=0x12000, controller_submit='split'),
    'full-submit': dict(transport=1, lease_bytes=0x12000, controller_submit='full'),
    'reference-112': dict(transport=1, lease_bytes=0x1c000, controller_submit='split'),
}


def load_core(package):
    path = Path(package)/'tools/install_native_vmu_only.py'
    if not path.is_file() or path.is_symlink():
        raise ValueError('Missing ordinary packaged VMU installer core')
    spec = importlib.util.spec_from_file_location('skies_next_private_core', path)
    if not spec or not spec.loader:
        raise ValueError('Cannot load packaged VMU installer core')
    core = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    core.SCHEMA, core.BACKUP_SCHEMA, core.STATE_SCHEMA = SCHEMA, BACKUP_SCHEMA, STATE_SCHEMA
    core.PARENT, core.BACKUP, core.STATE, core.DESTINATION = PARENT, BACKUP, STATE, DESTINATION
    core.TRANSPORTS = {name: 1 for name in CONTRACTS}
    return core


def load_package(core, package, mode=None):
    """Validate every geometry, then hash the untouched complete metadata.

    All modes share one backup/receipt identity. The selected lease is added
    only to the returned private metadata copy for the shared core's result.
    """
    p = core.load_personalizer(package)
    metadata = json.loads(p.read_regular(package/'skies-next.json', package, maximum=16384))
    core.require(isinstance(metadata, dict) and set(metadata) ==
        {'schema', 'source_commit', 'build', 'variants'} and metadata['schema'] == SCHEMA and
        isinstance(metadata['source_commit'], str) and re.fullmatch('[0-9a-f]{40}', metadata['source_commit']) and
        metadata['build'] == metadata['source_commit'][:12] and
        isinstance(metadata['variants'], dict) and set(metadata['variants']) == set(CONTRACTS),
        'Requires the complete three-mode Skies package')
    variants = {}
    for name, geometry in CONTRACTS.items():
        entry = metadata['variants'][name]
        core.require(isinstance(entry, dict) and set(entry) ==
            {'directory', 'transport', 'lease_bytes', 'controller_submit', 'module_sha256'} and
            entry['directory'] == 'variants/'+name and
            type(entry['transport']) is int and type(entry['lease_bytes']) is int and
            all(entry[key] == value for key, value in geometry.items()) and core.digest(entry['module_sha256']),
            'Mode geometry/controller contract differs: '+name)
        directory = package/entry['directory']
        p.ordinary_path(directory, package, directory=True)
        module, manifest, info = p.generic_module(directory)
        core.require(manifest['source_commit'] == metadata['source_commit'] and
            info['build'] == metadata['build'] and core.sha(module) == entry['module_sha256'],
            'Mode differs from its pinned source/runtime: '+name)
        variants[name] = module, manifest, info
    digest = core.sha(core.wire(metadata))
    selected = dict(metadata, lease_bytes=CONTRACTS[mode]['lease_bytes'] if mode else 0)
    return p, selected, variants, digest


def install(root, package, mode=None, profile=None, restore=False):
    if mode is None and restore is False:
        mode = 'baseline'
    if type(restore) is not bool or not ((restore and mode is None) or (not restore and mode in CONTRACTS)):
        raise ValueError('Select baseline, full-submit, reference-112 or restore')
    package, card_root = Path(package).resolve(strict=True), Path(root).resolve(strict=True)
    core = load_core(package)
    core.load_package = lambda directory: load_package(core, directory, mode)
    loaded = {}
    load_personalizer, publish = core.load_personalizer, core.publish
    def bound_personalizer(directory):
        p = load_personalizer(directory)
        loaded['personalizer'] = p
        select_profile = p.select_profile
        def capture_profile(*arguments, **keywords):
            item = select_profile(*arguments, **keywords)
            loaded['selected'] = item
            return item
        p.select_profile = capture_profile
        return p
    attempted = False
    def guarded_publish(path, data):
        nonlocal attempted
        if not restore and path == card_root/DESTINATION and not attempted:
            p, item = loaded['personalizer'], loaded['selected']
            core.require(p.read_regular(item['path'], card_root, expected=p.WIRE_BYTES) == item['wire'] and
                p.read_regular(item['path'].parent/'identity.bin', card_root, expected=16) == item['identity'],
                'Prepared profile/identity changed before loader publication')
            attempted = True
        return publish(path, data)
    core.load_personalizer, core.publish = bound_personalizer, guarded_publish
    result = core.install(card_root, package, mode, profile, restore)
    result['mode'] = result.pop('transport')
    result['storage_transport'] = 'SCI'
    if restore:
        result.pop('lease_bytes', None)
        result['candidate_build'] = result['build']
        p = loaded['personalizer']
        result['build'] = p.verify(p.read_regular(card_root/DESTINATION, card_root, maximum=core.LIMIT))['build']
    else:
        result['controller_submit'] = CONTRACTS[mode]['controller_submit']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    choice = parser.add_mutually_exclusive_group()
    choice.add_argument('--mode', choices=tuple(CONTRACTS), help='Default: baseline (72 KiB split controller)')
    choice.add_argument('--restore', action='store_true')
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = install(args.sd_root, args.package, args.mode, args.profile, args.restore)
        print(json.dumps(result, indent=2))
        if args.restore:
            print('Exact pre-package ordinary loader restored; earlier test receipts are unchanged.')
        else:
            print('Use SCI / Background Off (Standard) / saved Emulated VMU Off / CD music Off / RAM patches Off.')
            print('This module supplies virtual A2. Keep the same prepared profile for every comparison mode.')
            if result['mode'] in ('baseline', 'full-submit'):
                print('72 KiB mode refuses Maple command lists longer than 4 KiB; report any visible fault.')
        print('Safely eject before booting. Restore this package with the same installer and --restore.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Skies next installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
