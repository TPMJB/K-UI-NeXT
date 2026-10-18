#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect isolated passive/blank/SD/volatile proof workers and package their module."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

from check_loader_layout import EH, PH, inspect_elf, padded, region
from check_retail_instructions import audit
from check_retail_loader_layout import check_bss, check_stack_usage
import check_retail_loader_layout as resident_layout
from check_retail_stack import EDGE, NODE
from runtime_package import verify
from toy_pilot_symbols import _linked_symbols
from vmu_maple_observe_symbols import sd_gateway_span
from check_vmu_maple_observe_hook import check_hook, check_completion_hook
import retail_package as layout


def write_test_archive(directory, module, report_bytes, *, readonly_proof=False,
                       sd_readonly_proof=False, volatile_proof=False):
    """Install through the ordinary native-game route in the released shell."""
    if any(type(value) is not bool for value in
           (readonly_proof, sd_readonly_proof, volatile_proof)):
        raise ValueError("Read-only proof selection must be boolean")
    if sum((readonly_proof, sd_readonly_proof, volatile_proof)) > 1:
        raise ValueError("Select exactly one read-only proof mode")
    guide = ("vmu-volatile-card-test.md" if volatile_proof else
             "vmu-sd-readonly-card-test.md" if sd_readonly_proof else
             "vmu-readonly-card-test.md" if readonly_proof else "vmu-maple-observer-test.md")
    files = {"KUI/apps/games/retail-boot.kui": module,
             "README-FIRST.md": (Path("docs") / guide).read_bytes(),
             "observer-artifact-audit.json": report_bytes}
    if (sd_readonly_proof or volatile_proof) and (directory / "personalization-audit.json").exists():
        from personalize_vmu_sd_proof import validate_personalized
        derivation = validate_personalized(directory)
        if derivation.get("proof_mode", 2) != (3 if volatile_proof else 2):
            raise ValueError("Personalization mode differs from the selected test archive")
        if hashlib.sha256(module).hexdigest() != derivation["artifacts"]["retail-maple-observe.kui"]["personalized_sha256"]:
            raise ValueError("Archive module differs from the personalized runtime")
        files["personalization-audit.json"] = (directory / "personalization-audit.json").read_bytes()
        original_record = derivation["base_compile_provenance"]["name"]
        files[original_record] = (directory / original_record).read_bytes()
    files["SHA256SUMS.txt"] = "".join(
        hashlib.sha256(data).hexdigest() + "  " + name + "\n"
        for name, data in sorted(files.items())).encode()
    target = directory / ("vmu-volatile-card-test.zip" if volatile_proof else
                          "vmu-sd-readonly-card-test.zip" if sd_readonly_proof else
                          "vmu-readonly-card-test.zip" if readonly_proof else "vmu-maple-observer-test.zip")
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2026, 10, 16, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    return target


def inspect_worker(directory):
    elf = (directory / "worker.elf").read_bytes()
    image = inspect_elf(elf,
                        0x8CFD0000, 0x8CFE0000, private_p2=True,
                        entry_symbol="_kui_vmu_maple_observer_init", entry_at_base=False)
    header = EH.unpack_from(elf)
    segments = [PH.unpack_from(elf, header[5] + index * header[9])
                for index in range(header[10])]
    loads = [segment for segment in segments if segment[0] == 1]
    if (len(loads) != 2 or loads[0][6] != 5 or loads[1][6] != 6 or
            loads[0][2] != 0x8CFD0000 or not 0xACFD0000 <= loads[1][2] < 0xACFE0000):
        raise ValueError("Worker requires distinct read-only code and private writable load segments")
    payload = image["payload"]
    if (directory / "worker.bin").read_bytes() != payload:
        raise ValueError("Worker objcopy bytes differ from admitted ELF")
    words = struct.unpack_from("<16I", payload)
    names = ("magic", "version", "bytes", "initialize", "hook", "bss_begin", "bss_end",
             "stack_bottom", "stack_top", "worker_end", "lease_bytes", "snapshot", "terminal", "complete_hook",
             "readonly_mode", "sd_transaction")
    exports = dict(zip(names, words))
    symbols = image["symbols"]
    typed = _linked_symbols(elf, {"_kui_vmu_maple_observer_init",
        "_kui_vmu_maple_observer_hook", "_kui_vmu_maple_observer_terminal",
        "_kui_vmu_maple_observer_snapshot", "_kui_vmu_maple_observer_complete_hook",
        "_kui_vmu_maple_observer_sd_transaction"})
    if (words[0] != 0x564D4F31 or words[1] not in (5, 6) or words[2] != 64 or
            exports["lease_bytes"] != 0x30000 or
            exports["readonly_mode"] not in (0, 1, 2, 3) or
            (exports["readonly_mode"] == 3 and words[1] != 6) or
            (exports["readonly_mode"] < 2 and exports["sd_transaction"])):
        raise ValueError("Observer exports identity differs from profile")
    snapshot_bytes = 708 if words[1] == 6 else 644
    entries = [("initialize", "_kui_vmu_maple_observer_init"),
                        ("hook", "_kui_vmu_maple_observer_hook"),
                        ("terminal", "_kui_vmu_maple_observer_terminal"),
                        ("complete_hook", "_kui_vmu_maple_observer_complete_hook"),
                        ("snapshot", "_kui_vmu_maple_observer_snapshot")]
    if exports["readonly_mode"] >= 2:
        entries.append(("sd_transaction", "_kui_vmu_maple_observer_sd_transaction"))
    for key, symbol in entries:
        if exports[key] != symbols.get(symbol):
            raise ValueError("Worker export does not match linked symbol: " + key)
        address, size, kind, section = typed.get(symbol, (0, 0, 0, None))
        if (section is None or not size or
                not section[3] <= address < address + size <= section[3] + section[5]):
            raise ValueError("Worker export has no bounded typed symbol: " + key)
        if key == "snapshot":
            if size != snapshot_bytes or kind != 1 or section[1] != 8 or section[2] & 7 != 3:
                raise ValueError(f"Worker snapshot must be its {snapshot_bytes}-byte private BSS object")
        elif kind != 2 or size & 1 or section[2] & 7 != 6:
            raise ValueError("Worker entry must be a bounded cached-code function: " + key)
    if (not 0xACFD0000 <= exports["bss_begin"] <= exports["snapshot"] < exports["bss_end"]
            <= exports["stack_bottom"] < exports["stack_top"] <= 0xACFE0000
            or exports["stack_top"] - exports["stack_bottom"] != 4096
            or exports["worker_end"] != exports["stack_top"] - 0x20000000
            or exports["worker_end"] != image["memory_end"]
            or any(exports[key] & 31 for key in ("bss_begin", "bss_end", "stack_bottom", "stack_top"))
            or exports["snapshot"] & 3
            or exports["snapshot"] + snapshot_bytes > exports["bss_end"]
            or exports["bss_begin"] - 0x20000000 < 0x8CFD0000 + len(payload)):
        raise ValueError("Observer private BSS/stack is outside owned lease")
    for key, _ in entries:
        if key == "snapshot":
            continue
        if not 0x8CFD0040 <= exports[key] < 0x8CFD0000 + len(payload) or exports[key] & 1:
            raise ValueError("Worker entry is outside cached executable image: " + key)
    mode = exports["readonly_mode"]
    report = {"exports": exports, "binary_bytes": len(payload),
            "mode": ("VOLATILE RAM-save virtual A1 proof; SD read-only" if mode == 3 else
                     "READ-ONLY SD-backed virtual A1 proof" if mode == 2 else
                     "READ-ONLY virtual blank A1 proof" if mode == 1 else "read-only Toy Maple observation"),
            "virtual_vmu_enabled": bool(mode), "storage_writes_enabled": False,
            "virtual_writes_enabled": mode == 3,
            "volatile_writes_enabled": mode == 3,
            "persistence": False,
            "worker_sha256": hashlib.sha256(payload).hexdigest(),
            "hook_integer_state": check_hook(image, exports),
            "completion_integer_state": check_completion_hook(image, exports)}
    private_dma = inspect_private_dma(elf, image, exports)
    if private_dma is not None:
        report["private_dma"] = private_dma
    return report


def inspect_private_dma(elf, image, exports):
    """Mode 3 owns exact uncached chain/capture/write objects before MDSTAR."""
    required = (("issued_chain", "_issued_chain", 512, 32),
                ("captured_chain", "_captured_chain", 512, 4),
                ("write_payloads", "_write_payloads", 4096, 4))
    typed = _linked_symbols(elf, {symbol for _, symbol, _, _ in required})
    if exports["readonly_mode"] != 3:
        if typed:
            raise ValueError("Non-volatile proof contains private mutating DMA buffers")
        return None
    result = {}
    ranges = [(exports["snapshot"], exports["snapshot"] + 708)]
    for key, symbol, expected, alignment in required:
        address, size, kind, section = typed.get(symbol, (0, 0, 0, None))
        if (size != expected or kind != 1 or address % alignment or
                section is None or section[1] != 8 or section[2] & 7 != 3 or
                image["symbols"].get(symbol) != address or
                not 0xACFD0000 <= address < address + size <= 0xACFE0000 or
                not exports["bss_begin"] <= address < address + size <= exports["bss_end"] or
                not section[3] <= address < address + size <= section[3] + section[5] or
                any(address < end and begin < address + size for begin, end in ranges)):
            raise ValueError("Volatile proof requires its exact private P2 DMA object: " + symbol)
        ranges.append((address, address + size))
        result[key] = dict(address=f"0x{address:08x}", bytes=size, alignment=alignment)
    result["issued_chain"]["physical_address"] = (
        f"0x{int(result['issued_chain']['address'], 16) & 0x1fffffff:08x}")
    return result


def inspect_sd_profile(directory, stage, mode):
    """Bind the strict decoded profile to the bytes actually embedded in stage."""
    start = stage["symbols"].get("__vmu_sd_profile_blob_start")
    end = stage["symbols"].get("__vmu_sd_profile_blob_end")
    if mode < 2:
        if start is not None or end is not None:
            raise ValueError("Non-SD proof stage must not embed an SD profile")
        return None
    if start is None or end is None or start & 3 or end - start != 1536:
        raise ValueError("SD profile requires one bounded 1536-byte stage blob")
    wire = (directory / "vmu-sd-profile.bin").read_bytes()
    if len(wire) != 1536 or region(stage["payload"], start - 0x8CE00000,
                                  end - start, "embedded SD profile") != wire:
        raise ValueError("Stage contains a different SD profile")
    from vmu_sd_profile import decode_profile
    summary = decode_profile(wire)
    return dict(summary, embedded_address=f"0x{start:08x}", embedded_bytes=len(wire))


def _function_base(name):
    return name.rsplit(":", 1)[-1].lstrip("_").split(".", 1)[0]


def linked_stack_rows(reports, symbols):
    """Discard only functions absent from the linked ELF, retaining all clones."""
    linked = {_function_base(name) for name in symbols}
    rows = []
    for report in reports:
        for line in report.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) != 3:
                raise ValueError("Malformed compiler stack report: " + str(report))
            name = fields[0].rsplit(":", 1)[-1]
            if _function_base(name) not in linked:
                continue
            if fields[2] != "static" or not fields[1].isdigit():
                raise ValueError("Unbounded linked stack report: " + name)
            rows.append((name, int(fields[1])))
    if not rows:
        raise ValueError("Missing linked compiler stack reports")
    return rows


def require_worker_stack_coverage(directory, worker, rows):
    """Every linked VMU C entry and ownership helper needs its own C frame.

    A missing .su file must not silently shrink the conservative sum. Assembly
    trampolines have a separate fixed allowance; their three exact names are
    the only excluded VMU entries. Inlined functions have no linked symbol.
    """
    assembly = {"kui_vmu_maple_observer_hook", "kui_vmu_maple_observer_complete_hook",
                "kui_vmu_maple_observer_terminal"}
    guards = {"original_fingerprint", "physical_response_valid", "source_span",
              "response_span", "proof_response", "proof_layout", "proof_stats",
              "proof_stop", "sd_read_block", "read_word", "read_byte", "write_word",
              "unit_index", "record_subunit", "read_block", "commit_ram", "shape", "slot",
              "full", "geometry", "classify", "receive_geometry", "pointer_physical_overlap"}
    candidates = {name for name, address in worker["symbols"].items()
                  if 0x8CFD0040 <= address < 0x8CFE0000 and
                  (_function_base(name).startswith(("kui_", "ram_", "chain_")) or
                   _function_base(name) in guards)}
    elf_path = directory / "worker.elf"
    if elf_path.exists():
        typed = _linked_symbols(elf_path.read_bytes(), candidates)
        candidates = {name for name in candidates if typed.get(name, (0, 0, 0, None))[2] == 2}
    required = {name.lstrip("_") for name in candidates if _function_base(name) not in assembly}
    reported = {name.lstrip("_") for name, _ in rows}
    missing = sorted(required - reported)
    if missing:
        raise ValueError("Missing linked worker stack-usage frame: " + ", ".join(missing))
    return len(required)


def check_sd_gateway(directory, resident, elf):
    """No resident GD path may enter the lease-only worker-stack gateway."""
    address, size, declared = sd_gateway_span(elf, resident)
    reports = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    nodes = set()
    if not reports:
        raise ValueError("Missing final-LTO SD gateway callgraph proof")
    for report in reports:
        data = report.read_text()
        nodes.update(_function_base(title) for title, _ in NODE.findall(data))
        for source, target, _ in EDGE.findall(data):
            if _function_base(target) == "kui_vmu_observe_sd_lease":
                raise ValueError("Resident call enters worker-stack SD lease gateway: " + source)
    if "kui_vmu_observe_sd_lease" not in nodes:
        raise ValueError("Final-LTO graph omits SD lease gateway")
    # Its sole function pointer is passed by the high stage, never registered
    # as a low reader callback. Reject a resident literal pointing into it.
    payload = resident["payload"]
    for offset in range(0, len(payload) - 3, 4):
        at = 0x8C004000 + offset
        if address <= at < address + size:
            continue
        value = struct.unpack_from("<I", payload, offset)[0]
        if (value & 0xFF000000) in (0x0C000000, 0x8C000000, 0xAC000000):
            canonical = value & 0x1FFFFFFF | 0x80000000
            if address <= canonical < address + size:
                raise ValueError("Resident literal references worker-stack SD lease gateway")
    return {"address": f"0x{address:08x}", "bytes": size,
            "declared_symbol_bytes": declared, "bounds": "exclusive allocated linker section",
            "incoming_resident_edges": 0, "resident_pointer_references": 0,
            "callgraph_reports": [path.name for path in reports]}


def check_sd_stacks(directory, resident, worker, gateway):
    resident_rows = linked_stack_rows(sorted((directory / "resident/lto").glob("*.ltrans*.su")),
                                      resident["symbols"])
    worker_rows = linked_stack_rows(sorted((directory / "worker").rglob("*.su")), worker["symbols"])
    covered_worker_frames = require_worker_stack_coverage(directory, worker, worker_rows)
    for required in ("kui_retail_resident_dispatch", "kui_retail_gd_dispatch",
                     "kui_retail_image_read", "kui_loader_sd_stream_next"):
        if not any(_function_base(name) == required for name, _ in resident_rows):
            raise ValueError("Missing runtime stack-usage frame: " + required)
    lease_rows = [(name, frame) for name, frame in resident_rows
                  if _function_base(name) == "kui_vmu_observe_sd_lease"]
    if len(lease_rows) != 1 or lease_rows[0][0] != "kui_vmu_observe_sd_lease":
        raise ValueError("SD lease gateway requires one exact noipa stack frame")
    low_rows = [(name, frame) for name, frame in resident_rows if name != lease_rows[0][0]]
    available = (resident_layout.stack_top("sci", False, True) -
                 resident_layout.stack_bottom("sci", False, True) -
                 resident_layout.STACK_GUARD_BYTES - resident_layout.STACK_ALIGNMENT_GAP)
    low_total = sum(frame for _, frame in low_rows) + resident_layout.ASSEMBLY_STACK_BYTES
    if low_total > available:
        raise ValueError(f"Resident conservative stack sum {low_total} exceeds {available}")
    resident_sum = sum(frame for _, frame in resident_rows)
    worker_sum = sum(frame for _, frame in worker_rows)
    if worker_sum + resident_sum + 84 + 32 > 4096:
        raise ValueError("SD worker/gateway/bus stack sum exceeds private stack")
    return ({"conservative_bytes": low_total, "available_bytes": available,
             "assembly_allowance": resident_layout.ASSEMBLY_STACK_BYTES,
             "retained_c_frames": len(low_rows), "worker_only_gateway_frame": lease_rows[0][1],
             "gateway_exclusion_proof": gateway},
            {"c_frame_sum": worker_sum, "resident_callback_c_frame_sum": resident_sum,
             "assembly_bytes": 84, "guard_bytes": 32, "available_bytes": 4096,
             "conservative_bytes": worker_sum + resident_sum + 84 + 32,
             "linked_worker_frames": len(worker_rows), "linked_resident_frames": len(resident_rows),
             "required_worker_frame_coverage": covered_worker_frames})


def check_embedded_images(directory, worker, resident, stage, entry, module):
    """The reported proof objects must be the actual bytes booted by this ZIP."""
    for name, image in (("resident-sci", resident), ("stage", stage)):
        if (directory / (name + ".bin")).read_bytes() != image["payload"]:
            raise ValueError(name + ".bin differs from linked ELF bytes")
    for transport in ("scif", "sci", "ide", "scia"):
        prefix = "__retail_resident_" + transport + "_blob_"
        begin = stage["symbols"].get(prefix + "start", 0)
        end = stage["symbols"].get(prefix + "end", 0)
        actual = region(stage["payload"], begin - 0x8CE00000,
                        end - begin, "embedded observer resident")
        if actual != padded(resident["payload"]):
            raise ValueError("Stage contains a different resident: " + transport)
    start = stage["symbols"].get("__vmu_observe_worker_blob_start", 0)
    end = stage["symbols"].get("__vmu_observe_worker_blob_end", 0)
    if region(stage["payload"], start - 0x8CE00000,
              end - start, "embedded observer worker") != worker:
        raise ValueError("Stage contains a different observer worker")
    high_blob = padded(stage["payload"])
    es = entry["symbols"]
    if (es.get("__retail_map") != 0x8C011000 or
            es.get("__retail_stage_blob_start") != 0x8C012000 or
            es.get("__retail_stage_blob_end") != 0x8C010000 + len(entry["payload"]) or
            entry["memory_end"] != 0x8C010000 + len(entry["payload"]) or
            entry["payload"][0x2000:] != high_blob):
        raise ValueError("Entry contains a different stage or invalid relocation bounds")
    if any(region(entry["payload"], layout.MAP_OFFSET, layout.MAP_BYTES, "blank manifest")):
        raise ValueError("Packaged observer manifest must be blank")
    if region(entry["payload"], layout.HEADER_OFFSET, layout.HEADER_BYTES, "header") != layout.relocation_header(len(high_blob), False, True):
        raise ValueError("Observer relocation header must select the low native reader")
    runtime = verify(module)
    if (module[64:] != entry["payload"] or runtime["memory_bytes"] !=
            entry["memory_end"] - 0x8C010000):
        raise ValueError("Runtime package differs from the admitted entry ELF")
    return runtime


def main():
    directory = Path(sys.argv[1])
    report = inspect_worker(directory)
    mode = report["exports"]["readonly_mode"]
    resident_elf = (directory / "resident-sci.elf").read_bytes()
    resident = inspect_elf(resident_elf, 0x8C004000, 0x8C007800)
    stage = inspect_elf((directory / "stage.elf").read_bytes(),
                       0x8CE00000, 0x8CFE0000)
    entry = inspect_elf((directory / "entry.elf").read_bytes(),
                       0x8C010000, 0x8C112000)
    check_bss(resident, 0x8C004000, "__retail_resident")
    check_bss(stage, 0x8CE00000, "__retail_stage")
    profile = inspect_sd_profile(directory, stage, mode)
    if profile is not None:
        report["sd_profile"] = profile
        report["sd_profile_sha256"] = profile["wire_sha256"]
        if (directory / "personalization-audit.json").exists():
            from personalize_vmu_sd_proof import validate_personalized
            derivation = validate_personalized(directory)
            if (derivation.get("proof_mode", 2) != mode or
                    derivation["profile_sha256"] != profile["wire_sha256"] or
                    derivation["profile_address"] != profile["embedded_address"]):
                raise ValueError("Personalization audit differs from embedded SD profile")
            report["personalization"] = derivation
    if mode < 2:
        if "_kui_vmu_observe_sd_lease" in resident["symbols"]:
            raise ValueError("Non-SD proof resident contains the SD lease gateway")
        report["resident_stack"] = check_stack_usage(directory / "resident",
            resident["symbols"], transport="sci", low=True)
    report["instructions"] = {}
    for name in ("resident-sci", "worker", "stage", "entry"):
        disassembly = subprocess.check_output(
            ["sh-elf-objdump", "-d", str(directory / (name + ".elf"))], text=True)
        (directory / (name + ".dis")).write_text(disassembly)
        report["instructions"][name] = audit(name, disassembly)
    if mode >= 2:
        worker = inspect_elf((directory / "worker.elf").read_bytes(), 0x8CFD0000,
                             0x8CFE0000, private_p2=True,
                             entry_symbol="_kui_vmu_maple_observer_init", entry_at_base=False)
        gateway = check_sd_gateway(directory, resident, resident_elf)
        report["resident_stack"], report["worker_stack"] = check_sd_stacks(
            directory, resident, worker, gateway)
    else:
        worker_frames = []
        for path in (directory / "worker").rglob("*.su"):
            for line in path.read_text().splitlines():
                fields = line.split("\t")
                if len(fields) != 3 or fields[2] != "static":
                    raise ValueError("Unbounded worker stack report: " + str(path))
                worker_frames.append(int(fields[1]))
        if not worker_frames or sum(worker_frames) + 84 + 32 > 4096:
            raise ValueError("Worker conservative C/ASM stack sum exceeds private stack")
        report["worker_stack"] = {"c_frame_sum": sum(worker_frames),
                                   "assembly_bytes": 84, "guard_bytes": 32,
                                   "available_bytes": 4096}
    module = (directory / "retail-maple-observe.kui").read_bytes()
    report["runtime"] = check_embedded_images(directory,
        (directory / "worker.bin").read_bytes(), resident, stage, entry, module)
    report.update({"module_sha256": hashlib.sha256(module).hexdigest(),
                   "module_bytes": len(module)})
    report_bytes = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode()
    (directory / "observer-artifact-audit.json").write_bytes(report_bytes)
    print(write_test_archive(directory, module, report_bytes,
                             readonly_proof=mode == 1, sd_readonly_proof=mode == 2,
                             volatile_proof=mode == 3))


if __name__ == "__main__":
    main()
