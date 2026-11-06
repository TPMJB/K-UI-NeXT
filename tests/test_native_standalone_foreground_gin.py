#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute owner-supplied Gin copies and check the linked low resident survives.

This tests CPU-copy ownership only. It does not execute resident requests,
admit the canary manifest as a real filesystem, or certify a secondary game.
Original game bytes remain external and are never included in the report.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
from check_vmu_native_worker_gateway import read_elf
from test_vmu_native_heap_execution import MASK, signed
from test_vmu_native_sdk_poll_execution import PollSH
from sh4_delay_slots import validate_delay_slot

OWNERS = {
    "GINXFER.BIN": "fa12196993a7c757a78565963e257cd33443f80b3f226729fdc1cf7da51ed5b3",
    "QUAKE3/INTRO.BIN": "c8d4ea08a23a21023ddd368dcae7750fe4322afa73815c4eca8351e9f49d535b",
    "QUAKE3/Q3_USA.BIN": "42b40b4f2e94b56f16d81fbb132987a48f231a5d02962d0498e8e5a917cdd5ae"}
LOW, TOP = 0x8c004000, 0x8c007d00
GIN_STAGE, GIN_ENTRY, COPY_STORE = 0x8cfd0000, 0x8cfd2000, 0x8cfd2018
TARGET, LOAD = 0xac010000, 0x8c010000
ONE = (1, 0, 0, 0xfff1)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(test, message):
    if not test:
        raise ValueError(message)


def low_image(path):
    image = read_elf(path)
    data, _, segments, _, symbols = image
    require(symbols.get("__kui_native_standalone_foreground_resident") == ONE,
            "Resident lacks the explicit foreground-control marker")
    require(symbols["_start"][0] == LOW and
            symbols["__retail_hook_stack_bottom"][0] == 0x8c007ba0 and
            symbols["__retail_hook_stack"][0] == TOP,
            "Resident or352-byte private stack geometry changed")
    begin = symbols["__retail_resident_bss_begin"][0]
    end = symbols["__retail_resident_bss_end"][0]
    require(LOW < symbols["__retail_resident_binary_end"][0] <= begin <= end <= 0x8c007ba0,
            "Resident initialized/BSS bytes reach private stack")
    loads = [p for p in segments if p[0] == 1]
    require(len(loads) == 1 and loads[0][2] == loads[0][3] == LOW,
            "Resident must be one wholly low-memory load segment")
    p = loads[0]
    require(0 < p[4] <= p[5] and LOW+p[4] <= begin and LOW+p[5] == end,
            "Resident ELF segment does not match exported bounds")
    ranges = []
    for name, size in (("_manifest", 1020), ("_standalone_reader_region", 1888)):
        address, actual, kind, _ = symbols[name]
        require(kind == 1 and actual == size and begin <= address < address+size <= end,
                "Manifest/reader state is not wholly low resident BSS: "+name)
        ranges.append(dict(name=name, begin=hex(address), end=hex(address+size), bytes=size))
    return data[p[1]:p[1]+p[4]], dict(elf_sha256=digest(data),
        initialized_begin=hex(LOW), initialized_end=hex(LOW+p[4]),
        bss_begin=hex(begin), bss_end=hex(end),
        private_stack_begin="0x8c007ba0", private_stack_end=hex(TOP),
        retained_objects=ranges)


class Gin(PollSH):
    """Existing scalar SH decoder plus the exact Gin control-register forms."""
    def __init__(self, gin, app, resident):
        super().__init__(b"", 2, 1)
        self.r[15], self.pr = 0x8c00f214, 0x8c0109d6
        self.gbr = self.mach = self.macl = self.spc = self.ssr = 0
        self.vbr = 0x8c00f400
        # Exact bootstrap transfer ABI: source is rounded down below Gin,
        # count is the complete number of2048-byte sectors, with no36-byte
        # slice of the secondary executable. Unknown final padding is zero.
        self.source = (GIN_STAGE-len(app)) & 0xffff0000
        self.sectors = (len(app)+2047)//2048
        self.store_block(GIN_STAGE, gin)
        self.store_block(self.source, app+bytes(self.sectors*2048-len(app)))
        for offset, value in ((16, self.source), (20, self.sectors), (24, 4), (28, 1)):
            self.write(GIN_STAGE+offset, value)
        self.protected = ((LOW-32, TOP+32), (0x8c0000bc, 0x8c0000c4),
                          (0x8c000100, 0x8c000160))
        for first, last in self.protected:
            self.store_block(first, bytes((i*17+0x53)&255 for i in range(last-first)))
        self.store_block(LOW, resident)
        self.before = [(first, self.block(first, last-first)) for first, last in self.protected]
        self.pc, self.executing, self.instructions = GIN_ENTRY, GIN_ENTRY, 0
        self.writes = []
        self.copy_checkpoint = None

    def plain(self, op, pc):
        n = op >> 8 & 15
        form = op & 0xf0ff
        if form == 0x4020:  # SHAL
            value = self.r[n]; self.r[n] = value << 1 & MASK
            self.flag(value >> 31); self.instructions += 1
        elif form == 0x4021:  # SHAR
            value = self.r[n]; self.r[n] = signed(value, 32) >> 1 & MASK
            self.flag(value & 1); self.instructions += 1
        elif form in (0x400a, 0x401a, 0x402a, 0x402e, 0x401e, 0x404e, 0x403e):
            name = {0x400a:"mach", 0x401a:"macl", 0x402a:"pr", 0x402e:"vbr",
                    0x401e:"gbr", 0x404e:"spc", 0x403e:"ssr"}[form]
            setattr(self, name, self.r[n]); self.instructions += 1
        else:
            super().plain(op, pc)

    def step(self):
        pc = self.pc
        self.executing = pc
        if pc == GIN_ENTRY+32:
            self.copy_checkpoint = self.block(LOAD, self.sectors*2048)
        op = self.read(pc, 2)
        target, delay, n = None, False, op >> 8 & 15
        if op >> 12 in (10, 11):
            target, delay = pc+4+2*signed(op&4095, 12) & MASK, True
            if op >> 12 == 11:
                self.pr = pc+4
        elif op & 0xf0ff in (0x400b, 0x402b):
            target, delay = self.r[n], True
            if op & 0xf0ff == 0x400b:
                self.pr = pc+4
        elif op == 0x000b:
            target, delay = self.pr, True
        elif op == 0x002b:  # RTE; scalar register bank only, stated below.
            target, self.sr, delay = self.spc, self.ssr, True
        elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
            taken = bool(self.sr&1) == (op >> 8 in (0x89, 0x8d))
            delay = op >> 8 in (0x8d, 0x8f)
            target = pc+4+2*signed(op&255, 8) if taken else pc+(4 if delay else 2)
        if target is None:
            self.plain(op, pc); self.pc = pc+2
        else:
            self.instructions += 1
            if delay:
                slot = self.read(pc+2, 2)
                validate_delay_slot(slot, pc)
                self.plain(slot, pc+2)
            self.pc = target & MASK


def exercise(gin, app, resident, name):
    cpu = Gin(gin, app, resident)
    for _ in range(6000000):
        if cpu.pc == TARGET:
            break
        cpu.step()
    require(cpu.pc == cpu.spc == TARGET and cpu.r[15] == cpu.vbr == 0x8c00f400 and
            cpu.ssr == cpu.sr == 0x700000f0, "Gin did not reach the exact restart state")
    require(cpu.copy_checkpoint is not None and cpu.copy_checkpoint[:len(app)] == app,
            "First Gin copy is not the entire actual executable")
    require(not cpu.mmio_reads and not cpu.mmio_writes,
            "Gin unexpectedly touched unmodeled hardware")
    copy = [(address, width) for pc, address, _, width in cpu.writes if pc == COPY_STORE]
    require(len(copy) == cpu.sectors*512 and all(width == 4 for _, width in copy) and
            min(a for a, _ in copy) == LOAD and max(a+n for a, n in copy) == LOAD+cpu.sectors*2048,
            "Actual Gin copy stores have changed coverage")
    ranges = []
    for first, before in cpu.before:
        last = first+len(before)
        touching = [(pc, address, width) for pc, address, _, width in cpu.writes
                    if (address&0x1fffffff) < (last&0x1fffffff) and
                    (address&0x1fffffff)+width > (first&0x1fffffff)]
        require(not touching and cpu.block(first, len(before)) == before,
                "Gin writes the low resident, retained state, stack or a protected vector")
        ranges.append(dict(begin=hex(first), end=hex(last), bytes=len(before),
                           sha256=digest(before), writes=0, unchanged=True))
    result = dict(file=name, actual_bytes=len(app), actual_sha256=digest(app),
        staged_begin=hex(cpu.source), sectors=cpu.sectors, instructions=cpu.instructions,
        first_copy_begin=hex(LOAD), first_copy_end=hex(LOAD+cpu.sectors*2048),
        first_copy_actual_file_bytes_exact=True, actual_copy_store_pc=hex(COPY_STORE),
        copy_longword_stores=len(copy), rte_target=hex(cpu.pc), vbr=hex(cpu.vbr),
        sp=hex(cpu.r[15]), ssr=hex(cpu.ssr), protected_ranges=ranges,
        post_gin_payload_differing_bytes=sum(a != b for a, b in zip(app, cpu.block(LOAD, len(app)))))
    # Continue only the original CRT's SEGA fill and scalar initializer. The
    # fixture entry addresses bind these exact original-file SHA256 owners.
    # Entry dispatch before this CRT and the next SDK helper are out of scope.
    entry = {"QUAKE3/INTRO.BIN":0x8c02cd20, "QUAKE3/Q3_USA.BIN":0x8c118340}[name]
    source_hash = digest(cpu.block(entry, 0x9a))
    cpu.pc = entry
    start = cpu.instructions
    cpu.writes = []
    for _ in range(12000000):
        if cpu.pc == entry+0x1a:
            break
        cpu.step()
    require(cpu.pc == entry+0x1a, "Original scalar initializer exceeded its instruction bound")
    for first, before in cpu.before:
        last = first+len(before)
        require(not any((address&0x1fffffff) < (last&0x1fffffff) and
                        (address&0x1fffffff)+width > (first&0x1fffffff)
                        for _, address, _, width in cpu.writes) and
                cpu.block(first, len(before)) == before,
                "Original CRT changed the low resident or protected vectors")
    stores = []
    for offset, label in ((0x0e,"SEGA_fill"), (0x4a,"word_zero"),
                          (0x62,"copy1"), (0x74,"byte_zero"), (0x8c,"copy2")):
        writes = [(a, v, n) for pc, a, v, n in cpu.writes if pc == entry+offset]
        stores.append(dict(role=label, pc=hex(entry+offset), stores=len(writes),
            begin=hex(min(a for a, _, _ in writes)) if writes else None,
            end=hex(max(a+n for a, _, n in writes)) if writes else None,
            bytes=sum(n for _, _, n in writes)))
    require(source_hash == digest(cpu.block(entry, 0x9a)), "CRT initialization rewrote its own code")
    result["secondary_scalar_initializer"] = dict(entry=hex(entry),
        stop_before_next_sdk_helper=hex(cpu.pc), instructions=cpu.instructions-start,
        original_code_prefix_sha256=source_hash, stores=stores,
        low_resident_and_protected_vectors_writes=0, all_protected_bytes_unchanged=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--runtime-dir", required=True, type=Path)
    parser.add_argument("--report", "--output", dest="report", required=True, type=Path)
    args = parser.parse_args()
    if not __debug__:
        parser.error("Python assertions must remain enabled")
    files = {name:(args.runtime_dir/name).read_bytes() for name in OWNERS}
    require(all(digest(files[name]) == value for name, value in OWNERS.items()),
            "Original Gin/secondary input differs from the reviewed owner files")
    elf = args.build/"resident-sci.elf"
    initial_elf = elf.read_bytes()
    resident, geometry = low_image(elf)
    sources = ("tests/test_native_standalone_foreground_gin.py",
        "tests/test_vmu_native_heap_execution.py", "tests/test_vmu_native_sdk_poll_execution.py",
        "tools/profile_vmu_native_release_sh.py", "tools/sh4_delay_slots.py",
        "tools/check_vmu_native_worker_gateway.py")
    hashes = {name:digest((ROOT/name).read_bytes()) for name in sources}
    cases = [exercise(files["GINXFER.BIN"], files[name], resident, name)
             for name in OWNERS if name != "GINXFER.BIN"]
    require(elf.read_bytes() == initial_elf and all(digest((ROOT/name).read_bytes()) == value
            for name, value in hashes.items()), "Resident or proof source changed during replay")
    report = dict(schema="kui.native.foreground.actual-gin-preservation.v1", passed=True,
        resident=geometry, gin_sha256=OWNERS["GINXFER.BIN"], proof_sources_sha256=hashes,
        original_bytes_embedded=False, original_instruction_or_helper_folds=False, cases=cases,
        limits=["Executes actual Gin instructions with the complete real executable payloads; no36-byte slicing.",
            "Loader-header source/count fields are explicit bootstrap ABI inputs; complete filesystem/bootstrap transfer is not replayed.",
            "Unknown last-sector ISO padding is zero-filled; store addresses and preservation do not depend on those padding bytes.",
            "Low BSS/manifest/region/stack bytes are arbitrary canaries, not an admitted runtime filesystem seed; resident requests do not execute here.",
            "Flat aliased RAM and one scalar GPR bank; no cache, banked-register, IRQ, peripheral or console-timing model.",
            "After recording RTE, resumes only at the identified original CRT SEGA/scalar initializer and stops before its next SDK helper; entry dispatch and later SDK/game behavior are excluded."])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(dict(passed=True, cases=len(cases), report=str(args.report))))


if __name__ == "__main__":
    main()
