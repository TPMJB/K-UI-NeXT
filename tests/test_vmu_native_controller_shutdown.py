#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run actual worker cancellation/source proofs with a supplied original SDK.

The original is used read-only. Reports contain identity metadata and counts;
no game bytes. No SH toolchain, Make or canonical artifact mutation is needed.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["tests/test_vmu_native_controller_shutdown.c", "src/core/vmu_native_heap.c",
    "src/core/vmu_native_runtime.c", "src/core/vmu_native_layout.c", "src/core/vmu_native_live.c",
    "src/core/vmu_native_lifetime.c", "src/core/vmu_native_profile.c", "src/core/vmu_native_discovery.c",
    "src/core/vmu_native_patch.c", "src/core/hash.c", "src/core/data.c", "src/core/vmu_durable.c",
    "src/core/vmu_store.c", "src/core/vmu_emulation.c", "src/core/retail_image.c",
    "src/loader/vmu_readonly_overlay.c", "src/loader/vmu_volatile_chain.c", "src/loader/sd_reader.c",
    "src/loader/vmu_backing.c", "src/dreamcast/sci_sd_storage.c"]
FLAGS = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
    "-ffunction-sections", "-fdata-sections", "-fsanitize=address,undefined",
    "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-Iinclude", "-Isrc/loader",
    "-DKUI_VMU_EXPERIMENTAL=1"]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(command, **kwargs):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, **kwargs)
    if result.returncode:
        raise RuntimeError("Command failed: " + shlex.join(command) + "\n" + result.stdout)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    original = args.original.resolve()
    source_sha = digest(original)
    cc = shlex.split(os.environ.get("CC", "cc"))
    dependencies = invoke(cc + ["-MM", "-Iinclude", "-Isrc/loader"] + SOURCES).replace("\\\n", " ")
    paths = {Path(__file__).resolve()}
    for line in dependencies.splitlines():
        paths.update((ROOT / value).resolve() for value in shlex.split(line.split(":", 1)[1]))
    before = {str(path.relative_to(ROOT)): digest(path) for path in sorted(paths)}
    assert "src/loader/vmu_native_controller_shutdown.inc" in before
    rows = []
    with tempfile.TemporaryDirectory(prefix="kui-controller-shutdown-") as temporary:
        def one(profile_slot):
            profile, slot = profile_slot
            executable = Path(temporary) / (profile + "-" + slot)
            flags = (["-DKUI_SHUTDOWN_SPLIT_FIXTURE=1", "-DKUI_VMU_NATIVE_CACHED_CODE=1",
                "-DKUI_NATIVE_SPLIT_CONTROLLER_ISOLATION=1", "-DKUI_NATIVE_SPLIT_CONTROLLER_GENERAL=1",
                "-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1", "-DKUI_NATIVE_VMU_ONLY=1",
                "-DKUI_NATIVE_VMU_LITE=1"] if profile == "captured" else [])
            flags += (["-DKUI_VMU_VIRTUAL_A1=1"] if slot == "a1" else [])
            command = cc + FLAGS + flags + SOURCES + ["-Wl,--gc-sections", "-o", str(executable)]
            invoke(command)
            output = invoke([str(executable), str(original)],
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
            counts = re.search(r"Controller shutdown: (\d+) checks; (\d+) original source-role refusals, "
                r"(\d+) cancellation boundary refusals, (\d+) cancellations;", output)
            assert counts, output
            checks, sources, boundaries, cancels = map(int, counts.groups())
            assert sources >= 1703 and boundaries >= 184
            assert cancels == (4 if profile == "captured" else 2) + (32 - 0xffff130f.bit_count()) + 3 + 12
            return {"profile": profile, "slot": slot, "passed": True, "checks": checks,
                "source_role_refusals": sources, "boundary_refusals": boundaries,
                "cancellations": cancels, "output": output.strip(), "compile_argv": command[:-1] + ["<temporary-executable>"],
                "test_binary_sha256": digest(executable), "no_publication": True}
        with ThreadPoolExecutor(max_workers=2) as pool:
            rows = list(pool.map(one, [(p, s) for p in ("cooperative", "captured") for s in ("a2", "a1")]))
    after = {str(path.relative_to(ROOT)): digest(path) for path in sorted(paths)}
    assert before == after and digest(original) == source_sha
    report = {"schema": "kui.native.controller-shutdown.host-proof.v1", "passed": True,
        "original_bytes": original.stat().st_size, "original_sha256": source_sha,
        "proof_source_sha256": before, "source_stable_before_after": True, "sanitizers": ["address", "undefined"],
        "cases": rows, "scope": ["Unmodified supplied original SDK source contract and both complete timer helper roles.",
            "Actual persistent/controller C over synthetic independently initialized live SDK/heap/RX/MMIO state.",
            "Full-context fixture uses original finish/timer code; only manager-cell literal is rebound to synthetic state.",
            "Cooperative profile proves no reader-region lookup on cancellation; captured profile executes actual split entry and deferred preparation.",
            "Both virtual slots and TX banks; normal publication resumes only after SDK hardware restoration.",
            "Distinct stale-P1/pristine and fresh-P2/damaged views refuse source changes; write-only MDAPRO is never read.",
            "MSYS reserved-bit variations are accepted, every meaningful field bit remains checked."],
        "limits": ["No console timing, SH linked size/stack, secondary executable loading or whole-game compatibility claim.",
            "This host proof does not replace a later artifact-bound actual-SH audit or source/lease revalidation after secondary loading."]}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    for row in rows:
        print(row["profile"], row["slot"], row["output"])
    if args.report:
        print("report:", args.report, "sha256:", digest(args.report))


if __name__ == "__main__":
    main()
