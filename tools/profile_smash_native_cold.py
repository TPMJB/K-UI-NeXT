#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Count actual linked SHA256 instructions and stack operand traffic.

Executes SHA init, update and digest from the supplied native child stage.
Generated sample data is checked against hashlib. The full MDE update cost is
an explicitly labeled instruction/operand-count extrapolation; this model has
no cycle, cache, bus or wall-clock timing behavior.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from test_smash_menu_loader_runtime import RuntimeCPU

CTX, DATA, DIGEST, STACK, RETURN = 0x8cd00000, 0x8cd01000, 0x8cd02100, 0x8cdf0000, 0x8cd02000
STACK_BYTES, TARGET_BYTES = 65536, 1848288
COUNTERS = ("instructions", "stack_operand_reads", "stack_operand_read_bytes",
            "stack_operand_writes", "stack_operand_write_bytes")


def require(condition, message):
    if not condition:
        raise ValueError(message)


class HashCPU(RuntimeCPU):
    def __init__(self, stage):
        self.active = False
        super().__init__(b"")
        for segment in stage[2]:
            if segment[0] == 1:
                self.store_block(segment[2], stage[0][segment[1]:segment[1] + segment[4]])
                if segment[5] > segment[4]:
                    self.store_block(segment[2] + segment[4], bytes(segment[5] - segment[4]))
        self.symbols = stage[4]
        self.traffic = Counter()
        self.stack_low = STACK

    def read(self, address, count=4):
        if self.active and STACK - STACK_BYTES <= address < STACK:
            require(address + count <= STACK, "stack read escapes the admitted profiler stack")
            self.traffic["stack_operand_reads"] += 1
            self.traffic["stack_operand_read_bytes"] += count
            self.stack_low = min(self.stack_low, address)
        return super().read(address, count)

    def write(self, address, value, count=4):
        if self.active and STACK - STACK_BYTES <= address < STACK:
            require(address + count <= STACK, "stack write escapes the admitted profiler stack")
            self.traffic["stack_operand_writes"] += 1
            self.traffic["stack_operand_write_bytes"] += count
            self.stack_low = min(self.stack_low, address)
        return super().write(address, value, count)

    def plain(self, op, pc):
        if op & 0xf00f in (0x2004, 0x2005):  # MOV.B / MOV.W Rm,@-Rn.
            n, m = op >> 8 & 15, op >> 4 & 15
            require(n != m, "same-register predecrement is outside this measured SHA path")
            width = 1 if op & 15 == 4 else 2
            self.executing = pc
            self.r[n] = (self.r[n] - width) & 0xffffffff
            self.write(self.r[n], self.r[m], width)
            self.instructions += 1
        elif op & 0xf0fe == 0x4004:  # ROTL / ROTR, including the outgoing T bit.
            n, right = op >> 8 & 15, op & 1
            value = self.r[n]
            self.executing = pc
            self.r[n] = ((value >> 1) | (value << 31)) & 0xffffffff if right else \
                ((value << 1) | (value >> 31)) & 0xffffffff
            self.flag(value & 1 if right else value >> 31)
            self.instructions += 1
        else:
            super().plain(op, pc)

    def call(self, symbol, arguments):
        entry = self.symbols[symbol][0]
        self.pc, self.pr, self.r[15] = entry, RETURN, STACK
        for index, value in enumerate(arguments):
            self.r[4 + index] = value
        saved = self.r[8:15]
        self.instructions, self.traffic, self.stack_low = 0, Counter(), STACK
        self.mmio_reads, self.mmio_writes, self.writes = [], [], []
        self.active = True
        for _ in range(300000):
            if self.pc == RETURN:
                self.active = False
                require(self.r[15] == STACK and self.r[8:15] == saved,
                        "actual SHA function violated its callee-preserved integer ABI")
                require(not self.mmio_reads and not self.mmio_writes,
                        "actual SHA function accessed hardware")
                result = {key: int(self.traffic[key]) for key in COUNTERS if key != "instructions"}
                result.update(instructions=self.instructions, stack_max_depth=STACK - self.stack_low)
                return result
            self.step()
        raise ValueError("actual SHA function exceeded the instruction bound: " + symbol)


def sample(stage, size, pattern):
    cpu = HashCPU(stage)
    if pattern == "zero":
        source = bytes(size)
    elif pattern == "ones":
        source = bytes([255]) * size
    else:
        source = bytes((index * 73 + 0x53) & 255 for index in range(size))
    cpu.store_block(DATA, source)
    init = cpu.call("_kui_sha256_init", [CTX])
    update = cpu.call("_kui_sha256_update", [CTX, DATA, size])
    before = cpu.block(CTX, 104)
    digest = cpu.call("_kui_sha256_digest", [CTX, DIGEST])
    actual, expected = cpu.block(DIGEST, 32), hashlib.sha256(source).digest()
    require(actual == expected, "actual linked SHA disagrees with hashlib for sample " + str(size))
    require(cpu.block(CTX, 104) == before, "actual digest changed its const original context")
    require(cpu.block(DATA, size) == source, "actual linked SHA changed sample input")
    return dict(bytes=size, pattern=pattern, sha256=actual.hex(), hashlib_match=True,
                init=init, update=update, digest=digest)


def audit(elf):
    require(__debug__, "SHA instruction profiler requires Python assertions; run without -O")
    stage = read_elf(elf)
    functions = {}
    for name in ("_kui_sha256_init", "_kui_sha256_update", "_kui_sha256_digest"):
        address, body, _ = symbol_bytes(stage, name, 2)
        functions[name] = dict(address=hex(address), bytes=len(body), sha256=hashlib.sha256(body).hexdigest())
    samples = [sample(stage, size, pattern) for pattern in ("affine", "zero", "ones")
               for size in (32, 64, 96, 160, 224)]
    by_pattern = {pattern: {item["bytes"]: item["update"] for item in samples if item["pattern"] == pattern}
                  for pattern in ("affine", "zero", "ones")}
    reference = by_pattern["affine"]
    for measured in by_pattern.values():
        require(all(measured[size] == reference[size] for size in reference),
                "SHA instruction or operand counts depend on generated input data")
    delta = {key: reference[160][key] - reference[96][key] for key in COUNTERS}
    require(all(reference[224][key] - reference[160][key] == delta[key] and
                reference[96][key] - reference[32][key] == delta[key] for key in COUNTERS),
            "SHA update lacks the measured constant per-64-byte cost")
    blocks, tail = divmod(TARGET_BYTES, 64)
    require(tail == 32, "full MDE projection needs the measured32-byte residual")
    extrapolated = {key: reference[32][key] + blocks * delta[key] for key in COUNTERS}
    require(Path(elf).read_bytes() == stage[0], "stage ELF changed during SHA profiling")
    return dict(schema="kui.smash-native.sha-instruction-cost.v1", passed=True,
                elf_sha256=hashlib.sha256(stage[0]).hexdigest(), actual_linked_functions=functions,
                samples=samples, generated_patterns_identical_cost=True,
                constant_cost_per_additional_64_bytes=delta,
                full_mde_update_extrapolation=dict(bytes=TARGET_BYTES, complete_blocks=blocks,
                    residual_bytes=tail, counted_base_sample_bytes=32, extrapolated=True, **extrapolated),
                original_game_bytes_read_or_embedded=False,
                limits=["Executes all linked SHA init, update and digest instructions for each generated sample; hashlib supplies an independent digest oracle.",
                        "The full MDE update count is an affine extrapolation from32/96/160/224-byte samples, not an execution of all1848288 bytes.",
                        "Counts only executed instructions and explicit stack operand accesses. Instruction fetches, data-cache hits/misses, cycles, memory stalls, card latency and elapsed time are not modeled.",
                        "This SHA-only result does not measure the complete cold launch, resolution metadata proof, SDK validation or VMU initialization."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if not __debug__:
        parser.error("Python assertions must remain enabled")
    result = audit(args.elf)
    rendered = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
