#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect the isolated Maple observer and package its temporary retail module."""
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
from runtime_package import verify
from toy_pilot_symbols import _linked_symbols
from check_vmu_maple_observe_hook import check_hook
import retail_package as layout


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
    words = struct.unpack_from("<13I", payload)
    names = ("magic", "version", "bytes", "initialize", "hook", "bss_begin", "bss_end",
             "stack_bottom", "stack_top", "worker_end", "lease_bytes", "snapshot", "terminal")
    exports = dict(zip(names, words))
    symbols = image["symbols"]
    typed = _linked_symbols(elf, {"_kui_vmu_maple_observer_init",
        "_kui_vmu_maple_observer_hook", "_kui_vmu_maple_observer_terminal",
        "_kui_vmu_maple_observer_snapshot"})
    if words[:3] != (0x564D4F31, 1, 52) or exports["lease_bytes"] != 0x30000:
        raise ValueError("Observer exports identity differs from profile")
    for key, symbol in (("initialize", "_kui_vmu_maple_observer_init"),
                        ("hook", "_kui_vmu_maple_observer_hook"),
                        ("terminal", "_kui_vmu_maple_observer_terminal"),
                        ("snapshot", "_kui_vmu_maple_observer_snapshot")):
        if exports[key] != symbols.get(symbol):
            raise ValueError("Worker export does not match linked symbol: " + key)
        address, size, kind, section = typed.get(symbol, (0, 0, 0, None))
        if (section is None or not size or
                not section[3] <= address < address + size <= section[3] + section[5]):
            raise ValueError("Worker export has no bounded typed symbol: " + key)
        if key == "snapshot":
            if size != 272 or kind != 1 or section[1] != 8 or section[2] & 7 != 3:
                raise ValueError("Worker snapshot must be its 272-byte private BSS object")
        elif kind != 2 or size & 1 or section[2] & 7 != 6:
            raise ValueError("Worker entry must be a bounded cached-code function: " + key)
    if (not 0xACFD0000 <= exports["bss_begin"] <= exports["snapshot"] < exports["bss_end"]
            <= exports["stack_bottom"] < exports["stack_top"] <= 0xACFE0000
            or exports["stack_top"] - exports["stack_bottom"] != 4096
            or exports["worker_end"] != exports["stack_top"] - 0x20000000
            or exports["worker_end"] != image["memory_end"]
            or any(exports[key] & 31 for key in ("bss_begin", "bss_end", "stack_bottom", "stack_top"))
            or exports["snapshot"] & 3
            or exports["snapshot"] + 272 > exports["bss_end"]
            or exports["bss_begin"] - 0x20000000 < 0x8CFD0000 + len(payload)):
        raise ValueError("Observer private BSS/stack is outside owned lease")
    for key in ("initialize", "hook", "terminal"):
        if not 0x8CFD0034 <= exports[key] < 0x8CFD0000 + len(payload) or exports[key] & 1:
            raise ValueError("Worker entry is outside cached executable image: " + key)
    return {"exports": exports, "binary_bytes": len(payload),
            "worker_sha256": hashlib.sha256(payload).hexdigest(),
            "hook_integer_state": check_hook(image, exports)}


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
    resident = inspect_elf((directory / "resident-sci.elf").read_bytes(),
                           0x8C004000, 0x8C007800)
    stage = inspect_elf((directory / "stage.elf").read_bytes(),
                       0x8CE00000, 0x8CFE0000)
    entry = inspect_elf((directory / "entry.elf").read_bytes(),
                       0x8C010000, 0x8C112000)
    check_bss(resident, 0x8C004000, "__retail_resident")
    check_bss(stage, 0x8CE00000, "__retail_stage")
    report["resident_stack"] = check_stack_usage(directory / "resident",
        resident["symbols"], transport="sci", low=True)
    report["instructions"] = {}
    for name in ("resident-sci", "worker", "stage", "entry"):
        disassembly = subprocess.check_output(
            ["sh-elf-objdump", "-d", str(directory / (name + ".elf"))], text=True)
        (directory / (name + ".dis")).write_text(disassembly)
        report["instructions"][name] = audit(name, disassembly)
    worker_frames = []
    for path in (directory / "worker").rglob("*.su"):
        for line in path.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) != 3 or fields[2] != "static":
                raise ValueError("Unbounded worker stack report: " + str(path))
            worker_frames.append(int(fields[1]))
    if not worker_frames or sum(worker_frames) + 80 + 32 > 4096:
        raise ValueError("Worker conservative C/ASM stack sum exceeds private stack")
    report["worker_stack"] = {"c_frame_sum": sum(worker_frames),
                               "assembly_bytes": 80, "guard_bytes": 32,
                               "available_bytes": 4096}
    module = (directory / "retail-maple-observe.kui").read_bytes()
    report["runtime"] = check_embedded_images(directory,
        (directory / "worker.bin").read_bytes(), resident, stage, entry, module)
    report.update({"module_sha256": hashlib.sha256(module).hexdigest(),
                   "module_bytes": len(module), "mode": "read-only Toy Maple observation",
                   "virtual_vmu_enabled": False})
    report_bytes = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode()
    (directory / "observer-artifact-audit.json").write_bytes(report_bytes)
    files = {"KUI/retail.kui": module,
             "README-FIRST.md": Path("docs/vmu-maple-observer-test.md").read_bytes(),
             "observer-artifact-audit.json": report_bytes}
    files["SHA256SUMS.txt"] = "".join(
        hashlib.sha256(data).hexdigest() + "  " + name + "\n"
        for name, data in sorted(files.items())).encode()
    target = directory / "vmu-maple-observer-test.zip"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2026, 10, 16, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    print(target)


if __name__ == "__main__":
    main()
