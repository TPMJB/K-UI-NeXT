#!/usr/bin/env python3
"""Execute actual native release/rehook SH at fixed and relocated regions."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
from test_vmu_native_heap_execution import SH, MASK, signed
from check_vmu_native_worker_gateway import read_elf
from check_retail_instructions import audit
from check_loader_layout import EH as ELF_HEADER, SH as ELF_SECTION
from sh4_delay_slots import validate_delay_slot

GCC = ROOT / ".deps/sh-elf/bin/sh-elf-gcc"
OBJDUMP = ROOT / ".deps/sh-elf/bin/sh-elf-objdump"
FLAGS = ["-m4-single", "-ml", "-Iinclude", "-Isrc/loader", "-DKUI_RETAIL_ASYNC=1",
         "-DKUI_RETAIL_GD_ASYNC=1", "-DKUI_RETAIL_TRANSPORT=1", "-DKUI_RETAIL_LOW_RESIDENT=1"]
GAME_VBR, GAME_PC, STACK, IPRB = 0x8C110000, 0x8C220102, 0x8CF00000, 0xFFD00008


class VectorCPU(SH):
    def __init__(self, image, region):
        super().__init__(b"")
        for kind, offset, address, _, count, _, _, _ in image[2]:
            if kind == 1 and count:
                self.store_block(address, image[0][offset:offset + count])
        self.symbols, self.region = image[4], region
        self.control = {"sr": self.sr, "ssr": 0x40000000, "spc": GAME_PC,
                        "sgr": STACK, "vbr": region - 0x100, "dbr": self.r[0]}
        self.iprb = 0x5A1F
        self.r[15] = STACK
        self.original = self.r.copy()
        cell = self.symbols.get("_kui_retail_async_region_storage")
        if cell:
            self.write(cell[0], region)

    def read(self, address, count=4):
        if address == IPRB:
            assert count == 2
            return self.iprb
        return super().read(address, count)

    def write(self, address, value, count=4):
        if address == IPRB:
            assert count == 2
            self.iprb = value & 0xFFFF
            return
        super().write(address, value, count)

    def plain(self, op, pc):
        n, m = op >> 8 & 15, op >> 4 & 15
        stc = {0x0022: "vbr", 0x0032: "ssr", 0x0042: "spc", 0x003A: "sgr", 0x00FA: "dbr"}
        ldc = {0x402E: "vbr", 0x403E: "ssr", 0x404E: "spc", 0x40FA: "dbr"}
        if op & 0xF0FF in stc:
            self.r[n] = self.control[stc[op & 0xF0FF]]
        elif op & 0xF0FF in ldc:
            self.control[ldc[op & 0xF0FF]] = self.r[n]
        elif op & 0xF00F == 0x000E:
            self.r[n] = self.read((self.r[0] + self.r[m]) & MASK)
        elif op & 0xF00F == 0x0006:
            self.write((self.r[0] + self.r[n]) & MASK, self.r[m])
        elif op & 0xF00F == 0x200A:
            self.r[n] ^= self.r[m]
        elif op >> 8 == 0xCA:
            self.r[0] ^= op & 255
        else:
            super().plain(op, pc)

    def run_to(self, entry, stop):
        self.pc = entry
        for _ in range(6000):
            if self.pc == stop:
                return
            pc, op = self.pc, self.read(self.pc, 2)
            target, delay = None, False
            if op >> 12 == 0xA:
                target, delay = pc + 4 + 2 * signed(op & 4095, 12), True
            elif op & 0xF0FF == 0x402B:
                target, delay = self.r[op >> 8 & 15], True
            elif op >> 8 in (0x89, 0x8B):
                taken = bool(self.sr & 1) == (op >> 8 == 0x89)
                self.pc = pc + 4 + 2 * signed(op & 255, 8) if taken else pc + 2
                continue
            elif op == 0x002B:
                target, delay = self.control["spc"], True
            if delay:
                slot = self.read(pc + 2, 2)
                validate_delay_slot(slot, pc)
                self.plain(slot, pc + 2)
                if op == 0x002B:
                    self.sr = self.control["ssr"]
                self.pc = target & MASK
            else:
                self.plain(op, pc)
                self.pc += 2
        raise AssertionError("native vector replay did not reach its expected continuation")


def replay(image, region):
    cases = 0
    for vector in (0x100, 0x400, 0x600):
        for armed in (0, 1):
            for depth in range(4):
                cpu = VectorCPU(image, region)
                frame = region + 0x240
                for index, value in enumerate((GAME_VBR, 0x20, armed, depth)):
                    cpu.write(frame + 4 * index, value)
                offset = {0x100: 44, 0x400: 48, 0x600: 52}[vector]
                cpu.write(frame + offset, 17)
                entry = cpu.symbols[f"_kui_retail_release_{vector:x}"][0]
                cpu.run_to(entry, GAME_VBR + vector)
                assert cpu.r == cpu.original and cpu.control["vbr"] == GAME_VBR
                assert cpu.iprb == 0x5A2F and cpu.read(frame + offset) == 18
                expected = vector == 0x600 and armed and depth < 3
                assert cpu.read(frame + 12) == depth + int(expected)
                if expected:
                    assert cpu.control["spc"] == cpu.symbols["_kui_retail_rehook"][0]
                    assert cpu.read(frame + 16 + 8 * depth) == GAME_PC
                    assert cpu.read(frame + 20 + 8 * depth) == STACK
                else:
                    assert cpu.control["spc"] == GAME_PC
                cases += 1
    for depth in (1, 2, 3):
        for armed in (0, 1):
            for moved in (False, True):
                cpu = VectorCPU(image, region)
                cpu.sr = 0x40000001
                cpu.control["vbr"] = GAME_VBR + (0x10000 if moved else 0)
                cpu.iprb = 0x5A2F
                frame = region + 0x240
                for index, value in enumerate((GAME_VBR, 0x20, armed, depth)):
                    cpu.write(frame + 4 * index, value)
                cpu.write(frame + 16 + 8 * (depth - 1), GAME_PC)
                cpu.write(frame + 20 + 8 * (depth - 1), STACK)
                cpu.write(frame + 40, 31)
                cpu.run_to(cpu.symbols["_kui_retail_rehook"][0], GAME_PC)
                installed = armed and not moved
                assert cpu.r == cpu.original and cpu.sr == 0x40000001
                assert cpu.read(frame + 12) == depth - 1
                assert cpu.read(frame + 40) == 31 + int(installed)
                assert cpu.control["vbr"] == (region - 0x100 if installed else GAME_VBR + (0x10000 if moved else 0))
                assert cpu.iprb == (0x5A1F if installed else 0x5A2F)
                cases += 1
    # _start must preserve every argument across low BSS clearing, including
    # the gated fifth argument on the caller's unchanged temporary stack.
    cpu = VectorCPU(image, region)
    cpu.write(STACK, 0x8CE10000)
    args = cpu.r[4:8]
    name = "_kui_vmu_native_background_init" if "_kui_retail_async_region_storage" in cpu.symbols else "_kui_retail_resident_init"
    cpu.run_to(cpu.symbols["_start"][0], cpu.symbols[name][0])
    assert cpu.r[4:8] == args and cpu.r[15] == STACK and cpu.read(STACK) == 0x8CE10000
    return cases + 1


def run(command, **kwargs):
    return subprocess.check_output([str(x) for x in command], cwd=ROOT, **kwargs)


with tempfile.TemporaryDirectory(prefix="kui-native-async-vectors-") as tmp:
    directory = Path(tmp)
    old = directory / "old.S"
    old.write_bytes(run(["git", "show", "df035a8cae47:src/loader/retail_resident.S"]))
    current_obj, old_obj = directory / "current-default.o", directory / "old-default.o"
    for source, output in ((old, old_obj), (ROOT / "src/loader/retail_resident.S", current_obj)):
        run([GCC, *FLAGS, "-c", source, "-o", output])
    def allocated(path):
        data = path.read_bytes()
        header = ELF_HEADER.unpack_from(data)
        sections = [ELF_SECTION.unpack_from(data, header[6] + i * ELF_SECTION.size)
                    for i in range(header[12])]
        return [data[s[4]:s[4] + s[5]] for s in sections if s[2] & 2 and s[1] != 8]
    assert allocated(current_obj) == allocated(old_obj), "ordinary native assembly changed"
    linker = directory / "vectors.ld"
    linker.write_text("SECTIONS { . = 0x8c004000; .entry : { *(.entry) } .text : { *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } . = ALIGN(32); __retail_resident_bss_begin = .; .bss : { *(.bss*) } __retail_resident_bss_end = .; }")
    total = 0
    for enabled in (0, 1):
        source = directory / ("stub-" + str(enabled) + ".S")
        symbol = "_kui_retail_async_region_storage" if enabled else "_kui_retail_async_region"
        obj, stub, elf = (directory / (name + "-" + str(enabled) + suffix) for name, suffix in (("vectors", ".o"), ("stub", ".o"), ("vectors", ".elf")))
        run([GCC, *FLAGS, "-DKUI_VMU_NATIVE_BACKGROUND=" + str(enabled), "-c", "src/loader/retail_resident.S", "-o", obj])
        undefined = run([ROOT / ".deps/sh-elf/bin/sh-elf-nm", "-u", obj], text=True)
        resolved = {symbol, "_kui_retail_resident_init", "_kui_vmu_native_background_init",
                    "__retail_resident_bss_begin", "__retail_resident_bss_end"}
        absolute = ""
        for index, line in enumerate(undefined.splitlines()):
            name = line.split()[-1]
            if name not in resolved:
                absolute += f".global {name}\n.set {name},0x{0x8C030000 + index * 4:x}\n"
        source.write_text(".text\n.global _kui_retail_resident_init\n_kui_retail_resident_init:\n rts\n nop\n.global _kui_vmu_native_background_init\n_kui_vmu_native_background_init:\n rts\n nop\n.bss\n.align 5\n.global " + symbol + "\n" + symbol + ":\n.space " + ("4" if enabled else "0x760") + "\n" + absolute)
        run([GCC, "-m4-single", "-ml", "-c", source, "-o", stub])
        run([GCC, "-m4-single", "-ml", "-nostdlib", "-Wl,--build-id=none", "-T", linker, obj, stub, "-o", elf])
        image = read_elf(elf)
        audit("native-vectors", run([OBJDUMP, "-d", elf], text=True))
        regions = (0x8CE10000, 0x8C420400) if enabled else (image[4][symbol][0],)
        for region in regions:
            total += replay(image, region)
print(f"native async vectors: {total} actual SH release/rehook/init cases passed; ordinary assembly unchanged")
