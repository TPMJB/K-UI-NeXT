#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Report native service geometry from effective recipes and production headers.

This is a source/configuration inventory. Header constants are not linked
geometry. It does not inspect installed modules, replace their linked audits,
admit a game, or establish console compatibility.
No target objects are built: Make only expands a final reporting makefile, and
the host compiler evaluates the same geometry headers under those definitions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROFILES = {
    "standard_vmu_only_72": "Makefile.vmu-native-vmu-lite-paced",
    "background_compact_72": "Makefile.vmu-native-background-compact",
    "background_session_112": "Makefile.vmu-native-session-controls",
    "new_standalone": "Makefile.native-standalone-general",
}
VARIABLES = ("FLAGS", "NATIVE_FLAGS", "RESIDENT_FLAGS", "NATIVE_LAYOUT", "NATIVE_SPAN", "MAKEFILE_LIST")
PROBE = r'''
#include <stdio.h>
#include "kui/native_isolation.h"
#include "kui/vmu_native_background.h"
#ifndef KUI_NATIVE_STANDALONE_READER
#define KUI_NATIVE_STANDALONE_READER 0
#endif
#ifndef KUI_NATIVE_SESSION_CONTROLS
#define KUI_NATIVE_SESSION_CONTROLS 0
#endif
int main(void) {
 printf("%u %u %u %u %u %u %u\n",(unsigned)KUI_NATIVE_STANDALONE_READER,
 (unsigned)KUI_NATIVE_VMU_ONLY,(unsigned)KUI_NATIVE_READAHEAD,
 (unsigned)(KUI_NATIVE_STANDALONE_READER?0:KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES),
 (unsigned)(KUI_NATIVE_STANDALONE_READER?0:KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES),
 (unsigned)(KUI_NATIVE_READAHEAD?KUI_NATIVE_RA_DATA_BYTES:0),
 (unsigned)KUI_NATIVE_SESSION_CONTROLS);
}
'''
ROUTE_PROBE = r'''
#include <stdio.h>
#include "kui/native_standard_route.h"
#include "kui/native_background_route.h"
int main(void) {
 puts(kui_native_standard_package(false));
 puts(kui_native_standard_package(true));
 puts(kui_native_background_package(KUI_RETAIL_READER_ASYNC,false));
 puts(kui_native_background_package(KUI_RETAIL_READER_ASYNC,true));
 puts(kui_native_background_package(KUI_RETAIL_READER_ASYNC_EAGER,false));
}
'''


def run(command: list[str], root: Path) -> str:
    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
    if result.returncode:
        raise ValueError(f"{command[0]} failed: {result.stderr.strip()}")
    return result.stdout


def effective_recipe(root: Path, recipe: str, directory: Path) -> dict[str, str]:
    reporting = directory / "inventory.mk"
    reporting.write_text("".join(f"$(info KUI_INV_{name}=$({name}))\n" for name in VARIABLES)
                         + ".PHONY: kui-service-inventory-no-build\n"
                           "kui-service-inventory-no-build:\n\t@:\n")
    # A private empty BUILD keeps preexisting generated dependency files out of
    # this source inventory. This never selects or rebuilds a runtime package.
    output = run(["make", "--no-print-directory", "-f", recipe, "-f", str(reporting),
                  f"BUILD={directory / 'unused-build'}", "BUILD_ID=inventory",
                  "kui-service-inventory-no-build"], root)
    values = {}
    for line in output.splitlines():
        if line.startswith("KUI_INV_"):
            key, value = line[len("KUI_INV_"):].split("=", 1)
            if key in values:
                raise ValueError(f"duplicate recipe report: {key}")
            values[key] = value
    if set(values) != set(VARIABLES):
        raise ValueError(f"incomplete recipe report: {recipe}")
    return values


def host_probe(root: Path, directory: Path, source: str, definitions: list[str], cc: str,
               sources: set[str]) -> str:
    src, binary, dependencies = directory / "probe.c", directory / "probe", directory / "probe.d"
    src.write_text(source)
    run([*shlex.split(cc), "-std=c11", "-Wall", "-Wextra", "-Werror", "-Iinclude", "-Iconfig", "-Ifatfs/source",
         *definitions, "-MMD", "-MF", str(dependencies), "-MT", "inventory_probe",
         str(src), "-o", str(binary)], root)
    # Bind every transitive non-system include emitted by the compiler. A
    # hand-maintained header list silently misses newly included contracts.
    dependency_text = dependencies.read_text().replace("\\\n", "")
    target, separator, prerequisites = dependency_text.partition(":")
    if target != "inventory_probe" or not separator:
        raise ValueError("invalid compiler dependency report")
    for name in shlex.split(prerequisites):
        path = (root / name).resolve()
        if path == src.resolve():
            continue
        if not path.is_relative_to(root):
            raise ValueError(f"production header outside source root: {name}")
        sources.add(path.relative_to(root).as_posix())
    return run([str(binary)], root)


def inventory(root: Path = ROOT, cc: str = "cc") -> dict:
    root = root.resolve()
    profiles, sources = {}, set()
    with tempfile.TemporaryDirectory(prefix="kui-service-inventory-") as temp:
        directory = Path(temp)
        for name, recipe in PROFILES.items():
            values = effective_recipe(root, recipe, directory)
            flag_variable = "RESIDENT_FLAGS" if name == "new_standalone" else "NATIVE_FLAGS"
            definitions = [word for word in shlex.split(values[flag_variable])
                           if word.startswith(("-D", "-U"))]
            measured = [int(value) for value in host_probe(root, directory, PROBE, definitions, cc, sources).split()]
            if len(measured) != 7:
                raise ValueError(f"invalid compiled geometry report: {recipe}")
            standalone, vmu_only, read_ahead, worker, lease, cache, session = measured
            if values["NATIVE_SPAN"] and int(values["NATIVE_SPAN"], 0) != worker:
                raise ValueError(f"recipe/header worker span mismatch: {recipe}")
            makefiles = [p for p in values["MAKEFILE_LIST"].split() if p != str(directory / "inventory.mk")]
            sources.update(makefiles)
            if values["NATIVE_LAYOUT"]:
                sources.add(values["NATIVE_LAYOUT"])
            profiles[name] = {
                "recipe": recipe, "effective_definition_source": flag_variable,
                "geometry_basis": "compiler-evaluated production header constants; not linked artifact measurements",
                "definitions": definitions, "included_makefiles": makefiles,
                "worker_linker_script": values["NATIVE_LAYOUT"] or None,
                "standalone_demand_reader": bool(standalone), "vmu_only": bool(vmu_only),
                "read_ahead_compiled": bool(read_ahead), "session_controls_compiled": bool(session),
                "worker_bytes": worker, "retained_lease_bytes": lease, "cache_bytes": cache,
            }
        packages = host_probe(root, directory, ROUTE_PROBE, [], cc, sources).splitlines()
    if len(packages) != 5:
        raise ValueError("invalid production route report")
    return {
        "schema": "kui.native-service-source-inventory.v1",
        "scope": "effective recipes and compiler-evaluated source constants; no linked-geometry, installed-module or console claim",
        "inventory_tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "profiles": profiles,
        "production_package_selection": dict(zip(
            ("standard_vmu_off", "standard_vmu_on", "new_a2", "new_a1", "legacy_a2"), packages)),
        "service_mode_interpretation": {
            "scope": "route associations for review; runtime service behavior is not executed by this inventory",
            "vmu_only": {"profile": "standard_vmu_only_72", "reader": "Standard"},
            "combined": {"profile": "background_compact_72", "reader": "New or Legacy"},
            "prefetch_with_vmu_off": {"profile": "background_compact_72",
                "reservation_basis": "same compiled profile; service disable is not evidence of reclaimed memory"},
            "neither_new_fallback": {"profile": "new_standalone",
                "selection": "fallback only after combined executable proof refuses"},
            "cacheless_new_with_vmu": {"profile": None, "status": "no production recipe in this inventory"},
        },
        "source_sha256": {path: hashlib.sha256((root / path).read_bytes()).hexdigest()
                          for path in sorted(sources)},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    try:
        result = inventory(args.root, args.cc)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"native service inventory refused: {exc}\n")
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
