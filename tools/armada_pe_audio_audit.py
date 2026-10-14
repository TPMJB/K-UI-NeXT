#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read bounded SH4 PE metadata from privately supplied Armada runtime files.

The output contains hashes, section locations, and API metadata only. It does
not extract, distribute, or patch executable bytes. Nominal image addresses are
not proof of the addresses chosen by the Windows CE loader.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


MAX_FILE_BYTES = 16 * 1024 * 1024
MAX_SECTIONS = 32
MAX_SYMBOLS = 4096
MAX_DESCRIPTORS = 128
MAX_NAME_BYTES = 256


class PEError(ValueError):
    pass


class PE:
    def __init__(self, path):
        self.path = Path(path)
        size = self.path.stat().st_size
        if size > MAX_FILE_BYTES:
            raise PEError("PE file exceeds the 16 MiB audit limit")
        self.data = self.path.read_bytes()
        if self.data[:2] != b"MZ":
            raise PEError("File is not a DOS/PE image")
        pe = self.u32(0x3c)
        if pe > 1024 * 1024 or self.read(pe, 4) != b"PE\0\0":
            raise PEError("Invalid bounded PE header")
        machine, count = struct.unpack("<HH", self.read(pe + 4, 4))
        if machine != 0x1a6 or not 1 <= count <= MAX_SECTIONS:
            raise PEError("Expected SH4 PE with 1..32 sections")
        optional_bytes = self.u16(pe + 20)
        self.coff_characteristics = self.u16(pe + 22)
        if not 224 <= optional_bytes <= 512:
            raise PEError("Invalid PE32 optional header size")
        optional = pe + 24
        self.read(optional, optional_bytes)
        if self.u16(optional) != 0x10b:
            raise PEError("Expected PE32 optional header")
        self.image_base = self.u32(optional + 28)
        self.entry_rva = self.u32(optional + 16)
        self.header_bytes = self.u32(optional + 60)
        self.image_bytes = self.u32(optional + 56)
        self.sections = []
        for index in range(count):
            offset = optional + optional_bytes + 40 * index
            section = self.read(offset, 40)
            name = section[:8].split(b"\0", 1)[0].decode("ascii", "strict")
            virtual_bytes, rva, raw_bytes, raw_offset = struct.unpack_from("<IIII", section, 8)
            characteristics = struct.unpack_from("<I", section, 36)[0]
            self.read(raw_offset, raw_bytes)
            self.sections.append(dict(name=name, rva=rva, virtual_bytes=virtual_bytes,
                                      raw_bytes=raw_bytes, raw_offset=raw_offset,
                                      characteristics=characteristics))
        directory_count = self.u32(optional + 92)
        if directory_count > (optional_bytes - 96) // 8:
            raise PEError("PE directory table exceeds its header")
        self.directories = [struct.unpack("<II", self.read(optional + 96 + i * 8, 8))
                            for i in range(directory_count)]

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise PEError("Read exceeds file-backed PE bytes")
        return self.data[offset:offset + size]

    def u16(self, offset):
        return struct.unpack("<H", self.read(offset, 2))[0]

    def u32(self, offset):
        return struct.unpack("<I", self.read(offset, 4))[0]

    def rva_offset(self, rva, size=1):
        if 0 <= rva and rva + size <= min(self.header_bytes, len(self.data)):
            return rva
        matches = [s for s in self.sections if s["rva"] <= rva
                   and rva + size <= s["rva"] + s["raw_bytes"]]
        if len(matches) != 1:
            raise PEError("RVA does not identify one file-backed section")
        return matches[0]["raw_offset"] + rva - matches[0]["rva"]

    def rva_u32(self, rva):
        return self.u32(self.rva_offset(rva, 4))

    def string(self, rva):
        values = bytearray()
        for i in range(MAX_NAME_BYTES):
            value = self.data[self.rva_offset(rva + i)]
            if value == 0:
                return values.decode("ascii", "strict")
            values.append(value)
        raise PEError("PE name exceeds the bounded string limit")

    def exports(self):
        if not self.directories or not self.directories[0][0]:
            return []
        rva, size = self.directories[0]
        fields = struct.unpack("<IIHHIIIIIII", self.read(self.rva_offset(rva, 40), 40))
        _, _, _, _, _, base, count, named_count, functions, names, ordinals = fields
        if count > MAX_SYMBOLS or named_count > count:
            raise PEError("Export table exceeds audit limit")
        named = {}
        for i in range(named_count):
            index = self.u16(self.rva_offset(ordinals + i * 2, 2))
            if index >= count:
                raise PEError("Export name has an invalid ordinal index")
            named[index] = self.string(self.rva_u32(names + i * 4))
        result = []
        for i in range(count):
            target = self.rva_u32(functions + i * 4)
            if target:
                item = dict(ordinal=base + i, name=named.get(i), rva=target,
                            nominal_va=self.image_base + target)
                if rva <= target < rva + size:
                    item["forwarder"] = self.string(target)
                result.append(item)
        return result

    def imports(self):
        if len(self.directories) < 2 or not self.directories[1][0]:
            return []
        rva, size = self.directories[1]
        result = []
        terminated = False
        for index in range(min(MAX_DESCRIPTORS, size // 20)):
            original, timestamp, chain, name_rva, first = struct.unpack(
                "<IIIII", self.read(self.rva_offset(rva + index * 20, 20), 20))
            if not any((original, timestamp, chain, name_rva, first)):
                terminated = True
                break
            if not name_rva or not first:
                raise PEError("Incomplete import descriptor")
            dll = self.string(name_rva)
            lookup = original or first
            symbols = []
            for i in range(MAX_SYMBOLS):
                value = self.rva_u32(lookup + 4 * i)
                if not value:
                    break
                symbol = dict(iat_rva=first + 4 * i,
                              iat_nominal_va=self.image_base + first + 4 * i)
                if value & 0x80000000:
                    symbol["ordinal"] = value & 0xffff
                else:
                    symbol["hint"] = self.u16(self.rva_offset(value, 2))
                    symbol["name"] = self.string(value + 2)
                symbols.append(symbol)
            else:
                raise PEError("Import thunk table exceeds audit limit")
            result.append(dict(dll=dll, symbols=symbols))
        if not terminated:
            raise PEError("Import descriptors lack a bounded terminator")
        return result

    def metadata(self):
        return dict(file=self.path.name, bytes=len(self.data),
                    sha256=hashlib.sha256(self.data).hexdigest(), machine="SH4 (0x01a6)",
                    image_base=self.image_base, image_bytes=self.image_bytes,
                    entry_rva=self.entry_rva, sections=self.sections,
                    imports=self.imports(), exports=self.exports())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, help="Metadata JSON destination; stdout by default")
    args = parser.parse_args()
    try:
        result = {"format": "armada-sh4-pe-audit-v1", "files": [PE(p).metadata() for p in args.files]}
        encoded = json.dumps(result, indent=2) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
    except (OSError, PEError, UnicodeError) as error:
        parser.exit(1, "Audit failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()
