#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the separate G3 freestanding image inside its runtime envelope."""
import struct
from runtime_package import verify


def inspect_probe(package):
    info = verify(package)
    payload = package[64:]
    if len(payload) < 0x2004 or len(payload) > 0x102000 or info["memory_bytes"] != len(payload):
        raise ValueError("Invalid probe staging size")
    header = struct.unpack_from("<8s14I", payload, 0x100)
    resident = header[6]
    expected = (b"KUILDR01", 1, 64, 0x1000, 1600, 0x8ce00000,
                resident, 0x8ce00000, 0x8c010000, 0x8cd00000, 0x8cff0000,
                0x2000, 0x100000, 0, 0)
    if header != expected or not resident or resident % 4 or resident + 0x2000 != len(payload):
        raise ValueError("Invalid probe relocation header")
    if payload[0x1000:0x1000+1600] != bytes(1600):
        raise ValueError("Probe must ship with a blank card-specific manifest")
    return {**info, "resident_bytes": resident, "resident_address": "0x8ce00000",
            "client_address": "0x8c010000", "abi": "K-UI probe v1 (not retail GD BIOS)"}
