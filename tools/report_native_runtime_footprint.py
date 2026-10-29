#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure linked native runtime storage, keeping reservations and use separate.

Reports actual typed worker/auxiliary exports and linked BSS objects. This is
size accounting, not a replacement for allocation, relocation or stack proofs.
The complete game-heap lease excludes the separately linked low reader and the
temporary launcher stage; neither a .kui file size nor the worker alone is the
whole resident footprint.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_vmu_native_worker_gateway import read_elf, symbol_bytes


def require(value, message):
    if not value:
        raise ValueError(message)


def measure(directory):
    directory = Path(directory)
    worker = read_elf(directory / 'native-worker.elf')
    base, wire, _ = symbol_bytes(worker, '_kui_vmu_native_worker_exports', 1)
    require(len(wire) == 80, 'Expected complete persistent worker ABI v4')
    fields = struct.unpack('<20I', wire)
    require(fields[:3] == (0x4b4e5031, 4, 80), 'Unknown worker export identity')
    bss_begin, bss_end, stack_begin, stack_end, memory_end = fields[7:12]
    require(base <= bss_begin <= bss_end <= stack_begin < stack_end == memory_end,
            'Worker endpoints do not form a disjoint owned layout')
    worker_binary = (directory / 'native-worker.bin').read_bytes()
    require(len(worker_binary) == bss_begin - base, 'Worker image and BSS disagree')
    symbols = worker[4]
    marker = symbols.get('__kui_native_isolation_lease_bytes')
    require(marker and marker[1:] == (0, 0, 0xfff1), 'Missing exact lease-size marker')
    lease = marker[0]
    bss = []
    for name, (address, size, kind, owner) in symbols.items():
        if kind == 1 and size and bss_begin <= address < address + size <= bss_end:
            bss.append(dict(name=name, bytes=size))
    aux = read_elf(directory / 'native-ra-aux.elf')
    export_sections = [s for s in aux[3] if s[1] == 1 and s[2] & 2 and s[5] == 64
                       and aux[0][s[4]:s[4] + 12] == struct.pack('<3I', 0x4b524131, 1, 64)]
    require(len(export_sections) == 1, 'Missing bounded auxiliary export section')
    section = export_sections[0]
    a = struct.unpack('<16I', aux[0][section[4]:section[4] + 64])
    require(a[:3] == (0x4b524131, 1, 64), 'Unknown auxiliary export identity')
    image_end, aux_stack_begin, aux_stack_end, aux_end = a[6:10]
    aux_base = section[3]
    require(aux_base <= image_end <= aux_stack_begin - 0x20000000 <
            aux_stack_end - 0x20000000 == aux_end - 0x20000000,
            'Auxiliary endpoints overlap')
    # The stage places this fixed independently relocated auxiliary immediately
    # after the worker's map/reader page; the cache follows its complete span.
    aux_offset = memory_end - base + 0x1000
    aux_end_offset = aux_offset + aux_end - (aux_base + 0x20000000)
    # Both reviewed builds use the same independently checked 384-block data
    # cache. The historical layout may leave a gap before that final arena.
    cache_bytes = 192 * 1024
    cache_offset = lease - cache_bytes
    require(aux_end_offset <= cache_offset, 'Auxiliary overlaps the cache arena')
    result = dict(
        lease_payload_bytes=lease, allocator_header_bytes=32,
        physical_game_heap_cost_bytes=lease + 32,
        worker=dict(image_bytes=len(worker_binary), bss_bytes=bss_end - bss_begin,
                    stack_bytes=stack_end - stack_begin,
                    unused_before_stack_bytes=stack_begin - bss_end,
                    reserved_bytes=memory_end - base,
                    binary_sha256=hashlib.sha256(worker_binary).hexdigest(),
                    largest_bss_objects=sorted(bss, key=lambda r: (-r['bytes'], r['name']))[:16]),
        auxiliary=dict(image_bytes=image_end - aux_base,
                       stack_bytes=aux_stack_end - aux_stack_begin,
                       reserved_bytes=aux_end - (aux_base + 0x20000000)),
        prefetch_cache_bytes=cache_bytes,
        cache_offset=cache_offset,
        unused_before_cache_bytes=cache_offset - aux_end_offset,
        notes=['Temporary launch stage and separate low reader are excluded from this game-heap lease.',
               'Reserved spans include internal alignment and stack headroom.',
               'This report does not prove future game allocation demand.'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    current = measure(args.build)
    result = dict(schema='kui.native.runtime-footprint.v1', current=current)
    if args.baseline:
        old = measure(args.baseline)
        result['baseline'] = old
        result['saved_bytes'] = dict(
            game_heap=old['lease_payload_bytes'] - current['lease_payload_bytes'],
            worker_image=old['worker']['image_bytes'] - current['worker']['image_bytes'],
            worker_bss=old['worker']['bss_bytes'] - current['worker']['bss_bytes'])
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
