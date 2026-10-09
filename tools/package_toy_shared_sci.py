#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the isolated Toy asynchronous CDDA and shared SCI experiment."""
import argparse
import io
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tarfile
import tempfile
import zipfile

from check_loader_layout import EH, PH, SH, SYM, inspect_elf, padded, region
from check_retail_loader_layout import FORBIDDEN_PREFIXES, FORBIDDEN_SYMBOLS, check_bss, code_symbol
from check_retail_instructions import audit
from package_cdda_calibration import ROOT, archive_name, build_config, git, object_id, read_file, sha, write_archive
from package_cdda_preflight import allocated_sections
from package_cdda_toy_pilot import (BOOT_SHA256, DRIVER_SHA256, WORKER_BASE, WORKER_LIMIT,
    WORKER_EXPORTS, LEASE_BYTES, SNAPSHOT_VERSION, PILOT_LABEL, embedded_blob,
    linked_halfwords, linked_symbol_sizes, pilot_contract, ring_contract,
    snapshot_legend, stack_rows)
import retail_package as retail_layout
from retail_package import inspect_retail
from runtime_package import envelope, flatten_elf, verify
from toy_pilot_pause_stack_audit import audit_pause_stack
from toy_pilot_scratch_audit import audit_native_scratch

OUTPUT_NAME = 'K-UI-Toy-Async-CDDA-SCI-Test.zip'
README_SOURCE = 'docs/toy-shared-sci-test.md'
CONSOLE_EVIDENCE = 'docs/evidence/toy-shared-sci-console-2026-10-09.md'
TOPUP_EVIDENCE = 'docs/evidence/toy-shared-sci-topup-console-2026-10-09.md'
RUNTIME_FILE = 'KUI/apps/games/retail-boot.kui'
LAUNCHER_FILE = 'KUI/runtime.kui'
LAUNCHER_BUNDLE_SHA = '821391e91db7150b5a06707c6d8576ae41a0c56040a3147370a241e8cb0cc0b0'
LAUNCHER_SHA = '70db2b9c5958bd392443d8b763ef7b27658d853a1913981080a2ded386558bae'
SHARED_WORDS = ('calls', 'irq_calls', 'call_blocks', 'irq_blocks',
    'audio_claims', 'audio_pending', 'audio_releases', 'data_resumes',
    'errors', 'retries', 'token_yields', 'polled_blocks',
    'work_ticks_max', 'irq_ticks_max', 'active_token', 'card_owned')


def shared_report_contract():
    source = read_file(ROOT / 'src/loader/toy_pilot_sci.h').decode()
    found = re.search(r'struct kui_toy_pilot_sci_stats\s*\{([^}]*)\};', source, re.S)
    if not found:
        raise ValueError('Missing shared transport telemetry ABI')
    words = []
    for statement in found[1].split(';'):
        if not statement.strip():
            continue
        declaration = re.fullmatch(r'\s*uint32_t\s+(.+?)\s*', statement, re.S)
        if not declaration:
            raise ValueError('Shared stats contain a non-uint32 field')
        words.extend(name.strip() for name in declaration[1].split(','))
    if tuple(words) != SHARED_WORDS:
        raise ValueError('Shared report field order differs from package/checklist legend')
    config = read_file(ROOT / 'include/kui/toy_pilot.h').decode()
    if ('uint32_t sci_card, sci_acquire, sci_release, sci_healthy;' not in config or
            'sizeof(struct kui_toy_pilot_config)==68u' not in config):
        raise ValueError('Shared low SCI callback config ABI changed')
    pages = snapshot_legend()
    pages.insert(5, list(SHARED_WORDS))
    return pages

def inspect_shared_worker(data):
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
        (17, '_kui_toy_pilot_gd_bridge'),
        (18, '_kui_toy_pilot_pause_hook'),
    )})
    if any(symbols.get(name) != address for name, address in expected_symbols.items()):
        raise ValueError('Worker export reservations disagree with linked symbols')
    return {'payload': bytes(payload), 'symbols': symbols, 'memory_end': worker_end,
            'exports': list(exports), 'symbol_sizes': symbol_sizes}


def shared_early_cache_policy_audit(stage, worker, stage_disassembly, worker_disassembly):
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
    if tuple(row[1] for row in snapshot_rows[probe + 1:probe + 4]) != (0x6222, 0x2239, 0x1128):
        raise ValueError('Pilot snapshot no longer reads/masks/stores actual CCR')
    masks = [row for row in snapshot_rows[:probe] if row[1] & 0xff00 == 0x9300]
    if len(masks) != 1:
        raise ValueError('Pilot snapshot lacks its unique actual-CCR mask')
    literal(worker, WORKER_BASE, masks[0], 3, 2, 0x105)
    # The changed register base must still store word70 in the unchanged
    # API8 prefix. Bind it to actual owner+68 (shared config) +70*4.
    bases = [row for row in snapshot_rows[:probe] if row[1] & 0xff00 == 0xd100 and
             value_of(row, worker, WORKER_BASE) == worker['symbols'].get('_owner', 0) + 68 + 70 * 4 - 32]
    if len(bases) != 1:
        raise ValueError('Shared CCR store does not target the admitted snapshot word70')
    return {'title_word': '0x8c0c5bc4', 'admitted_original': '0x00000105',
            'replacement': '0x00000101', 'linked_patch_store': f'0x{patch[6][0]:08x}',
            'before_heap_hook_store_and_native_startup': True,
            'actual_CCR_probe': f'0x{snapshot_rows[probe][0]:08x}',
            'telemetry_mask': '0x00000105', 'expected_telemetry': '0x00000101',
            'hardware_observed_for_this_candidate': False}


def shared_layout(directory, inputs):
    """Keep relocation/memory/identity gates, plus separate shared stack audits."""
    from toy_shared_sci_audit import (audit_shared_bridges, audit_shared_stacks,
                                    audit_shared_schedule, audit_async_cdda)
    bounds = {'entry': (retail_layout.EXEC_ADDRESS,
                        retail_layout.EXEC_ADDRESS + retail_layout.STAGE_BLOB_OFFSET + retail_layout.STAGE_MAX_BYTES),
              'stage': (retail_layout.STAGE_ADDRESS, retail_layout.STAGE_MEMORY_END),
              'resident-sci': (retail_layout.LOW_RESIDENT_ADDRESS, retail_layout.LOW_RESIDENT_LIMIT),
              'worker': (WORKER_BASE, WORKER_LIMIT)}
    images, sections, disassemblies, instructions = {}, {}, {}, {}
    for name, (base, limit) in bounds.items():
        raw = inputs(directory / (name + '.elf'))
        images[name] = inspect_shared_worker(raw) if name == 'worker' else inspect_elf(raw, base, limit)
        sections[name] = allocated_sections(raw)
        bad = [symbol for symbol in images[name]['symbols']
               if symbol.startswith(FORBIDDEN_PREFIXES) or symbol in FORBIDDEN_SYMBOLS or
               symbol.startswith('_kui_cdda_aica_')]
        if bad:
            raise ValueError('Unexpected runtime or detached sound owner: ' + bad[0])
        disassembly = subprocess.check_output(['sh-elf-objdump', '-d', str(directory / (name + '.elf'))], text=True)
        disassemblies[name] = disassembly
        try:
            instructions[name] = audit(name, disassembly)
        except SystemExit as error:
            raise ValueError(str(error)) from error
    entry, stage, resident, worker = (images[name] for name in bounds)
    es, ss, rs, ws = (image['symbols'] for image in (entry, stage, resident, worker))
    if (rs.get('__retail_hook_stack_bottom') != 0x8C007800 or
            rs.get('__retail_hook_stack') != 0x8C007D00):
        raise ValueError('Standard low hook stack changed')
    check_bss(stage, retail_layout.STAGE_ADDRESS, '__retail_stage')
    check_bss(resident, retail_layout.LOW_RESIDENT_ADDRESS, '__retail_resident')
    for symbol in ('_kui_retail_resident_init', '_kui_retail_resident_hook',
                   '_kui_retail_resident_dispatch', '_kui_retail_gd_dispatch',
                   '_kui_retail_image_read', '_kui_loader_sd_stream_next',
                   '_kui_sci_sd_acquire', '_kui_sci_sd_release',
                   '_kui_toy_pilot_read_raw', '_kui_toy_pilot_heap_hook',
                   '_kui_toy_pilot_return_hook', '_kui_sci_sd_healthy'):
        code_symbol(resident, symbol, retail_layout.LOW_RESIDENT_ADDRESS)
    for symbol in ('_kui_retail_stage_main', '_kui_retail_stage_relay',
                   '_kui_retail_bootstrap_enter', '_kui_retail_game_resume',
                   '_kui_toy_pilot_stage_install', '_kui_toy_pilot_stage_publish'):
        code_symbol(stage, symbol, retail_layout.STAGE_ADDRESS)
    for name in ('read', 'write', 'copy', 'publish'):
        code_symbol(worker, '_kui_toy_pilot_bus_' + name, WORKER_BASE)
    code_symbol(worker, '_kui_toy_pilot_lease_allocate', WORKER_BASE)
    ring_symbols = {}
    for name in ('_ring_observe', '_ring_write_allowed', '_ring_recover'):
        matches = [symbol for symbol in ws if symbol == name or symbol.startswith(name + '.')]
        if not matches:
            raise ValueError('Missing continuous-ring ownership helper: ' + name)
        for symbol in matches:
            code_symbol(worker, symbol, WORKER_BASE)
            ring_symbols[symbol] = {'address': f'0x{ws[symbol]:08x}', 'bytes': worker['symbol_sizes'][symbol]}
    for address in (0x8c0840d6, 0x8c083fae, 0x8c069c00, 0x8c083d34,
                    0x8c068f20, 0x8c068b28, 0x8c069158, 0x8c0690d4, 0x8c068a04):
        if struct.pack('<I', address) in worker['payload']:
            raise ValueError('Unbounded sound helper literal remains: ' + hex(address))
    for label in (PILOT_LABEL, b'WORDS'):
        if label + b'\0' not in resident['payload']:
            raise ValueError('Final resident lacks numeric report legend')
    if bytes.fromhex(BOOT_SHA256) not in stage['payload'] or bytes.fromhex(DRIVER_SHA256) not in worker['payload']:
        raise ValueError('Exact original Toy executable/driver identity gate is missing')
    from toy_pilot_symbols import generate
    if inputs(directory / 'toy_pilot_resident_symbols.h') != generate(directory / 'resident-sci.elf').encode():
        raise ValueError('Stage addresses differ from actual linked low resident')
    blobs = {}
    for name in ('stage', 'resident-sci', 'worker'):
        data = inputs(directory / (name + '.bin'))
        if name != 'worker':
            data = padded(data)
        expected = images[name]['payload'] if name == 'worker' else padded(images[name]['payload'])
        if data != expected:
            raise ValueError('Binary does not equal admitted ELF load bytes: ' + name)
        blobs[name] = data
    embedded = {}
    for transport in ('scif', 'sci', 'ide', 'scia'):
        prefix = '__retail_resident_' + transport + '_blob_'
        embedded[transport] = embedded_blob(stage, prefix + 'start', prefix + 'end',
            retail_layout.STAGE_ADDRESS, blobs['resident-sci'], 'SCI resident ' + transport)
    if len({(embedded[t]['address'], embedded[t]['bytes']) for t in embedded}) != 1:
        raise ValueError('Legacy transport slots do not alias one SCI resident')
    embedded['worker'] = embedded_blob(stage, '__toy_pilot_worker_blob_start', '__toy_pilot_worker_blob_end',
        retail_layout.STAGE_ADDRESS, blobs['worker'], 'leased worker')
    begin, end = ss.get('__retail_trampoline_start', 0), ss.get('__retail_trampoline_end', 0)
    if begin % 4 or end - begin != retail_layout.TRAMPOLINE_BYTES:
        raise ValueError('Executable-entry trampoline changed')
    trampoline = region(stage['payload'], begin - retail_layout.STAGE_ADDRESS,
        retail_layout.TRAMPOLINE_BYTES, 'shared trampoline')
    if struct.pack('<I', ss['_kui_retail_game_resume'] | 0x20000000) not in trampoline:
        raise ValueError('Trampoline does not target linked uncached game relay')
    if (es.get('__retail_map') != retail_layout.EXEC_ADDRESS + retail_layout.MAP_OFFSET or
            es.get('__retail_stage_blob_start') != retail_layout.EXEC_ADDRESS + retail_layout.STAGE_BLOB_OFFSET or
            es.get('__retail_stage_blob_end') != retail_layout.EXEC_ADDRESS + len(entry['payload']) or
            entry['payload'][retail_layout.STAGE_BLOB_OFFSET:] != blobs['stage'] or
            entry['memory_end'] != retail_layout.EXEC_ADDRESS + len(entry['payload']) or
            region(entry['payload'], retail_layout.HEADER_OFFSET, retail_layout.HEADER_BYTES, 'relocation header') !=
            retail_layout.relocation_header(len(blobs['stage']), low=True)):
        raise ValueError('Entry/stage/relocation geometry changed')
    if any(region(entry['payload'], retail_layout.MAP_OFFSET, retail_layout.MAP_BYTES, 'blank manifest')):
        raise ValueError('Packaged manifest must be blank')
    bridges = audit_shared_bridges(worker, disassemblies['worker'])
    stacks = audit_shared_stacks(directory, resident, worker)
    schedule = audit_shared_schedule(ROOT, worker, disassemblies['worker'])
    async_audio = audit_async_cdda(ROOT, worker, disassemblies['worker'])
    return {'images': {name: {'payload_bytes': len(image['payload']), 'memory_end': f"0x{image['memory_end']:08x}",
                    'unresolved_symbols': 0} for name, image in images.items()},
            'allocated_sections': sections, 'embedded_blobs': embedded,
            'instruction_audit': instructions, 'shared_bridges': bridges, 'stack_audit': stacks,
            'shared_scheduling_audit': schedule,
            'asynchronous_CDDA_audit': async_audio,
            'native_pause_stack_audit': audit_pause_stack(worker),
            'native_scratch_audit': audit_native_scratch(resident, worker),
            'early_cache_policy_audit': shared_early_cache_policy_audit(stage, worker,
                    disassemblies['stage'], disassemblies['worker']),
            'continuous_ring_identity_audit': ring_symbols,
            'worker_export_words': worker['exports'],
            'resident_address': '0x8c004000', 'resident_limit': '0x8c007800',
            'worker_reservation': ['0x8cfd0000', '0x8cfe0000'],
            'native_SDK_scratch_preserved': ['0x8cfe0000', '0x8d000000']}

def source_function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        if end >= len(source):
            raise ValueError('Truncated production launcher function')
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def launcher_gate(bundle, package):
    """Use the exact launcher's source gate on valid and malformed envelopes."""
    if sha(bundle) != LAUNCHER_BUNDLE_SHA:
        raise ValueError('Launcher bundle differs from the retained reviewed delivery')
    with zipfile.ZipFile(io.BytesIO(bundle)) as old:
        launcher = old.read(LAUNCHER_FILE)
        source_tar = old.read('source-snapshot.tar')
        licenses = {name: old.read(name) for name in old.namelist() if name.startswith('LICENSES/')}
        launcher_evidence = {name: old.read(name) for name in
            ('evidence/launcher.elf', 'evidence/launcher.map', 'evidence/launcher.compile.json')}
    if sha(launcher) != LAUNCHER_SHA or verify(launcher)['build'] != '6c4a9915ba33':
        raise ValueError('Trusted launcher byte identity changed')
    with tarfile.open(fileobj=io.BytesIO(source_tar)) as archive:
        member = archive.extractfile('src/apps/games_retail.c')
        if member is None:
            raise ValueError('Retained launcher lacks its actual C source')
        source = member.read().decode()
        headers = {}
        for item in archive.getmembers():
            if item.isfile() and item.name.startswith('include/'):
                archive_name(item.name)
                stream = archive.extractfile(item)
                if stream is None or item.size > 1024 * 1024:
                    raise ValueError('Invalid launcher header')
                headers[item.name] = stream.read()
    prelude = '#include <stdint.h>\n#include <stdbool.h>\n#include <string.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include "kui/retail_loader_layout.h"\n#include "kui/retail_image.h"\nstruct kui_runtime_info {uint32_t payload_bytes,memory_bytes;};\nstruct kui_runtime_image {void *data;struct kui_runtime_info info;};\n'
    main = '''
int main(int argc,char **argv) {
 for(int i=1;i<argc;i++) {
  FILE *f=fopen(argv[i],"rb");if(!f)return 2;
  if(fseek(f,0,SEEK_END))return 2;
  long n=ftell(f);
  if(n<64 || fseek(f,0,SEEK_SET))return 2;
  uint8_t *bytes=malloc((size_t)n);if(!bytes)return 2;
  if(fread(bytes,1,(size_t)n,f)!=(size_t)n || fclose(f))return 2;
  struct kui_runtime_image image={.data=bytes+64};
  image.info.payload_bytes=le32(bytes+16);image.info.memory_bytes=le32(bytes+28);
  if(image.info.payload_bytes!=(uint32_t)n-64u)return 2;
  bool expected=strstr(argv[i],"reject-")==NULL;
  if(layout(&image,false)!=expected)return 1;
  free(bytes);
 }return 0;
}
'''
    c = prelude + source_function(source, 'static uint32_t le32(') + source_function(source, 'static bool layout(') + main
    with tempfile.TemporaryDirectory(prefix='kui-shared-launch-gate-') as temporary:
        folder = Path(temporary)
        for name, data in headers.items():
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (folder / 'gate.c').write_text(c)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
            '-fno-pie', '-no-pie', '-fsanitize=address,undefined', '-I', str(folder / 'include'),
            str(folder / 'gate.c'), '-o', str(folder / 'gate')], check=True)
        info = verify(package)
        tests = {'candidate.kui': package, 'reject-launcher.kui': launcher}
        payload = bytearray(package[64:])
        header = 0x100
        stage_bytes = struct.unpack_from('<I', payload, header + 28)[0]
        for name, offset, value in (('stage-length', header + 28, stage_bytes - 4),
                ('stage-alignment', header + 28, stage_bytes + 1),
                ('resident', header + 52, 0x8c004004), ('limit', header + 56, 0x8c007804),
                ('flags', header + 60, 1), ('stage-address', header + 24, 0x8ce10000),
                ('manifest-size', header + 20, 8192)):
            malformed = payload.copy()
            struct.pack_into('<I', malformed, offset, value)
            tests['reject-' + name + '.kui'] = envelope(malformed, len(malformed), info['build'])
        for name, offset in (('magic', header), ('manifest', 0x1000)):
            malformed = payload.copy()
            malformed[offset] ^= 1
            tests['reject-' + name + '.kui'] = envelope(malformed, len(malformed), info['build'])
        tests['reject-memory.kui'] = envelope(payload, len(payload) + 4, info['build'])
        paths = []
        for name, data in tests.items():
            verify(data)
            path = folder / name
            path.write_bytes(data)
            paths.append(str(path))
        subprocess.run([str(folder / 'gate'), *paths], check=True,
            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
    return launcher, licenses, launcher_evidence, source_tar, {
        'production_source_sha256': sha(source.encode()), 'source_archive_sha256': sha(source_tar),
        'accepted_candidate': True, 'rejected_malformed_or_wrong_file_cases': len(tests) - 1,
        'checked_with': 'actual retained launcher layout C function, ASan/UBSan'}


def collect(commit, build_dir, launcher_bundle):
    if git('status', '--porcelain') or git('rev-parse', 'HEAD') != commit:
        raise ValueError('Commit exact reviewed source before packaging')
    tree = git('rev-parse', 'HEAD^{tree}')
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    for name in filter(None, tracked):
        archive_name(name)
    build = (ROOT / build_dir).resolve()
    if not build.is_relative_to(ROOT) or build == ROOT:
        raise ValueError('Build directory must be separate and inside this source checkout')
    files, remembered = {}, {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate ZIP member: ' + name)
        files[name] = data

    def remember(path):
        data = read_file(path)
        digest = sha(data)
        if path in remembered and remembered[path] != digest:
            raise ValueError('Build input changed during collection')
        remembered[path] = digest
        return data

    config_bytes = remember(build / 'build-config')
    config = build_config(build / 'build-config')
    required = {'BUILD': commit[:12], 'PROFILE': '15', 'PILOT': '1', 'LOW': '1',
        'SLOTS': '64', 'SCI': '1', 'SCI_PACED': '1', 'SCI_FAULT': '1',
        'SCI_REUSE_TDRE': '0', 'GD_FIXED_STEP': '3', 'SHARED_SCI': '1', 'ASYNC_CDDA': '1',
        'OPT': '-Os -fno-tree-scev-cprop'}
    if config != required:
        raise ValueError('Complete build configuration differs from shared SCI candidate')
    ring = ring_contract(remember(ROOT / 'include/kui/toy_pilot_ring.h'))
    pilot = pilot_contract(remember(ROOT / 'include/kui/toy_pilot.h'))
    legend = shared_report_contract()
    runtime = remember(build / 'retail-toy-pilot.kui')
    info = inspect_retail(runtime)
    payload, memory = flatten_elf(remember(build / 'entry.elf'))
    if (info['build'] != commit[:12] or runtime[64:] != payload or info['memory_bytes'] != memory or
            commit[:12].encode() + b'\0' not in payload or PILOT_LABEL + b'\0' not in payload):
        raise ValueError('Candidate source/envelope/ELF/report identity mismatch')
    linked = shared_layout(build, remember)
    bundle_bytes = launcher_bundle.read_bytes()
    launcher, licenses, launcher_evidence, launcher_source, gate = launcher_gate(bundle_bytes, runtime)
    add(RUNTIME_FILE, runtime)
    add(LAUNCHER_FILE, launcher)
    add('evidence/launcher-source-snapshot.tar', launcher_source)
    for name, data in launcher_evidence.items():
        add(name, data)
    add('evidence/toy-shared-sci/build-config', config_bytes)
    add('evidence/toy-shared-sci/toy_pilot_resident_symbols.h', remember(build / 'toy_pilot_resident_symbols.h'))
    evidence = {}
    for path in sorted(build.rglob('*')):
        if not path.is_file() or path.suffix not in ('.elf', '.map', '.su'):
            continue
        data = remember(path)
        if not data:
            raise ValueError('Empty build evidence')
        name = path.relative_to(build).as_posix()
        evidence[name] = sha(data)
        add('evidence/toy-shared-sci/' + name, data)
    for name in ('entry', 'stage', 'resident-sci', 'worker'):
        if not {name + '.elf', name + '.map'} <= set(evidence):
            raise ValueError('Missing actual linked ELF/map evidence')
    for name, data in licenses.items():
        add(name, data)
    for path in sorted((ROOT / 'LICENSES').rglob('*')):
        if path.is_file():
            name = 'LICENSES/' + path.relative_to(ROOT / 'LICENSES').as_posix()
            data = read_file(path)
            if name in files and files[name] != data:
                raise ValueError('License notice conflict: ' + name)
            if name not in files:
                add(name, data)
    add('LICENSE', read_file(ROOT / 'LICENSE'))
    add('THIRD_PARTY.md', read_file(ROOT / 'THIRD_PARTY.md'))
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', commit], cwd=ROOT)
    add('source-snapshot.tar', snapshot)
    add('source-state.txt', ('Unpublished local source checkpoint; complete source in source-snapshot.tar.\n'
        'Commit: ' + commit + '\nTree: ' + tree + '\n').encode())
    readme = remember(ROOT / README_SOURCE)
    if b'TOY_SHARED_SCI_CHECKLIST_DRAFT' in readme:
        raise ValueError('Freeze checklist against final candidate before packaging')
    add('README.md', readme)
    tests_log = remember(ROOT / 'docs/evidence/toy-shared-sci-host-tests.txt')
    from test_toy_pilot import SUITES
    required_audio_suites = {'async-audio-gd', 'async-audio-engine',
                            'async-audio-worker', 'async-audio-stream'}
    if not required_audio_suites <= {name for name, _, _ in SUITES}:
        raise ValueError('Regression runner omits required async audio production suites')
    suite_count = len(SUITES) + 4
    footer = f'{suite_count} Toy pilot regression suites passed'.encode()
    if (not tests_log.rstrip().endswith(footer) or
            b'FAIL' in tests_log):
        raise ValueError('Missing successful recorded host checks')
    add('evidence/toy-shared-sci-host-tests.txt', tests_log)
    if b'foreground high EXEC/CHECK, nonblocking REQUEST' not in tests_log:
        raise ValueError('Missing production GD adapter foreground/nonblocking routing checks')
    for phrase in (b'Shared asynchronous SCI audio:', b'Async CDDA worker:',
            b'Toy async audio GD adapter: scalar/audio EXEC/CHECK services RAW without a data handle'):
        if phrase not in tests_log:
            raise ValueError('Missing recorded complete-sector async audio production checks')
    console_evidence = remember(ROOT / CONSOLE_EVIDENCE)
    if b'2faa62067610' not in console_evidence or b'369,263' not in console_evidence:
        raise ValueError('Missing recorded asynchronous console regression evidence')
    add('evidence/toy-shared-sci-console-2026-10-09.md', console_evidence)
    topup_evidence = remember(ROOT / TOPUP_EVIDENCE)
    if b'dc455cbfa50b' not in topup_evidence or b'4.42330 ms' not in topup_evidence:
        raise ValueError('Missing recorded foreground top-up console evidence')
    add('evidence/toy-shared-sci-topup-console-2026-10-09.md', topup_evidence)
    metadata = {'source_commit': commit, 'source_tree': tree, 'source_dirty': False,
        'source_publication': 'unpublished local checkpoint; complete source snapshot included',
        'source_snapshot_sha256': sha(snapshot), 'build_configuration': config,
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'scope': 'Exact Toy Commander asynchronous raw CDDA/shared SCI experiment with bounded foreground progress',
        'hardware_tested': False,
        'candidate': {**info, 'file': RUNTIME_FILE, 'sha256': sha(runtime)},
        'launcher': {**verify(launcher), 'file': LAUNCHER_FILE, 'sha256': sha(launcher),
            'unchanged_from_retained_delivery': True, 'bundle_sha256': LAUNCHER_BUNDLE_SHA},
        'launcher_layout_gate': gate, 'linked_layout': linked, 'evidence_sha256': evidence,
        'ring_constants': ring, 'pilot_constants': pilot, 'snapshot_API': 8,
        'host_regression_suites': suite_count,
        'snapshot_prefix_bytes': 448, 'worker_config_bytes': 68,
        'report_word_order': legend,
        'preceding_hardware_test': {'build': 'dc455cbfa50b',
            'outcome': 'User reported video restored to GD3 performance; no improvement over GD3 was established',
            'evidence': 'evidence/toy-shared-sci-topup-console-2026-10-09.md',
            'evidence_sha256': sha(topup_evidence),
            'new_candidate_tested': False},
        'raw_audio_delivery': {'asynchronous': True, 'sector_bytes': 2352,
            'pending_output': 'private staging only; no partial worker buffer delivery',
            'identity': ['LBA', 'mailbox generation', 'stable worker output address'],
            'raw_timing_scope': 'Elapsed request-to-complete-delivery latency including game execution between visits; not CPU blocking time',
            'sound_ring_policy_changed': False, 'worker_refill_cadence_changed': False},
        'clean_cdda_fallback': {'build': '7b55156aafa2',
            'package': 'K-UI-CDDA-Eight-Block-Test.zip',
            'sha256': 'ffd6628beaf4c964111598d1e19d902363b387c74b4b018ae11e9afeb43e2aae',
            'included_or_overwritten': False},
        'ordinary_reader_modified': False, 'game_files_included': False,
        'card_path_configuration_included': False, 'package_revision': 3}
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if {name for name in files if name.endswith('.kui')} != {RUNTIME_FILE, LAUNCHER_FILE}:
        raise ValueError('Unexpected installable runtime')
    if any(Path(name).suffix.lower() == '.drv' for name in files) or any(
            sha(data) in (BOOT_SHA256, DRIVER_SHA256) for data in files.values()):
        raise ValueError('Retail executable/driver included as archive member')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD') != commit or
            git('rev-parse', 'HEAD^{tree}') != tree or launcher_bundle.read_bytes() != bundle_bytes):
        raise ValueError('Source or retained launcher changed during collection')
    for path, digest in remembered.items():
        if sha(read_file(path)) != digest:
            raise ValueError('Build input changed during collection')
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n' for name, data in sorted(files.items())).encode())
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--source-commit', required=True, type=object_id)
    parser.add_argument('--build-dir', default='build/shared-sci-async-cdda', type=Path)
    parser.add_argument('--launcher-bundle', required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.output.is_symlink() or args.output.exists() or args.output.name != OUTPUT_NAME:
            raise ValueError('Use a new ' + OUTPUT_NAME + ' path; preserve previous artifacts')
        write_archive(args.output.resolve(), collect(args.source_commit, args.build_dir, args.launcher_bundle))
        print(json.dumps({'file': str(args.output.resolve()), 'bytes': args.output.stat().st_size,
            'sha256': sha(args.output.read_bytes()), 'build': args.source_commit[:12]}, indent=2))
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
