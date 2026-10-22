#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the selected-image GD-vector probe inside its runtime envelope.

This is a different relocation contract from the original synthetic-fixture
probe. Its card-specific manifest is populated only after package validation
on the console; distributed packages must contain an entirely blank manifest.
Constants match include/kui/image_loader_layout.h and are checked by host tests.
"""
import struct

from runtime_package import verify

MAGIC = b"KUIIMG01"
VERSION = 1
HEADER_OFFSET = 0x100
HEADER_BYTES = 64
MANIFEST_OFFSET = 0x1000
MANIFEST_BYTES = 65536
RESIDENT_BLOB_OFFSET = 0x12000
RESIDENT_ADDRESS = 0x8CE00000
RESIDENT_MAX_BYTES = 0x100000
CLIENT_ADDRESS = 0x8C010000
CLIENT_MAX_BYTES = 0x100000
CLIENT_STACK = 0x8CD00000
RESIDENT_STACK = 0x8CFF0000
HEADER = struct.Struct("<8s14I")


def inspect_image_probe(package):
    """Return envelope/relocation metadata; reject untrusted or stale layouts.

    The outer checksum verifies the whole packed executable. The fixed inner
    contract then bounds its relocation to the probe's reserved high RAM.
    This validator does not establish retail compatibility or inspect SH code;
    the separate ELF checker verifies linked symbols and memory reservations.
    """
    info = verify(package)
    payload = package[64:]
    if (not RESIDENT_BLOB_OFFSET + 4 <= len(payload) <=
            RESIDENT_BLOB_OFFSET + RESIDENT_MAX_BYTES or
            info["memory_bytes"] != len(payload)):
        raise ValueError("Invalid image-probe staging size")
    header = HEADER.unpack_from(payload, HEADER_OFFSET)
    resident_bytes = header[6]
    expected = (
        MAGIC, VERSION, HEADER_BYTES, MANIFEST_OFFSET, MANIFEST_BYTES,
        RESIDENT_ADDRESS, resident_bytes, RESIDENT_ADDRESS, CLIENT_ADDRESS,
        CLIENT_STACK, RESIDENT_STACK, RESIDENT_BLOB_OFFSET, CLIENT_MAX_BYTES,
        0, 0,
    )
    if (header != expected or not 4 <= resident_bytes <= RESIDENT_MAX_BYTES or
            resident_bytes % 4 or
            resident_bytes + RESIDENT_BLOB_OFFSET != len(payload)):
        raise ValueError("Invalid image-probe relocation header")
    if payload[MANIFEST_OFFSET:MANIFEST_OFFSET + MANIFEST_BYTES] != bytes(MANIFEST_BYTES):
        raise ValueError("Image probe must ship with a blank card-specific manifest")
    return {
        **info,
        "resident_bytes": resident_bytes,
        "resident_address": f"0x{RESIDENT_ADDRESS:08x}",
        "client_address": f"0x{CLIENT_ADDRESS:08x}",
        "manifest_bytes": MANIFEST_BYTES,
        "resident_blob_offset": RESIDENT_BLOB_OFFSET,
        "abi": "GD BIOS vector subset v1 (selected-image probe; no retail launch)",
    }
