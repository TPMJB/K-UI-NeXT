#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the separately linked native Background VMU resident.

The ordinary reader's generic stack allowance is unchanged. This profile
proves its actual linked libgcc helper instead: four stack bytes, plus an
explicit 32-byte allowance, the existing 16-byte guard and 32-byte top gap.
Cold initialization/binding use the installer stack; the storage gateway and
the validated non-returning terminal entry use the worker's own stack.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

from check_loader_layout import inspect_elf
from check_retail_loader_layout import exact_profile_marker
from check_retail_instructions import audit as instruction_audit
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from vmu_maple_observe_symbols import sd_gateway_span
import check_retail_stack as graph
from sh4_delay_slots import validate_delay_slot

ROOT = Path(__file__).resolve().parents[1]
LOW, BOTTOM, TOP = 0x8C004000, 0x8C007BA0, 0x8C007D00
GUARD, TOP_GAP, EXTRA_MARGIN = 16, 32, 32
ROOTS = {"kui_retail_resident_dispatch": 0, "kui_retail_async_irq": 44,
         "kui_retail_menu_return": 0}
COLD = frozenset({"kui_vmu_native_background_init", "kui_vmu_observe_background_base_init",
                  "kui_vmu_native_background_manifest_bind", "kui_vmu_observe_sd_lease",
                  "kui_retail_native_async_loan_begin", "kui_retail_native_async_loan_end"})
CALLBACKS = {
    ("guest", "retail_gd.c"): ("native_background_map",),
    ("kui_retail_gd_dispatch", "retail_gd.c"): ("check_sectors",),
    ("byte", "sci_stream.c"): ("transfer",),
    ("select", "sci_stream.c"): ("select_card",),
    ("deliver", "retail_cursor.c"): ("write_out",),
    ("kui_retail_cursor_feed", "retail_cursor.c"): ("write_out",),
    ("write_out", "retail_async.c"): ("native_background_map",),
}
ESCAPES = {("kui_retail_menu_return", "retail_resident.c"): "boot-ROM menu; does not return",
           ("kui_vmu_observe_low_failure", "vmu_native_background_resident.c"):
               "validated worker terminal; private stack switch separately audited"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def typed(image, name, kind, *, size=None, writable=False):
    record = image[4].get(name)
    require(record is not None, "missing linked symbol: " + name)
    address, count, actual, owner = record
    require(actual == kind and count > 0 and owner < len(image[3]),
            "missing typed linked symbol: " + name)
    section = image[3][owner]
    require(section[2] & 2 and section[3] <= address < address + count <= section[3] + section[5],
            "typed symbol escapes its allocated section: " + name)
    require(bool(section[2] & 4) == (kind == 2), "wrong symbol execution permission: " + name)
    if size is not None:
        require(count == size, "wrong typed symbol size: " + name)
    if writable:
        require(section[1] == 8 and section[2] & 3 == 3, "pointer workspace must be zero-fill writable BSS")
    return address, count


def disassemble(path):
    tool = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    return subprocess.check_output([tool, "-d", str(path)], text=True)


def rows(disassembly):
    result = {}
    pattern = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+(\S+)(?:\s+(.*))?$")
    for line in disassembly.splitlines():
        found = pattern.match(line)
        if not found:
            continue
        address, lo, hi, mnemonic, operands = found.groups()
        result[int(address, 16)] = (int(lo, 16) | int(hi, 16) << 8, mnemonic,
                                     (operands or "").split("!", 1)[0].strip())
    return result


def library_cfg(decoded, address, count):
    """Exhaust every branch of the actual integer libgcc division helper.

    Internal helpers precede its public symbol. No register/memory value is
    abstracted into a control transfer: only direct branches and RTS using
    the exact symbolic PR are admitted. Every path must return with its
    saved PR and balanced stack. A cycle, unknown operation or SP alias
    rejects the proof rather than acquiring a margin waiver.
    """
    lower, upper, sentinel = address - 64, address + count, -1
    allowed = {"extu.w", "cmp/eq", "div0u", "swap.w", "shlr16", "shll16", "div1",
               "xtrct", "rotcl", "mov", "nop"}
    visited, active = {}, set()
    maximum, exits = 0, 0

    def plain(pc, pr, stack):
        nonlocal maximum
        require(pc in decoded and lower <= pc < upper, "division helper leaves its bounded code")
        op, mnemonic, operands = decoded[pc]
        if op == 0x4F22:
            require(mnemonic == "sts.l" and operands.replace(" ", "") == "pr,@-r15",
                    "division helper PR push differs")
            stack += (pr,)
            maximum = max(maximum, len(stack) * 4)
        elif op == 0x4F26:
            require(mnemonic == "lds.l" and operands.replace(" ", "") == "@r15+,pr" and stack,
                    "division helper has an unmatched PR pop")
            pr, stack = stack[-1], stack[:-1]
        else:
            require(mnemonic in allowed and not re.search(r"\b(?:r15|pr|sr|vbr|gbr|ssr|spc|dbr)\b|@", operands),
                    "unmodeled division helper instruction: " + mnemonic + " " + operands)
        return pr, stack

    def visit(pc, pr, stack):
        nonlocal exits
        state = pc, pr, stack
        require(state not in active, "cyclic division helper control flow")
        if state in visited:
            return
        active.add(state)
        require(len(active) <= 512, "division helper path exceeds its exhaustive proof bound")
        require(pc in decoded and lower <= pc < upper, "division helper branches outside its bounded code")
        op, mnemonic, _ = decoded[pc]
        if op == 0x000B or op >> 12 in (0xA, 0xB):
            if op == 0x000B:
                target = pr
            else:
                displacement = op & 4095
                displacement -= 4096 if displacement & 2048 else 0
                target = pc + 4 + 2 * displacement
                if op >> 12 == 0xB:
                    pr = pc + 4
            require(pc + 2 in decoded, f"division helper lacks its delay slot at {pc:08x}")
            validate_delay_slot(decoded[pc + 2][0], pc)
            pr, stack = plain(pc + 2, pr, stack)
            if target == sentinel:
                require(not stack and pr == sentinel, "division helper returns with an unbalanced stack/PR")
                exits += 1
            else:
                visit(target, pr, stack)
        elif op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
            require(mnemonic in ("bt", "bf", "bt.s", "bf.s"), "division branch decode differs")
            displacement = op & 255
            displacement -= 256 if displacement & 128 else 0
            delayed = op >> 8 in (0x8D, 0x8F)
            if delayed:
                require(pc + 2 in decoded, "division conditional lacks its delay slot")
                validate_delay_slot(decoded[pc + 2][0], pc)
                pr, stack = plain(pc + 2, pr, stack)
            visit(pc + 4 + 2 * displacement, pr, stack)
            visit(pc + (4 if delayed else 2), pr, stack)
        else:
            pr, stack = plain(pc, pr, stack)
            visit(pc + 2, pr, stack)
        active.remove(state)
        visited[state] = True
        require(len(visited) <= 512, "division helper path proof exceeds its bound")

    visit(address, sentinel, ())
    require(exits and maximum == 4, "division helper must have a proved four-byte maximum")
    return {"symbol": "___udivsi3", "stack_bytes": maximum, "abstract_states": len(visited),
            "balanced_return_paths": exits, "internal_helper_window_bytes": 64,
            "unknown_instruction_or_cycle_rejected": True}


def stack_paths(frames, calls):
    callbacks, escapes, reached = set(), set(), set()
    memo = {}

    def resolve(function):
        if function in frames:
            return [function]
        alternatives = [name for name in frames if graph.base(name) == graph.base(function)]
        require(alternatives, "call has no emitted stack frame: " + function)
        return alternatives

    def depth(function, path=()):
        require(graph.base(function) not in COLD, "installer/worker path reached the private GD stack: " + function)
        require(function not in path, "recursive private GD call graph: " + " -> ".join(path + (function,)))
        if function in memo:
            return memo[function]
        if function not in frames:
            if graph.base(function) in graph.LIBRARY:
                return 0, [function + "(separately bounded library)"]
            return max((depth(target, path) for target in resolve(function)), key=lambda answer: answer[0])
        reached.add(function)
        best = 0, []
        for target, label in calls.get(function, []):
            if target == "__indirect_call":
                key = graph.base(function), label.split(":", 1)[0]
                if key in ESCAPES:
                    escapes.add(key)
                    continue
                require(key in CALLBACKS, "unadmitted private GD callback: " + function + " at " + label)
                targets = [node for callback in CALLBACKS[key] for node in resolve(callback)]
                callbacks.add((function, label, tuple(targets)))
            else:
                targets = [target]
            for callee in targets:
                answer = depth(callee, path + (function,))
                if answer[0] > best[0]:
                    best = answer
        memo[function] = frames[function] + best[0], [function + "(" + str(frames[function]) + ")"] + best[1]
        return memo[function]

    result = {}
    for root, assembly in ROOTS.items():
        candidates = [name for name in frames if graph.base(name) == root]
        require(len(candidates) == 1, "missing or duplicated private GD root: " + root)
        total, path = depth(candidates[0])
        result[root] = {"c_bytes": total, "assembly_entry_bytes": assembly,
                        "bytes": total + assembly, "path": path}
    require(any(graph.base(name) == "native_background_map" for name in reached),
            "guarded Background mapping callback was not walked")
    require(set(ESCAPES) <= escapes, "missing independently classified non-returning callback")
    return result, sorted(callbacks), sorted(escapes)


def c_body(text, signature):
    start = text.index(signature)
    begin = text.index("{", start)
    depth, end = 1, begin + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def source_contract():
    paths = ("include/kui/vmu_native_background.h", "include/kui/vmu_native_async_loan.h",
             "src/loader/vmu_native_background_resident.c", "src/loader/retail_resident.c",
             "src/loader/retail_async.c", "src/loader/retail_async.h", "src/loader/retail_resident.S",
             "src/loader/sci_sd_bus.c", "src/loader/vmu_native_passive_stage.inc",
             "src/loader/vmu_native_heap_probe_stage.c", "src/loader/retail_stage.c")
    sources = {path: (ROOT / path).read_text() for path in paths}
    constants = {}
    for name, value in re.findall(r"#define (KUI_VMU_NATIVE_BACKGROUND_\w+) UINT32_C\((0x[0-9a-fA-F]+|\d+)\)",
                                  sources[paths[0]]):
        constants[name] = int(value, 0)
    expected = {"KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES": 0x20000,
                "KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES": 0x30000,
                "KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES": 1020,
                "KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET": 0x20400,
                "KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES": 0x760}
    require(all(constants.get(key) == value for key, value in expected.items()),
            "Background workspace geometry changed")
    wrapper = sources["src/loader/vmu_native_background_resident.c"]
    guard = wrapper[:wrapper.index("#endif")]
    for flag in ("KUI_VMU_NATIVE_BACKGROUND", "KUI_VMU_NATIVE_PERSISTENT_PROBE", "KUI_RETAIL_ASYNC",
                 "KUI_RETAIL_LOW_RESIDENT", "KUI_GENERAL_SCI_RESUME_FIX", "KUI_RETAIL_NATIVE_DRIVE_STATE",
                 "KUI_RETAIL_NATIVE_DISC_CHECK"):
        require("!" + flag in guard, "Background marker lacks its mandatory source gate: " + flag)
    terminal = c_body(wrapper, "void kui_vmu_observe_low_failure(void) {")
    require(terminal.count("c->terminal") == 1 and "if(native_resident_terminal(c))" in terminal and
            'for(;;) __asm__ volatile("nop");' in terminal and
            "retail_display_restore(&display);" in terminal,
            "terminal escape/fallback contract changed")
    stage = sources["src/loader/vmu_native_passive_stage.inc"]
    fragments = ("memcpy((void *)(uintptr_t)(map_address+UINT32_C(0x20000000)),&manifest,",
                 "memcpy((void *)(uintptr_t)(region_address+UINT32_C(0x20000000)),native_background_boot_region,",
                 "kui_native_passive_publish(region_address,region_address+KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES);",
                 "if(bind_map(&manifest,(const void *)(uintptr_t)map_address)!=1)")
    positions = [stage.index(fragment) for fragment in fragments]
    require(positions == sorted(positions), "stage must copy map/region and publish executable vectors before binding")
    before_map = stage[:positions[0]]
    before_region = stage[positions[0]:positions[1]]
    require('volatile("ocbp @%0"' in before_map and 'volatile("ocbp @%0"' in before_region,
            "P1 lines must be purged before both P2 copies")
    heap = sources["src/loader/vmu_native_heap_probe_stage.c"]
    require("mode==KUI_VMU_NATIVE_MODE_CONTROL || mode==KUI_VMU_NATIVE_MODE_LEASE_ONLY" in heap and
            'stopped("BACKGROUND VMU REQUIRES LIVE WORKER RESERVATION"' in heap,
            "retained workspaces require the live native allocation")
    retail_stage = sources["src/loader/retail_stage.c"]
    require("int initialized=init(&manifest,&card,firmware,&display,native_background_boot_region);" in retail_stage and
            "const struct kui_retail_storage *,uint32_t,const struct retail_display_state *,void *);" in retail_stage,
            "stage must pass the protected workspace as the native initializer's fifth argument")
    return {"geometry": {key.removeprefix("KUI_VMU_NATIVE_BACKGROUND_").lower(): value
                          for key, value in expected.items()},
            "map_and_region_p2_copy_before_vector_publication_before_bind": True,
            "control_and_lease_only_modes_refused": True,
            "stage_supplies_fifth_workspace_argument": True,
            "sha256": {path: digest(text.encode()) for path, text in sources.items()}}


def stage_contract(directory, resident):
    path = directory / "stage.elf"
    if not path.is_file():
        return {"available": False, "complete_module_audited": False}
    data = path.read_bytes()
    inspected = inspect_elf(data, 0x8CE00000, 0x8CFE0000)
    image = read_elf(path)
    require(exact_profile_marker(data, "__kui_vmu_native_background_stage"), "missing Background stage marker")
    workspace, _ = typed(image, "_native_background_boot_region", 1, size=0x760, writable=True)
    # Stage wire decoding owns 160 slots. The validated native Background map
    # retains only its first 64 slots (1020 bytes) in the already proved lease.
    manifest, _ = typed(image, "_manifest", 1, size=2172, writable=True)
    installer, _ = typed(image, "_native_installer_stack", 1, size=0x10000, writable=True)
    typed(image, "_native_passive_run", 2)
    # This authored assembly entry is an executable NOTYPE label; its exact
    # cache-publication byte sequence is pinned by the primary native audit.
    publisher = image[4].get("_kui_native_passive_publish")
    require(publisher is not None and publisher[2] == 0 and publisher[3] < len(image[3]) and
            image[3][publisher[3]][2] & 6 == 6 and
            image[3][publisher[3]][3] <= publisher[0] <
            image[3][publisher[3]][3] + image[3][publisher[3]][5],
            "missing executable stage cache-publication entry")
    require(not workspace & 31 and not manifest & 3 and
            not installer & 31 and (workspace + 0x760 <= manifest or manifest + 2172 <= workspace),
            "stage map/vector workspaces overlap or have wrong alignment")
    generated = (directory / "vmu_maple_observe_symbols.h").read_text()
    bindings = re.findall(r"^#define KUI_VMU_NATIVE_BACKGROUND_BIND (0x[0-9a-fA-F]+)[uU]?$", generated, re.M)
    binder = resident["symbols"]["_kui_vmu_native_background_manifest_bind"]
    require(len(bindings) == 1 and int(bindings[0], 16) == binder,
            "stage generated binder address differs from the actual low binder")
    _, installer_code, _ = symbol_bytes(image, "_native_passive_run", 2)
    require(struct.pack("<I", binder) in installer_code,
            "actual installer lacks the admitted low binder constant")
    embedded = {}
    for transport in ("scif", "sci", "ide", "scia"):
        begin = image[4].get("__retail_resident_" + transport + "_blob_start")
        end = image[4].get("__retail_resident_" + transport + "_blob_end")
        require(begin is not None and end is not None and end[0] - begin[0] == len(resident["payload"]),
                "stage embedded resident size differs: " + transport)
        offset = begin[0] - 0x8CE00000
        require(inspected["payload"][offset:offset + len(resident["payload"])] == resident["payload"],
                "stage embedded bytes differ from the inspected Background resident: " + transport)
        embedded[transport] = hex(begin[0])
    return {"available": True, "complete_module_audited": False,
            "stage_sha256": digest(data), "workspace": hex(workspace), "workspace_bytes": 0x760,
            "map": hex(manifest), "stage_map_object_bytes": 2172, "retained_map_prefix_bytes": 1020,
            "installer_stack": hex(installer), "installer_stack_bytes": 0x10000,
            "generated_binder": hex(binder), "embedded_resident_copies": embedded,
            "memory_end": hex(inspected["memory_end"]),
            "scope": "stage bounds/profile/workspaces; full envelope and worker audited separately"}


def check(directory, *, self_test=False, require_stage=False):
    directory = Path(directory)
    path = directory / "resident-sci.elf"
    data = path.read_bytes()
    inspected = inspect_elf(data, LOW, BOTTOM)
    image = read_elf(path)
    symbols = inspected["symbols"]
    require(exact_profile_marker(data, "__kui_vmu_native_background_resident"),
            "missing exact repaired native Background resident profile")
    require(symbols.get("__retail_hook_stack_bottom") == BOTTOM and symbols.get("__retail_hook_stack") == TOP,
            "native Background must retain the exact 352-byte stack and guard boundaries")
    require(symbols.get("__retail_resident_bss_end") == inspected["memory_end"] <= BOTTOM and
            LOW + len(inspected["payload"]) <= symbols.get("__retail_resident_bss_begin", 0),
            "resident load/BSS boundaries changed")
    pointer, _ = typed(image, "_kui_retail_async_region_storage", 1, size=4, writable=True)
    require(not pointer & 3 and symbols["__retail_resident_bss_begin"] <= pointer < pointer + 4 <= BOTTOM,
            "relocatable region pointer escapes low BSS")
    require("_kui_retail_async_region" not in symbols, "Background retained a fixed low vector region")
    for function in (*ROOTS, "kui_vmu_native_background_init", "kui_vmu_native_background_manifest_bind",
                     "kui_vmu_observe_low_failure", "native_background_map"):
        typed(image, "_" + function, 2)
    # Reuse the existing exact one-function section proof: SH relaxation may
    # retain a larger declared ST_SIZE than the allocated gateway span.
    lease, lease_bytes, declared_lease_bytes = sd_gateway_span(data, inspected)
    require(symbols.get("__vmu_sd_lease_begin") <= lease < lease + lease_bytes <= symbols.get("__vmu_sd_lease_end"),
            "worker-only storage gateway escapes its exported section")
    require(struct.unpack_from("<I", inspected["payload"], 32)[0] ==
            symbols["_kui_vmu_native_background_init"],
            "low entry must name the separate five-argument Background initializer")
    binary = directory / "resident-sci.bin"
    if binary.is_file():
        require(binary.read_bytes() == inspected["payload"], "resident binary differs from the inspected ELF")
    disassembly = disassemble(path)
    instructions = instruction_audit("native-background-resident", disassembly)
    helper_names = [name for name, record in image[4].items() if record[2] == 2 and name.startswith("___")]
    require(helper_names == ["___udivsi3"], "unmodeled compiler-library functions: " + repr(helper_names))
    helper_address, helper_bytes = typed(image, "___udivsi3", 2)
    library = library_cfg(rows(disassembly), helper_address, helper_bytes)
    reports = sorted(directory.glob("resident-sci.elf.ltrans*.ci"))
    if not reports:
        reports = sorted((directory / "resident/lto").glob("*.ci"))
    require(reports, "missing actual LTO call-graph reports")
    frames, calls = graph.load(reports)
    paths, callbacks, escapes = stack_paths(frames, calls)
    worst = max(item["bytes"] for item in paths.values())
    total = worst + library["stack_bytes"] + EXTRA_MARGIN + GUARD + TOP_GAP
    require(total <= TOP - BOTTOM,
            f"Background private stack needs {total} bytes including its guard/top gap; available {TOP-BOTTOM}")
    stage = stage_contract(directory, inspected)
    require(not require_stage or stage["available"], "complete-profile audit requires the linked stage")
    source = source_contract()
    result = {"schema": "kui.native.background.audit.v1", "console_execution_verified": False,
              "resident_sha256": digest(data), "payload_bytes": len(inspected["payload"]),
              "memory_end": hex(inspected["memory_end"]), "low_limit": hex(BOTTOM),
              "region_pointer": {"address": hex(pointer), "bytes": 4, "fixed_region_absent": True},
              "worker_stack_gateway": {"address": hex(lease), "allocated_bytes": lease_bytes,
                                       "declared_function_bytes": declared_lease_bytes},
              "profile": {"native_background": True, "sci_resume_fix": True,
                          "logical_drive_state": True, "mounted_disc_check": True},
              "linked_integer_instructions": instructions,
              "private_stack": {"available_bytes": TOP - BOTTOM, "worst_c_and_entry_bytes": worst,
                                "linked_library": library, "additional_margin_bytes": EXTRA_MARGIN,
                                "generic64_byte_library_margin_used": False,
                                "guard_bytes": GUARD, "top_gap_bytes": TOP_GAP,
                                "conservative_total_bytes": total, "remaining_bytes": TOP - BOTTOM - total,
                                "worst_total_bytes": total,
                                "paths": paths, "callback_sites": len(callbacks),
                                "nonreturning_boundaries": [{"function": key[0], "source": key[1],
                                                             "contract": ESCAPES[key]} for key in escapes],
                                "installer_and_worker_roots_excluded": sorted(COLD)},
              "source_contract": source, "stage": stage}
    if self_test:
        rejected = []
        def refuse(name, callback):
            try:
                callback()
            except (ValueError, AssertionError):
                rejected.append(name)
            else:
                raise ValueError("Background audit accepted negative mutation: " + name)
        altered = dict(frames)
        altered["native_background_map"] += 128
        def oversized():
            checks, _, _ = stack_paths(altered, calls)
            need = max(item["bytes"] for item in checks.values()) + 4 + EXTRA_MARGIN + GUARD + TOP_GAP
            require(need <= TOP - BOTTOM, "oversized private stack")
        refuse("guarded_map_stack_growth", oversized)
        unknown = {name: list(edges) for name, edges in calls.items()}
        unknown["kui_retail_async_irq"].append(("__indirect_call", "unknown_callback.c:1:1"))
        refuse("unknown_irq_callback", lambda: stack_paths(frames, unknown))
        escaped = {name: list(edges) for name, edges in calls.items()}
        escaped["kui_retail_async_irq"].append(("kui_vmu_observe_sd_lease", "retail_async.c:1:1"))
        refuse("worker_gateway_on_gd_stack", lambda: stack_paths(frames, escaped))
        recursive = {name: list(edges) for name, edges in calls.items()}
        recursive["kui_retail_async_irq"].append(("kui_retail_async_irq", "retail_async.c:1:1"))
        refuse("recursive_irq_path", lambda: stack_paths(frames, recursive))
        decoded = rows(disassembly)
        mutated = dict(decoded)
        mutated[helper_address + 2] = (0x60F3, "mov", "r15,r0")
        refuse("hidden_library_stack_alias", lambda: library_cfg(mutated, helper_address, helper_bytes))
        mutated = dict(decoded)
        mutated[helper_address] = (0x0009, "nop", "")
        refuse("unbalanced_library_pop", lambda: library_cfg(mutated, helper_address, helper_bytes))
        mutated = dict(decoded)
        mutated[helper_address + 2] = (0xAFFE, "bra", "self")
        refuse("cyclic_library_helper", lambda: library_cfg(mutated, helper_address, helper_bytes))
        mutated = dict(decoded)
        mutated[helper_address + 2] = (0xA7FF, "bra", "outside")
        refuse("library_branch_outside_window", lambda: library_cfg(mutated, helper_address, helper_bytes))
        result["self_test"] = {"refused_mutations": rejected}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--require-stage", action="store_true")
    options = parser.parse_args()
    print(json.dumps(check(options.directory, self_test=options.self_test, require_stage=options.require_stage), indent=2))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, AssertionError) as error:
        sys.exit("Native Background audit failed: " + str(error))
