#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and package a GUI-only offline updater add-on, preserving root modules."""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

from package import storage_image
from update_package import ROOT, INHERIT_ROOT, build_package, catalog, member_info, verify
from update_source import project_archive

SOURCE_BASELINE = "0045dd96937ee7485ecd4fe0432b28babea8646a"
SOURCE_RECORD = "docs/evidence/updater-preserved-main-source.json"
# These GUI files contain only bank-path resolution and exact recognition of
# already installed readers. No reader implementation or recipe is exempt.
GUI_GLUE = frozenset(("src/apps/games_retail.c", "src/core/games_cdda_profile.h",
    "src/core/native_vmu_standard.c", "include/kui/native_vmu_standard.h"))
PROTECTED = ("src/loader/*", "Makefile.vmu*", "Makefile.native*", "Makefile.armada*", "Makefile.disc*",
    "Makefile.smash*", "Makefile.vc2*", "Makefile.swirl*",
    "src/apps/vmu*", "src/core/vmu*", "include/kui/vmu*", "include/kui/native*",
    "src/apps/disc_*", "src/core/disc_*", "include/kui/disc_*",
    "src/core/games_*", "src/apps/games.c", "src/core/retail_*", "include/kui/retail_*",
    "src/apps/games_retail.c", "src/apps/games_native*", "src/apps/native_*", "src/core/native_*",
    "src/apps/smash*", "src/core/smash*", "include/kui/smash*",
    "src/apps/vc2*", "src/core/vc2*", "include/kui/vc2*", "src/apps/swirl*", "src/core/swirl*", "include/kui/swirl*",
    "src/core/ce_*", "include/kui/ce_*", "include/kui/wince_*", "include/kui/cdda_*",
    "src/core/game_launch_settings.c", "include/kui/game_launch_settings.h", "include/kui/games_retail.h")
MANIFEST_OPTIONS = {
    "NATIVE_STANDARD_MANIFEST": "resources/updater-bindings/preserved-standard-manifest.h",
    "NATIVE_CDDA_MANIFEST": "resources/updater-bindings/native-cdda-manifest.h",
}
BINDING_FILES = tuple(sorted((*MANIFEST_OPTIONS.values(),
    "resources/updater-bindings/preserved-standard-manifest.json", "resources/updater-bindings/native-cdda-manifest.json")))
BINDING_SHA256 = {
    "resources/updater-bindings/native-cdda-manifest.h": "624a8d6a6661cae756c12bc913c7e21a1b73db1131b7eb7818bcd72ed4a84bc0",
    "resources/updater-bindings/native-cdda-manifest.json": "f72c75bec9d680bcd1fe0746677d88eeaf05b0cf5aa85b72458ecc1a36b4465f",
    "resources/updater-bindings/preserved-standard-manifest.h": "9f74bad8ba9937d41b18acab59df23d8e872f9b43648b504c893b398d8c9de00",
    "resources/updater-bindings/preserved-standard-manifest.json": "f9faac957192cfa38c5c30d6ffa4a3869d7e49a2aad78be043094b866d32c89b",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_state():
    head = git("rev-parse", "HEAD").decode().strip()
    if not re.fullmatch(r"[0-9a-f]{40}", head) or git("status", "--porcelain", "--untracked-files=all"):
        raise ValueError("The GUI add-on requires one clean source commit")
    return head


def protected_tree(commit):
    result = {}
    for row in git("ls-tree", "-rz", "--full-tree", commit).split(b"\0"):
        if not row:
            continue
        fields, raw_name = row.split(b"\t", 1)
        name = raw_name.decode()
        if not any(fnmatch.fnmatchcase(name, pattern) for pattern in PROTECTED):
            continue
        mode, kind, blob = fields.decode().split()
        if kind != "blob" or mode not in ("100644", "100755"):
            raise ValueError("Protected native source is not a regular Git blob: " + name)
        result[name] = {"mode": mode, "blob": blob}
    if not result:
        raise ValueError("The native source guard found no protected paths")
    return result


def source_reference():
    path = ROOT / SOURCE_RECORD
    if path.is_symlink() or not path.is_file():
        raise ValueError("The preserved main source record must be a regular tracked file")
    wire = path.read_bytes()
    reference = json.loads(wire)
    if (reference.get("schema") != "kui.updater.preserved-source.v1" or reference.get("baseline") != SOURCE_BASELINE or
            reference.get("patterns") != list(PROTECTED) or set(reference.get("bindings", {})) != set(BINDING_FILES)):
        raise ValueError("Preserved main source/binding record is invalid")
    if any(reference["bindings"][name].get("sha256") != BINDING_SHA256[name] for name in BINDING_FILES):
        raise ValueError("Preserved GUI binding hashes do not match the verified installed readers")
    blobs = reference.get("blobs")
    if not isinstance(blobs, dict) or not blobs:
        raise ValueError("Preserved main source record has no protected paths")
    for name, entry in blobs.items():
        if (not any(fnmatch.fnmatchcase(name, pattern) for pattern in PROTECTED) or
                not isinstance(entry, dict) or set(entry) != {"mode", "blob"} or
                entry["mode"] not in ("100644", "100755") or not re.fullmatch(r"[0-9a-f]{40}", entry["blob"])):
            raise ValueError("Invalid preserved native source entry")
    # Keep the reviewed main comparison usable in a source archive or shallow
    # checkout. The published baseline's complete blob map is in this record.
    baseline = reference.get("baseline_blobs")
    if not isinstance(baseline, dict) or not baseline:
        raise ValueError("Preserved main source record has no baseline blob map")
    unrelated = sorted(name for name in set(baseline) | set(blobs)
        if baseline.get(name) != blobs.get(name) and name not in GUI_GLUE)
    if unrelated:
        raise ValueError("Updater source record changes main's readers: " + ", ".join(unrelated[:12]))
    return reference, digest(wire)


def check_sources(commit):
    record, record_sha = source_reference()
    reference = record["blobs"]
    actual = protected_tree(commit)
    changed = sorted(name for name in set(reference) | set(actual) if reference.get(name) != actual.get(name))
    if changed:
        raise ValueError("GUI-only add-on changes protected native sources: " + ", ".join(changed[:12]))
    return {"baseline": SOURCE_BASELINE, "record_sha256": record_sha, "checked_paths": sorted(reference), "blobs": reference,
            "all_protected_sources_unchanged": True, "reader_sources_match_main": True}


def binding_manifests(commit=None):
    reference, _ = source_reference()
    result = {}
    for name in BINDING_FILES:
        expected = reference["bindings"][name]
        if commit is None:
            path = ROOT / name
            if path.is_symlink() or not path.is_file() or any(parent.is_symlink() for parent in path.parents):
                raise ValueError("Preserved GUI binding must be a regular file: " + name)
            data = path.read_bytes()
        else:
            data = git("show", commit + ":" + name)
        actual = {"bytes": len(data), "sha256": digest(data)}
        if actual != expected:
            raise ValueError("GUI binding differs from the verified installed reader: " + name)
        result[name] = actual
    return result


def destination(path):
    path = Path(path).absolute()
    if path.exists() or path.is_symlink():
        raise ValueError("Refusing to replace an existing add-on output")
    if path == ROOT or ROOT.is_relative_to(path):
        raise ValueError("Output cannot be the repository or its parent")
    if any(parent.is_symlink() for parent in path.parents):
        raise ValueError("Symlink in output directory")
    return path


def staged_runtime(directory):
    kui = Path(directory) / "KUI"
    if kui.is_symlink() or not kui.is_dir():
        raise ValueError("Add-on staging must contain one regular KUI directory")
    entries = list(kui.iterdir())
    if len(entries) != 1 or entries[0].name != "runtime.kui" or entries[0].is_symlink() or not entries[0].is_file():
        raise ValueError("GUI add-on KUI must contain runtime.kui only; no apps or other assets")
    return entries[0]


def payload_name(build_id):
    return "K-UI-updater-addon-testing-" + build_id + ".kup"


def build(output, jobs=2):
    head = source_state()
    guard = check_sources(head)  # Must precede every SDK invocation.
    bindings = binding_manifests()
    if bindings != binding_manifests(head):
        raise ValueError("GUI binding inputs differ from the clean source commit")
    if type(jobs) is not int or not 1 <= jobs <= 16:
        raise ValueError("Use between 1 and 16 compiler jobs")
    output = destination(output)
    if output.is_relative_to(ROOT) and subprocess.run(["git", "check-ignore", "-q", str(output)], cwd=ROOT).returncode:
        raise ValueError("Build output inside the repository must be ignored by Git")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".kui-addon-", dir=output.parent) as temporary:
        stage = Path(temporary)
        with (stage / "gui-build.log").open("w") as log:
            subprocess.run(["make", "-f", "Makefile.dc", "-j" + str(jobs),
                            *(name + "=" + path for name, path in MANIFEST_OPTIONS.items()), "build/kui-runtime.elf"],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        elf = ROOT / "build/kui-runtime.elf"
        if elf.is_symlink() or elf.with_suffix(".compile.json").is_symlink():
            raise ValueError("GUI ELF/compile evidence must be regular build outputs")
        receipt = json.loads(elf.with_suffix(".compile.json").read_text())
        elf_data = elf.read_bytes()
        if receipt.get("commit") != head or receipt.get("source_dirty") is not False or receipt.get("elf_sha256") != digest(elf_data):
            raise ValueError("GUI compile receipt does not bind this clean commit and ELF")
        try:
            runtime, _ = storage_image(elf_data, head[:12], "GUI-only updater")
        except SystemExit as error:
            raise ValueError(str(error)) from error
        member_info("runtime.kui", runtime, head[:12])
        if source_state() != head:
            raise ValueError("Source changed while building the GUI add-on")
        (stage / "KUI").mkdir()
        (stage / "KUI/runtime.kui").write_bytes(runtime)
        staged_runtime(stage)
        payload = stage / payload_name(head[:12])
        package = build_package(stage / "KUI", payload, head[:12], "testing", inherit_missing=True)
        report = {"kind": "offline-updater-addon", "source_commit": head, "build": head[:12],
            "source_dirty": False, "hardware_acceptance": False, "channel": "testing",
            "replacements": ["runtime.kui"], "inherited": list(catalog()[1:]),
            "payload": payload.name, "payload_sha256": digest(payload.read_bytes()),
            "runtime_sha256": digest(runtime), "elf_sha256": digest(elf_data), "compile": receipt,
            "native_source_guard": guard, "binding_manifests": bindings,
            "gui_manifest_options": MANIFEST_OPTIONS, "package": package}
        (stage / "build.json").write_text(json.dumps(report, indent=2) + "\n")
        (stage / "gui.compile.json").write_text(json.dumps(receipt, indent=2) + "\n")
        staged_runtime(stage)
        admitted_addon(stage)
        if source_state() != head or output.exists() or output.is_symlink():
            raise ValueError("Source or output changed before add-on publication")
        stage.rename(output)
    return report


def admitted_addon(directory):
    directory = Path(directory).absolute()
    if directory.is_symlink() or any(parent.is_symlink() for parent in directory.parents):
        raise ValueError("Symlink in add-on build directory")
    for name in ("build.json", "gui.compile.json"):
        file = directory / name
        if file.is_symlink() or not file.is_file():
            raise ValueError("Add-on evidence must be regular files")
    report = json.loads((directory / "build.json").read_text())
    commit = report.get("source_commit", "")
    if (report.get("kind") != "offline-updater-addon" or not re.fullmatch(r"[0-9a-f]{40}", commit) or
            report.get("build") != commit[:12] or report.get("source_dirty") is not False or
            report.get("hardware_acceptance") is not False or report.get("channel") != "testing" or
            report.get("replacements") != ["runtime.kui"] or report.get("inherited") != list(catalog()[1:]) or
            report.get("complete_cohort") is True):
        raise ValueError("Not an admitted clean GUI-only testing add-on")
    if report.get("native_source_guard") != check_sources(commit):
        raise ValueError("Native source guard does not bind this exact source tree")
    if report.get("binding_manifests") != binding_manifests(commit) or report.get("gui_manifest_options") != MANIFEST_OPTIONS:
        raise ValueError("GUI build did not preserve the required reader binding manifests")
    receipt = json.loads((directory / "gui.compile.json").read_text())
    if (receipt != report.get("compile") or receipt.get("commit") != commit or
            receipt.get("source_dirty") is not False or not re.fullmatch(r"[0-9a-f]{64}", report.get("elf_sha256", "")) or
            receipt.get("elf_sha256") != report["elf_sha256"]):
        raise ValueError("GUI-only compile evidence is inconsistent")
    runtime = staged_runtime(directory)
    name = report.get("payload")
    if name != payload_name(commit[:12]):
        raise ValueError("Invalid GUI-only payload name")
    payload = directory / name
    if payload.is_symlink() or not payload.is_file() or digest(payload.read_bytes()) != report.get("payload_sha256"):
        raise ValueError("GUI-only payload changed after admission")
    package = verify(payload)
    if package != report.get("package") or package["build"] != commit[:12] or package["channel"] != "testing":
        raise ValueError("GUI-only payload differs from its report")
    if ([row["path"] for row in package["members"]] != list(catalog()) or
            package["members"][0] != member_info("runtime.kui", runtime.read_bytes(), commit[:12]) or
            digest(runtime.read_bytes()) != report.get("runtime_sha256") or
            any(row["policy"] != INHERIT_ROOT for row in package["members"][1:])):
        raise ValueError("Add-on replaces a module or does not match its staged GUI")
    return directory, report, payload


def instructions(baseline, candidate):
    return f"""# K-UI GUI-only updater test

This add-on replaces only KUI/runtime.kui. It does not supply readers, workers,
VMU cards or game modules. It retains the software modules currently installed.
The earlier 28cadccf0e71-to-33b04057db86 full-cohort test ZIP is withdrawn.
If you installed that ZIP, restore your pre-test software backup first while
preserving your current VMU/save data. This add-on does not repair that install.

1. Power off and keep an independent backup of your current KUI folder.
2. Merge only this ZIP's KUI folder onto the existing card. Do not delete it.
3. Boot using your existing boot CD; confirm GUI build {baseline['build']} (A).
4. In Settings -> Updates select the supplied package, review build
   {candidate['build']} (B), confirm, wait for completion and restart.
5. Confirm B, then use Restore previous, confirm and restart. Confirm A.
   Holding B while the retained updater starts bypasses the selected GUI once.

Only the updater A -> B -> A round trip is requested. This is not a request to
repeat game compatibility testing or evidence of improved game compatibility.
Inherited root modules retain their existing presence or absence; they are not
copied into a bank or pinned to the GUI build. Saves/preferences remain shared.
If activation is uncertain, restart and check the version before retrying.

Sources, licences and build reports stay on your computer under source/ and
evidence/. Hardware acceptance is pending. Trust the package source; hashes
detect corruption and do not authenticate a publisher.
"""


def package_addon(baseline_dir, candidate_dir, output):
    baseline_dir, baseline, _ = admitted_addon(baseline_dir)
    candidate_dir, candidate, payload = admitted_addon(candidate_dir)
    if baseline["source_commit"] == candidate["source_commit"] or baseline["build"] == candidate["build"]:
        raise ValueError("The add-on test requires two different clean build IDs")
    output = destination(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    files = {"KUI/runtime.kui": staged_runtime(baseline_dir).read_bytes(),
        "KUI/updates/" + payload.name: payload.read_bytes(),
        "evidence/baseline-build.json": (baseline_dir / "build.json").read_bytes(),
        "evidence/candidate-build.json": (candidate_dir / "build.json").read_bytes(),
        "evidence/baseline-compile.json": (baseline_dir / "gui.compile.json").read_bytes(),
        "evidence/candidate-compile.json": (candidate_dir / "gui.compile.json").read_bytes()}
    if (digest(files["KUI/runtime.kui"]) != baseline["runtime_sha256"] or
            digest(files["KUI/updates/" + payload.name]) != candidate["payload_sha256"] or
            json.loads(files["evidence/baseline-build.json"]) != baseline or
            json.loads(files["evidence/candidate-build.json"]) != candidate or
            json.loads(files["evidence/baseline-compile.json"]) != baseline["compile"] or
            json.loads(files["evidence/candidate-compile.json"]) != candidate["compile"]):
        raise ValueError("Add-on bytes changed after admission")
    sources = {}
    for label, report in (("baseline", baseline), ("candidate", candidate)):
        data, _ = project_archive(report["source_commit"])
        name = "source/K-UI-" + label + "-" + report["build"] + ".tar.gz"
        files[name] = data
        sources[label] = {"commit": report["source_commit"], "path": name, "sha256": digest(data)}
    for name in ("LICENSE", "THIRD_PARTY.md", "dependencies.json"):
        data = git("show", candidate["source_commit"] + ":" + name)
        files["source/dependencies.json" if name == "dependencies.json" else name] = data
    files["START-HERE.md"] = instructions(baseline, candidate).encode()
    files["build.json"] = (json.dumps({"kind": "offline-updater-addon-test", "hardware_acceptance": False,
        "baseline": baseline["source_commit"], "candidate": candidate["source_commit"],
        "replacements": ["runtime.kui"], "inherited": list(catalog()[1:]), "source": sources}, indent=2) + "\n").encode()
    checks = {name: digest(data) for name, data in files.items()}
    files["SHA256SUMS"] = "".join(checks[name] + "  " + name + "\n" for name in sorted(checks)).encode()
    with tempfile.TemporaryDirectory(prefix=".kui-addon-zip-", dir=output.parent) as temporary:
        partial = Path(temporary) / "delivery.zip"
        with zipfile.ZipFile(partial, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(partial) as archive:
            if archive.testzip() is not None or set(archive.namelist()) != set(files):
                raise ValueError("Add-on ZIP failed its fixed-path CRC/inventory check")
            for name, data in files.items():
                if archive.read(name) != data:
                    raise ValueError("Add-on ZIP content changed: " + name)
        # Exclusive publication: an existing output is never replaced.
        os.link(partial, output)
    return {"file": str(output), "bytes": output.stat().st_size, "sha256": digest(output.read_bytes()),
            "baseline": baseline["build"], "candidate": candidate["build"], "hardware_acceptance": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("build")
    make.add_argument("--output", required=True, type=Path)
    make.add_argument("--jobs", type=int, default=2)
    package = commands.add_parser("package")
    package.add_argument("--baseline", required=True, type=Path)
    package.add_argument("--candidate", required=True, type=Path)
    package.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = build(args.output, args.jobs) if args.command == "build" else package_addon(args.baseline, args.candidate, args.output)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
