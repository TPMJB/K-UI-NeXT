#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only exact-owner Toy Maple completion inventory; never generates patches.

The scalar witnesses describe boundaries found in the owner's executable. They
are not a retail SDK implementation. A matching file cannot prove live ownership.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

BASE = 0x8C010000
OWNER_BYTES = 748444
OWNER_SHA256 = "ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd"
CONSUMER = 0x8C0BF37A
CONSUMER_LITERAL = 0x8C0B540C
CONSUMER_LOAD = 0x8C0B535C

# Sparse scalar witnesses, not an instruction payload or relocated SDK body.
WITNESSES = (
    (0x8C0B533E, 2, 0x400E, "completion wrapper installs its interrupt mask"),
    (0x8C0B5340, 2, 0xD231, "completion gate literal load"),
    (0x8C0B5342, 2, 0x420B, "completion gate call through R2"),
    (0x8C0B5354, 2, 0x4D11, "completion gate result must be nonnegative"),
    (0x8C0B5356, 2, 0x8901, "only success reaches consumer call"),
    (CONSUMER_LOAD, 2, 0xD32B, "consumer target loads into R3"),
    (0x8C0B535E, 2, 0x430B, "consumer call through R3"),
    (0x8C0B5360, 2, 0x0009, "consumer call delay slot"),
    (0x8C0B5408, 4, 0x8C0BF322, "gate function pointer"),
    (CONSUMER_LITERAL, 4, CONSUMER, "consumer function pointer"),
    (0x8C0BF33A, 2, 0x5036, "gate reads manager Maple base plus MDST offset"),
    (0x8C0BF33C, 2, 0xC801, "gate tests MDST busy bit"),
    (0x8C0BF33E, 2, 0x8902, "only idle reaches SDK lock"),
    (0x8C0BF34A, 2, 0x401B, "gate takes SDK TAS lock"),
    (0x8C0BF34C, 2, 0x8906, "only acquired lock reaches gate success"),
    (0x8C0BF35C, 2, 0xE000, "successful gate returns zero in R0"),
    (0x8C0BF3AC, 2, 0x420B, "main-device response consumer call"),
    (0x8C0BF414, 4, 0x8C0BFEBE, "main-device response consumer target"),
    (0x8C0BF3D2, 2, 0x430B, "subunit response consumer call"),
    (0x8C0BF418, 4, 0x8C0BFF84, "subunit response consumer target"),
    (0x8C0B5394, 2, 0x430B, "SDK ready/unlock call after response consumption"),
    (0x8C0B541C, 4, 0x8C0BF364, "SDK ready/unlock target"),
    (0x8C0B5470, 2, 0x410B, "registered application callback dispatch"),
    (0x8C0B54B6, 2, 0x400E, "completion wrapper restores interrupt mask"),
    (0x8C0B4DB6, 2, 0x11E6, "separate gun DMA start store"),
    (0x8C0B4E44, 4, 0x80000200, "separate gun DMA descriptor base control"),
    (0x8C0B4D2A, 2, 0x8B19, "gun-completion flag bypasses normal consumer"),
    (0x8C0B4F68, 4, 0x8C0B4D10, "normal interrupt callback registration"),
)


def h(value: int) -> str:
    return f"0x{value:08x}"


def read_at(data: bytes, address: int, size: int, base: int = BASE) -> int:
    offset = address - base
    if offset < 0 or size not in (2, 4) or offset + size > len(data):
        raise ValueError("Witness outside owner input")
    return int.from_bytes(data[offset:offset + size], "little")


def pc_relative_loads(data: bytes, target: int, base: int = BASE) -> list[int]:
    """All encoded MOV.L @(disp,PC),Rn references, including possible data."""
    return [base + offset for offset in range(0, len(data) - 1, 2)
            if (word := struct.unpack_from("<H", data, offset)[0]) >> 12 == 0xD
            and ((base + offset + 4) & ~3) + (word & 255) * 4 == target]


def pointer_locations(data: bytes, target: int, base: int = BASE) -> list[int]:
    return [base + offset for offset in range(0, len(data) - 3, 4)
            if struct.unpack_from("<I", data, offset)[0] == target]


def branch_locations(data: bytes, target: int, base: int = BASE) -> list[int]:
    """All encoded direct branches; a scan is not a control-flow proof."""
    found = []
    for offset in range(0, len(data) - 1, 2):
        word = struct.unpack_from("<H", data, offset)[0]
        if word >> 12 in (0xA, 0xB):
            displacement = word & 0xFFF
            if displacement & 0x800:
                displacement -= 0x1000
        elif word >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
            displacement = word & 255
            if displacement & 128:
                displacement -= 256
        else:
            continue
        if base + offset + 4 + displacement * 2 == target:
            found.append(base + offset)
    return found


def verify_identity(data: bytes, expected_bytes: int = OWNER_BYTES,
                    expected_sha256: str = OWNER_SHA256) -> None:
    if len(data) != expected_bytes or hashlib.sha256(data).hexdigest() != expected_sha256:
        raise ValueError("Requires the exact original Toy Commander 1GUTH.BIN owner input")


def analyze(data: bytes) -> dict:
    verify_identity(data)
    checks = [{"address": h(address), "bytes": size, "description": description,
               "matches": read_at(data, address, size) == value}
              for address, size, value, description in WITNESSES]
    if not all(check["matches"] for check in checks):
        raise ValueError("Exact-owner completion witness mismatch")
    loads = pc_relative_loads(data, CONSUMER_LITERAL)
    pointers = pointer_locations(data, CONSUMER)
    branches = branch_locations(data, CONSUMER)
    unique = loads == [CONSUMER_LOAD] and pointers == [CONSUMER_LITERAL] and not branches
    return {
        "schema": "kui-vmu-toy-completion-probe-v1",
        "retail_admitted": False,
        "patches_generated": False,
        "input": {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                  "crc32": h(zlib.crc32(data)), "load_address": h(BASE)},
        "witnesses": checks,
        "completion_literal": {
            "address": h(CONSUMER_LITERAL), "original_target": h(CONSUMER),
            "load_references": list(map(h, loads)),
            "target_pointer_locations": list(map(h, pointers)),
            "target_direct_branch_locations": list(map(h, branches)),
            "unique_encoded_route": unique, "call_register": "r3",
            "original_return_pr": "0x8c0b5362",
            "entry_r0": "zero after successful idle/lock gate",
            "tail_route_requirement": "Preserve PR/SR/integer state; restore original R3 target before tail jump",
        },
        "completion_order": [
            {"address": "0x8c0b533e", "action": "IMASK15 installed"},
            {"address": "0x8c0bf33a", "action": "Read MDST and require busy bit clear"},
            {"address": "0x8c0bf34a", "action": "Acquire SDK TAS lock"},
            {"address": "0x8c0b535e", "action": "Call common response consumer"},
            {"address": "0x8c0b538e", "action": "Publish consumed SDK state to public snapshot"},
            {"address": "0x8c0b5394", "action": "Set SDK ready and unlock"},
            {"address": "0x8c0b5470", "action": "Dispatch registered application callbacks"},
            {"address": "0x8c0b54b6", "action": "Restore inherited interrupt mask"},
        ],
        "receive_ownership": {
            "manager_pointer": "0x8c1223a4", "original_manager": "0x8c1212a8",
            "manager_alias": "P1: use the SDK's cached alias for current selector/table state",
            "rx_table_offset": "0x0fe8", "current_selector_offset": "0x10b4",
            "completed_bank": "1 - current selector, 24 entries of four bytes per bank",
            "rx_alias": "Table values are P2 pointers; dereference the actual table value",
            "main_consumer": "0x8c0bfebe", "unit_consumer": "0x8c0bff84",
            "consumer_coverage": "Four ports; each enabled port's main device and up to six units",
        },
        "special_dma": {
            "descriptor_control_base": "0x80000200", "start_store": "0x8c0b4db6",
            "normal_start_store": "0x8c0bf26a", "completion_flag": "0x8c118398",
            "flag_bypass": "0x8c0b4d2a", "normal_pre_hook_covers_special_start": False,
            "meaning": "Gun-mode DMA; pinned KOS maple_queue.c also emits 0x200 | (port << 16)",
            "effect": "A completion with the special flag bypasses the common normal response consumer",
        },
        "limitations": [
            "Static encoded references do not prove all dynamic control flow or runtime pointer values.",
            "The normal pre-submit hook does not cover the separate gun DMA start.",
            "No IRQ registration, callback ownership, live cache state, or response lifetime is admitted by a file hash.",
            "A passive experiment must verify MDST idle, SDK lock/ready state, normal schedule identity, and RX pointer bounds before every observation.",
            "This tool neither mutates responses nor supplies virtual VMU saves.",
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.executable.read_bytes())
    except (OSError, ValueError) as error:
        parser.exit(2, f"Completion inventory refused: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
