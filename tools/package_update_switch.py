#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver an updater upgrade and a ready-to-select ZIP in updates/test."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import zipfile

from update_addon import admitted_addon, destination
from update_source import project_archive
from update_test_bundle import bundle
from update_package import verify


def sha(data):
    return hashlib.sha256(data).hexdigest()


def package_switch(initial, test, output):
    initial_dir, initial_report, initial_package = admitted_addon(initial)
    test_dir, test_report, test_package = admitted_addon(test)
    if initial_report["source_commit"] == test_report["source_commit"]:
        raise ValueError("Use distinct admitted source/build identities for the switch check")
    output = destination(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kui-switch-delivery-", dir=output.parent) as temporary:
        base = Path(temporary)
        test_zip = base / ("K-UI-Test-Switch-" + test_report["build"] + ".zip")
        test_info = bundle(test_package, test_zip, test_report["source_commit"],
                           "Updater folder and Restart K-UI check")
        initial_source, _ = project_archive(initial_report["source_commit"])
        initial_data = initial_package.read_bytes()
        snapshot = base / "checked-upgrade.kup"
        snapshot.write_bytes(initial_data)
        if verify(snapshot) != initial_report["package"]:
            raise ValueError("Captured upgrade package differs from its admitted contents")
        initial_path = "KUI/updates/" + initial_package.name
        test_path = "KUI/updates/test/" + test_zip.name
        files = {initial_path: initial_data, test_path: test_zip.read_bytes(),
            "source/K-UI-" + initial_report["build"] + ".tar.gz": initial_source,
            "evidence/upgrade-build.json": (initial_dir / "build.json").read_bytes(),
            "evidence/test-build.json": (test_dir / "build.json").read_bytes()}
        files["START-HERE.md"] = f"""# K-UI: folders, test ZIPs and direct restart

This upgrades the working offline updater. It replaces no root runtime,
reader, VMU, game or preference files. Both packages update only the GUI and
explicitly retain installed readers and their presence or absence.

1. With the console off, merge the supplied KUI contents into the existing
   KUI folder. This adds one .kup in updates and one ZIP in updates/test.
2. In the current updater, select {initial_package.name}, confirm and restart.
   The previously installed GUI still uses its old BIOS restart this once.
   Confirm new build {initial_report['build']} after booting normally.
3. Open Updates -> test and choose {test_zip.name}. Review, install, then
   START -> Restart K-UI -> A. K-UI should reopen directly as
   {test_report['build']}, without visiting BIOS.
4. Restore previous and Restart K-UI to return to {initial_report['build']}.
   START also restarts K-UI when the selected version has not changed.

A opens folders/files, B returns to the parent, and X refreshes the current
folder. Keep future updater-ready test ZIPs together in this folder; they
contain exactly one checked .kup. Old manual-install ZIPs with no .kup or
multiple alternative packages are refused and require explicit conversion.

Only the new folder/ZIP/restart flow needs a console check. No repeat game,
VMU, CDDA or WinCE compatibility tests are requested. Save data and inherited
root readers are shared between GUI versions. Full GUI/module updates can
use direct restart; bootloader/firmware/hardware-reset work remains separate.

Source and build evidence stay on the computer. The test ZIP also contains
its matching source; only its embedded .kup is staged on the console.
Trust the source of packages. Checksums detect corruption, not publishers.
""".encode()
        files["build.json"] = (json.dumps({"kind": "offline-updater-folder-restart-test",
            "upgrade": initial_report["source_commit"], "test": test_report["source_commit"],
            "hardware_acceptance": False, "install_files": [initial_path, test_path],
            "reader_replacements": [], "test_bundle": {**test_info,"file":test_path}}, indent=2) + "\n").encode()
        files["SHA256SUMS"] = "".join(sha(data) + "  " + name + "\n"
            for name, data in sorted(files.items())).encode()
        partial = base / "delivery.zip"
        with zipfile.ZipFile(partial, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(files.items()):
                # The inner ZIP is already compressed and kept byte-identical.
                archive.writestr(name, data, compress_type=zipfile.ZIP_STORED if name == test_path else zipfile.ZIP_DEFLATED)
        with zipfile.ZipFile(partial) as archive:
            if archive.testzip() is not None or set(archive.namelist()) != set(files):
                raise ValueError("Delivery ZIP inventory or CRC mismatch")
            install = [name for name in archive.namelist() if name.startswith("KUI/")]
            if set(install) != {initial_path, test_path}:
                raise ValueError("Unexpected install file in GUI-only delivery")
            for name, data in files.items():
                if archive.read(name) != data:
                    raise ValueError("Delivery member changed: " + name)
        os.link(partial, output)
    return {"file": str(output), "bytes": output.stat().st_size,
            "sha256": sha(output.read_bytes()), "upgrade": initial_report["build"],
            "test": test_report["build"], "install_files": [initial_path, test_path]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upgrade", required=True, type=Path)
    parser.add_argument("--test", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(package_switch(args.upgrade, args.test, args.output), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
