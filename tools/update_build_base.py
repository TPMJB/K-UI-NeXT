#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and admit the first 15 members of the offline update catalogue.

Source .deps/kos/environ.sh before invoking this helper. Final staging requires
one clean commit and preserves the existing component-specific release gates.
Development --build-only runs never stage or admit an update package.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from package import (inspect_image_probe, inspect_probe, inspect_retail, inspect_zso,
                     storage_image, validate_native_release_baseline,
                     validate_ordinary_ce, write_native_cdda_modules,
                     write_wince_cdda_modules)
from package_armada_excerpt import verify_build
from runtime_package import envelope, flatten_elf
from update_package import CAPABILITY, catalog, member_info

BASE_MEMBERS = (
    "runtime.kui", "apps/games/probe.kui", "apps/games/image-probe.kui",
    "apps/games/retail-boot.kui", "apps/games/zso-boot.kui",
    "apps/games/lzo-boot.kui", "apps/games/ce-probe.kui",
    "apps/games/ce-cdda-armada.kui", "apps/games/ce-cdda-bam4.kui",
    "apps/games/ce-cdda-worms.kui", "apps/games/ce-cdda-wwp.kui",
    "apps/games/cdda-native-toy-raw.kui", "apps/games/cdda-native-toy-cooked.kui",
    "apps/games/cdda-native-mdk2.kui", "apps/games/retail-physical.kui",
)
NATIVE_FLAGS = (
    "KUI_RETAIL_LOW_RESIDENT=1", "KUI_RETAIL_STARTUP_TRACE=0",
    "KUI_RETAIL_SONIC_STACK_TEST=0", "KUI_GENERAL_SCI_RESUME_FIX=1",
    "KUI_CE_SCI_RESUME_FIX=1", "KUI_RETAIL_RETURN_TRACE=0",
    "KUI_RETAIL_NATIVE_DISC_CHECK=1", "KUI_RETAIL_NATIVE_DRIVE_STATE=1",
)
WINCE_FLAGS = (
    "KUI_RETAIL_LOW_RESIDENT=0", "KUI_RETAIL_STARTUP_TRACE=0",
    "KUI_RETAIL_SONIC_STACK_TEST=0", "KUI_GENERAL_SCI_RESUME_FIX=0",
    "KUI_CE_SCI_RESUME_FIX=1", "KUI_RETAIL_RETURN_TRACE=0",
    "KUI_RETAIL_NATIVE_DISC_CHECK=0", "KUI_RETAIL_NATIVE_DRIVE_STATE=0",
)
ORDINARY_TARGETS = (
    "build/loader/entry.elf", "build/loader/image_entry.elf",
    "build/retail/entry.elf", "native-zso", "native-lzo",
    "build/retail-ce/entry.elf", "retail-physical",
)


def source_state():
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                     text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain",
                                         "--untracked-files=all"], cwd=ROOT))
    return commit, dirty


def checked_directory(path):
    """Never stage through an existing symlink."""
    path = Path(path).absolute()
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise ValueError("Symlink in output directory: " + str(parent))
    path.mkdir(parents=True, exist_ok=True)
    return path


def build_base(stage, build_id, jobs=2, *, include_runtime=True,
               allow_dirty=False, evidence_dir=None, build_only=False):
    """Return build/admission evidence, writing only this group's fixed paths.

    The caller supplies the KUI staging directory. This group is deliberately
    incomplete: the other eleven catalogue members have separate producers.
    """
    if tuple(catalog()[:15]) != BASE_MEMBERS:
        raise ValueError("Base member recipes differ from the console catalogue")
    if not isinstance(jobs, int) or not 1 <= jobs <= 32:
        raise ValueError("jobs must be between 1 and 32")
    commit, dirty = source_state()
    if not re.fullmatch(r"[0-9a-f]{12}", build_id) or build_id != commit[:12]:
        raise ValueError("Build ID must identify the exact current source commit")
    if dirty and not (allow_dirty and build_only):
        raise ValueError("Final component admission requires clean committed source")
    if allow_dirty and not build_only:
        raise ValueError("--allow-dirty is limited to --build-only development checks")
    if not build_only and stage is None:
        raise ValueError("A KUI staging directory is required")
    evidence = checked_directory(evidence_dir or ROOT / "build/update-base-evidence")
    report = {"schema": "kui.update.base-build.v1", "source_commit": commit,
              "source_dirty": dirty, "build": build_id, "build_only": build_only,
              "expected_members": list(BASE_MEMBERS), "commands": [], "members": [],
              "base_members_complete": False, "update_cohort_prepared": False,
              "hardware_acceptance": False}

    def run(name, command, *, makeflags=None):
        log = evidence / (name + ".log")
        print("Building/checking " + name, flush=True)
        environment = None
        if makeflags is not None:
            environment = dict(os.environ, MAKEFLAGS=makeflags)
        with log.open("w") as output:
            result = subprocess.run(command, cwd=ROOT, stdout=output,
                                    stderr=subprocess.STDOUT, env=environment)
        report["commands"].append({"name": name, "argv": command,
                                   "log": str(log), "returncode": result.returncode,
                                   **({"makeflags": makeflags} if makeflags is not None else {})})
        if result.returncode:
            raise ValueError(name + " failed; inspect " + str(log))

    targets = list(ORDINARY_TARGETS)
    if include_runtime:
        targets.insert(0, "build/kui-runtime.elf")
    # Rebuild every production object for each candidate, even when only its
    # source identifier changed. None of these explicit targets rebuilds KOS.
    run("ordinary", ["make", "-B", "-f", "Makefile.dc", "-j" + str(jobs),
                     "BUILD_ID=" + build_id, *NATIVE_FLAGS, *targets])
    for profile in ("armada", "bam4", "worms", "wwp"):
        run("wince-" + profile,
            ["make", "-B", "-f", "Makefile." + profile + "-stream", "-j" + str(jobs),
             "BUILD_ID=" + build_id, *WINCE_FLAGS,
             "build/" + profile + "-ce-stream/entry.elf"])
    native_command = [sys.executable, "tools/build_native_cdda_compat.py",
                      "--jobs", str(jobs)]
    if allow_dirty:
        native_command.append("--allow-dirty")
    # The existing six-build driver invokes make internally; pass the same
    # always-make policy to all three references and three installed adapters.
    run("native-cdda", native_command, makeflags="--always-make")
    if source_state()[0] != commit:
        raise ValueError("Source commit changed while building the base members")
    if build_only:
        report["source_dirty_after_build"] = source_state()[1]
        return report
    if source_state() != (commit, False):
        raise ValueError("Source changed before final base-member admission")

    for name, arguments in (
        ("probe-layout", ["tools/check_loader_layout.py"]),
        ("image-probe-layout", ["tools/check_image_loader_layout.py"]),
        ("native-layout", ["tools/check_retail_loader_layout.py"]),
        ("native-instructions", ["tools/check_retail_instructions.py"]),
        ("ce-layout", ["tools/check_retail_loader_layout.py", "--ce", "build/retail-ce"]),
        ("ce-instructions", ["tools/check_retail_instructions.py", "build/retail-ce"]),
        ("zso-layout", ["tools/check_zso_loader.py", "build/retail-zso"]),
        ("lzo-layout", ["tools/check_zso_loader.py", "--codec", "lzo", "build/retail-lzo"]),
    ):
        run(name, [sys.executable, *arguments])

    stage = checked_directory(stage)
    games = checked_directory(stage / "apps/games")
    for relative in BASE_MEMBERS:
        if (stage / relative).is_symlink():
            raise ValueError("Symlink in staged member: " + relative)

    def admit(relative, elf_relative, inspector, validation=None, data=None):
        elf = ROOT / elf_relative
        compiled, _ = verify_build(elf, commit)
        payload, memory = flatten_elf(elf.read_bytes())
        if data is None:
            data = envelope(payload, memory, build_id)
        if data[64:] != payload:
            raise ValueError("Member differs from its actual linked ELF: " + relative)
        info = inspector(data)
        if validation is not None:
            validation(elf.parent, data)
        target = stage / relative
        checked_directory(target.parent)
        if target.is_symlink():
            raise ValueError("Symlink in staged member: " + relative)
        target.write_bytes(data)
        member = member_info(relative, data, build_id)
        member.update({"source_elf": str(elf), "compile": compiled,
                       "envelope": info, "validated": True})
        report["members"].append(member)

    if include_runtime:
        runtime, _ = storage_image((ROOT / "build/kui-runtime.elf").read_bytes(),
                                   build_id, "Updater GUI")
        if runtime[64:].count(CAPABILITY) != 1:
            raise ValueError("GUI immutable-bank capability is absent or duplicated")
        from runtime_package import verify
        admit("runtime.kui", "build/kui-runtime.elf", verify, data=runtime)
    admit("apps/games/probe.kui", "build/loader/entry.elf", inspect_probe)
    admit("apps/games/image-probe.kui", "build/loader/image_entry.elf", inspect_image_probe)
    baseline = validate_native_release_baseline(ROOT / "build/retail")
    admit("apps/games/retail-boot.kui", "build/retail/entry.elf", inspect_retail)
    report["native_release_baseline"] = baseline
    admit("apps/games/zso-boot.kui", "build/retail-zso/entry.elf", inspect_zso)
    admit("apps/games/lzo-boot.kui", "build/retail-lzo/entry.elf",
          lambda data: inspect_zso(data, "lzo"))
    admit("apps/games/ce-probe.kui", "build/retail-ce/entry.elf",
          lambda data: inspect_retail(data, ce=True), validate_ordinary_ce)

    wince = write_wince_cdda_modules(games, evidence, commit)
    native = write_native_cdda_modules(games, evidence, commit)
    for profiles in (wince, native):
        for proof in profiles.values():
            relative = proof["package"].removeprefix("KUI/")
            member = member_info(relative, (stage / relative).read_bytes(), build_id)
            member.update({"validated": True, "component_evidence": proof})
            report["members"].append(member)

    from check_retail_physical import audit as audit_physical
    physical_directory = ROOT / "build/retail-physical"
    physical = audit_physical(physical_directory)
    physical_data = (physical_directory / "retail-physical.kui").read_bytes()
    admit("apps/games/retail-physical.kui", "build/retail-physical/entry.elf",
          inspect_retail, data=physical_data)
    report["physical_standard"] = physical
    expected = BASE_MEMBERS if include_runtime else BASE_MEMBERS[1:]
    by_path = {member["path"]: member for member in report["members"]}
    if len(by_path) != len(report["members"]) or set(by_path) != set(expected):
        raise ValueError("The admitted base-member set differs from its fixed recipes")
    report["members"] = [by_path[name] for name in expected]
    if source_state() != (commit, False):
        raise ValueError("Source changed during base-member admission")
    report["base_members_complete"] = include_runtime
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, help="KUI directory of the new release")
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--skip-runtime", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    try:
        report = build_base(args.stage, args.build_id, args.jobs,
                            include_runtime=not args.skip_runtime,
                            allow_dirty=args.allow_dirty, evidence_dir=args.evidence_dir,
                            build_only=args.build_only)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"build": report["build"], "admitted_members": len(report["members"]),
                          "base_members_complete": report["base_members_complete"],
                          "report": str(args.report)}, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
