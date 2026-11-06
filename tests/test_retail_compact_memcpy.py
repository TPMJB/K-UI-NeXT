#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual linked compact resident memcpy against byte semantics.

The scalar model checks every data access, caller-owned buffer and integer
ABI boundary. It does not model hardware cache behavior or elapsed time.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
from test_native_standalone_foreground_gin import Gin
from check_vmu_native_worker_gateway import read_elf, symbol_bytes


def audit(elf):
    image = read_elf(elf)
    entry, code, _ = symbol_bytes(image, "_memcpy", 2)
    assert image[4].get("__kui_retail_compact_word_copy", (None,))[0] == 1
    # This exact leaf uses only caller-saved integer registers, T and ordinary
    # loads/stores; no stack/FPU/SQ/CCR instruction is hidden outside the model.
    expected = (0x6743, 0x6043, 0x205b, 0xc803, 0x8b08, 0xe104,
                0x3612, 0x8b05, 0x6256, 0x2422, 0x7404, 0x76fc,
                0x3612, 0x89f9, 0x2668, 0x8904, 0x6254, 0x2420,
                0x7401, 0x4610, 0x8bfa, 0x000b, 0x6073)
    assert code == b"".join(op.to_bytes(2, "little") for op in expected)
    symbols = image[4]
    bottom = symbols["__retail_hook_stack_bottom"][0]
    top = symbols["__retail_hook_stack"][0]
    bss_end = symbols["__retail_resident_bss_end"][0]
    assert bss_end <= bottom == 0x8c007ea0 and top == 0x8c008000

    class CopyCPU(Gin):
        live = False

        def read(self, address, count=4):
            if self.live and not entry <= address < entry + len(code):
                source = self.source_arg & 0x1fffffff
                physical = address & 0x1fffffff
                assert source <= physical <= source + self.count - count
                assert count in (1, 4) and (count == 1 or not address % 4)
                self.data_reads += 1
            return super().read(address, count)

        def write(self, address, value, count=4):
            if self.live:
                destination = self.destination_arg & 0x1fffffff
                physical = address & 0x1fffffff
                assert destination <= physical <= destination + self.count - count
                assert count in (1, 4) and (count == 1 or not address % 4)
            super().write(address, value, count)

    cpu = CopyCPU(b"", b"", b"")
    cpu.store_block(entry, code)
    cases = instructions = 0

    def exercise(source_alignment, destination_alignment, count, aliases, direction):
        nonlocal cases, instructions
        source_base, destination_base = (0x8c400000, 0x8c600000) if not direction else (0x8c600000, 0x8c400000)
        source = (source_base + source_alignment) | aliases[0]
        destination = (destination_base + destination_alignment) | aliases[1]
        payload = bytes((i * 29 + source_alignment * 7 + destination_alignment * 11) & 255 for i in range(count))
        cpu.live = False
        cpu.store_block(source - 8, b"\x5a" * 8 + payload + b"\x6b" * 8)
        cpu.store_block(destination - 8, b"\xa7" * (count + 16))
        cpu.source_arg, cpu.destination_arg, cpu.count = source, destination, count
        cpu.r = [(0x10203040 + i * 0x01030507) & 0xffffffff for i in range(16)]
        cpu.r[4:7] = destination, source, count
        before = list(cpu.r)
        cpu.pr, cpu.pc = 0x8c003000, entry
        cpu.sr = 0x700000f0 if cases & 1 else 0x400000f0
        special = cpu.sr & ~1, cpu.pr, cpu.gbr, cpu.vbr, cpu.mach, cpu.macl
        cpu.writes, cpu.data_reads = [], 0
        start, cpu.live = cpu.instructions, True
        for _ in range(6 * count + 40):
            if cpu.pc == cpu.pr:
                break
            cpu.step()
        else:
            raise AssertionError("Compact memcpy exceeded its instruction bound")
        cpu.live = False
        assert cpu.r[0] == destination and cpu.r[8:] == before[8:]
        assert (cpu.sr & ~1, cpu.pr, cpu.gbr, cpu.vbr, cpu.mach, cpu.macl) == special
        assert cpu.block(destination - 8, count + 16) == b"\xa7" * 8 + payload + b"\xa7" * 8
        assert cpu.block(source - 8, count + 16) == b"\x5a" * 8 + payload + b"\x6b" * 8
        assert sum(write[3] for write in cpu.writes) == count
        assert not cpu.mmio_reads and not cpu.mmio_writes
        if not (source_alignment | destination_alignment) & 3:
            assert sum(write[3] == 4 for write in cpu.writes) == count // 4
            assert sum(write[3] == 1 for write in cpu.writes) == count % 4
        else:
            assert all(write[3] == 1 for write in cpu.writes)
        instructions += cpu.instructions - start
        cases += 1

    for aliases in ((0, 0), (0x20000000, 0), (0, 0x20000000), (0x20000000, 0x20000000)):
        for direction in (0, 1):
            for source_alignment in range(4):
                for destination_alignment in range(4):
                    for count in range(133):
                        exercise(source_alignment, destination_alignment, count, aliases, direction)
            for count in (512, 513, 2048, 65537):
                exercise(0, 0, count, aliases, direction)
    return dict(schema="kui.retail.compact-memcpy.linked.v1", status="PASS",
        elf_sha256=hashlib.sha256(image[0]).hexdigest(), function_address=hex(entry),
        function_bytes=len(code), cases=cases, actual_SH_instructions=instructions,
        all_pointer_alignments=True, all_lengths_0_through_132=True,
        large_lengths=[512, 513, 2048, 65537], P1_P2_alias_combinations=4,
        both_nonoverlap_directions=True, exact_data_read_write_bounds=True,
        unaligned_byte_fallback=True, word_tail_exact=True, original_return_pointer=True,
        callee_saved_registers_PR_SP_preserved=True, no_stack_access=True,
        no_FPU_or_store_queue_instructions=True, no_MMIO=True,
        bss_end=hex(bss_end), stack_bottom=hex(bottom), stack_top=hex(top),
        unchanged_stack_bytes=top-bottom, reservation_increase=False,
        scope="Actual linked scalar SH copy; no hardware cache or elapsed-time claim")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = audit(args.elf)
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
