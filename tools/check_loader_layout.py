#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the three freestanding ELF images and their packed relocation bytes."""
import argparse
import json
from pathlib import Path
import struct

EH = struct.Struct("<16sHHIIIIIHHHHHH")
PH = struct.Struct("<8I")
SH = struct.Struct("<10I")
SYM = struct.Struct("<IIIBBH")
LOW = 0x8C010000
HIGH = 0x8CE00000


def region(data, offset, count, label):
    if offset < 0 or count < 0 or offset > len(data) or count > len(data) - offset:
        raise ValueError(f"Truncated {label}")
    return data[offset:offset + count]


def inspect_elf(data, base, limit):
    if len(data) < EH.size:
        raise ValueError("Truncated ELF header")
    ident, kind, machine, version, entry, po, so, _, es, ps, pn, ss, sn, _ = EH.unpack_from(data)
    if (ident[:7] != b"\x7fELF\x01\x01\x01" or kind != 2 or machine != 42 or
            version != 1 or entry != base or es != EH.size or ps != PH.size or
            ss != SH.size or not 0 < pn <= 32 or not 0 < sn <= 4096):
        raise ValueError("Requires a static little-endian SH ELF at the fixed entry")
    region(data, po, pn * ps, "program headers")
    region(data, so, sn * ss, "section headers")
    segments = []
    for index in range(pn):
        kind, offset, va, pa, size, memory, flags, _ = PH.unpack_from(data, po + index * ps)
        if kind in (2, 3, 7):
            raise ValueError("Dynamic, interpreted, or TLS image is not freestanding")
        if kind != 1:
            continue
        if (pa != va or va < base or memory == 0 or size > memory or
                va + memory > limit):
            raise ValueError("Load segment is outside its memory reservation")
        content = region(data, offset, size, "load segment")
        segments.append((va, memory, flags, content))
    segments.sort()
    if not segments or segments[0][0] != base or not segments[0][2] & 1 or len(segments[0][3]) < 4:
        raise ValueError("Entry is not the beginning of executable bytes")
    for previous, current in zip(segments, segments[1:]):
        if previous[0] + previous[1] > current[0]:
            raise ValueError("Overlapping load segments")
    file_end = max(va + len(content) for va, _, _, content in segments)
    payload = bytearray(file_end - base)
    for va, _, _, content in segments:
        payload[va - base:va - base + len(content)] = content
    sections = [SH.unpack_from(data, so + index * ss) for index in range(sn)]
    symbols = {}
    found_symbols = False
    for section in sections:
        _, kind, flags, address, offset, size, link, _, _, stride = section
        if flags & 0x400:
            raise ValueError("TLS section is not allowed")
        if flags & 2 and size and (address < base or address + size > limit):
            raise ValueError("Allocated section is outside its reservation")
        if kind != 2:
            continue
        found_symbols = True
        if stride != SYM.size or size % stride or link >= sn or sections[link][1] != 3:
            raise ValueError("Invalid symbol table")
        region(data, offset, size, "symbol table")
        strings = region(data, sections[link][4], sections[link][5], "symbol strings")
        for at in range(offset, offset + size, stride):
            name, value, _, _, _, index = SYM.unpack_from(data, at)
            if name >= len(strings):
                raise ValueError("Invalid symbol name")
            end = strings.find(b"\0", name)
            if end < 0:
                raise ValueError("Unterminated symbol name")
            label = strings[name:end].decode("ascii", errors="strict")
            if not label:
                continue
            if index == 0:
                raise ValueError(f"Unresolved freestanding symbol: {label}")
            if index == 0xFFF2:
                raise ValueError(f"Unallocated common symbol: {label}")
            if index < 0xFF00 and index >= sn:
                raise ValueError("Symbol refers to a missing section")
            symbols[label] = value
    if not found_symbols or symbols.get("_start") != base:
        raise ValueError("Missing fixed entry symbol")
    return {"payload": bytes(payload), "symbols": symbols,
            "memory_end": max(va + memory for va, memory, _, _ in segments)}


def padded(data):
    return data + bytes(-len(data) % 4)


def check_directory(directory):
    directory = Path(directory)
    images = {
        "client": inspect_elf((directory / "client.elf").read_bytes(), LOW, LOW + 0x100000),
        "resident": inspect_elf((directory / "resident.elf").read_bytes(), HIGH, 0x8CFE0000),
        "entry": inspect_elf((directory / "entry.elf").read_bytes(), LOW, LOW + 0x102000),
    }
    for name in ("client", "resident"):
        image = images[name]
        binary = (directory / f"{name}.bin").read_bytes()
        if padded(binary) != padded(image["payload"]):
            raise ValueError(f"{name}.bin does not match its ELF load bytes")
        syms = image["symbols"]
        if ("__bss_start" not in syms or "__bss_end" not in syms or
                syms["__bss_start"] % 4 or syms["__bss_end"] % 4 or
                syms["__bss_start"] < (LOW if name == "client" else HIGH) + len(image["payload"]) or
                syms["__bss_end"] != image["memory_end"]):
            raise ValueError(f"Invalid {name} BSS bounds")
    resident = images["resident"]
    entry = images["entry"]
    rs = resident["symbols"]
    es = entry["symbols"]
    client_bytes = padded((directory / "client.bin").read_bytes())
    resident_bytes = padded((directory / "resident.bin").read_bytes())
    if (rs.get("__client_image_start", 0) < HIGH or
            rs.get("__client_image_end", 0) - rs.get("__client_image_start", 0) != len(client_bytes)):
        raise ValueError("Invalid embedded client bounds")
    start = rs["__client_image_start"] - HIGH
    if resident["payload"][start:start + len(client_bytes)] != client_bytes:
        raise ValueError("Resident contains different client bytes")
    if (es.get("__loader_manifest") != LOW + 0x1000 or
            es.get("__resident_blob_start") != LOW + 0x2000 or
            es.get("__resident_blob_end") != LOW + len(entry["payload"]) or
            entry["payload"][0x2000:] != resident_bytes or
            entry["memory_end"] != LOW + len(entry["payload"])):
        raise ValueError("Invalid packed resident/manifest layout")
    if len(resident_bytes) > 0x100000:
        raise ValueError("Resident binary exceeds entry relocation bounds")
    # Imports would already have failed above. These entry definitions also
    # catch accidental linkage of a startup with the wrong C symbol prefix.
    if not LOW <= images["client"]["symbols"].get("_kui_probe_client_main", 0) < LOW + 0x100000:
        raise ValueError("Missing independently linked client entry")
    if not HIGH <= rs.get("_kui_loader_resident_main", 0) < 0x8CFE0000:
        raise ValueError("Missing resident C entry")
    return {name: {"payload_bytes": len(image["payload"]),
                   "memory_end": f"0x{image['memory_end']:08x}",
                   "unresolved_symbols": 0} for name, image in images.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build/loader")
    args = parser.parse_args()
    try:
        print(json.dumps(check_directory(args.directory), sort_keys=True))
    except (ValueError, OSError, UnicodeError, struct.error) as exc:
        raise SystemExit(f"Loader layout check failed: {exc}") from exc


if __name__ == "__main__":
    main()
