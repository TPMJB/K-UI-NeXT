#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the production native continuation C glue against closed boundaries.

The sampler, coherent query, fence MMIO and fixed runtime are independent
boundary models. Publication is the real production publisher. No guest or
provider I/O is executed; actual extracted sequencing is tested under ASAN.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ["-DKUI_ARMADA_CE_MAPLE_A2=1", "-DKUI_ARMADA_CE_MAPLE_FENCE=1",
         "-DKUI_ARMADA_CE_MAPLE_WRITE_LAYOUT=1", "-DKUI_ARMADA_CE_MAPLE_WRITE_STAGING=1",
         "-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=1", "-DKUI_ARMADA_CE_MAPLE_AUTO_STOP=6",
         "-DKUI_ARMADA_CE_MAPLE_AUTO_PHASE=2", "-DKUI_ARMADA_CE_MAPLE_AUTO_LIMIT=36000"]
FUNCTIONS = ["a2_copy_words", "a2_same_words", "a2_auto_capture", "a2_fault", "a2_extension_word", "a2_backend_call", "a2_minimum", "a2_run", "a2_held",
             "kui_armada_ce_maple_runtime_owner", "kui_armada_ce_maple_runtime_wait", "a2_terminal", "a2_service", "a2_pending",
             "kui_armada_ce_maple_traffic_run"]


def definition(source, name):
    pattern = re.compile(r"(?m)^(?:static\s+)?(?:unsigned|int|void|_Bool)\s+"
                         r"(?:__attribute__\s*\(\([^\n]*\)\)\s*)?" + re.escape(name) + r"\s*\(")
    matches = list(pattern.finditer(source))
    definitions = []
    for match in matches:
        opening = source.find("{", match.end())
        semicolon = source.find(";", match.end())
        if semicolon != -1 and semicolon < opening:
            continue
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if not depth:
                    definitions.append(source[match.start():index + 1])
                    break
    if len(definitions) != 1:
        raise ValueError(f"Expected one production definition for {name}: {len(definitions)}")
    return definitions[0]


def main():
    compiler = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory(prefix="kui-continuation-") as temporary:
        directory = Path(temporary)
        preprocessed = subprocess.check_output(
            [compiler, "-E", "-P", "-std=c11", "-I" + str(ROOT / "include"), *FLAGS,
             str(ROOT / "src/loader/armada_ce_maple_traffic.c")], text=True)
        state = re.search(r"(?ms)^static struct \{\n\s*uint32_t pending,rounds,raw,mode,sr,result,checked;.*?\} a2_continuation;",
                          preprocessed)
        if not state:
            raise ValueError("Production continuation state not found")
        extracted = directory / "production.inc"
        bodies = "\n\n".join(definition(preprocessed, name) for name in FUNCTIONS)
        # Fixed CPU/KData loads are the platform boundary, not host addresses.
        # Keep every branch/comparison intact while supplying controlled live
        # register values; low SH addresses lie in the host ASAN shadow gap.
        bodies, loads = re.subn(r"\*\(volatile const uint32_t \*\)\(uintptr_t\)(0x[0-9a-f]+[uU])",
                               r"test_register(\1)", bodies)
        if loads != 15:
            raise ValueError(f"Expected15 fixed platform reads, found {loads}")
        bodies, stores = re.subn(r"\*\(volatile uint32_t \*\)\(uintptr_t\)(0x[0-9a-f]+[uU])",
                                r"REG(\1)", bodies)
        if stores != 2:
            raise ValueError(f"Expected2 fixed diagnostic stores, found {stores}")
        substitutions = {
            '__asm__ volatile("stc sr,%0":"=r"(saved));': 'saved=test_sr;',
            '__asm__ volatile("ldc %0,sr"::"r"(masked):"t","memory");': 'test_sr=masked;',
            '__asm__ volatile("ldc %0,sr"::"r"(saved):"t","memory");': 'test_sr=saved;'}
        for before, after in substitutions.items():
            if bodies.count(before) != 1:
                raise ValueError("Expected exactly one platform boundary: " + before)
            bodies = bodies.replace(before, after)
        extracted.write_text(state.group() + "\n" + bodies + "\n")
        output = directory / "test"
        subprocess.run([compiler, "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-fno-omit-frame-pointer",
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                        "-I" + str(ROOT / "include"), "-I" + str(directory), *FLAGS,
                        str(ROOT / "tests/test_armada_ce_maple_continuation.c"),
                        str(ROOT / "src/loader/armada_ce_vmu_a2_publish.c"),
                        "-o", str(output)], check=True)
        environment = dict(os.environ)
        environment["ASAN_OPTIONS"] = "detect_leaks=0"
        subprocess.run([str(output)], check=True, env=environment)


if __name__ == "__main__":
    main()
