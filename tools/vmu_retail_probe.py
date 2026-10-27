#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inventory an owner's native Dreamcast executable without changing it.

This is a bounded SH4 *candidate* finder, not a disassembler or runtime patcher.
It follows only a small, explicitly decoded straight-line instruction subset.
Unknown instructions discard register knowledge; branches end a path after its
delay slot. A literal, or even a proven local MMIO access, is not a safe hook.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

MAX_BYTES = 0xC00000
MAX_REFERENCES = 2048
MAX_LITERALS = 2048
MAX_WINDOW = 64
REGISTER_NAMES = {
    0x005F6C00: "MAPLE_BASE",
    0x005F6C04: "MAPLE_DMA_ADDR",
    0x005F6C10: "MAPLE_DMA_TSEL",
    0x005F6C14: "MAPLE_ENABLE",
    0x005F6C18: "MAPLE_STATE",
    0x005F6C80: "MAPLE_SPEED",
    0x005F6C8C: "MAPLE_DMA_PROT",
}

# Scalar contracts already present in the independently authored Toy pilot.
# No executable instructions, SDK implementation, or game payload is bundled.
TOY_HEAP_PROFILE = {
    "name": "toy-commander-1guth-main-heap",
    "load_address": 0x8C010000,
    "bytes": 748444,
    "crc32": 0xCDC493B3,
    "sha256": "ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd",
    "witnesses": (
        (0x8C04E9B4, 4, 0x8C0B3E10, "heap initializer literal"),
        (0x8C04E9AC, 4, 0x0D000000, "startup RAM boundary"),
        (0x8C04E9B0, 4, 0x80000000, "startup alias constant"),
        (0x8C04E916, 2, 0xD327, "initializer pointer load"),
        (0x8C04E920, 2, 0x430B, "initializer indirect call"),
        (0x8C04E922, 2, 0x7420, "initializer call delay slot"),
    ),
    "allocator_address": 0x8C0B3EDE,
    "lease_bytes": 0x30000,
    "expected_lease_begin": 0x8CFD0000,
    "expected_lease_end": 0x8D000000,
}


def hex32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08x}"


def signed(value: int, bits: int) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def physical(address: int) -> int | None:
    """Accept only ordinary P0, P1 or P2 direct aliases, never P3/P4."""
    if address < 0x20000000 or 0x80000000 <= address < 0xC0000000:
        return address & 0x1FFFFFFF
    return None


def maple_register(address: int) -> str | None:
    mapped = physical(address)
    if mapped in REGISTER_NAMES:
        return REGISTER_NAMES[mapped]
    if mapped is not None and 0x005F6C00 <= mapped < 0x005F6D00:
        return "MAPLE_REGISTER_WINDOW"
    return None


def literal_kind(value: int) -> str | None:
    register = maple_register(value)
    if register:
        return register
    if physical(value) in (0x005F0000, 0x005F6000):
        return "HOLLY_BASE_CANDIDATE"
    return None


def validate_input(data: bytes, load_address: int, window: int) -> None:
    if not 2 <= len(data) <= MAX_BYTES or len(data) & 1:
        raise ValueError(f"executable must have an even length from 2 to {MAX_BYTES} bytes")
    if load_address & 3:
        raise ValueError("load address must be 4-byte aligned")
    base = physical(load_address)
    if base is None or not 0x0C010000 <= base < 0x0D000000:
        raise ValueError("load address must be a direct main-RAM alias above the boot area")
    if len(data) > 0x0D000000 - base:
        raise ValueError("executable extends past Dreamcast main RAM")
    if not 1 <= window <= MAX_WINDOW:
        raise ValueError(f"window must be from 1 to {MAX_WINDOW} instructions")


def read_at(data: bytes, base: int, address: int, size: int) -> int | None:
    offset = address - base
    if offset < 0 or size > len(data) - offset:
        return None
    return int.from_bytes(data[offset:offset + size], "little")


def trace_window(data: bytes, base: int, start: int, count: int) -> dict:
    """Follow constants along one basic block; never follow a guessed branch."""
    registers: list[int | None] = [None] * 16
    accesses, calls, words = [], [], []
    remaining_delay = None
    lost_at = []

    def memory(pc: int, access: str, width: int, address: int | None, value=None):
        if address is None or not maple_register(address):
            return
        result = {"pc": hex32(pc), "access": access, "width": width,
                  "address": hex32(address), "register": maple_register(address)}
        if value is not None:
            result["constant_value"] = hex32(value)
        accesses.append(result)

    for offset in range(start, min(len(data) - 1, start + count * 2), 2):
        pc = base + offset
        word = int.from_bytes(data[offset:offset + 2], "little")
        n, m, low = (word >> 8) & 15, (word >> 4) & 15, word & 15
        instruction = {"pc": hex32(pc), "word": f"0x{word:04x}"}
        known = True
        branch_delay = False
        stop = False
        if word & 0xF000 == 0xD000:
            address = ((pc + 4) & ~3) + (word & 255) * 4
            registers[n] = read_at(data, base, address, 4)
            instruction["operation"] = f"mov.l @(disp,pc),r{n}"
            instruction["literal_address"] = hex32(address)
        elif word & 0xF000 == 0x9000:
            value = read_at(data, base, pc + 4 + (word & 255) * 2, 2)
            registers[n] = None if value is None else signed(value, 16) & 0xFFFFFFFF
            instruction["operation"] = f"mov.w @(disp,pc),r{n}"
        elif word & 0xF000 == 0xE000:
            registers[n] = signed(word & 255, 8) & 0xFFFFFFFF
            instruction["operation"] = f"mov #imm,r{n}"
        elif word & 0xF000 == 0x7000:
            if registers[n] is not None:
                registers[n] = (registers[n] + signed(word & 255, 8)) & 0xFFFFFFFF
            instruction["operation"] = f"add #imm,r{n}"
        elif word & 0xF00F == 0x6003:
            registers[n] = registers[m]
            instruction["operation"] = f"mov r{m},r{n}"
        elif word & 0xF00F == 0x300C:
            registers[n] = (None if registers[n] is None or registers[m] is None
                            else (registers[n] + registers[m]) & 0xFFFFFFFF)
            instruction["operation"] = f"add r{m},r{n}"
        elif word & 0xF0FF in (0x4008, 0x4018, 0x4028):
            shift = {0x4008: 2, 0x4018: 8, 0x4028: 16}[word & 0xF0FF]
            if registers[n] is not None:
                registers[n] = (registers[n] << shift) & 0xFFFFFFFF
            instruction["operation"] = f"shll{shift} r{n}"
        elif word & 0xF00F in (0x600C, 0x600D):
            mask = 255 if low == 12 else 65535
            registers[n] = None if registers[m] is None else registers[m] & mask
            instruction["operation"] = f"extu.{'b' if low == 12 else 'w'} r{m},r{n}"
        elif word & 0xF00F in (0x2000, 0x2001, 0x2002, 0x2004, 0x2005, 0x2006):
            width = 1 << (low & 3)
            if low & 4 and registers[n] is not None:
                registers[n] = (registers[n] - width) & 0xFFFFFFFF
            memory(pc, "write", width, registers[n], registers[m])
            instruction["operation"] = f"store{width * 8} r{m},@r{n}"
        elif word & 0xF00F in (0x6000, 0x6001, 0x6002, 0x6004, 0x6005, 0x6006):
            width = 1 << (low & 3)
            memory(pc, "read", width, registers[m])
            if low & 4 and m != n and registers[m] is not None:
                registers[m] = (registers[m] + width) & 0xFFFFFFFF
            registers[n] = None
            instruction["operation"] = f"load{width * 8} @r{m},r{n}"
        elif word & 0xF000 in (0x1000, 0x5000):
            address_register = n if word & 0xF000 == 0x1000 else m
            address = registers[address_register]
            if address is not None:
                address = (address + low * 4) & 0xFFFFFFFF
            writing = word & 0xF000 == 0x1000
            memory(pc, "write" if writing else "read", 4, address,
                   registers[m] if writing else None)
            if not writing:
                registers[n] = None
            instruction["operation"] = "store32 displacement" if writing else "load32 displacement"
        elif word & 0xF0FF in (0x400B, 0x402B):
            target = registers[n]
            calls.append({"pc": hex32(pc), "kind": "jsr" if word & 0xF0FF == 0x400B else "jmp",
                          "target": hex32(target) if target is not None else None,
                          "register": n})
            instruction["operation"] = calls[-1]["kind"] + f" @r{n}"
            branch_delay = True
        elif word & 0xF000 in (0xA000, 0xB000):
            target = (pc + 4 + signed(word & 0xFFF, 12) * 2) & 0xFFFFFFFF
            calls.append({"pc": hex32(pc), "kind": "bsr" if word & 0xF000 == 0xB000 else "bra",
                          "target": hex32(target)})
            instruction["operation"] = calls[-1]["kind"] + " " + hex32(target)
            branch_delay = True
        elif word in (0x000B, 0x002B) or word & 0xF0FF in (0x0003, 0x0023):
            instruction["operation"] = "delayed control transfer"
            branch_delay = True
        elif word & 0xFF00 in (0x8900, 0x8B00, 0x8D00, 0x8F00):
            instruction["operation"] = "conditional branch"
            branch_delay = word & 0xFF00 in (0x8D00, 0x8F00)
            stop = not branch_delay
        elif word == 0x0009 or word & 0xF00F in (0x2008, 0x3000, 0x3002, 0x3003, 0x3006, 0x3007) or word & 0xFF00 in (0x8800, 0xC800):
            instruction["operation"] = "nop/test/compare"
        elif word & 0xF0FF in (0x0029, 0x0002, 0x0012, 0x0022, 0x0032, 0x0042, 0x003A, 0x00FA, 0x000A, 0x001A, 0x002A):
            registers[n] = None
            instruction["operation"] = f"read status/special register into r{n}"
        else:
            # An instruction which we have not decoded may write *any* known
            # register. Dropping all knowledge avoids invented MMIO proofs.
            registers = [None] * 16
            known = False
            lost_at.append(hex32(pc))
        instruction["decoded"] = known
        words.append(instruction)
        if remaining_delay is not None or stop:
            break
        if branch_delay:
            remaining_delay = 1
    return {"mmio_accesses": accesses, "calls": calls,
            "constant_knowledge_discarded_at": lost_at, "instructions": words}


def heap_evidence(data: bytes, base: int, profile=TOY_HEAP_PROFILE) -> dict:
    sha = hashlib.sha256(data).hexdigest()
    identity = (base == profile["load_address"] and len(data) == profile["bytes"]
                and zlib.crc32(data) == profile["crc32"] and sha == profile["sha256"])
    witnesses = []
    for address, size, expected, description in profile["witnesses"]:
        actual = read_at(data, base, address, size)
        witnesses.append({"address": hex32(address), "bytes": size,
                          "description": description, "matches": actual == expected})
    return {
        "profile": profile["name"], "full_identity_matches": identity,
        "all_startup_witnesses_match": all(w["matches"] for w in witnesses),
        "witnesses": witnesses,
        "allocator_address": hex32(profile["allocator_address"]),
        "lease_bytes": profile["lease_bytes"],
        "expected_lease_begin": hex32(profile["expected_lease_begin"]),
        "expected_lease_end": hex32(profile["expected_lease_end"]),
        "runtime_lease_proven": False,
        "note": "Original pilot obtains this allocation after the real heap initializer and checks live metadata. File identity alone grants no memory ownership.",
    }


def analyze(data: bytes, load_address=0x8C010000, window=24, ip: bytes | None = None) -> dict:
    validate_input(data, load_address, window)
    if ip is not None and len(ip) != 0x8000:
        raise ValueError("IP.BIN must contain the complete 32768-byte bootstrap image")
    literals = {}
    literal_count = 0
    for offset in range(0, len(data) - 3, 4):
        value = struct.unpack_from("<I", data, offset)[0]
        kind = literal_kind(value)
        if kind:
            literal_count += 1
            if len(literals) < MAX_LITERALS:
                literals[offset] = {"offset": offset, "address": hex32(load_address + offset),
                                    "value": hex32(value), "kind": kind}
    references = []
    reference_count = 0
    for offset in range(0, len(data) - 1, 2):
        word = struct.unpack_from("<H", data, offset)[0]
        if word & 0xF000 != 0xD000:
            continue
        pc = load_address + offset
        literal_offset = (((pc + 4) & ~3) + (word & 255) * 4) - load_address
        if literal_offset not in literals:
            continue
        reference_count += 1
        if len(references) < MAX_REFERENCES:
            references.append({"pc": hex32(pc), "destination_register": (word >> 8) & 15,
                               "literal": literals[literal_offset],
                               **trace_window(data, load_address, offset, window)})
    report = {
        "schema": "kui-vmu-retail-probe-v1", "retail_admitted": False,
        "patches_generated": False,
        "input": {"bytes": len(data), "load_address": hex32(load_address),
                  "sha256": hashlib.sha256(data).hexdigest(),
                  "crc32": hex32(zlib.crc32(data)), "decoding": "little-endian SH4 candidate words"},
        "literal_count": literal_count, "literals": list(literals.values()),
        "literal_output_truncated": literal_count > len(literals),
        "reference_count": reference_count, "references": references,
        "reference_output_truncated": reference_count > len(references),
        "known_heap_contract": heap_evidence(data, load_address),
        "limitations": [
            "Scanning data as possible instructions can produce false positives; no code boundaries are inferred.",
            "Only direct PC-relative longword literal references and a bounded straight-line subset are followed. Constructed addresses, overlays and other paths may be missed.",
            "Calls are nearby control-transfer candidates, not proof of Maple submission/completion callbacks.",
            "Original-image constants may be changed at runtime. IRQ, VBlank-triggered and polling consumers require live evidence.",
            "This report never admits a hook, installs code, reserves memory, or writes an input file.",
        ],
    }
    if ip is not None:
        text = lambda first, last: ip[first:last].decode("ascii", errors="replace").rstrip(" \x00")
        report["ip"] = {"bytes": len(ip), "sha256": hashlib.sha256(ip).hexdigest(),
                        "crc32": hex32(zlib.crc32(ip)), "product_number": text(0x40, 0x4A),
                        "boot_filename": text(0x60, 0x70), "title": text(0x80, 0x100)}
    return report


def read_bounded(path: Path, maximum: int) -> bytes:
    with path.open("rb") as source:
        data = source.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError(f"input exceeds {maximum} bytes: {path.name}")
    return data


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="owner-extracted, unscrambled 1ST_READ.BIN")
    parser.add_argument("--load-address", type=lambda value: int(value, 0), default=0x8C010000)
    parser.add_argument("--window", type=int, default=24, help=f"instructions per candidate, at most {MAX_WINDOW}")
    parser.add_argument("--ip", type=Path, help="optional complete owner-extracted IP.BIN")
    args = parser.parse_args(argv)
    try:
        data = read_bounded(args.executable, MAX_BYTES)
        ip = read_bounded(args.ip, 0x8000) if args.ip else None
        report = analyze(data, args.load_address, args.window, ip)
    except (OSError, ValueError) as error:
        print(f"VMU retail probe refused: {error}", file=sys.stderr)
        return 2
    json.dump(report, sys.stdout, indent=2)
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
