#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate retail low resident, high staging/relay, embedded bytes and stacks."""
import argparse
import json
from pathlib import Path
import struct

from check_loader_layout import inspect_elf, padded, region
import retail_package as layout

FORBIDDEN_PREFIXES = (
    "_kos_", "_arch_", "_thd_", "_fs_", "_mutex_", "_sem_", "_sd_",
    "_timer_", "_irq_", "_maple_", "_pvr_", "_snd_", "_dcload_", "___cxa_",
)
FORBIDDEN_SYMBOLS = {
    "_malloc", "_calloc", "_realloc", "_free", "_printf", "_snprintf",
    "_fprintf", "_puts", "_f_open", "_f_read", "_f_write", "_f_lseek",
    "_f_mount", "_f_close", "_kui_loader_sd_init", "_native_begin",
    "_native_ticks", "_native_end",  # standalone probe's timer-owning bus
}
# These initialize before game execution, using the high stage's separate
# 64 KiB stack. All remaining retained C functions are conservatively summed,
# even though most cannot appear together on a real call chain. No recursion
# is part of the supported resident graph. Assembly frames/dispatch allowance
# is counted separately; the guard and top alignment gap are unavailable.
INIT_ONLY = {
    "kui_retail_resident_init", "kui_retail_manifest_decode",
    "kui_retail_image_init", "kui_retail_gd_init", "kui_retail_sd_init",
    "kui_loader_sd_init_bus", "capacity",
}
ASSEMBLY_STACK_BYTES = 256
STACK_GUARD_BYTES = 16
STACK_ALIGNMENT_GAP = 32


def code_symbol(image, name, base):
    value = image["symbols"].get(name, 0)
    if value % 2 or not base <= value < base + len(image["payload"]):
        raise ValueError(f"Missing retail executable symbol: {name}")
    return value


def check_bss(image, base, prefix):
    symbols = image["symbols"]
    start = symbols.get(prefix + "_bss_begin", 0)
    end = symbols.get(prefix + "_bss_end", 0)
    binary_end = symbols.get(prefix + "_binary_end", 0)
    file_end = base + len(image["payload"])
    if (start % 4 or end % 4 or start < file_end or start > end or
            end != image["memory_end"] or not file_end <= binary_end <= file_end + 3 or
            binary_end > start):
        raise ValueError(f"Invalid {prefix} BSS bounds")


def check_stack_usage(directory, symbols):
    reports = list(Path(directory).rglob("*.su"))
    if not reports:
        raise ValueError("Missing compiler stack-usage reports")
    frames = {}
    for report in reports:
        for line in report.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) != 3:
                raise ValueError(f"Malformed stack-usage report: {report.name}")
            name = fields[0].rsplit(":", 1)[-1]
            if "_" + name not in symbols or name in INIT_ONLY:
                continue
            if fields[2] != "static":
                raise ValueError(f"Unbounded/dynamic resident stack frame: {name}")
            frame = int(fields[1])
            if frame < 0:
                raise ValueError(f"Invalid resident stack frame: {name}")
            frames[name] = max(frames.get(name, 0), frame)
    for name in ("kui_retail_resident_dispatch", "kui_retail_gd_dispatch",
                 "kui_retail_image_read", "kui_loader_sd_read"):
        if name not in frames:
            raise ValueError(f"Missing runtime stack-usage frame: {name}")
    available = (layout.HOOK_STACK - layout.HOOK_STACK_BOTTOM -
                 STACK_GUARD_BYTES - STACK_ALIGNMENT_GAP)
    maximum = sum(frames.values()) + ASSEMBLY_STACK_BYTES
    if maximum > available:
        raise ValueError(f"Resident conservative stack sum {maximum} exceeds {available}")
    return {"conservative_bytes": maximum, "available_bytes": available,
            "assembly_allowance": ASSEMBLY_STACK_BYTES, "retained_c_frames": len(frames)}


def check_directory(directory):
    directory = Path(directory)
    images = {
        "resident": inspect_elf((directory / "resident.elf").read_bytes(),
                                layout.RESIDENT_ADDRESS, layout.RESIDENT_LIMIT),
        "stage": inspect_elf((directory / "stage.elf").read_bytes(),
                             layout.STAGE_ADDRESS, layout.STAGE_MEMORY_END),
        "entry": inspect_elf((directory / "entry.elf").read_bytes(),
                             layout.EXEC_ADDRESS,
                             layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET +
                             layout.STAGE_MAX_BYTES),
    }
    for name, image in images.items():
        forbidden = [s for s in image["symbols"] if
                     s.startswith(FORBIDDEN_PREFIXES) or s in FORBIDDEN_SYMBOLS]
        if forbidden:
            raise ValueError(f"Forbidden runtime/device symbol in {name}: {forbidden[0]}")
    resident, stage, entry = (images[name] for name in ("resident", "stage", "entry"))
    for name, base, prefix in (
        ("resident", layout.RESIDENT_ADDRESS, "__retail_resident"),
        ("stage", layout.STAGE_ADDRESS, "__retail_stage"),
    ):
        image = images[name]
        if padded((directory / f"{name}.bin").read_bytes()) != padded(image["payload"]):
            raise ValueError(f"{name}.bin differs from linked ELF bytes")
        check_bss(image, base, prefix)

    for name in ("_kui_retail_resident_init", "_kui_retail_resident_hook",
                 "_kui_retail_resident_dispatch", "_kui_retail_gd_dispatch",
                 "_kui_retail_image_read", "_kui_loader_sd_read",
                 "_kui_retail_sd_acquire", "_kui_retail_sd_release"):
        code_symbol(resident, name, layout.RESIDENT_ADDRESS)
    rs, ss, es = (image["symbols"] for image in (resident, stage, entry))
    if (rs.get("__retail_hook_stack") != layout.HOOK_STACK or
            rs.get("__retail_hook_stack_bottom") != layout.HOOK_STACK_BOTTOM):
        raise ValueError("Resident hook stack is outside the reserved retired IP area")
    for name in ("_kui_retail_hook_active", "_kui_retail_hook_fault"):
        if not layout.RESIDENT_ADDRESS <= rs.get(name, 0) < resident["memory_end"]:
            raise ValueError(f"Missing resident-owned hook guard: {name}")

    for name in ("_kui_retail_stage_main", "_kui_retail_stage_relay",
                 "_kui_retail_bootstrap_enter", "_kui_retail_game_resume"):
        code_symbol(stage, name, layout.STAGE_ADDRESS)
    low_blob = padded((directory / "resident.bin").read_bytes())
    begin, end = ss.get("__retail_resident_blob_start", 0), ss.get("__retail_resident_blob_end", 0)
    if (begin < layout.STAGE_ADDRESS or end - begin != len(low_blob) or
            region(stage["payload"], begin - layout.STAGE_ADDRESS,
                   len(low_blob), "embedded low resident") != low_blob):
        raise ValueError("Stage contains a different/invalid low resident")
    begin, end = ss.get("__retail_trampoline_start", 0), ss.get("__retail_trampoline_end", 0)
    if begin % 4 or end - begin != layout.TRAMPOLINE_BYTES:
        raise ValueError("Invalid bounded executable-entry trampoline")
    trampoline = region(stage["payload"], begin - layout.STAGE_ADDRESS,
                        layout.TRAMPOLINE_BYTES, "entry trampoline")
    # The assembly relay saves CPU state before entering the C relay body.
    relay = ss["_kui_retail_game_resume"] | 0x20000000
    if struct.pack("<I", relay) not in trampoline:
        raise ValueError("Trampoline does not target the uncached high-stage relay")

    high_blob = padded((directory / "stage.bin").read_bytes())
    if not 4 <= len(high_blob) <= layout.STAGE_MAX_BYTES:
        raise ValueError("Temporary stage exceeds its relocation bound")
    base = layout.EXEC_ADDRESS
    if (es.get("__retail_map") != base + layout.MAP_OFFSET or
            es.get("__retail_stage_blob_start") != base + layout.STAGE_BLOB_OFFSET or
            es.get("__retail_stage_blob_end") != base + len(entry["payload"]) or
            entry["payload"][layout.STAGE_BLOB_OFFSET:] != high_blob or
            entry["memory_end"] != base + len(entry["payload"])):
        raise ValueError("Invalid packed retail stage/manifest layout")
    if any(region(entry["payload"], layout.MAP_OFFSET, layout.MAP_BYTES, "manifest")):
        raise ValueError("Packaged retail manifest must be blank")
    if (region(entry["payload"], layout.HEADER_OFFSET, layout.HEADER_BYTES, "header") !=
            layout.relocation_header(len(high_blob))):
        raise ValueError("Retail relocation header mismatch")
    result = {name: {"payload_bytes": len(image["payload"]),
                     "memory_end": f"0x{image['memory_end']:08x}",
                     "unresolved_symbols": 0} for name, image in images.items()}
    result["resident_stack"] = check_stack_usage(directory, rs)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build/retail")
    args = parser.parse_args()
    try:
        print(json.dumps(check_directory(args.directory), sort_keys=True))
    except (ValueError, OSError, UnicodeError, struct.error) as error:
        raise SystemExit(f"Retail loader layout check failed: {error}") from error


if __name__ == "__main__":
    main()
