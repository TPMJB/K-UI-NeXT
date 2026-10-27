#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install the combined New reader and virtual A2 cached metadata candidate."""
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
    """Keep the proven backup/publication implementation in its existing module."""
    sys.path.insert(0, str(package/'tools'))
    spec = importlib.util.spec_from_file_location(
        'cached_metadata_isolation_core', package/'tools/install_vmu_native_isolation.py')
    require(spec and spec.loader, 'Missing package isolation installer')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_metadata(package, core):
    p = core.load_personalizer(package)
    metadata = json.loads(p.read_regular(package/'isolation.json', package, maximum=16384))
    require(isinstance(metadata, dict) and metadata.get('schema') == SCHEMA and
            metadata.get('cached_metadata') is True and metadata.get('public_variant') == 'combined' and
            set(metadata.get('variants', {})) == set(MODULES),
            'This installer requires the combined cached metadata package')
    for name, destination in MODULES.items():
        combined = name == 'reader-only'
        entry = metadata['variants'][name]
        require(isinstance(entry, dict) and entry.get('directory') == 'variants/'+name and
                entry.get('destination') == str(destination) and
                entry.get('public_variant') == ('combined' if combined else name) and
                entry.get('reader') == ('New (X)' if combined else 'Standard'),
                'Cached metadata variant routing differs: '+name)
        expected = dict(cached_metadata=combined, virtual_a2=True,
                        maple_c_callbacks_enabled=True, controller_tick_off=combined,
                        prefetch_enabled=False, e1_wait_capture=False, maple_bypass=False,
                        maple_delay=False, support_hooks_retained=True,
                        heap_reclaim_coherence_guards_retained=True)
        require(all(entry.get(key) is value for key, value in expected.items()),
                'Cached metadata variant capabilities differ: '+name)
        raw = p.read_regular(package/entry['directory']/'variant-layout.json', package, maximum=32768)
        layout = json.loads(raw)
        require(isinstance(layout, dict) and
                entry.get('linked_layout_sha256') == hashlib.sha256(raw).hexdigest() and
                layout.get('variant') == name and layout.get('public_variant') == entry['public_variant'] and
                isinstance(entry.get('module_sha256'), str) and
                len(entry['module_sha256']) == 64 and
                layout.get('module_sha256') == entry['module_sha256'] and
                all(layout.get(key) is expected[key] for key in (
                    'cached_metadata', 'virtual_a2', 'maple_c_callbacks_enabled',
                    'controller_tick_off', 'prefetch_enabled', 'e1_wait_capture',
                    'maple_bypass', 'maple_delay', 'heap_reclaim_coherence_guards_retained')),
                'Cached metadata linked module binding differs: '+name)
    return metadata


def install(root, package, variant, profile=None):
    require(variant in PUBLIC_VARIANTS, 'Select combined, vmu-only or restore')
    package = Path(package).resolve(strict=True)
    core = load_core(package)
    metadata = validate_metadata(package, core)
    result = core.install(root, package, PUBLIC_VARIANTS[variant], profile)
    entry = metadata['variants'].get(PUBLIC_VARIANTS[variant], {})
    result.update(variant=variant, cached_metadata=entry.get('cached_metadata', False),
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
            print('Real Maple C callbacks are enabled. Controller TICK and read-ahead are OFF.')
            print('Read-only VMU metadata uses the cached view; SDK, heap and MMIO reads remain coherent.')
            print('Check the same Evolution transition, then SAVE/LOAD if it boots.')
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
