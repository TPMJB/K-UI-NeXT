#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare actual Toy workers using an asynchronous physical PCM consumer.

Synthetic timing is calibrated to one hardware summary, not a reconstruction
of that hardware run. No game audio or other game media is read by this tool.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


BASELINE = "56df22966bbb9093374372f8dad7a2e09a05fcb7"
CADENCES = (26042, 30000, 32552)
SPACINGS = (7813, 13021)
GAP_PHASES = (0, 7, 15, 31)
PUBLICATION_PHASES = (0, 2604, 5208)


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def freeze_baseline(repo, destination):
    paths = git(repo, "ls-tree", "-r", "--name-only", BASELINE, "include").decode().splitlines()
    paths += ["src/loader/toy_pilot_worker.c", "src/core/toy_pilot.c", "src/core/hash.c"]
    for relative in paths:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(git(repo, "show", f"{BASELINE}:{relative}"))


def identity(source_root, harness_root, worker_source=None):
    paths = sorted((source_root / "include").rglob("*.h"))
    paths += [source_root / "src/loader/toy_pilot_worker.c",
              source_root / "src/core/toy_pilot.c", source_root / "src/core/hash.c",
              harness_root / "tests/toy_pilot_async_fixture.h",
              harness_root / "tests/test_toy_pilot_cadence.c"]
    result = {}
    if worker_source is not None:
        result["worker_override:" + str(worker_source)] = hashlib.sha256(worker_source.read_bytes()).hexdigest()
    for path in paths:
        root = source_root if path.is_relative_to(source_root) else harness_root
        result[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def compile_worker(source_root, harness_root, output, sanitize, worker_source=None):
    before = identity(source_root, harness_root, worker_source)
    worker_source = worker_source or source_root / "src/loader/toy_pilot_worker.c"
    command = ["cc", "-std=c11", "-O1" if sanitize else "-O2", "-g", "-Wall", "-Wextra",
               "-Werror", "-Wpedantic", "-Wno-unused-function", "-fno-pie", "-no-pie",
               "-ffunction-sections", "-fdata-sections", "-I" + str(source_root / "include"),
               "-I" + str(source_root / "src/loader"),
               '-DTOY_WORKER_SOURCE="' + str(worker_source) + '"',
               str(harness_root / "tests/test_toy_pilot_cadence.c"),
               str(source_root / "src/core/toy_pilot.c"), str(source_root / "src/core/hash.c"),
               "-Wl,--gc-sections", "-o", str(output)]
    if sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    after = identity(source_root, harness_root, worker_source if worker_source != source_root / "src/loader/toy_pilot_worker.c" else None)
    if before != after:
        raise RuntimeError("Source changed during compilation; rerun after edits finish")
    return {"sources": before, "binary_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
            "compile_argv": command}


def run_profiles(binary, cadences, label, log):
    results = {}
    for cadence in cadences:
        for spacing in SPACINGS:
            for gap_phase in GAP_PHASES:
                for publication_phase in PUBLICATION_PHASES:
                    args = (cadence, spacing, 66732, gap_phase, publication_phase, 32, 1, 0, 0)
                    run = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True)
                    record = {"kind": "profile", "variant": label, "args": args,
                              "exit_code": run.returncode, "stdout": run.stdout, "stderr": run.stderr}
                    log.write(json.dumps(record) + "\n")
                    log.flush()
                    results[(cadence, spacing, gap_phase, publication_phase)] = record
    return results


def summarize(results, baseline):
    counts = collections.Counter()
    for key, result in results.items():
        if result["exit_code"] == 0:
            counts[key[:2]] += 1
    common = results.keys() & baseline.keys()
    lost = [key for key in sorted(common)
            if baseline[key]["exit_code"] == 0 and results[key]["exit_code"] != 0]
    gained = [key for key in sorted(common)
              if baseline[key]["exit_code"] != 0 and results[key]["exit_code"] == 0]
    return {"cells": {f"{cadence}/{spacing}": counts[(cadence, spacing)]
                      for cadence, spacing in sorted({key[:2] for key in results})},
            "total": sum(counts.values()), "profiles": len(results),
            "lost": lost, "gained": gained,
            "assertion_failures": sum(r["exit_code"] not in (0, 1) for r in results.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline-only", action="store_true")
    parser.add_argument("--characterize", action="store_true", help="Also sweep 22.3, 20.8, and 20 Hz")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--worker-source", type=Path, help="Scratch candidate worker override")
    args = parser.parse_args()
    repo = args.repo.resolve()
    output = (args.output or Path(tempfile.mkdtemp(prefix="toy-cadence-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    baseline_source = output / "baseline-source"
    harness_source = output / "harness-source"
    (harness_source / "tests").mkdir(parents=True, exist_ok=True)
    for relative in ("tests/toy_pilot_async_fixture.h", "tests/test_toy_pilot_cadence.c"):
        (harness_source / relative).write_bytes((repo / relative).read_bytes())
    freeze_baseline(repo, baseline_source)
    cadences = CADENCES + ((35000, 37500, 39063) if args.characterize else ())
    log_path = output / "profiles.jsonl"
    with log_path.open("w") as log:
        metadata = compile_worker(baseline_source, harness_source, output / "baseline", args.sanitizers)
        log.write(json.dumps({"kind": "identity", "variant": "baseline", "revision": BASELINE, **metadata}) + "\n")
        baseline = run_profiles(output / "baseline", cadences, "baseline", log)
        summary = {"baseline": summarize(baseline, baseline)}
        print(json.dumps({"variant": "baseline", **summary["baseline"]}), flush=True)
        if not args.baseline_only:
            metadata = compile_worker(repo, harness_source, output / "candidate", args.sanitizers, args.worker_source.resolve() if args.worker_source else None)
            log.write(json.dumps({"kind": "identity", "variant": "candidate", **metadata}) + "\n")
            candidate = run_profiles(output / "candidate", cadences, "candidate", log)
            summary["candidate"] = summarize(candidate, baseline)
            print(json.dumps({"variant": "candidate", **summary["candidate"]}), flush=True)
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"Results: {log_path}")
    if not args.baseline_only and (summary["candidate"]["lost"] or summary["candidate"]["assertion_failures"]):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
