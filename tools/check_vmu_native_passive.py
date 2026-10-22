#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit a relocated, single-pair passive Maple probe and its actual gateways.

The closed integer model replaces C callbacks with an ABI-compliant adversary.
It establishes bridge/register behaviour, not console execution, arbitrary-game
compatibility, or a proof of every indirect C memory access.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from check_vmu_native_heap_probe import inspect as inspect_base
from check_vmu_native_worker_gateway import (
    CPU, MASK, P1, P2, SPAN, literal_aware_instructions,
    model_gateway, read_elf, symbol_bytes,
)
from check_loader_layout import inspect_elf, region
from check_retail_instructions import audit as instruction_audit
from vmu_native_relocations import generate

ROOT = Path(__file__).resolve().parents[1]


def require(value, message):
    if not value:
        raise ValueError(message)


def relocated(binary, wire, destination):
    result = bytearray(binary)
    count = struct.unpack_from("<I", wire, 28)[0]
    for i in range(count):
        offset, value = struct.unpack_from("<II", wire, 72 + i * 8)
        struct.pack_into("<I", result, offset, value + destination - P2)
    return bytes(result)


class PassiveGateway(CPU):
    """Execute only the actual authored gateway, never a guessed transcript."""
    def __init__(self, seed, mask, base, blob, symbols, mode):
        super().__init__(seed, base, 1)
        self.blob, self.symbols, self.mode = blob, symbols, mode
        self.start_sp = 0x8C00EC00
        self.r[15] = self.start_sp
        self.r[14] = 0x8C6010FC
        self.r[12] = 1
        self.sr = (self.sr & ~0xF0) | mask << 4
        self.resume = 0x8C2D9B74
        self.consumer = 0x8C2D9C7E
        self.private_top = symbols["__vmu_native_worker_stack_top"]
        self.visit = symbols["_kui_vmu_native_passive_submit"]
        self.complete = symbols["_kui_vmu_native_passive_complete"]
        self.report = symbols["_kui_vmu_native_passive_report"]
        self.manager = 0xAC600000
        self.mem = {self.r[14]: self.manager, self.manager + 0x10F4: 0xA05F6C00,
                    symbols["_kui_vmu_native_passive_resume"]: self.resume,
                    symbols["_kui_vmu_native_passive_consumer"]: self.consumer}
        self.pc = symbols[{"submit": "_kui_vmu_native_passive_submit_hook",
                           "complete": "_kui_vmu_native_passive_complete_hook",
                           "terminal": "_kui_vmu_native_passive_terminal"}[mode]]
        self.pr = 0x8C02D312
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.sr, self.fpscr, self.fr.copy())
        self.called, self.done = [], False
        self.writes, self.reads = [], []

    def code(self, address, size):
        assert self.base <= address < address + size <= self.base + len(self.blob)
        return self.blob[address - self.base:address - self.base + size]

    def write(self, address, value):
        assert address % 4 == 0
        assert (self.start_sp - 96 <= address < self.start_sp or
                self.private_top - 256 <= address < self.private_top or
                address == 0xA05F6C18), "unadmitted gateway store " + hex(address)
        self.writes.append(address)
        self.mem[address] = value & MASK

    def plain(self, word, pc):
        n, m = word >> 8 & 15, word >> 4 & 15
        if word & 0xF0FF == 0x0002:  # STC SR,Rn
            self.r[n] = self.sr
            return
        if word & 0xF0FF == 0x400E:  # LDC Rn,SR
            self.sr = self.r[n]
            return
        if word & 0xF00F == 0x000E:  # MOV.L @(R0,Rm),Rn
            self.r[n] = self.read((self.r[m] + self.r[0]) & MASK)
            return
        if word == 0x4F03:
            self.r[15] -= 4
            self.write(self.r[15], self.sr)
            return
        if word == 0x4F07:
            self.sr = self.read(self.r[15]); self.r[15] += 4
            return
        super().plain(word, pc)

    def callback(self, target):
        assert self.sr & 0xF0 == 0xF0
        assert self.private_top - 256 <= self.r[15] <= self.private_top
        self.called.append(target)
        if target in (self.visit, self.complete):
            frame = self.r[4]
            regs, pr, mach, macl, gbr, sr, _, _ = self.initial
            expected = [gbr, macl, mach, pr] + list(reversed(regs[2:15])) + [sr, regs[1]]
            assert len(expected) == 19
            assert frame == self.start_sp - (80 if self.mode == "complete" else 76)
            assert [self.read(frame + i * 4) for i in range(19)] == expected
            if self.mode == "complete":
                assert self.read(frame + 76) == regs[0]
                self.done = True
                return
            return_pc = self.pr
            # Native C may overwrite caller-saved GPRs, condition T, PR and
            # MAC/GBR; preserving only ABI callee-saved R8..R15 is sufficient.
            for i in range(8):
                self.r[i] = (0x81234567 + i * 0x1234567) & MASK
            self.mach ^= 0x12345678; self.macl ^= 0x98765432
            self.gbr ^= 0x8ABCDEF0; self.sr ^= 1
            self.pr ^= 0x2468ABCD; self.pc = return_pc
        elif target == self.report:
            assert self.mode == "terminal"
            self.done = True
        else:
            raise AssertionError("unadmitted callback " + hex(target))

    def run(self):
        steps = 0
        while not self.done and self.pc != self.resume:
            steps += 1
            assert steps < 250
            pc = self.pc; word = struct.unpack("<H", self.code(pc, 2))[0]
            if (word >> 12 in (0xA, 0xB) or word & 0xF0FF in (0x400B, 0x402B)
                    or word == 0x000B):
                if word >> 12 in (0xA, 0xB):
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc + 4 + displacement * 2) & MASK
                    if word >> 12 == 0xB: self.pr = pc + 4
                elif word & 0xF0FF in (0x400B, 0x402B):
                    target = self.r[word >> 8 & 15]
                    if word & 0xF0FF == 0x400B: self.pr = pc + 4
                else:
                    target = self.pr
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target in (self.visit, self.complete, self.report):
                    self.callback(target)
                else:
                    self.pc = target
            else:
                self.plain(word, pc); self.pc = pc + 2
        return self

    def check(self):
        regs, pr, mach, macl, gbr, sr, fpscr, fr = self.initial
        assert self.fpscr == fpscr and self.fr == fr
        if self.mode != "submit":
            assert self.done and self.writes.count(0xA05F6C18) == 0
            assert self.called == [self.complete if self.mode == "complete" else self.report]
            return
        assert not self.done and self.pc == self.resume
        expected = regs.copy()
        expected[0], expected[2], expected[3] = 0xFD8, 0xA05F6C00, self.manager
        assert self.r == expected
        assert (self.pr, self.mach, self.macl, self.gbr, self.sr) == (pr, mach, macl, gbr, sr)
        assert self.called == [self.visit]
        assert self.writes.count(0xA05F6C18) == 1 and self.mem[0xA05F6C18] == 1


def model_hooks(binary, wire, symbols):
    cases = 0
    for destination in (P2, 0xAC38A880, 0xAC700000):
        blob = relocated(binary, wire, destination)
        mapped = {name: address + destination - P2 for name, address in symbols.items()}
        for seed in range(40):
            for mask in range(16):
                for mode in ("submit", "complete", "terminal"):
                    PassiveGateway(seed, mask, destination, blob, mapped, mode).run().check()
                    cases += 1
    return cases


def model_mutations(binary, symbols):
    """Reject control-flow/FPU changes without relying on ELF byte mismatch."""
    tests = []
    begin = symbols["_kui_vmu_native_passive_submit_hook"] - P2
    changed = bytearray(binary)
    struct.pack_into("<H", changed, begin, 0xF000)
    tests.append((changed, "submit", "FPU in gateway"))
    # Change the referenced callback literal in situ. Its wrong target is a
    # real known callback, so rejection must come from the frame/control-flow
    # model, rather than merely an out-of-bounds instruction read.
    for mode, target_name, wrong_name in (
            ("submit", "_kui_vmu_native_passive_submit", "_kui_vmu_native_passive_complete"),
            ("complete", "_kui_vmu_native_passive_complete", "_kui_vmu_native_passive_submit"),
            ("terminal", "_kui_vmu_native_passive_report", "_kui_vmu_native_passive_submit")):
        function = symbols["_kui_vmu_native_passive_" + ("terminal" if mode == "terminal" else mode + "_hook")]
        matches = []
        # Authored hook functions are all shorter than 256 bytes. Inspect
        # PC-relative MOV.L operands, not arbitrary pointer-looking words.
        for offset in range(function - P2, min(function - P2 + 256, len(binary)) - 1, 2):
            word = struct.unpack_from("<H", binary, offset)[0]
            if word >> 12 != 0xD:
                continue
            literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
            if (0 <= literal <= len(binary) - 4 and
                    struct.unpack_from("<I", binary, literal)[0] == symbols[target_name]):
                matches.append(literal)
        require(matches, "missing actual gateway callback literal for mutation")
        changed = bytearray(binary)
        struct.pack_into("<I", changed, matches[0], symbols[wrong_name])
        tests.append((changed, mode, "wrong callback target"))
    cases = 0
    for blob, mode, description in tests:
        try:
            PassiveGateway(7, 6, P2, blob, symbols, mode).run().check()
        except (AssertionError, struct.error):
            cases += 1
        else:
            raise ValueError("gateway model accepted " + description)
    return cases


def check(directory, *, self_test=False):
    require(__debug__, "gateway model requires Python assertions")
    directory = Path(directory)
    base = inspect_base(directory, self_test=self_test,
                        objdump=shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump"))
    worker = read_elf(directory / "native-worker.elf")
    data, header, segments, sections, raw_symbols = worker
    symbols = {name: record[0] for name, record in raw_symbols.items()}
    binary = (directory / "native-worker.bin").read_bytes()
    wire = (directory / "native-worker.reloc").read_bytes()
    require(len(segments) == 1, "worker must own one load segment")
    kind, offset, va, pa, size, memory, flags, _ = segments[0]
    require((kind, va, pa, size, memory, flags) == (1, P2, P1, len(binary), SPAN, 7),
            "worker must be P2 with a 64KiB physical load span")
    require(binary == region(data, offset, size, "worker load") and len(binary) >= 64,
            "worker binary differs from ELF load")
    fields = struct.unpack_from("<16I", binary)
    names = ("_kui_vmu_native_passive_arm", "_kui_vmu_native_passive_submit_hook",
             "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_terminal",
             "__vmu_native_worker_bss_begin", "__vmu_native_worker_bss_end",
             "__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top",
             "__vmu_native_worker_end", "_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer")
    require(fields[:3] == (0x4B4E5031, 1, 64) and
            fields[3:14] == tuple(symbols[name] for name in names) and not any(fields[14:]),
            "passive worker exports differ from exact typed symbols")
    require(fields[3] == header[4] and all(not p & 1 and P2 + 64 <= p < P2 + len(binary) for p in fields[3:7]),
            "worker entry/hook points outside initialized code")
    require(P2 + len(binary) == fields[7] <= fields[8] <= fields[9] and
            fields[9:12] == (P2 + 0xF000, P2 + SPAN, P2 + SPAN), "worker BSS/stack layout differs")
    address, exported, section = symbol_bytes(worker, "_kui_vmu_native_worker_exports", 1)
    require(address == P2 and exported == binary[:64] and section[2] & 7 == 2,
            "exports must be typed immutable header")
    for name in names[:4]:
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "exported entry is not executable")
    for name in names[-2:]:
        address, cell, section = symbol_bytes(worker, name, 1)
        require(len(cell) == 4 and address % 4 == 0 and not any(cell) and section[2] & 7 == 3,
                "resume/consumer cells must be typed, zero-initialized worker-owned words")
    for section in sections:
        require(not section[2] & 2 or not section[5] or
                P2 <= section[3] < section[3] + section[5] <= P2 + SPAN,
                "allocated worker bytes escape P2")
    for name, (value, _, symbol_kind, owner) in raw_symbols.items():
        require(symbol_kind not in (1, 2) or owner < len(sections) and P2 <= value < P2 + SPAN,
                "worker object/function escapes P2: " + name)
        require(not name.startswith(("_kui_loader_sd_", "_kui_sci_sd_", "_kui_vmu_durable_",
                                      "_kui_vmu_persist_", "_kui_retail_storage_")),
                "passive worker links a storage/persistence operation: " + name)
    require(generate(data, binary, P1, SPAN) == wire, "relocation table differs from exact ELF extraction")
    relocations = struct.unpack_from("<I", wire, 28)[0]
    require(all(struct.unpack_from("<I", wire, 76 + i * 8)[0] & 0xFF000000 == 0xAC000000 for i in range(relocations)),
            "internal relocated pointers must retain P2 aliases")
    stage = read_elf(directory / "stage.elf")
    prepared = inspect_elf(stage[0], 0x8CE00000, 0x8CFE0000)
    stage_binary = (directory / "stage.bin").read_bytes()
    require(prepared["payload"] == stage_binary, "stage binary differs from ELF")
    for begin, end, payload in (("__native_worker_blob_begin", "__native_worker_blob_end", binary),
                                ("__native_worker_reloc_begin", "__native_worker_reloc_end", wire)):
        a, b = stage[4][begin][0], stage[4][end][0]
        require(b - a == len(payload) and region(stage_binary, a - 0x8CE00000, b - a, begin) == payload,
                "stage embeds different worker/relocation bytes")
        require(any(s[1] == 1 and s[2] & 7 == 2 and s[3] <= a < b <= s[3] + s[5] for s in stage[3]),
                "embedded worker must occupy stage immutable data")
    gateway_address, gateway, _ = symbol_bytes(stage, "_kui_vmu_native_worker_call", 2)
    stage_cases = model_gateway(gateway_address, gateway)
    hook_cases = model_hooks(binary, wire, symbols)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    disassembly = subprocess.check_output([objdump, "-d", str(directory / "native-worker.elf")], text=True)
    try:
        instructions = instruction_audit("native-passive", disassembly)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    hook_ranges = [symbol_bytes(worker, name, 2)[:2] for name in names[1:4]]
    for address, _, mnemonic, operands in literal_aware_instructions(disassembly):
        require(mnemonic not in ("trapa", "sleep", "rte", "ldtlb"), "unexpected privileged worker instruction")
        if mnemonic in ("ldc", "ldc.l") and operands.split("!", 1)[0].strip().endswith(",sr"):
            require(any(begin <= address < begin + len(body) for begin, body in hook_ranges),
                    "C worker changes SR outside audited gateways")
    report = {"schema": "kui.native.passive.probe.artifact.v1", "allocator_bootstrap_audit": base,
              "module_sha256": base["module_sha256"], "runtime": base["runtime"],
              "worker_bytes": len(binary), "worker_span": SPAN, "worker_stack_bytes": 4096,
              "relocation_records": relocations, "worker_instructions": instructions,
              "stage_gateway_model_cases": stage_cases, "passive_gateway_model_cases": hook_cases,
              "stage_embedded_worker_verified": True, "maple_interception_installed": True,
              "virtual_replies_enabled": False, "storage_writes_enabled": False,
              "console_execution_verified": False, "expected_phase": 11}
    if self_test:
        report["gateway_mutation_rejections"] = model_mutations(binary, symbols)
        report["artifact_mutation_rejections"] = mutation_checks(directory)
    return report


def mutation_checks(directory):
    """Coherent changed flat images still must fail their typed ELF binding."""
    checks = 0
    with tempfile.TemporaryDirectory() as temp:
        target = Path(temp)
        for path in directory.iterdir():
            if path.is_file() and path.suffix in (".elf", ".bin", ".reloc", ".kui"):
                (target / path.name).symlink_to(path.resolve())
        for name, at in (("native-worker.bin", 0), ("native-worker.bin", 12),
                         ("native-worker.reloc", 72), ("stage.bin", 128)):
            path = target / name
            original = (directory / name).read_bytes()
            changed = bytearray(original); changed[at] ^= 1
            path.unlink(); path.write_bytes(changed)
            try:
                check(target)
            except (ValueError, AssertionError, KeyError, struct.error):
                checks += 1
            else:
                raise ValueError("modified artifact accepted: " + name)
            path.unlink(); path.symlink_to((directory / name).resolve())
    return checks


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    try:
        result = json.dumps(check(args.directory, self_test=args.self_test), indent=2) + "\n"
    except (OSError, ValueError, KeyError, AssertionError, struct.error, subprocess.CalledProcessError) as error:
        p.exit(2, f"Native passive artifact audit refused: {error}\n")
    if args.output: args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
