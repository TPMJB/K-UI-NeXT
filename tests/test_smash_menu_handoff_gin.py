#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove the original menu's argv and the post-copy GIN redirect boundary.

The exact original files are optional external inputs. No original executable
bytes are embedded in this test or its evidence. This is a scalar CPU/source
proof; it does not execute the replacement loader, game SDK or game itself.
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
from test_native_standalone_foreground_gin import Gin, GIN_STAGE, LOAD, TARGET
from test_smashpack_native_engine_gin import OWNERS

GIN_BYTES = 8436
GIN_CODE_OFFSET = 0x2000
GIN_CODE_CRC = 0xa0fc2827
GIN_TARGET_OFFSET = 0x20e4
MDE_CONTEXT = 0x8c19ee60
MENU_CONTEXT = 0x8c036a44
ARGUMENT_PARSER = 0x8c015a48
FOLDER_SETTER, MODE_SETTER = 0x8c03c9c0, 0x8c03ca12
RETURN = 0x8c003000

# (original folder string, original command string, mode, skip, sound).
ROWS = [("ALTBEAST", "ALTBEAST.SGA", 2, 0, 0),
        ("COLUMNS ", "COLUMNS.SGA", 0, 0, 0),
        ("GOLDAXE ", "GOLDAXE.SGA", 0, 0, 0),
        ("PHSTAR2 ", "PHSTAR2.SGA", 0, 0, 0),
        ("REVSHIN ", "REVSHIN.SGA", 0, 1, 0),
        ("SHFORCE ", "SHFORCE.SGA", 0, 0, 1),
        ("SONIC1  ", "SONIC1.SGA", 0, 1, 0),
        ("STRAGE2 ", "STRAGE2.SGA", 0, 1, 0),
        ("VECTORM ", "VECTORM.SGA", 4, 2, 0),
        ("WRESTWAR", "WRESTWAR.SGA", 0, 0, 0)]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def original_call(cpu, pc, arguments):
    cpu.pc, cpu.pr, cpu.r[15] = pc, RETURN, 0x8cff0000
    for index, value in enumerate(arguments):
        cpu.r[4 + index] = value
    before = cpu.instructions
    for _ in range(100000):
        if cpu.pc == RETURN:
            return cpu.instructions - before
        cpu.step()
    raise AssertionError("Original menu helper exceeded its instruction bound")


def menu_arguments(gin, menu):
    cpu = Gin(gin, menu, b"")
    cpu.store_block(LOAD, menu)
    result = []
    payloads = {}
    for folder, rom, mode, skip, sound in ROWS:
        # Read the exact source spelling, including the differing spaces
        # between the short ROM names and their first mode option.
        first = menu.index(("\\MDE_US.BIN " + rom).encode())
        end = menu.index(0, first)
        command = menu[first:end]
        tokens = ["\\MDE_US.BIN", rom, "MODE" + str(mode),
                  "SKIP" + str(skip), "SOUND" + str(sound)]
        assert command.decode().split() == tokens
        expected = (b"\0".join(t.encode() for t in tokens) + b"\0\0").ljust(256, b"\0")
        original = cpu.block(MENU_CONTEXT, 8192)
        cpu.store_block(MENU_CONTEXT + 256, bytes([0xa7]) * 256)
        instructions = original_call(cpu, ARGUMENT_PARSER, [MENU_CONTEXT, LOAD + first])
        actual = cpu.block(MENU_CONTEXT + 256, 256)
        assert actual == expected
        assert cpu.block(MENU_CONTEXT, 256) == original[:256]
        assert cpu.block(MENU_CONTEXT + 512, 8192 - 512) == original[512:]
        # Exercise the original low-IP setters rather than synthesizing the
        # folder shape. Their two-byte tail belongs to the original IP.
        cpu.store_block(0x8c0080f0, bytes([0xa7]) * 16)
        folder_offset = menu.index(folder.encode() + b"\0", 0x17f28)
        original_call(cpu, FOLDER_SETTER, [LOAD + folder_offset])
        assert cpu.r[0] == 0
        original_call(cpu, MODE_SETTER, [4])
        assert cpu.r[0] == 0
        ip = cpu.block(0x8c0080f0, 16)
        assert ip == folder.encode().ljust(12, b" ") + b"\x01\x04\xa7\xa7"
        payloads[folder.rstrip()] = actual, ip
        result.append(dict(folder=folder.rstrip(), tokens=tokens,
                           command_address=hex(LOAD + first),
                           parser_instructions=instructions,
                           argv_sha256=sha(actual), argv_zero_padded=True,
                           ip_folder_source_setters=True, ip_tail_preserved=True))
    return result, payloads


def menu_transfer_header(gin, menu):
    """Execute the original accepted-GIN span that writes transfer fields.

    The original menu entry at0x8c015b90 loads its mode fromGINS+24,
    the loader saves it atSP+12, and this straight-line source span loads
    literal1 intoR4 before storingGINS+28. The initial header has zero there.
    """
    cpu = Gin(gin, menu, b"")
    cpu.store_block(LOAD, menu)
    cpu.store_block(GIN_STAGE, gin[:8192])
    assert cpu.read(MENU_CONTEXT + 24) == 4 and cpu.read(GIN_STAGE + 28) == 0
    # Bind the caller's actual load/save contract, rather than treating the
    # transfer flag or mode as arbitrary constructor inputs.
    assert cpu.read(0x8c015b90, 2) == 0x56e6  # MOV.L @(24,R14),R6.
    assert cpu.read(0x8c015c5e, 2) == 0x1f63  # MOV.L R6,@(12,R15).
    cpu.r[9], cpu.r[12], cpu.r[15] = GIN_STAGE, MENU_CONTEXT, 0x8cfa0000
    cpu.r[4] = 0xfeedbeef
    cpu.write(cpu.r[15], 903)
    cpu.write(cpu.r[15] + 8, 0x8ce00000)
    cpu.write(cpu.r[15] + 12, cpu.read(MENU_CONTEXT + 24))
    cpu.pc = 0x8c015e12
    before = cpu.instructions
    for _ in range(100):
        if cpu.pc == 0x8c015e76:
            break
        cpu.step()
    assert cpu.pc == 0x8c015e76
    fields = [cpu.read(GIN_STAGE + offset) for offset in (16, 20, 24, 28)]
    assert fields == [0x8ce00000, 903, 4, 1]
    return dict(original_span_begin="0x8c015e12", original_span_end="0x8c015e76",
                instructions=cpu.instructions - before, staged_source=hex(fields[0]),
                sectors=fields[1], original_menu_mode=fields[2], transfer_flag=fields[3],
                original_header_transfer_flag=0, literal_one_store="0x8c015e74")


def gin_redirect(gin, app, argv, ip, hook):
    assert len(gin) == GIN_BYTES
    assert zlib.crc32(gin[GIN_CODE_OFFSET:]) == GIN_CODE_CRC
    assert struct.unpack_from("<I", gin, GIN_TARGET_OFFSET)[0] == TARGET
    changed = bytearray(gin)
    struct.pack_into("<I", changed, GIN_TARGET_OFFSET, hook)
    assert changed[:GIN_TARGET_OFFSET] == gin[:GIN_TARGET_OFFSET]
    assert changed[GIN_TARGET_OFFSET + 4:] == gin[GIN_TARGET_OFFSET + 4:]
    cpu = Gin(changed, app, b"")
    retained_begin, retained_end = 0x8c008100, 0x8c00c000
    retained = bytes((i * 13 + 0x65) & 255 for i in range(retained_end - retained_begin))
    cpu.store_block(retained_begin, retained)
    context = bytearray(app[MDE_CONTEXT - LOAD:MDE_CONTEXT - LOAD + 8192])
    assert struct.unpack_from("<4I", context) == (0x47494e53, 1, 0x3ac59e76, 8192)
    struct.pack_into("<4I", context, 16, cpu.source, cpu.sectors, 4, 1)
    context[256:512] = argv
    cpu.store_block(GIN_STAGE, context)
    cpu.store_block(0x8c0080f0, ip)
    for _ in range(6000000):
        if cpu.pc == hook:
            break
        cpu.step()
    assert cpu.pc == cpu.spc == hook
    assert cpu.copy_checkpoint[:len(app)] == app
    assert cpu.block(MDE_CONTEXT, 8192) == context
    assert cpu.block(0x8c0080f0, 16) == ip
    assert cpu.block(retained_begin, len(retained)) == retained
    assert not any((address & 0x1fffffff) < (retained_end & 0x1fffffff) and
                   (address & 0x1fffffff) + width > (retained_begin & 0x1fffffff)
                   for _, address, _, width in cpu.writes)
    assert cpu.block(LOAD, MDE_CONTEXT - LOAD) == app[:MDE_CONTEXT - LOAD]
    end = MDE_CONTEXT - LOAD + 8192
    assert cpu.block(LOAD + end, len(app) - end) == app[end:]
    assert cpu.r[15] == cpu.vbr == 0x8c00f400
    assert cpu.sr == cpu.ssr == 0x700000f0 and cpu.gbr == 0x8c000000
    assert cpu.pr == cpu.mach == 0 and cpu.macl == 0x28
    assert not cpu.mmio_reads and not cpu.mmio_writes
    return dict(gin_entry="0x8cfd2000", target_literal="0x8cfd20e4",
                final_rte="0x8cfd20c0", redirected_entry=hex(hook),
                staging_begin=hex(cpu.source), staging_end=hex(cpu.source + cpu.sectors * 2048),
                copied_context=hex(MDE_CONTEXT), original_image_preserved_except_gin_context=True,
                actual_argv_and_ip_preserved=True, instructions=cpu.instructions,
                retained_raw_handoff_and_stack_begin=hex(retained_begin),
                retained_raw_handoff_and_stack_end=hex(retained_end),
                retained_raw_handoff_and_stack_unchanged=True,
                original_scalar_restart_state_preserved=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--originals", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    # The boundary test stops before this sentinel's first instruction. The
    # loaded production hook is independently linked and audited by its test.
    parser.add_argument("--handoff-entry", type=lambda s: int(s, 0), default=0xac008300)
    args = parser.parse_args()
    assert args.handoff_entry == 0xac008300
    originals = {name: (args.originals / name).read_bytes() for name in OWNERS}
    assert all(sha(originals[name]) == expected for name, expected in OWNERS.items())
    rows, payloads = menu_arguments(originals["GINXFER.BIN"], originals["1ST_READ.BIN"])
    transfer = menu_transfer_header(originals["GINXFER.BIN"], originals["1ST_READ.BIN"])
    cases = [dict(folder=folder, **gin_redirect(originals["GINXFER.BIN"], originals["MDE_US.BIN"],
                      *payloads[folder], args.handoff_entry)) for folder in ("PHSTAR2", "SHFORCE")]
    report = dict(schema="kui.smash-menu.gin-handoff-source.v1", passed=True,
                  owner_sha256=OWNERS, original_menu_parser=hex(ARGUMENT_PARSER),
                  original_folder_setters=[hex(FOLDER_SETTER), hex(MODE_SETTER)],
                  gin_code_crc32=hex(GIN_CODE_CRC), gin_code_bytes=244,
                  menu_selections=rows, original_transfer_header=transfer, post_copy_cases=cases,
                  limits=["The hook sentinel is a stop boundary; replacement loader, media, SDK, game and VMU are not executed.",
                          "The scalar interpreter does not model register-bank switching; hardware post-RTE R0..R7 values are not established."])
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
