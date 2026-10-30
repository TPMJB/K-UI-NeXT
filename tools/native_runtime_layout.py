#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent, exact geometry oracle for the current native runtime.

The production headers are checked against these reviewed values; deriving the
oracle from those headers would let a changed layout silently authorize itself.
Historical 192 KiB fixtures keep their own geometry.
"""
LEASE_BYTES = 0x51000
WORKER_BYTES = 0x1c000
MAP_OFFSET = 0x1c000
REGION_OFFSET = 0x1c400
AUX_OFFSET = 0x1d000
AUX_SPAN = 0x4000
AUX_LINK_BASE = 0x8c421000
AUX_CODE_BEGIN = 0x8c422000
AUX_CODE_LIMIT = 0x8c424000
AUX_STACK_BOTTOM = 0xac424000
AUX_STACK_TOP = 0xac425000
STACK_OFFSET = 0x20000
STACK_END = 0x21000
DATA_OFFSET = 0x21000
DATA_BYTES = 0x30000

assert AUX_OFFSET + AUX_SPAN == STACK_END == DATA_OFFSET
assert STACK_END - STACK_OFFSET == 0x1000
assert DATA_OFFSET + DATA_BYTES == LEASE_BYTES
assert AUX_STACK_TOP - AUX_STACK_BOTTOM == 0x1000
assert AUX_CODE_LIMIT == AUX_STACK_BOTTOM - 0x20000000

# Explicit diagnostic builds retain their complete128KiB worker. Selection
# comes only from the immutable compiled profile marker, never a requested size.
from types import SimpleNamespace
GENERAL = SimpleNamespace(**{name: value for name, value in tuple(globals().items())
                             if name.isupper() and isinstance(value, int)})
DIAGNOSTIC = SimpleNamespace(**{**vars(GENERAL),
    'LEASE_BYTES': 0x55000, 'WORKER_BYTES': 0x20000, 'MAP_OFFSET': 0x20000,
    'REGION_OFFSET': 0x20400, 'AUX_OFFSET': 0x21000,
    'STACK_OFFSET': 0x24000, 'STACK_END': 0x25000, 'DATA_OFFSET': 0x25000})

HISTORICAL = SimpleNamespace(**{**vars(DIAGNOSTIC),
    'LEASE_BYTES': 0x60000, 'AUX_SPAN': 0x7000,
    'AUX_CODE_LIMIT': 0x8c426000, 'AUX_STACK_BOTTOM': 0xac427000,
    'AUX_STACK_TOP': 0xac428000, 'STACK_OFFSET': 0x27000,
    'STACK_END': 0x28000, 'DATA_OFFSET': 0x30000})

# Standalone virtual-A2 service owns only the measured worker and private
# stack. An exact compiled marker, never a caller-selected byte count, selects
# this geometry. No retained disc map, read-ahead auxiliary or cache exists.
VMU_ONLY = SimpleNamespace(**{**vars(GENERAL),
    'LEASE_BYTES': 0x1c000, 'WORKER_BYTES': 0x1c000,
    'MAP_OFFSET': 0, 'REGION_OFFSET': 0, 'AUX_OFFSET': 0, 'AUX_SPAN': 0,
    'STACK_OFFSET': 0x1b000, 'STACK_END': 0x1c000,
    'DATA_OFFSET': 0, 'DATA_BYTES': 0})

# VMU-lite: the same service with one staged reply and 4 KiB chain copies,
# selected by its own exact compiled marker on each image.
VMU_LITE = SimpleNamespace(**{**vars(VMU_ONLY),
    'LEASE_BYTES': 0x12000, 'WORKER_BYTES': 0x12000,
    'STACK_OFFSET': 0x11000, 'STACK_END': 0x12000})

def layout_for(symbols):
    compact = [symbols[name] for name in ('__kui_native_vmu_only_worker',
        '__kui_native_vmu_only_stage', '__kui_native_vmu_only_resident') if name in symbols]
    if compact:
        if any(value != (1, 0, 0, 0xfff1) for value in compact):
            raise ValueError('Malformed VMU-only runtime geometry marker')
        lite = [symbols[name] for name in ('__kui_native_vmu_lite_worker',
            '__kui_native_vmu_lite_stage', '__kui_native_vmu_lite_resident') if name in symbols]
        if any(value != (1, 0, 0, 0xfff1) for value in lite):
            raise ValueError('Malformed VMU-lite runtime geometry marker')
        selected = VMU_LITE if lite else VMU_ONLY
        lease = symbols.get('__kui_native_isolation_lease_bytes')
        if lease is not None and lease != (selected.LEASE_BYTES, 0, 0, 0xfff1):
            raise ValueError('VMU-only lease conflicts with its exact compiled profile')
        if any(name in symbols for name in ('__kui_native_readahead_stage',
                '__kui_native_readahead_worker', '__kui_native_readahead_resident')):
            raise ValueError('VMU-only runtime cannot retain read-ahead machinery')
        return selected
    markers = ('__kui_native_general_lean_stage_layout',
               '__kui_native_general_lean_worker_layout',
               '__kui_native_general_lean_auxiliary',
               '__kui_native_general_lean_resident_layout')
    present = [symbols[name] for name in markers if name in symbols]
    if any(value != (1, 0, 0, 0xfff1) for value in present):
        raise ValueError('Malformed General runtime geometry marker')
    lease_marker = symbols.get('__kui_native_isolation_lease_bytes')
    if present:
        selected = GENERAL
    elif lease_marker == (0x60000, 0, 0, 0xfff1):
        selected = HISTORICAL
    else:
        selected = DIAGNOSTIC
    if lease_marker is not None and lease_marker != (selected.LEASE_BYTES, 0, 0, 0xfff1):
        raise ValueError('Runtime lease marker conflicts with its exact compiled profile')
    return selected
