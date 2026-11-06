#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a bounded immutable compressed runtime-module bundle for one GUI."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib
from runtime_package import verify

MAGIC = b'KUIMB01\0'
HEADER, ENTRY, MAX_BYTES = 96, 128, 4 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).digest()


def valid_path(path):
    return isinstance(path, str) and len(path) < 80 and '..' not in path and re.fullmatch(
        r'apps/(games|disc)/[a-z0-9][a-z0-9._-]*\.kui', path) is not None


def encode(members, build):
    if not re.fullmatch(r'[0-9a-f]{12}', build) or not 1 <= len(members) <= 64:
        raise ValueError('A build and 1–64 module members are required')
    entries, blobs = bytearray(), bytearray()
    offset = HEADER + len(members) * ENTRY
    for path, data in sorted(members.items()):
        if not valid_path(path) or verify(data)['build'] != build:
            raise ValueError('Module path or source build differs: ' + path)
        compressed = zlib.compress(data, 9)
        entry = bytearray(ENTRY)
        entry[:len(path)] = path.encode('ascii')
        struct.pack_into('<4I', entry, 80, len(data), len(compressed), offset, 0)
        entry[96:128] = sha(data)
        entries.extend(entry);blobs.extend(compressed);offset += len(compressed)
    if offset > MAX_BYTES:
        raise ValueError('Compressed module bundle exceeds 4 MiB')
    header = bytearray(HEADER)
    header[:8] = MAGIC
    struct.pack_into('<6I', header, 8, 1, HEADER, len(members), ENTRY, offset, 0)
    header[32:44] = build.encode('ascii');header[64:96] = sha(entries)
    return bytes(header + entries + blobs)


def decode(data):
    if not HEADER <= len(data) <= MAX_BYTES or data[:8] != MAGIC:
        raise ValueError('Invalid module bundle')
    version, header, count, entry, total, reserved = struct.unpack_from('<6I', data, 8)
    build = data[32:44].decode('ascii', errors='replace')
    if (version != 1 or header != HEADER or not 1 <= count <= 64 or entry != ENTRY or
            total != len(data) or reserved or any(data[44:64]) or not re.fullmatch(r'[0-9a-f]{12}', build)):
        raise ValueError('Invalid module bundle header')
    offset = HEADER + count * ENTRY
    if offset > len(data) or sha(data[HEADER:offset]) != data[64:96]:
        raise ValueError('Module bundle manifest checksum differs')
    output, previous = {}, ''
    for index in range(count):
        e = data[HEADER + index * ENTRY:HEADER + (index + 1) * ENTRY]
        path = e[:80].split(b'\0', 1)[0].decode('ascii', errors='replace')
        raw, compressed, at, zero = struct.unpack_from('<4I', e, 80)
        if (not valid_path(path) or path <= previous or e[:80] != path.encode('ascii') + bytes(80 - len(path)) or
                not 68 <= raw <= MAX_BYTES + 64 or not compressed or at != offset or zero or compressed > len(data) - offset):
            raise ValueError('Invalid module bundle entry')
        decoder = zlib.decompressobj()
        member = decoder.decompress(data[offset:offset + compressed], raw + 1)
        if (len(member) != raw or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail or
                sha(member) != e[96:128] or verify(member)['build'] != build):
            raise ValueError('Embedded module integrity differs: ' + path)
        output[path] = member;offset += compressed;previous = path
    if offset != len(data):
        raise ValueError('Trailing module bundle bytes')
    return output


def locate(payload):
    prefix = MAGIC + struct.pack('<II', 1, HEADER)
    found, start = [], 0
    while True:
        at = payload.find(prefix, start)
        if at < 0:
            break
        if at + HEADER > len(payload):
            raise ValueError('Truncated embedded module bundle')
        size = struct.unpack_from('<I', payload, at + 24)[0]
        if size < HEADER or size > len(payload) - at:
            raise ValueError('Embedded module bundle bounds differ')
        data = payload[at:at + size];decode(data);found.append((at, data));start = at + 1
    if len(found) != 1:
        raise ValueError('Requires exactly one linked runtime module bundle')
    return found[0]


def build(members, build_id, output):
    data = encode(members, build_id)
    if decode(data) != members:
        raise ValueError('Bundle round trip differs')
    output = Path(output).absolute();output.mkdir(parents=True, exist_ok=True)
    binary = output / 'bundle.bin';binary.write_bytes(data)
    quoted = str(binary).replace('\\', '\\\\').replace('"', '\\"')
    assembly = '.section .rodata.kui_runtime_module_bundle,"a"\n.balign 32\n'
    assembly += '.global __kui_runtime_module_bundle_start\n__kui_runtime_module_bundle_start:\n'
    assembly += '.incbin "' + quoted + '"\n.global __kui_runtime_module_bundle_end\n__kui_runtime_module_bundle_end:\n'
    (output / 'bundle.S').write_text(assembly)
    report = dict(schema='kui.runtime-module-bundle.v1', build=build_id, bytes=len(data),
        sha256=sha(data).hex(), members={path: dict(bytes=len(body), sha256=sha(body).hex())
        for path, body in sorted(members.items())})
    (output / 'bundle.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-id', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--member', action='append', required=True, metavar='LOGICAL_PATH=FILE')
    args = parser.parse_args()
    try:
        members = {}
        for value in args.member:
            logical, physical = value.split('=', 1)
            if logical in members:
                raise ValueError('Duplicate module member')
            path = Path(physical)
            if path.is_symlink() or not path.is_file():
                raise ValueError('Use a regular module file')
            members[logical] = path.read_bytes()
        print(json.dumps(build(members, args.build_id, args.output), indent=2))
    except (OSError, ValueError, zlib.error) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
