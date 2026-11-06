#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/admit the six native Background, standalone and GD update members.

Source .deps/kos/environ.sh first. This helper never reads personal SD/card
profiles. Final admission requires a clean, unchanged commit; development
--build-only --allow-dirty runs do not stage installable cohort members.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from check_vmu_native_worker_gateway import read_elf
from package_large_disc_test import capability, native_build, stage_binary, vmu_build
from package_native_compat_test import background_module, standalone_module
from package_physical_disc_test import ordinary, provenance, vmu_layout
from retail_package import inspect_retail
from runtime_package import ELF_HEADER, PROGRAM_HEADER, envelope, flatten_elf, verify
from update_package import catalog, member_info
from vmu_persist_profile import template_profile
from vmu_native_relocations import generate as relocation_table

MODULE_MEMBERS = (
    "apps/games/retail-native-dual.kui",
    "apps/games/retail-native-dual-a1.kui",
    "apps/games/retail-native-standalone.kui",
    "apps/disc/native-disc.kui",
    "apps/disc/native-disc-vmu.kui",
    "apps/disc/native-disc-vmu-a1.kui",
)
RECIPES = (
    ("background-a2", "Makefile.vmu-native-background-compact", "all",
     "retail-native-background-compact.kui", MODULE_MEMBERS[0], "A2"),
    ("background-a1", "Makefile.vmu-native-background-compact-a1", "all",
     "retail-native-background-compact.kui", MODULE_MEMBERS[1], "A1"),
    ("standalone", "Makefile.native-standalone-general", None,
     "retail-native-standalone.kui", MODULE_MEMBERS[2], None),
    ("disc-native", "Makefile.disc-native", "all",
     "native-disc.kui", MODULE_MEMBERS[3], None),
    ("disc-a2", "Makefile.disc-vmu", "disc-vmu",
     "native-disc-vmu.kui", MODULE_MEMBERS[4], "A2"),
    ("disc-a1", "Makefile.disc-vmu-a1", "disc-vmu",
     "native-disc-vmu-a1.kui", MODULE_MEMBERS[5], "A1"),
)
ELFS = {
    "background": ("entry", "stage", "resident-sci", "resident-legacy",
                   "worker", "native-worker", "native-ra-aux"),
    "standalone": ("entry", "stage", "resident-sci"),
    "disc-native": ("entry", "stage"),
    "disc-vmu": ("disc-entry", "disc-stage", "native-worker", "support/support"),
}
LIMIT = 12 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def source_state():
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                     text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain",
                                         "--untracked-files=all"], cwd=ROOT))
    return commit, dirty


def checked_directory(path):
    path = Path(path).absolute()
    for parent in (path, *path.parents):
        require(not parent.is_symlink(), "Symlink in generated directory: " + str(parent))
    # Paths are inserted into C/assembler quoted include filenames by the
    # existing makefiles, so disallow shell/make syntax and spaces here.
    require(re.fullmatch(r"/[A-Za-z0-9_./-]+", str(path)) is not None,
            "Generated directory requires a plain absolute path")
    path.mkdir(parents=True, exist_ok=True)
    return path


def absolute_symbol(directory, filename, name, value):
    require(read_elf(directory / filename)[4].get(name) == (value, 0, 0, 0xfff1),
            filename + " lacks its exact linked identity: " + name)


def bind_entry(directory, module, entry="entry"):
    payload, memory = flatten_elf(ordinary(directory / (entry + ".elf")))
    require(module == envelope(payload, memory, verify(module)["build"]),
            "Module differs from its actual linked entry: " + str(directory))
    return payload


def standalone_resident(directory):
    resident = read_elf(directory / "resident-sci.elf")
    symbols = resident[4]
    require(symbols.get("__kui_native_standalone_reader_resident") == (1, 0, 0, 0xfff1),
            "Standalone low resident lacks its reader-only marker")
    require(symbols["_start"][0] == 0x8c004000 and
            symbols["__retail_hook_stack_bottom"][0] == 0x8c007ba0 and
            symbols["__retail_resident_bss_end"][0] <= 0x8c007ba0 and
            symbols["_standalone_reader_region"][1] == 0x760 and
            symbols["_manifest"][1] == 1020,
            "Standalone retained reader/map escaped the reviewed low reservation")
    require(not any("readahead" in name or "passive" in name or "vmu_observe" in name
                    for name in symbols), "Standalone resident contains a virtual/high worker")
    raw = ordinary(directory / "resident-sci.bin")
    header = ELF_HEADER.unpack_from(resident[0])
    require(header[9:11] == (PROGRAM_HEADER.size, 1),
            "Standalone resident requires one static load segment")
    kind, offset, address, physical, size, memory, flags, _ = PROGRAM_HEADER.unpack_from(
        resident[0], header[5])
    require(kind == 1 and address == physical == 0x8c004000 and flags == 7 and
            size == len(raw) and size <= memory <= 0x3ba0 and
            raw == resident[0][offset:offset + size],
            "Standalone low resident ELF/raw binary differ")
    stage = read_elf(directory / "stage.elf")[4]
    begin = stage["__retail_resident_sci_blob_start"][0] - 0x8ce00000
    end = stage["__retail_resident_sci_blob_end"][0] - 0x8ce00000
    require(0 <= begin < end and ordinary(directory / "stage.bin")[begin:end] == raw,
            "Standalone high stage embeds a different retained reader")
    return {"bss_end": symbols["__retail_resident_bss_end"][0],
            "stack_bottom": 0x8c007ba0, "reader_region_bytes": 0x760,
            "manifest_bytes": 1020, "embedded_bytes": len(raw)}


def linked_files(directory, family, commit, clean):
    proofs = {}
    for stem in ELFS[family]:
        elf = directory / (stem + ".elf")
        data = ordinary(elf)
        require(ordinary(directory / (stem + ".map")), "Empty linked map: " + stem)
        record = directory / (stem + ".compile.json")
        if clean:
            provenance(record, data, commit, True)
        else:
            found = json.loads(ordinary(record))
            require(found["commit"] == commit and
                    found["elf_sha256"] == hashlib.sha256(data).hexdigest(),
                    "Development linked receipt differs: " + str(record))
        proofs[stem] = {"elf": str(elf), "map": str(elf.with_suffix(".map")),
                        "compile_record": str(record),
                        "sha256": hashlib.sha256(data).hexdigest()}
    return proofs


def inspect_component(directory, name, build, slot, commit, clean):
    """Byte/ELF identities supplement (rather than replace) linked audits."""
    family = ("background" if name.startswith("background") else
              "disc-vmu" if name in ("disc-a1", "disc-a2") else name)
    proof = {"linked": linked_files(directory, family, commit, clean)}
    if family == "background":
        data = background_module(directory, build, slot)
        payload = bind_entry(directory, data)
        proof["envelope"] = inspect_retail(data)
        require(payload[0x2000:] == ordinary(directory / "stage.bin"),
                "Background entry embeds a different high stage")
        stage_binary(ordinary(directory / "stage.elf"), ordinary(directory / "stage.bin"), True)
        require(ordinary(directory / "vmu-sd-profile.bin") == template_profile(),
                "Background must contain only the blank nonpersonal SD template")
        absolute_symbol(directory, "stage.elf", "__kui_native_background_compact_stage", 1)
        absolute_symbol(directory, "native-worker.elf", "__kui_native_background_compact_worker", 1)
        absolute_symbol(directory, "stage.elf", "__kui_native_isolation_lease_bytes", 0x47000)
        proof.update({"virtual_slot": slot, "worker_bytes": 0x12000,
                      "complete_lease_bytes": 0x47000,
                      "storage": "SCI persistent worker; blank launcher-filled manifest"})
    elif family == "standalone":
        data = standalone_module(directory, build)
        payload = bind_entry(directory, data)
        proof["envelope"] = inspect_retail(data)
        require(payload[0x2000:] == ordinary(directory / "stage.bin"),
                "Standalone entry embeds a different high stage")
        stage_binary(ordinary(directory / "stage.elf"), ordinary(directory / "stage.bin"), True)
        absolute_symbol(directory, "stage.elf", "__kui_native_standalone_structural_stage", 1)
        require("__kui_native_standalone_doa2_stage" not in read_elf(directory / "stage.elf")[4],
                "Exact-game standalone pilot cannot fill the structural catalog slot")
        descriptor = struct.pack("<8I", 0x3153444b, 0x454e4f4c, 1, 32, 1, 0, 0, 0)
        require(payload.count(descriptor) == 1,
                "Standalone requires one no-VMU/no-lease structural capability")
        require(b"NEW STANDALONE DEMAND READER\0" in payload and
                b"DOA2 NEW STANDALONE DEMAND READER\0" not in payload,
                "Standalone module has the wrong reader identity")
        proof.update({"virtual_slot": None, "complete_lease_bytes": 0,
                      "storage": "SCI structural SDK demand reader",
                      "retained_reader": standalone_resident(directory),
                      "owner_game_proof_required_for_build": False})
    elif family == "disc-native":
        data = ordinary(directory / "native-disc.kui")
        payload = bind_entry(directory, data)
        proof["envelope"] = capability(data, 1, LIMIT)
        require(payload[0x2000:] == ordinary(directory / "stage.bin"),
                "Physical disc entry embeds a different high stage")
        stage_binary(ordinary(directory / "stage.elf"), ordinary(directory / "stage.bin"), False)
        if clean:
            native_build(directory, commit, LIMIT)
        proof.update({"virtual_slot": None, "native_executable_limit_bytes": LIMIT,
                      "storage": "physical GD-ROM; no virtual worker"})
    else:
        data = ordinary(directory / "native-disc-vmu.kui")
        payload = bind_entry(directory, data, "disc-entry")
        proof["envelope"] = capability(data, 3, LIMIT)
        require(payload[0x2000:] == ordinary(directory / "disc-stage.bin"),
                "Physical VMU entry embeds a different high stage")
        stage_binary(ordinary(directory / "disc-stage.elf"),
                     ordinary(directory / "disc-stage.bin"), True)
        # The GD stage takes its prepared card identity from the console's
        # launch request and does not embed an SD profile. If an old generated
        # directory happens to contain one, it must still be the blank seed.
        if (directory / "vmu-sd-profile.bin").exists():
            require(ordinary(directory / "vmu-sd-profile.bin") == template_profile(),
                    "Physical VMU build directory contains a personal SD profile")
        unit = int(slot[-1])
        absolute_symbol(directory, "disc-stage.elf", "__kui_disc_vmu_virtual_slot", unit)
        absolute_symbol(directory, "native-worker.elf", "__kui_native_physical_disc_virtual_slot", unit)
        require(ordinary(directory / "disc-vmu-slot-config") ==
                f"DISC_VMU_VIRTUAL_A1={int(slot == 'A1')}\n".encode(),
                "Physical VMU slot config differs from linked slot")
        caption = f"PHYSICAL DISC / VIRTUAL {slot} / SCI\0".encode()
        other = "A1" if slot == "A2" else "A2"
        require(caption in payload and
                f"PHYSICAL DISC / VIRTUAL {other} / SCI\0".encode() not in payload,
                "Physical VMU has a conflicting slot/storage identity")
        for stem, base, size in (("native-worker", 0x8c100000, vmu_layout()[0]),
                                 ("support/support", 0x8c100000 + vmu_layout()[0], vmu_layout()[1])):
            require(ordinary(directory / (stem + ".reloc")) == relocation_table(
                    ordinary(directory / (stem + ".elf")),
                    ordinary(directory / (stem + ".bin")), base, size),
                    "Physical VMU worker/support ELF/binary/relocations disagree")
        if slot == "A1":
            require(ordinary(directory / "native-disc-vmu-a1.kui") == data,
                    "Physical A1 delivery alias differs")
        if clean:
            vmu_build(directory, commit, LIMIT, slot)
        proof.update({"virtual_slot": slot, "native_executable_limit_bytes": LIMIT,
                      "storage": "physical GD-ROM plus SCI persistent virtual card"})
    require(verify(data)["build"] == build, "Mixed build identifier in native module group")
    return data, proof


def build_modules(stage, build_id, jobs=2, *, build_root=None, evidence_dir=None,
                  allow_dirty=False, build_only=False):
    """Return six fixed members' evidence; stage is the destination KUI directory.

    Existing generated objects are force-rebuilt, so a previous toolchain or
    command line cannot supply a stale production component. All six complete
    admission checks finish before this group writes any staging member.
    """
    require(all(path in catalog() for path in MODULE_MEMBERS),
            "Native module recipes differ from the console catalogue")
    require(type(jobs) is int and 1 <= jobs <= 32, "jobs must be between 1 and 32")
    commit, dirty = source_state()
    require(re.fullmatch(r"[0-9a-f]{12}", build_id) and build_id == commit[:12],
            "Build ID must identify the exact current source commit")
    require(not dirty or (allow_dirty and build_only),
            "Final native-module admission requires clean committed source")
    require(not allow_dirty or build_only, "--allow-dirty requires --build-only")
    require(stage is not None or build_only, "A KUI staging directory is required")
    build_root = checked_directory(build_root or ROOT / "build/update-modules" / build_id)
    evidence = checked_directory(evidence_dir or build_root / "evidence")
    report = {"schema": "kui.update.native-modules-build.v1", "source_commit": commit,
              "source_dirty": dirty, "build": build_id, "build_only": build_only,
              "build_root": str(build_root), "expected_members": list(MODULE_MEMBERS),
              "commands": [], "members": [], "native_modules_complete": False,
              "update_cohort_prepared": False, "hardware_acceptance": False}

    def run(name, command):
        log = evidence / (name + ".log")
        print("Building/checking " + name, flush=True)
        with log.open("w") as output:
            result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        report["commands"].append({"name": name, "argv": command, "log": str(log),
                                   "returncode": result.returncode})
        require(result.returncode == 0, name + " failed; inspect " + str(log))

    admitted = []
    for name, makefile, target, filename, relative, slot in RECIPES:
        directory = checked_directory(build_root / name)
        # Its two phony support outputs share one recursive link directory.
        # Keep the physical-VMU producer serial even when the other recipes
        # compile independent objects concurrently.
        component_jobs = 1 if name in ("disc-a1", "disc-a2") else jobs
        command = ["make", "-B", "-f", makefile, "-j" + str(component_jobs),
                   "BUILD=" + str(directory), "BUILD_ID=" + build_id,
                   target or str(directory / filename)]
        run(name, command)
        require(source_state()[0] == commit, "Source commit changed during native module build")
        family = ("background" if name.startswith("background") else
                  "disc-vmu" if name in ("disc-a1", "disc-a2") else name)
        # The original makefiles record the launch entry, but several older
        # profiles predate receipts for their subordinate linked components.
        for stem in ELFS[family]:
            run(name + "-receipt-" + stem.replace("/", "-"),
                [sys.executable, "tools/record_build.py", str(directory / (stem + ".elf"))])
        if family == "background":
            run(name + "-dual-audit", [sys.executable, "tools/check_native_dual_readers.py",
                                       str(directory), "--self-test"])
        elif family == "standalone":
            run(name + "-structural-host", ["make", "-f", makefile, "BUILD=" + str(directory),
                "BUILD_ID=" + build_id, "standalone-structural-host-tests"])
            run(name + "-instructions", [sys.executable, "-c",
                "import subprocess,sys;sys.path.insert(0,'tools');"
                "from check_retail_instructions import audit;"
                "d=sys.argv[1];print(sum(audit(n,subprocess.check_output("
                "['sh-elf-objdump','-d',d+'/'+n+'.elf'],text=True)) "
                "for n in ('entry','stage','resident-sci')))", str(directory)])
        data, proof = inspect_component(directory, name, build_id, slot, commit, not build_only)
        member = member_info(relative, data, build_id)
        member.update({"validated": True, "component": name, "build_directory": str(directory),
                       "component_evidence": proof})
        admitted.append((relative, data, member))
    require(source_state()[0] == commit, "Source commit changed during native module checks")
    if not build_only:
        require(source_state() == (commit, False), "Source changed before final native module staging")
        stage = checked_directory(stage)
        for relative, data, member in admitted:
            target = stage / relative
            checked_directory(target.parent)
            require(not target.is_symlink(), "Symlink in staged module: " + str(target))
            target.write_bytes(data)
            require(ordinary(target) == data, "Staged native module readback differs")
        require(source_state() == (commit, False), "Source changed during native module staging")
        report["native_modules_complete"] = True
    report["members"] = [member for _, _, member in admitted]
    report["source_dirty_after_build"] = source_state()[1]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", "--output", type=Path, help="Destination KUI directory")
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    try:
        report = build_modules(args.stage, args.build_id, args.jobs, build_root=args.build_root,
                               evidence_dir=args.evidence_dir, allow_dirty=args.allow_dirty,
                               build_only=args.build_only)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"build": report["build"], "checked_members": len(report["members"]),
                          "native_modules_complete": report["native_modules_complete"],
                          "report": str(args.report)}, indent=2))
    except (OSError, ValueError, KeyError, AssertionError, struct.error) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
