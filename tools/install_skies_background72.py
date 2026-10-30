#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install or restore compact Background, optionally with the 72 KiB Standard loader."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

SCHEMA = 'kui.native.skies-background72.delivery.v1'
BACKUP_SCHEMA = 'kui.native.skies-background72.backup.v1'
STATE_SCHEMA = 'kui.native.skies-background72.state.v1'
PARENT = Path('KUI/skies-background72')
DESTINATIONS = {
    'standalone': Path('KUI/apps/games/retail-boot.kui'),
    'background': Path('KUI/apps/games/retail-native-dual.kui'),
}
CONTRACTS = {
    'standalone': dict(transport=1, lease_bytes=0x12000, worker_bytes=0x12000,
        cache_bytes=0, controller_submit='split', background_reader_enabled=False,
        prefetch_enabled=False, receive_engine='standard-paced-dma'),
    'background': dict(transport=1, lease_bytes=0x47000, worker_bytes=0x12000,
        cache_bytes=0x30000, controller_submit='split', background_reader_enabled=True,
        prefetch_enabled=True, receive_engine='background-async-dma'),
}


def load_core(package, component):
    path = Path(package)/'tools/install_native_vmu_only.py'
    if not path.is_file() or path.is_symlink():
        raise ValueError('Missing ordinary packaged installer core')
    spec = importlib.util.spec_from_file_location('background72_private_'+component, path)
    if not spec or not spec.loader:
        raise ValueError('Cannot load packaged installer core')
    core = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    core.BACKUP_SCHEMA, core.STATE_SCHEMA = BACKUP_SCHEMA, STATE_SCHEMA
    core.PARENT = PARENT/component
    core.BACKUP, core.STATE = core.PARENT/'backup', core.PARENT/'state.json'
    core.DESTINATION = DESTINATIONS[component]
    core.TRANSPORTS = {component: 1}
    return core


def load_package(core, package):
    p = core.load_personalizer(package)
    metadata = json.loads(p.read_regular(package/'skies-background72.json', package, maximum=16384))
    core.require(isinstance(metadata, dict) and set(metadata) ==
        {'schema', 'source_commit', 'build', 'variants'} and metadata['schema'] == SCHEMA and
        isinstance(metadata['source_commit'], str) and re.fullmatch('[0-9a-f]{40}', metadata['source_commit']) and
        metadata['build'] == metadata['source_commit'][:12] and
        isinstance(metadata['variants'], dict) and set(metadata['variants']) == set(CONTRACTS),
        'Requires the complete paired 72 KiB loader package')
    variants = {}
    for component, geometry in CONTRACTS.items():
        entry = metadata['variants'][component]
        core.require(isinstance(entry, dict) and set(entry) == set(geometry)|{'directory', 'module_sha256'} and
            entry['directory'] == 'variants/'+component and core.digest(entry['module_sha256']) and
            all(type(entry[key]) is type(value) and entry[key] == value for key, value in geometry.items()),
            'Loader geometry/reader contract differs: '+component)
        directory = package/entry['directory']
        p.ordinary_path(directory, package, directory=True)
        module, manifest, info = p.generic_module(directory)
        core.require(manifest['source_commit'] == metadata['source_commit'] and
            info['build'] == metadata['build'] and core.sha(module) == entry['module_sha256'],
            'Loader differs from its pinned source/runtime: '+component)
        variants[component] = module, manifest, info
    return p, metadata, variants, core.sha(core.wire(metadata))


def install(root, package, profile=None, restore=False, background_only=False):
    if type(restore) is not bool or type(background_only) is not bool:
        raise ValueError('Restore/background-only must be booleans')
    root, package = Path(root).resolve(strict=True), Path(package).resolve(strict=True)
    components = ('background',) if background_only else tuple(CONTRACTS)
    cores = {name: load_core(package, name) for name in components}
    first = cores[components[0]]
    p, metadata, variants, package_sha = load_package(first, package)
    selected = None if restore or (background_only and profile is None) else p.select_profile(root, profile)
    plans = {}
    # Validate the complete package and every selected destination before backups.
    for component, core in cores.items():
        p.ordinary_path(root/core.DESTINATION.parent, root, directory=True)
        core.directory_preflight(p, root, core.PARENT)
        core.directory_preflight(p, root, core.BACKUP)
        current = p.read_regular(root/core.DESTINATION, root, maximum=core.LIMIT)
        p.verify(current)
        have_backup = (root/core.BACKUP).exists() or (root/core.BACKUP).is_symlink()
        core.require(have_backup or core.optional(p, root, core.STATE) is None,
                     'Loader receipt has no exact backup: '+component)
        core.require(have_backup or not restore, 'No paired loader backup exists: '+component)
        original = core.read_backup(p, root, package_sha) if have_backup else current
        state = core.read_state(p, root, package_sha, original)
        core.require((state is None and current == original) or
            (state is not None and core.sha(current) in state['allowed'] and
             (current == original or current == variants[component][0] or
              core.canonical_candidate(p, current, {component: variants[component]}))),
            'Current loader changed outside this package: '+component)
        if restore:
            target, active = original, 'restore'
        else:
            module, manifest, info = variants[component]
            target = p.personalize_bytes(module, manifest, info, selected['wire'], runtime_mode=0) if selected else module
            active = component
        final = core.make_state(package_sha, original, [core.sha(target)], active, False)
        plans[component] = dict(current=current, original=original, state=state, target=target,
            final=final, active=active, have_backup=have_backup)
    result = dict(build=metadata['build'], storage_transport='SCI', restored=restore,
                  background_only=background_only, components={})
    for component, plan in plans.items():
        item = dict(destination=str(DESTINATIONS[component]), module_sha256=first.sha(plan['target']),
                    build=p.verify(plan['target'])['build'], already_installed=plan['current'] == plan['target'])
        if not restore:
            item.update(CONTRACTS[component])
        result['components'][component] = item
    if selected:
        result.update(profile=str(selected['path'].relative_to(root)), pair_path=selected['summary']['pair_path'])
    if all(plan['current'] == plan['target'] and plan['state'] == plan['final'] for plan in plans.values()):
        return result

    def profile_guard():
        if selected:
            first.require(p.read_regular(selected['path'], root, expected=p.WIRE_BYTES) == selected['wire'] and
                p.read_regular(selected['path'].parent/'identity.bin', root, expected=16) == selected['identity'],
                'Prepared profile/identity changed before loader publication')

    for component, core in cores.items():
        first.require(p.read_regular(root/core.DESTINATION, root, maximum=core.LIMIT) == plans[component]['current'],
                      'Loader changed during paired preflight: '+component)
    profile_guard()
    for component, core in cores.items():
        plan = plans[component]
        if not plan['have_backup']:
            core.create_backup(p, root, package_sha, plan['original'])
            core.require(core.read_backup(p, root, package_sha) == plan['original'], 'Loader backup readback differs')
    # Any failure from the first pending receipt through final receipts rolls
    # back this invocation's exact previous loaders. Durable pending receipts
    # admit either exact candidate if rollback itself cannot complete.
    pending_written = []
    try:
        for component, core in cores.items():
            plan = plans[component]
            pending = core.make_state(package_sha, plan['original'],
                [core.sha(plan['current']), core.sha(plan['target'])], plan['active'], True)
            pending_written.append(component)
            core.publish(root/core.STATE, core.wire(pending))
        for component, core in cores.items():
            plan = plans[component]
            if plan['current'] != plan['target']:
                profile_guard()
                core.require(p.read_regular(root/core.DESTINATION, root, maximum=core.LIMIT) == plan['current'],
                             'Loader changed before publication: '+component)
                core.publish(root/core.DESTINATION, plan['target'])
                core.require(p.read_regular(root/core.DESTINATION, root, maximum=core.LIMIT) == plan['target'],
                             'Installed loader readback differs: '+component)
        for component, core in cores.items():
            core.publish(root/core.STATE, core.wire(plans[component]['final']))
    except BaseException:
        for component in reversed(pending_written):
            core, plan = cores[component], plans[component]
            now = p.read_regular(root/core.DESTINATION, root, maximum=core.LIMIT)
            if now == plan['target'] and now != plan['current']:
                core.publish(root/core.DESTINATION, plan['current'])
            if p.read_regular(root/core.DESTINATION, root, maximum=core.LIMIT) == plan['current']:
                previous = plan['state'] or core.make_state(package_sha, plan['original'],
                    [core.sha(plan['current'])], 'restore', False)
                core.publish(root/core.STATE, core.wire(previous))
        raise
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--restore', action='store_true')
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--background-only', action='store_true',
                        help='Install/restore only Background; no prepared profile is needed')
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        print(json.dumps(install(args.sd_root, args.package, args.profile, args.restore, args.background_only), indent=2))
        if args.restore:
            print(('Exact pre-package Background loader restored.' if args.background_only else
                   'Both exact pre-package loaders restored.')+' GUI restoration is independent: install-ui.py --restore.')
        else:
            print('Compact Background installed.' if args.background_only else 'Both 72 KiB VMU loaders installed.')
            print('Install the matching GUI with install-ui.py. Background VMU On uses the GUI game-card assignment.')
            print('Import and assign your card through VMU tools as described in README-FIRST.md.')
            if not args.background_only:print('Standard uses the selected prepared profile independently.')
        print('Cards, journals, assignments, saved settings and earlier backups are unchanged. Safely eject before booting.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Paired 72 KiB loader installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
