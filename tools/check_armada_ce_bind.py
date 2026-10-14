#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the additional Armada RAM-only controller and its retained call graph."""
import argparse
import json
from pathlib import Path
import subprocess
import struct
from check_retail_stack import load as load_graph, base as base_name
from check_loader_layout import inspect_elf, region
from check_retail_loader_layout import check_bss, check_directory, FORBIDDEN_PREFIXES, FORBIDDEN_SYMBOLS
from check_retail_instructions import audit

BASE = 0x8c004000
LIMIT = 0x8c007c00
STAGE = 0x8ce10000


def check(directory):
    directory = Path(directory)
    ordinary = check_directory(directory, ce=True)
    probe = inspect_elf((directory / "bind-probe.elf").read_bytes(), BASE, LIMIT,
                        entry_symbol="_kui_armada_ce_bind_probe")
    check_bss(probe, BASE, "__armada_bind")
    if any(s.startswith(FORBIDDEN_PREFIXES) or s in FORBIDDEN_SYMBOLS
           for s in probe["symbols"]):
        raise ValueError("Forbidden allocator, CE API, hardware or SDK dependency")
    if (directory / "bind-probe.bin").read_bytes() != probe["payload"]:
        raise ValueError("Controller binary differs from linked ELF")
    stage = inspect_elf((directory / "stage.elf").read_bytes(), STAGE, 0x8cfe0000)
    start = stage["symbols"].get("__armada_bind_blob_start", 0)
    end = stage["symbols"].get("__armada_bind_blob_end", 0)
    if start % 4 or end-start != len(probe["payload"]) or region(
            stage["payload"], start-STAGE, end-start, "binding controller") != probe["payload"]:
        raise ValueError("Embedded controller is missing, misaligned or different")
    marker=stage["symbols"].get("_kui_armada_ce_bind_stage_contract",0)
    if region(stage["payload"],marker-STAGE,4,"binding stage contract") != struct.pack("<I",0x43454231):
        raise ValueError("Binding C stage admission/installer contract is missing")
    resident = inspect_elf((directory / "resident-scia.elf").read_bytes(),
                           0x8c008300, 0x8c00d800)
    if resident["symbols"].get("_kui_armada_ce_bind_probe") != BASE:
        raise ValueError("Resident controller call target moved")
    _, calls=load_graph(sorted((directory / "scia/lto").glob("*.ci")))
    sources={base_name(source) for source, edges in calls.items()
             if any(base_name(target)=="kui_armada_ce_bind_probe" for target, _ in edges)}
    if sources!={"kui_retail_resident_dispatch","kui_retail_menu_return"}:
        raise ValueError("Controller must be called only from GD dispatch and terminal return")
    stack = ordinary["resident_stacks"]["scia"]
    if not stack["call_graph"] or stack["worst_bytes"] + stack["margin"] > stack["available_bytes"]:
        raise ValueError("Controller is not covered by bounded CE stack audit")
    disassembly = subprocess.check_output(
        ["sh-elf-objdump", "-d", str(directory / "bind-probe.elf")], text=True)
    (directory / "bind-probe.dis").write_text(disassembly)
    instructions = audit("bind-probe", disassembly)
    return {"controller_entry": hex(BASE), "controller_memory_end": hex(probe["memory_end"]),
            "controller_payload_bytes": len(probe["payload"]),
            "controller_unresolved_symbols": 0,
            "controller_instructions": instructions,
            "controller_bss_bytes": probe["memory_end"] - probe["symbols"]["__armada_bind_bss_begin"],
            "embedded_bytes_match": True, "resident_target_matches": True,
            "ce_dispatch_stack_includes_controller": True, "resident_stacks": ordinary["resident_stacks"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.directory), indent=2))


if __name__ == "__main__":
    main()
