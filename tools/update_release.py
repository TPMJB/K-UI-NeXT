#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a complete, admitted offline-update development release.

Run with the pinned KOS environment sourced. All component builders require
the same clean source commit. Historical adapters are exact approved pins;
no personal card profile or game image is accepted as a build input.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from update_package import ROOT, build_package, catalog, member_info, verify
from update_build_base import build_base
from update_build_modules import build_modules
from update_build_legacy import build_legacy


def source_state():
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT):
        raise ValueError("A complete update release requires a clean source commit")
    return head


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def owned_output(output, head):
    output = Path(output).resolve()
    if output == ROOT or ROOT.is_relative_to(output):
        raise ValueError("Output cannot be the repository or one of its parents")
    owner = output / "build-owner.json"
    if output.exists() and any(output.iterdir()):
        if not owner.is_file() or json.loads(owner.read_text()).get("commit") != head:
            raise ValueError("Output is not an existing build owned by this exact commit")
    output.mkdir(parents=True, exist_ok=True)
    owner.write_text(json.dumps({"commit": head, "purpose": "K-UI offline updater build"}, indent=2) + "\n")
    return output


def build_release(output, *, legacy_dir=None, legacy_archives=(), jobs=2, channel="testing"):
    head = source_state()
    if not 1 <= jobs <= 16:
        raise ValueError("Use between 1 and 16 compiler jobs")
    if legacy_dir is None and not legacy_archives:
        raise ValueError("Supply the approved legacy component directory or previous release archives")
    output = owned_output(output, head)
    stage, evidence = output / "KUI", output / "evidence"
    stage.mkdir(exist_ok=True)
    evidence.mkdir(exist_ok=True)
    # Sequential helpers avoid collisions in shared SDK/profile support builds.
    base = build_base(stage, head[:12], jobs=jobs, evidence_dir=evidence / "base")
    native = build_modules(stage, head[:12], jobs=jobs,
        build_root=ROOT / "build/update-modules" / head[:12], evidence_dir=evidence / "native")
    with (evidence / "native-slot-audit.log").open("w") as log:
        subprocess.run([sys.executable, str(ROOT / "tests/test_native_controller_slot_audit.py"),
            str(ROOT / "build/update-modules" / head[:12] / "background-a2"),
            str(ROOT / "build/update-modules" / head[:12] / "background-a1")], cwd=ROOT,
            stdout=log, stderr=subprocess.STDOUT, check=True)
    legacy = build_legacy(stage, head[:12], jobs=jobs, legacy_dir=legacy_dir,
        legacy_archives=legacy_archives, evidence_dir=evidence / "legacy",
        build_root=ROOT / "build/update-legacy" / head[:12])
    admitted = {}
    for name, report in (("base", base), ("native", native), ("legacy", legacy)):
        (evidence / (name + ".json")).write_text(json.dumps(report, indent=2) + "\n")
        for member in report["members"]:
            path = member["path"]
            if path in admitted or member.get("validated") is not True:
                raise ValueError("Duplicate or unvalidated component: " + path)
            actual = member_info(path, (stage / path).read_bytes(), head[:12])
            if actual["sha256"] != member["sha256"] or actual["bytes"] != member["bytes"]:
                raise ValueError("Admitted component changed: " + path)
            admitted[path] = actual
    if set(admitted) != set(catalog()):
        raise ValueError("Component builders did not produce the complete update catalog")
    subprocess.run([sys.executable, str(ROOT / "tools/make_loader_probe.py"),
        str(stage / "apps/games/probe.dat")], cwd=ROOT, check=True)
    payload = output / ("K-UI-updater-" + channel + "-" + head[:12] + ".kup")
    package = build_package(stage, payload, head[:12], channel)
    if package != verify(payload) or source_state() != head:
        raise ValueError("Release source or package changed during build")
    result = {"kind": "offline-updater-development", "source_commit": head, "build": head[:12],
        "source_dirty": False, "channel": channel, "complete_cohort": True,
        "hardware_acceptance": False, "payload": payload.name, "payload_sha256": digest(payload),
        "package": package, "members": [admitted[path] for path in catalog()],
        "baseline_fixture": {"path": "apps/games/probe.dat", "sha256": digest(stage / "apps/games/probe.dat")}}
    (output / "build.json").write_text(json.dumps(result, indent=2) + "\n")
    checksums = [(payload.name, digest(payload))] + [("KUI/" + path, admitted[path]["sha256"]) for path in catalog()]
    checksums.append(("KUI/apps/games/probe.dat", result["baseline_fixture"]["sha256"]))
    (output / "SHA256SUMS").write_text("".join(sha + "  " + path + "\n" for path, sha in checksums))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--legacy-dir", type=Path)
    parser.add_argument("--legacy-archive", action="append", type=Path, default=[])
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--channel", choices=("stable", "testing"), default="testing")
    args = parser.parse_args()
    try:
        result = build_release(args.output, legacy_dir=args.legacy_dir,
            legacy_archives=args.legacy_archive, jobs=args.jobs, channel=args.channel)
        print(json.dumps({"build": result["build"], "complete_cohort": result["complete_cohort"],
            "members": len(result["members"]), "payload": str(args.output / result["payload"]),
            "hardware_acceptance": False}, indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
