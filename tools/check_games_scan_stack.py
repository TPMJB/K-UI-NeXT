#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guard the SH-4 Games scan/list/identity chains on the 64 KiB I/O worker.

This is a scoped regression bound, not a proof of all KOS/libc or worker jobs.
Actual production recipes are replayed into separate audit objects with only
GCC's stack/callgraph reporting added. Known image/list/progress callbacks are
modeled; 12 KiB remains reserved for unmodeled descendants and thread context.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ("src/dreamcast/main.c", "src/apps/games.c", "src/apps/games_library.c",
           "src/apps/games_identity.c", "src/apps/games_covers.c", "src/core/game_image.c",
           "src/core/game_metadata.c", "src/core/games_group.c", "src/core/hash.c")
ENTRIES = ("kui_games_library_scan", "kui_games_scan", "kui_games_library_rows_query",
           "kui_games_library_find_query", "kui_games_library_jump_query", "kui_games_library_verify_identity")
WORKER = "src/dreamcast/main.c:worker"
WALK = "src/apps/games_covers.c:walk"
WALK_CYCLE = {WALK, "src/apps/games_covers.c:walk_entry", "kui_games_visit_images"}
HEADROOM = 12 * 1024
NODE = re.compile(r'node: \{ title: "([^"\n]+)" label: "([^"\n]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"\n]+)" targetname: "([^"\n]+)"')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_name(path):
    for source in SOURCES:
        if path == source or path.endswith("/" + source):
            return source
    return str(Path(path).relative_to(ROOT)) if Path(path).is_relative_to(ROOT) else path


def name(title):
    if ":" in title:
        path, function = title.rsplit(":", 1)
        return source_name(path) + ":" + function
    return title


def artifact(directory, source, suffix):
    return directory / (source.replace("/", "_") + suffix)


def load(directory):
    frames, calls, owners, qualifications = {}, {}, {}, {}
    for source in SOURCES:
        report = artifact(directory, source, ".ci")
        usage = artifact(directory, source, ".su")
        require(report.is_file() and usage.is_file(), "Missing compiler evidence for " + source)
        su = {}
        for line in usage.read_text().splitlines():
            location, size, kind = line.split("\t")
            path, _, _, function = location.rsplit(":", 3)
            su[function] = int(size), kind
        for title, label in NODE.findall(report.read_text()):
            parts = label.split(r"\n")
            if len(parts) < 3:
                continue  # external declaration, not an emitted frame
            match = re.fullmatch(r"(\d+) bytes \(([^)]+)\)", parts[-1])
            if not match:
                continue
            function, kind = name(title), match[2]
            path = parts[1].rsplit(":", 2)[0]
            require(source_name(path) == source or path.endswith(".h"), "Callgraph frame source mismatch")
            value = int(match[1])
            require(su.get(parts[0]) == (value, kind), "GCC .ci/.su disagree: " + function)
            require(function not in frames or ".h:" in function, "Duplicate emitted function: " + function)
            frames[function], owners[function], qualifications[function] = max(value, frames.get(function, 0)), source, kind
        for caller, callee in EDGE.findall(report.read_text()):
            calls.setdefault(name(caller), set()).add(name(callee))
    return frames, calls, owners, qualifications


def callbacks(frames):
    result = {}
    image = tuple(f"src/apps/{unit}.c:{function}" for unit, functions in (
        ("games", ("image_stat", "image_read")), ("games_covers", ("image_stat", "image_read")),
        ("games_identity", ("stat_file", "read_file"))) for function in functions)
    metadata = ("src/apps/games.c:metadata_sector", "src/apps/games.c:metadata_range",
                "src/apps/games_identity.c:metadata_read", "src/apps/games_identity.c:metadata_range",
                "src/apps/games_covers.c:metadata_sector", "src/apps/games_covers.c:metadata_range")
    generic = ("kui_log", "kui_cancelled")
    for function in (*image, *metadata, *generic, "src/dreamcast/main.c:games_scan_progress",
                     "src/apps/games_covers.c:page_view", "src/apps/games_covers.c:walk_entry"):
        require(function in frames, "Required callback frame missing; update the scoped binding: " + function)
    result["image file stat/read"] = image
    result["metadata sector/range"] = metadata
    result["log/cancel"] = generic
    return result


def analyze(graph, budget=65536, scan_depth=4):
    frames, original, owners, kinds = graph
    require(budget == 65536, "Actual I/O worker budget changed; review the 64 KiB contract")
    require(1 <= scan_depth <= 16, "Review changed Games directory depth")
    bindings = callbacks(frames)
    calls = {key: set(value) for key, value in original.items()}
    for caller, children in calls.items():
        if "__indirect_call" not in children:
            continue
        children.remove("__indirect_call")
        children.update(bindings["log/cancel"])
        if owners.get(caller) == "src/core/game_image.c":
            children.update(bindings["image file stat/read"])
        if owners.get(caller) == "src/core/game_metadata.c":
            children.update(bindings["metadata sector/range"])
        if caller == "kui_games_visit_images":
            children.add("src/apps/games_covers.c:walk_entry")
        if caller == "src/apps/games.c:list_with":
            children.add("src/apps/games_covers.c:page_view")
        if caller == "kui_games_scan":
            children.add("src/dreamcast/main.c:games_scan_progress")

    def reaches(start, target, seen=None):
        if start == target:
            return True
        seen = set() if seen is None else seen
        if start in seen:
            return False
        seen.add(start)
        return any(reaches(child, target, seen) for child in calls.get(start, ()) if child in frames)

    for entry in ENTRIES:
        require(entry in frames and reaches(WORKER, entry), "Required worker entry/edge missing: " + entry)
    for start, end in (("kui_games_library_scan", "kui_games_identity_group"),
                       ("kui_games_library_scan", "kui_games_list_rows"),
                       ("kui_games_library_scan", "kui_games_prefetch_titles"),
                       ("kui_games_scan", "kui_game_image_open_named_codec"),
                       ("kui_games_identity_group", "kui_game_image_open_named_codec")):
        require(reaches(start, end), "Required Scan chain missing; update audited scope: " + start + " -> " + end)
    boundaries = set()

    memo = {}
    def longest(function, active=()):
        if function not in frames:
            boundaries.add(function)
            return 0, []
        # GCC can inline walk() into walk_entry(). Count the emitted visitor
        # rounds, which remain present in both compiler shapes.
        if function == "kui_games_visit_images" and active.count(function) >= scan_depth:
            return 0, []
        require(function not in active or function in WALK_CYCLE, "Unbounded recursive path: " + function)
        require(kinds[function] == "static", "Dynamic stack frame: " + function)
        key = function, active.count("kui_games_visit_images")
        if key in memo:
            return memo[key]
        options = [longest(child, active + (function,)) for child in sorted(calls.get(function, ()))]
        extra, path = max(options, key=lambda value: value[0], default=(0, []))
        memo[key] = frames[function] + extra, [function] + path
        return memo[key]

    # Only main's ancestors leading to the selected jobs belong in this check.
    # Unrelated worker branches are not interpreted as simultaneous Scan calls.
    def prefix(function, target, active=()):
        if function == target:
            return 0, []
        if function not in frames or function in active or owners[function] != SOURCES[0]:
            return None
        require(kinds[function] == "static", "Dynamic worker ancestor: " + function)
        options = [value for child in calls.get(function, ())
                   if (value := prefix(child, target, active + (function,))) is not None]
        if not options:
            return None
        size, path = max(options, key=lambda value: value[0])
        return frames[function] + size, [function] + path

    results = []
    for entry in ENTRIES:
        before = prefix(WORKER, entry)
        require(before is not None, "Required main-to-job chain changed: " + entry)
        amount, path = longest(entry)
        total = before[0] + amount
        results.append({"entry": entry, "modeled_bytes": total, "reserved_bytes": HEADROOM,
                        "remaining_bytes": budget - total - HEADROOM,
                        "path": [{"function": function, "bytes": frames[function]} for function in before[1] + path]})
    worst = max(results, key=lambda value: value["modeled_bytes"])
    require(worst["remaining_bytes"] >= 0,
            f"Scan stack exceeds worker budget: {worst['modeled_bytes']} modeled + {HEADROOM} reserved > {budget}: " +
            " -> ".join(item["function"] for item in worst["path"]))
    return {"worker_bytes": budget, "reserved_unmodeled_bytes": HEADROOM, "scan_depth": scan_depth,
            "callbacks": bindings, "bounded_recursive_cycle": sorted(WALK_CYCLE), "chains": results,
            "unmodeled_boundaries": sorted(boundaries),
            "scope": "Games scan/list/identity compiler-generated C paths; not all worker jobs or a complete KOS/libc stack proof"}


def build(directory):
    directory.mkdir(parents=True, exist_ok=True)
    commands, dependencies = [], set()
    for source in SOURCES:
        target = "build/dc/main-runtime.o" if source == SOURCES[0] else "build/dc/" + source[:-2] + ".o"
        dry = subprocess.check_output(["make", "--no-print-directory", "-f", "Makefile.dc", "-n", "-B", target], cwd=ROOT, text=True)
        candidates = [shlex.split(line) for line in dry.splitlines() if line.startswith("kos-cc ")]
        matches = [command for command in candidates if "-c" in command and command[command.index("-c") + 1] == source]
        require(len(matches) == 1, "Cannot identify production compile recipe: " + source)
        command = matches[0]
        command[command.index("-o") + 1] = str(artifact(directory, source, ".o"))
        command += ["-fstack-usage", "-fcallgraph-info=su"]
        subprocess.run(command, cwd=ROOT, check=True)
        commands.append(command)
        dependency = artifact(directory, source, ".d").read_text().replace("\\\n", " ")
        dependencies.update(Path(item) for item in shlex.split(dependency.partition(":")[2]) if item and not item.endswith(":"))
    # Read the same constant used by thd_create_ex, compiled for the target.
    probe = directory / "budget.c"
    probe.write_text('#include "kui/worker_stack.h"\n#include "kui/games_covers.h"\nconst unsigned kui_scan_budget[] __attribute__((section(".kui_scan_budget")))={KUI_IO_WORKER_STACK_BYTES,KUI_GAMES_SCAN_DEPTH};\n')
    command = ["kos-cc", "-Iinclude", "-I.deps/fatfs/source", "-c", str(probe), "-o", str(directory / "budget.o")]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run(["sh-elf-objcopy", "-O", "binary", "--only-section=.kui_scan_budget", str(directory / "budget.o"), str(directory / "budget.bin")], cwd=ROOT, check=True)
    dependencies.update((Path("include/kui/worker_stack.h"), Path("Makefile.dc"), Path("tools/check_games_scan_stack.py")))
    provenance = {"compiler": subprocess.check_output(["sh-elf-gcc", "--version"], text=True).splitlines()[0],
                  "target": subprocess.check_output(["sh-elf-gcc", "-dumpmachine"], text=True).strip(),
                  "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                  "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
                  "compiler_driver_expansion": subprocess.check_output(
                      ["kos-cc", "-###", "-c", "-x", "c", "/dev/null"], stderr=subprocess.STDOUT, text=True),
                  "commands": commands, "budget_command": command,
                  "inputs": {str(path): digest(ROOT / path) for path in sorted(dependencies)}}
    (directory / "build.json").write_text(json.dumps(provenance, indent=2) + "\n")


def check(directory):
    provenance = json.loads((directory / "build.json").read_text())
    require(provenance["target"] == "sh-elf", "Stack evidence is not from the SH target")
    for path, expected in provenance["inputs"].items():
        require(digest(ROOT / path) == expected, "Stale stack evidence; rebuild: " + path)
    source = (ROOT / SOURCES[0]).read_text()
    require(re.search(r"\.stack_size\s*=\s*KUI_IO_WORKER_STACK_BYTES\b", source), "Worker allocation must use the shared budget")
    covers = (ROOT / "src/apps/games_covers.c").read_text()
    require(re.search(r"job->depth\s*\+\s*1u?\s*<\s*KUI_GAMES_SCAN_DEPTH", covers), "Review changed directory recursion bound")
    budget_bytes = (directory / "budget.bin").read_bytes()
    require(len(budget_bytes) == 8, "Invalid target-compiled budget constants")
    result = analyze(load(directory), *struct.unpack("<II", budget_bytes))
    result["compiler"] = provenance["compiler"]
    result["target"] = provenance["target"]
    result["source_commit"] = provenance["source_commit"]
    result["source_dirty"] = provenance["source_dirty"]
    result["compiler_driver_expansion"] = provenance["compiler_driver_expansion"]
    result["compile_commands"] = provenance["commands"]
    result["source_inputs_sha256"] = provenance["inputs"]
    (directory / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/games-scan-stack")
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args()
    try:
        directory = args.directory.resolve()
        if args.build:
            build(directory)
        result = check(directory)
        for chain in result["chains"]:
            print(f"{chain['entry']}: {chain['modeled_bytes']} modeled + {HEADROOM} reserved / {result['worker_bytes']}; {chain['remaining_bytes']} bytes remain")
        print("PASS scoped SH-4 Games stack regression; " + str(directory / "report.json"))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
