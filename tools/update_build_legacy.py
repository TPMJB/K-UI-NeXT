#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the two persistent VMU templates and retain three approved CDDA files.

Source .deps/kos/environ.sh first. Final staging requires clean committed
source, the existing linked component audits, and the exact approved legacy
files. No card, game executable, SD image, download, or private profile is used.
--build-only --allow-dirty is a local link check and never stages a release.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import zipfile

from runtime_package import verify as verify_runtime
from vmu_persist_profile import template_profile

ROOT = Path(__file__).resolve().parents[1]
VMU_MEMBERS = {"silver": "apps/games/vmu-silver.kui", "toy": "apps/games/vmu-toy.kui"}
LEGACY_MEMBERS = ("apps/games/cdda-toy-raw.kui", "apps/games/cdda-toy-cooked.kui",
                  "apps/games/retail-cdda-mdk2.kui")
EXPECTED_MEMBERS = (*VMU_MEMBERS.values(), *LEGACY_MEMBERS)
_EXTRA_ARCHIVE_NAMES = {
    LEGACY_MEMBERS[0]: ("preserved-toy-tests/AP-raw-reference.kui",),
    LEGACY_MEMBERS[1]: ("preserved-toy-tests/AT-cooked-data.kui",),
}
_PRODUCTION_ADMISSION_SOURCE = r'''
#include "src/core/games_cdda_profile.h"
#include <stdio.h>
#include <stdlib.h>
static void require(bool value) {if(!value)exit(1);}
int main(int argc,char **argv) {
    require(argc==4);
    const enum kui_games_cdda_profile profiles[]={KUI_GAMES_CDDA_TOY_RAW,
        KUI_GAMES_CDDA_TOY_COOKED,KUI_GAMES_CDDA_MDK2_RAW};
    for(unsigned i=0;i<3u;i++) {
        FILE *file=fopen(argv[i+1u],"rb");require(file && !fseek(file,0,SEEK_END));
        long length=ftell(file);require(length>=68 && length<=1048576 && !fseek(file,0,SEEK_SET));
        uint8_t *bytes=malloc((size_t)length);require(bytes!=NULL);
        require(fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));
        struct kui_runtime_image image={.data=bytes+64u};
        require(kui_runtime_header(bytes,(uint64_t)length,&image.info)==KUI_RUNTIME_OK);
        require(kui_crc32(0u,image.data,image.info.payload_bytes)==image.info.crc32);
        for(unsigned p=0;p<3u;p++)
            require(kui_games_cdda_package_admit(profiles[p],&image)==(p==i));
        bytes[length-1]^=1u;require(!kui_games_cdda_package_admit(profiles[i],&image));bytes[length-1]^=1u;
        image.info.build[0]^=1u;require(!kui_games_cdda_package_admit(profiles[i],&image));
        free(bytes);
    }
    puts("PASS approved legacy CDDA: exact production admission, cross-profile and changed-byte/build refusals");
    return 0;
}
'''


def source_state():
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=all"], cwd=ROOT))
    return commit, dirty


def checked_directory(path):
    path = Path(path).absolute()
    for item in (path, *path.parents):
        if item.is_symlink():
            raise ValueError("Symlink in component directory: " + str(item))
    path.mkdir(parents=True, exist_ok=True)
    return path


def checked_output(path):
    path = Path(path).absolute()
    checked_directory(path.parent)
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise ValueError("Output must be an ordinary component file: " + str(path))
    return path


def validate_legacy(path, data, pins=None):
    """Use the same explicit whitelist as the console codec and packager."""
    if pins is None:
        from update_package import legacy_catalog
        pins = legacy_catalog()
    if path not in pins or path not in LEGACY_MEMBERS:
        raise ValueError("Unapproved historical component: " + path)
    pin = pins[path]
    if len(data) != pin["bytes"] or hashlib.sha256(data).hexdigest() != pin["sha256"]:
        raise ValueError("Historical component bytes differ from the approved pin: " + path)
    runtime = verify_runtime(data)
    if runtime["build"] != pin["build"]:
        raise ValueError("Historical component build differs from the approved pin: " + path)
    return {"path": path, "policy": 1, "bytes": len(data), "sha256": pin["sha256"],
            "build": runtime["build"], "runtime": runtime, "validated": True,
            "source_policy": "unchanged approved production CDDA adapter"}


def legacy_sources(legacy_dir=None, legacy_archives=()):
    """Read only fixed member names and exact lengths from caller-owned inputs."""
    from update_package import legacy_catalog
    pins = legacy_catalog()
    if set(pins) != set(LEGACY_MEMBERS):
        raise ValueError("Legacy component recipes differ from the console whitelist")
    found = {}
    if legacy_dir is not None:
        base = Path(legacy_dir).absolute()
        for relative in LEGACY_MEMBERS:
            target = base / relative
            for item in (target, *target.parents):
                if item.is_symlink():
                    raise ValueError("Symlink in historical component input: " + str(item))
            if target.is_file():
                if target.stat().st_size != pins[relative]["bytes"]:
                    raise ValueError("Historical component has the wrong length: " + relative)
                data = target.read_bytes()
                proof = validate_legacy(relative, data, pins)
                proof["source_file"] = str(target)
                found[relative] = (data, proof)
    for archive_path in legacy_archives:
        archive_path = Path(archive_path).absolute()
        for item in (archive_path, *archive_path.parents):
            if item.is_symlink():
                raise ValueError("Symlink in historical source archive: " + str(item))
        if not archive_path.is_file() or archive_path.stat().st_size > 128 * 1024 * 1024:
            raise ValueError("Historical source archive must be an ordinary bounded ZIP")
        archive_sha = hashlib.sha256(archive_path.read_bytes()).hexdigest()
        with zipfile.ZipFile(archive_path) as archive:
            names = archive.namelist()
            for relative in LEGACY_MEMBERS:
                candidates = ("KUI/" + relative, relative, *_EXTRA_ARCHIVE_NAMES.get(relative, ()))
                for name in candidates:
                    if name not in names:
                        continue
                    if names.count(name) != 1 or archive.getinfo(name).file_size != pins[relative]["bytes"]:
                        raise ValueError("Ambiguous or wrongly sized historical archive member: " + name)
                    data = archive.read(name)
                    proof = validate_legacy(relative, data, pins)
                    if relative in found and found[relative][0] != data:
                        raise ValueError("Historical sources disagree: " + relative)
                    proof.update({"source_archive": str(archive_path), "source_archive_sha256": archive_sha,
                                  "source_member": name})
                    found[relative] = (data, proof)
    if set(found) != set(LEGACY_MEMBERS):
        raise ValueError("Missing approved historical components: " + ", ".join(sorted(set(LEGACY_MEMBERS) - set(found))))
    return found


def build_legacy(stage, build_id, jobs=2, *, legacy_dir=None, legacy_archives=(),
                 evidence_dir=None, build_root=None, build_only=False, allow_dirty=False):
    """Build and admit exactly this group's five fixed catalogue members."""
    from update_package import catalog, member_info
    if any(path not in catalog() for path in EXPECTED_MEMBERS):
        raise ValueError("Legacy/template recipes differ from the console catalogue")
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
    evidence = checked_directory(evidence_dir or ROOT / "build/update-legacy-evidence")
    build_path = Path(build_root or ROOT / "build/update-legacy").absolute()
    if not re.fullmatch(r"[A-Za-z0-9_./-]+", str(build_path)):
        raise ValueError("Build directory contains characters unsupported by the fixed Make recipes")
    builds = checked_directory(build_path)
    profile_path = checked_output(builds / "blank-template.bin")
    template = template_profile()
    profile_path.write_bytes(template)
    template_sha = hashlib.sha256(template).hexdigest()
    report = {"schema": "kui.update.legacy-build.v1", "source_commit": commit,
              "source_dirty": dirty, "build": build_id, "build_only": build_only,
              "expected_members": list(EXPECTED_MEMBERS), "commands": [], "members": [],
              "template_sha256": template_sha, "template_is_public_non_owner": True,
              "legacy_members_complete": False, "update_cohort_prepared": False,
              "hardware_acceptance": False}

    def run(name, command, *, env=None):
        log = checked_output(evidence / (name + ".log"))
        print("Building/checking " + name, flush=True)
        with log.open("w") as output:
            result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, env=env)
        report["commands"].append({"name": name, "argv": command,
                                   "log": str(log), "returncode": result.returncode})
        if result.returncode:
            raise ValueError(name + " failed; inspect " + str(log))

    for profile in VMU_MEMBERS:
        directory = checked_directory(builds / profile)
        run(profile + "-persistent", ["make", "-f", "Makefile.vmu-" + profile,
            "BUILD=" + str(directory), "BUILD_ID=" + build_id, "READONLY_PROOF=4",
            "SD_PROFILE=" + str(profile_path), "-j" + str(jobs), "all"])
    if source_state()[0] != commit:
        raise ValueError("Source commit changed while building legacy/template members")
    if build_only:
        report["source_dirty_after_build"] = source_state()[1]
        return report
    if source_state() != (commit, False):
        raise ValueError("Source changed before final legacy/template admission")

    historical = legacy_sources(legacy_dir, legacy_archives)
    stage = checked_directory(stage)
    for profile, relative in VMU_MEMBERS.items():
        directory = builds / profile
        run(profile + "-artifact-audit", [sys.executable, "tools/package_vmu_maple_observe.py",
                                         str(directory), "--maple-profile", profile])
        audit = json.loads((directory / "observer-artifact-audit.json").read_text())
        expected_profile = 2 if profile == "silver" else 1
        data = (directory / "retail-maple-observe.kui").read_bytes()
        if (audit["maple_profile"]["id"] != expected_profile or audit["exports"]["readonly_mode"] != 4 or
                audit.get("source_commit") != commit or audit.get("source_dirty") is not False or
                audit.get("module_sha256") != hashlib.sha256(data).hexdigest() or
                audit.get("sd_profile_sha256") != template_sha or
                (directory / "vmu-sd-profile.bin").read_bytes() != template):
            raise ValueError("VMU component differs from its exact persistent public-template admission: " + profile)
        item = member_info(relative, data, build_id)
        item.update({"policy": 0, "validated": True, "component_audit": audit,
                     "source_elf": str(directory / "entry.elf")})
        report["members"].append(item)
        target = checked_output(stage / relative)
        target.write_bytes(data)
    for relative in LEGACY_MEMBERS:
        data, proof = historical[relative]
        item = member_info(relative, data, build_id)
        item.update(proof)
        target = checked_output(stage / relative)
        target.write_bytes(data)
        report["members"].append(item)
    checker_source = checked_output(evidence / "legacy-production-admission.c")
    checker = checked_output(evidence / "legacy-production-admission")
    checker_source.write_text(_PRODUCTION_ADMISSION_SOURCE)
    run("legacy-production-admission-build", [*shlex.split(os.environ.get("HOST_CC", "cc")),
        "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-I" + str(ROOT),
        "-I" + str(ROOT / "include"), "-I" + str(ROOT / ".deps/fatfs/source"),
        str(checker_source), "src/core/runtime_image.c", "src/core/hash.c",
        "src/core/data.c", "src/core/retail_image.c", "-o", str(checker)])
    run("legacy-production-admission", [str(checker), *(str(stage / name) for name in LEGACY_MEMBERS)],
        env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
    report["production_legacy_admission"] = True
    if source_state() != (commit, False):
        raise ValueError("Source changed during legacy/template admission")
    report["legacy_members_complete"] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, help="KUI directory of the new release")
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--legacy-dir", type=Path, help="Directory containing the three approved apps/games members")
    parser.add_argument("--legacy-archive", action="append", default=[], type=Path)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    try:
        report = build_legacy(args.stage, args.build_id, args.jobs,
            legacy_dir=args.legacy_dir, legacy_archives=args.legacy_archive,
            evidence_dir=args.evidence_dir, build_root=args.build_root,
            build_only=args.build_only, allow_dirty=args.allow_dirty)
        checked_output(args.report).write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"build": report["build"], "admitted_members": len(report["members"]),
                          "legacy_members_complete": report["legacy_members_complete"],
                          "report": str(args.report)}, indent=2))
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
