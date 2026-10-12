#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate AE/AF DATA limits and unchanged AG/AH DATA against an AD control."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

import test_toy_data_budget as baseline

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = "tests/test_toy_ad_followup_data.c"
WIRE_FIXTURE = "tests/test_toy_startup_sci_wire.c"
HEADER = "src/loader/toy_pilot_sci.h"
DESCRIPTORS = {0: (4, 12, 6000), 1: (5, 24, 6000), 2: (6, 24, 12000),
               3: (7, 12, 6000), 4: (8, 12, 6000)}
MUTATIONS = [
    ("AE 24-block cap reverted to AD 12", 1, HEADER,
     "#define KUI_TOY_SCI_SERVICE_BLOCKS 24u", "#define KUI_TOY_SCI_SERVICE_BLOCKS 12u", 1),
    ("AF 12000-tick cap reverted to AD 6000", 2, HEADER,
     "#define KUI_TOY_SCI_SERVICE_TICKS 12000u", "#define KUI_TOY_SCI_SERVICE_TICKS 6000u", 1),
    ("follow-up report profile reverted to old AD ID", 1, baseline.ENGINE[0],
     "e.budget.profile=KUI_TOY_AD_BUDGET_REPORT_PROFILE;",
     "e.budget.profile=KUI_TOY_PILOT_DATA_BUDGET_PROFILE;", 1),
    ("block admission removed", 2, baseline.ENGINE[0],
     "if(e.verified_since>=KUI_TOY_SCI_SERVICE_BLOCKS){budget_exit=", "if(0){budget_exit=", 1),
    ("time admission removed", 2, baseline.ENGINE[0],
     "if(step && ticks()-before>=KUI_TOY_SCI_SERVICE_TICKS){budget_exit=", "if(0){budget_exit=", 1),
    ("stopped-clock step bound doubled", 2, HEADER,
     "#define KUI_TOY_SCI_SERVICE_STEPS 1024u", "#define KUI_TOY_SCI_SERVICE_STEPS 2048u", 1),
    ("intercall IRQ blocks excluded from foreground quota", 1, baseline.ENGINE[0],
     "if(e.verified_since<KUI_TOY_SCI_SERVICE_BLOCKS)++e.verified_since;",
     "if(!e.in_irq && e.verified_since<KUI_TOY_SCI_SERVICE_BLOCKS)++e.verified_since;", 2),
]
UNSAFE = [
    {"KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE": -1},
    {"KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE": 5},
    *[{"KUI_TOY_PILOT_DATA_BUDGET_PROFILE": n} for n in [0, 1, 2, 3]],
    *[{"KUI_TOY_PILOT_DATA_SCHEDULE_MODE": n} for n in [0, 1, 3]],
    {"KUI_TOY_PILOT_LOADER_TRACE": 0}, {"KUI_TOY_PILOT_DATA_PROBE": 0},
    {"KUI_TOY_PILOT_PRIVATE_P2": 0}, {"KUI_TOY_PILOT_NATIVE_CACHE": 1},
    {"KUI_TOY_PILOT_SHARED_SCI": 1}, {"KUI_TOY_PILOT_ASYNC_CDDA": 1},
    {"KUI_TOY_PILOT_SYNTHETIC_SOURCE": 1}, {"KUI_TOY_PILOT_GD_FIXED_STEP": 1},
    {"KUI_SCI_DMA_REUSE_TDRE": 1}, {"KUI_TOY_PILOT_DATA_PAYLOAD_MODE": 1},
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/ad-followup-data-host")
    args = parser.parse_args()
    directory = args.build_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(args.cc)
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    results = []
    observations = {}

    def compile_case(name, profile, fixture, mutation_dir=None):
        executable = directory / name
        defines = dict(baseline.DEFINES, KUI_TOY_PILOT_DATA_BUDGET_PROFILE=4,
                       KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=profile)
        if fixture == WIRE_FIXTURE:
            defines["KUI_SCI_STREAM_TEST"] = 1
        includes = (["-I" + str(mutation_dir)] if mutation_dir else []) + ["-Iinclude", "-Isrc/loader"]
        engine = str(mutation_dir / "toy_pilot_sci.c") if mutation_dir else baseline.ENGINE[0]
        sources = [fixture, engine, *baseline.ENGINE[1:]]
        command = [*cc, *flags, *includes, *[f"-D{k}={v}" for k, v in defines.items()],
                   *sources, "-o", str(executable)]
        built = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise RuntimeError(name + " compilation failed:\n" + built.stdout + built.stderr)
        return executable, command, sources

    def run_case(name, profile, fixture):
        executable, command, sources = compile_case(name, profile, fixture)
        run = subprocess.run([str(executable)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
        if run.returncode:
            raise RuntimeError(name + " failed:\n" + run.stdout + run.stderr)
        print(run.stdout.rstrip(), flush=True)
        results.append({"name": name, "followup_profile": profile, "command": command,
                        "stdout": run.stdout, "stderr": run.stderr, "exit_code": run.returncode,
                        "source_sha256": {s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest()
                                          for s in sources}})
        if fixture == FIXTURE:
            match = re.search(r"report=(\d+) blocks=(\d+) ticks=(\d+); fast-cap=(\d+) "
                              r"time-cap=(\d+) timed-calls=(\d+); DATA_TRANSCRIPT=([0-9a-f]{8})", run.stdout)
            if not match:
                raise RuntimeError("Missing independently observed fixture output: " + name)
            values = tuple(map(int, match.groups()[:6]))
            if values[:3] != DESCRIPTORS[profile] or values[3] != DESCRIPTORS[profile][1]:
                raise RuntimeError("Matrix descriptor or fast block cap mismatch: " + name)
            if not 0 < values[4] < values[3]:
                raise RuntimeError("Clock cap did not independently bind: " + name)
            observations[profile] = {"report_profile": values[0], "block_limit": values[1],
                                     "tick_limit": values[2], "fast_receiver_verified_blocks": values[3],
                                     "clock_limited_verified_blocks": values[4], "timed_calls": values[5],
                                     "DATA_transcript": match.group(7)}

    for profile in range(5):
        run_case(f"followup-{profile}", profile, FIXTURE)
        run_case(f"followup-wire-{profile}", profile, WIRE_FIXTURE)
    for profile in [3, 4]:
        if (observations[profile]["DATA_transcript"], observations[profile]["timed_calls"]) != (
                observations[0]["DATA_transcript"], observations[0]["timed_calls"]):
            raise RuntimeError(f"Follow-up {profile} changed DATA behavior from AD control")

    mutations = []
    engine_text = (ROOT / baseline.ENGINE[0]).read_text()
    header_text = (ROOT / HEADER).read_text()
    for index, (label, profile, source, needle, replacement, expected_count) in enumerate(MUTATIONS):
        text = header_text if source == HEADER else engine_text
        if text.count(needle) != expected_count:
            raise RuntimeError("Mutation anchor count changed: " + label)
        mutated = text.replace(needle, replacement)
        case_dir = directory / f"mutation-{index}"
        case_dir.mkdir(exist_ok=True)
        (case_dir / "toy_pilot_sci.c").write_text(mutated if source != HEADER else engine_text)
        (case_dir / "toy_pilot_sci.h").write_text(mutated if source == HEADER else header_text)
        executable, command, _ = compile_case(f"mutant-{index}", profile, FIXTURE, case_dir)
        run = subprocess.run([str(executable)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
        if run.returncode == 0 or "Assertion" not in run.stderr:
            raise RuntimeError("Mutation was not rejected by a fixture assertion: " + label + "\n" + run.stderr)
        mutations.append({"name": label, "followup_profile": profile, "source": source,
                          "command": command, "exit_code": run.returncode,
                          "assertion": run.stderr.strip().splitlines()[-1]})

    guards = []
    for changes in UNSAFE:
        defines = dict(baseline.DEFINES, KUI_TOY_PILOT_DATA_BUDGET_PROFILE=4,
                       KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=1)
        defines.update(changes)
        command = [*cc, "-std=c11", "-Iinclude", "-Isrc/loader",
                   *[f"-D{k}={v}" for k, v in defines.items()], "-x", "c", "-fsyntax-only", "-"]
        built = subprocess.run(command, cwd=ROOT, input='#include "kui/toy_loader_schedule.h"\n',
                               capture_output=True, text=True, timeout=30)
        if built.returncode == 0 or "#error" not in built.stderr:
            raise RuntimeError("Unsafe follow-up configuration accepted: " + repr(changes))
        guards.append({"changed_defines": changes, "command": command, "exit_code": built.returncode})
    report = {"schema": 1, "sanitizers": ["address", "undefined"],
              "scope": "Production DATA engine/GD/cursor with independent timing; production SCI receiver with real card framing. Hardware playback and audio factors are outside this runner.",
              "matrix_observations": observations, "AG_AH_DATA_match_AD_control": True,
              "results": results, "mutation_results": mutations, "unsafe_configurations": guards,
              "source_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in
                                [FIXTURE, WIRE_FIXTURE, "tests/test_toy_pilot_sci.c",
                                 "tests/test_toy_pilot_sci_async_integration.c", "tests/sci_stream_model.h",
                                 HEADER, "include/kui/toy_loader_ad_followup.h", "src/loader/sci_stream.c"]}}
    (directory / "ad-followup-data-tests.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"AD follow-up DATA profiles 1..4 passed; AG/AH DATA transcripts equal AD; "
          f"{len(mutations)} behavioral mutations and {len(guards)} unsafe configurations rejected")


if __name__ == "__main__":
    main()
