#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package two admitted clean builds for an actual offline upgrade test.

The initial KUI folder is a complete retained baseline. Its updates directory
contains the other build's .kup, so hardware testing exercises a real change
of build ID and the checked bank handoff. No games or user settings are read.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

from update_package import ROOT, catalog, member_info, verify


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def admitted_release(directory):
    directory = Path(directory).resolve()
    report = json.loads((directory / "build.json").read_text())
    commit = report.get("source_commit", "")
    if (report.get("kind") != "offline-updater-development" or report.get("complete_cohort") is not True or
            report.get("source_dirty") is not False or report.get("hardware_acceptance") is not False or
            report.get("channel") != "testing" or report.get("build") != commit[:12] or len(commit) != 40):
        raise ValueError("Not an admitted clean testing build: " + str(directory))
    name = report["payload"]
    if Path(name).name != name:
        raise ValueError("Invalid update payload leaf")
    payload = directory / name
    if digest(payload) != report["payload_sha256"] or verify(payload) != report["package"]:
        raise ValueError("Release package changed after admission")
    members = {entry["path"]: entry for entry in report["members"]}
    if len(members) != len(report["members"]) or set(members) != set(catalog()):
        raise ValueError("Incomplete release admission")
    package = report["package"]
    if (package["build"] != report["build"] or package["channel"] != "testing" or
            package["members"] != [members[path] for path in catalog()]):
        raise ValueError("Release payload and retained KUI cohort are different")
    for path in catalog():
        file = directory / "KUI" / path
        if file.is_symlink() or not file.is_file() or member_info(path, file.read_bytes(), report["build"]) != members[path]:
            raise ValueError("Admitted release member changed: " + path)
    fixture = directory / "KUI/apps/games/probe.dat"
    if digest(fixture) != report["baseline_fixture"]["sha256"]:
        raise ValueError("Baseline probe fixture changed")
    return directory, report, payload


def readme(baseline, candidate):
    return f"""# K-UI offline updater hardware test

This is a development test, not a public release. It has passed the component
build/admission checks and real FAT32/exFAT host installation/rollback tests.
Dreamcast execution, storage timing and game regressions still need testing.

Initial retained baseline: {baseline['build']}
Update target: {candidate['build']}

1. Power off the Dreamcast. Back up the existing KUI folder to your computer.
2. Merge this ZIP's KUI folder into the card's root, replacing the included
   executable files. Do not delete your existing KUI folder. Games, VMUs,
   preferences, covers and music are not included as replacement files.
3. Boot with your existing working K-UI boot CD. Confirm build
   {baseline['build']} in the header. No new boot CD is required.
4. Open Settings -> Updates. Select the supplied .kup and press A to check it.
   Review target {candidate['build']} and channel testing, then A to install.
5. Wait for the final result. START reviews a console restart; A confirms it.
   Boot K-UI again and confirm the header now shows {candidate['build']}.
6. Check your imported cards and settings, then launch familiar games. Use
   the reader and virtual-VMU options you normally use. Report failures with
   the build ID and a saved Diagnostics log or screen photo.
7. In Updates, use Y Restore previous, review, confirm and restart. Confirm
   {baseline['build']} returns. Reinstall the supplied update to test reuse.

Keep the console powered while writing. If activation cannot be confirmed,
restart and check the selected version. If the selected version will not
start, hold B while the retained updater runtime starts to use the original
baseline for that boot; then restore the baseline from Updates. Restore the
computer backup if the initial manual copy itself was interrupted.

The initial copy replaces root software once. Later updater installs preserve
that root baseline and use separate version folders. Updates do not guarantee
FAT metadata survives power loss and do not automatically detect a runtime
that hangs. Do not change cards/devices during an operation.

The two test builds deliberately have the same game-loading behaviour. Their
different clean source/build IDs exercise install, real chain loading, fast
handoff and rollback without adding another game compatibility experiment.
Three historical CDDA adapters are retained byte-for-byte at their existing
approved hashes. Other components are built from the stated clean commit.

Source, dependency archives, matching historical sources and build evidence
are under source/ and evidence/. These folders stay on your computer; only
the KUI folder belongs on the card. SHA256SUMS checks the included files.

Online downloading, release publication and automatic hang rollback are not
part of this test. Runtime/header checks and hashes detect corruption rather
than authenticate packages from an untrusted source.
"""


def package_test(baseline_dir, candidate_dir, output, *, legacy_sources=None):
    baseline_dir, baseline, _ = admitted_release(baseline_dir)
    candidate_dir, candidate, payload = admitted_release(candidate_dir)
    if baseline["source_commit"] == candidate["source_commit"]:
        raise ValueError("The hardware test needs two genuinely different clean build IDs")
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("Refusing to replace an existing test ZIP")
    work = output.parent / (output.stem + "-evidence")
    if work.exists():
        raise ValueError("Refusing to replace existing evidence")
    work.mkdir(parents=True)
    log = work / "actual-release-tests.log"
    with log.open("w") as stream:
        subprocess.run([sys.executable, str(ROOT / "tests/test_update_release_images.py"),
            "--baseline", str(baseline_dir / "KUI"), "--package", str(payload)], cwd=ROOT,
            stdout=stream, stderr=subprocess.STDOUT, check=True)
    from update_source import source_bundle
    sources = source_bundle(work / "source", {"baseline": baseline["source_commit"],
        "candidate": candidate["source_commit"]}, legacy_sources=legacy_sources)
    files = {"KUI/" + path: baseline_dir / "KUI" / path for path in catalog()}
    files["KUI/apps/games/probe.dat"] = baseline_dir / "KUI/apps/games/probe.dat"
    files["KUI/updates/" + payload.name] = payload
    files.update({"LICENSE": ROOT / "LICENSE", "THIRD_PARTY.md": ROOT / "THIRD_PARTY.md",
        "evidence/baseline-build.json": baseline_dir / "build.json",
        "evidence/candidate-build.json": candidate_dir / "build.json", "evidence/actual-release-tests.log": log})
    for prefix, directory in (("evidence/baseline", baseline_dir / "evidence"),
                              ("evidence/candidate", candidate_dir / "evidence"), ("source", work / "source")):
        for file in directory.rglob("*"):
            if file.is_symlink():
                raise ValueError("Symlink in release evidence/source")
            if file.is_file():
                files[prefix + "/" + file.relative_to(directory).as_posix()] = file
    build = {"kind": "offline-updater-hardware-test", "baseline": baseline["source_commit"],
        "candidate": candidate["source_commit"], "complete_cohort": True, "host_release_tests": "passed",
        "hardware_acceptance": False, "source": sources["provenance"],
        "source_provenance": {"path": "source/source-provenance.json", "sha256": sources["provenance_sha256"]}}
    texts = {"START-HERE.md": readme(baseline, candidate), "build.json": json.dumps(build, indent=2) + "\n"}
    checks = {name: digest(file) for name, file in files.items()}
    checks.update({name: hashlib.sha256(text.encode()).hexdigest() for name, text in texts.items()})
    texts["SHA256SUMS"] = "".join(checks[name] + "  " + name + "\n" for name in sorted(checks))
    partial = output.with_name(output.name + ".partial")
    try:
        with zipfile.ZipFile(partial, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, file in sorted(files.items()):
                archive.write(file, name)
            for name, text in sorted(texts.items()):
                archive.writestr(name, text)
        with zipfile.ZipFile(partial) as archive:
            if archive.testzip() is not None:
                raise ValueError("Test ZIP CRC check failed")
            for name, sha in checks.items():
                if hashlib.sha256(archive.read(name)).hexdigest() != sha:
                    raise ValueError("Test ZIP content differs: " + name)
        partial.rename(output)
    finally:
        partial.unlink(missing_ok=True)
    return {"file": str(output), "bytes": output.stat().st_size, "sha256": digest(output),
        "baseline": baseline["build"], "candidate": candidate["build"], "hardware_acceptance": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--legacy-sources", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(package_test(args.baseline, args.candidate, args.output,
            legacy_sources=args.legacy_sources), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
