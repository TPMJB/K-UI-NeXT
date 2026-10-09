#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed linked admission for the isolated, passive Toy loader trace.

This profile has its own 80-byte export and 56-byte low-control ABI.  The
baseline cache/audio auditor is reused verbatim; this module never makes its
76-byte profile more permissive.  Reviewed trace instruction identities admit
relocation, not new instructions, callbacks, memory writers, or timer setup.
These are authored-code proofs, not a simulation of console cache hardware.
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

from check_loader_layout import inspect_elf
from check_retail_instructions import audit as instruction_audit
from package_cdda_toy_pilot import (linked_symbol_sizes, pure_worker_leaf_audit,
                                   scalar_worker_adapter_audit, stack_rows)
from toy_pilot_cache_audit import Linked, audit_cache_layout, physical

ROOT = Path(__file__).resolve().parents[1]
LOW, LOW_LIMIT = 0x8C004000, 0x8C007800
HIGH, HIGH_LIMIT = 0x8CFD0000, 0x8CFE0000
STAGE, STAGE_LIMIT = 0x8CE00000, 0x8CF80000
P2 = 0x20000000
REQUIRED_CONFIG = {
    'PROFILE': '15', 'PILOT': '1', 'LOW': '1', 'SLOTS': '64',
    'SCI': '1', 'SCI_PACED': '1', 'SCI_FAULT': '1',
    'SCI_REUSE_TDRE': '0', 'GD_FIXED_STEP': '2', 'SHARED_SCI': '0',
    'ASYNC_CDDA': '0', 'PRIVATE_P2': '1', 'NATIVE_CACHE': '0',
    'SYNTHETIC_SOURCE': '0', 'LOADER_TRACE': '1',
    'OPT': '-Os -fno-tree-scev-cprop',
}
TRACE_FUNCTIONS = (
    '_add', '_initialize', '_clock_sample', '_measured',
    '_outstanding.part.0', '_kui_toy_loader_trace_begin',
    '_kui_toy_loader_trace_end', '_kui_toy_loader_trace_words',
    '_kui_toy_loader_trace_word_count', '_kui_toy_loader_trace_freeze',
)
MMIO_READS = {
    0xFFC00000: 2, 0xFFD80008: 4, 0xFFD80010: 2,
    0xFFD80004: 1, 0xFFD8000C: 4,
    0xA05F810C: 4, 0xA05F80CC: 4, 0xA05F80D8: 4, 0xA05F8050: 4,
}
# Filled with independently reviewed, normalized linked identities.  Absolute
# in-image addresses are replaced with exact symbol+offset references; scalar
# constants and external/MMIO addresses remain exact.  A new compiler shape is
# a refusal requiring review, never an automatically generated acceptance.
REVIEWED_TRACE = {
    '_add': '55fa0b8318a3fbdc40736b057246cd00ad9c87d7ae83df5c073c231cf784cbf1',
    '_initialize': 'ac0ba1616e5a147454bc3aedf0fdc62bb6006f37a796c605d6791a513efb6939',
    '_clock_sample': '75b1a4ccea285232ef5f75913907ae8a99c443a1d4e072bfab30182b70082398',
    '_measured': 'e544978994f7c41a8d31e84de0c89286a214b8c1a47cc674344469c2534287a1',
    '_outstanding.part.0': '921057b3d290d4f64712b211dd1882fd08337477ccf9f901dfbe23632ebfcf0c',
    '_kui_toy_loader_trace_begin': '64a1154ce1879370ed905926d0042435c9c578fcaee01c63c22a040b7d835a0c',
    '_kui_toy_loader_trace_end': 'f2d1728ac99a100a2c08ebbda24d90c504868b6368d1f9c33bc996c514300ec1',
    '_kui_toy_loader_trace_words': '6cc3e7c2bece580cbe4db09dfa15fc4543f5e24b4ec44329ec108382dc85ac54',
    '_kui_toy_loader_trace_word_count': '5eeda1fe2b60d30f55496cf7538e2149e56eb20f2a73654fa729f1df24c674e6',
    '_kui_toy_loader_trace_freeze': '05d8a055959310215169f3755c1783f94caa2b4d22c4e79d24e502e44add7820',
    '_memcpy': 'df34690b3870f0d9f53c7e7a2d5838252c732f69502b8a03ffa47d1dbaf2542c',
    '_kui_toy_pilot_gd_dispatch': 'b180fefabb7448e0d7194a8bf1b42c0cc4d7d7f51c991f388f0d14bf7bb99f75',
}
REVIEWED_TERMINAL = {
    '_kui_toy_loader_trace_report_capture': '37ba9320719ffe08651125aa2df3609a5ea78ec0edeeec55093646094a059248',
    '_kui_toy_loader_trace_report_page': '3694c77f487cd5ea8130d39d679037e2d0440b98544d0d16ed74ce93f23cf48e',
    '_kui_toy_loader_trace_terminal': '0be7008be9a147f1ae9916f139e84f068b44889a503b387c9b4daa7813bd2a06',
    '_retail_display_line': 'ab27d3a63fcf61ac4b3da574394ed0d217fd5d3018e4c7d84a28f728d7a49841',
    '_retail_display_restore': 'b0e7082721ae4972f0b1396c74af44adef83beb2c2647b97afa2ffcca9ef4ebf',
    '_retail_display_values': 'f17537bc62d7f25f8dea89ee93fa742f94c2caf2030546c00f3dc81be61f9e66',
    '_retail_display_pause': '78b8f80ffbc309bbd0139e32005f5a7517a87d3a77f8c88b9f8a4989198b5d47',
    '_memset': '61957130b5424325c94643108574911ddeddc6321debb3dfd347cfd612639ac0',
}
REVIEWED_STAGE = {
    '_toy_exports_check': '06a43968936375b1242b023933e5b22f997c8b744b7682f62658c59b31930a49',
    '_kui_toy_pilot_stage_install': 'a5fa532a56347ae418459115485fb2f788f9b962c9a8f87503b48939ea8ed8cc',
}
REVIEWED_INITIALIZE = {
    '_kui_toy_pilot_worker_initialize': '34e53cc72c8a5547f9b0f2e232d2bde6fd9fcc621a775122560c6e65cb69e061',
}


def read_config(directory):
    config = {}
    for line in (Path(directory) / 'build-config').read_text().splitlines():
        if '=' not in line:
            raise ValueError('Malformed trace build configuration')
        key, value = line.split('=', 1)
        if key in config:
            raise ValueError('Duplicate trace build configuration: ' + key)
        config[key] = value
    if any(config.get(key) != value for key, value in REQUIRED_CONFIG.items()):
        raise ValueError('Loader trace requires the exact R profile plus LOADER_TRACE=1')
    if set(config) != set(REQUIRED_CONFIG) | {'BUILD'}:
        raise ValueError('Loader trace configuration contains an unreviewed key')
    if not re.fullmatch(r'[0-9a-f]{12}', config.get('BUILD', '')):
        raise ValueError('Loader trace requires a twelve-digit hexadecimal build identity')
    return config


def load_profile(directory):
    directory = Path(directory).resolve()
    config = read_config(directory)
    objdump = shutil.which('sh-elf-objdump')
    if objdump is None:
        candidate = ROOT.parent / 'launch-logo-fix/.deps/sh-elf/bin/sh-elf-objdump'
        if not candidate.is_file():
            raise ValueError('SH objdump is required for linked loader trace admission')
        objdump = str(candidate)
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
            raise ValueError(name + '.bin differs from its actual ELF load bytes')
        images[name] = image
        disassemblies[name] = subprocess.check_output([objdump, '-d', str(path)], text=True)
    return config, images, disassemblies


def exact_function(code, name):
    begin = code.symbols.get(name, 0)
    size = code.image['symbol_sizes'].get(name, 0)
    if not size or size & 1 or not code.base <= begin < begin + size <= code.base + len(code.payload):
        raise ValueError('Missing exact-size linked function: ' + name)
    rows = [row for row in code.rows if begin <= row[0] < begin + size and row[0] not in code.literals]
    if not rows or rows[0][0] != begin:
        raise ValueError('Missing decoded linked function: ' + name)
    if any(code.half(at) != opcode for at, opcode, _, _ in rows):
        raise ValueError('Decoded function differs from actual linked bytes: ' + name)
    return begin, begin + size, rows


def owned_bss(image, objects):
    symbols, sizes = image['symbols'], image['symbol_sizes']
    begin, end = symbols['__toy_pilot_bss_begin'], symbols['__toy_pilot_bss_end']
    result = {}
    for name, required_size in objects.items():
        at, size = symbols.get(name, 0), sizes.get(name, 0)
        if size != required_size or not HIGH + P2 <= begin <= at < at + size <= end <= HIGH_LIMIT + P2:
            raise ValueError('Trace state lacks exact high P2 BSS ownership: ' + name)
        result[name] = {'address': hex(at), 'bytes': size}
    spans = sorted((symbols[name], symbols[name] + sizes[name], name) for name in objects)
    if any(left[1] > right[0] for left, right in zip(spans, spans[1:])):
        raise ValueError('Trace state objects overlap their independently owned high P2 reservations')
    return result


def audit_exports(resident, worker):
    s = worker['symbols']
    exports = struct.unpack_from('<20I', worker['payload'])
    if exports[:3] != (0x54595031, 8, 80):
        raise ValueError('Loader trace requires the separate TYP1/v8/80-byte export ABI')
    entries = {
        3: '_kui_toy_pilot_initialize', 4: '_kui_toy_pilot_request',
        5: '_kui_toy_pilot_service_hook', 6: '_kui_toy_pilot_am_init_hook',
        7: '_kui_toy_pilot_shutdown_hook', 8: '_kui_toy_pilot_snapshot',
        15: '_kui_toy_pilot_allstop_hook', 16: '_kui_toy_pilot_driver_load_hook',
        17: '_kui_toy_pilot_gd_bridge', 18: '_kui_toy_pilot_pause_hook',
        19: '_kui_toy_loader_trace_terminal_bridge',
    }
    bounds = {9: '__toy_pilot_bss_begin', 10: '__toy_pilot_bss_end',
              11: '__toy_pilot_stack_bottom', 12: '__toy_pilot_stack_top',
              13: '__toy_pilot_worker_end'}
    if any(exports[index] != s.get(name) for index, name in {**entries, **bounds}.items()) or exports[14] != 0x30000:
        raise ValueError('Trace exports differ from their exact linked functions/reservations')
    if any(not HIGH + 80 <= exports[index] < HIGH + len(worker['payload']) or exports[index] & 1
           for index in entries):
        raise ValueError('Trace export callback escapes owned cached code')
    bottom, top = s.get('__toy_pilot_gd_stack_bottom', 0), s.get('__toy_pilot_gd_stack_top', 0)
    if (bottom != s.get('__toy_pilot_bss_end') or top - bottom != 4096 or
            top != s.get('__toy_pilot_stack_bottom') or bottom & 31 or top & 31 or
            not HIGH + P2 <= bottom < top <= HIGH_LIMIT + P2 or
            physical(s.get('__toy_pilot_stack_top', 0)) != worker['memory_end']):
        raise ValueError('Trace GD stack is not an independently owned guarded 4KiB P2 reservation')
    rs = resident['symbols']
    control = rs.get('_kui_toy_pilot_boot_control', 0)
    if (not LOW + P2 <= control < control + 56 <= rs.get('__retail_resident_bss_end', 0) or
            rs.get('_kui_toy_pilot_heap_saved_r9') != control + 56):
        raise ValueError('Trace low boot control is not exactly 56 owned P2 bytes')
    return {'version': 8, 'bytes': 80, 'words': 20, 'boot_bytes': 56,
            'boot_control': hex(control), 'gd_bridge': hex(exports[17]),
            'terminal_bridge': hex(exports[19]), 'gd_stack_bottom': hex(bottom),
            'gd_stack_top': hex(top), 'gd_stack_bytes': 4096}


def audit_bridges(worker, disassembly):
    code = Linked(worker, HIGH, disassembly)
    s = worker['symbols']
    gd = s['_kui_toy_pilot_gd_bridge']
    # Fifth argument is captured before any incoming-stack save.  The bridge
    # is SR-neutral and inherits the separately proven low hook's BL/IMASK.
    code.sequence(gd, (0x60F2, 0x4F22, 0x2F86, 0x2F96, 0x68F3,
        None, 0x6212, None, 0x3320, 0x8B09, None, 0x2F06, None,
        0x400B, 9, None, 0x6212, None, 0x3320, 0x8900, 0xE0FF,
        0x6F83, 0x69F6, 0x68F6, 0x4F26, 0x000B, 9))
    for offset, register, value in ((10, 1, s['__toy_pilot_gd_stack_bottom']),
            (14, 3, 0xA55A4AA5), (20, 15, s['__toy_pilot_gd_stack_top']),
            (24, 0, s['_kui_toy_pilot_gd_dispatch']),
            (30, 1, s['__toy_pilot_gd_stack_bottom']), (34, 3, 0xA55A4AA5)):
        if code.literal(gd + offset, register)[1] != value:
            raise ValueError('Trace GD bridge changed its guard/argument/call binding')
    _, _, rows = exact_function(code, '_kui_toy_pilot_gd_bridge')
    if any(row[2] in ('ldc', 'ldc.l') and 'sr' in row[3] for row in rows):
        raise ValueError('Trace GD bridge must preserve the proven incoming SR mask')
    terminal = s['_kui_toy_loader_trace_terminal_bridge']
    code.sequence(terminal, (0x0102, None, 0x221B, 0x420E,
        0x2F16, 0x4F22, 0x2F86, 0x2F96, 0x68F3, None, 0x6212,
        None, 0x3320, 0x8B03, None, None, 0x400B, 9,
        0x6F83, 0x69F6, 0x68F6, 0x4F26, 0x61F6, 0x410E, 0x000B, 9))
    for offset, register, value in ((2, 2, 0x100000F0),
            (18, 1, s['__toy_pilot_gd_stack_bottom']), (22, 3, 0xA55A4AA5),
            (28, 15, s['__toy_pilot_gd_stack_top']),
            (30, 0, s['_kui_toy_loader_trace_terminal'])):
        if code.literal(terminal + offset, register)[1] != value:
            raise ValueError('Trace terminal bridge changed its mask/guard/call binding')
    guard_init = code.find('_kui_toy_pilot_worker_initialize',
        (None, 0x1126, None, None, 0x3236, 0x3128, 0x7103, 0x4109,
         0x7101, 0x8B00, 0xE101, 0x4110, 0x8B33))
    for offset, register, value in ((0, 7, 0xA55A4AA5),
            (4, 2, s['__toy_pilot_gd_stack_bottom']), (6, 1, s['__toy_pilot_gd_stack_top'])):
        if code.literal(guard_init + offset, register)[1] != value:
            raise ValueError('Trace GD stack guard initialization lost its owned bounds/pattern')
    target = guard_init + 24 + 4 + _signed(0x33, 8) * 2
    code.sequence(target, (0x2272, 0xAFC7, 0x7204))
    return {'gd_incoming_SR_preserved': True, 'gd_fifth_argument_copied_before_save': True,
            'gd_pre_and_post_guard_checks': True, 'terminal_mask_before_first_save': '0x100000f0',
            'terminal_original_SR_PR_SP_and_r8_r9_restored': True,
            'terminal_four_register_arguments_preserved': True,
            'gd_stack_owned_guard_initialization': True}


def audit_boot_publication(resident, worker, stage, low_dis, stage_dis):
    low, code = Linked(resident, LOW, low_dis), Linked(stage, STAGE, stage_dis)
    boot = resident['symbols']['_kui_toy_pilot_boot_control']
    # r8 is the exact admitted exports copy at SP+52.  Offsets68 and76 are
    # read through SP+116, then copied into low48/52, before INSTALLED.
    entry = code.find('_kui_toy_pilot_stage_install',
        (0x68F3, None, 0x4C0B, 0x7834, None, 0x6483, 0x3B18, None, 0x410B, 0x65B3))
    if code.literal(entry + 14, 1)[1] != stage['symbols']['_toy_exports_check']:
        raise ValueError('Trace exports local copy lacks exact linked admission')
    at = code.find('_kui_toy_pilot_stage_install', (0x518D, None, None,
        0x141A, 0x5184, 0x1418, 0x5188, 0x1419, 0x61F3, 0x7174,
        0x5211, 0x142C, 0x5113, 0x141D, 0xE100, 0x2412, 0x1411,
        0xE104, 0x1412, 0x4A0B, 9))
    if (code.literal(at + 2, 4)[1] != boot or code.literal(at + 4, 5)[1] != boot + 56):
        raise ValueError('Trace boot publication misses the exact 56-byte control span')
    # Terminal has no supplied render callback; its exact four registers are
    # display, six retained timing words, service.diag, and reserved zero.
    terminal = low.find('_kui_retail_menu_return', (0x511D, None, None, None, 0x410B, 0xE700))
    diag = resident['symbols']['_service'] + 0x90
    for offset, register, value in ((2, 6, diag), (4, 5, resident['symbols']['_toy_gd_timing']),
                                    (6, 4, resident['symbols']['_display'])):
        if low.literal(terminal + offset, register)[1] != value:
            raise ValueError('Trace terminal arguments escape the admitted low state')
    return {'exports_offset68_to_boot48': True, 'exports_offset76_to_boot52': True,
            'installed_status_after_all_callbacks': True, 'published_bytes': 56,
            'terminal_register_arguments': ['display', 'timing[6]', 'service.diag', 'zero'],
            'terminal_no_guest_render_callback': True}


def normalized_function(code, name):
    """Instruction/literal identity with only owned-image relocations removed."""
    begin, end, rows = exact_function(code, name)
    symbols, sizes = code.symbols, code.image['symbol_sizes']

    def value_identity(value):
        exact = sorted(label for label, at in symbols.items() if at == value
                       and not label.startswith('.') and not label.startswith('KUI_'))
        if exact:
            return ('symbol', exact[0])
        containing = [(sizes.get(label, 0), label, value - at)
                      for label, at in symbols.items() if sizes.get(label, 0)
                      and at < value < at + sizes[label] and not label.startswith('.')]
        if containing:
            _, label, offset = min(containing)
            return ('inside', label, offset)
        return ('value', value)

    result = []
    for at, opcode, mnemonic, _ in rows:
        if opcode & 0xF000 in (0xD000, 0x9000) and mnemonic in ('mov.l', 'mov.w'):
            width = 4 if opcode & 0xF000 == 0xD000 else 2
            target, value = code.literal(at, opcode >> 8 & 15, width)
            if begin <= target < end and target not in code.literals:
                raise ValueError('Reviewed literal overlaps executable trace instructions')
            result.append((at - begin, opcode & 0xFF00, width, value_identity(value)))
        else:
            result.append((at - begin, opcode))
    return hashlib.sha256(json.dumps((end - begin, result), separators=(',', ':')).encode()).hexdigest()


def reviewed_identities(code, expected, label):
    if not expected:
        raise ValueError('Missing independently reviewed linked ' + label + ' identities')
    observed = {name: normalized_function(code, name) for name in expected}
    if observed != expected:
        changed = [name for name in expected if observed[name] != expected[name]]
        raise ValueError('Changed reviewed linked ' + label + ' instructions/literals: ' + ', '.join(changed))
    return observed


def _signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def audit_trace_graph(code):
    """Walk actual SH control flow, resolve all calls and constant MMIO accesses.

    Register constants are tracked through branches, copies, additions and
    call-clobber rules.  The separately checked reviewed instruction identity
    closes arbitrary pointer-derived stores; this walk explicitly rejects MMIO
    stores, invalid widths, unknown callbacks, and branches into literal pools.
    """
    graph, reads = {}, set()
    allowed = {code.symbols[name]: name for name in TRACE_FUNCTIONS}
    primitives = {code.symbols['_memcpy']: '_memcpy'}
    allowed.update(primitives)
    for name in (*TRACE_FUNCTIONS, '_memcpy'):
        begin, end, rows = exact_function(code, name)
        rowmap = {row[0]: row for row in rows}
        # A symbolic private-stack anchor also resolves compiler spills of
        # constant callback addresses.  It is not a proposed runtime address.
        anchor, stack_low, stack_cells = 0xACFDE000, 0xACFDDC00, 272
        entry_state = [None] * (16 + stack_cells)
        entry_state[15] = anchor
        incoming, pending, transfers = {begin: tuple(entry_state)}, [begin], set()

        def merge(at, state):
            if at not in rowmap:
                raise ValueError('Trace control flow escapes executable function: ' + name)
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
            # Exact register-indirect and displacement SH load/store forms.
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
                    raise ValueError('Trace wrote outside owned/guest main RAM: ' + hex(address))
                if MMIO_READS.get(address) != width:
                    raise ValueError('Trace read an unreviewed MMIO address/width: ' + hex(address))
                reads.add((address, width))
            stack_index = (16 + (address - stack_low) // 4
                           if address is not None and address % 4 == 0 and
                           stack_low <= address < stack_low + stack_cells * 4 else None)
            loaded = state[stack_index] if stack_index is not None and width == 4 else None
            if store and stack_index is not None:
                state[stack_index] = state[m] if width == 4 else None
            predecrement = opcode & 0xF00F in (0x2004, 0x2005, 0x2006)
            if predecrement:
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
            # Postincrement register loads also destroy the source constant.
            if '@r' in operands and '+' in operands:
                source = re.search(r'@r(\d+)\+', operands)
                if source:
                    source = int(source[1])
                    # SH suppresses postincrement when load source=dest.
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
                raise ValueError('Unreviewed trace transfer: ' + name)
            delayed = (opcode & 0xF000 in (0xA000, 0xB000) or
                       opcode & 0xFF00 in (0x8D00, 0x8F00) or
                       opcode & 0xF0FF in (0x400B, 0x402B) or opcode == 0x000B)
            if delayed:
                target_register = opcode >> 8 & 15
                target = state[target_register] if opcode & 0xF0FF in (0x400B, 0x402B) else None
                if at + 2 not in rowmap:
                    raise ValueError('Trace transfer has no executable delay slot')
                state = effect(at + 2, state)
                if opcode == 0x000B:
                    continue
                if opcode & 0xF000 in (0xA000, 0xB000):
                    target = at + 4 + _signed(opcode & 0xFFF, 12) * 2
                if opcode & 0xF000 == 0xB000 or opcode & 0xF0FF in (0x400B, 0x402B):
                    if target not in allowed:
                        raise ValueError('Trace gained an unknown/out-of-scope callback in ' + name +
                                         ' at ' + hex(at) + ': ' + str(target))
                    transfers.add(allowed[target])
                    if opcode & 0xF0FF == 0x402B:
                        continue
                    state[:8] = [None] * 8
                    merge(at + 4, state)
                elif opcode & 0xF000 == 0xA000:
                    merge(target, state)
                else:
                    target = at + 4 + _signed(opcode & 255, 8) * 2
                    merge(target, state)
                    merge(at + 4, state)
            elif opcode & 0xFF00 in (0x8900, 0x8B00):
                merge(at + 4 + _signed(opcode & 255, 8) * 2, state)
                merge(at + 2, state)
            else:
                merge(at + 2, effect(at, state))
        graph[name] = sorted(transfers)
    if reads != set(MMIO_READS.items()):
        raise ValueError('Trace must retain all nine exact passive timer/PVR reads')
    return {'closed_functions': graph, 'MMIO_reads': [{'address': hex(at), 'bytes': size}
            for at, size in sorted(reads)], 'MMIO_writes': False,
            'timer_configuration_writes': False, 'audio_or_storage_callbacks': False,
            'unknown_indirect_transfers': False}


def audit_adapter(worker, disassembly):
    # The original adapter remains independently provable by the unchanged
    # baseline gate.  Only its symbol name is projected, never its opcodes.
    projected = copy.deepcopy(worker)
    body = '_dispatch_body'
    if body not in worker['symbols']:
        raise ValueError('Trace requires a separately bounded original scalar adapter body')
    projected['symbols']['_kui_toy_pilot_gd_dispatch'] = worker['symbols'][body]
    projected['symbol_sizes']['_kui_toy_pilot_gd_dispatch'] = worker['symbol_sizes'][body]
    pure = pure_worker_leaf_audit(worker, disassembly)
    scalar = scalar_worker_adapter_audit(projected, disassembly)
    code = Linked(worker, HIGH, disassembly)
    begin, end, rows = exact_function(code, '_kui_toy_pilot_gd_dispatch')
    # The wrapper's reviewed identity below binds argument preservation and
    # exactly begin -> unmodified body -> end -> original result return.
    observed = []
    for index, (at, opcode, mnemonic, _) in enumerate(rows):
        if mnemonic != 'jsr':
            continue
        register = opcode >> 8 & 15
        target = None
        for prev in reversed(rows[max(0, index - 8):index]):
            if prev[1] & 0xFF00 == 0xD000 | register << 8:
                target = code.literal(prev[0], register)[1]
                break
        observed.append(target)
    required = [worker['symbols'][name] for name in
                ('_kui_toy_loader_trace_begin', body, '_kui_toy_loader_trace_end')]
    if observed != required or end - begin > 128:
        raise ValueError('Trace adapter is not the exact bounded three-call thin wrapper')
    return {'scalar_body': scalar, 'pure_leaves': pure,
            'wrapper_call_order': ['trace_begin', 'original_scalar_body', 'trace_end'],
            'wrapper_bytes': end - begin, 'original_result_preserved': True}


def audit_stacks(directory, resident, worker, worker_disassembly):
    directory = Path(directory).resolve()
    low_rows = stack_rows(list((directory / 'sci/lto').glob('*.ltrans*.su')))
    worker_rows = stack_rows(list((directory / 'worker').rglob('*.su')))
    stage_rows = stack_rows(list((directory / 'stage').rglob('*.su')))
    # Exclude only whole entries which run on installer/audio stacks.  ALL
    # their shared low synchronous reader callees remain in the GD sum.
    excluded_names = {'kui_retail_resident_init', 'kui_toy_pilot_read_raw'}
    required_low = excluded_names | {'kui_retail_resident_dispatch', 'kui_retail_gd_dispatch',
                                      'kui_retail_image_read', 'kui_loader_sd_stream_next'}
    if (not required_low <= {row['function'] for row in low_rows} or
            any('_' + name not in resident['symbols'] for name in required_low)):
        raise ValueError('Missing final low synchronous reader stack evidence')
    trace_rows = stack_rows([directory / 'worker/src/loader/toy_loader_trace.su'])
    required_trace = {name[1:] for name in TRACE_FUNCTIONS}
    if not required_trace <= {row['function'] for row in trace_rows}:
        raise ValueError('Missing trace function static compiler stack evidence')
    code = Linked(worker, HIGH, worker_disassembly)
    # Check each trace .su frame against actual prologue saves/allocations,
    # including a call's delay-slot allocation.  Evidence cannot silently
    # understate a new linked frame while preserving the overall sum.
    for row in trace_rows:
        _, _, instructions = exact_function(code, '_' + row['function'])
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
            raise ValueError('Trace .su differs from the actual linked frame: ' + row['function'])
    required_high = {'dispatch_body', 'kui_toy_pilot_gd_dispatch',
                     'kui_toy_loader_trace_terminal', 'kui_toy_loader_trace_report_capture'}
    if not required_high <= {row['function'] for row in worker_rows}:
        raise ValueError('Missing high GD/terminal static compiler stack evidence')
    kept_low = [row for row in low_rows if row['function'] not in excluded_names]
    low_bytes, high_bytes = sum(row['bytes'] for row in kept_low), sum(row['bytes'] for row in worker_rows)
    assembly = 256
    limits = {'low': (low_bytes + assembly, 1232),
              'high_GD': (high_bytes + low_bytes + assembly, 4096 - 96),
              'audio_worker': (high_bytes + low_bytes +
                  sum(row['bytes'] for row in low_rows if row['function'] == 'kui_toy_pilot_read_raw') +
                  assembly, 8192 - 96)}
    for name, (total, available) in limits.items():
        if total > available:
            raise ValueError(f'{name} conservative stack sum {total} exceeds guarded reservation {available}')
    return {'conservative_sums': {name: {'bytes': total, 'available': available}
            for name, (total, available) in limits.items()}, 'assembly_allowance_bytes': assembly,
            'trace_emitted_functions': len(trace_rows), 'low_emitted_functions': len(low_rows),
            'high_emitted_functions': len(worker_rows),
            'stage_emitted_C_frame_sum_only': sum(row['bytes'] for row in stage_rows),
            'excluded_whole_low_entries_only': sorted(excluded_names),
            'all_low_synchronous_read_callback_callees_included': True,
            'audio_sum_includes_whole_low_raw_callback_and_callees': True,
            'all_high_C_frames_in_GD_sum': True, 'game_SDK_frames_included': False}


def audit_loader_trace(builddir):
    directory = Path(builddir).resolve()
    config, images, dis = load_profile(directory)
    low, worker, stage = (images[name] for name in ('resident-sci', 'worker', 'stage'))
    for name in images:
        try:
            instruction_audit(name, dis[name])
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
    exports = audit_exports(low, worker)
    cache = audit_cache_layout(low, worker, stage, dis['resident-sci'], dis['worker'], dis['stage'])
    state = owned_bss(worker, {'_report': 1664, '_visit': 108, '_request': 96,
                              '_trace_words': 1664, '_pilot_words': 512,
                              '_page': 4, '_stopped_display': 56, '_row': 4})
    code = Linked(worker, HIGH, dis['worker'])
    trace_hashes = reviewed_identities(code, REVIEWED_TRACE, 'trace')
    terminal_hashes = reviewed_identities(code, REVIEWED_TERMINAL, 'terminal')
    initialize_hash = reviewed_identities(code, REVIEWED_INITIALIZE, 'GD guard initialization')
    if (worker['symbol_sizes'].get('_ceilings.0') != 28 or
            code.data(worker['symbols'].get('_ceilings.0', 0), 28) !=
            struct.pack('<7I', 782, 1563, 3125, 6250, 12500, 25000, 50000)):
        raise ValueError('Trace timing histogram thresholds differ from the reviewed tick scale')
    stage_hashes = reviewed_identities(Linked(stage, STAGE, dis['stage']), REVIEWED_STAGE, 'stage publication')
    graph = audit_trace_graph(code)
    return {'profile': 'toy-loader-trace-R', 'build': config['BUILD'],
            'exports': exports,
            'trace': {'version': 1, 'report_magic': '0x4c545231', 'report_words': 416,
                      'report_bytes': 1664, 'tick_hz': 781250,
                      'state': state, 'passive_callgraph': graph,
                      'reviewed_linked_identities': trace_hashes,
                      'terminal_linked_identities': terminal_hashes},
            'bridges': audit_bridges(worker, dis['worker']),
            'publication': audit_boot_publication(low, worker, stage, dis['resident-sci'], dis['stage']),
            'stage_linked_identities': stage_hashes,
            'GD_guard_initialize_linked_identity': initialize_hash,
            'adapter': audit_adapter(worker, dis['worker']),
            'stacks': audit_stacks(directory, low, worker, dis['worker']),
            'retained_cache_audio_heap_proofs': cache,
            'hardware_behavior_verified': False, 'audio_or_video_improvement_proven': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builddir', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit_loader_trace(args.builddir), indent=2, sort_keys=True))
