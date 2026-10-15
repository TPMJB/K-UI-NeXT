#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inventory and group CE ROM components from owner-supplied extracted kernels.

Accepts one or more 0WINCEOS.BIN paths. Writes metadata JSON to stdout; never
extracts retail bytes, decompresses sections, patches games or admits playback.
Groups mean identical stored section layouts and hashes, not a compatible ABI.
"""
import argparse
import hashlib
import json
import struct

from armada_ce_rom_audit import ROMInventory


MAX_KERNELS = 128


def payload_identity(module):
    # Physical storage and nominal placement may differ between ROMs. Preserve
    # section layout/compression while comparing the exact stored payload bytes.
    fields = ('virtual_bytes', 'rva', 'stored_bytes', 'flags', 'compressed',
              'stored_sha256')
    shape = {'entry_rva': module['entry_rva'],
             'virtual_bytes': module['virtual_bytes'],
             'object_flags': module['object_flags'],
             'directories': module['directories'],
             'sections': [{key: section[key] for key in fields}
                          for section in module['sections']]}
    return hashlib.sha256(json.dumps(shape, sort_keys=True,
                                     separators=(',', ':')).encode()).hexdigest()


def inventory(paths):
    if not 1 <= len(paths) <= MAX_KERNELS:
        raise ValueError('Supply between 1 and 128 kernel paths')
    kernels, groups, sections, files = [], {}, {}, {}
    for index, path in enumerate(paths):
        rom = ROMInventory(path)
        modules = []
        for name, module in sorted(rom.modules.items()):
            identity = payload_identity(module)
            modules.append({**module, 'stored_payload_identity': identity})
            groups.setdefault((name, identity), []).append(index)
            for section in module['sections']:
                if section['stored_bytes']:
                    key = (name, section['rva'], section['virtual_bytes'],
                           section['stored_bytes'], section['flags'],
                           section['compressed'], section['stored_sha256'])
                    sections.setdefault(key, set()).add(index)
        for file in rom.files:
            key = (file['name'].lower(), file['bytes'], file['stored_bytes'],
                   file['compressed'], file['stored_sha256'])
            files.setdefault(key, set()).add(index)
        kernels.append(dict(source=str(path), bytes=len(rom.data),
                            sha256=hashlib.sha256(rom.data).hexdigest(),
                            load_header=rom.load_header,
                            rom_header_address=rom.header_address,
                            rom_header_provenance=rom.header_provenance,
                            rom_header=rom.header, modules=modules,
                            rom_files=rom.files, copy_records=rom.copies))
    return dict(format='kui-wince-runtime-inventory-v1',
                playback_admitted=False,
                meaning='Stored component identity only; live mappings, APIs, '
                        'memory ownership and compatibility remain unproven.',
                compressed_sections='Inventoried and hashed as stored; not decoded.',
                kernels=kernels,
                component_groups=[dict(module=name, stored_payload_identity=identity,
                                       kernel_indices=indices)
                                  for (name, identity), indices in sorted(groups.items())],
                section_groups=[dict(module=name, rva=rva, virtual_bytes=virtual,
                                     stored_bytes=stored, flags=flags,
                                     compressed=compressed, stored_sha256=digest,
                                     kernel_indices=sorted(indices))
                                for (name, rva, virtual, stored, flags, compressed, digest),
                                indices in sorted(sections.items())],
                rom_file_groups=[dict(name=name, bytes=size, stored_bytes=stored,
                                      compressed=compressed, stored_sha256=digest,
                                      kernel_indices=sorted(indices))
                                 for (name, size, stored, compressed, digest), indices
                                 in sorted(files.items())])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernels', nargs='+', metavar='0WINCEOS.BIN')
    args = parser.parse_args()
    try:
        print(json.dumps(inventory(args.kernels), indent=2))
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        parser.exit(1, 'Inventory failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
