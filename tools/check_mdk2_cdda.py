#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the isolated MDK2 CDDA images, embedded bytes, and authored stacks."""
import argparse
import json
from pathlib import Path
import struct

from check_loader_layout import inspect_elf, padded
from runtime_package import verify
from toy_pilot_symbols import _linked_symbols

LOW, LOW_LIMIT, LOW_STACK_END = 0x8C004000, 0x8C007800, 0x8C007D00
WORKER, WORKER_LIMIT = 0x8CFD0000, 0x8CFE0000
MAP_ADDRESS, MAP_LIMIT, MAP_BYTES = 0x8CFD8000, 0x8CFD9000, 4096
STAGE, HEAP_HEADER = 0x8CE00000, 0x8CFCFFE0
P2 = 0x20000000
EXPORTS = struct.Struct("<23I")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def binary_matches(directory, name, image):
    binary = (directory / (name + ".bin")).read_bytes()
    require(padded(binary) == padded(image["payload"]),
            name + ".bin differs from admitted ELF load bytes")
    return binary


def span(image, base, start, end):
    symbols = image["symbols"]
    begin, finish = symbols.get(start, 0), symbols.get(end, 0)
    require(base <= begin <= finish <= base + len(image["payload"]),
            "Invalid embedded span: " + start)
    return image["payload"][begin - base:finish - base]


def stack_records(paths):
    """Use the emitted, optimized frames rather than source local estimates."""
    paths = list(paths)
    require(bool(paths), "Missing emitted stack-usage records")
    result = []
    for path in paths:
        for row in path.read_text().splitlines():
            parts = row.split("\t")
            require(len(parts) == 3 and parts[1].isdigit() and parts[2] == "static",
                    "Unbounded or malformed stack record: " + str(path))
            result.append((parts[0].rsplit(":", 1)[-1], int(parts[1])))
    return result


def stack_bounds(directory):
    low = stack_records(directory.glob("resident-sci.elf.ltrans*.su"))
    worker = stack_records((directory / "worker").rglob("*.su"))
    lookup = {}
    for name, frame in worker:
        lookup[name] = max(lookup.get(name, 0), frame)
    # Reviewed synchronous adapter calls only these worker C functions from
    # the low GD stack. They do mailbox/projection work, with no SDK, card,
    # G2 operation, audio-stack selection, or recursive GD dispatch.
    mailbox = ("kui_toy_pilot_gd_dispatch", "kui_toy_pilot_gd_project",
               "kui_toy_pilot_request", "kui_toy_pilot_snapshot")
    require(all(name in lookup for name in mailbox),
            "Missing reviewed GD mailbox frame")
    # The graph for this fixed SHARED0/SCHEDULE0 profile is nonrecursive.
    # Summing every emitted low C frame deliberately exceeds every reviewed
    # normal path, including callbacks and the terminal display path. The
    # entry chooses stack_top-16; libgcc's reviewed __udivsi3 saves one PR.
    low_bound = 16 + 4 + sum(frame for _, frame in low) + sum(lookup[n] for n in mailbox)
    require(low_bound <= 1280 - 32, "Low GD stack has less than 32 bytes of margin")
    # All SDK original calls in the interposers run on the game's inherited
    # stack. Authored worker callbacks use only this worker and the admitted
    # low reader. The bridge claims against recursion and reserves a 16-byte
    # anchor. Summing both complete C graphs is a conservative bound; the
    # inspected libgcc right-shift helper uses no stack and divide uses 4B.
    private_bound = 16 + 4 + sum(frame for _, frame in low + worker)
    require(private_bound <= 8192 - 32, "Private worker stack has less than 32 bytes of margin")
    return {"method": "sum of every emitted frame in each reviewed nonrecursive authored graph",
            "low_gd_upper_bound": low_bound, "low_gd_capacity": 1280,
            "private_worker_upper_bound": private_bound, "private_worker_capacity": 8192}


def check_directory(directory):
    directory = Path(directory)
    low = inspect_elf((directory / "resident-sci.elf").read_bytes(),
                      LOW, LOW_LIMIT, private_p2=True)
    worker = inspect_elf((directory / "worker.elf").read_bytes(),
                         WORKER, WORKER_LIMIT, private_p2=True,
                         entry_symbol="_kui_toy_pilot_initialize", entry_at_base=False)
    stage = inspect_elf((directory / "stage.elf").read_bytes(), STAGE, HEAP_HEADER)
    entry = inspect_elf((directory / "entry.elf").read_bytes(), 0x8C010000, 0x8C112000)
    low_bytes = binary_matches(directory, "resident-sci", low)
    worker_bytes = binary_matches(directory, "worker", worker)
    stage_bytes = binary_matches(directory, "stage", stage)
    ls, ws, ss = low["symbols"], worker["symbols"], stage["symbols"]
    control = _linked_symbols((directory / "resident-sci.elf").read_bytes(),
                              {"_kui_toy_pilot_boot_control"}).get(
                                  "_kui_toy_pilot_boot_control", (0, 0, 0, None))
    control_address, control_bytes, control_kind, control_section = control
    require(control_kind == 1 and control_bytes == 56 and control_section is not None and
            control_section[1] == 8 and control_section[2] & 7 == 3 and
            control_address % 4 == 0 and
            control_section[3] <= control_address < control_address + control_bytes <=
            control_section[3] + control_section[5] and
            LOW + P2 <= control_address < control_address + control_bytes <= LOW_LIMIT + P2,
            "MDK2 low bootstrap control is not its protected 56-byte object")
    manifest_symbol = _linked_symbols((directory / "resident-sci.elf").read_bytes(),
                                      {"_mdk2_manifest"}).get("_mdk2_manifest", (0, 0, 0, None))
    manifest_address, manifest_bytes, manifest_kind, _ = manifest_symbol
    require(manifest_kind == 1 and manifest_bytes == 4 and
            LOW + P2 <= manifest_address < manifest_address + manifest_bytes <= LOW_LIMIT + P2,
            "MDK2 low reader does not own its protected manifest pointer")
    stage_manifest = _linked_symbols((directory / "stage.elf").read_bytes(),
                                     {"_manifest"}).get("_manifest", (0, 0, 0, None))
    source_address, source_bytes, source_kind, _ = stage_manifest
    require(source_kind == 1 and source_bytes == 252 + 256 * 12 and
            STAGE <= source_address < source_address + source_bytes <= stage["memory_end"] and
            source_bytes <= MAP_BYTES and MAP_LIMIT < WORKER_LIMIT,
            "MDK2 stage does not own its complete 256-slot transient map")
    require(ls.get("__retail_hook_stack_bottom") == LOW_LIMIT + P2 and
            ls.get("__retail_hook_stack") == LOW_STACK_END + P2 and
            LOW_STACK_END - LOW_LIMIT == 1280 and LOW_STACK_END <= 0x8C008000 and
            ls.get("__retail_resident_bss_end") == low["memory_end"] + P2 and
            low["memory_end"] <= LOW_LIMIT,
            "Low state or guarded stack boundaries moved")
    require(len(worker_bytes) >= EXPORTS.size and len(worker_bytes) <= 65536,
            "Invalid compact worker length")
    fields = EXPORTS.unpack_from(worker_bytes)
    require(fields[:3] == (0x54595031, 8, 92), "MDK2 worker requires the 92-byte v8 exports")
    names = {3: "_kui_toy_pilot_initialize", 4: "_kui_toy_pilot_request",
             5: "_kui_toy_pilot_service_hook", 6: "_kui_toy_pilot_am_init_hook",
             7: "_kui_toy_pilot_shutdown_hook", 8: "_kui_toy_pilot_snapshot",
             15: "_kui_toy_pilot_allstop_hook", 16: "_kui_toy_pilot_driver_load_hook",
             17: "_kui_toy_pilot_gd_dispatch", 19: "_kui_mdk2_cdda_shutdown2_hook",
             20: "_kui_mdk2_cdda_heap_reset_hook", 22: "_kui_mdk2_cdda_heap_init_hook"}
    for index, name in names.items():
        require(fields[index] == ws.get(name) and not fields[index] & 1 and
                WORKER + EXPORTS.size <= fields[index] < WORKER + len(worker_bytes),
                "Worker export is not its admitted cached entry: " + name)
    require(fields[18] == 0, "MDK2 must not export a Toy pause helper")
    bridge = _linked_symbols((directory / "worker.elf").read_bytes(),
                             {"_kui_toy_pilot_bridge_active"}).get(
                                 "_kui_toy_pilot_bridge_active", (0, 0, 0, None))
    bridge_address, bridge_bytes, bridge_kind, bridge_section = bridge
    require(fields[21] == bridge_address == ws.get("_kui_toy_pilot_bridge_active") and
            bridge_bytes == 4 and bridge_kind == 1 and bridge_section is not None and
            bridge_section[1] == 8 and bridge_section[2] & 7 == 3 and
            bridge_address % 4 == 0 and
            bridge_section[3] <= bridge_address < bridge_address + bridge_bytes <=
            bridge_section[3] + bridge_section[5] and
            fields[9] <= bridge_address < bridge_address + bridge_bytes <= fields[10],
            "MDK2 live bridge export is not its exact writable P2 BSS word")
    for index, name in ((9, "__toy_pilot_bss_begin"), (10, "__toy_pilot_bss_end"),
                        (11, "__toy_pilot_stack_bottom"), (12, "__toy_pilot_stack_top"),
                        (13, "__toy_pilot_worker_end")):
        require(fields[index] == ws.get(name), "Worker state export differs from ELF: " + name)
    require(fields[9] >= WORKER + len(worker_bytes) + P2 and
            fields[9] <= fields[10] <= fields[11] < fields[12] and
            fields[12] - fields[11] == 8192 and fields[12] - P2 == fields[13] and
            fields[13] == worker["memory_end"] <= MAP_ADDRESS and fields[14] == 0x30000 and
            all(value % 32 == 0 for value in fields[9:14]),
            "Worker private state, 8 KiB stack, or main lease is invalid")
    require(ws.get("__toy_pilot_gd_stack_bottom") == ws.get("__toy_pilot_gd_stack_top"),
            "MDK2 synchronous profile unexpectedly selected a second GD stack")
    require(ss.get("__retail_stage_bss_end") == stage["memory_end"] < HEAP_HEADER,
            "Transient stage can overlap the first allocation header")
    require(span(stage, STAGE, "__toy_pilot_worker_blob_start", "__toy_pilot_worker_blob_end") == worker_bytes,
            "Stage embeds different worker bytes")
    require(span(stage, STAGE, "__retail_resident_sci_blob_start", "__retail_resident_sci_blob_end") == low_bytes,
            "Stage embeds different low reader bytes")
    require(span(entry, 0x8C010000, "__retail_stage_blob_start", "__retail_stage_blob_end") == stage_bytes,
            "Entry embeds a different stage")
    package = (directory / "retail-mdk2-cdda.kui").read_bytes()
    envelope = verify(package)
    require(padded(package[64:]) == padded(entry["payload"]) and
            envelope["memory_bytes"] == entry["memory_end"] - 0x8C010000,
            "Packaged entry differs from its ELF")
    return {"package": envelope, "manifest_slots": 256,
            "held_map_begin": f"0x{MAP_ADDRESS:08x}", "held_map_end": f"0x{MAP_LIMIT:08x}",
            "map_bytes": source_bytes, "low_resident_bytes": len(low_bytes),
            "low_state_end": f"0x{low['memory_end']:08x}",
            "worker_bytes": len(worker_bytes), "worker_end": f"0x{worker['memory_end']:08x}",
            "stage_state_end": f"0x{stage['memory_end']:08x}",
            "exports_bytes": EXPORTS.size, "boot_control_bytes": control_bytes,
            "bridge_active": f"0x{bridge_address:08x}", "stack_bounds": stack_bounds(directory)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path, default=Path("build/mdk2-cdda"))
    args = parser.parse_args()
    try:
        print(json.dumps(check_directory(args.directory), sort_keys=True))
    except (ValueError, OSError, UnicodeError, struct.error) as exc:
        raise SystemExit("MDK2 CDDA layout check failed: " + str(exc)) from exc


if __name__ == "__main__":
    main()
