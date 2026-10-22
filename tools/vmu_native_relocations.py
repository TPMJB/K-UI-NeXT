#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract reviewed native-worker pointer relocations from a linked SH ELF.

Link with --emit-relocs. The table binds the exact objcopy binary; no game
executable input is used. Unknown/external relocations are refused. Runtime
copying and relocation do not establish allocation, hook or lifetime authority.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib

EH = struct.Struct("<16sHHIIIIIHHHHHH")
PH = struct.Struct("<IIIIIIII")
SH = struct.Struct("<IIIIIIIIII")
SYM = struct.Struct("<IIIBBH")
REL = struct.Struct("<II")
RELA = struct.Struct("<IIi")
HEADER = 72
MAX_SPAN = 0x20000


def region(data, at, count):
    if at < 0 or count < 0 or at > len(data) or count > len(data) - at:
        raise ValueError("ELF range outside file")
    return data[at:at + count]


def physical(address):
    if address & 0xE0000000 not in (0, 0x80000000, 0xA0000000):
        raise ValueError("pointer does not use a direct RAM alias")
    return address & 0x1FFFFFFF


def inside(address, base, span, endpoint=False):
    try:
        offset = physical(address) - physical(base)
    except ValueError:
        return False
    return 0 <= offset <= span if endpoint else 0 <= offset < span


def validate_loads(data, binary, link_base, span_bytes, sections=None):
    """Bind virtual aliases to nonoverlapping physical loads and flat bytes.

    Production ELFs have program headers; the tiny section-only extractor
    fixtures intentionally do not. No virtual-address subtraction is used
    for the flat image: mixed cached/uncached loads share one physical span.
    """
    header = EH.unpack(region(data, 0, EH.size))
    if not header[10]:
        return []
    if header[9] != PH.size or not 0 < header[10] <= 32:
        raise ValueError("malformed worker program headers")
    if sections is None:
        sections = [SH.unpack(region(data, header[6] + i * SH.size, SH.size))
                    for i in range(header[12])]
    loads = []
    start = physical(link_base)
    for i in range(header[10]):
        kind, offset, va, pa, size, memory, flags, alignment = PH.unpack(
            region(data, header[5] + i * PH.size, PH.size))
        if kind != 1:
            continue
        first = physical(pa)
        if (not memory or size > memory or first != physical(va) or first < start or
                memory > span_bytes - (first - start) or first - start > span_bytes or
                size > len(binary) - (first - start) or first - start > len(binary) or
                flags & ~7 or not flags & 4 or alignment and alignment & (alignment - 1)):
            raise ValueError("invalid worker physical load span")
        payload = region(data, offset, size)
        if payload != binary[first - start:first - start + size]:
            raise ValueError("worker PT_LOAD bytes differ from flat physical image")
        loads.append(dict(offset=offset, virtual=va, physical=pa, first=first,
                          bytes=size, memory=memory, flags=flags, alignment=alignment))
    if not loads:
        raise ValueError("worker program headers omit load segments")
    loads.sort(key=lambda load: load["first"])
    end = start
    for load in loads:
        if load["first"] < end:
            raise ValueError("worker PT_LOAD physical ranges overlap")
        gap_end = min(load["first"] - start, len(binary))
        if any(binary[min(end - start, len(binary)):gap_end]):
            raise ValueError("flat worker has nonzero bytes between physical loads")
        end = load["first"] + load["memory"]
    if loads[0]["first"] != start or end != start + span_bytes:
        raise ValueError("worker physical loads do not bind exact allocation endpoints")
    if max(load["first"] - start + load["bytes"] for load in loads) != len(binary):
        raise ValueError("flat worker tail differs from physical initialized loads")
    for section in sections:
        if not section[2] & 2 or not section[5]:
            continue
        first, size = physical(section[3]), section[5]
        owners = [load for load in loads if load["virtual"] <= section[3] and
                  section[3] + size <= load["virtual"] + load["memory"] and
                  load["first"] <= first and first + size <= load["first"] + load["memory"]]
        if len(owners) != 1:
            raise ValueError("allocated worker section lacks unique alias-matched PT_LOAD")
        load = owners[0]
        if (section[2] & 1 and not load["flags"] & 2 or
                section[2] & 4 and not load["flags"] & 1 or
                section[1] == 1 and first + size > load["first"] + load["bytes"]):
            raise ValueError("worker section permissions/bytes disagree with its PT_LOAD")
    return loads


def generate(data, binary, link_base=0x8CFD0000, span_bytes=None):
    header = EH.unpack(region(data, 0, EH.size))
    ident = header[0]
    if (ident[:7] != b"\x7fELF\x01\x01\x01" or header[1] != 2 or
            header[2] != 42 or header[3] != 1 or header[8] != EH.size or
            header[11] != SH.size or not 0 < header[12] <= 4096):
        raise ValueError("requires a linked little-endian ELF32 SH executable")
    sections = [SH.unpack(region(data, header[6] + i * SH.size, SH.size))
                for i in range(header[12])]
    symbols = {}
    symbol_values = {}
    for index, section in enumerate(sections):
        if section[1] != 2:
            continue
        if section[9] != SYM.size or section[5] % SYM.size or section[6] >= len(sections):
            raise ValueError("malformed symbol table")
        strings = sections[section[6]]
        if strings[1] != 3:
            raise ValueError("symbol strings are not a string table")
        names = region(data, strings[4], strings[5])
        table = []
        for at in range(section[4], section[4] + section[5], SYM.size):
            symbol = SYM.unpack(region(data, at, SYM.size))
            if symbol[0] >= len(names) or b"\0" not in names[symbol[0]:]:
                raise ValueError("malformed symbol name")
            name = bytes(names[symbol[0]:].split(b"\0", 1)[0])
            table.append(symbol)
            # Local symbols are scoped to their translation unit. Different
            # C files can legitimately define their own static ram()/read()
            # helpers. Relocations still bind by table INDEX below; only
            # externally visible names share a namespace. The end symbol is
            # retained for the older explicit-span inference fixture too.
            if name and (symbol[3] >> 4 or name == b"__vmu_observe_worker_end"):
                if name in symbol_values and symbol_values[name] != symbol[1]:
                    raise ValueError("conflicting symbol values")
                symbol_values[name] = symbol[1]
        symbols[index] = table
    if span_bytes is None:
        end = symbol_values.get(b"__vmu_observe_worker_end")
        if end is None:
            raise ValueError("missing worker-end symbol; supply --span-bytes explicitly")
        span_bytes = physical(end) - physical(link_base)
    if (not 0x8C000000 <= link_base < 0x8D000000 or link_base & 31 or
            not 0 < span_bytes <= MAX_SPAN or span_bytes & 31 or
            span_bytes > 0x8D000000 - link_base or not 0 < len(binary) <= span_bytes):
        raise ValueError("invalid worker binary or linked RAM span")
    validate_loads(data, binary, link_base, span_bytes, sections)
    covered = bytearray(len(binary))
    occupied = bytearray(span_bytes)
    for section in sections:
        if not section[2] & 2 or not section[5]:
            continue
        if (section[1] not in (1, 8) or not inside(section[3], link_base, span_bytes) or
                section[5] > span_bytes - (physical(section[3]) - physical(link_base))):
            raise ValueError("allocated section is outside the worker span")
        offset = physical(section[3]) - physical(link_base)
        if any(occupied[offset:offset + section[5]]):
            raise ValueError("allocated sections physically overlap")
        occupied[offset:offset + section[5]] = b"\1" * section[5]
        if section[1] == 8:
            continue
        payload = region(data, section[4], section[5])
        if offset + len(payload) > len(binary) or binary[offset:offset + len(payload)] != payload:
            raise ValueError("ELF initialized section disagrees with worker binary")
        if any(covered[offset:offset + len(payload)]):
            raise ValueError("allocated sections physically overlap")
        covered[offset:offset + len(payload)] = b"\1" * len(payload)
    if not covered or not covered[0] or not covered[-1] or any(b and not c for b, c in zip(binary, covered)):
        raise ValueError("binary includes nonzero bytes outside initialized ELF sections")
    records = {}
    relocation_sections = 0
    relative_kinds = {2, 3, 4, 5, 6}  # REL32 and reviewed PC-relative SH branches/loads
    for section in sections:
        if section[1] not in (4, 9):
            continue
        if section[7] >= len(sections):
            raise ValueError("invalid relocation target section")
        target = sections[section[7]]
        if not target[2] & 2:
            continue  # debug-only relocations are never loaded
        if target[1] != 1:
            raise ValueError("relocation target is not initialized worker bytes")
        relocation_sections += 1
        codec = RELA if section[1] == 4 else REL
        if section[9] != codec.size or section[5] % codec.size or section[6] not in symbols:
            raise ValueError("malformed relocation table")
        table = symbols[section[6]]
        for at in range(section[4], section[4] + section[5], codec.size):
            relocation = codec.unpack(region(data, at, codec.size))
            address, info = relocation[:2]
            kind, sym = info & 255, info >> 8
            if kind == 0:
                continue
            if sym >= len(table) or table[sym][5] == 0 or not inside(table[sym][1], link_base, span_bytes, True):
                raise ValueError("external or unresolved worker relocation")
            if kind != 1 and kind not in relative_kinds:
                raise ValueError(f"unsupported SH relocation type {kind}")
            width = 4 if kind in (1, 2) else 2
            if not target[3] <= address <= target[3] + target[5] - width:
                raise ValueError("relocation outside its initialized section")
            offset = physical(address) - physical(link_base)
            if offset < 0 or offset + width > len(binary):
                raise ValueError("relocation outside worker binary")
            if kind != 1:
                continue  # moving both internal endpoints leaves displacement unchanged
            value = struct.unpack_from("<I", binary, offset)[0]
            if offset & 3 or not inside(value, link_base, span_bytes, True) or offset in records:
                raise ValueError("invalid or duplicate internal pointer relocation")
            records[offset] = value
    if not relocation_sections:
        raise ValueError("missing relocation sections; link worker with --emit-relocs")
    for offset in range(0, len(binary) - 3, 4):
        value = struct.unpack_from("<I", binary, offset)[0]
        if inside(value, link_base, span_bytes, True) and records.get(offset) != value:
            raise ValueError(f"unannotated internal address at binary offset0x{offset:x}")
    wire = bytearray(HEADER + 8 * len(records))
    wire[:8] = b"KUIVRL01"
    struct.pack_into("<8I", wire, 8, 1, HEADER, len(binary), span_bytes,
                     link_base, len(records), len(wire), 0)
    wire[40:72] = hashlib.sha256(binary).digest()
    for index, (offset, value) in enumerate(sorted(records.items())):
        struct.pack_into("<II", wire, HEADER + index * 8, offset, value)
    struct.pack_into("<I", wire, 36, zlib.crc32(wire))
    return bytes(wire)


def self_test():
    base = 0x8CFD0000
    binary = struct.pack("<4I", base, 0xACFD0020, 0x0CFD0040, 0xA05F6C00)
    names = b"\0__vmu_observe_worker_end\0text\0private\0"
    symbols = b"".join(SYM.pack(*s) for s in [
        (0, 0, 0, 0, 0, 0), (names.index(b"text"), base, 0, 0, 0, 1),
        (names.index(b"private"), 0xACFD0020, 0, 0, 0, 2),
        (1, base + 64, 0, 0, 0, 0xFFF1)])
    relocs = b"".join(RELA.pack(base + i * 4, ((i + 1) << 8) | 1, 0) for i in range(3))
    data = bytearray(EH.size)
    chunks = []
    for chunk in (binary, names, symbols, relocs):
        chunks.append((len(data), len(chunk)));data.extend(chunk)
    offset = len(data)
    sections = [(0,) * 10,
        (0, 1, 6, base, *chunks[0], 0, 0, 4, 0),
        (0, 8, 3, base + 16, 0, 48, 0, 0, 4, 0),
        (0, 3, 0, 0, *chunks[1], 0, 0, 1, 0),
        (0, 2, 0, 0, *chunks[2], 3, 0, 4, SYM.size),
        (0, 4, 0, 0, *chunks[3], 4, 1, 4, RELA.size)]
    data.extend(b"".join(SH.pack(*s) for s in sections))
    EH.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + b"\0" * 9, 2, 42, 1,
                 base, 0, offset, 0, EH.size, 0, 0, SH.size, len(sections), 0)
    wire = generate(data, binary)
    assert len(wire) == 96 and struct.unpack_from("<I", wire, 28)[0] == 3
    checks = 1
    def refused(elf, blob):
        nonlocal checks
        try:
            generate(elf, blob)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("bad ELF accepted")
    changed = bytearray(data);changed[chunks[3][0] + 4] = 99;refused(changed, binary)
    changed = bytearray(data);struct.pack_into("<I", changed, chunks[3][0] + RELA.size, base);refused(changed, binary)
    changed = bytearray(data);struct.pack_into("<I", changed, chunks[2][0] + SYM.size + 4, 0xA05F6C00);refused(changed, binary)
    changed = bytearray(data);struct.pack_into("<I", changed, chunks[3][0] + 4, 0);refused(changed, binary)
    changed = bytearray(binary);changed[-1] ^= 1;refused(data, changed)
    changed = bytearray(data);struct.pack_into("<I", changed, offset + 5 * SH.size + 4, 1);refused(changed, binary)
    # Two same-named local functions at distinct addresses are valid; both
    # relocation indices must remain independently bound to their symbols.
    changed = bytearray(data)
    struct.pack_into("<I", changed, chunks[2][0] + 2 * SYM.size, names.index(b"text"))
    assert generate(changed, binary) == wire
    checks += 1
    # The equivalent pair of global definitions remains a conflict.
    changed[chunks[2][0] + SYM.size + 12] = 0x10
    changed[chunks[2][0] + 2 * SYM.size + 12] = 0x10
    refused(changed, binary)
    # A mixed-alias production-style flat image: immutable P2 header,
    # cached P1 code, uncached P2 initialized/BSS state. The extractor must
    # reason about physical adjacency rather than the large VMA alias gaps.
    mixed_binary = bytes([0x31] * 128)
    payload_offset = EH.size + 3 * PH.size
    mixed = bytearray(payload_offset) + bytearray(mixed_binary)
    EH.pack_into(mixed, 0, b"\x7fELF\x01\x01\x01" + b"\0" * 9, 2, 42, 1,
                 base + 32, EH.size, 0, 0, EH.size, PH.size, 3, SH.size, 0, 0)
    headers = [(1, payload_offset, base + 0x20000000, base, 32, 32, 4, 4),
               (1, payload_offset + 32, base + 32, base + 32, 64, 64, 5, 4),
               (1, payload_offset + 96, base + 0x20000000 + 96, base + 96, 32, 160, 6, 4)]
    for i, program in enumerate(headers): PH.pack_into(mixed, EH.size + i * PH.size, *program)
    allocated = [(0, 1, 2, base + 0x20000000, payload_offset, 32, 0, 0, 4, 0),
                 (0, 1, 6, base + 32, payload_offset + 32, 64, 0, 0, 4, 0),
                 (0, 1, 3, base + 0x20000000 + 96, payload_offset + 96, 32, 0, 0, 4, 0),
                 (0, 8, 3, base + 0x20000000 + 128, 0, 128, 0, 0, 4, 0)]
    assert len(validate_loads(mixed, mixed_binary, base, 256, allocated)) == 3
    checks += 1
    def load_refused(changed, blob=mixed_binary, sections=allocated):
        nonlocal checks
        try:
            validate_loads(changed, blob, base, 256, sections)
        except ValueError:
            checks += 1
        else:
            raise AssertionError("bad mixed worker physical loads accepted")
    changed = bytearray(mixed)
    struct.pack_into("<I", changed, EH.size + PH.size + 12, base + 36)
    load_refused(changed)  # VMA/LMA physical addresses differ.
    changed = bytearray(mixed)
    struct.pack_into("<I", changed, EH.size + 2 * PH.size + 8, base + 0x20000000 + 64)
    struct.pack_into("<I", changed, EH.size + 2 * PH.size + 12, base + 64)
    load_refused(changed)  # Aliases overlap the cached code physically.
    changed = bytearray(mixed)
    struct.pack_into("<I", changed, EH.size + 2 * PH.size + 24, 4)
    load_refused(changed)  # Mutable section inside a read-only load.
    changed = bytearray(mixed)
    struct.pack_into("<II", changed, EH.size + 16, 24, 24)
    load_refused(changed)  # Nonzero flat bytes in an unowned physical gap.
    changed = bytearray(mixed)
    struct.pack_into("<H", changed, 44, 2)
    load_refused(changed)  # Missing state load/allocation endpoint.
    load_refused(mixed, sections=allocated[:2] +
                 [(0, 1, 3, base + 96, payload_offset + 96, 32, 0, 0, 4, 0)] + allocated[3:])
    print(f"Native relocation ELF extraction: {checks} checks passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", nargs="?", type=Path)
    parser.add_argument("binary", nargs="?", type=Path)
    parser.add_argument("output", nargs="?", type=Path)
    parser.add_argument("--link-base", type=lambda v: int(v, 0), default=0x8CFD0000)
    parser.add_argument("--span-bytes", type=lambda v: int(v, 0))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test();return
    if not args.elf or not args.binary or not args.output:
        parser.error("ELF, binary and output are required")
    try:
        wire = generate(args.elf.read_bytes(), args.binary.read_bytes(), args.link_base, args.span_bytes)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(wire)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(2, f"Native relocation extraction refused: {error}\n")
    print(f"Native worker relocation table: {(len(wire) - HEADER) // 8} pointers, {len(wire)} bytes")


if __name__ == "__main__":
    main()
