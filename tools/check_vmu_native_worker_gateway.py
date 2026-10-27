#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the P2 worker artifacts and execute the actual stage-linked gateway.

The closed integer model substitutes an ABI-compliant adversary for the worker
body. Static and host checks do not establish console execution or game hooks.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from test_armada_ce_maple_hook import CPU, MASK
from check_loader_layout import EH, PH, SH, SYM, inspect_elf, region
from check_retail_instructions import audit as instruction_audit
from vmu_native_relocations import generate

P1, P2, SPAN = 0x8C100000, 0xAC100000, 0x10000
ENTRY, TOP, CONFIG, RESULT = 0xAC38A8C0, 0xAC39A880, 0x8CE80000, 0x8CE80100


def read_elf(path):
    data = Path(path).read_bytes()
    h = EH.unpack(region(data, 0, EH.size, "ELF header"))
    if (h[0][:7] != b"\x7fELF\x01\x01\x01" or h[1:4] != (2, 42, 1) or
            h[8] != EH.size or h[9] != PH.size or h[11] != SH.size or
            not 0 < h[10] <= 32 or not 0 < h[12] <= 4096):
        raise ValueError("requires bounded static little-endian SH ELF")
    segments = [PH.unpack(region(data, h[5] + i * PH.size, PH.size, "program header")) for i in range(h[10])]
    sections = [SH.unpack(region(data, h[6] + i * SH.size, SH.size, "section header")) for i in range(h[12])]
    symbols = {}
    for s in sections:
        if s[1] in (6, 11) or s[2] & 0x400:
            raise ValueError("dynamic or TLS sections are forbidden")
        if s[1] != 2:
            continue
        if s[9] != SYM.size or s[5] % SYM.size or s[6] >= len(sections):
            raise ValueError("malformed symbol table")
        strings = sections[s[6]]
        if strings[1] != 3:
            raise ValueError("malformed symbol strings")
        names = region(data, strings[4], strings[5], "symbol names")
        for at in range(s[4], s[4] + s[5], SYM.size):
            name, value, size, info, _, owner = SYM.unpack(region(data, at, SYM.size, "symbol"))
            if name >= len(names) or b"\0" not in names[name:]:
                raise ValueError("malformed symbol name")
            name = names[name:].split(b"\0", 1)[0].decode("ascii")
            if not name:
                continue
            if owner in (0, 0xFFF2) or (owner < 0xFF00 and owner >= len(sections)):
                raise ValueError("undefined or unallocated symbol: " + name)
            record = value, size, info & 15, owner
            if name in symbols and symbols[name] != record and info >> 4:
                raise ValueError("conflicting global symbols: " + name)
            symbols[name] = record
    if not symbols:
        raise ValueError("missing symbol table")
    return data, h, segments, sections, symbols


def symbol_bytes(image, name, kind):
    data, _, _, sections, symbols = image
    value, size, actual_kind, owner = symbols[name]
    if owner >= len(sections) or actual_kind != kind:
        raise ValueError("unowned or incorrectly typed symbol: " + name)
    section = sections[owner]
    if (section[1] != 1 or not section[2] & 2 or not size or
            not section[3] <= value < value + size <= section[3] + section[5]):
        raise ValueError("unbounded initialized symbol: " + name)
    return value, region(data, section[4] + value - section[3], size, name), section


def literal_aware_instructions(disassembly):
    decoded, literals = [], set()
    for line in disassembly.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+(\S+)(?:\s+(.*))?$", line)
        if m:
            decoded.append((int(m[1], 16), int(m[2], 16) | int(m[3], 16) << 8, m[4], m[5] or ""))
    for address, opcode, mnemonic, _ in sorted(decoded):
        if address in literals:
            continue
        if opcode & 0xF000 == 0xD000 and mnemonic == "mov.l":
            target = ((address + 4) & ~3) + 4 * (opcode & 255)
            literals.update((target, target + 2))
        elif opcode & 0xF000 == 0x9000 and mnemonic == "mov.w":
            literals.add(address + 4 + 2 * (opcode & 255))
    return [r for r in decoded if r[0] not in literals and not r[2].startswith(".")]


class Gateway(CPU):
    def __init__(self, seed, mask, base, blob, entry=ENTRY, top=TOP, config=CONFIG, result=RESULT):
        super().__init__(seed, base, 1)
        self.blob = blob
        self.r[4:8] = [entry, top, config, result]
        self.r[15] = 0x8C00ED00
        self.start_sp = self.r[15]
        self.sr = (self.sr & ~0xF0) | mask << 4
        self.pr = base + len(blob) + 0x100
        self.pc, self.mem, self.called = base, {}, 0
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy())
        self.expected_top = top

    def code(self, address, size):
        assert self.base <= address and address + size <= self.base + len(self.blob)
        return self.blob[address - self.base:address - self.base + size]

    def write(self, address, value):
        assert address % 4 == 0 and self.start_sp - 52 <= address < self.start_sp
        self.mem[address] = value & MASK

    def plain(self, word, pc):
        n, m = word >> 8 & 15, word >> 4 & 15
        if word == 0x4F03:
            self.r[15] -= 4;self.write(self.r[15], self.sr);return
        if word == 0x4F07:
            self.sr = self.read(self.r[15]);self.r[15] += 4;return
        if word == 0x0002:
            self.r[0] = self.sr;return
        if word & 0xF0FF == 0x4009:
            self.r[n] >>= 2;return
        if word >> 8 == 0xC9:
            self.r[0] &= word & 255;return
        if word >> 8 == 0xC8:
            self.t((self.r[0] & (word & 255)) == 0);return
        if word >> 8 == 0x88:
            immediate = (word & 255) - 256 if word & 128 else word & 255
            self.t(self.r[0] == (immediate & MASK));return
        if word & 0xF00F == 0x2009:
            self.r[n] &= self.r[m];return
        if word & 0xF00F == 0x2008:
            self.t((self.r[n] & self.r[m]) == 0);return
        if word & 0xF00F == 0x3006:
            self.t(self.r[n] > self.r[m]);return
        super().plain(word, pc)

    def worker(self):
        self.called += 1
        assert self.r[4:6] == [CONFIG, RESULT]
        assert self.r[15] == self.expected_top and self.sr & 0xF0 == 0xF0
        return_pc = self.pr
        for i in range(8):
            self.r[i] = (0x81234567 + i * 0x1234567) & MASK
        self.r[0] = 0x92345678
        self.mach ^= 0x12345678;self.macl ^= 0x98765432;self.gbr ^= 0x8ABCDEF0
        self.sr ^= 1;self.pr ^= 0x2468ABCD
        self.pc = return_pc

    def run(self):
        steps = 0
        while self.pc != self.initial[1]:
            steps += 1;assert steps < 200
            pc = self.pc;word = struct.unpack("<H", self.code(pc, 2))[0]
            if word >> 12 == 0xA or word & 0xF0FF == 0x400B or word == 0xB:
                if word >> 12 == 0xA:
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc + 4 + displacement * 2) & MASK
                elif word & 0xF0FF == 0x400B:
                    target = self.r[word >> 8 & 15];self.pr = pc + 4
                else:
                    target = self.pr
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target == ENTRY:
                    self.worker()
                else:
                    self.pc = target
            elif word >> 8 in (0x89, 0x8B):
                displacement = word & 255
                displacement -= 256 if displacement & 128 else 0
                take = bool(self.sr & 1) == (word >> 8 == 0x89)
                self.pc = (pc + 4 + displacement * 2 if take else pc + 2) & MASK
            else:
                self.plain(word, pc);self.pc = pc + 2
        return self

    def check(self, called):
        registers, pr, mach, macl, gbr, sr, fpscr, fr = self.initial
        assert self.called == called and self.r[8:] == registers[8:]
        assert (self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr) == (pr, mach, macl, gbr, sr, fpscr, fr)
        assert self.r[0] == (0x92345678 if called else MASK)


def model_gateway(base, blob):
    cases = 0
    for seed in range(80):
        for mask in range(16):
            Gateway(seed, mask, base, blob).run().check(int(mask == 15));cases += 1
    invalid = (("entry", ENTRY + 1), ("entry", ENTRY - 0x20000000), ("top", TOP + 4),
               ("top", 0xAD000020), ("top", 0xAC000FE0), ("config", 0), ("result", 0))
    for field, value in invalid:
        Gateway(1, 15, base, blob, **{field: value}).run().check(0);cases += 1
    Gateway(1, 15, base, blob, top=0xAD000000).run().check(1)
    return cases + 1


def check(directory):
    if not __debug__:
        raise ValueError("gateway model requires Python assertions; run without -O/PYTHONOPTIMIZE")
    directory = Path(directory)
    worker = read_elf(directory / "native-worker.elf")
    data, header, segments, sections, symbols = worker
    binary = (directory / "native-worker.bin").read_bytes()
    wire = (directory / "native-worker.reloc").read_bytes()
    if len(segments) != 1:
        raise ValueError("worker must own one bounded load segment")
    kind, offset, va, pa, size, memory, flags, _ = segments[0]
    if (kind, va, pa, size, memory, flags) != (1, P2, P1, len(binary), SPAN, 7):
        raise ValueError("worker must be entirely P2 with a64KiB physical load span")
    if binary != region(data, offset, size, "worker load") or len(binary) < 64:
        raise ValueError("worker flat binary differs from ELF load")
    fields = struct.unpack_from("<16I", binary)
    names = ("_kui_vmu_native_worker_probe", "__vmu_native_worker_bss_begin", "__vmu_native_worker_bss_end",
             "__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top", "__vmu_native_worker_end")
    expected = tuple(symbols[name][0] for name in names)
    if (fields[:3] != (0x4B4E5731, 1, 64) or fields[3:9] != expected or any(fields[9:]) or
            fields[3] != header[4] or fields[3] & 1 or not P2 + 64 <= fields[3] < P2 + len(binary) or
            not P2 + len(binary) == fields[4] <= fields[5] <= fields[6] or
            fields[6:] != (P2 + 0xF000, P2 + SPAN, P2 + SPAN, 0, 0, 0, 0, 0, 0, 0)):
        raise ValueError("worker exports/BSS/owned-stack layout mismatch")
    address, exported, section = symbol_bytes(worker, "_kui_vmu_native_worker_exports", 1)
    if address != P2 or exported != binary[:64] or section[2] & 7 != 2:
        raise ValueError("exports must be a typed read-only64byte header")
    for section in sections:
        if section[2] & 2 and section[5] and not P2 <= section[3] < section[3] + section[5] <= P2 + SPAN:
            raise ValueError("worker allocated bytes escape P2")
    for name, (value, _, symbol_kind, owner) in symbols.items():
        if symbol_kind in (1, 2) and (owner >= len(sections) or not P2 <= value < P2 + SPAN):
            raise ValueError("worker object/function outside owned P2 span: " + name)
    if generate(data, binary, P1, SPAN) != wire:
        raise ValueError("relocation table differs from exact ELF extraction")
    count = struct.unpack_from("<I", wire, 28)[0]
    if any(struct.unpack_from("<I", wire, 76 + i * 8)[0] & 0xFF000000 != 0xAC000000 for i in range(count)):
        raise ValueError("internal relocated pointers must retain P2 aliases")
    stage = read_elf(directory / "stage.elf")
    prepared = inspect_elf(stage[0], 0x8CE00000, 0x8CFE0000)
    stage_binary = (directory / "stage.bin").read_bytes()
    if prepared["payload"] != stage_binary:
        raise ValueError("stage flat binary differs from ELF")
    for begin, end, payload in (("__native_worker_blob_begin", "__native_worker_blob_end", binary),
                                ("__native_worker_reloc_begin", "__native_worker_reloc_end", wire)):
        a, b = stage[4][begin][0], stage[4][end][0]
        if b - a != len(payload) or region(stage_binary, a - 0x8CE00000, b - a, begin) != payload:
            raise ValueError("stage embedded worker/relocation bytes differ")
        if not any(s[1] == 1 and s[2] & 7 == 2 and s[3] <= a < b <= s[3] + s[5] for s in stage[3]):
            raise ValueError("embedded worker metadata is not immutable stage ROdata")
    base, gateway, section = symbol_bytes(stage, "_kui_vmu_native_worker_call", 2)
    if base & 1 or len(gateway) & 1 or section[2] & 7 != 6:
        raise ValueError("gateway must be typed stage executable code")
    cases = model_gateway(base, gateway)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    worker_dis = subprocess.check_output([objdump, "-d", str(directory / "native-worker.elf")], text=True)
    gateway_dis = subprocess.check_output([objdump, "-d", f"--start-address={base}",
        f"--stop-address={base + len(gateway)}", str(directory / "stage.elf")], text=True)
    try:
        instructions = instruction_audit("native-worker", worker_dis)
        instruction_audit("native-gateway", gateway_dis)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    for _, _, mnemonic, operands in literal_aware_instructions(worker_dis):
        if mnemonic in ("trapa", "sleep", "rte", "ldtlb") or (mnemonic in ("ldc", "ldc.l") and
                operands.split("!", 1)[0].strip().endswith(",sr")):
            raise ValueError("worker contains an unexpected privileged state-changing instruction")
    return {"worker_bytes": len(binary), "worker_span": SPAN, "worker_stack_bytes": 4096,
            "relocation_records": count, "worker_instructions": instructions,
            "stage_gateway_address": f"0x{base:08x}", "gateway_bytes": len(gateway),
            "gateway_model_cases": cases, "stage_embedded_worker_verified": True,
            "console_execution_verified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        report = check(args.directory)
    except (OSError, ValueError, KeyError, AssertionError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(2, f"Native worker artifact audit refused: {error}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
