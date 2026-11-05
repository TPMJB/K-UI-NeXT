#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver an admitted GUI through a deterministic manual root migration."""
import argparse
import json
import os
from pathlib import Path
import tempfile
import zipfile

from update_addon import admitted_addon, destination, digest, git, BINDING_FILES, SOURCE_RECORD
from update_package import INHERIT_ROOT, catalog, member_info, verify
from update_source import project_archive

def instructions(build):
    return f"""# K-UI updater compatibility repair — {build}

INSTALL THIS REPAIR ON THE COMPUTER. Do not open this ZIP in K-UI Updates.
Its only installable file is KUI/runtime.kui. There is deliberately no .kup.

1. Power off the Dreamcast and put its SD card in the computer. Keep a copy
   of the current KUI/runtime.kui on the computer as your recovery backup.
2. On the SD card, rename KUI/system to KUI/system-before-updater-repair
   if that folder exists. If the backup name already exists, use a new name;
   do not replace it. This preserves old installed versions while retiring
   their saved startup selection. KUI/apps/system is a DIFFERENT folder:
   leave KUI/apps and all its contents in place.
3. If KUI/boot.kui exists, rename it to KUI/boot-before-updater-repair.kui
   (or another unused backup name). The boot CD otherwise tries that optional
   override before runtime.kui.
4. Extract this ZIP on the computer and merge its KUI folder into the existing
   SD-card KUI folder, replacing only KUI/runtime.kui. Do not replace or remove
   the existing KUI folder. Eject the card normally.
5. Boot with your usual CD. Confirm build {build} on screen. No B timing or
   Original baseline selection is needed. START -> Restart K-UI -> A should
   return to the same build. The updater recreates KUI/system when needed.

Keep installed readers, game modules, VMUs, journals, settings and games.
The renamed system folder contains update versions, selection records and the
temporary update cache. Save data and settings remain in their existing paths.

Why the previous delivery failed: the folders/restart updater uses an exact
26-entry manifest, while the supplied bridge had 34. Its actual decoder rejects
that exact package with "Incomplete update cohort". An older ab1 updater uses
34 entries instead. A single bridge cannot pass both old exact-count checks.
Replacing runtime.kui without retiring the saved bank can still start the old
GUI. This migration bypasses both old package gates and the saved bank.

This delivery installs GUI build {build} and retains existing reader files.
The repair uses a manual root migration. Its source and verified
Standard/CDDA binding manifests are retained. Hardware acceptance is pending.
Only confirm this build starts and Restart K-UI returns to it; no repeat game
compatibility matrix is requested. Future updater-ready packages can then be
selected from Updates, including subfolders.

To undo the migration with the console off: restore your backed-up runtime,
move any newly created KUI/system aside, and rename the preserved system folder
back to KUI/system. Restore the optional boot.kui backup if you renamed one.
Keep current save data in place; software rollback does not roll back saves.

The withdrawn 28cadccf0e71-to-33b04057db86 full-cohort ZIP remains withdrawn.
This GUI repair preserves the reader files currently installed; if that ZIP
replaced your readers, restore your pre-test software backup while preserving
your current VMU/save data. Keep the independent runtime backup for recovery.

Only KUI/runtime.kui belongs on the card. Source, licences, binding manifests
and build evidence stay on the computer. Checksums detect corruption, not
publisher identity.
"""


def package_repair(addon_directory, output):
    directory, report, package = admitted_addon(addon_directory)
    info = report["package"]
    members = catalog()
    if (tuple(row["path"] for row in info["members"]) != members or
            info["members"][0]["policy"] != 0 or
            any(row["policy"] != INHERIT_ROOT for row in info["members"][1:])):
        raise ValueError("The root repair requires a GUI-only package matching its admitted catalog")
    output = destination(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    runtime = (directory / "KUI/runtime.kui").read_bytes()
    package_data = package.read_bytes()
    report_data = (directory / "build.json").read_bytes()
    compile_data = (directory / "gui.compile.json").read_bytes()
    if (digest(runtime) != report["runtime_sha256"] or digest(package_data) != report["payload_sha256"] or
            json.loads(report_data) != report or json.loads(compile_data) != report["compile"] or
            member_info("runtime.kui", runtime, report["build"]) != info["members"][0] or
            package_data[128 + 128 * len(members):] != runtime):
        raise ValueError("Repair GUI/package/evidence changed after admission")
    source, _ = project_archive(report["source_commit"])
    source_name = "source/K-UI-" + report["build"] + ".tar.gz"
    files = {"KUI/runtime.kui": runtime, source_name: source,
        "evidence/gui-build.json": report_data, "evidence/gui.compile.json": compile_data,
        "START-HERE.md": instructions(report["build"]).encode()}
    # A packaging-only repair does not rebuild or relabel the admitted GUI.
    # Retain its exact source and the subsequent delivery/test changes.
    delivery_commit = git("rev-parse", "HEAD").decode().strip()
    delivery_patch = git("diff", "--binary", report["source_commit"], delivery_commit)
    delivery_source = {"commit": delivery_commit}
    if delivery_patch:
        patch_name = "source/repair-delivery.patch"
        files[patch_name] = delivery_patch
        delivery_source.update(path=patch_name, sha256=digest(delivery_patch))
    for name in BINDING_FILES:
        data = git("show", report["source_commit"] + ":" + name)
        expected = report["binding_manifests"][name]
        if {"bytes": len(data), "sha256": digest(data)} != expected:
            raise ValueError("Repair binding manifest differs from its admitted source")
        files["evidence/bindings/" + Path(name).name] = data
    guard = git("show", report["source_commit"] + ":" + SOURCE_RECORD)
    if digest(guard) != report["native_source_guard"]["record_sha256"]:
        raise ValueError("Repair source-preservation record differs from admission")
    files["evidence/preserved-main-source.json"] = guard
    for name in ("LICENSE", "THIRD_PARTY.md", "dependencies.json"):
        data = git("show", report["source_commit"] + ":" + name)
        files["source/dependencies.json" if name == "dependencies.json" else name] = data
    install_files = ["KUI/runtime.kui"]
    record = {"kind": "offline-updater-compatibility-repair", "source_commit": report["source_commit"],
        "build": report["build"], "hardware_acceptance": False, "root_gui_replacement": True,
        "installation": "manual-root-migration", "retire_old_selection": "rename KUI/system",
        "optional_boot_override": "rename KUI/boot.kui if present", "updater_installable": False,
        "install_files": install_files, "reader_replacements": [], "replacements": ["runtime.kui"],
        "inherited_root": list(members[1:]), "build_manifest_entries": len(members),
        "runtime_sha256": digest(runtime), "build_package_sha256": digest(package_data),
        "delivery_source": delivery_source,
        "source": {"path": source_name, "sha256": digest(source)}}
    files["build.json"] = (json.dumps(record, indent=2) + "\n").encode()
    files["SHA256SUMS"] = "".join(digest(data) + "  " + name + "\n" for name, data in sorted(files.items())).encode()
    with tempfile.TemporaryDirectory(prefix="kui-repair-delivery-", dir=output.parent) as temporary:
        base = Path(temporary)
        snapshot = base / "checked.kup"
        snapshot.write_bytes(package_data)
        if verify(snapshot) != info:
            raise ValueError("Captured compatibility package differs from its admitted contents")
        partial = base / "repair.zip"
        with zipfile.ZipFile(partial, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(partial) as archive:
            if (archive.testzip() is not None or set(archive.namelist()) != set(files) or
                    {name for name in archive.namelist() if name.startswith("KUI/")} != set(install_files)):
                raise ValueError("Repair ZIP inventory or CRC check failed")
            for name, data in files.items():
                if archive.read(name) != data:
                    raise ValueError("Repair ZIP member changed: " + name)
        os.link(partial, output)
    return {"file": str(output), "bytes": output.stat().st_size, "sha256": digest(output.read_bytes()),
        "build": report["build"], "source_commit": report["source_commit"], "install_files": install_files,
        "hardware_acceptance": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--addon", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(package_repair(args.addon, args.output), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
