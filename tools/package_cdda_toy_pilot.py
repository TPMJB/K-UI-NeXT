#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the isolated three-sector Toy video test with the clean CDDA worker."""
import argparse
import json
import posixpath
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
from toy_pilot_pause_stack_audit import audit_pause_stack
from toy_pilot_scratch_audit import audit_native_scratch

OUTPUT_NAME = 'K-UI-Toy-Video-Streaming-Test.zip'
README_SOURCE = 'docs/cdda-toy-video-test.md'
BUILD_DIRECTORY = 'build/toy-pilot'
GD_RUNTIME_FILE = 'pilot/15-toy-gd-three-sector.kui'
# Exact separately reviewed eight-block worker; pinned after cross-build.
DIAGNOSTIC_WORKER_SHA256 = 'f3b510df3034634e01e3dd911711f94551062534d6cbf4b635900a366f903d3a'
RUNTIME_NAME = 'retail-toy-pilot.kui'
WORKER_BASE = 0x8CFD0000
WORKER_LIMIT = 0x8CFE0000
LEASE_END = 0x8D000000
LEASE_BYTES = 0x30000
SOUND_BYTES = 0x20000
RING_FRAMES = 32768
RING_MONO_BYTES = 65536
BLOCK_FRAMES = 4096
BLOCK_COUNT = 8
MONO_BLOCK_BYTES = 8192
SNAPSHOT_VERSION = 8
BOOT_BYTES = 748444
BOOT_SHA256 = 'ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd'
DRIVER_BYTES = 20740
DRIVER_SHA256 = '477ede3766c27fa58e4c14d5218c583b7806a29c328a6e0d4965293375b704e5'
DRIVER_CRC32 = '70cceeb2'
PILOT_LABEL = b'PILOT PAGE'
WORKER_EXPORTS = struct.Struct('<19I')
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
    'bus_last_result', 'bus_deferrals', 'updater_entries', 'updater_returns',
    'pause_entries', 'pause_retries', 'pause_pumps', 'pause_retired',
    'pause_detail', 'pause_max_attempts', 'cache_policy', 'service_visits_per_update',
    'native_owner', 'native_work_token', 'gd_owned_command', 'gd_owned_token',
    'native_gd_state', 'check_token', 'check_destination', 'check_result',
    'recovery_counts[0]', 'recovery_counts[1]', 'recovery_counts[2]', 'recovery_counts[3]',
    'recovery_counts[4]', 'recovery_counts[5]', 'recovery_counts[6]', 'recovery_counts[7]',
    'data_blocked_calls', 'data_blocked_intervals', 'data_blocked_ticks_max', 'data_blocked_ticks_total',
    'recovery_last_reason', 'recovery_last_gd_command', 'recovery_last_cursor_age', 'data_blocked_open',
    'reserve_last_cursor', 'reserve_last_proof_age', 'reserve_last_sample_age', 'reserve_last_remaining',
    'reserve_last_bank', 'reserve_last_bank_filled', 'reserve_last_fill_stream', 'reserve_last_site',
    'raw_read_ticks_last', 'raw_read_ticks_max', 'raw_read_ticks_total', 'raw_read_timing_calls',
    'reserve_last_bank_state', 'reserve_last_probe_age', 'reserve_last_window_ticks', 'reserve_last_window_calls',
)
GD_TIMING_WORDS = (
    'GD_timing_marker', 'GD_fixed_step', 'measured_read_steps', 'measured_sectors',
    'read_ticks_total_modulo32', 'read_ticks_max', 'actual_sectors_max', 'timer_invalid_steps',
    'GD_diag_calls', 'GD_diag_requests', 'GD_diag_rejected', 'GD_diag_last_error',
    'reserved_zero', 'reserved_zero', 'reserved_zero', 'reserved_zero',
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
            for page in range(7)]


def ring_contract(data):
    """Keep numeric ownership admission and the delivered checklist in sync."""
    source = data.decode('utf-8')
    expected = {'KUI_TOY_RING_FRAMES': RING_FRAMES, 'KUI_TOY_RING_HALF': 16384,
                'KUI_TOY_RING_BLOCK': BLOCK_FRAMES, 'KUI_TOY_RING_BLOCKS': BLOCK_COUNT,
                'KUI_TOY_RING_PROBE_TICKS': 391, 'KUI_TOY_RING_PROBE_READS': 16,
                'KUI_TOY_RING_FRESH_TICKS': 39063, 'KUI_TOY_RING_RESERVE_TICKS': 3907,
                'KUI_TOY_RING_PHASE_FRAMES': 64}
    found = {}
    for name, value in expected.items():
        definitions = re.findall(r'^#define\s+' + re.escape(name) + r'\s+(\d+)u\b', source, re.M)
        if len(definitions) != 1 or int(definitions[0]) != value:
            raise ValueError('Ring admission changed; review checklist and package metadata: ' + name)
        found[name] = value
    return found


def pilot_contract(data):
    """Admit the reviewed ABI and physical allocation independently of labels."""
    source = data.decode('utf-8')
    expected = {'KUI_TOY_PILOT_API': SNAPSHOT_VERSION,
                'KUI_TOY_PILOT_BLOCKS': BLOCK_COUNT,
                'KUI_TOY_PILOT_BLOCK_FRAMES': BLOCK_FRAMES,
                'KUI_TOY_PILOT_MONO_BYTES': MONO_BLOCK_BYTES,
                'KUI_TOY_PILOT_BANK_BYTES': MONO_BLOCK_BYTES * 2,
                'KUI_TOY_PILOT_RING_MONO_BYTES': RING_MONO_BYTES,
                'KUI_TOY_PILOT_SOUND_BYTES': SOUND_BYTES,
                'KUI_TOY_PILOT_RAW_BYTES': 2352,
                'KUI_TOY_PILOT_RAW_SECTORS': 1}
    for name, value in expected.items():
        definitions = re.findall(r'^#define\s+' + re.escape(name) + r'\s+(\d+)u\b', source, re.M)
        if len(definitions) != 1 or int(definitions[0]) != value:
            raise ValueError('Pilot ABI or geometry changed; review package metadata: ' + name)
    aliases = re.findall(r'^#define\s+KUI_TOY_PILOT_BANK_FRAMES\s+(\w+)\s*$', source, re.M)
    if aliases != ['KUI_TOY_PILOT_BLOCK_FRAMES']:
        raise ValueError('Legacy bank frame alias no longer identifies one logical block')
    return expected


def selected_checklist(data, gd_fixed_step):
    """Keep the new test instructions separate from the retained baseline."""
    if gd_fixed_step != 3:
        raise ValueError('The video test requires fixed three-sector GD steps')
    return data


def embedded_blob(container, begin_name, end_name, base, expected, label):
    """Compare the bytes used by the relocation path with their own linked ELF."""
    begin = container['symbols'].get(begin_name, 0)
    end = container['symbols'].get(end_name, 0)
    if (begin % 4 or begin < base or end - begin != len(expected) or
            region(container['payload'], begin - base, len(expected), label) != expected):
        raise ValueError('Invalid or different embedded bytes: ' + label)
    return {'address': f'0x{begin:08x}', 'bytes': len(expected), 'sha256': sha(expected)}


def linked_symbol_sizes(data):
    """Read sizes from an ELF already admitted by the strict image validator."""
    header = EH.unpack_from(data)
    sections = [SH.unpack_from(data, header[6] + index * header[11])
                for index in range(header[12])]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = region(data, strings[4], strings[5], 'linked symbol names')
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, _, size, _, _, _ = SYM.unpack_from(data, offset)
            if name:
                end = names.find(b'\0', name)
                if end < 0:
                    raise ValueError('Unterminated admitted ELF symbol name')
                result[names[name:end].decode('ascii')] = size
    return result


def fixed_step_store_audit(resident, size, step=3):
    """Admit the reviewed linked cap/store/call sequence, not a build label."""
    if step not in (2, 3):
        raise ValueError('Only the reviewed fixed2 and fixed3 GD sequences are admitted')
    base = retail_layout.LOW_RESIDENT_ADDRESS
    begin = resident['symbols']['_step']
    payload = resident['payload']

    def half(address):
        return struct.unpack_from('<H', region(payload, address - base, 2, 'GD step instruction'))[0]

    def literal(address):
        opcode = half(address)
        target = ((address + 4) & ~3) + (opcode & 255) * 4
        return struct.unpack_from('<I', region(payload, target - base, 4, 'GD step literal'))[0]

    if step == 2:
        # Retain the shipped 7b sequence: r1=2 supplies both the cap and the
        # fifth GD_EXEC argument, stored in the BSR delay slot.
        candidates = [address for address in range(begin, begin + size - 10, 2)
                      if (half(address) == 0xE102 and half(address + 2) == 0x1814 and
                          half(address + 4) == 0xE700 and
                          half(address + 6) & 0xFF00 == 0xD400 and
                          half(address + 8) & 0xF000 == 0xB000 and
                          half(address + 10) == 0x2F12)]
        literal_offset, call_offset, argument_offset = 6, 8, 10
    else:
        # With step=3 GCC must separately materialize GD_EXEC=2. Admit the
        # reviewed stack-argument store and r7=0 delay slot as well as the cap.
        candidates = [address for address in range(begin, begin + size - 12, 2)
                      if (half(address) == 0xE103 and half(address + 2) == 0x1814 and
                          half(address + 4) == 0xE102 and half(address + 6) == 0x2F12 and
                          half(address + 8) & 0xFF00 == 0xD400 and
                          half(address + 10) & 0xF000 == 0xB000 and
                          half(address + 12) == 0xE700)]
        literal_offset, call_offset, argument_offset = 8, 10, 6
    if len(candidates) != 1:
        raise ValueError('GD step lacks the reviewed fixed' + str(step) +
                         '/store/EXEC-argument/dispatch sequence')
    address = candidates[0]
    service = resident['symbols'].get('_service', 0)
    bases = [pc for pc in range(begin, address, 2)
             if half(pc) & 0xFF00 == 0xD800 and literal(pc) == service + 124]
    displacement = half(address + call_offset) & 0xFFF
    if displacement & 0x800:
        displacement -= 0x1000
    call = address + call_offset + 4 + displacement * 2
    if (len(bases) != 1 or not service or literal(address + literal_offset) != service or
            call != resident['symbols'].get('_kui_retail_gd_dispatch')):
        raise ValueError('Fixed' + str(step) +
                         ' instruction does not target the admitted GD service and dispatch')
    return {'constant_instruction': f'0x{address:08x}', 'step_store': f'0x{address + 2:08x}',
            'service_step_offset': 140, 'service_address': f'0x{service:08x}',
            'value': step, 'GD_EXEC_stack_argument': 2,
            'GD_EXEC_argument_store': f'0x{address + argument_offset:08x}',
            'core_dispatch_call': f'0x{address + call_offset:08x}',
            'r7_delay_slot': 0 if step == 3 else None,
            'scope': 'Reviewed immediate' + str(step) +
                     '/store/EXEC-argument/core-call/delay-slot sequence and linked targets; source/host tests cover branch behavior'}


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
    if exports[:3] != (0x54595031, SNAPSHOT_VERSION, 76) or entry != exports[3]:
        raise ValueError('Worker export ABI or ELF initialization entry differs')
    allocated = allocated_sections(data)
    for address in (*exports[3:9], *exports[15:19]):
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
        (16, '_kui_toy_pilot_driver_load_hook'),
        (17, '_kui_toy_pilot_gd_dispatch'),
        (18, '_kui_toy_pilot_pause_hook'),
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
                  'kui_toy_pilot_snapshot', 'kui_toy_pilot_gd_dispatch',
                  'kui_toy_pilot_gd_project'}
    worker_rows = stack_rows(list((directory / 'worker').rglob('*.su')))
    pure_rows = [row for row in worker_rows if row['function'].split('.', 1)[0] in pure_names]
    for name in ('kui_toy_pilot_request', 'kui_toy_pilot_snapshot',
                 'kui_toy_pilot_gd_project', 'kui_toy_pilot_gd_dispatch'):
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
            'scope': 'Pilot GD path including scalar audio adapter and protected base/map callbacks; no sound SDK calls',
            'ordinary_retail_stack_validator_changed': False, 'call_graph': False}


def linked_halfwords(disassembly):
    """Decode instructions while retaining the existing literal-pool exclusion."""
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
    return decoded, literals


def pure_worker_leaf_audit(worker, disassembly):
    """The scalar GD adapter may call these three reviewed integer leaves.

    Each leaf retains strict no-call/no-indirect/no-external-branch checks.
    The separate adapter audit admits only these linked targets and exactly
    three reviewed protected-low callbacks; it does not weaken this contract.
    PC-relative literal pools are data, as in the linked instruction audit.
    """
    decoded, literals = linked_halfwords(disassembly)
    result = {}
    for name in ('_kui_toy_pilot_request', '_kui_toy_pilot_snapshot',
                 '_kui_toy_pilot_gd_project'):
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


def scalar_worker_adapter_audit(worker, disassembly):
    """Admit the linked scalar adapter, without admitting SDK/storage/service calls.

    Pure calls must load one of exactly three reviewed leaf addresses. The
    other three call sites must load the fifth argument from the established
    caller stack twice, and the protected map slot from the saved GD object
    once. An unknown call origin, new literal target, or external branch is a
    packaging failure, rather than a broader indirect-call exception.
    """
    name = '_kui_toy_pilot_gd_dispatch'
    begin = worker['symbols'].get(name, 0)
    size = worker['symbol_sizes'].get(name, 0)
    if (begin < WORKER_BASE or size < 4 or size > 512 or size % 2 or
            begin + size > WORKER_BASE + len(worker['payload'])):
        raise ValueError('Missing/oversized bounded scalar GD adapter')
    decoded, literals = linked_halfwords(disassembly)
    rows = [row for row in decoded if begin <= row[0] < begin + size and row[0] not in literals]
    if not rows or not any(row[2] == 'rts' for row in rows):
        raise ValueError('Scalar GD adapter lacks a decoded return')
    pure = {worker['symbols'][label]: label for label in (
        '_kui_toy_pilot_request', '_kui_toy_pilot_snapshot', '_kui_toy_pilot_gd_project')}
    # These are protocol/reset constants and the checked P1 destination alias,
    # not callable addresses. Every other loaded literal must be a pure leaf.
    scalar_literals = {0x00010000, 0x00ffffff, 0x8c000000}
    # These exact linked BSS objects retain scalar terminal evidence only.
    # A data address is never an admitted jsr origin below.
    diagnostic_objects = {'_diagnostic_service': 4, '_kui_toy_pilot_native_diagnostics': 32}
    diagnostic_data = set()
    for label, bytes_required in diagnostic_objects.items():
        address = worker['symbols'].get(label, 0)
        if (worker['symbol_sizes'].get(label, 0) != bytes_required or
                not worker['symbols']['__toy_pilot_bss_begin'] <= address or
                address + bytes_required > worker['symbols']['__toy_pilot_bss_end']):
            raise ValueError('Scalar diagnostics lack exact linked BSS ownership: ' + label)
        diagnostic_data.add(address)
    observed_leaves, calls = set(), []
    opaque = {'protected_base_argument': 0, 'protected_map_slot': 0}

    def literal_value(address, opcode):
        at = ((address + 4) & ~3) + (opcode & 255) * 4
        value = struct.unpack('<I', region(worker['payload'], at - WORKER_BASE, 4,
                                           'scalar adapter literal'))[0]
        return at, value

    first_transfer = next((i for i, row in enumerate(rows)
                           if row[2] in ('bra', 'bt', 'bf', 'bt.s', 'bf.s', 'jsr')), len(rows))
    prologue = rows[:first_transfer]
    frame = 0
    for _, opcode, _, _ in prologue:
        if opcode & 0xff0f == 0x2f06 or opcode == 0x4f22:
            frame += 4
        elif opcode & 0xff00 == 0x7f00:
            immediate = opcode & 255
            frame -= immediate - 256 if immediate & 128 else immediate
    if frame != 28 or not any(row[1] == 0x6843 for row in prologue):
        raise ValueError('Scalar GD adapter caller frame/context provenance changed')

    for index, (address, opcode, mnemonic, operands) in enumerate(rows):
        if opcode & 0xf000 == 0xd000 and mnemonic == 'mov.l':
            _, value = literal_value(address, opcode)
            if value in pure:
                observed_leaves.add(value)
            elif value not in scalar_literals and value not in diagnostic_data:
                raise ValueError('Scalar GD adapter gained an unreviewed literal target/value')
        if mnemonic in ('bsr', 'bsrf', 'jmp', 'jmp/n', 'braf', 'rte', 'trapa', 'jsr/n'):
            raise ValueError('Scalar GD adapter gained an unreviewed transfer')
        if mnemonic in ('bra', 'bt', 'bf', 'bt.s', 'bf.s'):
            target = re.match(r'^([0-9a-f]+)\s', operands)
            if (not target or not begin <= int(target[1], 16) < begin + size or
                    int(target[1], 16) in literals):
                raise ValueError('Scalar GD adapter gained an external/invalid branch')
        if mnemonic != 'jsr':
            continue
        register = re.fullmatch(r'@r(\d+)', operands.strip())
        if not register:
            raise ValueError('Scalar GD adapter call has an unknown operand')
        register = int(register[1])
        origin = None
        # Resolve only a nearby, uninterrupted last write to the call register.
        # A branch/call or an unreviewed producer terminates this local proof.
        for previous in reversed(rows[max(0, index - 8):index]):
            at, code, operation, arguments = previous
            if operation in ('bra', 'bt', 'bf', 'bt.s', 'bf.s', 'rts', 'jsr'):
                break
            arguments = arguments.split('!', 1)[0].strip()
            if not re.search(r'(?:^|,)\s*r' + str(register) + r'$', arguments):
                continue
            if code & 0xf000 == 0xd000 and operation == 'mov.l':
                _, value = literal_value(at, code)
                if value in pure:
                    origin = pure[value]
            elif (code & 0xf000 == 0x5000 and
                  (code >> 8) & 15 == register and operation == 'mov.l'):
                source, displacement = (code >> 4) & 15, (code & 15) * 4
                if source == 15 and displacement == frame:
                    origin = 'protected_base_argument'
                elif source == 8 and displacement == 4:
                    origin = 'protected_map_slot'
            break
        if origin is None:
            raise ValueError('Scalar GD adapter gained an unknown/SDK/storage/service call origin')
        if origin in opaque:
            opaque[origin] += 1
        calls.append({'address': f'0x{address:08x}', 'origin': origin})
    if observed_leaves != set(pure) or opaque != {
            'protected_base_argument': 2, 'protected_map_slot': 1}:
        raise ValueError('Scalar GD adapter reviewed leaves/three protected callback sites changed')
    return {'address': f'0x{begin:08x}', 'symbol_bytes': size, 'maximum_symbol_bytes': 512,
            'decoded_nonliteral_halfwords': len(rows), 'leaf_targets': sorted(pure.values()),
            'reviewed_opaque_calls': opaque, 'opaque_call_count': sum(opaque.values()),
            'call_sites': calls, 'caller_frame_bytes': frame,
            'direct_sdk_storage_service_targets': False,
            'no_external_or_unreviewed_indirect_transfers': True}


def early_cache_policy_audit(stage, worker, stage_disassembly, worker_disassembly):
    """Check the actual early RAM-policy patch and actual CCR telemetry read.

    The reviewed compiler sequence is deliberately narrow: identity checking
    must precede the cache-policy write/publication, which must precede arming
    the heap hook. A later installer-only cache patch is not an equivalent
    lifecycle contract and must fail packaging.
    """
    base = retail_layout.STAGE_ADDRESS
    table = stage['symbols'].get('_toy_patches', 0)
    record = struct.pack('<IIHH', 0x8c0c5bc4, 0x105, 4, 10)
    records = region(stage['payload'], table - base, 20 * 12, 'original title patch table')
    if sum(records[at:at + 12] == record for at in range(0, len(records), 12)) != 1:
        raise ValueError('Stage lacks its unique original 0x105 cache-word admission')
    decoded, literals = linked_halfwords(stage_disassembly)
    begin = stage['symbols'].get('_kui_retail_stage_relay', 0)
    headers = [int(match[1], 16) for match in re.finditer(
        r'^([0-9a-f]+) <[^>]+>:$', stage_disassembly, re.M)]
    end = min((at for at in headers if at > begin), default=begin)
    if not base <= begin < end <= begin + 1024:
        raise ValueError('Missing bounded early-stage relay')
    rows = [row for row in decoded if begin <= row[0] < end and row[0] not in literals]

    def literal(image, image_base, row, register, width, expected):
        address, opcode, _, _ = row
        if opcode & 0xff00 != (0xd000 if width == 4 else 0x9000) | register << 8:
            raise ValueError('Early cache audit expected its exact PC-relative load')
        at = (((address + 4) & ~3) + (opcode & 255) * 4 if width == 4 else
              address + 4 + (opcode & 255) * 2)
        value = int.from_bytes(region(image['payload'], at - image_base, width,
                                      'linked cache-policy literal'), 'little')
        if value != expected:
            raise ValueError('Early cache audit literal changed')

    def value_of(row, image, image_base):
        address, opcode, _, _ = row
        if opcode & 0xf000 != 0xd000:
            return None
        at = ((address + 4) & ~3) + (opcode & 255) * 4
        return int.from_bytes(region(image['payload'], at - image_base, 4,
                                     'linked cache call target'), 'little')

    starts = [index for index, row in enumerate(rows) if row[1] & 0xff00 == 0xd100 and
              value_of(row, stage, base) == 0x8c0c5bc4]
    if len(starts) != 1:
        raise ValueError('Early relay lacks its unique title-cache patch')
    index = starts[0]
    patch = rows[index:index + 11]
    if len(patch) != 11:
        raise ValueError('Early cache patch/publication sequence is truncated')
    literal(stage, base, patch[0], 1, 4, 0x8c0c5bc4)
    literal(stage, base, patch[1], 2, 2, 0x105)
    if tuple(row[1] for row in patch[2:5]) != (0x6312, 0x3320, 0x8b00):
        raise ValueError('Early cache patch lost native-word comparison/conditional selection')
    literal(stage, base, patch[5], 9, 2, 0x101)
    if patch[6][1] != 0x2192:
        raise ValueError('Early cache patch lost its actual title-word store')
    literal(stage, base, patch[7], 5, 4, 0x8c0c5bc8)
    literal(stage, base, patch[8], 4, 4, 0x8c0c5bc4)
    if tuple(row[1] for row in patch[9:11]) != (0x480b, 0x0009):
        raise ValueError('Early cache patch lost its bounded publication call')
    prior_targets = {value_of(row, stage, base) for row in rows[:index]}
    if (stage['symbols'].get('_toy_original_patches_check') not in prior_targets or
            stage['symbols'].get('_kui_toy_pilot_stage_publish') not in prior_targets):
        raise ValueError('Early cache patch lacks preceding original-word checking/publication binding')
    following = rows[index + 11:index + 17]
    if len(following) != 6:
        raise ValueError('Early cache patch lacks following heap-hook publication')
    literal(stage, base, following[0], 4, 4, 0x8c04e9b4)
    literal(stage, base, following[2], 5, 4, 0x8c04e9b8)
    if tuple(row[1] for row in following[3:]) != (0x2412, 0x480b, 0x0009):
        raise ValueError('Early cache patch is no longer before heap-hook store/publication')

    worker_rows, worker_literals = linked_halfwords(worker_disassembly)
    snapshot = worker['symbols']['_kui_toy_pilot_snapshot']
    snapshot_end = snapshot + worker['symbol_sizes']['_kui_toy_pilot_snapshot']
    snapshot_rows = [row for row in worker_rows if snapshot <= row[0] < snapshot_end
                     and row[0] not in worker_literals]
    probes = [i for i, row in enumerate(snapshot_rows) if row[1] & 0xff00 == 0xd200 and
              value_of(row, worker, WORKER_BASE) == 0xff00001c]
    if len(probes) != 1:
        raise ValueError('Pilot snapshot lacks actual CCR telemetry')
    probe = probes[0]
    if tuple(row[1] for row in snapshot_rows[probe + 1:probe + 4]) != (0x6222, 0x2239, 0x1124):
        raise ValueError('Pilot snapshot no longer reads/masks/stores actual CCR')
    masks = [row for row in snapshot_rows[:probe] if row[1] & 0xff00 == 0x9300]
    if len(masks) != 1:
        raise ValueError('Pilot snapshot lacks its unique actual-CCR mask')
    literal(worker, WORKER_BASE, masks[0], 3, 2, 0x105)
    return {'title_word': '0x8c0c5bc4', 'admitted_original': '0x00000105',
            'replacement': '0x00000101', 'linked_patch_store': f'0x{patch[6][0]:08x}',
            'before_heap_hook_store_and_native_startup': True,
            'actual_CCR_probe': f'0x{snapshot_rows[probe][0]:08x}',
            'telemetry_mask': '0x00000105', 'expected_telemetry': '0x00000101',
            'hardware_observed_for_this_candidate': False}


def pilot_layout(directory, inputs, gd_fixed_step=0):
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
    gd_timing_audit = None
    if gd_fixed_step:
        sizes = linked_symbol_sizes(inputs(directory / 'resident-sci.elf'))
        timing = resident['symbols'].get('_toy_gd_timing', 0)
        clock = code_symbol(resident, '_toy_gd_clock_valid', retail_layout.LOW_RESIDENT_ADDRESS)
        dispatch = code_symbol(resident, '_step', retail_layout.LOW_RESIDENT_ADDRESS)
        if (sizes.get('_toy_gd_timing') != 24 or timing % 4 or
                not resident['symbols'].get('__retail_resident_bss_begin', 0) <= timing <=
                resident['symbols'].get('__retail_resident_bss_end', 0) - 24 or
                not 0 < sizes.get('_toy_gd_clock_valid', 0) <= 96 or
                not 0 < sizes.get('_step', 0) <= 512 or
                struct.pack('<I', 0x47444D31) not in resident['payload']):
            raise ValueError('Focused GD timing layout, profile helper or report marker differs')
        gd_timing_audit = {
            'timing_words': 6, 'timing_bytes': 24, 'timing_address': f'0x{timing:08x}',
            'clock_profile_helper': f'0x{clock:08x}', 'clock_profile_helper_bytes': sizes['_toy_gd_clock_valid'],
            'GD_EXEC_step': f'0x{dispatch:08x}', 'GD_EXEC_step_bytes': sizes['_step'],
            'terminal_marker': '0x47444d31', 'snapshot_version': SNAPSHOT_VERSION,
            'snapshot_bytes': 448, 'existing_snapshot_prefix_bytes': 320,
            'scope': 'Actual linked storage, profile helper, dispatch and terminal marker; source/host tests verify branch and field order',
            'fixed_step_store': fixed_step_store_audit(resident, sizes['_step'], gd_fixed_step),
        }
    elif '_toy_gd_timing' in resident['symbols'] or '_toy_gd_clock_valid' in resident['symbols']:
        raise ValueError('Baseline image unexpectedly contains the focused GD timing experiment')
    for name in ('read', 'write', 'copy', 'publish'):
        if '_kui_toy_pilot_bus_' + name not in worker['symbols']:
            raise ValueError('Missing linked bounded pilot bus operation: ' + name)
    if '_kui_toy_pilot_lease_allocate' not in worker['symbols']:
        raise ValueError('Missing linked bounded tracked sound lease')
    ring_symbols = {}
    for name in ('_ring_observe', '_ring_write_allowed', '_ring_recover'):
        matches = [symbol for symbol in worker['symbols']
                   if symbol == name or symbol.startswith(name + '.')]
        if not matches:
            raise ValueError('Linked image lacks the continuous-ring ownership path: ' + name)
        for symbol in matches:
            code_symbol(worker, symbol, WORKER_BASE)
            ring_symbols[symbol] = {'address': f"0x{worker['symbols'][symbol]:08x}",
                                    'bytes': worker['symbol_sizes'][symbol]}
    # Original lifecycle forwarding (including global stop) remains required.
    # New worker observations/copies/allocations/packets may not call these
    # previous unbounded helpers. Numerical scan complements the source audit.
    for address in (0x8c0840d6, 0x8c083fae, 0x8c069c00, 0x8c083d34,
                    0x8c068f20, 0x8c068b28, 0x8c069158, 0x8c0690d4, 0x8c068a04):
        if struct.pack('<I', address) in worker['payload']:
            raise ValueError('Worker retains unbounded sound helper literal: ' + hex(address))
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
    if len({(embedded[name]['address'], embedded[name]['bytes'])
            for name in ('scif', 'sci', 'ide', 'scia')}) != 1:
        raise ValueError('Pilot stage must share one resident blob across legacy symbols')
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
    leaf = scalar = pause_stack = None
    disassemblies = {}
    for name in bounds:
        disassembly = subprocess.check_output(
            ['sh-elf-objdump', '-d', str(directory / (name + '.elf'))], text=True)
        disassemblies[name] = disassembly
        try:
            instructions[name] = audit(name, disassembly)
        except SystemExit as error:
            raise ValueError(str(error)) from error
        if name == 'worker':
            leaf = pure_worker_leaf_audit(worker, disassembly)
            scalar = scalar_worker_adapter_audit(worker, disassembly)
            pause_stack = audit_pause_stack(worker)
    cache_policy = early_cache_policy_audit(stage, worker, disassemblies['stage'], disassemblies['worker'])
    native_scratch = audit_native_scratch(resident, worker)
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
        'GD_scalar_worker_adapter_audit': scalar,
        'native_pause_stack_audit': pause_stack,
        'GD_timing_identity_audit': gd_timing_audit,
        'continuous_ring_identity_audit': {'linked_ownership_symbols': ring_symbols,
                                           'scope': 'Actual emitted ownership helpers; host tests audit behavior'},
        'native_scratch_audit': native_scratch,
        'early_cache_policy_audit': cache_policy,
        'legacy_blob_slots': 'four aliases of one admitted standard SCI resident',
        'embedded_resident_copies': 1,
        'resident_address': '0x8c004000', 'resident_limit': '0x8c007800',
        'hook_stack': ['0x8c007800', '0x8c007d00'],
        'worker_reservation': ['0x8cfd0000', '0x8cfe0000'],
        'worker_private_stack': [f'0x{bottom:08x}', f'0x{top:08x}'], 'manifest_slots': 64,
        'worker_export_words': worker['exports'],
    }


def collect(commit, published_tree, *, local_source=False,
            build_dir=BUILD_DIRECTORY, sci_reuse_tdre=0, runtime_file=GD_RUNTIME_FILE,
            gd_fixed_step=3):
    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before packaging')
    checkpoint = git('rev-parse', 'HEAD')
    tree = git('rev-parse', 'HEAD^{tree}')
    if local_source and (published_tree is not None or commit != checkpoint):
        raise ValueError('Local source must identify the exact committed checkpoint without a published tree')
    if not local_source and tree != published_tree:
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

    if gd_fixed_step != 3 or sci_reuse_tdre != 0 or runtime_file != GD_RUNTIME_FILE:
        raise ValueError('The video test requires fixed three-sector steps and the retained SCI transport')
    build = (ROOT / build_dir).resolve()
    if not build.is_relative_to(ROOT) or build == ROOT:
        raise ValueError('Build directory must be a separate directory within this source checkout')
    config_bytes = remember(build / 'build-config')
    ring_constants = ring_contract(remember(ROOT / 'include/kui/toy_pilot_ring.h'))
    pilot_constants = pilot_contract(remember(ROOT / 'include/kui/toy_pilot.h'))
    report_words = snapshot_legend()
    if gd_fixed_step:
        report_words.insert(5, list(GD_TIMING_WORDS))
    config = build_config(build / 'build-config')
    required = {'BUILD': commit[:12], 'PROFILE': '15', 'LOW': '1', 'SLOTS': '64',
                'SCI': '1', 'PILOT': '1', 'SCI_PACED': '1', 'SCI_FAULT': '1',
                'SCI_REUSE_TDRE': str(sci_reuse_tdre),
                'GD_FIXED_STEP': str(gd_fixed_step),
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
    linked = pilot_layout(build, remember, gd_fixed_step=gd_fixed_step)
    if linked['embedded_blobs']['worker']['sha256'] != DIAGNOSTIC_WORKER_SHA256:
        raise ValueError('The pilot differs from the reviewed diagnostic ring worker')
    add(runtime_file, runtime)
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
    source_url = None if local_source else 'https://github.com/TPMJB/K-UI-NeXT/tree/' + commit
    if source_url:
        add('source-url.txt', (source_url + '\n').encode('utf-8'))
    else:
        add('source-state.txt', ('Unpublished local checkpoint; complete source is in source-snapshot.tar.\n'
                                'Commit: ' + commit + '\nTree: ' + tree + '\n').encode('utf-8'))
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT)
    add('source-snapshot.tar', snapshot)
    sci_prototype = read_file(ROOT / 'docs/evidence/cdda-sci-interleave-cache-prototype.patch')
    add('experiments/sci-interleave-cache.patch', sci_prototype)
    add('evidence/sci-interleave-cache-tests.txt',
        read_file(ROOT / 'docs/evidence/cdda-sci-interleave-cache-tests.txt'))
    for name in ('cdda-toy-video-host-tests.txt',
                 'cdda-eight-block-console-clean-2026-10-08.json',
                 'cdda-eight-block-cadence-2026-10-08.json',
                 'cdda-eight-block-cadence-2026-10-08-optimized.jsonl',
                 'cdda-eight-block-cadence-2026-10-08-sanitized.jsonl'):
        data = read_file(ROOT / 'docs/evidence' / name)
        add('docs/evidence/' + name, data)
        add('evidence/' + name, data)
    source_hashes = {}
    for name in sorted(tracked):
        if (name in ('Makefile.toy_pilot', 'src/loader/sci_sd_bus.c',
                     'src/loader/retail_resident.c',
                     'tools/toy_pilot_pause_stack_audit.py',
                     'tools/toy_pilot_scratch_audit.py') or name.startswith(('src/loader/toy_pilot',
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
        if source == README_SOURCE:
            data = selected_checklist(data, gd_fixed_step)
        copied = document_copy(data, source, destination, available, tracked, commit)
        if local_source:
            # Keep local delivery navigable without inventing a public commit.
            # The exact original target remains in the byte-preserved source tar.
            archive = posixpath.relpath('source-snapshot.tar', posixpath.dirname(destination) or '.')
            copied = re.sub(rb'https://github\.com/TPMJB/K-UI-NeXT/(?:blob|tree)/' +
                            commit.encode('ascii') + rb'/[^\s)]+',
                            archive.encode('ascii'), copied)
        add(destination, copied)
    metadata = {
        'source_commit': commit, 'source_tree': tree, 'local_checkpoint': checkpoint,
        'source_dirty': False,
        'source_publication': ('unpublished local checkpoint; complete source snapshot included' if local_source
                               else 'published; exact tree equals local checkpoint'),
        'source_url': source_url, 'source_snapshot': 'source-snapshot.tar',
        'source_snapshot_sha256': sha(snapshot),
        'pilot_source_sha256': source_hashes,
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'scope': 'Exact-title Toy Commander three-sector video test; retained eight-block CDDA worker',
        'separate_SCI_prototype': {
            'source_commit': '62835ff84a570dfa02824ab34af6b7e030f682c8',
            'base_commit': '56df22966bbb9093374372f8dad7a2e09a05fcb7',
            'patch_file': 'experiments/sci-interleave-cache.patch',
            'patch_sha256': sha(sci_prototype),
            'linked_into_runtime': False,
            'hardware_tested': False,
            'sanitizer_configurations_passed': 6,
            'synthetic_audio_physical_blocks': {'baseline': 178, 'prototype': 147},
            'synthetic_total_physical_blocks': {'baseline': 434, 'prototype': 403},
            'validation_log': 'evidence/sci-interleave-cache-tests.txt',
        },
        'profile_number': 15, 'hardware_tested': False, 'numerical_pass_target': None,
        'host_validation': {
            'regression_suites_passed': 22,
            'regression_log': 'evidence/cdda-toy-video-host-tests.txt',
            'canonical_schedules': {'baseline_passed': 54, 'candidate_passed': 72, 'total': 72},
            'expanded_schedules': {'baseline_passed': 63, 'candidate_passed': 120, 'total': 144},
            'all_baseline_successes_retained': True,
            'optimized_and_sanitized_agree': True,
            'remaining_failures': '24 synthetic 20 Hz profiles conservatively recover with UNOBSERVED_HALF',
            'cadence_evidence': 'evidence/cdda-eight-block-cadence-2026-10-08.json',
            'cadence_scope': 'Retained comparison of the unchanged eight-block audio worker; not a three-sector GD video model or hardware result',
        },
        'file': runtime_file, 'build': envelope['build'], 'runtime_sha256': sha(runtime),
        'variant': 'retained eight-block CDDA with fixed three-sector GD steps and baseline SCI',
        'SCI_TDRE_sample_reuse': bool(sci_reuse_tdre),
        'GD_fixed_step_sectors': gd_fixed_step,
        'payload_crc32': f'{zlib.crc32(payload):08x}', 'envelope': envelope,
        'linked_layout': linked, 'build_configuration': config,
        'build_configuration_sha256': sha(config_bytes), 'evidence_sha256': evidence,
        'document_source_sha256': document_hashes,
        'retained_fallback': {
            'source_checkpoint': '7b55156aafa26a74c0c3ef3f595a9756cba5c73d',
            'runtime_build': '7b55156aafa2',
            'git_tag': 'cdda-console-clean-7b55156-20261008',
            'package_filename': 'K-UI-CDDA-Eight-Block-Test.zip',
            'package_sha256': 'ffd6628beaf4c964111598d1e19d902363b387c74b4b018ae11e9afeb43e2aae',
            'console_run_audio_report': 'user heard no interruptions; all eight recovery counters and handoff gaps zero',
            'console_run_evidence': 'evidence/cdda-eight-block-console-clean-2026-10-08.json',
            'audio_worker_byte_identical_to_fallback': True,
            'fallback_package_included_or_overwritten': False,
        },
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
            'runtime_driver_input_sha256_checked_after_file_load_before_sdk_install': True,
            'runtime_driver_hash_bytes_exclude_install_alignment_padding': True,
            'installed_arm_driver_readback_sha256_checked': False,
        },
        'installation': {
            'manual_only': True, 'card_target': '/KUI/apps/games/retail-boot.kui',
            'required_runtime': 'retained working 1.8.5 runtime',
            'working_reader_backup': '/KUI/apps/games/retail-boot-before-observe.kui',
            'working_CDDA_backup': '/KUI/apps/games/retail-boot-working-cdda.kui',
            'working_runtime_replaced_in_bundle': False, 'ordinary_reader_default_changed': False,
            'game_files_or_descriptor_installed': False, 'card_path_configuration_installed': False,
            'pilot_installed_on_extraction': False, 'prior_artifacts_overwritten': False,
        },
        'audio_contract': {
            'admitted_ring_constants': ring_constants,
            'admitted_pilot_constants': pilot_constants,
            'main_heap_lease_bytes': LEASE_BYTES, 'main_heap_lease': ['0x8cfd0000', '0x8d000000'],
            'worker_body': ['0x8cfd0000', '0x8cfe0000'],
            'upper_lease_padding_used_by_worker': False,
            'sound_fixed_heap_bytes': SOUND_BYTES, 'logical_refill_blocks': BLOCK_COUNT,
            'mono_block_bytes': MONO_BLOCK_BYTES, 'stereo_bytes_per_block': MONO_BLOCK_BYTES * 2,
            'stereo_frames_per_block': BLOCK_FRAMES,
            'mono_ring_bytes': RING_MONO_BYTES, 'stereo_frames_per_ring': RING_FRAMES,
            'cursor_proof_halves': 2, 'cursor_proof_half_frames': 16384,
            'unobserved_progress_rejection_frames': 16384,
            'sample_rate': 44100, 'ports': [62, 63], 'hardware_looping': True,
            'logical_block_duration_milliseconds_approximately': 93,
            'nominal_retired_block_refill_milliseconds_approximately': 650,
            'nominal_refill_window_is_observed_or_guaranteed': False,
            'ring_duration_milliseconds_approximately': 743,
            'periodic_finite_voice_restart': False,
            'uninterrupted_audio_established': False,
            'ARM_side_refill_stop_watchdog': False,
            'SH_service_stall_can_replay_buffered_audio': True,
            'driver_upload_or_arm_reset': False, 'timer_reprogramming': False,
            'frame_callback_is_a_guaranteed_service_deadline': False,
            'all_games_compatible': False, 'video_skip_fixed': False,
            'confirmed_Start_fix_baseline': '6bb04d248b35',
            'prior_Start_mapping_fix_retained': True,
            'video_skip_pause_retry_has_bounded_retirement_pump': True,
            'live_GD_GETSCD_REQ_STAT_DRIVE_audio_position_refreshed': True,
            'baseline_subcode_audio_status': 21,
            'baseline_subcode_audio_status_hex': '0x15',
            'audio_status_notation': 'GETSCD status bytes are hexadecimal 0x11/0x12/0x13/0x14/0x15',
            'pilot_cursor_telemetry_establishes_game_GD_position_responses': True,
            'audible_or_stereo_output_established_by_build_checks': False,
            'game_sdk_waits_proved_bounded': False,
            'new_pilot_sound_operations_call_game_sdk': False,
            'new_pilot_G2_transactions_bounded': True,
            'G2_transaction_poll_cap': 10000,
            'G2_transaction_TMU0_tick_cap': 1563,
            'G2_transaction_nominal_limit_milliseconds': 2,
            'G2_DMA_enabled_or_started_action': 'defer; no DMA register writes',
            'unresolved_BUSY_nominal_limit_seconds': 1,
            'queue_commit': 'payload first, header last; published-stalled is never retried',
            'sound_lease': 'tracked native tail record after bounded canary drain',
            'quiescent_service_sound_bus_or_heap_work': False,
            'terminal_snapshot_version': SNAPSHOT_VERSION,
            'terminal_snapshot_bytes': 448,
            'existing_snapshot_prefix_bytes': 320,
            'v5_snapshot_prefix_bytes': 384,
            'v6_snapshot_prefix_retained_bytes': 440,
            'diagnostic_extension_bytes': 128,
            'diagnostic_extension_page': 6 if gd_fixed_step else 5,
            'pressure_extension_page': 7 if gd_fixed_step else 6,
            'diagnostic_worker_SHA256': DIAGNOSTIC_WORKER_SHA256,
            'diagnostic_recovery_count_scope': 'successfully queued internal STOP/reprime decisions only; excludes intentional controls, source EOF and refused STOP attempts',
            'diagnostic_recovery_reason_legend': {
                '0': 'none; no successful internal recovery decision recorded',
                '1': 'invalid ports: activity or cursor outside admitted ring',
                '2': 'circular stereo separation beyond 64 frames plus the bounded publication-age advance',
                '3': 'unobserved progress exceeds the retained 16384-frame proof bound or excessive cursor delta',
                '4': 'implausible progress or played-frame overflow',
                '5': 'initial START proof deadline or upper bound',
                '6': 'missing READY block or absolute first-frame mismatch',
                '7': 'copy reserve exhausted',
                '8': 'stream fill overflow',
            },
            'diagnostic_recovery_counter_index': 'reason minus one; eight counters',
            'diagnostic_last_GD_command_scope': 'captured at the recovery decision before STOP publication',
            'diagnostic_cursor_age_scope': 'TMU0 ticks since the last accepted coherent cursor observation; zero if start not yet proven',
            'diagnostic_data_block_scope': 'sampled first command16/17 fill denial while running and needing the next ordered block, through first observed fill opportunity, control application, revoke, fault or successful recovery; includes time between visits',
            'diagnostic_data_block_is_card_busy_measurement': False,
            'diagnostic_data_block_interval_count_scope': 'completed sampled spans only; the open span is excluded',
            'diagnostic_data_block_open_scope': 'open flag1 indicates a span not yet included in max/total; durations are retained only at closure',
            'diagnostic_reserve_pressure_scope': 'coherent denied copy-gate inputs captured before its STOP attempt; can survive a deferred or superseded STOP; not a successful-recovery count',
            'diagnostic_reserve_site_legend': {'1': 'playback', '2': 'fill begin', '3': 'pre-card', '4': 'post-card', '5': 'copy plane'},
            'diagnostic_reserve_proof_age_scope': 'TMU0 ticks since the older justified per-channel hardware capture lower bound, not since the accepted observation',
            'diagnostic_bank_field_scope': 'bank IDs are 0 through 7; bank_fills counts completed 4096-frame blocks, bank_ends counts slower-channel retired 4096-frame blocks, and bank_starts counts full hardware START requests; the 448-byte wire layout is unchanged from v7',
            'diagnostic_observed_window_scope': 'TMU0 ticks and inclusive worker-visit difference from the target block\'s accepted slower-channel retirement or initial START to the denied gate; ordinary proof renewal does not reset this per-block anchor; not physical entry time or raw-read count',
            'diagnostic_raw_read_scope': 'actual callback only; includes failed live reads, excludes cached remnants and callbacks returning to revoked/stale epochs; total modulo32 bits',
            'worker_config_version': SNAPSHOT_VERSION,
            'worker_config_bytes': 52,
            'worker_fault_report': 'direct low terminal hook after restoring SR; no game teardown',
            'controller_report': 'exact reset callback before game sound teardown',
            'ring_refill_ownership': 'two changed publications per channel within 50ms; same 16384-frame proof half, at most 64 frames phase; slower channel retires logical blocks, faster channel and capture uncertainty protect each target block\'s next absolute start with the unchanged 5ms copy reserve',
            'ring_probe_maximum_observations_per_attempt': 16,
            'ring_probe_maximum_attempts_per_worker_visit': 8,
            'additional_bounded_observation_attempts': {'entry': 1, 'after_refill': 1},
            'additional_observation_time_charged_to_service_entry_clock': True,
            'ring_probe_observation_admission_TMU0_tick_limit': 391,
            'ring_probe_admission_can_overrun_by_one_observation': True,
            'ring_publication_proof_TMU0_tick_limit': 39063,
            'ring_capture_lower_bound': 'last before-paired-read observation of the value two distinct changes back, independently per channel; older unsigned channel age anchors copies',
            'ring_capture_lower_bound_uses_intervening_changed_value_observation': False,
            'ring_stereo_phase_maximum_frames': 64,
            'ring_phase_recovery_allowance': '64 frames plus ceil(frames since this probe began); bounded by the unchanged 50 ms probe window',
            'ring_ambiguous_stereo_pair_action': 'retry without accepting a new cursor or retiring another block; previous proof retains only its existing conservative copy deadline',
            'ring_asynchronous_publication_allowance_changes_ownership_acceptance': False,
            'ring_copy_reserve_TMU0_ticks': 3907,
            'ring_missed_or_ambiguous_deadline': 'request STOP; retain memory ownership; restart only after acknowledged STOP, full ring plus20ms, fresh inactive ports',
            'ring_STOP_fence_from_original_START': False,
            'updater_breadcrumbs_are_a_watchdog': False,
            'GD_audio_completion': 'first EXEC acknowledges accepted mailbox command',
            'GD_audio_completion_waits_for_worker_application': False,
            'applied_generation_reports_actual_worker_application': True,
            'service_visits_per_native_updater': 2,
            'maximum_raw_sectors_per_fill_quantum': 1,
            'maximum_ordered_block_fragments_per_fill_quantum': 2,
            'second_fragment_must_use_same_generation_tagged_cached_raw_sector': True,
            'maximum_fill_quanta_per_worker_visit': 4,
            'extra_fill_quantum_admission_TMU0_ticks': 12500,
            'extra_fill_quantum_admission_nominal_milliseconds': 16,
            'refill_admission_budget_can_overrun_by_one_quantum': True,
            'raw_sector_cache': 'existing buffer; exact LBA and current nonzero request generation',
            'native_IRQ_private_stack_scratch': 'dispatch-scoped exact16 bytes: CHECK write at PR8c0bd374, REQUEST36 read at PR8c0bd57e; captured SP and linked stack bounds',
            'private_raw_buffer_bytes': 2352,
            'native_control_fault_captures_exact_GD_ownership_and_CHECK': True,
            'GD_completion_or_native_owner_forcibly_cleared': False,
            'cache_policy': {
                'title_word_address': '0x8c0c5bc4', 'admitted_original': '0x00000105',
                'replacement': '0x00000101', 'patch_before_native_system_initialization': True,
                'retains_instruction_and_operand_caches': True,
                'operand_cache_policy': 'P1 write-through before title movie callbacks invalidate tags',
                'actual_runtime_check': 'cache_policy snapshot is actual CCR & 0x105; expected 0x101',
                'post_heap_patch_is_sufficient': False,
                'hardware_confirmed_for_this_candidate': False,
            },
            'pause_bridge': {
                'authored_worker_stack': 'guarded private worker stack',
                'native_SDK_CHECK_stack': 'validated suspended game caller stack below preserved bridge frame',
                'native_SDK_CHECK_uses_private_worker_stack': False,
                'native_game_stack_reservation_bytes': 192,
                'reviewed_known_native_frame_bytes': 128,
                'SDK_internal_stack_frames_included_in_private_stack_audit': False,
                'callback_guard': 'validated native callback identity and scalar error classes; delegate-taking classes are unreachable for this GD encoder',
                'native_callback_or_SDK_waits_proved_bounded': False,
                'native_file_owner_action': 'defer manual SDK pump; allow existing VBlank retirement within retry budget',
                'native_file_completion_callbacks_called_by_pilot': False,
                'returning_retry_attempt_limit': 65536,
                'returning_retry_tick_limit': 781250,
                'successful_pause_drain_tick_limit': 1562500,
                'successful_pause_drain_attempt_limit': 1048576,
                'retry_budget_guarantees_VBlank_opportunity': False,
                'pause_detail_legend': {
                    '0': 'no pause bridge admission fault',
                    '1': 'entry SR.BL prevents admission',
                    '2': 'excessive gap between pause retries',
                    '3': 'pause retry count/time budget exceeded',
                    '4': 'native context/table admission or retry context failed',
                    '5': 'native context invalid after SDK retirement visit',
                    '6': 'reserved; former incorrect dynamic delegate veto removed',
                    '7': 'suspended native game stack/reservation failed admission',
                },
                'hardware_confirmed_for_this_candidate': False,
            },
            'queue_reuse_acknowledgement': 'full packet plus driver/request generation under one mask',
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
        'report': {'title': PILOT_LABEL.decode('ascii'), 'pages': len(report_words),
                   'displayed_page_numbers': list(range(len(report_words))), 'rows_per_page': 4,
                   'words_per_row': 4, 'word_legend_by_page': report_words,
                   'page_frames': 900, 'pages_repeat_until_power_off': True,
                   'nominal_page_seconds_at_60hz': 15, 'nominal_page_seconds_at_50hz': 18,
                   'numerical_pass_target': None},
        'GD_step_test': {
            'enabled': bool(gd_fixed_step), 'maximum_sectors_per_data_step': gd_fixed_step or None,
            'previous_hardware_clean_CDDA_data_step': 2,
            'mixed_GD2_GD3_sanitizer_scenarios_passed': 18,
            'mixed_model_evidence': 'evidence/cdda-toy-video-gd-model-2026-10-08.md',
            'long_GD_ownership_can_still_require_audio_recovery': True,
            'paced_SCI_TDRE_reuse': bool(sci_reuse_tdre),
            'sector_limit_is_a_wall_clock_deadline': False,
            'measured_scope': 'GD EXEC service dispatch with credited data sectors under its existing SR mask; excludes IRQ hook entry/exit and trailing profile-check overhead',
            'retained_timing_requires_profile_checks_before_and_after_read': True if gd_fixed_step else None,
            'timing_profile': 'FRQCR low12=0e0a; TMU0 reload=ffffffff, TCR0&0027=0002, running',
            'total_ticks_wrap_modulo_32_bits': True,
            'untimed_step_reasons': 'incompatible profile, zero start counter or zero elapsed ticks',
            'terminal_extra_page': 5 if gd_fixed_step else None,
            'terminal_extra_page_storage': 'existing 512-byte image.block; original snapshot prefix at 0..319, GD timing at 320..383, worker diagnostics at 384..447, pressure and observed-window fields at 448..511',
            'terminal_extra_page_marker': '0x47444d31' if gd_fixed_step else None,
            'hardware_tested': False,
            'comparison_baseline': '7b55156aafa2',
            'diagnostic_ring_worker_SHA256': DIAGNOSTIC_WORKER_SHA256,
        },
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode('utf-8'))
    if ({name for name in files if name.endswith('.kui')} != {runtime_file} or
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


def collect_bundle(commit, published_tree, *, local_source=False,
                   build_dir=BUILD_DIRECTORY, sci_build_dir=None, gd_fixed_step=3):
    """Audit one isolated video test with the retained audio and SCI transport."""
    if gd_fixed_step != 3 or sci_build_dir is not None:
        raise ValueError('The video test must contain one three-sector baseline-SCI runtime')
    files = collect(commit, published_tree, local_source=local_source,
                    build_dir=build_dir, gd_fixed_step=gd_fixed_step,
                    runtime_file=GD_RUNTIME_FILE)
    files.pop('SHA256SUMS')
    metadata = json.loads(files['build.json'])
    metadata['test_order'] = [GD_RUNTIME_FILE]
    metadata['optional_SCI_variant'] = None
    metadata['evidence_directory'] = 'evidence/toy-pilot/'
    metadata['bundle_runtime_files'] = metadata['test_order']
    metadata['package_revision'] = 11
    files['build.json'] = (json.dumps(metadata, indent=2) + '\n').encode('utf-8')
    if {name for name in files if name.endswith('.kui')} != {GD_RUNTIME_FILE}:
        raise ValueError('The video bundle contains an unexpected runtime')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD') != metadata['local_checkpoint'] or
            git('rev-parse', 'HEAD^{tree}') != metadata['source_tree']):
        raise ValueError('Source checkpoint changed while collecting the video test')
    files['SHA256SUMS'] = ''.join(sha(data) + '  ' + name + '\n'
                                 for name, data in sorted(files.items())).encode('utf-8')
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-commit', required=True, type=object_id)
    parser.add_argument('--build-dir', type=Path, default=Path(BUILD_DIRECTORY),
                        help='Video test build with GD_FIXED_STEP=3 and SCI_REUSE_TDRE=0')
    parser.add_argument('--sci-build-dir', type=Path,
                        help='Rejected for this isolated video test')
    parser.add_argument('--gd-fixed-step', type=int, choices=(3,), default=3,
                        help='Required fixed three-sector GD data-step policy')
    publication = parser.add_mutually_exclusive_group(required=True)
    publication.add_argument('--published-tree', type=object_id)
    publication.add_argument('--local-source', action='store_true',
                             help='Package the exact local checkpoint without claiming publication')
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
        write_archive(output, collect_bundle(args.source_commit, args.published_tree,
                                             local_source=args.local_source,
                                             build_dir=args.build_dir,
                                             sci_build_dir=args.sci_build_dir,
                                             gd_fixed_step=args.gd_fixed_step))
        print(json.dumps({'file': str(output), 'bytes': output.stat().st_size,
                          'sha256': sha(output.read_bytes()), 'build': args.source_commit[:12],
                          'profile': 15}, indent=2))
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
