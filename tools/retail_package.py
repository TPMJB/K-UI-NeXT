#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the first retail-launch envelope and its temporary-stage contract."""
import struct

from runtime_package import verify

MAGIC = b"KUIRBT01"
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
HOOK_STACK = 0x8C00C000
TRAMPOLINE_BYTES = 128
HEADER = struct.Struct("<8s14I")


def relocation_header(stage_bytes):
    return HEADER.pack(
        MAGIC, VERSION, HEADER_BYTES, MAP_OFFSET, MAP_BYTES, STAGE_ADDRESS,
        stage_bytes, STAGE_ADDRESS, EXEC_ADDRESS, EXEC_MAX_BYTES, STAGE_STACK,
        STAGE_BLOB_OFFSET, RESIDENT_ADDRESS, RESIDENT_LIMIT, 0,
    )


def inspect_retail(package):
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
    stage_bytes = HEADER.unpack(header)[6]
    if (not 4 <= stage_bytes <= STAGE_MAX_BYTES or stage_bytes % 4 or
            stage_bytes + STAGE_BLOB_OFFSET != len(payload) or
            header != relocation_header(stage_bytes)):
        raise ValueError("Invalid retail relocation header")
    if any(payload[MAP_OFFSET:MAP_OFFSET + MAP_BYTES]):
        raise ValueError("Retail package must ship with a blank card-specific manifest")
    return {
        **info,
        "stage_bytes": stage_bytes,
        "stage_address": f"0x{STAGE_ADDRESS:08x}",
        "resident_address": f"0x{RESIDENT_ADDRESS:08x}",
        "resident_limit": f"0x{RESIDENT_LIMIT:08x}",
        "manifest_bytes": MAP_BYTES,
        "abi": "First native-GD retail launch experiment; hardware untested",
    }
