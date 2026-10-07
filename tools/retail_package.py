#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the native-GD retail-launch envelope and temporary-stage contract."""
import struct

from runtime_package import verify

MAGIC = b"KUIRBT01"
# The Windows CE placement probe package (ce-probe.kui): the same envelope with
# its own magic and a stage linked 64 KiB higher, clear of CE's boot prefix.
CE_MAGIC = b"KUIRCE01"
CE_STAGE_ADDRESS = 0x8CE10000
VERSION = 1
HEADER_OFFSET = 0x100
HEADER_BYTES = 64
MAP_OFFSET = 0x1000
MAP_BYTES = 0x1000
STAGE_BLOB_OFFSET = 0x2000
STAGE_ADDRESS = 0x8CE00000
STAGE_MAX_BYTES = 0x100000
STAGE_MEMORY_END = 0x8CFE0000
STAGE_STACK = 0x8CFF0000
EXEC_ADDRESS = 0x8C010000
EXEC_MAX_BYTES = 0xC00000
RESIDENT_ADDRESS = 0x8C008300
RESIDENT_LIMIT = 0x8C00BB00
HOOK_STACK_BOTTOM = 0x8C00BB00
# The background SCI reader's resident: 352-byte stack proven by call graph.
ASYNC_RESIDENT_LIMIT = 0x8C00BEA0
ASYNC_HOOK_STACK_BOTTOM = 0x8C00BEA0
HOOK_STACK = 0x8C00C000
# Opt-in native placement below IP.BIN. Header selection is an exact known
# tuple; callers cannot supply arbitrary resident bounds.
LOW_RESIDENT_ADDRESS = 0x8C004000
LOW_RESIDENT_LIMIT = 0x8C007800
LOW_ASYNC_RESIDENT_LIMIT = 0x8C007BA0
LOW_HOOK_STACK = 0x8C007D00
# The Windows CE boot test's SCI reader: image below C800, 2 KiB stack above.
CE_RESIDENT_LIMIT = 0x8C00C800
CE_HOOK_STACK = 0x8C00D000
# Its background reader: image below D800, 2 KiB stack up to bootstrap 2.
CE_ASYNC_RESIDENT_LIMIT = 0x8C00D800
CE_ASYNC_HOOK_STACK = 0x8C00E000
TRAMPOLINE_BYTES = 128
HEADER = struct.Struct("<8s14I")


def stage_address(ce=False):
    return CE_STAGE_ADDRESS if ce else STAGE_ADDRESS


def relocation_header(stage_bytes, ce=False, low=False):
    if ce and low:
        raise ValueError("Windows CE does not support the native low resident layout")
    stage = stage_address(ce)
    return HEADER.pack(
        CE_MAGIC if ce else MAGIC, VERSION, HEADER_BYTES, MAP_OFFSET, MAP_BYTES, stage,
        stage_bytes, stage, EXEC_ADDRESS, EXEC_MAX_BYTES, STAGE_STACK,
        STAGE_BLOB_OFFSET, LOW_RESIDENT_ADDRESS if low else RESIDENT_ADDRESS,
        LOW_RESIDENT_LIMIT if low else RESIDENT_LIMIT, 0,
    )


def resident_layout(header, ce=False):
    """Select and validate one complete known relocation header."""
    if len(header) != HEADER_BYTES:
        raise ValueError("Invalid retail relocation header")
    fields = HEADER.unpack(header)
    stage_bytes = fields[6]
    if not 4 <= stage_bytes <= STAGE_MAX_BYTES or stage_bytes % 4:
        raise ValueError("Invalid retail relocation header")
    for low in ((False,) if ce else (False, True)):
        if header == relocation_header(stage_bytes, ce, low):
            return {
                "low": low,
                "address": LOW_RESIDENT_ADDRESS if low else RESIDENT_ADDRESS,
                "standard_limit": LOW_RESIDENT_LIMIT if low else RESIDENT_LIMIT,
                "async_limit": LOW_ASYNC_RESIDENT_LIMIT if low else ASYNC_RESIDENT_LIMIT,
                "hook_stack": LOW_HOOK_STACK if low else HOOK_STACK,
            }
    raise ValueError("Invalid retail relocation header")


def inspect_retail(package, ce=False, formats=False):
    """Validate shipped bytes, not title compatibility or console acceptance.

    Only the console fills the card-specific manifest and reads the owner's
    game bytes. Distributed retail packages must contain neither of them.
    ELF placement and embedded-code identity are checked separately.
    """
    info = verify(package)
    payload = package[64:]
    if (not STAGE_BLOB_OFFSET + 4 <= len(payload) <=
            STAGE_BLOB_OFFSET + STAGE_MAX_BYTES or
            info["memory_bytes"] != len(payload)):
        raise ValueError("Invalid retail staging size")
    header = payload[HEADER_OFFSET:HEADER_OFFSET + HEADER_BYTES]
    placement = resident_layout(header, ce)
    stage_bytes = HEADER.unpack(header)[6]
    if (not 4 <= stage_bytes <= STAGE_MAX_BYTES or stage_bytes % 4 or
            stage_bytes + STAGE_BLOB_OFFSET != len(payload)):
        raise ValueError("Invalid retail relocation header")
    if any(payload[MAP_OFFSET:MAP_OFFSET + MAP_BYTES]):
        raise ValueError("Retail package must ship with a blank card-specific manifest")
    return {
        **info,
        "stage_bytes": stage_bytes,
        "stage_address": f"0x{stage_address(ce):08x}",
        "resident_address": f"0x{placement['address']:08x}",
        "resident_limit": f"0x{placement['standard_limit']:08x}",
        "manifest_bytes": MAP_BYTES,
        "abi": ("Windows CE placement probe; stops before CE runs" if ce else
                "Native CD/GD image formats test (GDI, ISO, BIN/CUE, CDI, BIN/IMG); title compatibility requires console testing" if formats else
                "Native GD-ROM GDI launch; title compatibility requires console testing"),
    }
