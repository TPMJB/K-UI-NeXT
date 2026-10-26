#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay Rayman's original A2 classification against actual virtual replies.

Supply the exact owner-provided Rayman2 startup. No retail bytes are bundled.
The model executes the original SDK attachment/identity/geometry parsers,
strcmp/strncmp, higher device classifier, complete game device-kind update and
card-selection gate. It initializes only the BSS/input fixtures needed by those
bounded functions, and does not claim full console enumeration or mounting.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
STARTUP_SHA256 = "9e34357880d5fbbca1559ce8797baed66c6b4370e967b2af23f07cba9b3a7099"
spec = importlib.util.spec_from_file_location("kui_heap_execution", ROOT / "tests/test_vmu_native_heap_execution.py")
heap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(heap)
from profile_vmu_native_release_sh import ReleaseSH
from check_vmu_native_worker_gateway import read_elf
from check_vmu_native_persistent import prepared_symbol_records
from sh4_delay_slots import validate_delay_slot

SOURCE_FILES = ("tools/prove_rayman_vmu_identity.py", "src/core/vmu_emulation.c",
    "include/kui/vmu_emulation.h", "tools/profile_vmu_native_release_sh.py",
    "tests/test_vmu_native_heap_execution.py", "tools/check_vmu_native_worker_gateway.py",
    "tools/check_vmu_native_persistent.py", "tools/sh4_delay_slots.py")

class SH(heap.SH):
    def plain(self, op, pc):
        self.executing = pc
        n, m = (op >> 8)&15, (op >> 4)&15
        if op & 0xf00f == 0x0007:  # MUL.L Rm,Rn
            self.macl = self.r[n]*self.r[m] & heap.MASK
        elif op & 0xf0ff == 0x001a: # STS MACL,Rn
            self.r[n] = self.macl
        elif op & 0xf00f == 0x000e: # MOV.L @(R0,Rm),Rn
            self.r[n] = self.read((self.r[0]+self.r[m])&heap.MASK)
        elif op & 0xf00f == 0x6000: # MOV.B @Rm,Rn
            self.r[n] = heap.signed(self.read(self.r[m],1),8)&heap.MASK
        elif op & 0xff00 == 0x8400: # MOV.B @(disp,Rm),R0
            self.r[0] = heap.signed(self.read(self.r[m]+(op&15),1),8)&heap.MASK
        elif op & 0xf00f == 0x600c: # EXTU.B Rm,Rn
            self.r[n] = self.r[m]&255
        elif op & 0xf00f == 0x6004: # MOV.B @Rm+,Rn
            self.r[n] = heap.signed(self.read(self.r[m],1),8)&heap.MASK
            self.r[m] = (self.r[m]+1)&heap.MASK
        elif op & 0xf00f == 0x2000: # MOV.B Rm,@Rn
            self.write(self.r[n],self.r[m],1)
        elif op & 0xff00 == 0x8000: # MOV.B R0,@(disp,Rm)
            self.write(self.r[m]+(op&15),self.r[0],1)
        elif op & 0xff00 == 0x8100: # MOV.W R0,@(disp,Rm)
            self.write(self.r[m]+2*(op&15),self.r[0],2)
        elif op & 0xf00f == 0x0004: # MOV.B Rm,@(R0,Rn)
            self.write(self.r[0]+self.r[n],self.r[m],1)
        elif op & 0xf00f == 0x0005: # MOV.W Rm,@(R0,Rn)
            self.write(self.r[0]+self.r[n],self.r[m],2)
        elif op & 0xf00f == 0x000c: # MOV.B @(R0,Rm),Rn
            self.r[n] = heap.signed(self.read(self.r[0]+self.r[m],1),8)&heap.MASK
        elif op & 0xf00f == 0x600b: # NEG Rm,Rn
            self.r[n] = -self.r[m]&heap.MASK
        elif op & 0xf00f == 0x6007: # NOT Rm,Rn
            self.r[n] = ~self.r[m]&heap.MASK
        elif op & 0xf00f == 0x200a: # XOR Rm,Rn
            self.r[n] ^= self.r[m]
        elif op & 0xf00f == 0x3003: # CMP/GE Rm,Rn (signed)
            self.flag(heap.signed(self.r[n],32)>=heap.signed(self.r[m],32))
        elif op & 0xf0ff == 0x4018: # SHLL8 Rn
            self.r[n] = self.r[n]<<8&heap.MASK
        elif op & 0xf0ff == 0x4028: # SHLL16 Rn
            self.r[n] = self.r[n]<<16&heap.MASK
        elif op & 0xf00f == 0x400d: # SHLD Rm,Rn
            shift=self.r[m]
            self.r[n] = (self.r[n]<<(shift&31)&heap.MASK) if not shift&0x80000000 else (0 if not shift&31 else self.r[n]>>(32-(shift&31)))
        elif op & 0xff00 == 0x8800: # CMP/EQ #imm,R0
            self.flag(self.r[0]==(heap.signed(op&255,8)&heap.MASK))
        elif op & 0xf00f == 0x600f: # EXTS.W Rm,Rn
            self.r[n] = heap.signed(self.r[m]&65535,16)&heap.MASK
        elif op & 0xff00 == 0x8500: # MOV.W @(disp,Rm),R0
            self.r[0] = heap.signed(self.read(self.r[m]+2*(op&15),2),16)&heap.MASK
        elif op & 0xf00f == 0x3007: # CMP/GT Rm,Rn
            self.flag(heap.signed(self.r[n],32)>heap.signed(self.r[m],32))
        elif op & 0xf0ff == 0x4011: # CMP/PZ Rn
            self.flag(heap.signed(self.r[n],32)>=0)
        elif op & 0xf00f == 0x200c: # CMP/STR Rm,Rn
            v=self.r[n]^self.r[m]
            self.flag(any(((v>>(8*i))&255)==0 for i in range(4)))
        elif op & 0xf0ff == 0x0029: # MOVT Rn
            self.r[n] = self.sr&1
        else:
            return super().plain(op,pc)
        self.instructions += 1
        self.executing = pc

WRAPPER = r"""
#include "kui/vmu_emulation.h"
#include <stddef.h>
#include <stdint.h>
static kui_vmu_emulation_t card;
static int read_block(void *c,uint16_t b,uint8_t *out) {(void)c;(void)b;(void)out;return -1;}
static int commit_block(void *c,uint16_t b,const uint8_t *in) {(void)c;(void)b;(void)in;return -1;}
size_t make_reply(uint8_t *out,unsigned command) {
    const struct kui_vmu_emulation_io io={0,read_block,commit_block};
    const uint8_t request[4]={(uint8_t)command,2,0,0};
    size_t bytes=0;
    if(kui_vmu_emulation_init(&card,2,&io))return 0;
    if(kui_vmu_emulation_handle(&card,request,4,out,196,&bytes)!=1)return 0;
    return bytes;
}
"""


class Target(ReleaseSH):
    """Execute the linked endpoint without any protocol or libc substitution."""
    FUNCTIONS = {"_kui_vmu_emulation_init", "_kui_vmu_emulation_handle",
        "_kui_vmu_emulation_reset", "_put32@vmu_emulation.c", "_memset", "_memcpy",
        "_memcmp", "___movmemSI12_i4"}
    OBJECTS = {"_name.2@vmu_emulation.c", "_producer.1@vmu_emulation.c",
        "_version.0@vmu_emulation.c"}
    STATE, IO, REQUEST, RESPONSE, LENGTH, STACK = (
        0xac500000, 0xac501000, 0xac502000, 0xac503020, 0xac504000, 0xac7f0000)
    RETURN = 0xdeadbeee

    def __init__(self, path):
        super().__init__(path)
        self.functions, self.objects = {}, {}
        for name, address, count, kind, owner in prepared_symbol_records(self.elf):
            if name not in self.FUNCTIONS | self.OBJECTS:
                continue
            destination = self.functions if name in self.FUNCTIONS else self.objects
            assert name not in destination and count and owner < len(self.elf[3])
            section = self.elf[3][owner]
            assert section[1] == 1 and section[2] & 2
            assert section[3] <= address < address + count <= section[3] + section[5]
            assert kind == (2 if destination is self.functions else 1)
            assert bool(section[2] & 4) == (destination is self.functions)
            at = section[4] + address - section[3]
            destination[name] = (address, self.elf[0][at:at + count])
        assert set(self.functions) == self.FUNCTIONS and set(self.objects) == self.OBJECTS
        self.recording = False
        self.executed = set()

    def write(self, address, value, count=4):
        if getattr(self, "recording", False):
            at = address & 0x1fffffff
            ranges = ((self.STATE, 0x1000), (self.RESPONSE, 524), (self.LENGTH, 4),
                (self.STACK - 4096, 4096 + 16))
            assert any((begin & 0x1fffffff) <= at < at + count <=
                (begin & 0x1fffffff) + size for begin, size in ranges), (
                    "Compiled endpoint wrote outside private state/output/stack", hex(address), count)
        super().write(address, value, count)

    def instruction(self, address):
        at = address & 0x1fffffff
        owners = [name for name, (begin, body) in self.functions.items()
            if (begin & 0x1fffffff) <= at < at + 2 <= (begin & 0x1fffffff) + len(body)]
        assert len(owners) == 1, ("Compiled endpoint left typed function closure", hex(address))
        self.executed.add(owners[0])
        return self.read(address, 2)

    def plain(self, op, pc):
        if op & 0xff00 == 0x8400:  # MOV.B @(disp,Rm),R0
            self.executing = pc
            self.r[0] = heap.signed(self.read(self.r[(op >> 4) & 15] + (op & 15), 1), 8) & heap.MASK
            self.instructions += 1
        elif op & 0xf0ff == 0x4019:  # SHLR8 Rn
            self.executing = pc
            self.r[(op >> 8) & 15] >>= 8
            self.instructions += 1
        elif op & 0xf0ff == 0x4018:  # SHLL8 Rn; does not change T
            self.executing = pc
            n = (op >> 8) & 15
            self.r[n] = self.r[n] << 8 & heap.MASK
            self.instructions += 1
        else:
            super().plain(op, pc)

    def call_endpoint(self, name, args):
        assert not self.stubs
        before, before_sr = self.r[8:15].copy(), self.sr
        self.pc, self.pr, self.r[15] = self.functions[name][0], self.RETURN, self.STACK
        for i, value in enumerate(args):
            if i < 4:
                self.r[4 + i] = value
            else:
                self.write(self.STACK + 4 * (i - 4), value)
        self.instructions = 0
        self.executed.clear()
        self.recording = True
        try:
            for _ in range(20000):
                pc = self.pc
                if pc == self.RETURN:
                    assert self.r[15] == self.STACK and self.r[8:15] == before
                    assert self.sr & ~1 == before_sr & ~1
                    return {"result": self.r[0], "instructions": self.instructions,
                        "executed_functions": sorted(self.executed)}
                op = self.instruction(pc)
                target, delay, n = None, False, (op >> 8) & 15
                if op >> 12 in (0xa, 0xb):
                    target = (pc + 4 + 2 * heap.signed(op & 0xfff, 12)) & heap.MASK
                    delay = True
                    if op >> 12 == 0xb:
                        self.pr = pc + 4
                elif op & 0xf0ff in (0x400b, 0x402b):
                    target, delay = self.r[n], True
                    if op & 0xf0ff == 0x400b:
                        self.pr = pc + 4
                elif op == 0xb:
                    target, delay = self.pr, True
                elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
                    taken = bool(self.sr & 1) == (op >> 8 in (0x89, 0x8d))
                    delay = op >> 8 in (0x8d, 0x8f)
                    target = pc + 4 + 2 * heap.signed(op & 255, 8) if taken else pc + (4 if delay else 2)
                if target is None:
                    self.plain(op, pc)
                    self.pc = pc + 2
                else:
                    self.instructions += 1
                    if delay:
                        slot = self.instruction(pc + 2)
                        validate_delay_slot(slot, pc)
                        self.plain(slot, pc + 2)
                    self.pc = target & heap.MASK
            raise AssertionError("Compiled endpoint exceeded bounded instruction count")
        finally:
            self.recording = False

    def reply(self, command):
        # Nonzero callback addresses satisfy init but are outside the executable
        # closure: any attempted storage callback would fail this replay.
        self.store_block(self.IO, bytes.fromhex("7856341200f05fac04f05fac"))
        request = bytes([command, 2, 0, 0]) if command in (1, 2) else bytes.fromhex("0a0200020000000200000000")
        self.store_block(self.REQUEST, request)
        self.store_block(self.RESPONSE - 16, bytes([0xa5]) * (524 + 32))
        initialization = self.call_endpoint("_kui_vmu_emulation_init", [self.STATE, 2, self.IO])
        assert initialization["result"] == 0
        io_before = self.block(self.IO, 12)
        dispatch = self.call_endpoint("_kui_vmu_emulation_handle", [self.STATE,
            self.REQUEST, len(request), self.RESPONSE, 524, self.LENGTH])
        count = self.read(self.LENGTH)
        assert dispatch["result"] == 1 and count == {1: 116, 2: 196, 10: 32}[command]
        assert self.block(self.RESPONSE - 16, 16) == bytes([0xa5]) * 16
        assert self.block(self.RESPONSE + count, 524 + 16 - count) == bytes([0xa5]) * (524 + 16 - count)
        assert self.block(self.IO, 12) == io_before and self.block(self.REQUEST, len(request)) == request
        return self.block(self.RESPONSE, count), {"command": command,
            "initialization": initialization, "dispatch": dispatch,
            "response_bytes": count, "response_sha256": hashlib.sha256(self.block(self.RESPONSE, count)).hexdigest()}


def compiled_worker(path, host_replies):
    cpu = Target(path)
    replies, calls = [], []
    for command in (1, 2, 10):
        reply, call = cpu.reply(command)
        if command in (1, 2):
            assert reply == host_replies[command - 1], "Actual worker identity differs from source protocol"
            replies.append(reply)
        else:
            assert reply == bytes.fromhex("0800020700000002ff000000ff00fe000100fd000d000000c8001f0000000000")
        calls.append(call)
    def pins(records):
        return {name: {"address": hex(address), "bytes": len(body),
            "sha256": hashlib.sha256(body).hexdigest()} for name, (address, body) in sorted(records.items())}
    return replies, {"worker_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "functions": pins(cpu.functions), "objects": pins(cpu.objects), "calls": calls,
        "folded_functions": [], "private_write_bounds": True,
        "actual_packets_feed_original_game": True}


def compile_protocol(directory):
    wrapper, library = directory / "protocol.c", directory / "protocol.so"
    wrapper.write_text(WRAPPER)
    command = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(command + ["-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "include"), str(wrapper), str(ROOT / "src/core/vmu_emulation.c"),
        "-o", str(library)], check=True, capture_output=True)
    lib = ctypes.CDLL(str(library))
    lib.make_reply.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint]
    lib.make_reply.restype = ctypes.c_size_t
    return lib


def actual_reply(lib, command):
    out = (ctypes.c_uint8 * 196)()
    count = lib.make_reply(out, command)
    assert count == (116 if command == 1 else 196)
    reply = bytes(out[:count])
    assert reply[:4] == bytes([5 if command == 1 else 6, 0, 2, 28 if command == 1 else 48])
    assert reply[4:8] == bytes([0, 0, 0, 2]), "Storage-only function must remain unchanged"
    assert reply[8:12] == bytes.fromhex("000f4100")
    assert reply[22:52] == b"Visual Memory                 "
    producer = b"Independent K-UI memory card emulation"
    assert reply[52:52 + len(producer)] == producer
    return reply


def enumeration(image, reply):
    cpu = SH(image)
    manager, rx, cardrx, sp = 0x8c400000, 0x8c500000, 0x8c500400, 0x8cfff000
    cpu.write(0x8c2f09fc, manager)
    cpu.write(manager + 0x10c4, 0)  # Native transport's admitted CPU/DMA byte order.
    cpu.write(manager + 0x10b4, 0)
    cpu.write(manager + 0xfe8 + 96, rx)
    cpu.write(manager + 4, 0x20, 1)  # Prior controller only; physical A1 absent.
    cpu.write(rx, 0x03220008)  # GETCOND success after native byte2|=2 publication.
    cpu.run(0x8c0e5216, [0], sp)
    assert cpu.block(manager + 4, 4) == bytes.fromhex("22200200")
    info, state = manager + 0x44c + 120 * 2, manager + 40 + 44 * 2
    cpu.write(cardrx, 0xffffffff)
    cpu.write(sp, cardrx)
    cpu.run(0x8c0e5524, [2, manager + 4, info, state], sp)
    assert cpu.read(state, 1) == 0 and cpu.read(state + 4, 1) == 1
    cpu.run(0x8c0e5216, [0], sp)
    assert cpu.block(manager + 4, 4) == bytes.fromhex("22220000")
    cpu.write(state + 7, 1, 1)  # Original SDK previously issued INFO.
    cpu.store_block(cardrx, reply)
    cpu.write(sp, cardrx)
    cpu.run(0x8c0e5524, [2, manager + 4, info, state], sp)
    assert cpu.read(state, 1) == 1 and cpu.read(info) == 2
    assert cpu.block(info + 4, 4) == bytes.fromhex("000f4100")
    decoded = 0x8c501000
    result, _, _ = cpu.run(0x8c0d8f54, [decoded, info], sp)
    geometry = [cpu.read(decoded + 2 * i, 2) for i in range(5)]
    assert result == 0 and geometry == [1, 512, 4, 1, 0]
    return {"physical_A1_present": False, "A2_INFO_scheduled": True,
        "A2_INFO_accepted": True, "decoded_storage_geometry": geometry}


def product_class(image, original_reply, variant):
    reply = bytearray(original_reply)
    if variant != "current":
        name = "K-UI Virtual Memory" if variant == "old_name" else "Other storage card"
        reply[22:52] = name.encode("ascii").ljust(30, b" ")
    assert reply[:22] == original_reply[:22] and reply[52:] == original_reply[52:]
    cpu = SH(image)
    manager, reply_at, sp = 0x8c400000, 0x8c500000, 0x8cfff000
    info = manager + 0x44c + 120 * 2
    cpu.store_block(reply_at, reply)
    cpu.write(0x8c2f09fc, manager)
    cpu.write(manager + 0x10c4, 0)
    cpu.run(0x8c0e5656, [info, 0, reply_at], sp)
    assert cpu.read(info) == 2
    # Original initializer8c05b580 maps abstractslot5(A2) to SDKendpoint2.
    cpu.write(0x8c2d1dbc + 4 * 5, 2)
    device = 0x8c2eb020 + 52 * 2
    cpu.run(0x8c0cf2d6, [device], sp)  # Actual non-controller inputobject reset.
    cpu.write(device + 36, info + 20)  # Original adapter8c0cf636 publishes this name.
    cpu.write(0x8c2eb500 + 4 * 2, 0)
    klass, _, instructions = cpu.run(0x8c088280, [5], sp)
    abstract = 0x8c502000
    cpu.write(0x8c2e2544 + 4 * 5, abstract)
    cpu.write(abstract + 10, klass, 2)  # Exactly original classifier publication8c088994.
    cpu.write(0x8c0c879c, 1, 2)
    cpu.write(0x8c2d1e44, 1, 1)
    cpu.write(0x8c2d1e4b + 1, 1, 1)  # Only A2's game-device row is active.
    cpu.run(0x8c05c4ba, [], sp)
    kind = cpu.read(0x8c2d1e4b + 1, 1)
    cpu.write(0x8c2d20cc, 0xffffffff)
    selected, _, _ = cpu.run(0x8c05cddc, [1], sp)
    expected = (64, 6, 1, 1) if variant == "current" else (32, 4, 0, 0xffffffff)
    got = klass, kind, selected, cpu.read(0x8c2d20cc)
    assert got == expected, (variant, got, expected)
    return {"variant": variant, "name": reply[22:52].decode("ascii"),
        "function": cpu.read(info), "classifier": klass,
        "classification_instructions": instructions, "game_device_kind": kind,
        "A2_selection_result": selected, "selected_card": cpu.read(0x8c2d20cc)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--startup", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--worker", type=Path, help="Execute this actual linked worker's endpoint and use its replies")
    args = parser.parse_args()
    image = args.startup.read_bytes()
    assert len(image) == 935648 and hashlib.sha256(image).hexdigest() == STARTUP_SHA256
    with tempfile.TemporaryDirectory(prefix="kui-rayman-identity-") as temporary:
        lib = compile_protocol(Path(temporary))
        replies = [actual_reply(lib, cmd) for cmd in (1, 2)]
        assert replies[0][4:] == replies[1][4:116]
        worker_report = None
        if args.worker:
            replies, worker_report = compiled_worker(args.worker, replies)
        attachment = enumeration(image, replies[0])
        cases = []
        for command, reply in zip((1, 2), replies):
            for variant in ("current", "old_name", "other_name"):
                case = product_class(image, reply, variant)
                case["command"] = command
                cases.append(case)
    report = {"schema": "kui.rayman.original-vmu-identity-replay.v1", "pass": True,
        "startup_sha256": STARTUP_SHA256, "native_protocol_sha256": hashlib.sha256(
            (ROOT / "src/core/vmu_emulation.c").read_bytes()).hexdigest(),
        "original_functions": ["0x8c0e5216", "0x8c0e5524", "0x8c0e5656", "0x8c0d8f54",
            "0x8c0cf2d6", "0x8c088280", "0x8c09d214", "0x8c09c524", "0x8c088fa0",
            "0x8c05c4ba", "0x8c05cddc"],
        "attachment": attachment, "cases": cases,
        "compiled_worker": worker_report,
        "source_hashes": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in SOURCE_FILES},
        "scope": "Actual original parsers/classifier/game selection bytes with initialized BSS fixtures; no full console enumeration, filesystem mounting or save-persistence claim."}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
