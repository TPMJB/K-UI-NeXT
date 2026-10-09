#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate fail-closed linked admission for the cooked DATA diagnostic.

The original T gate remains unchanged. This gate retains its cache/audio,
bridge, export, scalar-body and passive timer/PVR proofs, then independently
admits the temporary DATA callback instrumentation and appended report.
An empty identity table or changed instruction is a hard refusal.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

import toy_loader_trace_audit as trace
from check_loader_layout import inspect_elf, EH, SH, SYM, region
from check_retail_instructions import audit as instruction_audit
from package_cdda_toy_pilot import (linked_symbol_sizes, pure_worker_leaf_audit,
                                   scalar_worker_adapter_audit, stack_rows)
from toy_pilot_cache_audit import Linked, audit_cache_layout

ROOT = Path(__file__).resolve().parents[1]
LOW, LOW_LIMIT = trace.LOW, trace.LOW_LIMIT
HIGH, HIGH_LIMIT = trace.HIGH, trace.HIGH_LIMIT
STAGE, STAGE_LIMIT, P2 = trace.STAGE, trace.STAGE_LIMIT, trace.P2
REQUIRED_CONFIG = {**trace.REQUIRED_CONFIG, 'DATA_PROBE': '1'}
CHANGED_FUNCTIONS = {
    '_kui_toy_pilot_gd_dispatch',
    '_kui_toy_loader_trace_report_capture',
    '_kui_toy_loader_trace_report_page',
    '_kui_toy_loader_trace_terminal',
}
# Fill only after independent review of the complete final source and ELF.
# Never derive acceptance identities automatically from the candidate binary.
REVIEWED_DATA = {}
REVIEWED_REPORT = {}
PROBE_MMIO_READS = {at: width for at, width in trace.MMIO_READS.items()
                    if at >= 0xFFC00000}
_signed = trace._signed
exact_function = trace.exact_function

TRACE_OBJECTS = {'_report': 1664, '_visit': 108, '_request': 96,
                 '_trace_words': 1664, '_pilot_words': 512,
                 '_page': 4, '_stopped_display': 56, '_row': 4}
DATA_OBJECTS = {'_data_probe_report': 768, '_data_probe_scope': 36,
                '_data_probe_read_state': 88}


def audit_state(worker):
    # One ownership pass is essential: new DATA objects must not overlap
    # retained TRACE/PILOT objects even if both sets individually fit BSS.
    combined = trace.owned_bss(worker, {**TRACE_OBJECTS, **DATA_OBJECTS})
    return ({name: combined[name] for name in TRACE_OBJECTS},
            {name: combined[name] for name in DATA_OBJECTS})


def read_config(directory):
    config = {}
    for line in (Path(directory) / 'build-config').read_text().splitlines():
        if '=' not in line:
            raise ValueError('Malformed DATA probe build configuration')
        key, value = line.split('=', 1)
        if key in config:
            raise ValueError('Duplicate DATA probe build configuration: ' + key)
        config[key] = value
    if any(config.get(key) != value for key, value in REQUIRED_CONFIG.items()):
        raise ValueError('DATA probe requires exact R plus LOADER_TRACE=1 and DATA_PROBE=1')
    if set(config) != set(REQUIRED_CONFIG) | {'BUILD'}:
        raise ValueError('DATA probe configuration contains an unreviewed key')
    if not re.fullmatch(r'[0-9a-f]{12}', config.get('BUILD', '')):
        raise ValueError('DATA probe requires a twelve-digit hexadecimal build identity')
    return config


def load_profile(directory):
    directory = Path(directory).resolve()
    config = read_config(directory)
    objdump = shutil.which('sh-elf-objdump')
    if objdump is None:
        raise ValueError('SH objdump is required for linked DATA probe admission')
    images, disassemblies = {}, {}
    for name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                              ('worker', HIGH, HIGH_LIMIT), ('stage', STAGE, STAGE_LIMIT)):
        path = directory / (name + '.elf')
        raw = path.read_bytes()
        options = {'private_p2': True} if name != 'stage' else {}
        if name == 'worker':
            options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
        image = inspect_elf(raw, base, limit, **options)
        image['symbol_sizes'] = linked_symbol_sizes(raw)
        if (directory / (name + '.bin')).read_bytes() != image['payload']:
            raise ValueError(name + '.bin differs from actual ELF load bytes')
        images[name] = image
        disassemblies[name] = subprocess.check_output([objdump, '-d', str(path)], text=True)
    return config, images, disassemblies


def function_symbols(raw):
    """Read STT_FUNC identities from the already admitted actual ELF."""
    header = EH.unpack_from(raw)
    sections = [SH.unpack_from(raw, header[6] + i * header[11]) for i in range(header[12])]
    result = set()
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = region(raw, strings[4], strings[5], 'function symbol names')
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, _, size, info, _, _ = SYM.unpack_from(raw, offset)
            if name and size and info & 15 == 2:
                end = names.find(b'\0', name)
                if end < 0:
                    raise ValueError('Unterminated admitted function symbol')
                result.add(names[name:end].decode('ascii'))
    return result


def audit_retained(directory, config, images, dis, baseline):
    """Compare actual new images with independently admitted exact T."""
    baseline = Path(baseline).resolve()
    old_config, old_images, old_dis = trace.load_profile(baseline)
    trace.audit_loader_trace(baseline)
    old_low, low = old_images['resident-sci'], images['resident-sci']
    old_id, new_id = old_config['BUILD'].encode(), config['BUILD'].encode()
    if old_low['payload'].count(old_id) != 1 or low['payload'].count(new_id) != 1:
        raise ValueError('Low build identity is not the exact single reviewed string')
    canonical = low['payload'].replace(new_id, old_id)
    if canonical != old_low['payload'] or low['memory_end'] != old_low['memory_end']:
        raise ValueError('DATA probe changed retained low bytes/reservations')
    if low['symbols'] != old_low['symbols'] or low['symbol_sizes'] != old_low['symbol_sizes']:
        raise ValueError('DATA probe changed retained low linked symbols or object sizes')
    old_code = Linked(old_images['worker'], HIGH, old_dis['worker'])
    code = Linked(images['worker'], HIGH, dis['worker'])
    old_names = function_symbols((baseline / 'worker.elf').read_bytes())
    names = old_names - CHANGED_FUNCTIONS
    identities = {}
    for name in sorted(names):
        before = trace.normalized_function(old_code, name)
        after = trace.normalized_function(code, name)
        if before != after:
            raise ValueError('DATA probe changed retained worker/audio instructions: ' + name)
        identities[name] = after
    new_names = function_symbols((Path(directory) / 'worker.elf').read_bytes()) - old_names
    if new_names != set(REVIEWED_DATA):
        raise ValueError('DATA probe added an unreviewed or missing linked function')
    return {'low_binary_identical_except_build_string': True,
            'low_canonical_sha256': hashlib.sha256(canonical).hexdigest(),
            'low_symbols_and_reservations_identical': True,
            'retained_worker_function_identities': identities,
            'baseline_build': old_config['BUILD'], 'retained_audio_instructions_identical': True}


def audit_probe_graph(code, low):
    """T's constant/control-flow walk, scoped to reviewed probe functions.

    Exact instruction identities close unknown pointer-derived RAM writes.
    This walk independently rejects constant MMIO writes, wrong widths,
    unknown callbacks, and branches into literals. Only the two unchanged
    exact low DATA callbacks are admitted as external call targets.
    """
    graph, reads = {}, set()
    allowed = {code.symbols[name]: name for name in REVIEWED_DATA}
    primitives = {code.symbols[name]: name for name in ('_memcpy', '_memset')}
    low_callbacks = {low['symbols'][name]: name for name in ('_read_sectors', '_transfer_block')}
    allowed.update(low_callbacks)
    allowed.update(primitives)
    for name in (*tuple(REVIEWED_DATA), '_memcpy', '_memset'):
        begin, end, rows = exact_function(code, name)
        rowmap = {row[0]: row for row in rows}
        anchor, stack_low, stack_cells = 0xACFDE000, 0xACFDDC00, 272
        entry_state = [None] * (16 + stack_cells)
        entry_state[15] = anchor
        incoming, pending, transfers = {begin: tuple(entry_state)}, [begin], set()

        def merge(at, state):
            if at not in rowmap:
                raise ValueError('Probe control flow escapes executable function: ' + name)
            old = incoming.get(at)
            if old is None:
                incoming[at] = tuple(state)
                pending.append(at)
            else:
                joined = tuple(a if a == b else None for a, b in zip(old, state))
                if joined != old:
                    incoming[at] = joined
                    pending.append(at)

        def effect(at, state):
            _, opcode, mnemonic, operands = rowmap[at]
            n, m = opcode >> 8 & 15, opcode >> 4 & 15
            operands = operands.split('!', 1)[0].strip()
            address, width, store = None, None, False
            if opcode & 0xF00F in (0x6000, 0x6001, 0x6002):
                address, width = state[m], (1, 2, 4)[opcode & 15]
            elif opcode & 0xF00F in (0x6004, 0x6005, 0x6006):
                address, width = state[m], (1, 2, 4)[(opcode & 15) - 4]
            elif opcode & 0xF000 == 0x5000:
                address = None if state[m] is None else (state[m] + (opcode & 15) * 4) & 0xFFFFFFFF
                width = 4
            elif opcode & 0xF00F in (0x2000, 0x2001, 0x2002, 0x2004, 0x2005, 0x2006):
                width, store = (1, 2, 4)[(opcode & 15) % 4], True
                address = state[n]
                if opcode & 4 and address is not None:
                    address = (address - width) & 0xFFFFFFFF
            elif opcode & 0xF000 == 0x1000:
                width, store = 4, True
                address = None if state[n] is None else (state[n] + (opcode & 15) * 4) & 0xFFFFFFFF
            if address is not None and not 0x8C000000 <= address < 0x8D000000 and not 0xAC000000 <= address < 0xAD000000:
                if store:
                    raise ValueError('Probe wrote outside owned/guest main RAM: ' + hex(address))
                if PROBE_MMIO_READS.get(address) != width:
                    raise ValueError('Probe read an unreviewed MMIO address/width: ' + hex(address))
                reads.add((address, width))
            stack_index = (16 + (address - stack_low) // 4
                           if address is not None and address % 4 == 0 and
                           stack_low <= address < stack_low + stack_cells * 4 else None)
            loaded = state[stack_index] if stack_index is not None and width == 4 else None
            if store and stack_index is not None:
                state[stack_index] = state[m] if width == 4 else None
            if opcode & 0xF00F in (0x2004, 0x2005, 0x2006):
                state[n] = address
            if opcode & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
                width = 4 if opcode & 0xF000 == 0xD000 else 2
                value = code.literal(at, n, width)[1]
                state[n] = (value if width == 4 else _signed(value, 16)) & 0xFFFFFFFF
            elif opcode & 0xF00F == 0x6003:
                state[n] = state[m]
            elif opcode & 0xF000 == 0xE000:
                state[n] = _signed(opcode & 255, 8) & 0xFFFFFFFF
            elif opcode & 0xF000 == 0x7000:
                state[n] = None if state[n] is None else (state[n] + _signed(opcode & 255, 8)) & 0xFFFFFFFF
            elif opcode & 0xF00F == 0x300C:
                state[n] = None if state[n] is None or state[m] is None else (state[n] + state[m]) & 0xFFFFFFFF
            elif mnemonic.startswith('mov.') and width is not None and not store:
                state[n] = loaded
            elif mnemonic not in ('cmp/eq', 'cmp/hs', 'cmp/hi', 'cmp/pl', 'cmp/pz', 'tst',
                                    'bt', 'bf', 'bt.s', 'bf.s', 'bra', 'bsr', 'jsr', 'jmp', 'rts', 'nop'):
                dest = re.search(r'(?:^|,)r(\d+)$', operands)
                if dest:
                    state[int(dest[1])] = None
            if '@r' in operands and '+' in operands:
                source = re.search(r'@r(\d+)\+', operands)
                if source:
                    source = int(source[1])
                    if not (mnemonic.startswith('mov.') and source == n):
                        state[source] = None if state[source] is None else (state[source] + (width or 4)) & 0xFFFFFFFF
            if mnemonic in ('sts.l', 'stc.l') and '@-r15' in operands:
                state[15] = None if state[15] is None else state[15] - 4
            return state

        while pending:
            at = pending.pop()
            state = list(incoming[at])
            _, opcode, mnemonic, _ = rowmap[at]
            if mnemonic in ('rte', 'trapa', 'bsrf', 'braf', 'jsr/n', 'jmp/n'):
                raise ValueError('Unreviewed probe transfer: ' + name)
            delayed = (opcode & 0xF000 in (0xA000, 0xB000) or
                       opcode & 0xFF00 in (0x8D00, 0x8F00) or
                       opcode & 0xF0FF in (0x400B, 0x402B) or opcode == 0x000B)
            if delayed:
                register = opcode >> 8 & 15
                target = state[register] if opcode & 0xF0FF in (0x400B, 0x402B) else None
                if at + 2 not in rowmap:
                    raise ValueError('Probe transfer has no executable delay slot')
                state = effect(at + 2, state)
                if opcode == 0x000B:
                    continue
                if opcode & 0xF000 in (0xA000, 0xB000):
                    target = at + 4 + _signed(opcode & 0xFFF, 12) * 2
                if opcode & 0xF000 == 0xB000 or opcode & 0xF0FF in (0x400B, 0x402B):
                    if target not in allowed:
                        raise ValueError('Probe gained an unknown/out-of-scope callback in ' + name +
                                         ' at ' + hex(at) + ': ' + str(target))
                    transfers.add(allowed[target])
                    if opcode & 0xF0FF == 0x402B:
                        continue
                    state[:8] = [None] * 8
                    merge(at + 4, state)
                elif opcode & 0xF000 == 0xA000:
                    merge(target, state)
                else:
                    merge(at + 4 + _signed(opcode & 255, 8) * 2, state)
                    merge(at + 4, state)
            elif opcode & 0xFF00 in (0x8900, 0x8B00):
                merge(at + 4 + _signed(opcode & 255, 8) * 2, state)
                merge(at + 2, state)
            else:
                merge(at + 2, effect(at, state))
        graph[name] = sorted(transfers)
    if reads != set(PROBE_MMIO_READS.items()):
        raise ValueError('DATA probe must retain the five exact passive clock reads')
    return {'closed_functions': graph, 'MMIO_reads': [{'address': hex(at), 'bytes': size}
            for at, size in sorted(reads)], 'MMIO_writes': False,
            'timer_configuration_writes': False,
            'low_storage_callbacks': sorted(low_callbacks.values()),
            'audio_callbacks': False, 'unknown_indirect_transfers': False}


def audit_adapter(worker, disassembly):
    projected = copy.deepcopy(worker)
    body = '_dispatch_body'
    if body not in worker['symbols']:
        raise ValueError('DATA probe requires separately bounded original scalar adapter body')
    projected['symbols']['_kui_toy_pilot_gd_dispatch'] = worker['symbols'][body]
    projected['symbol_sizes']['_kui_toy_pilot_gd_dispatch'] = worker['symbol_sizes'][body]
    scalar = scalar_worker_adapter_audit(projected, disassembly)
    pure = pure_worker_leaf_audit(worker, disassembly)
    code = Linked(worker, HIGH, disassembly)
    begin, end, rows = exact_function(code, '_kui_toy_pilot_gd_dispatch')
    observed = []
    for index, (at, opcode, mnemonic, _) in enumerate(rows):
        if mnemonic != 'jsr':
            continue
        register, target = opcode >> 8 & 15, None
        for prev in reversed(rows[max(0, index - 12):index]):
            if prev[1] & 0xFF00 == 0xD000 | register << 8:
                target = code.literal(prev[0], register)[1]
                break
        observed.append(target)
    order = ('_kui_toy_loader_trace_begin', '_kui_toy_loader_trace_words',
             '_kui_toy_loader_data_probe_begin', body,
             '_kui_toy_loader_data_probe_end', '_kui_toy_loader_trace_end')
    if observed != [worker['symbols'][name] for name in order] or end - begin > 192:
        raise ValueError('DATA adapter is not the bounded six-call thin wrapper')
    return {'scalar_body': scalar, 'pure_leaves': pure, 'wrapper_call_order': list(order),
            'wrapper_bytes': end - begin, 'original_result_preserved': True}


def audit_probe_bindings(directory, low, code):
    """Verify generated typed ABI and linked direct low callback constants."""
    from toy_pilot_symbols import generate
    expected = generate(Path(directory) / 'resident-sci.elf', private_p2=True, data_probe=True)
    if (Path(directory) / 'toy_pilot_resident_symbols.h').read_text() != expected:
        raise ValueError('DATA generated addresses differ from exact linked typed low objects')
    addresses = {name: low['symbols'][name] for name in
                 ('_card', '_kui_retail_hook_sr', '_kui_retail_native_caller',
                  '_read_sectors', '_transfer_block')}
    values = set()
    for name in REVIEWED_DATA:
        for at, opcode, mnemonic, _ in exact_function(code, name)[2]:
            if opcode & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
                width = 4 if opcode & 0xF000 == 0xD000 else 2
                values.add(code.literal(at, opcode >> 8 & 15, width)[1])
    for name in ('_kui_retail_hook_sr', '_read_sectors', '_transfer_block'):
        if addresses[name] not in values:
            raise ValueError('DATA probe lost exact linked low binding: ' + name)
    # Initial reviewed compiler shape shares SR and caller-PR anchor.
    # A changed shape refuses here until its actual read binding is reviewed.
    context = code.find('_kui_toy_loader_data_probe_begin',
                        (None, 0xE501, 0x6012, 0x1D04, 0x5115, 0x1D15))
    if (code.literal(context, 1)[1] != addresses['_kui_retail_hook_sr'] or
            addresses['_kui_retail_hook_sr'] + 20 != addresses['_kui_retail_native_caller']):
        raise ValueError('DATA saved SR/caller PR read binding changed')
    if not ({addresses['_card'], addresses['_card'] + 4,
             addresses['_card'] + 28} & values):
        raise ValueError('DATA probe lost exact low card/SD callback binding')
    return {'typed_generated_header_exact': True,
            'low_addresses': {name: hex(at) for name, at in addresses.items()},
            'storage_bytes': 72, 'SD_offset': 4, 'SD_bytes': 44,
            'SD_transfer_block_offset': 28, 'original_SR_read_only': True,
            'original_caller_PR_read_only': True, 'cooked_DATA_only': True,
            'low_read_and_block_callbacks_direct_and_exact': True}


def audit_probe_stacks(directory, low, worker, disassembly, baseline=None):
    directory = Path(directory).resolve()
    rows = stack_rows([directory / 'worker/src/loader/toy_loader_data_probe.su'])
    emitted = {name[1:] for name in REVIEWED_DATA}
    unlinked = {'kui_toy_loader_data_probe_word_count'}
    if {row['function'] for row in rows} != emitted | unlinked:
        raise ValueError('Missing or unreviewed DATA static stack evidence')
    code = Linked(worker, HIGH, disassembly)
    for row in rows:
        if row['function'] in unlinked:
            if row['bytes'] or '_' + row['function'] in worker['symbols']:
                raise ValueError('Unexpected linked word-count accessor or nonzero dropped frame')
            continue
        instructions = exact_function(code, '_' + row['function'])[2]
        frame, stop_after = 0, None
        for index, (_, opcode, mnemonic, _) in enumerate(instructions):
            if stop_after is not None and index > stop_after:
                break
            if opcode & 0xFF0F == 0x2F06 or opcode == 0x4F22:
                frame += 4
            elif opcode & 0xFF00 == 0x7F00:
                frame -= _signed(opcode & 255, 8)
            if stop_after is None and mnemonic in ('bt', 'bf', 'bt.s', 'bf.s', 'bra', 'bsr', 'jsr', 'jmp', 'rts'):
                stop_after = index + int(mnemonic in ('bt.s', 'bf.s', 'bra', 'bsr', 'jsr', 'jmp', 'rts'))
        if row['bytes'] != frame:
            raise ValueError('DATA probe .su differs from actual linked frame: ' + row['function'])
    proof = trace.audit_stacks(directory, low, worker, disassembly)
    proof['DATA_probe_emitted_functions'] = len(rows) - len(unlinked)
    proof['DATA_probe_actual_frames_verified'] = True
    return proof


def audit_data_probe(builddir, baseline=None):
    directory = Path(builddir).resolve()
    baseline = baseline or ROOT / 'build/recovered-t-check'
    if not REVIEWED_DATA or set(REVIEWED_REPORT) != CHANGED_FUNCTIONS:
        raise ValueError('Missing independently reviewed DATA/report identities')
    config, images, dis = load_profile(directory)
    low, worker, stage = (images[name] for name in ('resident-sci', 'worker', 'stage'))
    for name in images:
        try:
            instruction_audit(name, dis[name])
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
    exports = trace.audit_exports(low, worker)
    code = Linked(worker, HIGH, dis['worker'])
    retained = audit_retained(directory, config, images, dis, baseline)
    unchanged_trace = {name: identity for name, identity in trace.REVIEWED_TRACE.items()
                       if name not in CHANGED_FUNCTIONS}
    trace_hashes = trace.reviewed_identities(code, unchanged_trace, 'retained trace')
    trace_state, state = audit_state(worker)
    if (worker['symbol_sizes'].get('_ceilings.0') != 28 or
            code.data(worker['symbols'].get('_ceilings.0', 0), 28) !=
            struct.pack('<7I', 782, 1563, 3125, 6250, 12500, 25000, 50000)):
        raise ValueError('Retained trace histogram thresholds changed in DATA probe')
    trace_graph = trace.audit_trace_graph(code)
    data_hashes = trace.reviewed_identities(code, REVIEWED_DATA, 'DATA probe')
    report_hashes = trace.reviewed_identities(code, REVIEWED_REPORT, 'DATA wrapper/report')
    probe_graph = audit_probe_graph(code, low)
    bindings = audit_probe_bindings(directory, low, code)
    cache = audit_cache_layout(low, worker, stage, dis['resident-sci'], dis['worker'], dis['stage'])
    return {'profile': 'toy-loader-data-probe-R', 'build': config['BUILD'],
            'exports': exports, 'retained': retained,
            'retained_trace': {'report_words': 416, 'passive_callgraph': trace_graph,
                               'state': trace_state,
                               'reviewed_linked_identities': trace_hashes},
            'DATA_probe': {'report_words': 192, 'phase_words': 88, 'version': 1,
                           'report_magic': '0x4c445031', 'tick_hz': 781250,
                           'reviewed_linked_identities': data_hashes,
                           'report_linked_identities': report_hashes,
                           'passive_callgraph': probe_graph, 'bindings': bindings,
                           'state': state},
            'bridges': trace.audit_bridges(worker, dis['worker']),
            'publication': trace.audit_boot_publication(low, worker, stage, dis['resident-sci'], dis['stage']),
            'stage_linked_identities': trace.reviewed_identities(Linked(stage, STAGE, dis['stage']), trace.REVIEWED_STAGE, 'stage publication'),
            'adapter': audit_adapter(worker, dis['worker']),
            'stacks': audit_probe_stacks(directory, low, worker, dis['worker']),
            'retained_cache_audio_heap_proofs': cache,
            'hardware_behavior_verified': False, 'audio_or_video_improvement_proven': False}


def review_candidate(builddir, baseline=None):
    """Print candidate evidence only; this function never grants admission."""
    directory = Path(builddir).resolve()
    baseline = Path(baseline or ROOT / 'build/recovered-t-check').resolve()
    config, images, dis = load_profile(directory)
    low, worker = images['resident-sci'], images['worker']
    code = Linked(worker, HIGH, dis['worker'])
    original = function_symbols((baseline / 'worker.elf').read_bytes())
    names = (function_symbols((directory / 'worker.elf').read_bytes()) - original) | CHANGED_FUNCTIONS
    functions = {}
    for name in sorted(names):
        begin, end, rows = exact_function(code, name)
        literals = []
        for at, opcode, mnemonic, _ in rows:
            if opcode & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
                width = 4 if opcode & 0xF000 == 0xD000 else 2
                address, value = code.literal(at, opcode >> 8 & 15, width)
                literals.append({'instruction': hex(at), 'address': hex(address),
                                 'width': width, 'value': hex(value)})
        functions[name] = {'begin': hex(begin), 'bytes': end - begin,
                           'candidate_normalized_sha256': trace.normalized_function(code, name),
                           'instructions': [[hex(at), hex(opcode), mnemonic, operands]
                                            for at, opcode, mnemonic, operands in rows],
                           'literals': literals}
    return {'admitted': False, 'independent_review_required': True,
            'config': config, 'functions': functions,
            'typed_generated_header': (directory / 'toy_pilot_resident_symbols.h').read_text(),
            'DATA_state': {name: {'address': hex(worker['symbols'].get(name, 0)),
                                  'bytes': worker['symbol_sizes'].get(name, 0)}
                           for name in ('_data_probe_report', '_data_probe_scope', '_data_probe_read_state')},
            'DATA_compiler_frames': stack_rows([directory / 'worker/src/loader/toy_loader_data_probe.su']),
            'conservative_stack_evidence': trace.audit_stacks(directory, low, worker, dis['worker']),
            'images': {name: {'sha256': hashlib.sha256(image['payload']).hexdigest(),
                              'payload_bytes': len(image['payload']),
                              'memory_end': hex(image['memory_end'])}
                       for name, image in images.items()}}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builddir', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--review-candidate', action='store_true')
    args = parser.parse_args()
    result = (review_candidate(args.builddir, args.baseline) if args.review_candidate
              else audit_data_probe(args.builddir, args.baseline))
    print(json.dumps(result, indent=2, sort_keys=True))
