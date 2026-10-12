#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise all four actual startup DATA budget profiles and reject mutations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ["src/loader/toy_pilot_sci.c", "src/core/retail_cursor.c",
          "src/core/retail_image.c", "src/core/retail_gd.c"]
DEFINES = {
    "KUI_TOY_PILOT_DATA_SCHEDULE_MODE": 2, "KUI_TOY_PILOT_PRIVATE_P2": 1,
    "KUI_TOY_PILOT_GD_FIXED_STEP": 2, "KUI_TOY_PILOT_LOADER_TRACE": 1,
    "KUI_TOY_PILOT_DATA_PROBE": 1, "KUI_RETAIL_GD_ASYNC": 1,
    "KUI_TOY_LOADER_SCHEDULE_HOST_TEST": 1, "KUI_TOY_PILOT_SCI_TEST": 1,
    "KUI_TOY_PILOT_SHARED_SCI": 0, "KUI_TOY_PILOT_ASYNC_CDDA": 0,
    "KUI_TOY_PILOT_NATIVE_CACHE": 0, "KUI_TOY_PILOT_SYNTHETIC_SOURCE": 0,
    "KUI_SCI_DMA_REUSE_TDRE": 0, "KUI_TOY_PILOT_DATA_PAYLOAD_MODE": 0,
}
MUTATIONS = [
    ("block admission removed", "if(e.verified_since>=KUI_TOY_SCI_SERVICE_BLOCKS)", "if(0)"),
    ("time admission removed", "if(step && ticks()-before>=KUI_TOY_SCI_SERVICE_TICKS){budget_exit=", "if(0){budget_exit="),
    ("token slice return removed", "if(kui_sci_stream_token_pending()){budget_exit=", "if(0){budget_exit="),
    ("cumulative blocks recounted", "e.stats.call_blocks-blocks_before);", "e.stats.call_blocks-blocks_before+1u);"),
    ("telemetry saturation removed", "*out=value>UINT32_MAX-*out?UINT32_MAX:*out+value;", "*out=*out+value;"),
    ("successful credit counted as zero", "if(!credit)sci_budget_add", "if(1)sci_budget_add"),
    ("revoked credit unsigned subtraction", "s && s->completed_bytes>=credit_before?s->completed_bytes-credit_before:0u", "s?s->completed_bytes-credit_before:0u"),
]
UNSAFE = [
    {"KUI_TOY_PILOT_DATA_BUDGET_PROFILE": -1},
    {"KUI_TOY_PILOT_DATA_BUDGET_PROFILE": 5},
    *[{"KUI_TOY_PILOT_DATA_SCHEDULE_MODE": mode} for mode in [0, 1, 3]],
    {"KUI_TOY_PILOT_LOADER_TRACE": 0}, {"KUI_TOY_PILOT_DATA_PROBE": 0},
    {"KUI_TOY_PILOT_SHARED_SCI": 1}, {"KUI_TOY_PILOT_ASYNC_CDDA": 1},
    {"KUI_TOY_PILOT_PRIVATE_P2": 0}, {"KUI_TOY_PILOT_NATIVE_CACHE": 1},
    {"KUI_TOY_PILOT_SYNTHETIC_SOURCE": 1}, {"KUI_TOY_PILOT_GD_FIXED_STEP": 1},
    {"KUI_SCI_DMA_REUSE_TDRE": 1}, {"KUI_TOY_PILOT_DATA_PAYLOAD_MODE": 1},
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/budget-host")
    args = parser.parse_args()
    directory = args.build_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(args.cc)
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
             "-Iinclude", "-Isrc/loader"]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    results = []

    def compile_case(name, profile, fixture, engine=None, extra=None):
        executable = directory / name
        defines = dict(DEFINES, KUI_TOY_PILOT_DATA_BUDGET_PROFILE=profile)
        defines.update(extra or {})
        sources = [fixture, engine or ENGINE[0], *ENGINE[1:]]
        command = [*cc, *flags, *[f"-D{k}={v}" for k, v in defines.items()],
                   *sources, "-o", str(executable)]
        built = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(name + " compilation failed:\n" + built.stdout + built.stderr)
        return executable, command, sources

    def run_case(name, profile, fixture, extra=None):
        executable, command, sources = compile_case(name, profile, fixture, extra=extra)
        run = subprocess.run([str(executable)], cwd=ROOT, env=env, capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(name + " failed:\n" + run.stdout + run.stderr)
        print(run.stdout.rstrip(), flush=True)
        results.append({"name": name, "profile": profile, "command": command,
                        "output": run.stdout, "stderr": run.stderr, "exit_code": run.returncode,
                        "source_sha256": {s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest()
                                          for s in sources}})
        return executable

    legacy = run_case("legacy-startup-engine", 0, "tests/test_toy_startup_sci.c")
    symbols = subprocess.check_output(["nm", "--defined-only", str(legacy)], text=True)
    if "kui_toy_pilot_sci_budget_snapshot" in symbols or "sci_budget_add" in symbols:
        raise RuntimeError("Legacy profile unexpectedly emitted budget helpers")
    for profile in range(1, 5):
        run_case(f"budget-{profile}", profile, "tests/test_toy_data_budget.c")
        run_case(f"budget-wire-{profile}", profile, "tests/test_toy_startup_sci_wire.c",
                 {"KUI_SCI_STREAM_TEST": 1})

    original = (ROOT / ENGINE[0]).read_text()
    mutation_results = []
    for index, (label, needle, replacement) in enumerate(MUTATIONS):
        if original.count(needle) != 1:
            raise RuntimeError("Mutation anchor is not unique: " + label)
        path = directory / f"mutated-engine-{index}.c"
        path.write_text(original.replace(needle, replacement))
        executable, command, _ = compile_case(f"mutant-{index}", 1,
                                               "tests/test_toy_data_budget.c", str(path))
        run = subprocess.run([str(executable)], cwd=ROOT, env=env, capture_output=True, text=True)
        if run.returncode == 0 or "Assertion" not in run.stderr:
            raise RuntimeError("Mutation was not rejected by an assertion: " + label + "\n" + run.stderr)
        mutation_results.append({"name": label, "command": command, "exit_code": run.returncode,
                                 "assertion": run.stderr.strip().splitlines()[-1]})

    guards = []
    for changes in UNSAFE:
        defines = dict(DEFINES, KUI_TOY_PILOT_DATA_BUDGET_PROFILE=1)
        defines.update(changes)
        command = [*cc, "-std=c11", "-Iinclude", "-Isrc/loader",
                   *[f"-D{k}={v}" for k, v in defines.items()], "-x", "c", "-fsyntax-only", "-"]
        built = subprocess.run(command, cwd=ROOT, input='#include "toy_pilot_sci.h"\n',
                               capture_output=True, text=True)
        if built.returncode == 0 or "#error" not in built.stderr:
            raise RuntimeError("Unsafe configuration unexpectedly accepted: " + repr(changes))
        guards.append({"changed_defines": changes, "exit_code": built.returncode})
    (directory / "budget-tests.json").write_text(json.dumps({
        "schema": 1, "sanitizers": ["address", "undefined"],
        "leak_sanitizer": "disabled: restricted /proc, fixture heap allocation absent",
        "scope": "production engine/cursor, independent timing and real-wire card model; hardware playback unproven",
        "legacy_budget_symbols_absent": True, "results": results,
        "mutation_results": mutation_results, "unsafe_configurations": guards,
    }, indent=2) + "\n")
    print(f"Startup DATA budget profiles 1..4 passed; {len(MUTATIONS)} behavioral mutations and {len(UNSAFE)} unsafe configurations rejected")


if __name__ == "__main__":
    main()
