#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated read-only CE Maple controller and retained callers."""
import argparse
import json
from pathlib import Path

from check_armada_ce_bind import check as check_binding
from check_loader_layout import inspect_elf
from check_armada_ce_excerpt import audit_cache_reader, function_sizes


def check(directory):
    directory = Path(directory)
    report = check_binding(directory)
    elf = (directory / "bind-probe.elf").read_bytes()
    controller = inspect_elf(elf,
                             0x8C004000, 0x8C007C00,
                             entry_symbol="_kui_armada_ce_bind_probe")
    # The new symbol is a versioned, linked declaration of this observer's
    # report ABI and read-only scope; it is not a runtime guest input.
    address = controller["symbols"].get("_kui_armada_ce_maple_probe_contract", 0)
    at = address - 0x8C004000
    if not 0 <= at <= len(controller["payload"]) - 4 or controller["payload"][at:at + 4] != b"CMP4":
        raise ValueError("CE Maple header controller contract is missing")
    report["cache_reader"] = audit_cache_reader(controller,function_sizes(elf))
    report["purpose"] = "read-only CE Maple ordinary descriptor and pooled response headers"
    report["virtual_vmu_enabled"] = False
    report["dma_chain_followed"] = True
    report["dma_payload_read"] = False
    report["dma_idle_or_ownership_established"] = False
    report["driver_root_metadata_followed"] = True
    report["transfer_buffer_addresses_dereferenced"] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.directory), indent=2))


if __name__ == "__main__":
    main()
