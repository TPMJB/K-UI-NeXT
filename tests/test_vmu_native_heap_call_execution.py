#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute external original caller expressions and the patched hook transport.

No retail instruction bytes are embedded. Existing complete cache records bind
the reviewed inputs; JGR's partial static metadata is evidence only, never a cache or
launch admission. This independent SH model executes real input words, checking
both original and replacement target transport under unrelated caller states.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import unittest

from test_vmu_native_heap_execution import SH, MASK, physical, signed, verified_program_image
from sh4_delay_slots import validate_delay_slot

ROOT = Path(__file__).resolve().parents[1]
GAMES = ("ps2", "skies", "sa2", "grandia2", "evolution1", "evolution2", "jgr", "rayman2", "lodoss")
OPTIONS = argparse.Namespace(output=None, record_directory=None, **{game: os.environ.get(
    f"KUI_NATIVE_{game.upper()}_STARTUP") for game in GAMES})
RESULTS = []
STARTUP_STACK_RESULTS = []
STACK, HOOK = 0x8C00EF00, 0x8C004200


def record_file(game):
    directory = getattr(OPTIONS, "record_directory", None) or ROOT / "tests/fixtures/native-cache"
    return Path(directory) / f"{game}.nvr"


class CallerSH(SH):
    def __init__(self, image, load):
        super().__init__(image)
        assert load == 0x8C010000
        self.image_begin, self.image_end = physical(load), physical(load) + len(image)
        self.trace = []
        self.reads = []

    def read(self, address, count=4):
        at = physical(address)
        assert self.image_begin <= at <= self.image_end - count, (hex(address), count)
        self.reads.append((address, count))
        return super().read(address, count)

    def write(self, address, value, count=4):
        assert STACK <= address <= STACK + 32 - count, (hex(address), count)
        super().write(address, value, count)

    def execute_caller(self, start, call, selected, slot, target, first, count, seed):
        self.r = [((0x11335577 + 0x01030507 * i) ^ seed) & MASK for i in range(16)]
        self.r[15], self.pr, self.sr = STACK, 0x8C00D00E, 0x600000F0 | (seed & 1)
        self.pc = start
        selected_seen = 0
        for _ in range(64):
            pc = self.pc
            assert start <= pc <= call, hex(pc)
            self.trace.append(pc)
            op = self.read(pc, 2)
            if pc == selected:
                assert op >> 12 == 0xD
                assert physical(((pc + 4) & ~3) + (op & 255) * 4) == physical(slot)
                selected_seen += 1
            if pc == call:
                assert op & 0xF0FF == 0x400B
                captured_target = self.r[(op >> 8) & 15]
                self.pr = call + 4
                slot_word = self.read(pc + 2, 2)
                validate_delay_slot(slot_word,pc)
                self.plain(slot_word,pc + 2)
                assert selected_seen == 1
                assert captured_target == target, (hex(captured_target), hex(target))
                assert (self.r[4], self.r[5], self.pr) == (first, count, call + 4)
                assert self.r[15] == STACK
                return dict(instructions=self.instructions + 1, selected_loads=selected_seen,
                            target=target, first=self.r[4], bytes=self.r[5], pr=self.pr,
                            stack_spills=len(self.writes), trace=self.trace)
            if op >> 8 in (0x89, 0x8B):
                taken = bool(self.sr & 1) == (op >> 8 == 0x89)
                destination = pc + 4 + 2 * signed(op & 255, 8) if taken else pc + 2
                assert destination > pc and destination <= selected
                self.instructions += 1
                self.pc = destination
            else:
                # The shared model rejects unsupported/control instructions.
                # Actual function calls are handled only at the proved call PC.
                self.plain(op, pc)
                self.pc = pc + 2
        raise AssertionError("caller exceeded bounded instruction path")


def metadata(game, image):
    if game == "jgr":
        partial = json.loads((ROOT / "docs/evidence/native-allocator-family-2026-10-23.json").read_bytes())["inputs"][game]
        assert partial["partial_only"]
        assert hashlib.sha256(image).hexdigest() == partial["sha256"]
        assert len(image) == partial["bytes"]
        call = {key: int(value, 16) for key, value in partial["proved_call"].items()}
        return 0x8C010000, int(partial["initializer_slot"], 16), int(partial["initializer"], 16), call
    wire = record_file(game).read_bytes()
    assert len(wire) == 1024 and wire[:8] == b"KUINVR01"
    assert hashlib.sha256(wire[:992]).digest() == wire[992:]
    assert hashlib.sha256(image).digest() == wire[32:64]
    assert len(image) == struct.unpack_from("<I", wire, 24)[0]
    heap = struct.unpack_from("<18I", wire, 608)
    load, call, pr, begin, count = struct.unpack_from("<5I", wire, 680)
    assert pr == call + 4
    return heap[5], heap[7], heap[8], dict(load=load, call=call, return_pr=pr, begin=begin, bytes=count)


class CallerExecution(unittest.TestCase):
    def test_rayman_startup_owner_stack_frames(self):
        path=OPTIONS.rayman2
        if not path:self.skipTest('Exact external Rayman startup not supplied')
        source,image,_=verified_program_image(path,record_file('rayman2'))
        _,_,_,call=metadata('rayman2',image)
        def op(at):return struct.unpack_from('<H',image,at-0x8c010000)[0]
        def literal(at):
            word=op(at);self.assertEqual(word>>12,0xd)
            return struct.unpack_from('<I',image,((at+4)&~3)+(word&255)*4-0x8c010000)[0]
        # Bind the nested original calls around the stack prologues. Helpers
        # between them are ABI-returning calls; their bodies and game boot are
        # deliberately outside this stack-frame proof.
        self.assertEqual(literal(0x8c01001a),0x8c0cc540)
        self.assertEqual(literal(0x8c0cc55e),0x8c010d92)
        branch=op(0x8c010d96);self.assertEqual(branch>>12,0xb)
        self.assertEqual(0x8c010d96+4+2*signed(branch&4095,12),0x8c010b66)
        self.assertEqual(literal(0x8c010b74),0x8c010e86)
        self.assertEqual(call['call'],0x8c010eca)
        spans=((0x8c0cc542,0x8c0cc544),(0x8c010d92,0x8c010d96),
               (0x8c010b66,0x8c010b70),(0x8c010e86,0x8c010e92))
        cases=[]
        for alias in (0,0x80000000,0xa0000000):
            model=SH(image);model.r[15]=alias|0x0c00f400
            for begin,end in spans:
                for pc in range(begin,end,2):model.plain(op(pc),pc)
            self.assertEqual(model.r[15],alias|0x0c00b3a4)
            self.assertLess(physical(model.r[15]),0x0c00c100)
            self.assertGreaterEqual(physical(model.r[15])-52,0x0c008000+0x100-52)
            self.assertGreater(physical(model.r[15])-52,0x0c007d00+32)
            cases.append(dict(boot_sp=hex(alias|0x0c00f400),heap_hook_sp=hex(model.r[15]),
                              original_stack_instruction_count=model.instructions))
        STARTUP_STACK_RESULTS.append(dict(game='rayman2',source_sha256=hashlib.sha256(source).hexdigest(),
            original_call_edges_verified=True,original_prologues_executed=4,
            intervening_helpers='ABI returns preserve SP; helper bodies not executed',
            game_boot_verified=False,old_lower_bound_refuses=True,
            retained_low_reader_and_52byte_frame_disjoint=True,cases=cases))
        print('Original Rayman startup stack: 16 KiB caller frame reaches heap hook at 0c00b3a4')

    def execute(self, game):
        path = getattr(OPTIONS, game)
        if not path:
            self.skipTest(f"Supply --{game} or KUI_NATIVE_{game.upper()}_STARTUP")
        source = Path(path).read_bytes()
        extent = None
        if game == "jgr":
            image = source
        else:
            source, image, extent = verified_program_image(
                path, record_file(game))
        load, slot, target, call = metadata(game, image)
        self.assertEqual(struct.unpack_from("<I", image, physical(slot) - physical(load))[0], target)
        # The production decoder grants no caller state. Execute every bounded
        # possible start independently; keep only starts that agree in all six
        # original/patched and unrelated-register/SR trials. Earlier unrelated
        # calls or unknown data cannot become a successful expression.
        accepted = []
        for start in range(max(load, call["call"] - 48), call["load"] + 1, 2):
            trials = []
            for replacement in (target, HOOK):
                patched = bytearray(image)
                struct.pack_into("<I", patched, physical(slot) - physical(load), replacement)
                for seed in (0, 0x5A3C196D, 0xA5C3E692):
                    try:
                        model = CallerSH(patched, load)
                        result = model.execute_caller(start, call["call"], call["load"], slot,
                                                      replacement, call["begin"], call["bytes"], seed)
                        trials.append(result)
                    except AssertionError:
                        break
                else:
                    continue
                break
            if len(trials) == 6:
                accepted.append((start, trials))
        self.assertTrue(accepted, f"No independent original/patched argument transport for {game}")
        start, trials = accepted[0]
        result = dict(game=game, image_sha256=hashlib.sha256(image).hexdigest(),
                      image_bytes=len(image), source_sha256=hashlib.sha256(source).hexdigest(),
                      source_bytes=len(source), discarded_suffix_bytes=len(source)-len(image),
                      program_extent_production_verified=extent is not None,
                      start=f"0x{start:08x}", call=f"0x{call['call']:08x}",
                      selected=f"0x{call['load']:08x}", slot=f"0x{slot:08x}",
                      original_target=f"0x{target:08x}", hook_target=f"0x{HOOK:08x}",
                      begin=f"0x{call['begin']:08x}", bytes=f"0x{call['bytes']:08x}",
                      pr=f"0x{call['return_pr']:08x}", successful_caller_starts=len(accepted),
                      independent_trials=len(trials), instructions=[r["instructions"] for r in trials],
                      selected_literal_executed_once=all(r["selected_loads"] == 1 for r in trials),
                      immutable_image_reads=True, only_bounded_stack_spills=True,
                      jgr_partial_only=game == "jgr")
        RESULTS.append(result)
        print(f"Original/patched {game} caller: {len(trials)} trials, start={start:08x}, "
              f"target load executed once, R4/R5/PR preserved")


for _game in GAMES:
    setattr(CallerExecution, "test_" + _game, lambda self, game=_game: self.execute(game))


if __name__ == "__main__":
    if not __debug__:
        raise SystemExit("Run original caller model with Python assertions enabled")
    parser = argparse.ArgumentParser(description=__doc__)
    for game in GAMES:
        parser.add_argument("--" + game, type=Path, default=getattr(OPTIONS, game))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--record-directory", type=Path,
                        help="Fresh production-generated records for a separately audited corpus")
    OPTIONS, remaining = parser.parse_known_args()
    suite = unittest.main(argv=[sys.argv[0], *remaining], exit=False)
    if suite.result.wasSuccessful() and OPTIONS.output:
        OPTIONS.output.write_text(json.dumps(dict(schema="kui.native.original-caller-execution.v1",
            retail_code_embedded=False, results=RESULTS,
            original_startup_stack_frames=STARTUP_STACK_RESULTS), indent=2) + "\n")
    raise SystemExit(not suite.result.wasSuccessful())
