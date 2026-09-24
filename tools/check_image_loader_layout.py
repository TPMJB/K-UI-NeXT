#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the selected-GDI probe's independent client, resident and package."""
import argparse
import json
from pathlib import Path
import struct

from check_loader_layout import HIGH, LOW, inspect_elf, padded, region

CLIENT_LIMIT = LOW + 0x100000
RESIDENT_LIMIT = 0x8CFC0000  # First byte of the dedicated GD hook stack.
HOOK_STACK = 0x8CFD0000
RESIDENT_STACK = 0x8CFF0000
CLIENT_STACK = 0x8CD00000
VECTOR = 0x8C0000BC
MANIFEST_OFFSET = 0x1000
MANIFEST_BYTES = 65536
BLOB_OFFSET = 0x12000
MAX_RESIDENT_BYTES = 0x100000
HEADER = struct.Struct("<8s14I")
FORBIDDEN = ("_kos_", "_arch_", "_thd_", "_fs_", "_mutex_", "_sem_",
             "_sd_", "_timer_", "_irq_", "_maple_", "_pvr_", "_snd_",
             "_dcload_")


def code_symbol(image, name, base):
    address = image["symbols"].get(name, 0)
    if address % 2 or not base <= address < base + len(image["payload"]):
        raise ValueError(f"Missing independently linked executable symbol: {name}")
    return address


def check_directory(directory):
    directory = Path(directory)
    images = {
        "image_client": inspect_elf((directory / "image_client.elf").read_bytes(),
                                    LOW, CLIENT_LIMIT),
        "image_resident": inspect_elf((directory / "image_resident.elf").read_bytes(),
                                      HIGH, RESIDENT_LIMIT),
        "image_entry": inspect_elf((directory / "image_entry.elf").read_bytes(),
                                   LOW, LOW + BLOB_OFFSET + MAX_RESIDENT_BYTES),
    }
    for name, image in images.items():
        forbidden = [s for s in image["symbols"] if s.startswith(FORBIDDEN)]
        if forbidden:
            raise ValueError(f"KOS/runtime symbol in {name}: {forbidden[0]}")
    for name, base in (("image_client", LOW), ("image_resident", HIGH)):
        image = images[name]
        if padded((directory / f"{name}.bin").read_bytes()) != padded(image["payload"]):
            raise ValueError(f"{name}.bin does not match its ELF load bytes")
        syms = image["symbols"]
        start, end = syms.get("__bss_start", 0), syms.get("__bss_end", 0)
        binary_end = syms.get("__binary_end", 0)
        file_end = base + len(image["payload"])
        if (start % 4 or end % 4 or start < file_end or start > end or
                end != image["memory_end"] or binary_end > start or
                binary_end < file_end or binary_end > file_end + 3):
            raise ValueError(f"Invalid {name} BSS bounds")

    client, resident, entry = (images[n] for n in
                              ("image_client", "image_resident", "image_entry"))
    cs, rs, es = (i["symbols"] for i in (client, resident, entry))
    code_symbol(client, "_kui_image_client_main", LOW)
    call = code_symbol(client, "_kui_image_client_gd_call", LOW)
    literal = cs.get("__image_gd_vector_literal", 0)
    if (cs.get("__image_gd_vector_address") != VECTOR or literal % 4 or
            not call <= literal < call + 128 or
            region(client["payload"], literal - LOW, 4, "GD vector literal") !=
            struct.pack("<I", VECTOR)):
        raise ValueError("Independent client does not reference the GD BIOS vector")
    for name in ("_kui_image_resident_main", "_kui_image_gd_hook",
                 "_kui_image_gd_dispatch", "_kui_resident_image_read",
                 "_kui_gd_service_dispatch", "_kui_loader_sd_read"):
        code_symbol(resident, name, HIGH)
    if rs.get("__image_gd_hook_stack") != HOOK_STACK:
        raise ValueError("GD hook does not name its separate resident stack")

    client_bytes = padded((directory / "image_client.bin").read_bytes())
    resident_bytes = padded((directory / "image_resident.bin").read_bytes())
    begin, end = rs.get("__client_image_start", 0), rs.get("__client_image_end", 0)
    if (begin < HIGH or end - begin != len(client_bytes) or
            region(resident["payload"], begin - HIGH, len(client_bytes),
                   "embedded client") != client_bytes):
        raise ValueError("Resident contains an invalid or different embedded client")
    if (es.get("__loader_manifest") != LOW + MANIFEST_OFFSET or
            es.get("__resident_blob_start") != LOW + BLOB_OFFSET or
            es.get("__resident_blob_end") != LOW + len(entry["payload"]) or
            entry["memory_end"] != LOW + len(entry["payload"]) or
            entry["payload"][BLOB_OFFSET:] != resident_bytes):
        raise ValueError("Invalid packed selected-image resident/manifest layout")
    if not 0 < len(resident_bytes) <= MAX_RESIDENT_BYTES:
        raise ValueError("Resident binary exceeds entry relocation bounds")
    if any(region(entry["payload"], MANIFEST_OFFSET, MANIFEST_BYTES, "manifest")):
        raise ValueError("Packaged manifest must be empty before launch-time mapping")
    expected = HEADER.pack(b"KUIIMG01", 1, HEADER.size, MANIFEST_OFFSET,
                           MANIFEST_BYTES, HIGH, len(resident_bytes), HIGH,
                           LOW, CLIENT_STACK, RESIDENT_STACK, BLOB_OFFSET,
                           0x100000, 0, 0)
    if region(entry["payload"], 0x100, HEADER.size, "inner header") != expected:
        raise ValueError("Selected-image inner relocation header mismatch")
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
        raise SystemExit(f"Image loader layout check failed: {exc}") from exc


if __name__ == "__main__":
    main()
