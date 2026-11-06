#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the actual private firmware-exit proof with independent fixtures.

--original-manifest optionally adds the five privately available originals.
Only identity metadata and results are written; executable bytes stay private.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["tests/test_vmu_native_firmware_exit.c", "src/core/vmu_native_program.c",
           "src/core/vmu_native_heap.c", "src/core/vmu_native_patch.c", "src/core/data.c",
           "src/core/vmu_native_discovery.c", "src/core/hash.c"]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, **kwargs):
    result = subprocess.run(command, cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    if result.returncode:
        print(result.stdout, end="")
        result.check_returncode()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original-manifest", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    cc = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Iinclude",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    deps = run(cc + ["-MM", "-Iinclude"] + SOURCES).stdout.replace("\\\n", " ")
    dependency_paths = {Path(__file__).resolve()}
    for line in deps.splitlines():
        dependency_paths.update((ROOT / p).resolve() for p in shlex.split(line.split(":", 1)[1]))
    before = {str(p.relative_to(ROOT)): sha(p) for p in sorted(dependency_paths)}
    assert "src/core/vmu_native_firmware_exit.inc" in before
    originals = []
    fixture_args = []
    if args.original_manifest:
        manifest = json.loads(args.original_manifest.read_text())
        for title, expected in [("GENERATOR", -1), ("MDK", -1), ("QUAKE", 1),
                                ("SMASH", 1), ("TOY", 1)]:
            row, = [r for r in manifest["files"] if title in r["title"].upper()]
            path = Path(row["startup"])
            assert path.stat().st_size == row["source_bytes"] and sha(path) == row["source_sha256"]
            originals.append({"title": row["title"], "source_bytes": row["source_bytes"],
                              "source_sha256": row["source_sha256"], "expected_result": expected})
            fixture_args.extend([str(path), str(expected)])
    with tempfile.TemporaryDirectory(prefix="firmware-exit-") as temporary:
        executable = Path(temporary) / "test-firmware-exit"
        command = cc + flags + SOURCES + ["-o", str(executable)]
        run(command)
        result = run([str(executable)] + fixture_args,
                     env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
        binary_sha = sha(executable)
    after = {str(p.relative_to(ROOT)): sha(p) for p in sorted(dependency_paths)}
    assert before == after, "Proof source changed during the run"
    if args.original_manifest:
        for index, row in enumerate(originals):
            assert sha(fixture_args[2 * index]) == row["source_sha256"], "Original changed during run"
    checks = int(re.search(r"firmware exit: (\d+) checks passed", result.stdout)[1])
    report = {"schema": "kui.native-firmware-exit-proof.v1", "passed": True,
              "checks": checks, "sanitizers": ["address", "undefined"],
              "source_sha256": before, "test_binary_sha256": binary_sha,
              "compile_argv": command[:-1] + ["<temporary-test-executable>"],
              "originals": originals,
              "refusals": ["unaccounted firmware consumers", "malformed or duplicate helper",
                           "all helper body/pool single-bit mutations", "outside body/pool entries",
                           "partial and address transports", "unproved formatter data roles",
                           "PVR source-pool entries and outside reads"],
              "scope": "Encoded-control and immutable literal-role proof; no console compatibility claim."}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(result.stdout, end="")
    if args.report:
        print("report:", args.report, "sha256:", sha(args.report))


if __name__ == "__main__":
    main()
