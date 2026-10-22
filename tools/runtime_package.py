#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/inspect the version-1 SD runtime envelope; no third-party packages."""
import argparse
import json
from pathlib import Path
import re
import struct
import zlib

MAGIC = b"KUIRUN1\0"
ADDRESS = 0x8C010000
MAX_BYTES = 4 * 1024 * 1024
MAX_MEMORY = 8 * 1024 * 1024
ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIIIIIII")


def flatten_elf(data):
    if len(data) < ELF_HEADER.size:
        raise ValueError("ELF header is truncated")
    ident, kind, machine, version, entry, phoff, _, _, ehsize, phsize, phnum, *_ = ELF_HEADER.unpack_from(data)
    if (ident[:7] != b"\x7fELF\x01\x01\x01" or kind != 2 or machine != 42 or
            version != 1 or entry != ADDRESS or ehsize != ELF_HEADER.size or
            phsize != PROGRAM_HEADER.size or not 0 < phnum <= 128 or
            phoff < ELF_HEADER.size or phoff + phsize * phnum > len(data)):
        raise ValueError("Requires a static little-endian SH executable at 0x8c010000")
    segments = []
    for i in range(phnum):
        ptype, offset, vaddr, paddr, filesz, memsz, flags, _ = PROGRAM_HEADER.unpack_from(data, phoff + i * phsize)
        if ptype in (2, 3):
            raise ValueError("Dynamic/interpreted executables are unsupported")
        if ptype != 1:
            continue
        if (paddr != vaddr or vaddr < ADDRESS or filesz > memsz or not memsz or
                vaddr + memsz > ADDRESS + MAX_MEMORY or offset + filesz > len(data)):
            raise ValueError("Invalid ELF load segment")
        segments.append((vaddr, offset, filesz, memsz, flags))
    segments.sort()
    if not segments or segments[0][0] != ADDRESS or not segments[0][4] & 1 or segments[0][2] < 4:
        raise ValueError("Entry is not the beginning of an executable load segment")
    for left, right in zip(segments, segments[1:]):
        if left[0] + left[3] > right[0]:
            raise ValueError("Overlapping ELF load segments")
    file_end = max(address + size for address, _, size, _, _ in segments if size)
    payload_size = (file_end - ADDRESS + 3) & ~3
    memory_size = (max(address + size for address, _, _, size, _ in segments) - ADDRESS + 3) & ~3
    if payload_size > MAX_BYTES:
        raise ValueError("Runtime payload exceeds the bootstrap's 4 MiB limit")
    payload = bytearray(payload_size)
    for address, offset, size, _, _ in segments:
        payload[address - ADDRESS:address - ADDRESS + size] = data[offset:offset + size]
    return bytes(payload), memory_size


def envelope(payload, memory_size, build):
    if not re.fullmatch(r"[0-9a-f]{12}", build):
        raise ValueError("Build identifier must be 12 lowercase hexadecimal characters")
    if not 4 <= len(payload) <= MAX_BYTES or len(payload) % 4:
        raise ValueError("Payload must contain 4-byte-aligned SH instructions")
    if not len(payload) <= memory_size <= MAX_MEMORY or memory_size % 4:
        raise ValueError("Invalid memory footprint")
    header = bytearray(64)
    header[:8] = MAGIC
    struct.pack_into("<8I", header, 8, 1, 64, len(payload), ADDRESS, ADDRESS,
                     memory_size, zlib.crc32(payload), 0)
    header[40:52] = build.encode("ascii")
    struct.pack_into("<I", header, 60, zlib.crc32(header[:60]))
    return bytes(header) + payload


def verify(data):
    if len(data) < 64 or data[:8] != MAGIC:
        raise ValueError("Invalid or truncated runtime header")
    version, header_size, size, address, entry, memory, crc, flags = struct.unpack_from("<8I", data, 8)
    if (header_size != 64 or flags or data[52:60] != bytes(8) or
            not re.fullmatch(rb"[0-9a-f]{12}", data[40:52])):
        raise ValueError("Invalid runtime header fields")
    if zlib.crc32(data[:60]) != struct.unpack_from("<I", data, 60)[0]:
        raise ValueError("Header checksum mismatch")
    if version != 1 or address != ADDRESS or entry != ADDRESS:
        raise ValueError("Unsupported runtime version or address")
    if (not 4 <= size <= MAX_BYTES or size % 4 or len(data) != 64 + size or
            not size <= memory <= MAX_MEMORY or memory % 4):
        raise ValueError("Invalid runtime size")
    if zlib.crc32(data[64:]) != crc:
        raise ValueError("Payload checksum mismatch")
    return {"format": version, "build": data[40:52].decode("ascii"),
            "payload_bytes": size, "memory_bytes": memory, "crc32": f"{crc:08x}"}


def rejection_cases(good):
    verify(good)
    bad_magic = bytearray(good)
    bad_magic[0] ^= 1
    bad_payload = bytearray(good)
    bad_payload[-1] ^= 1
    wrong_version = bytearray(good)
    struct.pack_into("<I", wrong_version, 8, 2)
    struct.pack_into("<I", wrong_version, 60, zlib.crc32(wrong_version[:60]))
    oversized = bytearray(good[:64])
    struct.pack_into("<I", oversized, 16, MAX_BYTES + 4)
    struct.pack_into("<I", oversized, 60, zlib.crc32(oversized[:60]))
    return {"bad-magic.kui": bytes(bad_magic), "bad-checksum.kui": bytes(bad_payload),
            "truncated.kui": good[:-1], "wrong-version.kui": bytes(wrong_version),
            "oversized.kui": bytes(oversized)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inspect = sub.add_parser("verify")
    inspect.add_argument("package", type=Path)
    build = sub.add_parser("build")
    build.add_argument("elf", type=Path)
    build.add_argument("output", type=Path)
    build.add_argument("--build-id", required=True)
    args = parser.parse_args()
    try:
        if args.command == "build":
            payload, memory = flatten_elf(args.elf.read_bytes())
            data = envelope(payload, memory, args.build_id)
            args.output.write_bytes(data)
        else:
            if args.package.stat().st_size > MAX_BYTES + 64:
                raise ValueError("Oversized runtime package")
            data = args.package.read_bytes()
        print(json.dumps(verify(data), indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
