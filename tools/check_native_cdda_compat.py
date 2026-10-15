#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check stage-only native CDDA compatibility builds against exact adapters.

Each experimental directory has a sibling native-reference-PROFILE build of
the same configuration and stamp with NATIVE_COMPAT=0. The reference is never
installed: byte equality proves that this feature changed no playback worker
or low reader. These are build checks; they do not claim console compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_loader_layout import inspect_elf, padded
from check_mdk2_cdda import check_directory as check_mdk2, stack_records
from runtime_package import verify


def require(condition, message):
    if not condition:
        raise ValueError(message)


def config(directory):
    result = {}
    for row in (directory / 'build-config').read_text().splitlines():
        require('=' in row, 'Malformed native build configuration')
        key, value = row.split('=', 1)
        require(key not in result, 'Duplicate native build configuration')
        result[key] = value
    return result


def expected_descriptor(profile):
    mdk2 = profile == 'mdk2'
    values = (0x4e49554b, 0x31304443, 1, 128, 2 if mdk2 else 1,
              int(profile == 'toy-cooked'), 256 if mdk2 else 64,
              0x8c004000, 0x8c007800, 0xac007800, 0xac007d00,
              0x8cfd0000, 0x8cfe0000, 0x8d000000,
              750804 if mdk2 else 748444, 0x682d90e3 if mdk2 else 0xcdc493b3)
    boot = ('ac5826c9a371de04207748e60e49e0a3c13a39468902f89d8c1c82e1f544bb48' if mdk2 else
            'ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd')
    driver = ('af8f90440edceacf33a90d094c9b570d8d40c2774a5f6067953277859a6ba413' if mdk2 else
              '477ede3766c27fa58e4c14d5218c583b7806a29c328a6e0d4965293375b704e5')
    return struct.pack('<16I', *values) + bytes.fromhex(boot + driver)


def image(directory, name, base, limit, private=False, worker=False):
    result = inspect_elf((directory / (name + '.elf')).read_bytes(), base, limit,
                         private_p2=private, entry_at_base=not worker,
                         entry_symbol='_kui_toy_pilot_initialize' if worker else '_start')
    if name != 'entry':
        require(padded((directory / (name + '.bin')).read_bytes()) == padded(result['payload']),
                name + ' binary differs from its ELF')
    return result


def embedded(stage, start, end, expected, label):
    symbols = stage['symbols']
    first, last = symbols.get(start, 0), symbols.get(end, 0)
    require(0x8ce00000 <= first <= last <= 0x8ce00000 + len(stage['payload']),
            'Invalid embedded ' + label + ' bounds')
    require(padded(stage['payload'][first - 0x8ce00000:last - 0x8ce00000]) == padded(expected),
            'Embedded ' + label + ' differs from its actual linked image')


def check_directory(directory, profile, reference=None):
    directory = Path(directory)
    require(profile in ('toy-raw', 'toy-cooked', 'mdk2'), 'Unknown native adapter profile')
    reference = Path(reference) if reference else directory.parent / ('native-reference-' + profile)
    current, retained = config(directory), config(reference)
    require(current.pop('NATIVE_COMPAT', None) == '1', 'Native compatibility selector must be 1')
    require(retained.pop('NATIVE_COMPAT', '0') == '0', 'Reference must use exact admission')
    require(current == retained, 'Experimental and reference configurations differ beyond admission')
    require(current.get('PRIVATE_P2') == '1' and current.get('SCI') == '1',
            'Native adapter needs its retained P2/SCI allocation')
    if profile != 'mdk2':
        require(current.get('NATIVE_CACHE') == '0' and current.get('DATA_PIPELINE_PROFILE') == '2' and
                current.get('COOKED_DATA_PROFILE', '0') == str(int(profile == 'toy-cooked')),
                'Toy compatibility adapter differs from retained AP/AT playback')
    else:
        check_mdk2(directory)
        check_mdk2(reference)
    images = {
        'resident-sci': image(directory, 'resident-sci', 0x8c004000, 0x8c007800, private=True),
        'worker': image(directory, 'worker', 0x8cfd0000, 0x8cfe0000, private=True, worker=True),
        'stage': image(directory, 'stage', 0x8ce00000, 0x8cfcffe0),
        'entry': image(directory, 'entry', 0x8c010000, 0x8c112000),
    }
    parity = {}
    for name in ('resident-sci', 'worker'):
        data = (directory / (name + '.bin')).read_bytes()
        require(data == (reference / (name + '.bin')).read_bytes(),
                'Compatibility feature changed retained ' + name + ' playback bytes')
        parity[name] = hashlib.sha256(data).hexdigest()
    stage, entry = images['stage'], images['entry']
    embedded(stage, '__toy_pilot_worker_blob_start', '__toy_pilot_worker_blob_end',
             images['worker']['payload'], 'worker')
    for transport in ('scif', 'sci', 'ide', 'scia'):
        prefix = '__retail_resident_' + transport + '_blob_'
        embedded(stage, prefix + 'start', prefix + 'end', images['resident-sci']['payload'],
                 transport + ' resident')
    first, last = (entry['symbols'].get('__retail_stage_blob_' + suffix, 0)
                   for suffix in ('start', 'end'))
    require(0x8c010000 <= first <= last <= 0x8c010000 + len(entry['payload']) and
            entry['payload'][first - 0x8c010000:last - 0x8c010000] == stage['payload'],
            'Entry does not embed the checked stage')
    descriptor = expected_descriptor(profile)
    offset = stage['symbols'].get('_kui_native_cdda_adapter_descriptor', 0) - 0x8ce00000
    require(0 <= offset <= len(stage['payload']) - len(descriptor) and
            stage['payload'][offset:offset + len(descriptor)] == descriptor and
            stage['payload'].count(b'KUINCD01') == 1,
            'Native typed descriptor is missing, duplicated or differs from the selected adapter')
    require(descriptor[64:96] in stage['payload'] and
            descriptor[96:] in images['worker']['payload'],
            'Original executable or ARM driver SHA guard is missing')
    low = stack_records(directory.glob('resident-sci.elf.ltrans*.su'))
    worker = stack_records((directory / 'worker').rglob('*.su'))
    # No new service path exists: parity above proves the exact same linked
    # reader/worker as the reference. Sum all emitted authored frames anyway;
    # this exceeds each retained nonrecursive path and includes bridge space.
    private_bound = 256 + sum(frame for _, frame in low + worker)
    require(private_bound <= 8192 - 32, 'Native private worker stack margin failed')
    worker_symbols = images['worker']['symbols']
    bottom, top = (worker_symbols.get('__toy_pilot_stack_' + end, 0) for end in ('bottom', 'top'))
    require(top - bottom == 8192 and 0xacfd0000 <= bottom < top <= 0xacfe0000,
            'Native worker private stack allocation differs')
    package_name = 'retail-mdk2-cdda.kui' if profile == 'mdk2' else 'retail-toy-pilot.kui'
    package = (directory / package_name).read_bytes()
    info = verify(package)
    require(package[64:] == entry['payload'] and info['build'] == current['BUILD'],
            'Native package differs from the checked entry/configuration')
    return {'profile': profile, 'experimental': True, 'console_tested': False,
            'descriptor_bytes': len(descriptor), 'package': info,
            'unchanged_reference_playback_sha256': parity,
            'private_worker_stack_upper_bound': private_bound,
            'private_worker_stack_capacity': 8192,
            'reference_directory': str(reference)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('profile', choices=('toy-raw', 'toy-cooked', 'mdk2'))
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    print(json.dumps(check_directory(args.directory, args.profile, args.reference), sort_keys=True))


if __name__ == '__main__':
    main()
