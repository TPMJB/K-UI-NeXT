#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the early reset lifetime and original return-context helpers."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
from test_native_standalone_foreground_gin import Gin, LOAD
from test_smashpack_native_engine_gin import OWNERS


def source(files):
    mde, menu, gin = files['MDE_US.BIN'], files['1ST_READ.BIN'], files['GINXFER.BIN']
    assert zlib.crc32(mde[0x17f0:0x181a]) == 0xd71c4066
    cpu = Gin(gin, mde, b'');cpu.store_block(LOAD, mde)
    cpu.pc, cpu.pr, cpu.r[15] = 0x8c0117f0, 0x8c003000, 0x8c00f400
    cleanup = [0x8c1a99ac, 0x8c1a99ac, 0x8c1a650a, 0x8c1a5b04, 0x8c010154]
    calls = []
    for _ in range(100):
        if cpu.pc == 0x8c1599ee:
            break
        if cpu.pc in set(cleanup):
            calls.append(cpu.pc);cpu.r[0] = 0;cpu.pc = cpu.pr
        cpu.step()
    assert cpu.pc == 0x8c1599ee and calls == cleanup
    # Execute the original return helper, including its actual64-byte memcpy,
    # until its tail branch reaches the GINS executor. No cleanup is invoked.
    cpu.pc, cpu.pr, cpu.r[15] = 0x8c1599ee, 0x8c003000, 0x8c00f400
    cpu.gbr = 0x8c2f9b80
    before = cpu.instructions
    for _ in range(10000):
        if cpu.pc == 0x8c159ab8:
            break
        cpu.step()
    assert cpu.pc == 0x8c159ab8 and cpu.r[4:7] == [0x8c19eea0, 0x8c19f1a0, 4]
    assert cpu.gbr == 0x8c2f9b80 and cpu.block(cpu.r[4], 14) == b'\\manifest.bin\0'
    return_helper_instructions = cpu.instructions - before
    # The small scalar CRT resets menu BSS, not all16MiB. The subsequent
    # fresh menu SDK/allocator still owns a new lifetime; old lease contents
    # surviving this particular loop do not retain allocation ownership.
    cpu = Gin(gin, menu, b'');cpu.store_block(LOAD, menu)
    cpu.pc, cpu.pr, cpu.r[15] = 0x8c038c40, 0x8c003000, 0x8c00f3f8
    retained = bytes([0xa7]) * 4096;cpu.store_block(0x8cbe0000, retained)
    cpu.writes = []
    for _ in range(1000000):
        if cpu.pc == cpu.pr:
            break
        cpu.step()
    assert cpu.pc == cpu.pr and cpu.block(0x8cbe0000, 4096) == retained
    zero = [(a, n) for pc, a, _, n in cpu.writes if pc == 0x8c038c4a]
    assert zero and min(a for a, _ in zero) == 0x8c043d80 and max(a+n for a, n in zero) == 0x8c06e760
    return dict(schema='kui.smash-menu.return-source.v1', passed=True, original_sha256=OWNERS,
        reset_entry='0x8c0117f0', reset_source_bytes=42, reset_source_crc32='d71c4066',
        cleanup_calls_before_gins=[hex(x) for x in calls], original_return_helper='0x8c1599ee',
        later_gins_executor='0x8c159ab8', original_return_helper_instructions=return_helper_instructions,
        original_return_parser_input='\\manifest.bin', original_return_folder='', original_return_mode=4,
        original_gbr_preserved=True, scalar_menu_bss_clear=['0x8c043d80', '0x8c06e760'],
        old_high_ram_canary_unchanged_by_scalar_crt=True,
        limits=['Cleanup callees are ABI substitutions in the reset-wrapper ordering probe. The original return helper and its memcpy execute.',
                'The original scalar CRT does not zero the old high worker bytes in this test; this grants no ownership across the fresh SDK heap initialization.',
                'No game, filesystem, DMA or real hardware execution claim is made.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--originals', type=Path, required=True);p.add_argument('--report', type=Path)
    a = p.parse_args();files = {name: (a.originals/name).read_bytes() for name in OWNERS}
    assert all(hashlib.sha256(files[n]).hexdigest() == h for n, h in OWNERS.items())
    report = source(files);text = json.dumps(report, indent=2) + '\n'
    if a.report:
        a.report.parent.mkdir(parents=True, exist_ok=True);a.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
