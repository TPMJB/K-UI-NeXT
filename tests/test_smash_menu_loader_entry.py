#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked low menu loader's integer capture/fallback assembly.

The production ELF supplies all executed assembly. Its C dispatcher is
substituted with one ABI-conforming fallback: caller-clobbered registers and
integer special state change, but the original cache/mode/bank survive. This
test establishes exact integer restoration, not the C selection/media path.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
from check_vmu_native_worker_gateway import read_elf
from test_native_standalone_foreground_gin import Gin

ENTRY, TARGET, CCR = 0xac008300, 0xac010000, 0xff00001c
STACK, FRAME = 0xac00c000, 0x8c00f3ac


class LoaderCPU(Gin):
    def __init__(self, ccr):
        super().__init__(b"", b"", b"")
        self.original_ccr = ccr
        self.mmio_reads = []

    def read(self, address, count=4):
        if address == CCR:
            assert count == 4
            self.mmio_reads.append(address)
            return self.original_ccr
        return super().read(address, count)

    def plain(self, op, pc):
        n, form = op >> 8 & 15, op & 0xf0ff
        pushes = {0x4003: "sr", 0x4023: "vbr", 0x4013: "gbr",
                  0x4002: "mach", 0x4012: "macl"}
        pops = {0x4006: "mach", 0x4016: "macl", 0x4017: "gbr",
                0x4027: "vbr", 0x4037: "ssr"}
        if form in pushes:
            self.r[n] -= 4
            self.write(self.r[n], getattr(self, pushes[form]))
            self.instructions += 1
        elif form in pops:
            setattr(self, pops[form], self.read(self.r[n]))
            self.r[n] += 4
            self.instructions += 1
        else:
            super().plain(op, pc)


def exercise(image, segments, symbols, pattern, ccr):
    cpu = LoaderCPU(ccr)
    for segment in segments:
        if segment[0] == 1:
            cpu.store_block(segment[2], image[segment[1]:segment[1] + segment[4]])
    cpu.r = [((0x12345678 + i * 0x11111111) ^ pattern) & 0xffffffff for i in range(16)]
    cpu.r[15] = 0x8c00f400
    cpu.sr = 0x700000f0 | (pattern & 1)
    cpu.pr = cpu.mach = 0
    cpu.macl, cpu.gbr, cpu.vbr = 40, 0x8c000000, 0x8c00f400
    cpu.pc = ENTRY
    before = (list(cpu.r), cpu.sr, cpu.pr, cpu.mach, cpu.macl, cpu.gbr, cpu.vbr)
    main = symbols["_kui_smash_menu_loader_main"][0]
    calls = 0
    for _ in range(200):
        if cpu.pc == main:
            calls += 1
            assert cpu.r[4] == FRAME and cpu.r[5] == ccr and cpu.r[15] == STACK
            frame = [cpu.read(FRAME + 4 * i) for i in range(21)]
            expected = [before[4], before[3], before[5], before[6], before[1], before[2]]
            expected += list(reversed(before[0][:15]))
            assert frame == expected
            # Stand in for an ABI-conforming nonnative C return. Preserve
            # callee-saved registers, stack, CCR and SR bank/mode bits.
            for n in range(8):
                cpu.r[n] = 0xa5a50000 + n
            cpu.mach, cpu.macl = 0xabcd, 0xfeed
            cpu.gbr, cpu.vbr = 0x8c100000, 0x8c200000
            cpu.sr ^= 1
            cpu.pc = cpu.pr
        if cpu.pc == TARGET:
            break
        cpu.step()
    else:
        raise AssertionError("Linked handoff fallback exceeded its instruction bound")
    assert calls == 1
    assert (cpu.r, cpu.sr, cpu.pr, cpu.mach, cpu.macl, cpu.gbr, cpu.vbr) == before
    assert cpu.mmio_reads == [CCR] and not cpu.mmio_writes
    return cpu.instructions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    image, _, segments, _, symbols = read_elf(args.elf)
    assert symbols["_kui_smash_menu_loader_entry"][0] == ENTRY
    assert symbols["__smash_menu_loader_stack_top"][0] == STACK
    bottom = symbols["__smash_menu_loader_stack_bottom"][0]
    assert bottom == STACK - 2048
    assert ENTRY < symbols["__smash_menu_loader_binary_end"][0] <= symbols["__smash_menu_loader_bss_begin"][0]
    assert symbols["__smash_menu_loader_bss_end"][0] <= bottom
    # Establish the hardware bank statement directly from the executed
    # prefix: all actual R0..R14 are pushed before any one can be changed;
    # PR/SR/VBR/GBR/MACH/MACL follow without an SR write or bank switch.
    cpu = LoaderCPU(0x909)
    for segment in segments:
        if segment[0] == 1:
            cpu.store_block(segment[2], image[segment[1]:segment[1] + segment[4]])
    assert [cpu.read(ENTRY + 2 * i, 2) for i in range(15)] == [0x2f06 | i << 4 for i in range(15)]
    assert [cpu.read(ENTRY + 30 + 2 * i, 2) for i in range(6)] == [0x4f22, 0x4f03, 0x4f23, 0x4f13, 0x4f02, 0x4f12]
    ccrs = [0, 0x100, 0x909, 0x109]
    instructions = [exercise(image, segments, symbols, pattern, ccr)
                    for pattern in range(8) for ccr in ccrs]
    report = dict(schema="kui.smash-menu.loader-entry.v1", passed=True,
                  elf_sha256=hashlib.sha256(image).hexdigest(), entry=hex(ENTRY),
                  fallback_target=hex(TARGET), integer_frame=hex(FRAME), integer_frame_words=21,
                  private_stack_begin=hex(bottom), private_stack_end=hex(STACK),
                  fallback_cases=len(instructions), fallback_instructions=sorted(set(instructions)),
                  exact_register_and_integer_control_restoration=True,
                  no_sr_bank_change_before_integer_capture=True,
                  no_cache_register_writes=True,
                  limits=["The C dispatcher is substituted by an ABI-conforming fallback; its selection, quiescence, media and native-entry paths are not executed.",
                          "Register banking is not emulated. The linked prefix proves no SR/bank change precedes capture of the actual active R0..R7.",
                          "No cycle, cache timing, game, SDK or VMU execution claim is made."])
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
