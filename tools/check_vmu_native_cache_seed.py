#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently bind the optional native-resolution seed to its linked bytes.

The generic build must contain one empty immutable seed. Personalization or
the frontend supplies a complete record under its documented trust policy;
this audit does not authenticate user configuration or prove live ownership.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_loader_layout import inspect_elf, region
from personalize_vmu_sd_proof import readonly_blob
from runtime_package import ADDRESS, verify
from toy_pilot_symbols import _linked_symbols

STAGE, LIMIT = 0x8CE00000, 0x8CFE0000
SEED_BYTES, RECORD_BYTES = 1120, 1024
START, END = "__native_resolution_cache_seed_start", "__native_resolution_cache_seed_end"


def require(value, message):
    if not value:
        raise ValueError(message)


def empty_seed():
    # This constructor deliberately does not import the personalizer's codec.
    return b"KUINVS01" + struct.pack("<4I", 1, SEED_BYTES, 0, RECORD_BYTES) + bytes(SEED_BYTES - 24)


def generic_seed(data):
    require(len(data) == SEED_BYTES and data == empty_seed(),
            "generic cache seed differs from its complete canonical empty template")


def audit(stage_data, entry_data, module, *, cached_code):
    require(type(cached_code) is bool, "cache audit requires an explicit worker alias policy")
    stage = inspect_elf(stage_data, STAGE, LIMIT)
    symbols = stage["symbols"]
    present = {name for name in (START, END) if name in symbols}
    if not present:
        require(not any(name.startswith("_kui_vmu_native_resolution_cache_") for name in symbols),
                "resolution-cache implementation is linked without its exact seed exports")
        require(stage["payload"].find(empty_seed()) == -1,
                "untyped resolution seed template is present without its exact exports")
        return {"enabled": False}
    require(present == {START, END} and cached_code, "resolution cache requires both seed exports and cached native code")
    typed = _linked_symbols(stage_data, {START, END})
    address, size, kind, _ = typed[START]
    require(kind == 1 and size == SEED_BYTES, "resolution seed must be one sized typed object")
    bound, file_offset = readonly_blob(stage_data, stage, START, END, SEED_BYTES)
    require(bound == address, "typed resolution seed binding differs")
    template = region(stage_data, file_offset, SEED_BYTES, "typed resolution seed")
    generic_seed(template)
    offset = address - STAGE
    require(region(stage["payload"], offset, SEED_BYTES, "stage resolution seed") == template and
            stage["payload"].find(template) == offset and stage["payload"].find(template, offset + 1) == -1,
            "stage does not contain exactly its unique typed empty seed")
    for first, last in (("__vmu_sd_profile_blob_start", "__vmu_sd_profile_blob_end"),
                        ("__native_worker_blob_begin", "__native_worker_blob_end"),
                        ("__native_worker_reloc_begin", "__native_worker_reloc_end"),
                        ("__retail_trampoline_start", "__retail_trampoline_end")):
        require(first in symbols and last in symbols and symbols[first] < symbols[last],
                "cache seed lacks a bounded protected stage span: " + first)
        require(address + SEED_BYTES <= symbols[first] or symbols[last] <= address,
                "resolution seed overlaps another stage profile/blob/template")
    entry = inspect_elf(entry_data, ADDRESS, 0x8C112000)
    stage_begin, stage_file = readonly_blob(entry_data, entry,
        "__retail_stage_blob_start", "__retail_stage_blob_end", len(stage["payload"]))
    require(region(entry_data, stage_file, len(stage["payload"]), "entry stage") == stage["payload"],
            "entry embeds a different cache-enabled stage")
    verify(module)
    require(module[64:] == entry["payload"], "module differs from its cache-enabled entry")
    module_offset = 64 + stage_begin - ADDRESS + offset
    require(region(module, module_offset, SEED_BYTES, "module resolution seed") == template and
            module.find(template) == module_offset and module.find(template, module_offset + 1) == -1,
            "module does not contain exactly its typed unique empty seed")
    return {"enabled": True, "seed_address": hex(address), "seed_stage_file_offset": file_offset,
            "seed_module_offset": module_offset, "seed_bytes": SEED_BYTES, "record_bytes": RECORD_BYTES,
            "independent_digest_offset": 24, "record_offset": 64,
            "empty_template_sha256": hashlib.sha256(template).hexdigest(),
            "typed_immutable_object": True, "unique_stage_and_module_template": True,
            "profile_worker_relocation_trampoline_disjoint": True,
            "trust_scope": "personalized immutable module or explicit ordinary frontend configuration",
            "live_ownership_not_cached": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    directory = args.directory
    try:
        result = audit((directory / "stage.elf").read_bytes(), (directory / "entry.elf").read_bytes(),
                       (directory / "retail-maple-observe.kui").read_bytes(), cached_code=True)
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(2, "Native resolution seed audit refused: " + str(error) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
