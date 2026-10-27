#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Targeted Evolution 1 execution of both isolation installers, before arming.

This does not replace the full corpus companion audit. Actual worker arming,
SDK publication, storage, and console behavior have separate checks.
"""
import argparse
import hashlib
import json
from pathlib import Path

from check_native_bottom_heap import (ABSOLUTE_ONE, Cold, LEASE_BYTES,
    REFUSALS, ROOT, require)
from check_vmu_native_worker_gateway import read_elf, symbol_bytes

SCHEMA = 'kui.native.isolation.evolution1-targeted-cold.v1'
LOW_NAMES = ('_kui_vmu_observe_heap_hook', '_kui_vmu_observe_return_hook',
    '_kui_vmu_observe_boot_control', '_kui_vmu_native_heap_original',
    '_kui_vmu_observe_sd_lease')
SOURCES = ('tools/check_native_isolation_cold.py',
    'tools/check_native_bottom_heap.py',
    'tools/profile_vmu_native_release_sh.py',
    'tools/check_vmu_native_worker_gateway.py',
    'tests/test_vmu_native_heap_execution.py', 'tools/sh4_delay_slots.py')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def actual_low(build):
    """Read the Standard role tuple from bounded typed resident ELF symbols."""
    image = read_elf(build / 'resident-sci.elf')
    values = []
    for name in LOW_NAMES:
        value, size, kind, owner = image[4][name]
        if name == LOW_NAMES[4]:
            # The assembly entry's linker-relaxed size can extend beyond its
            # dedicated section. Only its typed, allocated entry address is
            # used here; this cold boundary never executes the SD loan.
            require(kind == 2 and owner < len(image[3]),
                    'Standard SD loan lacks a typed ELF entry')
            section = image[3][owner]
            require(section[1] == 1 and section[2] & 6 == 6 and
                    section[3] <= value < value + 2 <= section[3] + section[5],
                    'Standard SD loan entry lies outside its executable section')
        elif name in (LOW_NAMES[0], LOW_NAMES[1]):
            symbol_bytes(image, name, 2)
        else:
            require(kind == 1 and owner < len(image[3]),
                    'Standard resident object lacks typed ownership: ' + name)
            section = image[3][owner]
            require(size == (56 if name == LOW_NAMES[2] else 32) and
                    section[2] & 2 and section[3] <= value < value + size <= section[3] + section[5],
                    'Standard resident object lacks exact allocated bounds: ' + name)
        values.append(value)
    require('_kui_vmu_native_background_manifest_bind' not in image[4],
            'Standard resident unexpectedly has a background binding')
    return tuple(values) + (0,)


def check(build, variant, row):
    build = Path(build).resolve()
    artifacts = {p.name: sha(p) for p in sorted(build.iterdir())
                 if p.suffix in ('.elf', '.bin', '.reloc') and p.is_file()}
    marker = '__kui_native_' + variant + '_isolation_'
    for file, role in (('stage.elf', 'stage'), ('native-worker.elf', 'worker')):
        symbols = read_elf(build / file)[4]
        require(symbols.get(marker + role) == ABSOLUTE_ONE,
                'Wrong compiled isolation variant: ' + file)
        require(symbols.get('__kui_native_bottom_heap_' + role) == ABSOLUTE_ONE and
                symbols.get('__kui_native_isolation_lease_bytes') ==
                (LEASE_BYTES, 0, 0, 0xfff1), 'Wrong compiled bottom lease geometry')
        require(all('__kui_native_'+other+'_isolation_'+role not in symbols
                    for other in {'reader_only','standard_vmu','metadata_cache'}-{variant}),
                'Conflicting compiled isolation mode')
        if variant == 'metadata_cache':
            require(symbols.get('__kui_native_ra_tick_off_isolation_'+role) == ABSOLUTE_ONE and
                    all('__kui_native_'+other+role not in symbols for other in
                        ('maple_bypass_isolation_','maple_delay_isolation_','e1_wait_capture_')),
                    'Combined cold control requires real callbacks with TICK off')
    standard_low = actual_low(build) if variant == 'standard_vmu' else None
    reader = 0 if standard_low else 1
    positive, negative = [], []
    for alias in (0, 0x80000000, 0xa0000000):
        for seed in (0, 1):
            cpu = Cold(build, row, reader=reader, alias=alias, seed=seed,
                       standard_low=standard_low)
            require(cpu.run_cold(), 'Cold installer refused: ' + str(cpu.refusal))
            require(cpu.owner_calls == [('allocate', cpu.count - LEASE_BYTES - 64),
                    ('allocate', LEASE_BYTES), ('release', cpu.begin + LEASE_BYTES + 64)],
                    'Original allocator call order or arguments differ')
            require(cpu.unchanged_calls == 3 and cpu.code_calls == 2 and cpu.passive_calls == 1,
                    'Mandatory cold guards or validated handoff omitted')
            if cpu.has_budget:
                require(cpu.budget_prepare_calls == cpu.budget_core_calls ==
                        cpu.budget_publish_calls == 1 and not cpu.budget_slot and
                        not cpu.budget_writes and not cpu.budget_purges and not cpu.budget_scans,
                        'Evolution 1 unexpectedly changed an allocator reserve or full-scanned')
            positive.append(dict(alias=hex(alias), seed=seed, instructions=cpu.instructions,
                original_calls=cpu.owner_calls, payload=hex(cpu.published),
                guard_calls=cpu.unchanged_calls, code_checks=cpu.code_calls,
                passive_handoffs=cpu.passive_calls, budget_calls=cpu.budget_core_calls))
    for mutation in REFUSALS:
        cpu = Cold(build, row, reader=reader, seed=0, mutation=mutation,
                   standard_low=standard_low)
        require(not cpu.run_cold() and cpu.refusal is not None,
                'Cold installer accepted mutation: ' + mutation)
        require(cpu.published is None and cpu.passive_calls == 0,
                'Refused installer published or handed off: ' + mutation)
        if mutation == 'cold-release-code':
            require(cpu.refusal == (6, 0x63) and not cpu.owner_calls,
                    'Invalid cold release code reached original allocator')
        if mutation in ('bad-allocate-callback', 'bad-release-callback'):
            require(cpu.refusal[0] == 4 and not cpu.owner_calls,
                    'Invalid callback reached original allocator')
        if mutation in ('allocated-header', 'allocated-not-empty', 'release-code-before-free',
                        'release-candidate', 'release-free-overlap', 'release-size-mismatch'):
            require(cpu.refusal == (6, 0x61) and len(cpu.owner_calls) == 2 and
                    all(call[0] == 'allocate' for call in cpu.owner_calls),
                    'Invalid ownership/release proof reached original free')
        negative.append(dict(mutation=mutation, refusal=cpu.refusal,
                             original_calls=cpu.owner_calls))
    require(all(sha(build / name) == digest for name, digest in artifacts.items()),
            'Runtime artifact changed during cold execution')
    return dict(variant=variant, build=str(build), artifacts=artifacts,
                reader=reader, standard_low=standard_low,
                positive_cases=positive, refusal_cases=negative,
                positive_count=len(positive), refusal_count=len(negative))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reader-build', type=Path, required=True)
    parser.add_argument('--standard-build', type=Path, required=True)
    parser.add_argument('--startup', type=Path, required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cached-metadata',action='store_true',
                        help='Require the combined VMU/New metadata-cache isolation markers')
    args = parser.parse_args()
    row = dict(slug='evolution1', startup=str(args.startup.resolve()),
               record=str(args.record.resolve()))
    report = dict(schema=SCHEMA, scope='Targeted original Evolution 1 cold installer execution',
        boundary='Validated _native_passive_run handoff; worker arming and SDK publication are separate',
        lease_bytes=LEASE_BYTES, inputs={name: dict(path=row[name], sha256=sha(row[name]))
            for name in ('startup', 'record')},
        source_sha256={name: sha(ROOT / name) for name in SOURCES},
        results=[check(args.reader_build, 'metadata_cache' if args.cached_metadata else 'reader_only', row),
                 check(args.standard_build, 'standard_vmu', row)])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(schema=SCHEMA, output=str(args.output),
        positive_count=sum(x['positive_count'] for x in report['results']),
        refusal_count=sum(x['refusal_count'] for x in report['results']))))


if __name__ == '__main__':
    main()
