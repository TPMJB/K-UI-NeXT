#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit a relocated read-only Maple bridge and its returning gateways.

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


def original_release_state(registers, sr):
    """Independent reviewed original release+0..19 integer transcript.

    The actual PS2 witness is 0002 9354 4009 4009 c90f 6703 0002 2039
    cbf0 400e; its MOV.W literal is ff0f, sign-extended by the SH core.
    The installer separately admits these original bytes before patching.
    """
    result = registers.copy()
    for opcode in (0x0002, 0x9354, 0x4009, 0x4009, 0xC90F,
                   0x6703, 0x0002, 0x2039, 0xCBF0, 0x400E):
        if opcode == 0x0002:
            result[0] = sr
        elif opcode == 0x9354:
            result[3] = 0xFFFFFF0F
        elif opcode == 0x4009:
            result[0] >>= 2
        elif opcode == 0xC90F:
            result[0] &= 15
        elif opcode == 0x6703:
            result[7] = result[0]
        elif opcode == 0x2039:
            result[0] &= result[3]
        elif opcode == 0xCBF0:
            result[0] |= 0xF0
        elif opcode == 0x400E:
            sr = result[0]
        else:
            raise AssertionError("unmodeled original allocator instruction")
    return result, sr


class ReadonlyGateway(CPU):
    """Execute only the actual authored gateway, never a guessed transcript."""
    def __init__(self, seed, mask, base, blob, symbols, mode):
        super().__init__(seed, base, 1)
        self.blob, self.symbols, self.mode = blob, symbols, mode
        self.start_sp = 0x8C00EC00
        self.r[15] = self.start_sp
        self.r[14] = 0x8C6010FC
        self.r[12] = 1
        if mode == "release":
            # Gateway ABI preservation applies even to null/malformed input;
            # the independent C guard suite decides which values may return.
            payloads = (0, 1, 0x8C410020, 0x8C00F000,
                        (base & 0x1FFFFFFF) + 0x10000)
            if seed < len(payloads):
                self.r[4] = payloads[seed]
        self.sr = (self.sr & ~0xF0) | mask << 4
        self.resume = 0x8C2D9B74
        self.consumer = 0x8C2D9C7E + seed * 2
        self.release_resume = 0x8C2B178C
        self.private_top = symbols["__vmu_native_worker_stack_top"]
        self.visit = symbols["_kui_vmu_native_passive_submit"]
        self.complete = symbols["_kui_vmu_native_passive_complete"]
        self.report = symbols["_kui_vmu_native_passive_report"]
        self.release_check = symbols["_kui_vmu_native_readonly_release_check"]
        self.manager = 0xAC600000
        self.mem = {self.r[14]: self.manager, self.manager + 0x10F4: 0xA05F6C00,
                    symbols["_kui_vmu_native_passive_resume"]: self.resume,
                    symbols["_kui_vmu_native_passive_consumer"]: self.consumer,
                    symbols["_kui_vmu_native_readonly_release_resume"]: self.release_resume}
        self.pc = symbols[{"submit": "_kui_vmu_native_passive_submit_hook",
                           "complete": "_kui_vmu_native_passive_complete_hook",
                           "terminal": "_kui_vmu_native_passive_terminal",
                           "release": "_kui_vmu_native_readonly_release_hook"}[mode]]
        self.pr = 0x8C02D312
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.sr, self.fpscr, self.fr.copy())
        self.called, self.done = [], False
        self.writes, self.reads, self.release_masks = [], [], []

    def code(self, address, size):
        assert self.base <= address < address + size <= self.base + len(self.blob)
        return self.blob[address - self.base:address - self.base + size]

    def read(self, address):
        if address == self.symbols["_kui_vmu_native_passive_consumer"]:
            regs, pr, mach, macl, gbr, sr, _, _ = self.initial
            assert self.mode == "complete" and self.called == [self.complete]
            assert self.r[15] == self.start_sp, "consumer cell read before owner frame consumed"
            assert self.sr == sr, "consumer cell read before inherited SR restoration"
            assert (self.pr, self.mach, self.macl, self.gbr) == (pr, mach, macl, gbr)
            assert all(self.r[i] == regs[i] for i in range(16) if i != 3), "consumer cell read before full restoration"
        return super().read(address)

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
            if self.called and self.mode in ("submit", "complete", "release"):
                # IRQ admission is restored only after all inherited frame
                # words have been read and the original SP is restored.
                assert self.r[15] == self.start_sp, "SR restored before consuming owner frame"
                assert self.code(pc + 2, 8) == struct.pack("<4H", 9, 9, 9, 9), "missing inherited-SR latency NOPs"
                if self.mode == "release":
                    assert self.r[n] & 0xF0 == 0xF0, "release reopened IRQs after lifetime validation"
                    self.release_masks.append(self.r[n])
            self.sr = self.r[n]
            return
        if word & 0xF0FF == 0x4009:  # SHLR2 Rn: no T-bit change
            self.r[n] >>= 2
            return
        if word & 0xFF00 == 0xC900:  # AND #imm,R0: no T-bit change
            self.r[0] &= word & 255
            return
        if word & 0xF00F == 0x2009:  # AND Rm,Rn: no T-bit change
            self.r[n] &= self.r[m]
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
        if target in (self.visit, self.complete, self.release_check):
            frame = self.r[4]
            regs, pr, mach, macl, gbr, sr, _, _ = self.initial
            expected = [gbr, macl, mach, pr] + list(reversed(regs[2:15])) + [sr, regs[1]]
            assert len(expected) == 19
            assert frame == self.start_sp - (80 if self.mode == "complete" else 76)
            assert [self.read(frame + i * 4) for i in range(19)] == expected
            if self.mode == "complete":
                assert self.read(frame + 76) == regs[0]
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
        exit_pc = self.consumer if self.mode == "complete" else self.release_resume if self.mode == "release" else self.resume
        while not self.done and self.pc != exit_pc:
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
                if target in (self.visit, self.complete, self.report, self.release_check):
                    self.callback(target)
                else:
                    self.pc = target
            else:
                self.plain(word, pc); self.pc = pc + 2
        return self

    def check(self):
        regs, pr, mach, macl, gbr, sr, fpscr, fr = self.initial
        assert self.fpscr == fpscr and self.fr == fr
        if self.mode == "terminal":
            assert self.done and self.writes.count(0xA05F6C18) == 0
            assert self.called == [self.report]
            return
        if self.mode == "complete":
            assert not self.done and self.pc == self.consumer
            expected = regs.copy(); expected[3] = self.consumer
            assert self.r == expected, "completion changed SDK registers"
            assert (self.pr, self.mach, self.macl, self.gbr, self.sr) == (pr, mach, macl, gbr, sr)
            assert self.called == [self.complete]
            assert self.reads.count(self.symbols["_kui_vmu_native_passive_consumer"]) == 1, "consumer target was not loaded dynamically"
            assert self.writes.count(0xA05F6C18) == 0, "completion restarted Maple DMA"
            return
        if self.mode == "release":
            assert not self.done and self.pc == self.release_resume
            expected, masked_sr = original_release_state(regs, sr)
            assert expected[0] == masked_sr == sr | 0xF0 and expected[7] == (sr >> 4) & 15
            assert self.r == expected, "release differs from original release+20 register state"
            assert (self.pr, self.mach, self.macl, self.gbr, self.sr) == (pr, mach, macl, gbr, masked_sr)
            assert self.called == [self.release_check]
            assert self.release_masks == [sr | 0xF0, sr | 0xF0], "release mask replay changed inherited SR bits"
            assert self.reads.count(self.symbols["_kui_vmu_native_readonly_release_resume"]) == 1
            assert self.writes.count(0xA05F6C18) == 0, "release started Maple DMA"
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
                for mode in ("submit", "complete", "terminal", "release"):
                    ReadonlyGateway(seed, mask, destination, blob, mapped, mode).run().check()
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
            ("terminal", "_kui_vmu_native_passive_report", "_kui_vmu_native_passive_submit"),
            ("release", "_kui_vmu_native_readonly_release_check", "_kui_vmu_native_passive_complete")):
        function = symbols["_kui_vmu_native_readonly_release_hook"] if mode == "release" else symbols["_kui_vmu_native_passive_" + ("terminal" if mode == "terminal" else mode + "_hook")]
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
    # The completion must dereference its own consumer cell, not a stage
    # address, callback, or submission resume cell.
    complete = symbols["_kui_vmu_native_passive_complete_hook"] - P2
    matches = []
    for offset in range(complete, min(complete + 256, len(binary)) - 1, 2):
        word = struct.unpack_from("<H", binary, offset)[0]
        if word & 0xFF00 != 0xD300:
            continue
        literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
        if (0 <= literal <= len(binary) - 4 and
                struct.unpack_from("<I", binary, literal)[0] == symbols["_kui_vmu_native_passive_consumer"]):
            matches.append(literal)
    require(len(matches) == 1, "missing unique dynamic consumer-cell load")
    changed = bytearray(binary)
    struct.pack_into("<I", changed, matches[0], symbols["_kui_vmu_native_passive_resume"])
    tests.append((changed, "complete", "wrong consumer cell"))
    # Move inherited SR restoration one frame pop too early. This must fail
    # independently of final register equality, since a same-priority IRQ may
    # arrive as soon as the original mask is restored.
    pattern = struct.pack("<4H", 0x63F6, 0x61F6, 0x60F6, 0x430E)
    at = binary.find(pattern, complete, min(complete + 256, len(binary)))
    require(at >= 0, "missing complete-frame final restore sequence")
    changed = bytearray(binary)
    struct.pack_into("<2H", changed, at + 4, 0x430E, 0x60F6)
    tests.append((changed, "complete", "premature inherited SR restoration"))
    changed = bytearray(binary)
    struct.pack_into("<H", changed, at + 8, 0x6883)  # MOV R8,R8 is otherwise harmless.
    tests.append((changed, "complete", "missing SR latency instruction"))
    # Replay the original MDST store twice; the linked model admits that
    # exact address but requires precisely one store during submission.
    submit = symbols["_kui_vmu_native_passive_submit_hook"] - P2
    store = binary.find(struct.pack("<H", 0x12C6), submit, min(submit + 256, len(binary)))
    require(store >= 0, "missing original MDST replay")
    changed = bytearray(binary)
    struct.pack_into("<H", changed, store + 2, 0x12C6)
    tests.append((changed, "submit", "duplicate MDST replay"))
    # Apply the same lifetime-entry restoration mutation to the allocator
    # wrapper. Its C callback may execute in contexts different from Maple.
    release = symbols["_kui_vmu_native_readonly_release_hook"] - P2
    pattern = struct.pack("<5H", 0x60F6, 0x61F6, 0x6703, 0xCBF0, 0x400E)
    at = binary.find(pattern, release, min(release + 256, len(binary)))
    require(at >= 0, "missing release-frame final restore sequence")
    changed = bytearray(binary)
    struct.pack_into("<2H", changed, at + 2, 0x400E, 0x61F6)
    tests.append((changed, "release", "premature release SR restoration"))
    # Removing the OR before the first post-callback SR load produces the
    # same final state after the original mask-establishment sequence, yet
    # transiently permits IRQs to invalidate the just-checked free list.
    changed = bytearray(binary)
    struct.pack_into("<H", changed, at + 6, 0x0009)
    tests.append((changed, "release", "transient release IRQ admission"))
    cases = 0
    for blob, mode, description in tests:
        try:
            ReadonlyGateway(7, 6, P2, blob, symbols, mode).run().check()
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
             "__vmu_native_worker_end", "_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
             "_kui_vmu_native_readonly_release_hook", "_kui_vmu_native_readonly_release_resume")
    require(fields[:3] == (0x4B4E5031, 1, 64) and
            fields[3:16] == tuple(symbols[name] for name in names),
            "read-only worker exports differ from exact typed symbols")
    require(fields[3] == header[4] and all(not p & 1 and P2 + 64 <= p < P2 + len(binary) for p in fields[3:7]),
            "worker entry/hook points outside initialized code")
    require(P2 + len(binary) == fields[7] <= fields[8] <= fields[9] and
            fields[9:12] == (P2 + 0xF000, P2 + SPAN, P2 + SPAN), "worker BSS/stack layout differs")
    address, exported, section = symbol_bytes(worker, "_kui_vmu_native_worker_exports", 1)
    require(address == P2 and exported == binary[:64] and section[2] & 7 == 2,
            "exports must be typed immutable header")
    for name in names[:4] + (names[-2],):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "exported entry is not executable")
    for name in ("_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
                 "_kui_vmu_native_readonly_release_resume"):
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
                                      "_kui_vmu_persist_", "_kui_vmu_volatile_", "_kui_retail_storage_")),
                "read-only worker links a storage/persistence operation: " + name)
    for name in ("_kui_vmu_readonly_init_address", "_kui_vmu_readonly_handle",
                 "_kui_vmu_readonly_overlay_prepare", "_kui_vmu_readonly_overlay_complete",
                 "_kui_vmu_native_readonly_release_check", "_kui_vmu_native_lifetime_release_safe"):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "missing bounded read-only bridge implementation: " + name)
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
        instructions = instruction_audit("native-readonly", disassembly)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    hook_ranges = [symbol_bytes(worker, name, 2)[:2] for name in names[1:4] + (names[-2],)]
    for address, _, mnemonic, operands in literal_aware_instructions(disassembly):
        require(mnemonic not in ("trapa", "sleep", "rte", "ldtlb"), "unexpected privileged worker instruction")
        if mnemonic in ("ldc", "ldc.l") and operands.split("!", 1)[0].strip().endswith(",sr"):
            require(any(begin <= address < begin + len(body) for begin, body in hook_ranges),
                    "C worker changes SR outside audited gateways")
    report = {"schema": "kui.native.readonly.probe.artifact.v1", "allocator_bootstrap_audit": base,
              "module_sha256": base["module_sha256"], "runtime": base["runtime"],
              "worker_bytes": len(binary), "worker_span": SPAN, "worker_stack_bytes": 4096,
              "relocation_records": relocations, "worker_instructions": instructions,
              "stage_gateway_model_cases": stage_cases, "readonly_gateway_model_cases": hook_cases,
              "original_release_mask_model_cases": hook_cases // 4,
              "stage_embedded_worker_verified": True, "maple_interception_installed": True,
              "virtual_replies_enabled": True, "storage_writes_enabled": False,
              "console_execution_verified": False, "expected_phase": 13}
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
        p.exit(2, f"Native read-only artifact audit refused: {error}\n")
    if args.output: args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
