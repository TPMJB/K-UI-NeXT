#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the dedicated native SCI Standard LZ4-ZSO linked images."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess

from check_loader_layout import inspect_elf, padded, region
from check_retail_loader_layout import (ASSEMBLY_STACK_BYTES, FORBIDDEN_PREFIXES,
                                        FORBIDDEN_SYMBOLS, STACK_ALIGNMENT_GAP,
                                        STACK_GUARD_BYTES, check_bss, code_symbol)
from check_retail_instructions import audit
from check_retail_stack import NODE, base as function_base
import zso_package as layout

IMAGES = ("resident-sci", "stage", "entry")
REQUIRED_RESIDENT = (
    "_kui_retail_resident_init", "_kui_retail_resident_hook",
    "_kui_retail_resident_dispatch", "_kui_retail_gd_dispatch",
    "_kui_retail_zso_image_read", "_kui_zso_read_block",
    "_kui_loader_sd_stream_next", "_kui_sci_sd_acquire", "_kui_sci_sd_release",
)
REQUIRED_FRAMES = (
    "kui_retail_resident_dispatch", "kui_retail_gd_dispatch",
    "kui_retail_zso_image_read", "kui_zso_read_block", "kui_loader_sd_stream_next",
)


def check_init_domain(directory, images):
    """Prove that only the named initializer uses the existing high stack.

    Pin the linked wrapper/bootstrap/entry bytes rather than trusting a
    function name. Final resident graphs must not call the initializer, and
    its sole low-image address literal belongs to the pinned entry. The
    stage's checked call sites initialize low code before entering owner boot.
    Shared helpers retain their full conservative runtime stack charge.
    """
    resident, stage, entry = (images[name] for name in IMAGES)
    rs, ss = resident["symbols"], stage["symbols"]
    entry_code = struct.pack("<16H6I", *(
        0xD007, 0x400E, 0xD007, 0xD108, 0xD208, 0x4209, 0x6306, 0x2132,
        0x7104, 0x4210, 0x8BFA, 0xDF06, 0xD406, 0xD003, 0x402B, 0x0009),
        0x500000F0, layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET,
        layout.STAGE_ADDRESS, len(entry["payload"]) - layout.STAGE_BLOB_OFFSET,
        layout.STAGE_STACK, layout.EXEC_ADDRESS + layout.MAP_OFFSET)
    if region(entry["payload"], 0, len(entry_code), "ZSO high-stack wrapper") != entry_code:
        raise ValueError("ZSO entry must mask SR and establish the exact high initialization stack")
    stage_code = struct.pack("<32H7I", *(
        0xD012, 0x402B, 0x0009, 0xD013, 0xD113, 0x2012,
        *([0x0009] * 8), 0xD00C, 0x402B, 0x0009, 0xD007, 0xD107,
        0xE200, 0x3012, 0x8903, 0x2022, 0x7004, 0xAFFA, 0x0009,
        0xD004, 0x400B, 0x0009, 0xAFFE, 0x0009, 0x0009),
        ss["__retail_stage_bss_begin"], ss["__retail_stage_bss_end"],
        ss["_kui_retail_stage_main"], layout.STAGE_ADDRESS + 0x20000006,
        layout.STAGE_ADDRESS + 34, 0xFF00001C, 0x00000909)
    if region(stage["payload"], 0, len(stage_code), "ZSO high-stage bootstrap") != stage_code:
        raise ValueError("ZSO bootstrap must preserve the high stack through the stage-main call")
    init_address = rs["_kui_retail_resident_init"]
    resident_code = struct.pack("<12H3I", *(
        0xD005, 0xD106, 0xE200, 0x3012, 0x8903, 0x2022,
        0x7004, 0xAFFA, 0x0009, 0xD003, 0x402B, 0x0009),
        rs["__retail_resident_bss_begin"], rs["__retail_resident_bss_end"], init_address)
    if region(resident["payload"], 0, len(resident_code), "ZSO resident bootstrap") != resident_code:
        raise ValueError("ZSO resident initializer entry must not switch stacks")
    pointers = [at for at in range(0, len(resident["payload"]) - 3, 4)
                if struct.unpack_from("<I", resident["payload"], at)[0] in
                (init_address, init_address | 0x20000000, init_address & 0x1FFFFFFF)]
    if pointers != [32]:
        raise ValueError("ZSO resident initializer must have exactly one pinned entry reference")
    reports = sorted((Path(directory) / "sci/lto").glob("*.ltrans*.ci"))
    if not reports:
        raise ValueError("Missing final ZSO initializer call-graph proof")
    nodes = []
    edge = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')
    for report in reports:
        text = report.read_text()
        nodes += [(title, label) for title, label in NODE.findall(text)
                  if function_base(title) == "kui_retail_resident_init"]
        if any(function_base(target) == "kui_retail_resident_init" for _, target in edge.findall(text)):
            raise ValueError("ZSO runtime call graph enters the high-stack-only initializer")
    if len(nodes) != 1 or nodes[0][0] != "kui_retail_resident_init":
        raise ValueError("Missing or duplicated ZSO initializer graph node")
    found = re.search(r"\\n(\d+) bytes \(static\)", nodes[0][1])
    if not found:
        raise ValueError("ZSO initializer graph requires a static frame")
    # Use objdump's instruction classification, then independently resolve PC
    # literals from linked bytes. The held target is ABI callee-saved r8..r14;
    # intervening calls cannot silently replace it. No branch/target-register
    # rewrite is accepted in the short straight-line handoff sequence.
    decoded = []
    for line in (Path(directory) / "stage.dis").read_text().splitlines():
        item = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+(\S+)(?:\s+(.*))?$", line)
        if item:
            decoded.append((int(item[1], 16), int(item[2], 16) | int(item[3], 16) << 8,
                            item[4], (item[5] or "").split("!", 1)[0].strip()))
    # A referenced literal can itself resemble a PC-load opcode. Apply the
    # same forward-only literal classification as the instruction gate before
    # looking for call sites; data cannot manufacture an initialization proof.
    decoded.sort()
    literals = set()
    for address, opcode, mnemonic, _ in decoded:
        if address in literals:
            continue
        if opcode & 0xF000 == 0xD000 and mnemonic == "mov.l":
            target = ((address + 4) & ~3) + (opcode & 255) * 4
            literals.update((target, target + 2))
        elif opcode & 0xF000 == 0x9000 and mnemonic == "mov.w":
            literals.add(address + 4 + (opcode & 255) * 2)
    decoded = [row for row in decoded if row[0] not in literals]
    main = ss["_kui_retail_stage_main"]
    next_symbol = min(value for value in ss.values() if value > main)
    decoded = [row for row in decoded if main <= row[0] < next_symbol]
    init_calls, boot_calls = [], []
    for index, (address, opcode, mnemonic, _) in enumerate(decoded):
        if opcode & 0xF000 != 0xD000 or mnemonic != "mov.l":
            continue
        target = ((address + 4) & ~3) + (opcode & 255) * 4
        value = struct.unpack("<I", region(stage["payload"], target - layout.STAGE_ADDRESS, 4, "stage call literal"))[0]
        reg = (opcode >> 8) & 15
        if value == ss["_kui_retail_bootstrap_enter"] and index + 1 < len(decoded):
            call = decoded[index + 1]
            if call[0] == address + 2 and call[1] == (0x400B | reg << 8):
                boot_calls.append(call[0])
        if value != layout.RESIDENT_ADDRESS or not 8 <= reg <= 14:
            continue
        for following in decoded[index + 1:]:
            if following[0] > address + 64:
                break
            if following[1] == (0x400B | reg << 8):
                init_calls.append(following[0])
                break
            if following[2] in ("bra", "braf", "jmp", "rts", "rte", "bt", "bf", "bt.s", "bf.s"):
                break
            if following[3].split(",")[-1].strip() == f"r{reg}":
                break
    if len(init_calls) != 1 or len(boot_calls) != 1 or init_calls[0] >= boot_calls[0]:
        raise ValueError("ZSO stage must initialize its resident on the high stack before owner bootstrap")
    return {"frame_bytes": int(found.group(1)), "resident_entry_bytes": len(resident_code),
            "wrapper_bytes": len(entry_code), "stage_bootstrap_bytes": len(stage_code),
            "init_call": f"0x{init_calls[0]:08x}", "owner_boot_call": f"0x{boot_calls[0]:08x}",
            "runtime_incoming_edges": 0}


def check_stack_usage(directory, symbols, init_proof):
    """Count every final LTO row, including local clones and initialization.

    The one independently proven high-stack initialization frame is charged
    to its high-stack budget. Every shared helper and all other rows remain
    charged to the private runtime stack. Pre-LTO reports are not a fallback.
    """
    reports = sorted((Path(directory) / "sci/lto").glob("*.ltrans*.su"))
    if not reports:
        raise ValueError("Missing final ZSO compiler stack-usage reports")
    frames, total, rows, init_frames = set(), ASSEMBLY_STACK_BYTES, 0, []
    for report in reports:
        for line in report.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) != 3:
                raise ValueError("Malformed ZSO stack-usage report")
            name = fields[0].rsplit(":", 1)[-1]
            if fields[2] != "static":
                raise ValueError(f"Unbounded/dynamic ZSO resident stack frame: {name}")
            try:
                frame = int(fields[1])
            except ValueError as error:
                raise ValueError("Malformed ZSO resident stack size") from error
            if frame < 0:
                raise ValueError("Invalid ZSO resident stack size")
            frames.add(name)
            total += frame
            rows += 1
            if function_base(name) == "kui_retail_resident_init":
                init_frames.append((name, frame))
    for name in REQUIRED_FRAMES:
        if name not in frames or "_" + name not in symbols:
            raise ValueError(f"Missing ZSO runtime stack-usage frame: {name}")
    if init_frames != [("kui_retail_resident_init", init_proof["frame_bytes"])]:
        raise ValueError("ZSO initializer frame and final call graph differ")
    high_reports = sorted((Path(directory) / "stage").rglob("*.su"))
    if not high_reports:
        raise ValueError("Missing ZSO high-stage stack reports")
    high_frames, main_found = 0, False
    for report in high_reports:
        for line in report.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) != 3 or fields[2] != "static":
                raise ValueError("Malformed/dynamic ZSO high-stage stack report")
            frame = int(fields[1])
            if frame < 0:
                raise ValueError("Invalid ZSO high-stage stack size")
            high_frames += frame
            main_found |= fields[0].rsplit(":", 1)[-1] == "kui_retail_stage_main"
    if not main_found:
        raise ValueError("Missing ZSO high-stage main stack frame")
    high_available = layout.STAGE_STACK - layout.STAGE_MEMORY_END - STACK_GUARD_BYTES - STACK_ALIGNMENT_GAP
    if total + high_frames > high_available:
        raise ValueError("ZSO initialization exceeds its existing high-stack budget")
    available = layout.HOOK_STACK - layout.RESIDENT_LIMIT - STACK_GUARD_BYTES - STACK_ALIGNMENT_GAP
    runtime = total - init_frames[0][1]
    if runtime > available:
        raise ValueError(f"ZSO resident conservative stack sum {runtime} exceeds {available}")
    return {"conservative_bytes": runtime, "available_bytes": available,
            "assembly_allowance": ASSEMBLY_STACK_BYTES, "retained_c_frames": rows,
            "initialization": {**init_proof, "conservative_bytes": total + high_frames,
                               "available_bytes": high_available}}


def check_instructions(directory):
    """Always audit actual linked disassembly; no opt-out for this profile."""
    count = 0
    for name in IMAGES:
        try:
            disassembly = subprocess.check_output(
                ["sh-elf-objdump", "-d", str(Path(directory) / (name + ".elf"))], text=True)
            count += audit(name, disassembly)
        except (OSError, subprocess.CalledProcessError, SystemExit) as error:
            raise ValueError(f"ZSO linked integer-instruction audit failed: {error}") from error
        (Path(directory) / (name + ".dis")).write_text(disassembly)
    return {"instructions": count, "bootstrap_fpscr_setups": 1, "integer_only": True}


def check_directory(directory):
    directory = Path(directory)
    profile = (directory / "layout-config").read_text().strip()
    if not re.fullmatch(r"ZSO=1 LOW=1 TRANSPORT=1 READER=0 SLOTS=32 PATCHES=0 BUILD=[0-9a-f]{12}", profile):
        raise ValueError("ZSO build profile must be native SCI Standard, low, 32 slots and no RAM patches")
    images = {
        "resident-sci": inspect_elf((directory / "resident-sci.elf").read_bytes(),
                                    layout.RESIDENT_ADDRESS, layout.RESIDENT_LIMIT),
        "stage": inspect_elf((directory / "stage.elf").read_bytes(),
                             layout.STAGE_ADDRESS, layout.STAGE_MEMORY_END),
        "entry": inspect_elf((directory / "entry.elf").read_bytes(), layout.EXEC_ADDRESS,
                             layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET + layout.STAGE_MAX_BYTES),
    }
    for name, image in images.items():
        forbidden = [s for s in image["symbols"] if s.startswith(FORBIDDEN_PREFIXES) or s in FORBIDDEN_SYMBOLS or
                     s.startswith(("_kui_retail_async_", "_kui_retail_ce_", "_kui_ata_", "_kui_game_ram_patch"))]
        if forbidden:
            raise ValueError(f"Forbidden runtime/device/profile symbol in ZSO {name}: {forbidden[0]}")
    resident, stage, entry = (images[name] for name in IMAGES)
    for name, prefix, base in (("resident-sci", "__retail_resident", layout.RESIDENT_ADDRESS),
                               ("stage", "__retail_stage", layout.STAGE_ADDRESS)):
        if padded((directory / (name + ".bin")).read_bytes()) != padded(images[name]["payload"]):
            raise ValueError(f"ZSO {name}.bin differs from linked ELF bytes")
        check_bss(images[name], base, prefix)
    rs, ss, es = resident["symbols"], stage["symbols"], entry["symbols"]
    for symbol in REQUIRED_RESIDENT:
        code_symbol(resident, symbol, layout.RESIDENT_ADDRESS)
    if (rs.get("__retail_hook_stack") != layout.HOOK_STACK or
            rs.get("__retail_hook_stack_bottom") != layout.RESIDENT_LIMIT):
        raise ValueError("ZSO hook stack is outside its low resident reservation")
    for symbol in ("_kui_retail_hook_active", "_kui_retail_hook_fault"):
        if not layout.RESIDENT_ADDRESS <= rs.get(symbol, 0) < resident["memory_end"]:
            raise ValueError(f"Missing ZSO resident-owned hook guard: {symbol}")
    for symbol in ("_kui_retail_stage_main", "_kui_retail_stage_relay", "_kui_retail_bootstrap_enter",
                   "_kui_retail_game_resume", "_kui_retail_zso_manifest_decode", "_kui_retail_zso_verify_source"):
        code_symbol(stage, symbol, layout.STAGE_ADDRESS)
    low_blob = padded((directory / "resident-sci.bin").read_bytes())
    begin = ss.get("__retail_resident_sci_blob_start", 0)
    end = ss.get("__retail_resident_sci_blob_end", 0)
    if (begin % 4 or begin < layout.STAGE_ADDRESS or end - begin != len(low_blob) or
            region(stage["payload"], begin - layout.STAGE_ADDRESS, len(low_blob), "ZSO SCI resident") != low_blob):
        raise ValueError("ZSO stage contains a different/invalid embedded SCI resident")
    # Generic assembly labels are aliases of this one resident, never alternate
    # SCIF/IDE/background images hidden inside a purported SCI-only payload.
    for transport in ("scif", "ide", "scia"):
        label = "__retail_resident_" + transport + "_blob_"
        if ss.get(label + "start") != begin or ss.get(label + "end") != end:
            raise ValueError("ZSO stage must contain exactly one aliased Standard SCI resident")
    start, stop = ss.get("__retail_trampoline_start", 0), ss.get("__retail_trampoline_end", 0)
    if start % 4 or stop - start != 128:
        raise ValueError("Invalid ZSO executable-entry trampoline")
    trampoline = region(stage["payload"], start - layout.STAGE_ADDRESS, 128, "ZSO trampoline")
    if struct.pack("<I", ss["_kui_retail_game_resume"] | 0x20000000) not in trampoline:
        raise ValueError("ZSO trampoline does not target the uncached high-stage relay")
    high_blob = padded((directory / "stage.bin").read_bytes())
    if (es.get("__retail_map") != layout.EXEC_ADDRESS + layout.MAP_OFFSET or
            es.get("__retail_stage_blob_start") != layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET or
            es.get("__retail_stage_blob_end") != layout.EXEC_ADDRESS + len(entry["payload"]) or
            entry["payload"][layout.STAGE_BLOB_OFFSET:] != high_blob or
            entry["memory_end"] != layout.EXEC_ADDRESS + len(entry["payload"])):
        raise ValueError("Invalid packed ZSO stage/manifest layout")
    contract = layout.inspect_payload(entry["payload"], entry["memory_end"] - layout.EXEC_ADDRESS)
    instructions = check_instructions(directory)
    init_proof = check_init_domain(directory, images)
    stacks = check_stack_usage(directory, rs, init_proof)
    result = {name: {"payload_bytes": len(image["payload"]),
                     "memory_end": f"0x{image['memory_end']:08x}", "unresolved_symbols": 0}
              for name, image in images.items()}
    result.update(contract=contract, resident_stack=stacks, instruction_audit=instructions)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path, default=Path("build/retail-zso"))
    args = parser.parse_args()
    try:
        print(json.dumps(check_directory(args.directory), sort_keys=True))
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        parser.exit(1, f"ZSO loader check failed: {error}\n")


if __name__ == "__main__":
    main()
