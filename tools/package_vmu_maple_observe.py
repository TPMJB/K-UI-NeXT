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

from check_loader_layout import inspect_elf
from check_retail_instructions import audit
from check_retail_loader_layout import check_bss, check_stack_usage
from runtime_package import verify


def inspect_worker(directory):
    image = inspect_elf((directory / "worker.elf").read_bytes(),
                        0x8CFD0000, 0x8CFE0000, private_p2=True,
                        entry_symbol="_kui_vmu_maple_observer_init", entry_at_base=False)
    payload = image["payload"]
    if (directory / "worker.bin").read_bytes() != payload:
        raise ValueError("Worker objcopy bytes differ from admitted ELF")
    words = struct.unpack_from("<13I", payload)
    names = ("magic", "version", "bytes", "initialize", "hook", "bss_begin", "bss_end",
             "stack_bottom", "stack_top", "worker_end", "lease_bytes", "snapshot", "terminal")
    exports = dict(zip(names, words))
    symbols = image["symbols"]
    if words[:3] != (0x564D4F31, 1, 52) or exports["lease_bytes"] != 0x30000:
        raise ValueError("Observer exports identity differs from profile")
    for key, symbol in (("initialize", "_kui_vmu_maple_observer_init"),
                        ("hook", "_kui_vmu_maple_observer_hook"),
                        ("terminal", "_kui_vmu_maple_observer_terminal"),
                        ("snapshot", "_kui_vmu_maple_observer_snapshot")):
        if exports[key] != symbols.get(symbol):
            raise ValueError("Worker export does not match linked symbol: " + key)
    if (not 0xACFD0000 <= exports["bss_begin"] <= exports["snapshot"] < exports["bss_end"]
            <= exports["stack_bottom"] < exports["stack_top"] <= 0xACFE0000
            or exports["stack_top"] - exports["stack_bottom"] != 4096
            or exports["worker_end"] != exports["stack_top"] - 0x20000000
            or exports["worker_end"] != image["memory_end"]):
        raise ValueError("Observer private BSS/stack is outside owned lease")
    for key in ("initialize", "hook", "terminal"):
        if not 0x8CFD0034 <= exports[key] < 0x8CFD0000 + len(payload) or exports[key] & 1:
            raise ValueError("Worker entry is outside cached executable image: " + key)
    return {"exports": exports, "binary_bytes": len(payload),
            "worker_sha256": hashlib.sha256(payload).hexdigest()}


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
    report["runtime"] = verify(module)
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
