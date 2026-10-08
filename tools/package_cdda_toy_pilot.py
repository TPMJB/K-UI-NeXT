#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the separate exact-title finite stereo Toy Commander pilot."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile
import zlib

from check_loader_layout import EH, PH, SH, SYM, inspect_elf, padded, region
from check_retail_loader_layout import (
    FORBIDDEN_PREFIXES, FORBIDDEN_SYMBOLS, check_bss, code_symbol,
)
from check_retail_instructions import audit
from package_cdda_calibration import (
    ROOT, archive_name, build_config, git, object_id, read_file, sha, write_archive,
)
from package_cdda_mixed import document_copy
from package_cdda_preflight import (
    TOY_DESCRIPTOR_BYTES, TOY_DESCRIPTOR_SHA256, allocated_sections,
)
import retail_package as retail_layout
from retail_package import inspect_retail
from runtime_package import flatten_elf

OUTPUT_NAME = 'K-UI-CDDA-Toy-Pilot.zip'
README_SOURCE = 'docs/cdda-toy-pilot-test.md'
BUILD_DIRECTORY = 'build/toy-pilot'
RUNTIME_FILE = 'pilot/15-toy-finite-stereo.kui'
RUNTIME_NAME = 'retail-toy-pilot.kui'
WORKER_BASE = 0x8CFD0000
WORKER_LIMIT = 0x8CFE0000
LEASE_END = 0x8D000000
LEASE_BYTES = 0x30000
SOUND_BYTES = 0x20000
MONO_BANK_BYTES = 32768
BOOT_BYTES = 748444
BOOT_SHA256 = 'ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd'
DRIVER_BYTES = 20740
DRIVER_SHA256 = '477ede3766c27fa58e4c14d5218c583b7806a29c328a6e0d4965293375b704e5'
DRIVER_CRC32 = '70cceeb2'
PILOT_LABEL = b'PILOT PAGE'
WORKER_EXPORTS = struct.Struct('<16I')
SNAPSHOT_WORDS = (
    'magic', 'version', 'bytes', 'state',
    'fault', 'generation', 'applied_generation', 'driver_generation',
    'command', 'parameters[0]', 'parameters[1]', 'parameters[2]',
    'position_fad', 'track', 'end_fad', 'main_begin',
    'main_end', 'worker_end', 'stack_used', 'stack_fault',
    'driver_bytes', 'driver_crc', 'driver_verified', 'sound_address',
    'sound_bytes', 'sound_generation', 'service_calls', 'service_skips',
    'service_gap_max', 'step_ticks_max', 'raw_calls', 'raw_bytes',
    'raw_errors', 'copy_calls', 'copy_ticks_max', 'bank_fills',
    'bank_starts', 'bank_ends', 'handoff_gaps', 'gap_ticks_max',
    'start_waits', 'stop_waits', 'queue_errors', 'stale_actions',
    'active_left', 'active_right', 'cursor_left', 'cursor_right',
    'started_observed', 'finite_ends', 'shutdowns', 'sdk_init_result',
    'retired_frames', 'filled_frames', 'queue_producer', 'queue_consumer',
    'dma_busy', 'dma_suspended', 'hardware_loops', 'active_bank_writes',
)


def snapshot_legend():
    """Refuse stale report documentation when the all-uint32 telemetry ABI changes."""
    source = read_file(ROOT / 'include/kui/toy_pilot.h').decode('utf-8')
    found = re.search(r'struct kui_toy_pilot_snapshot\s*\{([^}]*)\};', source, re.S)
    if not found:
        raise ValueError('Missing public snapshot ABI')
    body = re.sub(r'/\*.*?\*/', '', found[1], flags=re.S)
    words = []
    for statement in body.split(';'):
        if not statement.strip():
            continue
        declaration = re.fullmatch(r'\s*uint32_t\s+(.+?)\s*', statement, re.S)
        if not declaration:
            raise ValueError('Report ABI contains a non-uint32 declaration')
        for field in declaration[1].split(','):
            name = re.fullmatch(r'\s*(\w+)(?:\[(\d+)\])?\s*', field)
            if not name:
                raise ValueError('Unrecognized report field: ' + field)
            words.extend([f'{name[1]}[{i}]' for i in range(int(name[2]))]
                         if name[2] else [name[1]])
    if tuple(words) != SNAPSHOT_WORDS:
        raise ValueError('Pilot report ABI changed; update and review the package/checklist legend')
    return [list(SNAPSHOT_WORDS[page * 16:(page + 1) * 16]) +
            ['reserved_zero'] * max(0, (page + 1) * 16 - len(SNAPSHOT_WORDS))
            for page in range(4)]


def embedded_blob(container, begin_name, end_name, base, expected, label):
    """Compare the bytes used by the relocation path with their own linked ELF."""
    begin = container['symbols'].get(begin_name, 0)
    end = container['symbols'].get(end_name, 0)
    if (begin % 4 or begin < base or end - begin != len(expected) or
            region(container['payload'], begin - base, len(expected), label) != expected):
        raise ValueError('Invalid or different embedded bytes: ' + label)
    return {'address': f'0x{begin:08x}', 'bytes': len(expected), 'sha256': sha(expected)}


def inspect_worker(data):
    """The worker starts with a numeric export table, not an executable reset entry."""
    if len(data) < EH.size:
        raise ValueError('Truncated worker ELF')
    ident, kind, machine, version, entry, po, so, _, es, ps, pn, ss, sn, _ = EH.unpack_from(data)
    if (ident[:7] != b'\x7fELF\x01\x01\x01' or kind != 2 or machine != 42 or
            version != 1 or es != EH.size or ps != PH.size or ss != SH.size or
            not 0 < pn <= 32 or not 0 < sn <= 4096):
        raise ValueError('Worker must be a static little-endian SH ELF')
    region(data, po, pn * ps, 'worker program headers')
    region(data, so, sn * ss, 'worker section headers')
    segments = []
    for index in range(pn):
        kind, offset, va, pa, size, memory, flags, _ = PH.unpack_from(data, po + index * ps)
        if kind in (2, 3, 7):
            raise ValueError('Worker is dynamic, interpreted, or TLS')
        if kind != 1:
            continue
        if (pa != va or va < WORKER_BASE or memory == 0 or size > memory or
                va + memory > WORKER_LIMIT):
            raise ValueError('Worker segment lies outside the 64 KiB leased body')
        segments.append((va, memory, flags, region(data, offset, size, 'worker load segment')))
    segments.sort()
    if not segments or segments[0][0] != WORKER_BASE or len(segments[0][3]) < WORKER_EXPORTS.size:
        raise ValueError('Worker export table does not start its fixed-address image')
    for previous, current in zip(segments, segments[1:]):
        if previous[0] + previous[1] > current[0]:
            raise ValueError('Overlapping worker segments')
    file_end = max(va + len(content) for va, _, _, content in segments)
    payload = bytearray(file_end - WORKER_BASE)
    for va, _, _, content in segments:
        payload[va - WORKER_BASE:va - WORKER_BASE + len(content)] = content
    sections = [SH.unpack_from(data, so + index * ss) for index in range(sn)]
    symbols, symbol_sizes, found_symbols = {}, {}, False
    for section in sections:
        _, kind, flags, address, offset, size, link, _, _, stride = section
        if flags & 0x400:
            raise ValueError('Worker TLS section is forbidden')
        if flags & 2 and size and (address < WORKER_BASE or address + size > WORKER_LIMIT):
            raise ValueError('Worker allocated section is outside its leased body')
        if kind != 2:
            continue
        found_symbols = True
        if stride != SYM.size or size % stride or link >= sn or sections[link][1] != 3:
            raise ValueError('Invalid worker symbol table')
        region(data, offset, size, 'worker symbol table')
        strings = region(data, sections[link][4], sections[link][5], 'worker symbol strings')
        for at in range(offset, offset + size, stride):
            name, value, symbol_size, info, other, index = SYM.unpack_from(data, at)
            if name >= len(strings):
                raise ValueError('Invalid worker symbol name')
            end = strings.find(b'\0', name)
            if end < 0:
                raise ValueError('Unterminated worker symbol name')
            label = strings[name:end].decode('ascii')
            if not label:
                continue
            if index in (0, 0xFFF2):
                raise ValueError('Unresolved/common worker symbol: ' + label)
            if index < 0xFF00 and index >= sn:
                raise ValueError('Worker symbol refers to an absent section')
            if info & 15 == 4:
                if info >> 4 or index != 0xFFF1 or value or symbol_size:
                    raise ValueError('Invalid worker file metadata symbol')
                continue
            if (info == 0x20 and other == 2 and not symbol_size and index < sn and
                    sections[index][1] == 1 and sections[index][2] == 0 and
                    sections[index][3] == 0 and value < sections[index][5]):
                continue
            symbols[label] = value
            symbol_sizes[label] = symbol_size
    if not found_symbols:
        raise ValueError('Worker lacks linked symbols')
    exports = WORKER_EXPORTS.unpack_from(payload)
    if exports[:3] != (0x54595031, 1, 64) or entry != exports[3]:
        raise ValueError('Worker export ABI or ELF initialization entry differs')
    allocated = allocated_sections(data)
    for address in (*exports[3:9], exports[15]):
        if address % 2 or address < WORKER_BASE + WORKER_EXPORTS.size or not any(
                item['flags'] & 4 and item['address'] <= address < item['address'] + item['bytes']
                for item in allocated.values()):
            raise ValueError('Worker export does not target its actual executable section')
    binary_end = symbols.get('__toy_pilot_binary_end', 0)
    bss_begin, bss_end = exports[9:11]
    bottom, top, worker_end, lease_bytes = exports[11:15]
    if (binary_end != ((file_end + 31) & ~31) or
            binary_end > bss_begin or bss_begin % 4 or bss_end % 4 or
            not bss_begin <= bss_end <= bottom < top <= worker_end <= WORKER_LIMIT or
            top - bottom != 8192 or bottom % 32 or top % 32 or lease_bytes != LEASE_BYTES or
            worker_end != max(va + memory for va, memory, _, _ in segments)):
        raise ValueError('Worker initialized/BSS/guarded-stack reservation differs')
    expected_symbols = {
        '__toy_pilot_bss_begin': bss_begin, '__toy_pilot_bss_end': bss_end,
        '__toy_pilot_stack_bottom': bottom, '__toy_pilot_stack_top': top,
        '__toy_pilot_worker_end': worker_end,
    }
    expected_symbols.update({name: exports[index] for index, name in (
        (3, '_kui_toy_pilot_initialize'), (4, '_kui_toy_pilot_request'),
        (5, '_kui_toy_pilot_service_hook'), (6, '_kui_toy_pilot_am_init_hook'),
        (7, '_kui_toy_pilot_shutdown_hook'), (8, '_kui_toy_pilot_snapshot'),
        (15, '_kui_toy_pilot_allstop_hook'),
    )})
    if any(symbols.get(name) != address for name, address in expected_symbols.items()):
        raise ValueError('Worker export reservations disagree with linked symbols')
    return {'payload': bytes(payload), 'symbols': symbols, 'memory_end': worker_end,
            'exports': list(exports), 'symbol_sizes': symbol_sizes}


def conservative_stack(directory, *, stack_bytes, assembly_bytes):
    """Sum emitted static C frames, including clones, without inventing a graph."""
    reports = sorted(Path(directory).rglob('*.su'))
    if not reports:
        raise ValueError('Missing worker compiler stack evidence')
    frames = []
    for report in reports:
        for line in read_file(report).decode('utf-8').splitlines():
            fields = line.split('\t')
            if len(fields) != 3 or fields[2] != 'static':
                raise ValueError('Nonstatic or malformed worker stack evidence: ' + report.name)
            value = int(fields[1])
            if value < 0:
                raise ValueError('Invalid worker stack frame: ' + report.name)
            frames.append(value)
    total = sum(frames) + assembly_bytes
    available = stack_bytes - 96  # 64-byte worker guard and 32-byte alignment allowance.
    if total > available:
        raise ValueError(f'Worker stack sum {total} exceeds guarded reservation {available}')
    return {'conservative_bytes': total, 'available_bytes': available,
            'assembly_allowance': assembly_bytes, 'emitted_c_frames': len(frames),
            'call_graph': False,
            'scope': 'Authored linked C frames plus assembly allowance; not game SDK internals',
            'external_sdk_frames_included': False}


def stack_rows(reports):
    if not reports:
        raise ValueError('Missing final compiler stack evidence')
    rows = []
    for report in sorted(reports):
        for line in read_file(report).decode('utf-8').splitlines():
            fields = line.split('\t')
            if len(fields) != 3 or fields[2] != 'static':
                raise ValueError('Nonstatic or malformed stack evidence: ' + str(report.relative_to(ROOT)))
            value = int(fields[1])
            if value < 0:
                raise ValueError('Negative stack frame in ' + report.name)
            rows.append({'function': fields[0].rsplit(':', 1)[-1], 'bytes': value,
                         'report': report.relative_to(ROOT).as_posix()})
    return rows


def pilot_low_stack(directory, resident_symbols, worker_symbols):
    """The pilot adds pure FD mailbox calls but runs raw card callbacks on FD's stack.

    Exclude only the whole initialization entry (called on the temporary high
    stage stack) and whole raw callback entry (called on the worker stack).
    Shared callees remain in the conservative GD-stack sum. All final LTO
    rows are counted, including clones and other initialization work.
    """
    low_rows = stack_rows(list((directory / 'sci/lto').glob('*.ltrans*.su')))
    names = {row['function'] for row in low_rows}
    excluded_names = {'kui_retail_resident_init', 'kui_toy_pilot_read_raw'}
    required = {'kui_retail_resident_dispatch', 'kui_retail_gd_dispatch',
                'kui_retail_image_read', 'kui_loader_sd_stream_next'} | excluded_names
    if not required <= names or any('_' + name not in resident_symbols for name in required):
        raise ValueError('Missing required pilot low entry or its emitted stack evidence')
    pure_names = {'kui_toy_pilot_request', 'single_audio', 'manifest', 'publish',
                  'kui_toy_pilot_snapshot'}
    worker_rows = stack_rows(list((directory / 'worker').rglob('*.su')))
    pure_rows = [row for row in worker_rows if row['function'].split('.', 1)[0] in pure_names]
    for name in ('kui_toy_pilot_request', 'kui_toy_pilot_snapshot'):
        if (not any(row['function'].split('.', 1)[0] == name for row in pure_rows) or
                '_' + name not in worker_symbols):
            raise ValueError('Missing GD-callable pure worker stack evidence: ' + name)
    kept = [row for row in low_rows if row['function'] not in excluded_names]
    excluded = [row for row in low_rows if row['function'] in excluded_names]
    available = 0x8C007D00 - 0x8C007800 - 16 - 32
    total = sum(row['bytes'] for row in (*kept, *pure_rows)) + 256
    if total > available:
        raise ValueError(f'Pilot GD stack conservative sum {total} exceeds {available}')
    return {'conservative_bytes': total, 'available_bytes': available,
            'margin': available - total, 'assembly_allowance': 256,
            'retained_low_c_frames': len(kept), 'added_pure_worker_frames': pure_rows,
            'excluded_entry_frames': excluded,
            'excluded_entry_scope': {
                'kui_retail_resident_init': 'Runs on the temporary high stage stack, not the GD hook stack',
                'kui_toy_pilot_read_raw': 'Runs on the worker private stack; every shared callee remains counted',
            },
            'scope': 'Pilot GD path including pure worker mailbox/snapshot calls; no sound SDK calls',
            'ordinary_retail_stack_validator_changed': False, 'call_graph': False}


def pure_worker_leaf_audit(worker, disassembly):
    """The GD path may call only the current two integer leaf entries.

    New calls, indirect transfers or tail branches must fail packaging until
    their full reachable stack/side-effect contract is independently reviewed.
    PC-relative literal pools are data, as in the linked instruction audit.
    """
    decoded = []
    for line in disassembly.splitlines():
        match = re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+'
                         r'(\S+)(?:\s+(.*))?$', line)
        if match:
            decoded.append((int(match[1], 16), int(match[2], 16) | int(match[3], 16) << 8,
                            match[4], match[5] or ''))
    decoded.sort()
    literals = set()
    for address, opcode, mnemonic, _ in decoded:
        if address in literals:
            continue
        if opcode & 0xF000 == 0xD000 and mnemonic == 'mov.l':
            target = ((address + 4) & ~3) + (opcode & 255) * 4
            literals.update((target, target + 2))
        elif opcode & 0xF000 == 0x9000 and mnemonic == 'mov.w':
            literals.add(address + 4 + (opcode & 255) * 2)
    result = {}
    for name in ('_kui_toy_pilot_request', '_kui_toy_pilot_snapshot'):
        begin = worker['symbols'][name]
        size = worker['symbol_sizes'].get(name, 0)
        if size < 4 or size % 2 or begin + size > WORKER_BASE + len(worker['payload']):
            raise ValueError('Missing bounded GD leaf function size: ' + name)
        rows = [row for row in decoded if begin <= row[0] < begin + size and row[0] not in literals]
        if not rows or not any(row[2] == 'rts' for row in rows):
            raise ValueError('GD pure function lacks decoded leaf return: ' + name)
        for address, _, mnemonic, operands in rows:
            if mnemonic in ('jsr', 'jsr/n', 'bsr', 'bsrf', 'jmp', 'jmp/n', 'braf', 'rte', 'trapa'):
                raise ValueError('GD pure worker entry gained a call/indirect transfer: ' + name)
            if mnemonic in ('bra', 'bt', 'bf', 'bt.s', 'bf.s'):
                target = re.match(r'^([0-9a-f]+)\s', operands)
                if (not target or not begin <= int(target[1], 16) < begin + size or
                        int(target[1], 16) in literals):
                    raise ValueError('GD pure worker entry gained an external/invalid branch: ' + name)
        result[name] = {'address': f'0x{begin:08x}', 'symbol_bytes': size,
                        'decoded_nonliteral_halfwords': len(rows), 'leaf_no_calls': True,
                        'no_external_or_indirect_transfers': True}
    return result


def pilot_layout(directory, inputs):
    """Audit only the actual compiled images, not fictitious transport variants."""
    images, sections = {}, {}
    bounds = {
        'entry': (retail_layout.EXEC_ADDRESS,
                  retail_layout.EXEC_ADDRESS + retail_layout.STAGE_BLOB_OFFSET +
                  retail_layout.STAGE_MAX_BYTES),
        'stage': (retail_layout.STAGE_ADDRESS, retail_layout.STAGE_MEMORY_END),
        'resident-sci': (retail_layout.LOW_RESIDENT_ADDRESS, retail_layout.LOW_RESIDENT_LIMIT),
        'worker': (WORKER_BASE, WORKER_LIMIT),
    }
    for name, (base, limit) in bounds.items():
        raw = inputs(directory / (name + '.elf'))
        images[name] = inspect_worker(raw) if name == 'worker' else inspect_elf(raw, base, limit)
        sections[name] = allocated_sections(raw)
        bad = [symbol for symbol in images[name]['symbols']
               if symbol.startswith(FORBIDDEN_PREFIXES) or symbol in FORBIDDEN_SYMBOLS or
               symbol.startswith('_kui_cdda_aica_')]
        if bad:
            raise ValueError('Unexpected runtime or detached sound owner in ' + name + ': ' + bad[0])
    entry, stage, resident, worker = (images[name] for name in bounds)
    blobs = {}
    for name in ('stage', 'resident-sci', 'worker'):
        raw = inputs(directory / (name + '.bin'))
        data = raw if name == 'worker' else padded(raw)
        expected = images[name]['payload'] if name == 'worker' else padded(images[name]['payload'])
        if data != expected:
            raise ValueError(name + '.bin differs from its linked ELF load bytes')
        blobs[name] = data
    check_bss(stage, retail_layout.STAGE_ADDRESS, '__retail_stage')
    check_bss(resident, retail_layout.LOW_RESIDENT_ADDRESS, '__retail_resident')
    es, ss, rs, ws = (images[name]['symbols'] for name in bounds)
    if (rs.get('__retail_hook_stack_bottom') != 0x8C007800 or
            rs.get('__retail_hook_stack') != 0x8C007D00):
        raise ValueError('Pilot changed the standard guarded low SCI stack')
    for symbol in ('_kui_retail_resident_init', '_kui_retail_resident_hook',
                   '_kui_retail_resident_dispatch', '_kui_retail_gd_dispatch',
                   '_kui_retail_image_read', '_kui_loader_sd_stream_next',
                   '_kui_sci_sd_acquire', '_kui_sci_sd_release'):
        code_symbol(resident, symbol, retail_layout.LOW_RESIDENT_ADDRESS)
    for symbol in ('_kui_retail_hook_active', '_kui_retail_hook_fault'):
        if not retail_layout.LOW_RESIDENT_ADDRESS <= rs.get(symbol, 0) < resident['memory_end']:
            raise ValueError('Missing low-resident hook state: ' + symbol)
    for symbol in ('_kui_retail_stage_main', '_kui_retail_stage_relay',
                   '_kui_retail_bootstrap_enter', '_kui_retail_game_resume',
                   '_kui_toy_pilot_stage_install', '_kui_toy_pilot_stage_publish'):
        code_symbol(stage, symbol, retail_layout.STAGE_ADDRESS)
    for symbol in ('_kui_toy_pilot_read_raw', '_kui_toy_pilot_heap_hook',
                   '_kui_toy_pilot_return_hook'):
        code_symbol(resident, symbol, retail_layout.LOW_RESIDENT_ADDRESS)
    for label in (PILOT_LABEL, b'WORDS'):
        if label + b'\0' not in resident['payload']:
            raise ValueError('Final low resident lacks its exact pilot report legend')
    if bytes.fromhex(BOOT_SHA256) not in stage['payload']:
        raise ValueError('Linked stage lacks the exact original boot SHA-256 identity')
    if bytes.fromhex(DRIVER_SHA256) not in worker['payload']:
        raise ValueError('Linked worker lacks the exact driver-input SHA-256 identity')
    # The generated low addresses must come from this resident, not stale
    # numeric bindings copied from another build.
    from toy_pilot_symbols import generate
    generated = inputs(directory / 'toy_pilot_resident_symbols.h')
    if generated != generate(directory / 'resident-sci.elf').encode('utf-8'):
        raise ValueError('Stage bindings differ from the actual linked low resident')
    embedded = {}
    for transport in ('scif', 'sci', 'ide', 'scia'):
        prefix = '__retail_resident_' + transport + '_blob_'
        embedded[transport] = embedded_blob(stage, prefix + 'start', prefix + 'end',
                                            retail_layout.STAGE_ADDRESS,
                                            blobs['resident-sci'], 'SCI resident ' + transport)
    embedded['worker'] = embedded_blob(stage, '__toy_pilot_worker_blob_start',
                                       '__toy_pilot_worker_blob_end', retail_layout.STAGE_ADDRESS,
                                       blobs['worker'], 'leased worker')
    begin, end = ss.get('__retail_trampoline_start', 0), ss.get('__retail_trampoline_end', 0)
    if begin % 4 or end - begin != retail_layout.TRAMPOLINE_BYTES:
        raise ValueError('Invalid bounded executable-entry trampoline')
    trampoline = region(stage['payload'], begin - retail_layout.STAGE_ADDRESS,
                        retail_layout.TRAMPOLINE_BYTES, 'pilot trampoline')
    if struct.pack('<I', ss['_kui_retail_game_resume'] | 0x20000000) not in trampoline:
        raise ValueError('Trampoline does not target the actual uncached stage relay')
    if (es.get('__retail_map') != retail_layout.EXEC_ADDRESS + retail_layout.MAP_OFFSET or
            es.get('__retail_stage_blob_start') != retail_layout.EXEC_ADDRESS +
            retail_layout.STAGE_BLOB_OFFSET or
            es.get('__retail_stage_blob_end') != retail_layout.EXEC_ADDRESS + len(entry['payload']) or
            entry['payload'][retail_layout.STAGE_BLOB_OFFSET:] != blobs['stage'] or
            entry['memory_end'] != retail_layout.EXEC_ADDRESS + len(entry['payload']) or
            region(entry['payload'], retail_layout.HEADER_OFFSET, retail_layout.HEADER_BYTES,
                   'relocation header') != retail_layout.relocation_header(len(blobs['stage']), low=True)):
        raise ValueError('Invalid pilot entry/stage/relocation geometry')
    if any(region(entry['payload'], retail_layout.MAP_OFFSET, retail_layout.MAP_BYTES,
                  'card-specific manifest')):
        raise ValueError('Packaged card manifest must be blank')
    instructions = {}
    leaf = None
    for name in bounds:
        disassembly = subprocess.check_output(
            ['sh-elf-objdump', '-d', str(directory / (name + '.elf'))], text=True)
        try:
            instructions[name] = audit(name, disassembly)
        except SystemExit as error:
            raise ValueError(str(error)) from error
        if name == 'worker':
            leaf = pure_worker_leaf_audit(worker, disassembly)
    low_stack = pilot_low_stack(directory, rs, ws)
    # The worker link defines its own guard/stack reservation inside the 64 KiB lease.
    bottom, top = ws.get('__toy_pilot_stack_bottom', 0), ws.get('__toy_pilot_stack_top', 0)
    if not (WORKER_BASE + len(worker['payload']) <= bottom < top <= WORKER_LIMIT and
            bottom % 32 == top % 32 == 0):
        raise ValueError('Worker lacks a bounded private stack inside its 64 KiB body')
    worker_stack = conservative_stack(directory / 'worker', stack_bytes=top - bottom,
                                       assembly_bytes=256)
    # The callback executes the low reader's C code on the worker stack.
    # Add every low C frame except high-only initialization; this intentionally
    # overcounts terminal/GD-only work rather than removing shared callees.
    callback_rows = [row for row in stack_rows(list((directory / 'sci/lto').glob('*.ltrans*.su')))
                     if row['function'] != 'kui_retail_resident_init']
    callback_bytes = sum(row['bytes'] for row in callback_rows)
    worker_stack['callback_low_c_frames'] = len(callback_rows)
    worker_stack['callback_low_c_bytes'] = callback_bytes
    worker_stack['conservative_bytes'] += callback_bytes
    worker_stack['scope'] = ('All authored worker C frames, low callback C frames and assembly allowance; '
                             'not game SDK internals')
    if worker_stack['conservative_bytes'] > worker_stack['available_bytes']:
        raise ValueError('Worker and shared low callback exceed the guarded worker stack')
    return {
        'images': {name: {'payload_bytes': len(image['payload']),
                         'memory_end': f"0x{image['memory_end']:08x}", 'unresolved_symbols': 0}
                   for name, image in images.items()},
        'allocated_sections': sections, 'embedded_blobs': embedded,
        'resident_stack': low_stack, 'worker_stack': worker_stack,
        'instruction_audit': instructions, 'compiled_resident_transports': ['SCI'],
        'GD_pure_worker_leaf_audit': leaf,
        'legacy_blob_slots': 'four exact copies of the same admitted standard SCI resident',
        'resident_address': '0x8c004000', 'resident_limit': '0x8c007800',
        'hook_stack': ['0x8c007800', '0x8c007d00'],
        'worker_reservation': ['0x8cfd0000', '0x8cfe0000'],
        'worker_private_stack': [f'0x{bottom:08x}', f'0x{top:08x}'], 'manifest_slots': 64,
        'worker_export_words': worker['exports'],
    }


def collect(commit, published_tree):
    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before packaging')
    checkpoint = git('rev-parse', 'HEAD')
    tree = git('rev-parse', 'HEAD^{tree}')
    if tree != published_tree:
        raise ValueError('Published source tree differs from the archived checkpoint')
    tracked = {name for name in subprocess.check_output(
        ['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8').split('\0') if name}
    for name in tracked:
        archive_name(name)
    if README_SOURCE not in tracked:
        raise ValueError('Commit the pilot checklist before packaging')
    if b'TOY_PILOT_CHECKLIST_DRAFT' in read_file(ROOT / README_SOURCE):
        raise ValueError('Freeze the checklist against the final report implementation')
    files, inputs = {}, {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    def remember(path):
        data = read_file(path)
        digest = sha(data)
        if path in inputs and inputs[path] != digest:
            raise ValueError('Build input changed while collecting: ' + str(path.relative_to(ROOT)))
        inputs[path] = digest
        return data

    build = ROOT / BUILD_DIRECTORY
    config_bytes = remember(build / 'build-config')
    report_words = snapshot_legend()
    config = build_config(build / 'build-config')
    required = {'BUILD': commit[:12], 'PROFILE': '15', 'LOW': '1', 'SLOTS': '64',
                'SCI': '1', 'PILOT': '1', 'SCI_PACED': '1', 'SCI_FAULT': '1',
                'OPT': '-Os -fno-tree-scev-cprop'}
    if config != required:
        raise ValueError('Wrong complete Toy pilot build configuration')
    runtime = remember(build / RUNTIME_NAME)
    envelope = inspect_retail(runtime)
    entry_elf = remember(build / 'entry.elf')
    payload, memory = flatten_elf(entry_elf)
    if (envelope['build'] != commit[:12] or runtime[64:] != payload or
            envelope['memory_bytes'] != memory or
            commit[:12].encode('ascii') + b'\0' not in payload or
            PILOT_LABEL + b'\0' not in payload):
        raise ValueError('Pilot envelope/ELF/source/report identity mismatch')
    linked = pilot_layout(build, remember)
    add(RUNTIME_FILE, runtime)
    add('evidence/toy-pilot/build-config', config_bytes)
    add('evidence/toy-pilot/toy_pilot_resident_symbols.h',
        remember(build / 'toy_pilot_resident_symbols.h'))
    evidence = {}
    for path in sorted(build.rglob('*')):
        if not path.is_file() or path.suffix not in ('.elf', '.map', '.su'):
            continue
        name = path.relative_to(build).as_posix()
        data = remember(path)
        if not data:
            raise ValueError('Empty build evidence: ' + name)
        evidence[name] = sha(data)
        add('evidence/toy-pilot/' + name, data)
    for name in ('entry', 'stage', 'resident-sci', 'worker'):
        if not {name + '.elf', name + '.map'} <= set(evidence):
            raise ValueError('Missing actual linked ELF/map evidence: ' + name)
    if not any(name.endswith('.su') for name in evidence):
        raise ValueError('Missing compiler stack evidence')
    for path in sorted((ROOT / 'LICENSES').rglob('*')):
        if path.is_file():
            add('LICENSES/' + path.relative_to(ROOT / 'LICENSES').as_posix(), read_file(path))
    add('LICENSE', read_file(ROOT / 'LICENSE'))
    notice = read_file(ROOT / 'data/known-dumps/README.txt')
    notice_name = 'LICENSES/known-dumps-README.txt'
    if notice_name in files:
        if files[notice_name] != notice:
            raise ValueError('Known-dumps license notices disagree')
    else:
        add(notice_name, notice)
    source_url = 'https://github.com/TPMJB/K-UI-NeXT/tree/' + commit
    add('source-url.txt', (source_url + '\n').encode('utf-8'))
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT)
    add('source-snapshot.tar', snapshot)
    source_hashes = {}
    for name in sorted(tracked):
        if (name == 'Makefile.toy_pilot' or name.startswith(('src/loader/toy_pilot',
                'src/core/toy_pilot', 'include/kui/toy_pilot', 'tools/package_cdda_toy_pilot',
                'tools/toy_pilot_symbols'))):
            source_hashes[name] = sha(read_file(ROOT / name))
    documents = []
    for path in sorted((ROOT / 'docs').glob('cdda-*.md')):
        source = path.relative_to(ROOT).as_posix()
        documents.extend(((source, source), (source, path.name)))
    for path in sorted((ROOT / 'docs/evidence').glob('cdda-*.md')):
        source = path.relative_to(ROOT).as_posix()
        documents.extend(((source, source), (source, 'evidence/' + path.name)))
    documents.extend(((README_SOURCE, 'README.md'), ('THIRD_PARTY.md', 'THIRD_PARTY.md')))
    available = set(files) | {destination for _, destination in documents} | {'build.json', 'SHA256SUMS'}
    document_hashes = {}
    for source, destination in documents:
        data = read_file(ROOT / source)
        document_hashes[source] = sha(data)
        add(destination, document_copy(data, source, destination, available, tracked, commit))
    metadata = {
        'source_commit': commit, 'source_tree': tree, 'local_checkpoint': checkpoint,
        'source_dirty': False, 'source_publication': 'published; exact tree equals local checkpoint',
        'source_url': source_url, 'source_snapshot': 'source-snapshot.tar',
        'source_snapshot_sha256': sha(snapshot),
        'pilot_source_sha256': source_hashes,
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'scope': 'Exact-title Toy Commander finite stereo CDDA pilot; game ARM driver and allocators',
        'profile_number': 15, 'hardware_tested': False, 'numerical_pass_target': None,
        'file': RUNTIME_FILE, 'build': envelope['build'], 'runtime_sha256': sha(runtime),
        'payload_crc32': f'{zlib.crc32(payload):08x}', 'envelope': envelope,
        'linked_layout': linked, 'build_configuration': config,
        'build_configuration_sha256': sha(config_bytes), 'evidence_sha256': evidence,
        'document_source_sha256': document_hashes,
        'required_image': {
            'descriptor_bytes': TOY_DESCRIPTOR_BYTES, 'descriptor_sha256': TOY_DESCRIPTOR_SHA256,
            'descriptor_crc32': 'e25531d1', 'boot_bytes': BOOT_BYTES, 'boot_sha256': BOOT_SHA256,
            'ip_crc32': '38ba2868', 'driver_bytes': DRIVER_BYTES, 'driver_sha256': DRIVER_SHA256,
            'driver_crc32': DRIVER_CRC32,
            'complete_backing_tracks': 15, 'audio_tracks': 12,
            'location': 'same existing complete original Toy image/card used for passing preflight13',
            'repeat_tests00_through14_required_for_unchanged_image_card': False,
            'copied_game_content_included': False,
            'sha256_values_identify_preflight_and_uploaded_inputs': True,
            'runtime_gdi_admission_uses_crc_and_geometry': True,
            'runtime_boot_sha256_checked_before_hooks': True,
            'runtime_manifest_carries_descriptor_sha256': False,
            'runtime_driver_input_sha256_checked_before_am_init': True,
            'installed_arm_driver_readback_sha256_checked': False,
        },
        'installation': {
            'manual_only': True, 'card_target': '/KUI/apps/games/retail-boot.kui',
            'required_runtime': 'retained working 1.8.5 runtime',
            'working_reader_backup': '/KUI/apps/games/retail-boot-before-observe.kui',
            'working_runtime_replaced_in_bundle': False, 'ordinary_reader_default_changed': False,
            'game_files_or_descriptor_installed': False, 'card_path_configuration_installed': False,
            'pilot_installed_on_extraction': False, 'prior_artifacts_overwritten': False,
        },
        'audio_contract': {
            'main_heap_lease_bytes': LEASE_BYTES, 'main_heap_lease': ['0x8cfd0000', '0x8d000000'],
            'worker_body': ['0x8cfd0000', '0x8cfe0000'],
            'upper_lease_padding_used_by_worker': False,
            'sound_fixed_heap_bytes': SOUND_BYTES, 'stereo_banks': 2,
            'mono_bank_bytes': MONO_BANK_BYTES, 'stereo_frames_per_bank': MONO_BANK_BYTES // 2,
            'sample_rate': 44100, 'ports': [62, 63], 'hardware_looping': False,
            'finite_bank_duration_milliseconds_approximately': 371,
            'handoff_silence_allowed_for_first_pilot': True,
            'driver_upload_or_arm_reset': False, 'timer_reprogramming': False,
            'frame_callback_is_a_guaranteed_service_deadline': False,
            'all_games_compatible': False, 'video_skip_fixed': False,
            'live_GD_GETSCD_REQ_STAT_DRIVE_audio_position_refreshed': False,
            'baseline_subcode_audio_status': 15,
            'pilot_cursor_telemetry_establishes_game_GD_position_responses': False,
            'audible_or_stereo_output_established_by_build_checks': False,
            'game_sdk_waits_proved_bounded': False,
        },
        'clock_admission': {
            'read_only': True, 'frequency_or_timer_writes': False,
            'FRQCR_16bit_address': '0xffc00000', 'FRQCR_mask': '0x0fff',
            'FRQCR_expected': '0x0e0a', 'TMU0_reload_expected': '0xffffffff',
            'TCR0_mask': '0x0027', 'TCR0_expected': '0x0002', 'TSTR_running_mask': '0x01',
            'assumed_peripheral_clock_hz': 50000000, 'timer_divider': 64,
            'nominal_microseconds_per_tick': 1.28,
            'oscillator_independently_calibrated': False,
            'incompatible_profile_action': 'clock fault; do not reprogram timer',
        },
        'return_control': 'A+B+X+Y+Start',
        'report': {'title': PILOT_LABEL.decode('ascii'), 'pages': 4,
                   'displayed_page_numbers': [0, 1, 2, 3], 'rows_per_page': 4,
                   'words_per_row': 4, 'word_legend_by_page': report_words,
                   'page_frames': 900, 'pages_repeat_until_power_off': True,
                   'nominal_page_seconds_at_60hz': 15, 'nominal_page_seconds_at_50hz': 18,
                   'numerical_pass_target': None},
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode('utf-8'))
    if ({name for name in files if name.endswith('.kui')} != {RUNTIME_FILE} or
            any(name.startswith(('KUI/', 'runtimes/', 'source-fatfs/')) for name in files) or
            any(Path(name).suffix.lower() == '.drv' for name in files)):
        raise ValueError('Bundle must contain one manually installed pilot and no retail driver')
    if any(sha(data) in (BOOT_SHA256, DRIVER_SHA256) for data in files.values()):
        raise ValueError('A retail executable or driver was included as a package member')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD') != checkpoint or
            git('rev-parse', 'HEAD^{tree}') != tree):
        raise ValueError('Source changed while collecting the pilot package')
    for path, digest in inputs.items():
        if sha(read_file(path)) != digest:
            raise ValueError('Build input changed while collecting: ' + str(path.relative_to(ROOT)))
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode('utf-8'))
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-commit', required=True, type=object_id)
    parser.add_argument('--published-tree', required=True, type=object_id)
    args = parser.parse_args()
    try:
        if args.output.is_symlink():
            raise ValueError('Output must not be a symlink')
        output = args.output.resolve()
        if output.name != OUTPUT_NAME or output.exists():
            raise ValueError('Use a new ' + OUTPUT_NAME + ' path; preserve previous artifacts')
        if output.is_relative_to(ROOT):
            result = subprocess.run(['git', 'ls-files', '--error-unmatch', '--',
                                     output.relative_to(ROOT).as_posix()], cwd=ROOT,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if result.returncode == 0:
                raise ValueError('Output must not overwrite a tracked repository file')
        write_archive(output, collect(args.source_commit, args.published_tree))
        print(json.dumps({'file': str(output), 'bytes': output.stat().st_size,
                          'sha256': sha(output.read_bytes()), 'build': args.source_commit[:12],
                          'profile': 15}, indent=2))
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
