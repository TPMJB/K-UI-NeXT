#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Print one line of JSON with each low resident's end, free bytes, stack sum
and largest symbols, for a CI annotation (logs are not always retrievable)."""
import json
from pathlib import Path
import subprocess
import sys

from check_retail_loader_layout import (ASYNC, RESIDENTS, check_async_stack,
                                        check_stack_usage, resident_limit,
                                        entry_placement, inspect_entry)


def symbols(elf):
    out = subprocess.run(["sh-elf-nm", "-S", "--size-sort", str(elf)],
                         capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        fields = line.split()
        if len(fields) == 4:
            rows.append((fields[3].lstrip("_"), int(fields[1], 16), fields[2]))
    return rows


def main():
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/retail")
    ce = "--ce" in sys.argv[2:]
    placement = entry_placement(inspect_entry(directory), ce)
    low, resident_address = placement["low"], placement["address"]
    report = {}
    for transport in RESIDENTS:
        elf = directory / f"resident-{transport}.elf"
        nm = subprocess.run(["sh-elf-nm", str(elf)], capture_output=True, text=True,
                            check=True).stdout
        values = {f.split()[-1]: int(f.split()[0], 16) for f in nm.splitlines()
                  if len(f.split()) == 3}
        end = values["__retail_resident_bss_end"]
        binary = values["__retail_resident_binary_end"]
        try:
            if transport == ASYNC:
                stack = check_async_stack(directory / transport, ce, low)
                stack["conservative_bytes"] = stack["worst_bytes"]
            else:
                stack = check_stack_usage(directory / transport, set(values), transport, ce, low)
        except ValueError as error:
            stack = {"conservative_bytes": str(error), "available_bytes": None}
        rows = symbols(elf)
        report[transport] = {
            "code_data": binary - resident_address,
            "bss": end - values["__retail_resident_bss_begin"],
            "free": resident_limit(transport, ce, low) - end,
            "stack": stack["conservative_bytes"], "stack_limit": stack["available_bytes"],
            "top": [[n, s, k] for n, s, k in rows[-16:]] if transport in ("sci", ASYNC) else [],
        }
    print(json.dumps(report, separators=(",", ":")))


if __name__ == "__main__":
    main()
