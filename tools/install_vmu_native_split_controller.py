#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install the combined New reader and virtual A2 split-controller candidate."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

SCHEMA = 'kui.native.isolation.delivery.v1'
MODULES = {'reader-only': Path('KUI/apps/games/retail-native-dual.kui'),
           'vmu-only': Path('KUI/apps/games/retail-boot.kui')}
PUBLIC_VARIANTS = {'combined': 'reader-only', 'vmu-only': 'vmu-only', 'restore': 'restore'}


def require(value, message):
    if not value:
        raise ValueError(message)


def load_core(package):
    """Reuse the existing backup, publication and exact-restore implementation."""
    sys.path.insert(0, str(package/'tools'))
    spec = importlib.util.spec_from_file_location(
        'split_controller_isolation_core', package/'tools/install_vmu_native_isolation.py')
    require(spec and spec.loader, 'Missing package isolation installer')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_metadata(package, core):
    p = core.load_personalizer(package)
    metadata = json.loads(p.read_regular(package/'isolation.json', package, maximum=16384))
    require(isinstance(metadata, dict) and metadata.get('schema') == SCHEMA and
            metadata.get('split_controller') is True and metadata.get('cached_metadata') is False and
            metadata.get('public_variant') == 'combined' and
            set(metadata.get('variants', {})) == set(MODULES),
            'This installer requires the combined split-controller package')
    for name, destination in MODULES.items():
        combined = name == 'reader-only'
        entry = metadata['variants'][name]
        require(isinstance(entry, dict) and entry.get('directory') == 'variants/'+name and
                entry.get('destination') == str(destination) and
                entry.get('public_variant') == ('combined' if combined else name) and
                entry.get('reader') == ('New (X)' if combined else 'Standard'),
                'Split-controller variant routing differs: '+name)
        expected = dict(split_controller=combined, cached_metadata=False, virtual_a2=True,
                        maple_c_callbacks_enabled=True, controller_tick_off=combined,
                        prefetch_enabled=False, e1_wait_capture=False, maple_bypass=False,
                        maple_delay=False, completion_delay=False, submission_delay=False,
                        post_start_delay=False, support_hooks_retained=True,
                        heap_reclaim_coherence_guards_retained=True)
        require(all(entry.get(key) is value for key, value in expected.items()),
                'Split-controller variant capabilities differ: '+name)
        raw = p.read_regular(package/entry['directory']/'variant-layout.json', package, maximum=32768)
        layout = json.loads(raw)
        require(isinstance(layout, dict) and
                entry.get('linked_layout_sha256') == hashlib.sha256(raw).hexdigest() and
                layout.get('variant') == name and layout.get('public_variant') == entry['public_variant'] and
                isinstance(entry.get('module_sha256'), str) and len(entry['module_sha256']) == 64 and
                layout.get('module_sha256') == entry['module_sha256'] and
                all(layout.get(key) is value for key, value in expected.items()),
                'Split-controller linked module binding differs: '+name)
    return metadata


def install(root, package, variant, profile=None):
    require(variant in PUBLIC_VARIANTS, 'Select combined, vmu-only or restore')
    package = Path(package).resolve(strict=True)
    core = load_core(package)
    metadata = validate_metadata(package, core)
    result = core.install(root, package, PUBLIC_VARIANTS[variant], profile)
    entry = metadata['variants'].get(PUBLIC_VARIANTS[variant], {})
    result.update(variant=variant, split_controller=entry.get('split_controller', False),
                  cached_metadata=entry.get('cached_metadata', False),
                  virtual_a2=entry.get('virtual_a2'),
                  maple_c_callbacks_enabled=entry.get('maple_c_callbacks_enabled'),
                  controller_tick_off=entry.get('controller_tick_off'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--variant', required=True, choices=tuple(PUBLIC_VARIANTS))
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = install(args.sd_root, args.package, args.variant, args.profile)
        print(json.dumps(result, indent=2))
        if args.variant == 'combined':
            print('Use SCI / Background New (X). Virtual A2 is ON.')
            print('Virtual VMU handling is enabled. Controller TICK and read-ahead are OFF.')
            print('The split-controller path uses assembly submit capture and full completion checks; cached metadata is OFF.')
            print('Check virtual A2 and the same Sofdec transition, then SAVE/LOAD when a save point is available.')
        elif args.variant == 'vmu-only':
            print('Use SCI / Background Off (Standard). Virtual A2 is ON.')
            print('The ordinary Standard module is replaced until --variant restore is run.')
        else:
            print('Both original modules restored.')
        print('GUI, prepared saves, profile and launch settings were not changed.')
        print('Safely eject before booting. Restore afterward with the same command and --variant restore.')
    except (OSError, ValueError, KeyError, TypeError, UnicodeError) as error:
        parser.exit(2, 'Combined installation refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
