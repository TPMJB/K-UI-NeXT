#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Worst-case private-stack depth of the background reader's resident.

That resident is linked with -fcallgraph-info=su: each LTO partition's .ci
file lists every emitted function with its static frame and its calls, with
indirect calls marked. The entries that run on the private stack are walked
to their deepest path: GD calls (kui_retail_resident_dispatch; the hook's own
frame is on the caller's stack), the stream interrupt (kui_retail_async_irq,
after the assembly entry pushes 44 bytes) and the menu return. Indirect calls
reach only the resident's callbacks (bus transfer/select, GD map/check, the
cursor's writer), so each counts as the deepest its source file can reach
(INDIRECT); the menu return's
jump to the boot ROM never comes back. Recursion, a dynamic or missing frame,
a missing callback or an indirect call from elsewhere fails the check.
"""
from pathlib import Path
import re
import sys

ROOTS = {"kui_retail_resident_dispatch": 0, "kui_retail_async_irq": 44,
         "kui_retail_menu_return": 0}
# Each source file's indirect calls and the callbacks they can reach.
INDIRECT = {
    "retail_gd.c": ("map_guest", "check_sectors"),   # ops.map, ops.check
    "sci_stream.c": ("transfer", "select_card"),     # bus.transfer, bus.select
    "retail_cursor.c": ("write_out",),               # the cursor's writer
    "retail_async.c": ("map_guest",),                # ops.map
}
CALLBACKS = tuple(sorted({c for targets in INDIRECT.values() for c in targets}))
REBOOT = ("kui_retail_menu_return", "retail_resident.c")
# Covers the compiler's library calls, which the call graph does not list:
# the resident links only __udivsi3, which pushes PR (4 bytes).
MARGIN = 64

NODE = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)" label: "([^"]*)"')


def name(title):
    return title if title.startswith("__") else title.rsplit(":", 1)[-1]


def base(function):
    return function.split(".", 1)[0]


def load(reports):
    frames, calls = {}, {}
    for report in reports:
        text = report.read_text()
        for title, label in NODE.findall(text):
            function = name(title)
            if function == "__indirect_call":
                continue
            found = re.search(r"\\n(\d+) bytes \((\w+)\)", label)
            if not found:
                continue  # an external (e.g. libgcc) function: checked below if called
            if found.group(2) != "static":
                raise ValueError(f"Dynamic stack frame: {function}")
            frames[function] = max(frames.get(function, 0), int(found.group(1)))
        for source, target, label in EDGE.findall(text):
            calls.setdefault(name(source), []).append((name(target), label.rsplit("/", 1)[-1]))
    return frames, calls


# Leaf library helpers the compiler calls; their frames are small and fixed.
LIBRARY = {"__udivsi3": 0, "__sdivsi3": 0, "__udivsi3_i4i": 0, "__sdivsi3_i4i": 0,
           "memcpy": 0, "memset": 0}


def worst(frames, calls):
    callbacks = {}
    for callback in CALLBACKS:
        found = [f for f in frames if base(f) == callback]
        if not found:
            raise ValueError(f"Missing callback frame: {callback}")
        callbacks[callback] = found
    memo = {}

    def depth(function, path):
        if function in path:
            raise ValueError("Recursion: " + " -> ".join(path + (function,)))
        if function in memo:
            return memo[function]
        if function not in frames:
            if base(function) in LIBRARY:
                return (LIBRARY[base(function)], [function])
            # Identical code folding leaves a merged clone (e.g. a second
            # reverse.lto_priv.N) as an alias without a node of its own:
            # count the deepest emitted function of that name instead.
            twins = [f for f in frames if base(f) == base(function)]
            if not twins:
                raise ValueError(f"Call to a function without a frame: {function}")
            return max(depth(twin, path) for twin in twins)
        best = (0, [])
        for target, label in calls.get(function, []):
            if target == "__indirect_call":
                source_file = label.split(":", 1)[0]
                if (base(function), source_file) == REBOOT:
                    continue
                if source_file not in INDIRECT:
                    raise ValueError(f"Unexpected indirect call in {function} at {label}")
                targets = [f for c in INDIRECT[source_file] for f in callbacks[c]]
            else:
                targets = [target]
            for callee in targets:
                found = depth(callee, path + (function,))
                if found[0] > best[0]:
                    best = found
        memo[function] = (frames[function] + best[0], [f"{function}({frames[function]})"] + best[1])
        return memo[function]

    result = {}
    for root, entry in ROOTS.items():
        roots = [f for f in frames if base(f) == root]
        if len(roots) != 1:
            raise ValueError(f"Missing or duplicated stack root: {root}")
        total, chain = depth(roots[0], ())
        result[root] = {"bytes": total + entry, "path": " -> ".join(chain)}
    return result


def check(directory, available):
    reports = sorted(Path(directory).glob("lto/*.ci"))
    if not reports:
        raise ValueError("Missing compiler call-graph reports")
    frames, calls = load(reports)
    result = worst(frames, calls)
    deepest = max(entry["bytes"] for entry in result.values())
    if deepest + MARGIN > available:
        raise ValueError(f"Resident worst-case stack {deepest} + {MARGIN} exceeds {available}")
    return {"worst_bytes": deepest, "margin": MARGIN, "available_bytes": available,
            "paths": result}


def main():
    import json
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/retail/scia")
    available = int(sys.argv[2], 0) if len(sys.argv) > 2 else 352 - 48
    print(json.dumps(check(directory, available), indent=1))


if __name__ == "__main__":
    main()
