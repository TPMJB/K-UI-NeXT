#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the built return gate and original MDE's pure argument helpers.

The worker retirement API and raw-card hardware are explicit boundaries;
their production bodies have independent worker and raw-reader tests. The
entry, low C, original parser/setters and terminal quiescence execute here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_runtime import RuntimeCPU, FIRMWARE, FIRMWARE_ADDRESSES,session_flow,card_receipt as forward_card_receipt,phase_video_seed

RESET, ENTRY, TARGET = 0x8c0117f0, 0xac008300, 0xac010000
RAW, RETIRE, STOP, CALLER = 0xac008100, 0x8c300000, 0xac007000, 0x8c003000
CONTEXT, IP, CCR = 0x8c19ee60, 0x8c0080f0, 0xff00001c
TAGS = {0xf4000008 + 32*i for i in range(512)}
DMA = {0xffa0000c + 16*i for i in range(4)}
G2_ENABLE = {0xa05f7800 + 32*i + 20 for i in range(4)}
G2_ACTIVE = {0xa05f7800 + 32*i + 24 for i in range(4)}
IRQ_MASKS = {0xa05f6910 + 16*bank + 4*kind for bank in range(3) for kind in range(3)}
PVR_DIAGNOSTIC_READS = {0xa05f8044, 0xa05f805c, 0xa05f80d0, 0xa05f8050, 0xa05f8054}
PVR_BORDER = 0xa05f8040
READ_REGS = TAGS | DMA | G2_ENABLE | G2_ACTIVE | PVR_DIAGNOSTIC_READS | {0xff000010, 0xa05f7418, 0xa05f6c14, 0xa05f6c18, 0xa05f6808, 0xa05f7c18, 0xa05f6820, 0xa0702c00}
WRITE_REGS = TAGS | DMA | G2_ENABLE | IRQ_MASKS | {PVR_BORDER, 0xff000010, 0xa05f6c14, 0xa05f7c14, 0xa0702c00}


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def blank_return():
    return struct.pack("<4I", 0x5349554b, 0x3154524d, 1, 368) + bytes(352)


def card_receipt():
    return forward_card_receipt(owner=2)


def phase_video_flow(symbols):
    return symbols.get('__kui_smash_return_phase_video_transport') == (1,0,0,0xfff1)


def capsule(session=False,video=False):
    data = bytearray((i * 19 + 0x31) & 255 for i in range(2048))
    data[128:496] = blank_return()
    if video:data[496:576]=struct.pack('<4I',0x5349554b,0x31444956,1,80)+bytes(64)
    if session:data[0x2a0:0x300]=struct.pack('<4I',0x5349554b,0x32305243,2,96)+bytes(80)
    return bytes(data)


class ReturnCPU(RuntimeCPU):
    def __init__(self, data, kind):
        self.hardware = {}
        self.hardware_writes = []
        self.diagnostic_writes = []
        super().__init__(data)
        self.kind, self.retire_calls, self.original_helpers = kind, 0, []
        self.irq_progress = False
        self.wait_visits = 0
        self.retired_success = False
        self.private_min_sp = 0xac00c000
        self.return_progress = []

    def read(self, address, count=4):
        physical = address & 0x1fffffff
        if address == CCR:
            return super().read(address, count)
        if not 0x0c000000 <= physical < 0x0d000000:
            assert count == 4 and address in READ_REGS, f'unadmitted MMIO read {address:08x}'
            self.mmio_reads.append(address)
            return self.hardware.get(address, 0)
        return super().read(address, count)

    def write(self, address, value, count=4):
        physical = address & 0x1fffffff
        if address == CCR:
            return super().write(address, value, count)
        if not 0x0c000000 <= physical < 0x0d000000:
            assert count == 4 and address in WRITE_REGS, f'unadmitted MMIO write {address:08x}'
            self.hardware[address] = value
            (self.diagnostic_writes if address == PVR_BORDER else self.hardware_writes).append((address, value))
            return
        super().write(address, value, count)

    def raw_read(self):
        lba, destination, blocks = self.r[4:7]
        assert 1 <= blocks <= 4096
        self.raw_calls.append((lba, destination, blocks))
        for i in range(blocks):
            logical = {100: 0, 101: 1, 200: 2, 201: 3}[lba + i]
            block = self.capsule[logical * 512:(logical + 1) * 512]
            for offset in range(0, 512, 4):
                self.write(destination + i * 512 + offset, int.from_bytes(block[offset:offset + 4], "little"))
        self.r[0], self.pc = 0, self.pr

    def plain(self, op, pc):
        if op & 0xff00 == 0xc700:  # MOVA @(disp,PC),R0, used by the P2 cache/entry bridges.
            self.r[0] = ((pc + 4) & ~3) + 4 * (op & 255)
            self.instructions += 1
        else:
            super().plain(op, pc)

    def step(self):
        sp = self.r[15] & 0x1fffffff
        if 0x0c00b800 <= sp < 0x0c00c000:
            self.private_min_sp = min(self.private_min_sp, sp | 0xa0000000)
        super().step()


def prepare(image, segments, symbols, mde, kind):
    session=session_flow(symbols)
    video=phase_video_flow(symbols)
    expected = capsule(session,video)
    if kind == 'bad_video_slot':
        assert video
        changed=bytearray(expected);changed[512]=1;expected=bytes(changed)
    actual = bytearray(expected)
    if kind == "bad_capsule_crc":
        actual[1700] ^= 1
    cpu = ReturnCPU(bytes(actual), kind)
    cpu.store_block(0x8c010000, mde)
    for segment in segments:
        if segment[0] == 1:
            cpu.store_block(segment[2], image[segment[1]:segment[1] + segment[4]])
            if segment[5] > segment[4]:
                cpu.store_block(segment[2] + segment[4], bytes(segment[5] - segment[4]))
    begin = symbols["__smash_menu_return_begin"][0]
    end = symbols["__smash_menu_return_binary_end"][0]
    runtime = symbols["_kui_smash_menu_return_runtime"][0]
    runtime_bytes=420+96*session+80*video
    assert symbols["_kui_smash_menu_return_runtime"][1] == runtime_bytes
    initialized = bytearray(cpu.block(begin, end - begin))
    initialized[runtime - begin:runtime - begin + runtime_bytes] = bytes(runtime_bytes)
    raw = bytes((i * 23 + 0x77) & 255 for i in range(512))
    cpu.store_block(RAW, raw)
    plan = [0x5349554b, 0x31444c4d, 1, 64, 2048, 2048, crc(expected), 128,
            100, 2, 200, 4, 0, 0, 0, 0]
    plan[-1] = crc(struct.pack("<16I", *plan))
    returning = bytearray(struct.pack("<8I", 0x5349554b, 0x3154524d, 1, 368, 1, 0, 0, 0)
                          + struct.pack("<16I", *plan) + bytes(272))
    struct.pack_into("<I", returning, 28, crc(returning))
    if kind == "bad_return_seed":
        returning[32] ^= 1
    tail = list(FIRMWARE) + [STOP, RETIRE, 0, crc(raw)]
    wire = bytearray(returning + struct.pack("<13I", *tail)+(card_receipt() if session else b'')+
        (phase_video_seed() if video else b''))
    struct.pack_into("<I", wire, 412, zlib.crc32(wire, crc(initialized)) & 0xffffffff)
    if kind == "changed_retire_pointer":
        struct.pack_into("<I", wire, 408, RETIRE + 4)
    if kind == "changed_stop_pointer":
        struct.pack_into("<I", wire, 404, STOP + 4)
    if kind == 'changed_card_receipt':
        assert session
        wire[420+45]^=1
    if kind in ('changed_phase_video','malformed_phase_video','old_video_runtime_abi'):
        assert video
        offset=420+96*session
        if kind=='old_video_runtime_abi':wire[offset:offset+80]=bytes(80)
        else:
            # A still-typed, individually checksummed timing edit must fail
            # the enclosing runtime CRC, independently of VID1's own CRC.
            wire[offset+(36 if kind=='changed_phase_video' else 24)]^=1
            wire[offset+72:offset+76]=bytes(4)
            wire[offset+72:offset+76]=struct.pack('<I',crc(wire[offset:offset+80]))
        if kind!='changed_phase_video':
            # A valid outer CRC cannot authorize a blank/invalid VID1 shape.
            wire[412:416]=bytes(4)
            wire[412:416]=struct.pack('<I',zlib.crc32(wire,crc(initialized)))
    cpu.store_block(runtime, wire)
    cpu.store_block(0xac00b800, bytes([0x5b])*32)
    # The old RPG's five tokens must never be mistaken for return-menu args.
    folder, rom, sound = ("PHSTAR2", "PHSTAR2.SGA", 0) if kind == "psii_return" else ("SHFORCE", "SHFORCE.SGA", 1)
    old_argv = (b"\0".join(x.encode() for x in ["\\MDE_US.BIN", rom, "MODE0", "SKIP0", "SOUND" + str(sound)]) + b"\0\0").ljust(256, b"\0")
    cpu.store_block(CONTEXT + 256, old_argv)
    command = b"\\1ST_READ.BIN" if kind == "psii_return" else b"\\manifest.bin"
    cpu.store_block(CONTEXT + 64, (command + b"\0").ljust(64, b"\0"))
    cpu.store_block(IP, folder.encode().ljust(12, b" ") + b"\x01\x04\xa7\xb8")
    cpu.store_block(RESET, struct.pack("<4HI", 0xd001, 0x402b, 9, 9, ENTRY))
    for address in FIRMWARE_ADDRESSES:
        cpu.store_block(address, struct.pack("<I", 0xdeadbeef))
    cpu.r = [0x12340000 + 0x1111 * i for i in range(15)] + [0x8c00f400]
    cpu.sr, cpu.pr, cpu.mach, cpu.macl = 0x40000001, CALLER, 0x1234, 0xabcd
    if kind == "busy_bl_masked":
        cpu.sr |= 0x10000000
    if kind == "busy_imask_masked":
        cpu.sr |= 0xf0
    if kind == "busy_timeout":
        cpu.store_block(symbols['_smash_return_waits'][0], struct.pack('<I', 4095))
    # GBR is live owner state, not the original cold-GIN constant.
    cpu.gbr, cpu.vbr, cpu.pc = 0x8c2f9b80, 0x8c004000, RESET
    before = (list(cpu.r), cpu.sr, cpu.pr, cpu.mach, cpu.macl, cpu.gbr, cpu.vbr)
    owner = cpu.block(0x8c010000, len(mde))
    old_ip = cpu.block(IP, 16)
    return cpu, expected, command, before, owner, old_ip


def exercise(image, segments, symbols, mde, kind,return_cpu=False):
    cpu, expected, command, before, owner, old_ip = prepare(image, segments, symbols, mde, kind)
    reason = 0
    diagnostic = symbols['_kui_smash_menu_return_diagnostic'][0]
    diagnostic_seen = None
    forbidden = {0x8c1a99ac, 0x8c1a650a, 0x8c1a5b04, 0x8c010154, 0x8c159ab8}
    helpers = {0x8c1598c0, 0x8c1ad2c8, 0x8c1ad31a}
    for _ in range(4000000):
        assert cpu.pc not in forbidden, "Return reached original cleanup/GIN code"
        assert cpu.pc not in (CALLER, STOP+4), 'Busy or bad authority resumed an untrusted caller'
        if cpu.pc == symbols['__smash_menu_return_wait_owner'][0]:
            cpu.wait_visits += 1
            assert (cpu.sr, cpu.gbr, cpu.vbr, cpu.r[15]) == (before[1], before[5], before[6], before[0][15]-84)
            assert cpu.block(0x8c010000, len(mde)) == owner and cpu.block(IP, 16) == old_ip
            assert not cpu.original_helpers and not cpu.raw_calls and not cpu.hardware_writes and not cpu.cache_writes
            if kind == 'busy_irq_progress':
                cpu.irq_progress = True
        if cpu.pc in helpers:
            assert cpu.retired_success
            cpu.original_helpers.append(cpu.pc)
        if cpu.pc == symbols['_kui_smash_return_progress'][0]:
            cpu.return_progress.append((cpu.r[4], cpu.r[5]))
        if cpu.pc == RETIRE:
            cpu.retire_calls += 1
            assert cpu.sr & 0x100000f0 == 0x100000f0
            assert cpu.block(0x8c010000, len(mde)) == owner and cpu.block(IP, 16) == old_ip
            busy = kind.startswith('busy_') and not (kind == 'busy_irq_progress' and cpu.irq_progress)
            cpu.r[0] = 0 if busy else (0xffffffff if kind == "bad_worker" else 1)
            if cpu.r[0] == 1:
                assert not cpu.retired_success
                cpu.retired_success = True
            cpu.pc = cpu.pr
        if cpu.pc in (RAW,RAW-0x20000000):
            assert cpu.retired_success and len(cpu.original_helpers) == 3
            cpu.raw_read()
        if cpu.pc == TARGET:
            break
        if cpu.pc == STOP:
            reason = cpu.r[4]
            break
        if cpu.read(diagnostic) == 0x4d525453 and cpu.read(diagnostic+4):
            if diagnostic_seen is None:
                diagnostic_seen = cpu.instructions
            elif cpu.instructions-diagnostic_seen > 64:
                reason = cpu.read(diagnostic+4)
                break
        cpu.step()
    else:
        raise AssertionError("Return gate exceeded its instruction bound")
    result = dict(case=kind, instructions=cpu.instructions, reason=reason,
                  retirement_calls=cpu.retire_calls,
                  diagnostic_writes=[dict(address=hex(a), value=hex(v)) for a,v in cpu.diagnostic_writes],
                  irq_wait_visits=cpu.wait_visits,
                  original_helpers=[hex(x) for x in cpu.original_helpers],
                  raw_calls=[dict(lba=lba, destination=hex(out), blocks=n) for lba, out, n in cpu.raw_calls],
                  return_progress=[dict(phase=hex(p), detail=hex(v)) for p,v in cpu.return_progress])
    assert cpu.block(0xac00b800, 32) == bytes([0x5b])*32
    result.update(private_min_sp=hex(cpu.private_min_sp),
                  observed_private_stack_bytes=0xac00c000-cpu.private_min_sp,
                  private_stack_guard_unchanged=True)
    firmware = [cpu.read(address) for address in FIRMWARE_ADDRESSES]
    if kind in ("sf_return", "psii_return", "sf_return_again", "busy_irq_progress"):
        assert cpu.pc == TARGET and not reason
        assert not cpu.diagnostic_writes, 'Successful return must not draw red diagnostic progress'
        assert not set(cpu.mmio_reads) & PVR_DIAGNOSTIC_READS, 'Successful return must not inspect game scanout'
        assert cpu.block(symbols['_kui_smash_return_progress_state'][0],8) == struct.pack('<2I',0x10a,0)
        assert cpu.return_progress == [(0x101,0)] * (2 if kind == 'busy_irq_progress' else 1) + [
            (0x105,100),(0x106,0),(0x107,0),(0x108,0),(0x109,0),(0x10a,0)]
        assert cpu.retire_calls == (2 if kind == 'busy_irq_progress' else 1)
        assert cpu.wait_visits == (1 if kind == 'busy_irq_progress' else 0)
        assert cpu.original_helpers == [0x8c1598c0, 0x8c1ad2c8, 0x8c1ad31a]
        assert [(a, n) for a, _, n in cpu.raw_calls] == [(100, 1), (101, 1), (200, 2)]
        seed = cpu.block(TARGET + 128, 368)
        words = struct.unpack_from("<8I", seed)
        assert words[:7] == (0x5349554b, 0x3154524d, 1, 368, 3, 0, 0)
        normalized = bytearray(seed);normalized[28:32] = bytes(4)
        assert crc(normalized) == words[7]
        assert seed[96:352] == (command + b"\0\0").ljust(256, b"\0")
        assert seed[352:] == b" " * 12 + b"\x01\x04\xa7\xb8"
        assert cpu.block(TARGET, 128) == expected[:128]
        if session_flow(symbols):
            if phase_video_flow(symbols):
                assert cpu.block(TARGET+496,80)==phase_video_seed()
                assert cpu.block(TARGET+576,0x2a0-576)==expected[576:0x2a0]
                result.update(original_video_carried=True,original_video_bytes=80)
            else:assert cpu.block(TARGET+496,0x2a0-496)==expected[496:0x2a0]
            assert cpu.block(TARGET+0x2a0,96)==card_receipt()
            assert cpu.block(TARGET+0x300,2048-0x300)==expected[0x300:]
            result.update(live_receipt_bytes_carried=96,receipt_offset='0x2a0')
        else:
            assert cpu.block(TARGET+496,2048-496)==expected[496:]
        assert firmware == FIRMWARE
        assert cpu.gbr == 0 and cpu.r[15] == 0x8cffff00 and cpu.vbr == 0x8c000000 and cpu.sr == 0x500000f0
        result.update(fresh_outer_cold_entry=True, exact_current_return_context=True,
                      old_native_argv_not_reused=True, firmware_restored=True,
                      progress_ram_only=True, no_progress_scanout_access=True)
    else:
        expected_reason = {"bad_worker": 6, "bad_capsule_crc": 10, "bad_return_seed": 2,
                           'changed_phase_video':4,'malformed_phase_video':2,
                           'old_video_runtime_abi':2,'bad_video_slot':15,
                           "changed_retire_pointer": 4, "changed_stop_pointer": 4,'changed_card_receipt':4,
                           "busy_timeout": 13, "busy_bl_masked": 12, "busy_imask_masked": 12}[kind]
        assert reason == expected_reason and firmware == [0xdeadbeef] * 9
        expected_progress = ([(0x101,0),(0x105,100),(0x106,0),(0x107,0),(0x108,0)]
            if kind=='bad_video_slot' else [(0x101,0),(0x105,100),(0x106,0),(0x107,0)]
            if kind == 'bad_capsule_crc' else [(0x101,0)] * (2 if kind == 'busy_timeout' else 1)
            if kind in ('bad_worker','busy_timeout','busy_bl_masked','busy_imask_masked') else [])
        assert cpu.return_progress == expected_progress
        assert cpu.pc != STOP, 'Terminal reporter must preserve the current scanout without an inherited renderer'
        assert cpu.diagnostic_writes and all(v == 0xff0000 for _,v in cpu.diagnostic_writes)
        if kind in ("bad_capsule_crc",'bad_video_slot'):
            assert cpu.block(TARGET + 128, 368) == blank_return()
        else:
            assert cpu.block(0x8c010000, len(mde)) == owner and not cpu.raw_calls
            assert cpu.block(IP, 16) == old_ip and not cpu.original_helpers
        if kind.startswith('busy_'):
            assert cpu.retire_calls == (2 if kind == 'busy_timeout' else 1)
            assert cpu.wait_visits == (1 if kind == 'busy_timeout' else 0)
            assert not cpu.hardware_writes and not cpu.cache_writes
            result.update(no_old_cleanup_resumed=True, busy_state_preserved=True)
        if kind in ('bad_return_seed', 'changed_retire_pointer', 'changed_stop_pointer','changed_card_receipt',
                    'changed_phase_video','malformed_phase_video','old_video_runtime_abi'):
            assert cpu.pc != STOP and not cpu.retire_calls
            result.update(no_untrusted_diagnostic_callback=True)
        result.update(refused_without_firmware_restore=True)
    return (result,cpu) if return_cpu else result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--mde", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    mde = args.mde.read_bytes()
    assert hashlib.sha256(mde).hexdigest() == "04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61"
    image, _, segments, _, symbols = read_elf(args.elf)
    assert symbols["_kui_smash_menu_return_entry"][0] == ENTRY
    kinds = ["sf_return", "psii_return", "sf_return_again", "busy_irq_progress", "busy_timeout",
              "busy_bl_masked", "busy_imask_masked", "bad_worker", "bad_capsule_crc",
              "bad_return_seed", "changed_retire_pointer", "changed_stop_pointer"]
    if phase_video_flow(symbols):kinds += ['changed_phase_video','malformed_phase_video','old_video_runtime_abi','bad_video_slot']
    cases = [exercise(image, segments, symbols, mde, kind) for kind in kinds]
    report = dict(schema="kui.smash-menu.return-runtime.v1", passed=True,
                  elf_sha256=hashlib.sha256(image).hexdigest(), original_mde_sha256=hashlib.sha256(mde).hexdigest(),
                  source_reset_entry=hex(RESET), cases=cases,
                  limits=["The production worker retirement gateway and raw SD hardware are substituted by explicit return values and a four-block fixture; separate production worker/raw tests establish those bodies.",
                          "The timeout fixture starts the private retry counter at 4095 and checks its final increment/refusal; it does not execute 4096 complete C passes.",
                          "Observed private-stack usage includes actual return C and original parser/setter helpers; substituted raw-reader and worker-gateway stack closure comes from their separate linked audits.",
                          "DMA and cache MMIO execute against idle register models; cache timing, register banking and actual downstream menu/game/VMU execution remain hardware checks."])
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True);args.report.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
