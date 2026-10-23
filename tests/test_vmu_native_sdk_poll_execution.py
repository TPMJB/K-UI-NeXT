#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay the owner-supplied Evolution SDK's actual Maple polling lifecycle.

No retail instructions or executable bytes are embedded. The existing bounded
SH integer interpreter executes the original initializer, submission routine,
ready transition and reply consumer. Only the ASIC revision register and Maple
DMA hardware are modeled; there are no substituted SDK or game functions.
Optional virtual-A1 publication uses actual host-built C INFO responses.
Unsupported instructions/MMIO fail instead of being approximated. This is an
instruction and packet-shape test, not a cycle, cache or interrupt-timing model.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from test_vmu_native_heap_execution import SH, MASK, signed
from profile_vmu_native_release_sh import ReleaseSH

DIGEST = "ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35"
IMAGE_BYTES = 2143652
ASIC, MDSTAR, MDST = 0xA05F689C, 0xA05F6C04, 0xA05F6C18
MMIO = {ASIC, MDSTAR, MDST, 0xA05F6C10, 0xA05F6C14, 0xA05F6C80, 0xA05F6C8C}
INITIALIZE, SUBMIT, READY, CONSUME = 0x8C1DA8FC, 0x8C1DAD26, 0x8C1DAF38, 0x8C1DAF4E
MANAGER_CELL, MANAGER = 0x8C32D6E4, 0x8C32C5E8
TX, RX, STACK = 0x0C248420, 0x0C23C400, 0x8C00E000


def hx(value):
    return f"0x{value:08x}"


def actual_vmu_info():
    """Obtain exact INFO bytes from our actual C emulation handler, without I/O.

    The small host-only adapter rejects any storage callback. The original SH
    replay still models transport/publication and does not execute VMU hooks.
    """
    wrapper = r'''
#include "kui/vmu_emulation.h"
static unsigned io_calls;
static int read_block(void *c,uint16_t b,uint8_t d[512]) {
    (void)c;(void)b;(void)d;++io_calls;return KUI_VMU_EMU_IO_ERROR;
}
static int commit_block(void *c,uint16_t b,const uint8_t d[512]) {
    (void)c;(void)b;(void)d;++io_calls;return KUI_VMU_EMU_IO_ERROR;
}
int model_info(unsigned char command,unsigned char *out,size_t *length) {
    kui_vmu_emulation_t state;
    struct kui_vmu_emulation_io io={0,read_block,commit_block};
    uint8_t request[4]={command,1,0,0};
    io_calls=0;
    if(kui_vmu_emulation_init(&state,1,&io)) return -98;
    int result=kui_vmu_emulation_handle(&state,request,4,out,196,length);
    return io_calls?-99:result;
}
'''
    replies = {}
    with tempfile.TemporaryDirectory(prefix="kui-sdk-info-") as scratch:
        library = Path(scratch) / "info.so"
        subprocess.run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I", str(ROOT / "include"),
            "-x", "c", "-", str(ROOT / "src/core/vmu_emulation.c"), "-o", str(library)],
            input=wrapper, text=True, check=True, capture_output=True)
        adapter = ctypes.CDLL(str(library)).model_info
        adapter.argtypes = [ctypes.c_ubyte, ctypes.POINTER(ctypes.c_ubyte), ctypes.POINTER(ctypes.c_size_t)]
        adapter.restype = ctypes.c_int
        for command in (1, 2):
            output, length = (ctypes.c_ubyte * 196)(), ctypes.c_size_t()
            assert adapter(command, output, ctypes.byref(length)) == 1
            packet = bytes(output[:length.value])
            assert len(packet) == (116 if command == 1 else 196)
            assert packet[:4] == bytes([5 if command == 1 else 6, 0, 1, 28 if command == 1 else 48])
            assert int.from_bytes(packet[4:8], "little") == 0x02000000
            assert int.from_bytes(packet[8:12], "little") == 0x00410F00
            replies[command] = packet
    return replies


class PollSH(ReleaseSH):
    """Original RAM execution plus explicitly bounded hardware side effects."""

    def __init__(self, image, revision, controllers, virtual_info=None):
        # Reuse ReleaseSH's integer decoder, not its ELF/worker constructor.
        SH.__init__(self, image)
        self.mmio = {ASIC: revision}
        self.mmio_reads, self.mmio_writes, self.batches = [], [], []
        self.controllers = controllers
        self.virtual_info = virtual_info

    def read(self, address, count=4):
        if address in MMIO:
            assert count == 4
            self.mmio_reads.append(address)
            return self.mmio.get(address, 0)
        return SH.read(self, address, count)

    def write(self, address, value, count=4):
        if address in MMIO:
            assert count == 4 and address != ASIC
            self.mmio[address] = value & MASK
            self.mmio_writes.append((self.executing, address, value & MASK))
            if address == MDST and value == 1:
                self.dma()
            return
        SH.write(self, address, value, count)

    def dma(self):
        """Consume only the actual submitted chain and return physical replies.

        Completion is immediate in this sequencing model. Execution does not
        claim how long hardware DMA, software hooks or interrupts would take.
        """
        source = self.mmio[MDSTAR]
        assert source in (TX, TX + 0x3000)
        bank, address, frames, nops = int(source != TX), source, [], 0
        for _ in range(256):
            control = self.read(address)
            address += 4
            if control == 0x700:
                nops += 1
                continue
            receiver, header = self.read(address), self.read(address + 4)
            words = control & 255
            payload = [self.read(address + 8 + i * 4) for i in range(words)]
            address += 8 + 4 * words
            assert address <= source + 0x3000
            destination, command = header >> 8 & 255, header & 255
            port = destination >> 6
            unit = destination & 63
            assert unit == 32 or (self.virtual_info and destination == 1)
            slot = port * 6 + (1 if unit == 1 else 0)
            assert receiver == RX + bank * 0x6000 + slot * 1024
            assert command in (1, 2, 9)
            assert words == (1 if command == 9 else 0)
            assert payload == ([0x01000000] if command == 9 else [])
            response = MASK
            if unit == 1:
                assert command in self.virtual_info
                packet = self.virtual_info[command]
                self.store_block(receiver, packet)
                response = int.from_bytes(packet[:4], "little")
            elif self.controllers & (1 << port):
                response = ((28 if command == 1 else 3) << 24) | \
                    (destination << 16) | ((port * 64) << 8) | (5 if command == 1 else 8)
                self.store_block(receiver, bytes(116 if command == 1 else 16))
                self.write(receiver + 4, 0x01000000)
                if command == 9:
                    self.write(receiver + 8, MASK)
                    self.write(receiver + 12, 0x80808080)
                if self.virtual_info and port == 0:
                    response |= 0x00010000
            self.write(receiver, response)
            frames.append(dict(port=port, unit=unit, command=command, control=hx(control),
                rx=hx(receiver), header=hx(header), payload=[hx(v) for v in payload],
                model_response=hx(response)))
            if control & 0x80000000:
                break
        else:
            raise AssertionError("Original DMA chain exceeded256 entries")
        self.batches.append(dict(source=hx(source), bank=bank, nops=nops,
                                 source_bytes=address - source, frames=frames))
        self.mmio[MDST] = 0

    def plain(self, op, pc):
        """Exact additional integer instructions present on the replayed path."""
        n, m = op >> 8 & 15, op >> 4 & 15
        self.executing = pc
        if op & 0xF00F == 0x0006:  # mov.l Rm,@(R0,Rn)
            self.write((self.r[0] + self.r[n]) & MASK, self.r[m])
        elif op & 0xF00F == 0x0004:  # mov.b Rm,@(R0,Rn)
            self.write((self.r[0] + self.r[n]) & MASK, self.r[m], 1)
        elif op & 0xF00F == 0x000C:  # mov.b @(R0,Rm),Rn
            self.r[n] = signed(self.read((self.r[0] + self.r[m]) & MASK, 1), 8) & MASK
        elif op & 0xF00F == 0x3003:  # cmp/ge Rm,Rn
            self.flag(signed(self.r[n], 32) >= signed(self.r[m], 32))
        elif op & 0xF00F == 0x3007:  # cmp/gt Rm,Rn
            self.flag(signed(self.r[n], 32) > signed(self.r[m], 32))
        elif op & 0xF00F == 0x600E:  # exts.b Rm,Rn
            self.r[n] = signed(self.r[m] & 255, 8) & MASK
        elif op & 0xF00F == 0x600F:  # exts.w Rm,Rn
            self.r[n] = signed(self.r[m] & 65535, 16) & MASK
        elif op & 0xF00F == 0x6008:  # swap.b Rm,Rn
            self.r[n] = (self.r[m] & 0xFFFF0000) | ((self.r[m] & 255) << 8) | ((self.r[m] >> 8) & 255)
        elif op & 0xF00F == 0x6009:  # swap.w Rm,Rn
            self.r[n] = ((self.r[m] >> 16) | (self.r[m] << 16)) & MASK
        elif op & 0xF0FF == 0x4011:  # cmp/pz Rn
            self.flag(signed(self.r[n], 32) >= 0)
        elif op & 0xF0FF == 0x4015:  # cmp/pl Rn
            self.flag(signed(self.r[n], 32) > 0)
        elif op & 0xF0FF == 0x4018:  # shll8 Rn
            self.r[n] = self.r[n] << 8 & MASK
        elif op & 0xF0FF == 0x4019:  # shlr8 Rn
            self.r[n] >>= 8
        elif op & 0xF0FF == 0x401B:  # tas.b @Rn
            value = self.read(self.r[n], 1)
            self.flag(value == 0)
            self.write(self.r[n], value | 128, 1)
        elif op & 0xFF00 == 0x8400:  # mov.b @(disp,Rm),R0
            self.r[0] = signed(self.read((self.r[m] + (op & 15)) & MASK, 1), 8) & MASK
        elif op & 0xF0FF == 0x002A:  # sts PR,Rn
            self.r[n] = self.pr
        else:
            return ReleaseSH.plain(self, op, pc)
        self.instructions += 1

    def original_call(self, address, args=()):
        result, _, steps = SH.run(self, address, args, STACK)
        assert result == 0, (hx(address), hx(result))
        return steps


def replay(image, revision, controllers, rounds, virtual_info=None):
    cpu = PollSH(image, revision, controllers, virtual_info)
    # Actual root-call literal arguments. The original initializer aligns and
    # maps these raw addresses itself; the model does not seed an SDK manager.
    initializer = cpu.original_call(INITIALIZE, [0x8C248408, 0xC000, 0x8C23C3E8, 0xC000])
    assert cpu.read(MANAGER_CELL) == MANAGER
    assert cpu.read(MANAGER + 0xFC4) == TX | 0xA0000000
    assert cpu.read(MANAGER + 0xFC8) == RX | 0xA0000000
    ports = 3 if revision <= 1 else 4
    modes = cpu.block(MANAGER + 0xF90, 24)
    assert modes == bytes([0x52, *([0x45] * 5)] * ports + [0] * ((4 - ports) * 6))
    for bank in range(2):
        for index in range(24):
            assert cpu.read(MANAGER + 0xFE8 + (bank * 24 + index) * 4) == \
                0xA0000000 | (RX + bank * 0x6000 + index * 1024)
    history = []
    for ordinal in range(rounds):
        before = len(cpu.batches)
        submit = cpu.original_call(SUBMIT)
        assert len(cpu.batches) == before + 1
        batch = cpu.batches[-1]
        assert batch["bank"] == ordinal % 2
        main_frames = [frame for frame in batch["frames"] if frame["unit"] == 32]
        assert len(main_frames) == ports
        expected = [9 if ordinal and controllers & (1 << port) else 1 for port in range(ports)]
        assert [frame["port"] for frame in main_frames] == list(range(ports))
        assert [frame["command"] for frame in main_frames] == expected
        virtual_frames = [frame for frame in batch["frames"] if frame["unit"] == 1]
        assert len(batch["frames"]) == ports + len(virtual_frames)
        assert len(virtual_frames) <= 1
        assert not virtual_frames or (virtual_info and ordinal)
        ready, consumer = cpu.original_call(READY), cpu.original_call(CONSUME)
        history.append(dict(round=ordinal, submit_instructions=submit, ready_instructions=ready,
            consumer_instructions=consumer,
            all_getcond_fast_eligible=all(cmd == 9 for cmd in expected) and not virtual_frames,
            mixed_main_fast_eligible=bool(ordinal and controllers & 1) and not virtual_frames,
            **batch))
    return dict(asic_revision=revision, physical_controller_mask=hx(controllers), virtual_a1=bool(virtual_info),
        enabled_ports=ports, initializer_instructions=initializer,
        initial_device_modes=modes.hex(), rounds=history,
        mmio_read_addresses=[hx(a) for a in sorted(set(cpu.mmio_reads))],
        mmio_writes=[dict(pc=hx(pc), address=hx(a), value=hx(v)) for pc, a, v in cpu.mmio_writes])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--startup", type=Path, default=os.environ.get("KUI_NATIVE_EVOLUTION1_STARTUP"))
    parser.add_argument("--rounds", type=int, default=12)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--virtual-a1", action="store_true",
                        help="Also replay physical A with virtual A1 INFO generated by the production C handler")
    args = parser.parse_args()
    if not args.startup:
        parser.error("Supply --startup or KUI_NATIVE_EVOLUTION1_STARTUP")
    if not 6 <= args.rounds <= 120:
        parser.error("--rounds must be between6 and120")
    if not __debug__:
        parser.error("Run with Python assertions enabled")
    image = args.startup.read_bytes()
    assert len(image) == IMAGE_BYTES and hashlib.sha256(image).hexdigest() == DIGEST
    cases = [replay(image, revision, controllers, args.rounds)
             for revision in (0, 2) for controllers in (0, 1, 3, 15)]
    virtual_info = actual_vmu_info() if args.virtual_a1 else None
    if virtual_info:
        cases.extend(replay(image, revision, 1, args.rounds, virtual_info) for revision in (0, 2))
    report = dict(schema="kui.native.original-sdk-poll-execution.v1", date="2026-10-23",
        executable_sha256=DIGEST, executable_bytes=IMAGE_BYTES, retail_code_embedded=False,
        command="python3 tests/test_vmu_native_sdk_poll_execution.py --startup <owner-supplied Evolution1 startup>" +
            (" --virtual-a1" if virtual_info else "") + f" --rounds {args.rounds}" +
            " --output docs/evidence/native-evolution-sdk-poll-2026-10-23.json",
        original_entrypoints={"initializer": hx(INITIALIZE), "submit": hx(SUBMIT),
                              "ready": hx(READY), "consumer": hx(CONSUME)},
        hardware_model=["ASIC revision register0xa05f689c set independently to0 and2",
            "Original MDSTAR/MDST register writes trigger bounded DMA traversal through LAST",
            "Present main controllers answer DEVINFO and GETCOND with exact typed headers/function",
            "Absent main controllers answer FFFFFFFF; no physical VMUs or other accessories",
            "Optional virtual A1: parent sender byte OR1 and A1 INFO/ALLINFO replies published in the model",
            "Virtual INFO bytes are generated by actual src/core/vmu_emulation.c via host C adapter; storage callbacks are trapped",
            "DMA completion is immediate; ready/consumer functions are called explicitly afterward"],
        function_stubs=False, seeded_sdk_manager=False,
        findings={"physical_A_only_all_getcond_fast_hits": 0,
            "physical_A_only_steady_pairs": 2 * (args.rounds - 1),
            "physical_A_only_steady_commands": {"asic0": [9, 1, 1], "asic2": [9, 1, 1, 1]},
            "source_banks_alternate": [hx(TX), hx(TX + 0x3000)],
            "virtual_a1_cases": [dict(asic_revision=case["asic_revision"],
                info_rounds=[r["round"] for r in case["rounds"]
                             if any(f["unit"] == 1 for f in r["frames"])],
                mixed_main_fast_hits=sum(r["mixed_main_fast_eligible"] for r in case["rounds"]),
                total_rounds=len(case["rounds"])) for case in cases if case["virtual_a1"]]},
        virtual_info_source=None if not virtual_info else dict(
            source="src/core/vmu_emulation.c", host_compiler="cc -std=c11 -O2 -shared -fPIC",
            source_sha256=hashlib.sha256((ROOT / "src/core/vmu_emulation.c").read_bytes()).hexdigest(),
            replies={str(cmd): dict(bytes=len(packet), sha256=hashlib.sha256(packet).hexdigest(),
                header=packet[:4].hex(), function=hx(int.from_bytes(packet[4:8], "little")),
                function_data=hx(int.from_bytes(packet[8:12], "little")))
                for cmd, packet in virtual_info.items()}, storage_callbacks=0),
        limits=["Integer instruction counts are not cycle counts or measured console time",
            "Flat coherent RAM aliases do not model SH operand/instruction caches",
            "No interrupt scheduling, PVR scanline progress, video, emulated VMU hooks or SD latency modeled",
            "Virtual A1 publication is modeled as completed RX bytes; worker filtering/ownership guards are not executed by this test",
            "No game-level VMU requests are injected; this covers automatic SDK discovery/polling only",
            "Observed SDK packet shapes do not prove the cause of the hardware Evolution black screen",
            "ASIC revision0/2 are test inputs; the user's actual revision has not been read"], cases=cases)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Original Evolution SDK polling: {len(cases)} cases, {len(cases) * args.rounds} actual DMA batches; "
          f"physical-A-only strict all-GETCOND hits0/{2 * (args.rounds - 1)} steady pairs")
    for case in report["findings"]["virtual_a1_cases"]:
        print(f"Virtual A1 ASIC{case['asic_revision']}: INFO rounds{case['info_rounds']}; "
              f"mixed-main eligibility{case['mixed_main_fast_hits']}/{case['total_rounds']} total rounds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
