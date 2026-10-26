#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated automatic native allocator console probe's linked bytes.

This is an artifact/ABI check, not console or broad-game compatibility proof.
It deliberately does not reuse the exact-Toy/Silver original-target checker.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from check_loader_layout import inspect_elf, padded, region
from check_retail_instructions import audit as audit_instructions
from check_retail_loader_layout import check_bss
from runtime_package import verify
from toy_pilot_symbols import _linked_symbols
import retail_package as layout

LOW = 0x8C004000
HIGH = 0x8CE00000
LIMIT = 0x8CFE0000
MASK = 0x100000F0


def require(condition, message):
    if not condition:
        raise ValueError(message)


def words(data):
    require(len(data) % 2 == 0, "Odd executable function size")
    return list(struct.unpack("<" + "H" * (len(data) // 2), data))


def typed_function(elf, image, name, base):
    address, size, kind, section = _linked_symbols(elf, {name}).get(name, (0, 0, 0, None))
    require(kind == 2 and size and not size & 1 and not address & 1 and section is not None,
            "Missing bounded typed function: " + name)
    require(section[1] == 1 and section[2] & 7 == 6 and
            section[3] <= address < address + size <= section[3] + section[5] and
            image["symbols"].get(name) == address,
            "Function outside linked executable section: " + name)
    body = region(image["payload"], address - base, size, name)
    require(body == region(elf, section[4] + address - section[3], size, name),
            "Function ELF/load bytes differ: " + name)
    return address, body


def bss_object(elf, image, name, expected, reserved, *, low_limit=0x8C007800):
    address, size, kind, section = _linked_symbols(elf, {name}).get(name, (0, 0, 0, None))
    require(kind == 1 and size == expected and section is not None and not address & 31 and
            section[1] == 8 and section[2] & 7 == 3 and
            section[3] <= address < address + reserved <= section[3] + section[5] and
            LOW <= address < address + reserved <= low_limit and
            image["symbols"].get(name) == address,
            "Low control must occupy its typed BSS cache lines: " + name)
    require(not any(address + size <= value < address + reserved
                    for value in image["symbols"].values()),
            "Control cache-line padding is shared: " + name)
    return address


def literal(body, address, index, register):
    op, = struct.unpack_from("<H", body, 2 * index)
    require(op & 0xFF00 == 0xD000 | register << 8, "Wrong PC-relative load register")
    pc = address + 2 * index
    at = ((pc + 4) & ~3) + 4 * (op & 255)
    return struct.unpack("<I", region(body, at - address, 4, "typed hook literal"))[0]


def sequence(values, expected, start=0, end=None):
    end = len(values) if end is None else end
    return [i for i in range(start, end - len(expected) + 1)
            if values[i:i + len(expected)] == expected]


def startup_guard(ops, call):
    candidates = [i for i in range(call - 2) if ops[i] & 0xFF00 == 0xD300 and
                  ops[i + 1:i + 3] == [0x5031, 0x3040]]
    require(len(candidates) == 1, "Missing unique initializer argument guard")
    return candidates[0]


def inspect_dynamic_hook(elf, image, *, low_limit=0x8C007800):
    name = "_kui_vmu_observe_heap_hook"
    address, body = typed_function(elf, image, name, LOW)
    ops = words(body)
    cell = bss_object(elf, image, "_kui_vmu_native_heap_original", 32, 32, low_limit=low_limit)
    control = bss_object(elf, image, "_kui_vmu_observe_boot_control", 56, 64, low_limit=low_limit)
    require(not (cell < control + 64 and control < cell + 32), "Low control objects overlap")
    if ops[0] & 0xFF00 == 0xD200:
        mask_load = 0
        prefix = [0x0102, 0x221B, 0x420E, 0x2F16]
        require(ops[1:5] == prefix, "Entry SR must be captured and masked before stack write")
    else:
        mask_load = 1
        require(ops[0] == 0x0102 and ops[2:5] == [0x221B, 0x420E, 0x2F16],
                "Entry SR must be captured and masked before stack write")
    require(literal(body, address, mask_load, 2) == MASK and
            literal(body, address, 5, 2) == control + 0x20000000 and
            ops[6:8] == [0x5022, 0x8801] and ops[8] & 0xFF00 == 0x8B00 and
            ops[9:11] == [0xE002, 0x1202],
            "Entry must require ARMED and publish INITIALIZING through its exact P2 control")
    # The address is selected from the actual linked P2 cell, not a CLI label
    # or a hardcoded game's heap initializer. The extra dereference is vital.
    calls = [i for i in range(len(ops) - 3) if ops[i] & 0xFF00 == 0xD300 and
             ops[i + 1:i + 4] == [0x6332, 0x430B, 0x0009]]
    require(len(calls) == 1, "Require exactly one dynamic original-initializer call")
    call = calls[0]
    require(literal(body, address, call, 3) == cell + 0x20000000,
            "Original initializer must be loaded through the exact P2 target cell")
    require(call > 0 and ops[call - 1] == 0x4F07,
            "Original SR/SP must be restored immediately before original call")
    require(sequence(ops, [0x1245, 0x1256, 0x61F3, 0x7104, 0x1217,
                           0x012A, 0x1219, 0x129B, 0x12AC], end=call),
            "R4/R5, original SP/PR and callee-preserved registers were not captured")
    guard = startup_guard(ops, call)
    require(guard >= 0 and literal(body, address, guard, 3) == cell + 0x20000000 and
            ops[guard + 1:guard + 3] == [0x5031, 0x3040] and
            ops[guard + 4:guard + 6] == [0x5032, 0x3050] and
            ops[guard + 7:guard + 9] == [0x5033, 0x5129] and
            literal(body, address, guard + 9, 3) == 0x1FFFFFFF and
            ops[guard + 10:guard + 12] == [0x2139, 0x3010],
            "Initializer must bind actual R4/R5 and physical caller PR before its first write")
    branch_targets = []

    def refusal(index, opcode):
        require(ops[index] & 0xFF00 == opcode, "Mismatch must branch before original initializer")
        displacement = ops[index] & 255
        if displacement & 128:
            displacement -= 256
        target = address + 2 * index + 4 + 2 * displacement
        require(address <= target < address + len(body) and not target & 1 and
                ops[(target - address) // 2] == 0xE10B,
                "Initializer mismatch must select the terminal repeated-entry refusal")
        branch_targets.append(target)
    for index in (guard + 3, guard + 6, guard + 12):
        refusal(index, 0x8B00)
    refusal(8, 0x8B00)
    entry_sp = sequence(ops, [0x5127], start=guard + 13, end=call)
    require(len(entry_sp) == 1, "Captured startup stack must be checked before initializer")
    stack_start = entry_sp[0]
    marker=_linked_symbols(elf,{"__kui_native_owner_stack_ip_hook"}).get("__kui_native_owner_stack_ip_hook")
    stack_low=0x0C008100 if marker is not None else 0x0C00C100
    require(marker is None or marker[:3]==(1,0,0),"Invalid IP owner-stack capability marker")
    require(stack_low-52>(low_limit&0x1FFFFFFF)+32,
            "Inherited owner frame overlaps retained native reservation")
    low_loads = [i for i in range(stack_start, call) if ops[i] & 0xFF00 == 0xD000 and
                 literal(body, address, i, 0) == stack_low]
    high_loads = [i for i in range(stack_start, call) if ops[i] & 0xFF00 == 0xD000 and
                  literal(body, address, i, 0) == 0x0C00F400]
    lower, upper = (sequence(ops, [value], start=stack_start, end=call)
                    for value in (0x3102, 0x3106))
    require(len(low_loads) == len(high_loads) == len(lower) == len(upper) == 1 and
            low_loads[0] < lower[0] < high_loads[0] < upper[0] and
            0x2139 in ops[stack_start:lower[0]],
            "Startup stack physical range must be checked before original initializer")
    refusal(lower[0] + 1, 0x8B00)
    refusal(upper[0] + 1, 0x8900)
    require(len(set(branch_targets)) == 1, "Argument refusal branches diverge")
    first_push = next((i for i in range(call + 4, len(ops))
                       if ops[i] & 0xFF0F == 0x2F06 or ops[i] in (0x4F02, 0x4F12)), None)
    require(first_push is not None, "Missing inherited caller frame")
    post = ops[call + 4:first_push]
    require(0x0902 in post and 0x2A9B in post and 0x4A0E in post and
            post.index(0x0902) < post.index(0x2A9B) < post.index(0x4A0E),
            "Original SR must be captured and IMASK15/BL installed before any frame push")
    masks = [i for i in range(call + 4, first_push) if ops[i] & 0xFF00 == 0xDA00]
    require(len(masks) == 1 and literal(body, address, masks[0], 10) == MASK,
            "Post-call interrupt mask differs from linked contract")
    # Exactly 13 words: SR, MACH, MACL, R0..R8, original PR.
    publish = [i for i in range(call + 4, len(ops) - 1) if ops[i] & 0xF000 == 0xB000]
    require(len(publish) == 1 and ops[publish[0] + 1] == 0x0009 and
            ops[publish[0] - 3:publish[0]] == [0x61F3, 0x62F3, 0x7234],
            "Inherited caller frame must publish exactly 52 bytes")
    pushes = [op for op in ops[first_push:publish[0]]
              if op & 0xFF0F == 0x2F06 or op in (0x4F02, 0x4F12)]
    require(pushes == [0x2F96, 0x4F02, 0x4F12] +
            [0x2F06 | reg << 4 for reg in range(9)] + [0x2F06],
            "Inherited caller frame save order differs")
    instruction = publish[0]
    displacement = ops[instruction] & 0xFFF
    if displacement & 0x800:
        displacement -= 0x1000
    publisher = address + 2 * instruction + 4 + 2 * displacement
    require(publisher == address + len(body), "Frame publisher leaves the exact linked helper")
    publish_body = region(image["payload"], publisher - LOW, 28, "frame publisher")
    publish_ops = words(publish_body)
    require(publish_ops[1:11] == [0x2139, 0x2239, 0xE3E0, 0x2139, 0x01A3,
                                0x7120, 0x3122, 0x8BFB, 0x000B, 0x0009] and
            literal(publish_body, publisher, 0, 3) == 0xDFFFFFFF,
            "Inherited frame cache publication differs from bounded OCBP helper")
    restores = [0x6F83, 0x4F26] + [0x60F6 | reg << 8 for reg in range(8, -1, -1)] + \
        [0x4F16, 0x4F06, 0x4F07, 0x000B, 0x0009]
    require(len(sequence(ops, restores)) == 1,
            "Caller SP, PR, integer and MAC state must restore before original SR")
    require(0x591B in ops[publish[0]:] and 0x5A1C in ops[publish[0]:],
            "Original R9/R10 must be recovered from low control")
    return {"hook": f"0x{address:08x}", "hook_bytes": len(body),
            "dynamic_original_cell": f"0x{cell:08x}",
            "dynamic_original_p2": f"0x{cell + 0x20000000:08x}",
            "original_call": f"0x{address + 2 * (call + 2):08x}",
            "startup_argument_guard": {"entry_r4_offset": 4, "entry_r5_offset": 8,
                                       "physical_return_pr_offset": 12,
                                       "checked_before_initializer": True,
                                       "physical_stack_low": f"0x{stack_low:08x}",
                                       "physical_stack_high": "0x0c00f400"},
            "ip_owner_stack_capability": marker is not None,
            "inherited_frame_above_retained_reservation": True,
            "inherited_frame_bytes": 52,
            "mask_before_frame": True}


def mutate_word(elf, image, address, value, *, low_limit=0x8C007800):
    raw = bytearray(elf)
    symbols = _linked_symbols(elf, {"_kui_vmu_observe_heap_hook"})
    section = symbols["_kui_vmu_observe_heap_hook"][3]
    struct.pack_into("<H", raw, section[4] + address - section[3], value)
    return bytes(raw), inspect_elf(bytes(raw), LOW, low_limit)


def regression(elf, image, *, low_limit=0x8C007800):
    address, body = typed_function(elf, image, "_kui_vmu_observe_heap_hook", LOW)
    ops = words(body)
    call = next(i for i in range(len(ops) - 3) if ops[i] & 0xFF00 == 0xD300 and
                ops[i + 1:i + 4] == [0x6332, 0x430B, 0x0009])
    cases = [(address + 2 * (call + 1), 0x0009),
             (address + 2 * (call - 1), 0x0009)]
    for expected in (0x1245, 0x1256, 0x4A0E, 0x7234, 0x64F6, 0x420E):
        index = ops.index(expected)
        cases.append((address + 2 * index, 0x0009))
    op = ops[call]
    target_literal = ((address + 2 * call + 4) & ~3) + 4 * (op & 255)
    cases.append((target_literal, 0x3E10))  # old Toy fixed-target low half
    guard = startup_guard(ops, call)
    for index in (guard + 2, guard + 3, guard + 5, guard + 6, guard + 11, guard + 12):
        cases.append((address + 2 * index, 0x0009))
    for expected in (0x5127, 0x3102, 0x3106):
        cases.append((address + 2 * ops.index(expected), 0x0009))
    for at, value in cases:
        changed_elf, changed_image = mutate_word(elf, image, at, value, low_limit=low_limit)
        try:
            inspect_dynamic_hook(changed_elf, changed_image, low_limit=low_limit)
        except ValueError:
            continue
        raise ValueError("Dynamic initializer/frame mutation was accepted")
    mask, push = ops.index(0x4A0E), ops.index(0x2F96)
    changed_elf, changed_image = mutate_word(elf, image, address + 2 * mask, 0x2F96, low_limit=low_limit)
    changed_elf, changed_image = mutate_word(changed_elf, changed_image, address + 2 * push, 0x4A0E, low_limit=low_limit)
    try:
        inspect_dynamic_hook(changed_elf, changed_image, low_limit=low_limit)
    except ValueError:
        return len(cases) + 1
    raise ValueError("Relaxed push-before-interrupt-mask mutation was accepted")


def inspect(directory, *, self_test=False, objdump="sh-elf-objdump"):
    images, elfs = {}, {}
    for name, base, limit in (("resident-sci", LOW, 0x8C007800),
                              ("stage", HIGH, LIMIT), ("entry", layout.EXEC_ADDRESS, 0x8C112000)):
        elfs[name] = (directory / (name + ".elf")).read_bytes()
        images[name] = inspect_elf(elfs[name], base, limit)
        if name != "entry":
            require((directory / (name + ".bin")).read_bytes() == images[name]["payload"],
                    name + " objcopy bytes differ from linked ELF")
    resident, stage, entry = (images[name] for name in ("resident-sci", "stage", "entry"))
    check_bss(resident, LOW, "__retail_resident")
    check_bss(stage, HIGH, "__retail_stage")
    forbidden = ("_kui_vmu_maple_observer_", "_kui_vmu_observe_sd_lease",
                 "_kui_vmu_durable_", "_kui_vmu_persist_", "__vmu_observe_worker_blob_")
    for name in ("resident-sci", "stage"):
        require(not any(symbol.startswith(forbidden) for symbol in images[name]["symbols"]),
                "Allocator-only probe links a VMU worker/storage entry: " + name)
    for name in ("_kui_vmu_native_heap_discover", "_kui_vmu_native_heap_call_discover", "_kui_vmu_native_heap_fresh",
                 "_kui_vmu_native_heap_preallocate", "_kui_vmu_native_heap_postallocate",
                 "_kui_vmu_native_heap_lease_valid", "_kui_vmu_observe_stage_install"):
        typed_function(elfs["stage"], stage, name, HIGH)
    for transport in ("scif", "sci", "ide", "scia"):
        prefix = "__retail_resident_" + transport + "_blob_"
        begin, end = (stage["symbols"].get(prefix + suffix, 0) for suffix in ("start", "end"))
        require(region(stage["payload"], begin - HIGH, end - begin, "embedded resident") ==
                padded(resident["payload"]), "Stage embeds a different resident: " + transport)
    es = entry["symbols"]
    require(es.get("__retail_map") == layout.EXEC_ADDRESS + layout.MAP_OFFSET and
            es.get("__retail_stage_blob_start") == layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET and
            es.get("__retail_stage_blob_end") == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["memory_end"] == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["payload"][layout.STAGE_BLOB_OFFSET:] == padded(stage["payload"]),
            "Entry relocation/stage bytes differ from admitted ELF")
    require(not any(region(entry["payload"], layout.MAP_OFFSET, layout.MAP_BYTES, "blank manifest")),
            "Probe launch manifest must ship blank")
    require(region(entry["payload"], layout.HEADER_OFFSET, layout.HEADER_BYTES, "relocation header") ==
            layout.relocation_header(len(padded(stage["payload"])), False, True),
            "Probe must select the low synchronous native resident")
    module = (directory / "retail-maple-observe.kui").read_bytes()
    runtime = verify(module)
    require(module[64:] == entry["payload"] and
            runtime["memory_bytes"] == entry["memory_end"] - layout.EXEC_ADDRESS,
            "Runtime package differs from admitted entry ELF")
    instruction_counts = {}
    for name in images:
        disassembly = subprocess.check_output([objdump, "-d", str(directory / (name + ".elf"))], text=True)
        instruction_counts[name] = audit_instructions(name, disassembly)
    report = {"schema": "kui.native.heap.probe.artifact.v1", "runtime": runtime,
              "module_sha256": hashlib.sha256(module).hexdigest(),
              "resident_memory_end": f"0x{resident['memory_end']:08x}",
              "stage_memory_end": f"0x{stage['memory_end']:08x}",
              "dynamic_bootstrap": inspect_dynamic_hook(elfs["resident-sci"], resident),
              "integer_instruction_counts": instruction_counts,
              "vmuwriter_or_mapleworker_linked": False, "console_verified": False}
    if self_test:
        report["dynamic_bootstrap_mutation_rejections"] = regression(elfs["resident-sci"], resident)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--objdump", default="sh-elf-objdump")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = inspect(args.directory, self_test=args.self_test, objdump=args.objdump)
        result = json.dumps(report, indent=2) + "\n"
        if args.output:
            args.output.write_text(result)
        print(result, end="")
    except (OSError, ValueError, struct.error, subprocess.SubprocessError) as error:
        parser.exit(1, f"Native allocator probe audit refused: {error}\n")


if __name__ == "__main__":
    main()
