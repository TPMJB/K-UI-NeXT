#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the actual relocated native persistent worker, low SCI loan and source.

Integer gateway/ABI models establish the authored control transfers; they do
not establish console execution or universal game/reset compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import retail_package as layout
import check_retail_loader_layout as resident_layout
from check_vmu_native_heap_probe import (
    LOW, HIGH, LIMIT, typed_function, check_bss, inspect_dynamic_hook, regression,
)
from check_vmu_native_readonly import model_hooks, model_mutations
from check_vmu_native_worker_gateway import (
    P1, P2, literal_aware_instructions, model_gateway, read_elf, symbol_bytes,
)
from check_loader_layout import inspect_elf, padded, region
from check_retail_instructions import audit as instruction_audit
from runtime_package import verify
from package_vmu_maple_observe import (linked_stack_rows, _function_base, check_pio_callgraph, linked_function_names)
from check_retail_stack import EDGE, NODE, load as load_graph
from vmu_maple_observe_symbols import sd_gateway_span
from check_vmu_pio_write import check_pio_entry
from vmu_native_relocations import generate

ROOT = Path(__file__).resolve().parents[1]
SPAN = 0x20000


def require(value, message):
    if not value:
        raise ValueError(message)


def inspect_base(directory, *, self_test=False, objdump="sh-elf-objdump"):
    """Reuse exact bootstrap/layout checks while admitting the one SCI loan."""
    images, elfs = {}, {}
    for name, base, limit in (("resident-sci", LOW, 0x8C007800),
                              ("stage", HIGH, LIMIT), ("entry", layout.EXEC_ADDRESS, 0x8C112000)):
        elfs[name] = (directory / (name + ".elf")).read_bytes()
        images[name] = inspect_elf(elfs[name], base, limit)
        if name != "entry":
            require((directory / (name + ".bin")).read_bytes() == images[name]["payload"],
                    name + " binary differs from ELF")
    resident, stage, entry = (images[name] for name in ("resident-sci", "stage", "entry"))
    check_bss(resident, LOW, "__retail_resident")
    check_bss(stage, HIGH, "__retail_stage")
    for name in ("resident-sci", "stage"):
        require(not any(symbol.startswith(("_kui_vmu_maple_observer_", "__vmu_observe_worker_blob_"))
                        for symbol in images[name]["symbols"]), "old fixed-address VMU worker linked")
    require(not any(symbol.startswith(("_kui_vmu_durable_", "_kui_vmu_persist_", "_kui_sci_sd_write",
                                       "_kui_sci_sd_sync")) for symbol in resident["symbols"]),
            "low reader contains a high-only persistent writer")
    for name in ("_kui_vmu_native_heap_discover", "_kui_vmu_native_heap_call_discover", "_kui_vmu_native_heap_fresh",
                 "_kui_vmu_native_heap_preallocate", "_kui_vmu_native_heap_postallocate",
                 "_kui_vmu_native_heap_lease_valid", "_kui_vmu_observe_stage_install",
                 "_kui_vmu_persist_stage_admit", "_kui_vmu_persist_stage_prepare",
                 "_kui_vmu_persist_stage_handoff_matches"):
        typed_function(elfs["stage"], stage, name, HIGH)
    for transport in ("scif", "sci", "ide", "scia"):
        prefix = "__retail_resident_" + transport + "_blob_"
        begin, end = (stage["symbols"].get(prefix + suffix, 0) for suffix in ("start", "end"))
        require(region(stage["payload"], begin - HIGH, end - begin, "embedded resident") ==
                padded(resident["payload"]), "stage embeds different resident: " + transport)
    es = entry["symbols"]
    require(es.get("__retail_map") == layout.EXEC_ADDRESS + layout.MAP_OFFSET and
            es.get("__retail_stage_blob_start") == layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET and
            es.get("__retail_stage_blob_end") == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["memory_end"] == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["payload"][layout.STAGE_BLOB_OFFSET:] == padded(stage["payload"]),
            "entry relocation/stage bytes differ from ELF")
    require(not any(region(entry["payload"], layout.MAP_OFFSET, layout.MAP_BYTES, "blank manifest")),
            "launch manifest must ship blank")
    require(region(entry["payload"], layout.HEADER_OFFSET, layout.HEADER_BYTES, "relocation header") ==
            layout.relocation_header(len(padded(stage["payload"])), False, True),
            "persistent probe must select low synchronous native reader")
    module = (directory / "retail-maple-observe.kui").read_bytes()
    runtime = verify(module)
    require(module[64:] == entry["payload"] and runtime["memory_bytes"] == entry["memory_end"] - layout.EXEC_ADDRESS,
            "module differs from actual entry ELF")
    instructions = {}
    for name in images:
        disassembly = subprocess.check_output([objdump, "-d", str(directory / (name + ".elf"))], text=True)
        instructions[name] = instruction_audit(name, disassembly)
    gateway = check_loan_gateway(directory, resident, elfs["resident-sci"])
    report = {"schema": "kui.native.persistent.bootstrap.v1", "runtime": runtime,
              "module_sha256": hashlib.sha256(module).hexdigest(),
              "resident_memory_end": f"0x{resident['memory_end']:08x}",
              "stage_memory_end": f"0x{stage['memory_end']:08x}",
              "dynamic_bootstrap": inspect_dynamic_hook(elfs["resident-sci"], resident),
              "integer_instruction_counts": instructions, "sd_lease_gateway": gateway,
              "old_fixed_worker_linked": False, "console_verified": False}
    if self_test:
        report["dynamic_bootstrap_mutation_rejections"] = regression(elfs["resident-sci"], resident)
    return report


def check_loan_gateway(directory, resident, elf):
    address, size, declared = sd_gateway_span(elf, resident)
    reports = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    if not reports:
        reports = sorted(directory.glob("resident-sci.elf*.ci"))
    nodes = set()
    require(reports, "missing final-LTO SCI loan callgraph proof")
    for report in reports:
        data = report.read_text()
        nodes.update(_function_base(title) for title, _ in NODE.findall(data))
        require(not any(_function_base(target) == "kui_vmu_observe_sd_lease"
                        for _, target, _ in EDGE.findall(data)), "resident call enters worker-stack-only loan")
    require("kui_vmu_observe_sd_lease" in nodes, "final-LTO graph omits SCI loan")
    for offset in range(0, len(resident["payload"]) - 3, 4):
        at = LOW + offset
        if address <= at < address + size:
            continue
        value = struct.unpack_from("<I", resident["payload"], offset)[0]
        if value & 0xFF000000 in (0x0C000000, 0x8C000000, 0xAC000000):
            canonical = value & 0x1FFFFFFF | 0x80000000
            require(not address <= canonical < address + size, "low resident literal enters worker-stack-only loan")
    return {"address": f"0x{address:08x}", "bytes": size, "declared_symbol_bytes": declared,
            "incoming_resident_edges": 0, "resident_pointer_references": 0,
            "callgraph_reports": [report.name for report in reports]}


def check_pio_writers(directory, worker):
    results = {}
    for image_name, image, base, folder, adapter in (
            ("worker", worker, P2, "native", "durable_card_pio_write"),
            ("stage", read_elf(directory / "stage.elf"), HIGH, "stage", "native_persist_pio_write")):
        elf = image[0]
        binary = (directory / ("native-worker.bin" if image_name == "worker" else "stage.bin")).read_bytes()
        address, body, _ = symbol_bytes(image, "_" + adapter, 2)
        writer, _, _ = symbol_bytes(image, "_kui_sci_sd_write", 2)
        report = check_pio_entry({"payload": binary}, address, len(body), writer, base=base)
        report.update(check_pio_callgraph(directory / folder, adapter, linked_function_names(elf, {})))
        results[image_name] = report
    return results


def check_source(directory, runtime):
    raw = (directory / "entry.compile.json").read_bytes()
    record = json.loads(raw)
    require(isinstance(record, dict) and isinstance(record.get("commit"), str) and
            re.fullmatch(r"[0-9a-f]{40}", record["commit"]) and type(record.get("source_dirty")) is bool and
            record.get("elf_sha256") == hashlib.sha256((directory / "entry.elf").read_bytes()).hexdigest() and
            runtime["build"] == record["commit"][:12], "source provenance differs from compiled entry")
    return {"commit": record["commit"], "source_dirty": record["source_dirty"],
            "compile_provenance_sha256": hashlib.sha256(raw).hexdigest(),
            "elf_sha256": record["elf_sha256"]}


def worker_stack_paths(directory, worker, resident):
    """Walk source-qualified emitted graphs with finite authored callbacks."""
    report_sets = {"worker": sorted((directory / "native").rglob("*.ci")),
                   "low": sorted(directory.glob("resident-sci.elf*.ltrans*.ci"))}
    if not report_sets["low"]:
        report_sets["low"] = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    images = {"worker": worker[4], "low": resident["symbols"]}
    frames, calls, origins = {}, {}, {}
    for domain, reports in report_sets.items():
        require(reports, "missing emitted callgraph: " + domain)
        f, c = load_graph(reports, qualified=True, scope=domain)
        live = {_function_base(name) for name in images[domain]}
        f = {name: value for name, value in f.items() if _function_base(name) in live}
        frames.update(f); calls.update(c)
        for report in reports:
            for title, label in NODE.findall(report.read_text()):
                pieces = label.split(r"\n")
                if len(pieces) >= 2:
                    origins[domain + ":" + title] = Path(pieces[1].rsplit(":", 2)[0]).name
    by_base = {}
    for name in frames:
        by_base.setdefault((name.split(":", 1)[0], _function_base(name)), []).append(name)
    def resolve(domain, function, owner=None):
        exact = domain + ":" + function
        found = [exact] if exact in frames else by_base.get((domain, _function_base(function)), [])
        if owner is not None:
            found = [name for name in found if origins.get(name) == owner]
        require(found, "missing emitted callback frame: " + domain + ":" + function)
        return found
    def high(*names):
        return [node for name in names for node in resolve("worker", name)]
    def bus(*names):
        return [node for name in names for node in resolve("low", name, "sci_sd_bus.c")]
    memo, paths, visiting, indirect_sites, null_sites = {}, {}, set(), set(), set()
    def indirect(name, label):
        domain, function = name.split(":", 1)[0], _function_base(name)
        source = Path(label.split(":", 1)[0]).name
        if domain == "low":
            require((function, source) == ("kui_vmu_observe_sd_lease", "vmu_sd_lease.inc"),
                    "unexpected low callback from worker stack: " + name + " at " + label)
            return high("kui_vmu_native_persistent_sd_transaction")
        fixed = {
            "vmu_durable.c": ("persistent_lease",),
            "vmu_emulation.c": ("durable_card_read", "durable_card_commit"),
            "vmu_native_heap.c": ("read_heap",),
            "vmu_native_layout.c": ("read_value",),
            "vmu_native_lifetime.c": ("read_heap",),
            "vmu_native_runtime.c": ("boundary_read",),
            "vmu_store.c": ("durable_card_sector_read", "durable_card_sector_write", "durable_card_sector_sync"),
            "vmu_readonly_overlay.c": ("persistent_admit", "persistent_handler"),
        }
        owners = {
            "vmu_durable.c": {"durable_card_execute"},
            "vmu_emulation.c": {"kui_vmu_emulation_handle"},
            "vmu_native_heap.c": {"read_is", "kui_vmu_native_heap_lease_valid"},
            "vmu_native_layout.c": {"kui_vmu_native_layout_bind_checked"},
            "vmu_native_lifetime.c": {"kui_vmu_native_lifetime_release_safe_ranges"},
            "vmu_native_runtime.c": {"kui_vmu_native_runtime_check"},
            "vmu_store.c": {"write_sync", "kui_vmu_store_open", "kui_vmu_store_read", "kui_vmu_store_sync"},
            "vmu_readonly_overlay.c": {"kui_vmu_readonly_overlay_prepare", "kui_vmu_readonly_overlay_complete"},
        }
        if source in fixed:
            require(function in owners[source], "unexpected callback source owner: " + name)
            return high(*fixed[source])
        if (function, source) == ("persistent_lease", "vmu_native_passive.c"):
            return resolve("low", "kui_vmu_observe_sd_lease")
        if source == "sd_reader.c":
            targets = {"transfer": ("transfer",), "ticks": ("ticks",), "release": ("select_card",),
                       "read_payload": ("transfer_block",), "kui_loader_sd_read": ("select_card",)}
            require(function in targets, "unexpected initialized-card reader callback: " + name)
            return bus(*targets[function])
        if source == "vmu_backing.c":
            require(function in ("byte", "wait_byte", "kui_vmu_sd_identity_read"),
                    "unexpected backing callback: " + name)
            return bus("transfer", "ticks", "select_card")
        if source == "sci_sd_storage.c":
            if function == "kui_sci_sd_write" and label.endswith(":83:17"):
                # Actual native PIO adapters independently prove this callback
                # NULL at the sole writer entry; this optional branch is dead.
                null_sites.add((name, label)); return []
            return bus("transfer", "ticks", "select_card")
        raise ValueError("unadmitted indirect worker call: " + name + " at " + label)
    def depth(name):
        if name in memo:
            return memo[name]
        require(name not in visiting, "recursive private-stack path: " + name)
        if name not in frames:
            if _function_base(name) in ("__udivsi3", "__sdivsi3", "__udivdi3", "__umoddi3", "__moddi3"):
                return 0, [name + "(library margin)"]
            domain, target = name.split(":", 1)
            alternatives = resolve(domain, target)
            return max((depth(node) for node in alternatives), key=lambda item: item[0])
        visiting.add(name); best = (0, [])
        for target, label in calls.get(name, []):
            targets = indirect(name, label) if target == "__indirect_call" else [target]
            if target == "__indirect_call":
                indirect_sites.add((name, label, tuple(targets)))
            for callee in targets:
                found = depth(callee)
                if found[0] > best[0]: best = found
        visiting.remove(name)
        memo[name] = (frames[name] + best[0], [name + "(" + str(frames[name]) + ")"] + best[1])
        return memo[name]
    for root in ("kui_vmu_native_passive_arm", "kui_vmu_native_passive_submit",
                 "kui_vmu_native_passive_complete", "kui_vmu_native_readonly_release_check",
                 "kui_vmu_native_passive_report"):
        answer = max((depth(node) for node in resolve("worker", root)), key=lambda item: item[0])
        total = answer[0] + 80 + 64
        require(total <= 4096, f"private stack path {root} needs{total} bytes")
        paths[root] = {"c_frame_bytes": answer[0], "assembly_and_guard_margin": 144,
                       "conservative_bytes": total, "worst_path": answer[1]}
    require(null_sites, "missing independently guarded optional writer block callback")
    return {"paths": paths, "indirect_sites": len(indirect_sites),
            "null_writer_block_sites": [list(site) for site in sorted(null_sites)],
            "source_qualified_calls": True}


def check_stacks(directory, worker):
    resident = inspect_elf((directory / "resident-sci.elf").read_bytes(), LOW, 0x8C007800)
    low_rows = linked_stack_rows(sorted((directory / "resident/lto").glob("*.ltrans*.su")), resident["symbols"])
    high_rows = linked_stack_rows(sorted((directory / "native").rglob("*.su")), worker[4])
    reported = {_function_base(name) for name, _ in high_rows}
    assembly = {"kui_vmu_native_passive_submit_hook", "kui_vmu_native_passive_complete_hook",
                "kui_vmu_native_passive_terminal", "kui_vmu_native_readonly_release_hook"}
    required = {_function_base(name) for name, (_, _, kind, _) in worker[4].items()
                if kind == 2 and _function_base(name).startswith(("kui_", "durable_", "readonly_overlay_", "sci_storage_"))
                and _function_base(name) not in assembly}
    require(required <= reported, "missing linked native C stack frame: " + ", ".join(sorted(required - reported)))
    lease = [(name, frame) for name, frame in low_rows if _function_base(name) == "kui_vmu_observe_sd_lease"]
    require(len(lease) == 1, "missing unique worker-stack-only lease frame")
    # Every retained C frame is included, including low bus callbacks called
    # from the worker. Summing them overstates any single actual path.
    high_sum, low_sum = sum(frame for _, frame in high_rows), sum(frame for _, frame in low_rows)
    call_paths = worker_stack_paths(directory, worker, resident)
    total = max(path["conservative_bytes"] for path in call_paths["paths"].values())
    available = (resident_layout.stack_top("sci", False, True) -
                 resident_layout.stack_bottom("sci", False, True) -
                 resident_layout.STACK_GUARD_BYTES - resident_layout.STACK_ALIGNMENT_GAP)
    ordinary = low_sum - lease[0][1] + resident_layout.ASSEMBLY_STACK_BYTES
    require(ordinary <= available, f"ordinary native GD stack sum {ordinary} exceeds{available}")
    return {"worker_c_frame_sum": high_sum, "resident_callback_c_frame_sum": low_sum,
            "conservative_worker_bytes": total, "available_worker_bytes": 4096,
            "ordinary_low_stack_bytes": ordinary, "ordinary_low_available_bytes": available,
            "lease_only_frame_bytes": lease[0][1], "required_worker_frames": len(required), "callgraph": call_paths}


def check(directory, *, self_test=False):
    require(__debug__, "gateway model requires Python assertions")
    directory = Path(directory)
    base = inspect_base(directory, self_test=self_test,
                        objdump=shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump"))
    worker = read_elf(directory / "native-worker.elf")
    data, header, segments, sections, raw_symbols = worker
    symbols = {name: record[0] for name, record in raw_symbols.items()}
    binary = (directory / "native-worker.bin").read_bytes()
    wire = (directory / "native-worker.reloc").read_bytes()
    require(len(segments) == 1, "worker must own one load segment")
    kind, offset, va, pa, size, memory, flags, _ = segments[0]
    require((kind, va, pa, size, memory, flags) == (1, P2, P1, len(binary), SPAN, 7),
            "worker must be P2 with a 128KiB physical load span")
    require(binary == region(data, offset, size, "worker load") and len(binary) >= 80,
            "worker binary differs from ELF load")
    fields = struct.unpack_from("<20I", binary)
    names = ("_kui_vmu_native_passive_arm", "_kui_vmu_native_passive_submit_hook",
             "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_terminal",
             "__vmu_native_worker_bss_begin", "__vmu_native_worker_bss_end",
             "__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top",
             "__vmu_native_worker_end", "_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
             "_kui_vmu_native_readonly_release_hook", "_kui_vmu_native_readonly_release_resume",
             "_kui_vmu_native_persistent_sd_transaction")
    require(fields[:3] == (0x4B4E5031, 1, 80) and
            fields[3:17] == tuple(symbols[name] for name in names) and not any(fields[17:]),
            "read-only worker exports differ from exact typed symbols")
    require(fields[3] == header[4] and all(not p & 1 and P2 + 80 <= p < P2 + len(binary) for p in fields[3:7]),
            "worker entry/hook points outside initialized code")
    require(P2 + len(binary) == fields[7] <= fields[8] <= fields[9] and
            fields[9:12] == (P2 + 0x1F000, P2 + SPAN, P2 + SPAN), "worker BSS/stack layout differs")
    address, exported, section = symbol_bytes(worker, "_kui_vmu_native_worker_exports", 1)
    require(address == P2 and exported == binary[:80] and section[2] & 7 == 2,
            "exports must be typed immutable header")
    for name in names[:4] + (names[11], names[13]):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "exported entry is not executable")
    for name in ("_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
                 "_kui_vmu_native_readonly_release_resume"):
        address, cell, section = symbol_bytes(worker, name, 1)
        require(len(cell) == 4 and address % 4 == 0 and not any(cell) and section[2] & 7 == 3,
                "resume/consumer cells must be typed, zero-initialized worker-owned words")
    for section in sections:
        require(not section[2] & 2 or not section[5] or
                P2 <= section[3] < section[3] + section[5] <= P2 + SPAN,
                "allocated worker bytes escape P2")
    for name, (value, _, symbol_kind, owner) in raw_symbols.items():
        require(symbol_kind not in (1, 2) or owner < len(sections) and P2 <= value < P2 + SPAN,
                "worker object/function escapes P2: " + name)
        require(not name.startswith(("_kui_retail_storage_", "_kui_sci_sd_acquire", "_kui_sci_sd_release",
                                      "_kui_loader_sd_init", "_kui_loader_sd_adopt", "_kui_loader_sd_shutdown")),
                "persistent worker independently owns/reinitializes resident storage: " + name)
    for name in ("_kui_vmu_durable_init", "_kui_vmu_durable_handle", "_kui_vmu_durable_admit",
                 "_kui_vmu_durable_transaction", "_kui_vmu_store_open", "_kui_vmu_store_write",
                 "_kui_vmu_store_read", "_kui_sci_sd_write", "_kui_sci_sd_sync",
                 "_kui_vmu_readonly_overlay_prepare", "_kui_vmu_readonly_overlay_complete",
                 "_kui_vmu_volatile_chain_build", "_kui_vmu_volatile_chain_verify",
                 "_kui_vmu_native_readonly_release_check", "_kui_vmu_native_lifetime_release_safe_ranges"):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "missing bounded persistent bridge implementation: " + name)
    require(generate(data, binary, P1, SPAN) == wire, "relocation table differs from exact ELF extraction")
    relocations = struct.unpack_from("<I", wire, 28)[0]
    require(all(struct.unpack_from("<I", wire, 76 + i * 8)[0] & 0xFF000000 == 0xAC000000 for i in range(relocations)),
            "internal relocated pointers must retain P2 aliases")
    stage = read_elf(directory / "stage.elf")
    prepared = inspect_elf(stage[0], 0x8CE00000, 0x8CFE0000)
    stage_binary = (directory / "stage.bin").read_bytes()
    require(prepared["payload"] == stage_binary, "stage binary differs from ELF")
    for begin, end, payload in (("__native_worker_blob_begin", "__native_worker_blob_end", binary),
                                ("__native_worker_reloc_begin", "__native_worker_reloc_end", wire)):
        a, b = stage[4][begin][0], stage[4][end][0]
        require(b - a == len(payload) and region(stage_binary, a - 0x8CE00000, b - a, begin) == payload,
                "stage embeds different worker/relocation bytes")
        require(any(s[1] == 1 and s[2] & 7 == 2 and s[3] <= a < b <= s[3] + s[5] for s in stage[3]),
                "embedded worker must occupy stage immutable data")
    gateway_address, gateway, _ = symbol_bytes(stage, "_kui_vmu_native_worker_call", 2)
    stage_cases = model_gateway(gateway_address, gateway)
    hook_cases = model_hooks(binary, wire, symbols)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    disassembly = subprocess.check_output([objdump, "-d", str(directory / "native-worker.elf")], text=True)
    try:
        instructions = instruction_audit("native-readonly", disassembly)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    hook_ranges = [symbol_bytes(worker, name, 2)[:2] for name in names[1:4] + (names[11],)]
    for address, _, mnemonic, operands in literal_aware_instructions(disassembly):
        require(mnemonic not in ("trapa", "sleep", "rte", "ldtlb"), "unexpected privileged worker instruction")
        if mnemonic in ("ldc", "ldc.l") and operands.split("!", 1)[0].strip().endswith(",sr"):
            require(any(begin <= address < begin + len(body) for begin, body in hook_ranges),
                    "C worker changes SR outside audited gateways")
    pio = check_pio_writers(directory, worker)
    stacks = check_stacks(directory, worker)
    source = check_source(directory, base["runtime"])
    report = {"schema": "kui.native.persistent.probe.artifact.v1", "allocator_bootstrap_audit": base,
              "module_sha256": base["module_sha256"], "runtime": base["runtime"],
              "worker_bytes": len(binary), "worker_span": SPAN, "worker_stack_bytes": 4096,
              "relocation_records": relocations, "worker_instructions": instructions,
              "stage_gateway_model_cases": stage_cases, "persistent_gateway_model_cases": hook_cases,
              "original_release_mask_model_cases": hook_cases // 4,
              "stage_embedded_worker_verified": True, "maple_interception_installed": True,
              "virtual_replies_enabled": True, "storage_writes_enabled": True, "scoped_sci_transactions": True,
              "source_provenance": source, "stack_audit": stacks, "pio_writer_audit": pio,
              "console_execution_verified": False, "expected_phase": "native persistent session"}
    if self_test:
        report["gateway_mutation_rejections"] = model_mutations(binary, symbols)
        report["artifact_mutation_rejections"] = mutation_checks(directory)
    return report


def mutation_checks(directory):
    """Coherent changed flat images still must fail their typed ELF binding."""
    checks = 0
    with tempfile.TemporaryDirectory() as temp:
        target = Path(temp)
        for path in directory.iterdir():
            if path.is_file() and path.suffix in (".elf", ".bin", ".reloc", ".kui"):
                (target / path.name).symlink_to(path.resolve())
        for name, at in (("native-worker.bin", 0), ("native-worker.bin", 12),
                         ("native-worker.reloc", 72), ("stage.bin", 128)):
            path = target / name
            original = (directory / name).read_bytes()
            changed = bytearray(original); changed[at] ^= 1
            path.unlink(); path.write_bytes(changed)
            try:
                check(target)
            except (ValueError, AssertionError, KeyError, struct.error):
                checks += 1
            else:
                raise ValueError("modified artifact accepted: " + name)
            path.unlink(); path.symlink_to((directory / name).resolve())
    return checks



def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    try:
        result = json.dumps(check(args.directory, self_test=args.self_test), indent=2) + "\n"
    except (OSError, ValueError, KeyError, AssertionError, struct.error, subprocess.CalledProcessError) as error:
        p.exit(2, f"Native persistent artifact audit refused: {error}\n")
    if args.output: args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
