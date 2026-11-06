#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile real CE protocol/publication policy for A2 default and Swirl A1.

All fixture bytes are synthetic and hand authored. The test models bounded RX
memory and a synchronous provider contract; it proves no hardware behavior.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--json", type=Path, help="Optional scalar-only result report")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sources = [
        "tests/test_swirl_ce_vmu_target.c",
        "src/loader/armada_ce_vmu_a2_publish.c",
        "src/core/armada_ce_vmu_readonly.c",
        "src/core/armada_ce_vmu_write.c",
        "src/core/vmu_readonly.c",
        "src/core/vmu_emulation.c",
        "src/core/vmu_image.c",
    ]
    results = []
    rejected = []
    for name, header, definitions, diagnostic in [
        ("invalid-swirl-selector", "kui/ce_vmu_target.h", ["-DKUI_SMASH_SWIRL_VMU=2"],
         "KUI_SMASH_SWIRL_VMU"),
        ("swirl-readonly-address-mismatch", "kui/armada_ce_vmu_readonly.h",
         ["-DKUI_SMASH_SWIRL_VMU=1", "-DKUI_VMU_READONLY_ADDRESS=2"], "source-selected virtual address"),
    ]:
        command = shlex.split(args.cc) + ["-std=c11", "-Iinclude", "-x", "c", "-fsyntax-only"]
        process = subprocess.run(command + definitions + ["-"], input=f'#include "{header}"\n',
                                 cwd=root, capture_output=True, text=True)
        if not process.returncode or diagnostic not in process.stderr:
            raise RuntimeError(f"{name} did not reject the invalid configuration:\n{process.stderr}")
        rejected.append({"configuration": name, "status": "rejected-as-required"})
    environment = os.environ.copy()
    # Fixture owns only static storage. LeakSanitizer cannot enumerate /proc in
    # some managed runners; address/undefined checks remain enabled.
    environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
    with tempfile.TemporaryDirectory(prefix="kui-swirl-target-") as temp:
        for name, address, attachment, other_address, other_attachment, swirl in [
            ("default-a2", 2, "0x00020000u", 1, "0x00010000u", False),
            ("swirl-a1", 1, "0x00010000u", 2, "0x00020000u", True),
        ]:
            binary = Path(temp) / name
            command = shlex.split(args.cc) + [
                "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                "-fno-pie", "-no-pie", "-Iinclude", "-DKUI_ARMADA_CE_MAPLE_WRITE_STAGING=1",
                f"-DKUI_VMU_READONLY_ADDRESS={address}", f"-DTEST_EXPECTED_ADDRESS={address}u",
                f"-DTEST_EXPECTED_INDEX={address}u", f"-DTEST_EXPECTED_ATTACHMENT={attachment}",
                f"-DTEST_OTHER_ADDRESS={other_address}u", f"-DTEST_OTHER_ATTACHMENT={other_attachment}",
            ]
            if swirl:
                command += ["-DKUI_SMASH_SWIRL_VMU=1"]
            command += sources + ["-o", str(binary)]
            subprocess.run(command, cwd=root, check=True)
            process = subprocess.run([str(binary)], capture_output=True, text=True, env=environment)
            if process.returncode:
                raise RuntimeError(f"{name} failed ({process.returncode}):\n{process.stderr}")
            result = json.loads(process.stdout)
            if result["address"] != address or result["hardware_executed"]:
                raise RuntimeError(f"Unexpected result from {name}: {result}")
            result.update(profile=name, status="passed", sanitizers=["address", "undefined"])
            results.append(result)
    report = {"test": "ce-vmu-target-selection", "results": results,
              "negative_compile_checks": rejected, "hardware_executed": False}
    output = json.dumps(report, indent=2) + "\n"
    if args.json:
        args.json.write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
