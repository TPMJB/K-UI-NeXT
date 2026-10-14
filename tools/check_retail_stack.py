#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Worst-case private-stack depth of the background reader's resident.

That resident is linked with -fcallgraph-info=su: each LTO partition's .ci
file lists every emitted function with its static frame and its calls, with
indirect calls marked. The entries that run on the private stack are walked
to their deepest path: GD calls (kui_retail_resident_dispatch; the hook's own
frame is on the caller's stack), the stream interrupt (kui_retail_async_irq,
after the assembly entry pushes 44 bytes; in the Windows CE build
kui_retail_ce_irq, after kui_retail_ce_isr pushes 28) and the menu return.
Indirect calls reach only the resident's callbacks (bus transfer/select, GD
map/check, the cursor's writer and, under CE, the PIO stream's read_part),
so each counts as the deepest its source file can reach (INDIRECT); the menu
return's
jump to the boot ROM never comes back. Recursion, a dynamic or missing frame,
a missing callback or an indirect call from elsewhere fails the check.
"""
from pathlib import Path
import re
import sys

ROOTS = {"kui_retail_resident_dispatch": 0, "kui_retail_async_irq": 44,
         "kui_retail_menu_return": 0}
CE_ROOTS = {"kui_retail_resident_dispatch": 0, "kui_retail_ce_irq": 28,
            "kui_retail_menu_return": 0}
# Each source file's indirect calls and the callbacks they can reach.
INDIRECT = {
    "retail_gd.c": ("map_guest", "check_sectors"),   # ops.map, ops.check
    "sci_stream.c": ("transfer", "select_card"),     # bus.transfer, bus.select
    "retail_cursor.c": ("write_out",),               # the cursor's writer
    "retail_async.c": ("map_guest",),                # ops.map
}
CE_INDIRECT = dict(INDIRECT, **{
    "retail_gd.c": ("map_guest", "check_sectors", "kui_retail_async_read_part"),  # + read_part
})
REBOOT = ("kui_retail_menu_return", "retail_resident.c")
# Covers the compiler's library calls, which the call graph does not list:
# the resident links only __udivsi3, which pushes PR (4 bytes).
MARGIN = 64

NODE = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)" label: "([^"]*)"')


def name(title):
    return title if title.startswith("__") else title.rsplit(":", 1)[-1]


def base(function):
    return name(function).split(".", 1)[0]


def load(reports, *, qualified=False, scope=None):
    frames, calls = {}, {}
    # The isolated excerpt links separate non-LTO C units. Keep GCC's
    # source-qualified local names there instead of merging equal locals
    # across units. Ordinary residents retain their existing graph format.
    def graph_name(title):
        result = title if qualified else name(title)
        return scope + ":" + result if scope and result != "__indirect_call" else result
    for report in reports:
        text = report.read_text()
        for title, label in NODE.findall(text):
            function = graph_name(title)
            if function == "__indirect_call":
                continue
            found = re.search(r"\\n(\d+) bytes \((\w+)\)", label)
            if not found:
                continue  # an external (e.g. libgcc) function: checked below if called
            if found.group(2) != "static":
                raise ValueError(f"Dynamic stack frame: {function}")
            frames[function] = max(frames.get(function, 0), int(found.group(1)))
        for source, target, label in EDGE.findall(text):
            calls.setdefault(graph_name(source), []).append((graph_name(target), label.rsplit("/", 1)[-1]))
    return frames, calls


# Leaf library helpers the compiler calls; their frames are small and fixed.
LIBRARY = {"__udivsi3": 0, "__sdivsi3": 0, "__udivsi3_i4i": 0, "__sdivsi3_i4i": 0,
           "memcpy": 0, "memset": 0}
# This authored leaf saves SR in r1, masks interrupts, performs one MOV,
# executes eight NOPs and restores SR. It never touches r15. The excerpt's
# linked-byte auditor separately pins every byte and the sole P2 caller.
EXCERPT_ASM_FRAMES = {"kui_armada_ce_excerpt_array_read": 0}
# These execute only after leaving the private GD stack. Include the
# integer-only native-owner dispatcher too: absence of FPU instructions is
# insufficient to make a kernel callback/native API safe on the GD stack.
EXCERPT_NATIVE_PATH = frozenset({
    "kui_armada_ce_excerpt_attempt", "kui_armada_ce_excerpt_ce_call",
    "kui_armada_ce_excerpt_deferred", "kui_armada_ce_excerpt_dispatch",
    "kui_armada_ce_excerpt_owner_callback", "kui_armada_ce_excerpt_owner_deferred",
    "revalidate",
})
STREAM_NATIVE_PATH = frozenset({
    "kui_armada_ce_stream_bind_deferred", "kui_armada_ce_stream_bind_native",
    "kui_armada_ce_stream_worker", "kui_armada_ce_stream_native_start",
    "kui_armada_ce_stream_native_reap", "kui_armada_ce_stream_native_ticks",
    "kui_armada_ce_stream_native_sleep", "kui_armada_ce_stream_native_write_begin",
    "kui_armada_ce_stream_native_write_end", "native_call", "native_publish",
    "kui_armada_ce_stream_owner_word", "mapped_word", "read_native",
    "kui_armada_ce_stream_rebind_owner", "kui_armada_ce_stream_rebind_word",
    "worker_mapped_read", "worker_native_read", "worker_address",
    "worker_mask_begin", "worker_mask_end", "worker_sr",
})
# The immutable resident bus descriptor is separately pinned in the linked
# audit. These indirect targets cross back to that low image only; they may
# never resolve to an equal-named controller helper or to a native worker.
STREAM_BUS_CALLBACKS = (
    "open_bus", "hook", "unhook", "stream_wake", "kui_sci_stream_busy",
    "kui_sci_stream_ready", "kui_sci_stream_poll", "kui_sci_stream_wait",
    "kui_sci_stream_fetch", "kui_sci_stream_take", "kui_sci_stream_discard",
    "kui_sci_stream_token_budget")
STREAM_BUS_INDIRECT = {
    ("finish", "armada_ce_stream_transport.c"):
        ("kui_sci_stream_busy", "kui_sci_stream_poll", "kui_sci_stream_wait"),
    ("release_audio", "armada_ce_stream_transport.c"):
        ("kui_sci_stream_discard", "stream_wake"),
    ("kui_armada_ce_stream_transport_pump", "armada_ce_stream_transport.c"):
        tuple(n for n in STREAM_BUS_CALLBACKS if n not in ("kui_sci_stream_wait", "kui_sci_stream_ready")),
    ("kui_armada_ce_stream_transport_event", "armada_ce_stream_transport.c"):
        ("kui_sci_stream_busy", "kui_sci_stream_take", "kui_sci_stream_token_budget", "unhook"),
}


def worst(frames, calls, ce=False, armada_bind=False, armada_excerpt=False,
          armada_stream=False, roots_override=None):
    roots, indirect = (CE_ROOTS, CE_INDIRECT) if ce else (ROOTS, INDIRECT)
    if roots_override is not None:
        roots = roots_override
    by_function = {}
    narrow_files = set()
    if armada_bind:
        if not ce:
            raise ValueError("Armada binding controller requires CE")
        indirect = dict(indirect, **{"armada_ce_ram_bind.h": ("read_ram",)})
        indirect["armada_ce_live_sound.h" if armada_excerpt else
                 "armada_ce_table_trace.h"] = ("read_ram",)
        if armada_excerpt:
            # The physical and mapped readers have different signatures and
            # different callees. A file-wide union here would invent a cycle
            # through live_mapped and conceal an unexpected callback site.
            by_function = {
                ("kui_armada_ce_live_physical", "armada_ce_live_sound.h"): ("read_ram",),
                ("kui_armada_ce_live_mapped", "armada_ce_live_sound.h"): ("read_mapped",),
                ("kui_armada_ce_live_word", "armada_ce_ram_bind.h"):
                    ("kui_armada_ce_live_physical", "kui_armada_ce_live_mapped"),
                ("kui_armada_ce_ram_bind_with_reader", "armada_ce_ram_bind.h"):
                    ("kui_armada_ce_live_physical", "kui_armada_ce_live_mapped"),
                ("read_mapped", "armada_ce_cache_word.h"): ("read_cache",),
                ("kui_armada_ce_cache_word", "armada_ce_cache_word.h"): ("read_cache",),
                ("read_cache", "armada_ce_excerpt_entry.c"):
                    ("kui_armada_ce_excerpt_array_read",),
            }
            narrow_files = {source for _, source in by_function}
            if armada_stream:
                by_function.update({key: tuple("resident:" + n for n in values)
                                    for key, values in STREAM_BUS_INDIRECT.items()})
                narrow_files.add("armada_ce_stream_transport.c")
    callbacks = {}
    required_callbacks = {c for targets in list(indirect.values()) + list(by_function.values())
                          for c in targets}
    if armada_stream:
        required_callbacks.add("excerpt:write_audio")
    for callback in sorted(required_callbacks):
        domain, _, target = callback.partition(":")
        found = ([f for f in frames if base(f) == target and
                  f.startswith("excerpt:") == (domain == "excerpt")]
                 if domain in ("resident", "excerpt") else
                 [f for f in frames if base(f) == callback])
        if not found:
            raise ValueError(f"Missing callback frame: {callback}")
        callbacks[callback] = found
    memo = {}

    def depth(function, path):
        if armada_excerpt and base(function) in (EXCERPT_NATIVE_PATH |
                                                 (STREAM_NATIVE_PATH if armada_stream else frozenset())):
            raise ValueError("Native excerpt path reached private GD stack: " +
                             " -> ".join(path + (function,)))
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
                selected = by_function.get((base(function), source_file))
                if (armada_stream and function.startswith("excerpt:") and
                        (base(function), source_file) == ("kui_retail_cursor_feed", "retail_cursor.c")):
                    selected = ("excerpt:write_audio",)
                if selected is None and source_file in narrow_files:
                    raise ValueError(f"Unexpected excerpt callback in {function} at {label}")
                if selected is None and source_file not in indirect:
                    raise ValueError(f"Unexpected indirect call in {function} at {label}")
                targets = [f for c in (selected if selected is not None else indirect[source_file])
                           for f in callbacks[c]]
            else:
                targets = [target]
            for callee in targets:
                found = depth(callee, path + (function,))
                if found[0] > best[0]:
                    best = found
        memo[function] = (frames[function] + best[0], [f"{function}({frames[function]})"] + best[1])
        return memo[function]

    result = {}
    for root, entry in roots.items():
        roots = [f for f in frames if base(f) == root]
        if len(roots) != 1:
            raise ValueError(f"Missing or duplicated stack root: {root}")
        total, chain = depth(roots[0], ())
        result[root] = {"bytes": total + entry, "path": " -> ".join(chain)}
    return result


def check(directory, available, ce=False):
    reports = sorted(Path(directory).glob("lto/*.ci"))
    if not reports:
        raise ValueError("Missing compiler call-graph reports")
    excerpt_report = Path(directory).parent / "excerpt.ci"
    armada_excerpt = excerpt_report.is_file()
    armada_stream = (Path(directory).parent / "stream-native.ci").is_file()
    if armada_excerpt and not ce:
        raise ValueError("Armada excerpt controller requires CE")
    frames, calls = load(reports, qualified=armada_excerpt)
    armada_bind = "kui_armada_ce_bind_probe" in {
        base(target) for edges in calls.values() for target, _ in edges}
    if armada_bind:
        if not ce:
            raise ValueError("Armada binding controller requires CE")
        probe_report=Path(directory).parent / "bind-probe.ci"
        if not probe_report.is_file():
            raise ValueError("Missing retained Armada controller call graph")
        extra_reports = [probe_report]
        if armada_excerpt:
            minic_report = Path(directory).parent / "excerpt-minic.ci"
            if not minic_report.is_file():
                raise ValueError("Missing retained excerpt minic call graph")
            extra_reports += [excerpt_report, minic_report]
            # The high Armada stream profile can add separate integer-only
            # model/transport/native units. Retain their real frames rather
            # than treating a cross-unit callback as an assumed leaf.
            extra_reports += sorted(Path(directory).parent.glob("stream-*.ci"))
            # Resident and controller are separately linked ELFs, each with
            # its own minic helpers. Only this one authored entry crosses
            # the image boundary; controller globals resolve in its image.
            calls = {source: [("excerpt:" + target if target ==
                               "kui_armada_ce_bind_probe" else target, label)
                              for target, label in edges]
                     for source, edges in calls.items()}
        for report in extra_reports:
            more_frames, more_calls=load([report], qualified=armada_excerpt,
                                        scope="excerpt" if armada_excerpt else None)
            if set(frames)&set(more_frames):
                raise ValueError("Armada controller call graph name collision")
            frames.update(more_frames); calls.update(more_calls)
        if armada_excerpt:
            for symbol, size in EXCERPT_ASM_FRAMES.items():
                key = "excerpt:" + symbol
                if any(base(frame) == symbol for frame in frames):
                    raise ValueError("Authored excerpt assembly leaf has a compiler frame")
                frames[key] = size
    elif armada_excerpt:
        raise ValueError("Excerpt graph has no admitted resident controller call")
    result = worst(frames, calls, ce, armada_bind, armada_excerpt, armada_stream)
    deepest = max(entry["bytes"] for entry in result.values())
    if deepest + MARGIN > available:
        raise ValueError(f"Resident worst-case stack {deepest} + {MARGIN} exceeds {available}")
    checked = {"worst_bytes": deepest, "margin": MARGIN, "available_bytes": available,
               "paths": result}
    if armada_excerpt:
        checked["excerpt_graph_qualified"] = True
        checked["stream_native_paths_excluded"] = armada_stream
    if armada_stream:
        # Worker storage visits the same controller transport under a short
        # CLI region on its native stack. Retain the real cross-image SCI
        # frames and apply the same native-call prohibition and 2KiB budget.
        worker = worst(frames, calls, ce, armada_bind, armada_excerpt, True,
                       roots_override={"kui_armada_ce_stream_transport_pump": 0,
                                       "kui_armada_ce_stream_transport_submit": 0,
                                       "kui_armada_ce_stream_transport_snapshot": 0,
                                       "kui_armada_ce_stream_transport_cancel": 0})
        worker_worst = max(entry["bytes"] for entry in worker.values())
        if worker_worst + MARGIN > available:
            raise ValueError("Stream masked worker transport exceeds the admitted private-stack budget")
        checked["stream_masked_worker_transport"] = {
            "worst_bytes": worker_worst, "margin": MARGIN, "available_bytes": available, "paths": worker}
    return checked


def main():
    import json
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/retail/scia")
    available = int(sys.argv[2], 0) if len(sys.argv) > 2 else 352 - 48
    print(json.dumps(check(directory, available), indent=1))


if __name__ == "__main__":
    main()
