#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove preservation against pinned pre-delay control bytes after build labels.

The old ELF/build directories need not survive. These SHA256 pins come from
the independently retained reader-component-comparison.json (8adf1a039035)
and standard-control-comparison.json (9956562f91d6) receipts in the passing
cec542aefa8a delivery. Normalize only literal labels, the worker digest and
CRC in its exact relocation wire, and the module's two envelope CRCs. Current
relocations are first regenerated from the actual ELF and binary. A complete
normalized file must match its pinned old SHA256; arbitrary differences or
relocation-record changes cannot be hidden by this normalization.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

from runtime_package import envelope, verify
from vmu_native_relocations import generate


READER_BUILD = '8adf1a039035'
STANDARD_BUILD = '9956562f91d6'
READER_PINS = {
    'resident-sci.bin': '9bf29fb1032bd0eff2b9174dc70ccfc704ca61154b0f486d9966559ea03e23cc',
    'resident-legacy.bin': 'a81d7d2af6d6ed8d9f137889e0d6247c12e8cd71da6ebb28dfd3a8b3f71d1213',
    'native-ra-aux.bin': '61ab1f513910fce19665d742ad872bea92ee69afe1af6030d09ecb04f96d0974',
    'native-ra-aux.reloc': '87ceef2187655c6c0bbe70d6844067785426f10afacabbab568fcf1c83cc9dfa',
}
# name: (complete old SHA256, exact old byte count, literal label count)
STANDARD_PINS = {
    'native-worker.bin': ('a0834174c683e2768691b9e54d08a1f7cc6409c606b343be8ee2910babb4a503', 55232, 1),
    'native-worker.reloc': ('7bd35814049d0f69c9cf8c4e6c94375b16de0a0ab64f9daea4f9fdbb450a5054', 6704, 0),
    'resident-sci.bin': ('0ea7ef2bd1e9babb100cdfd08c6655ae3a231529beb6254f6401d18dbfc1226f', 12220, 1),
    'retail-maple-observe.kui': ('8d1faa5d3a16542ae6966c846552dba930b61b03e69c5c6d814772c7fed94133', 219568, 7),
    'retail-native-isolation-standard.kui': ('8d1faa5d3a16542ae6966c846552dba930b61b03e69c5c6d814772c7fed94133', 219568, 7),
    'stage.bin': ('b355b1c3d84f8eb7e9fb71098ed373c18b0944ad6e9180d86ad68297de9bb53d', 211312, 6),
    'vmu-sd-profile.bin': ('147eb84a549e91b8121f363980f601bd2d80212c880f24ffeaa4fb180b6e9860', 4096, 0),
    'worker.bin': ('9f80f33e4aeeb302dd9aab8654d9fc3f3b72f28787aa4d2fd2f2b65db01852ab', 5336, 1),
}
REFERENCE_RECEIPTS = {
    'reader-component-comparison.json': '9280332da4aeeab7e04dcb3e8942427094f9e9275bc70798bcaa1f5d3c375706',
    'standard-control-comparison.json': 'eeb1527cb027bb59dd0e1dd63aadaa24adbb4cbba2f3d79b2c7d21ca54a14f42',
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_label(directory):
    info = verify((directory / 'retail-maple-observe.kui').read_bytes())
    return info['build']


def labels(data, current, old, expected=None):
    require(re.fullmatch(r'[0-9a-f]{12}', current) is not None,
            'invalid current build label')
    count = data.count(current.encode())
    if expected is not None:
        require(count == expected, 'unexpected literal build-label count')
    return data.replace(current.encode(), old.encode()), count


def current_relocation(directory, stem):
    binary = (directory / (stem + '.bin')).read_bytes()
    wire = (directory / (stem + '.reloc')).read_bytes()
    link_base, span_bytes = ((0x8c421000, 0x7000) if stem == 'native-ra-aux'
                             else (0x8c100000, 0x20000))
    # generate checks the ELF's allocated sections, load aliases, pointer
    # records, ordering and complete correspondence to the flat binary.
    require(generate((directory / (stem + '.elf')).read_bytes(), binary,
                     link_base, span_bytes) == wire,
            stem + ' relocation is not the exact current ELF extraction')
    require(len(wire) >= 72 and wire[:8] == b'KUIVRL01', 'invalid relocation header')
    version, header, size, span, base, records, total, crc = struct.unpack_from('<8I', wire, 8)
    require(version == 1 and header == 72 and size == len(binary) and
            total == len(wire) == 72 + records * 8 and 0 < size <= span and
            0x8c000000 <= base < 0x8d000000 and not base & 31,
            'invalid relocation fields')
    cleared = bytearray(wire)
    struct.pack_into('<I', cleared, 36, 0)
    require(zlib.crc32(cleared) == crc, 'current relocation CRC32 mismatch')
    require(wire[40:72] == hashlib.sha256(binary).digest(),
            'current relocation binary SHA256 mismatch')
    return binary, wire


def relocation_digest(wire, binary):
    result = bytearray(wire)
    result[40:72] = hashlib.sha256(binary).digest()
    struct.pack_into('<I', result, 36, 0)
    struct.pack_into('<I', result, 36, zlib.crc32(result))
    # Only the documented digest and CRC fields may change.
    require(result[:36] == wire[:36] and result[72:] == wire[72:],
            'relocation normalization changed metadata or pointer records')
    return bytes(result)


def embedded_relocation(data, wire, normalized):
    require(data.count(wire) == 1, 'embedded relocation wire lacks one unique occurrence')
    return data.replace(wire, normalized, 1)


def check_reader(directory, current):
    # The auxiliary table must still bind the complete actual auxiliary ELF.
    current_relocation(directory, 'native-ra-aux')
    report = {}
    for name, old_sha in READER_PINS.items():
        data = (directory / name).read_bytes()
        normalized, count = labels(data, current, READER_BUILD,
                                   0 if name.startswith('native-ra-aux') else 1)
        require(sha(normalized) == old_sha, 'reader component changed beyond its build label: ' + name)
        report[name] = dict(old_sha256=old_sha, new_sha256=sha(data),
                           identical_after_build_label_normalization=True,
                           normalized_sha256=sha(normalized), label_occurrences=count)
    return report


def check_standard(directory, current):
    binary, wire = current_relocation(directory, 'native-worker')
    normalized_binary, _ = labels(binary, current, STANDARD_BUILD, 1)
    normalized_wire = relocation_digest(wire, normalized_binary)
    components = []
    for name, (old_sha, size, expected_labels) in STANDARD_PINS.items():
        data = (directory / name).read_bytes()
        require(len(data) == size, 'Standard component byte count changed: ' + name)
        count = data.count(current.encode())
        require(count == expected_labels, 'unexpected Standard literal label count: ' + name)
        if name == 'native-worker.reloc':
            normalized = normalized_wire
            normalization = ['SHA256 of label-normalized native-worker.bin', 'relocation CRC32']
        elif name == 'stage.bin':
            normalized = embedded_relocation(data, wire, normalized_wire)
            normalized, _ = labels(normalized, current, STANDARD_BUILD, expected_labels)
            normalization = ['literal build labels',
                             'embedded worker relocation SHA256 and relocation CRC32']
        elif name.endswith('.kui'):
            info = verify(data)
            require(info['build'] == current and
                    envelope(data[64:], info['memory_bytes'], current) == data,
                    'Standard module envelope is not canonical')
            payload = embedded_relocation(data[64:], wire, normalized_wire)
            payload, _ = labels(payload, current, STANDARD_BUILD, expected_labels - 1)
            normalized = envelope(payload, info['memory_bytes'], STANDARD_BUILD)
            verify(normalized)
            normalization = ['literal build labels',
                             'embedded worker relocation SHA256 and relocation CRC32',
                             'module payload CRC32', 'module header CRC32']
        else:
            normalized, _ = labels(data, current, STANDARD_BUILD, expected_labels)
            normalization = ['literal build labels'] if count else []
        require(sha(normalized) == old_sha, 'Standard component changed beyond known derived label fields: ' + name)
        components.append(dict(name=name, bytes=len(data), old_sha256=old_sha,
                               new_sha256=sha(data), normalized_sha256=sha(normalized),
                               raw_equal=sha(data) == old_sha, normalized_equal=True,
                               normalization=normalization, label_occurrences=count))
    return dict(schema='kui.native.standard-isolation.label-only-binary-comparison.v1',
                scope='Standard VMU control raw binaries only; no console, IRQ timing or PVR behavior claim',
                old_build=STANDARD_BUILD, new_build=current,
                old_directory='pinned complete old SHA256 values from the passing cec542aefa8a delivery',
                new_directory=str(directory.resolve()),
                relocation_records_equal=True, relocation_sha256_and_crc32_valid=True,
                module_headers_and_payload_crc32_valid=True, components=components,
                all_components_normalized_equal=True,
                reference_receipt_sha256=REFERENCE_RECEIPTS['standard-control-comparison.json'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reader-build', type=Path, required=True)
    parser.add_argument('--standard-build', type=Path, required=True)
    parser.add_argument('--reader-output', type=Path, required=True)
    parser.add_argument('--standard-output', type=Path, required=True)
    args = parser.parse_args()
    reader_label, standard_label = build_label(args.reader_build), build_label(args.standard_build)
    require(reader_label == standard_label, 'isolation build labels differ')
    reader = check_reader(args.reader_build, reader_label)
    standard = check_standard(args.standard_build, standard_label)
    for destination, report in ((args.reader_output, reader), (args.standard_output, standard)):
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(reader_components=len(reader), standard_components=len(standard['components']),
                          all_components_normalized_equal=True, build=reader_label)))


if __name__ == '__main__':
    main()
