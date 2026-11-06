#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute diagnostic-only forward marks in the linked low menu loader.

The production clock reader has its own MMIO proof. Here only that reader is
replaced by a monotonic stamp boundary so mark placement and late publication
can be checked independently of timer behavior or host execution speed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
import test_smash_menu_loader_runtime as runtime
from check_vmu_native_worker_gateway import read_elf

TRACE_OFFSET, TRACE_BYTES = 736, 992
VIDEO_OFFSET, VIDEO_BYTES = TRACE_OFFSET+TRACE_BYTES, 80
TRACE_HEADER = struct.pack("<8I", 0x5349554b, 0x314d4954, 1, TRACE_BYTES, 0, 0, 0, 0)


def capsule(phase=False):
    del phase
    data = bytearray(ORIGINAL_CAPSULE())
    data[TRACE_OFFSET:TRACE_OFFSET + TRACE_BYTES] = TRACE_HEADER + bytes(TRACE_BYTES - 32)
    data[VIDEO_OFFSET:VIDEO_OFFSET+VIDEO_BYTES] = struct.pack('<4I',0x5349554b,0x31444956,1,80)+bytes(64)
    return bytes(data)


ORIGINAL_CAPSULE = runtime.make_capsule


def clock_boundary(cpu, clock):
    if cpu.pc != clock:
        return False
    cpu.phase_clock_reads += 1
    cpu.phase_events.append(('clock',cpu.phase_clock_reads))
    stamp = struct.pack("<8I", 7, 0xc023, 0xe0a, 0,
                        cpu.phase_clock_reads * 1000, 100 + cpu.phase_clock_reads, 0, 0)
    cpu.store_block(cpu.r[4], stamp)
    cpu.pc = cpu.pr
    return True


def callback(cpu, symbols, address, count):
    owner = (list(cpu.r), cpu.sr, cpu.pr, cpu.mach, cpu.macl, cpu.gbr, cpu.vbr, cpu.pc)
    sentinel = 0xac007100
    cpu.pc = symbols["_kui_smash_menu_loader_redirect"][0]
    cpu.pr, cpu.r[15] = sentinel, 0xac00c000
    cpu.r[4], cpu.r[5] = address, count
    clock = symbols["_kui_smash_phase_clock_read"][0]
    for _ in range(100000):
        if cpu.pc == sentinel:
            break
        if not clock_boundary(cpu, clock):
            cpu.step()
    else:
        raise AssertionError("Diagnostic redirect exceeded its linked instruction bound")
    assert cpu.r[0] == 1
    cpu.r, cpu.sr, cpu.pr, cpu.mach, cpu.macl, cpu.gbr, cpu.vbr, cpu.pc = owner


def exercise(image, segments, symbols, kind):
    def selected_capsule(phase=False):
        data=bytearray(capsule(phase))
        if kind == "malformed_trace":
            struct.pack_into('<I',data,TRACE_OFFSET+16,1)
        if kind == 'malformed_video':
            struct.pack_into('<I',data,VIDEO_OFFSET+16,1)
        return bytes(data)
    runtime.make_capsule = selected_capsule
    try:
        cpu, expected, argv, ip, selection, _ = runtime.prepare(image, segments, symbols, kind)
    finally:
        runtime.make_capsule = ORIGINAL_CAPSULE
    cpu.phase_clock_reads = 0
    cpu.phase_events = []
    raw_read=cpu.raw_read
    def observed_raw_read():
        cpu.phase_events.append(('raw',cpu.r[4],cpu.r[6]))
        raw_read()
    cpu.raw_read=observed_raw_read
    if kind in ("short_trace_bounds","short_video_bounds"):
        runtime_address=symbols['_kui_smash_menu_loader_runtime'][0]
        seed=bytearray(cpu.block(runtime_address,64))
        struct.pack_into('<I',seed,16,1537 if kind=='short_trace_bounds' else 1760)
        struct.pack_into('<I',seed,60,0)
        struct.pack_into('<I',seed,60,runtime.crc(seed))
        cpu.store_block(runtime_address,seed)
    if kind=='bad_video_runtime_crc':
        address=symbols['_kui_smash_menu_loader_phase_video'][0]
        broken=bytearray(cpu.block(address,VIDEO_BYTES));broken[72]^=1
        cpu.store_block(address,broken)
    clock = symbols["_kui_smash_phase_clock_read"][0]
    trace = symbols["_kui_smash_menu_loader_phase_trace"][0]
    # Other payload writes do not start a trace. First MDE arrival does, and
    # a repeated callback for the first destination cannot reset that stamp.
    callback(cpu, symbols, 0xace00200, 512)
    callback(cpu, symbols, 0xace00000, 0)
    assert cpu.phase_clock_reads == 0
    callback(cpu, symbols, 0x8ce00000, 512)
    first = cpu.block(trace, TRACE_BYTES)
    assert cpu.phase_clock_reads == 1
    callback(cpu, symbols, 0xace00000, 512)
    assert cpu.phase_clock_reads == 1 and cpu.block(trace, TRACE_BYTES) == first
    diagnostic = symbols["_kui_smash_menu_loader_diagnostic"][0]
    reason = 0
    for _ in range(14000000):
        if cpu.pc == runtime.RAW:
            cpu.raw_read()
        if cpu.pc == runtime.TARGET:
            break
        if cpu.pc == 0xac007000:
            reason = cpu.read(diagnostic + 4)
            break
        if not clock_boundary(cpu, clock):
            cpu.step()
    else:
        raise AssertionError("Diagnostic low loader exceeded its linked instruction bound")
    carried = cpu.block(runtime.TARGET + TRACE_OFFSET, TRACE_BYTES)
    expected_events=[('clock',1),('clock',2),('clock',3),
                     ('raw',100,1),('raw',101,1),('raw',200,2),('clock',4)]
    assert cpu.phase_events == expected_events + ([('clock',5)] if kind in ('phantasy_star_ii','shining_force') else [])
    if kind == "bad_capsule_crc":
        assert reason == 11 and cpu.pc != runtime.TARGET
        untrusted = bytearray(expected[TRACE_OFFSET:TRACE_OFFSET + TRACE_BYTES])
        untrusted[1700 - TRACE_OFFSET] ^= 1
        assert carried == untrusted
        assert cpu.phase_clock_reads == 4
        return dict(case=kind, reason=reason, trace_unchanged_before_payload_crc=True)
    if kind in ("malformed_trace","short_trace_bounds"):
        assert reason == 14 and cpu.pc != runtime.TARGET
        assert carried == expected[TRACE_OFFSET:TRACE_OFFSET + TRACE_BYTES]
        assert [cpu.read(a) for a in runtime.FIRMWARE_ADDRESSES] == [0xdeadbeef]*9
        assert cpu.phase_clock_reads == 4
        return dict(case=kind,reason=reason,invalid_trace_not_published=True,
                    firmware_not_restored=True,cold_entry_not_reached=True)
    if kind in ('malformed_video','short_video_bounds','bad_video_runtime_crc'):
        assert reason==15 and cpu.pc!=runtime.TARGET
        assert cpu.block(runtime.TARGET+VIDEO_OFFSET,VIDEO_BYTES)==expected[VIDEO_OFFSET:VIDEO_OFFSET+VIDEO_BYTES]
        assert [cpu.read(a) for a in runtime.FIRMWARE_ADDRESSES]==[0xdeadbeef]*9
        assert cpu.phase_clock_reads==4
        return dict(case=kind,reason=reason,invalid_video_not_published=True,
                    firmware_not_restored=True,cold_entry_not_reached=True)
    assert reason == 0 and cpu.pc == runtime.TARGET
    header = struct.unpack_from("<8I", carried)
    assert header == (0x5349554b, 0x314d4954, 1, TRACE_BYTES, 5, 0, 0, 0)
    entries = [struct.unpack_from("<10I", carried, 32 + 40 * i) for i in range(5)]
    assert [entry[0] for entry in entries] == [1, 2, 3, 4, 5]
    assert [entry[6] for entry in entries] == [1000, 2000, 3000, 4000, 5000]
    assert all(entry[1] == 0 and entry[2:6] == (7, 0xc023, 0xe0a, 0) for entry in entries)
    assert carried[32 + 40 * 5:] == bytes(TRACE_BYTES - 32 - 40 * 5)
    assert cpu.block(runtime.TARGET, 64) == expected[:64]
    assert cpu.block(runtime.TARGET+VIDEO_OFFSET,VIDEO_BYTES)==runtime.phase_video_seed()
    assert cpu.block(runtime.TARGET + VIDEO_OFFSET + VIDEO_BYTES,
                     len(expected) - VIDEO_OFFSET - VIDEO_BYTES) == expected[VIDEO_OFFSET + VIDEO_BYTES:]
    assert [cpu.read(a) for a in runtime.FIRMWARE_ADDRESSES] == runtime.original_firmware(kind)
    assert cpu.block(runtime.TARGET + 64 + 32, 256) == argv
    assert cpu.block(runtime.TARGET + 64 + 288, 16) == ip
    return dict(case=kind, captured_selection=selection, phases=[entry[0] for entry in entries],
                first_mde_stamp_one_shot=True, trace_published_after_payload_crc=True,
                firmware_exact=True, ordinary_cold_entry=True,
                raw_reads_strictly_between_hydration_marks=True,
                original_video_copied_after_capsule_crc=True,
                trace_bytes=TRACE_BYTES, working_runtime_bytes=472)


def audit(elf):
    image, _, segments, _, symbols = read_elf(elf)
    assert symbols["_kui_smash_menu_loader_runtime"][1] == 472
    assert symbols["_kui_smash_menu_loader_phase_trace"][1] == TRACE_BYTES
    cases = [exercise(image, segments, symbols, kind)
             for kind in ("phantasy_star_ii", "shining_force", "bad_capsule_crc",
                          "malformed_trace","short_trace_bounds","malformed_video",
                          "short_video_bounds","bad_video_runtime_crc")]
    return dict(schema="kui.smash-menu.loader-phase.v1", passed=True,
                elf_sha256=hashlib.sha256(image).hexdigest(), cases=cases,
                limits=["All linked low loader and trace instructions execute; only raw-card I/O and the independently checked clock reader are finite ABI boundaries.",
                        "Synthetic stamps establish marker order and placement, not console time or performance.",
                        "The ordinary cold-child entry is the execution boundary; high-stage trace import has its own linked proof."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    rendered = json.dumps(audit(args.elf), indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
