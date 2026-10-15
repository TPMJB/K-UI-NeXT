#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect the separate native SCI Standard LZ4-ZSO launch package."""
import argparse
import json
from pathlib import Path
import struct

import retail_package as retail
from runtime_package import verify

MAGIC = b"KUIRZB01"
WIRE_MAGIC = b"KUIRZS01"
VERSION = 1
HEADER_OFFSET = retail.HEADER_OFFSET
HEADER_BYTES = retail.HEADER_BYTES
MAP_OFFSET = retail.MAP_OFFSET
MAP_BYTES = retail.MAP_BYTES
STAGE_BLOB_OFFSET = retail.STAGE_BLOB_OFFSET
STAGE_ADDRESS = retail.STAGE_ADDRESS
STAGE_MAX_BYTES = retail.STAGE_MAX_BYTES
STAGE_MEMORY_END = retail.STAGE_MEMORY_END
STAGE_STACK = retail.STAGE_STACK
EXEC_ADDRESS = retail.EXEC_ADDRESS
EXEC_MAX_BYTES = retail.EXEC_MAX_BYTES
RESIDENT_ADDRESS = retail.LOW_RESIDENT_ADDRESS
RESIDENT_LIMIT = retail.LOW_RESIDENT_LIMIT
HOOK_STACK = retail.LOW_HOOK_STACK
HEADER = struct.Struct("<8s14I")
MAP_SLOTS = 32
MAX_EXTENTS = 31


def relocation_header(stage_bytes):
    """The sole supported tuple; no alternate transports or placements."""
    return HEADER.pack(MAGIC, VERSION, HEADER_BYTES, MAP_OFFSET, MAP_BYTES,
                       STAGE_ADDRESS, stage_bytes, STAGE_ADDRESS, EXEC_ADDRESS,
                       EXEC_MAX_BYTES, STAGE_STACK, STAGE_BLOB_OFFSET,
                       RESIDENT_ADDRESS, RESIDENT_LIMIT, 0)


def inspect_payload(payload, memory_bytes=None):
    """Check distributed bytes before the owner fills the private map."""
    if (not STAGE_BLOB_OFFSET + 4 <= len(payload) <= STAGE_BLOB_OFFSET + STAGE_MAX_BYTES or
            memory_bytes is not None and memory_bytes != len(payload)):
        raise ValueError("Invalid ZSO staging size")
    header = payload[HEADER_OFFSET:HEADER_OFFSET + HEADER_BYTES]
    stage_bytes = HEADER.unpack(header)[6]
    if (not 4 <= stage_bytes <= STAGE_MAX_BYTES or stage_bytes % 4 or
            stage_bytes + STAGE_BLOB_OFFSET != len(payload) or
            header != relocation_header(stage_bytes)):
        raise ValueError("Invalid ZSO relocation header")
    if any(payload[MAP_OFFSET:MAP_OFFSET + MAP_BYTES]):
        raise ValueError("ZSO package must ship with a blank card-specific manifest")
    if retail.RAM_PATCH_MAGIC in payload:
        raise ValueError("ZSO package cannot contain a RAM patch capability")
    return {
        "stage_bytes": stage_bytes,
        "stage_address": f"0x{STAGE_ADDRESS:08x}",
        "resident_address": f"0x{RESIDENT_ADDRESS:08x}",
        "resident_limit": f"0x{RESIDENT_LIMIT:08x}",
        "manifest_bytes": MAP_BYTES,
        "manifest_magic": WIRE_MAGIC.decode("ascii"),
        "manifest_slots": MAP_SLOTS,
        "physical_file_fragments": MAX_EXTENTS,
        "transport": "SCI",
        "reader": "Standard",
        "codec": "standard LZ4 ZISO v1; independent 2048-byte blocks",
        "abi": "Opt-in native ZSO first slice; console compatibility requires testing",
    }


def inspect_zso(package):
    """Validate envelope/header/map, never claim game or hardware acceptance."""
    info = verify(package)
    return {**info, **inspect_payload(package[64:], info["memory_bytes"])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    try:
        if args.package.stat().st_size > STAGE_BLOB_OFFSET + STAGE_MAX_BYTES + 64:
            raise ValueError("Oversized ZSO launch package")
        print(json.dumps(inspect_zso(args.package.read_bytes()), sort_keys=True))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"ZSO package check failed: {error}\n")


if __name__ == "__main__":
    main()
