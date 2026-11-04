#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Wrap one checked test .kup for direct selection in K-UI's Updates browser.

Manual-install ZIPs are not converted implicitly. Assemble an explicit variant
with update_package.py first; only its declared replacements will be installed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import zipfile

from update_package import INHERIT_ROOT, verify
from update_source import project_archive


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bundle(package, output, source_commit, label):
    package, output = Path(package).absolute(), Path(output).absolute()
    if (package.is_symlink() or not package.is_file() or
            any(parent.is_symlink() for parent in package.parents)):
        raise ValueError("Use a regular checked .kup file")
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ValueError("Supply the exact source commit for this test")
    if (not label or len(label) > 96 or any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in label)):
        raise ValueError("Use a short readable test label")
    info = verify(package)
    if info["channel"] != "testing" or info["build"] != source_commit[:12]:
        raise ValueError("The testing package must identify the supplied source commit")
    if output.exists() or output.is_symlink() or any(parent.is_symlink() for parent in output.parents):
        raise ValueError("Refusing to replace an existing bundle or use a symlink")
    source, _ = project_archive(source_commit)
    package_data = package.read_bytes()
    # Bind the bytes copied into the ZIP to the package just inspected.
    if verify(package) != info:
        raise ValueError("Test package changed while collecting its source")
    member = "update-" + info["build"] + ".kup"
    replaced = [entry["path"] for entry in info["members"] if entry["policy"] != INHERIT_ROOT]
    retained = [entry["path"] for entry in info["members"] if entry["policy"] == INHERIT_ROOT]
    record = {"kind": "kui-test-update-bundle", "label": label, "source_commit": source_commit,
              "build": info["build"], "package": member, "package_sha256": sha(package_data),
              "replacements": replaced, "inherited_root": retained, "hardware_acceptance": False}
    files = {member: package_data, "source/K-UI-" + info["build"] + ".tar.gz": source,
             "build.json": (json.dumps(record, indent=2) + "\n").encode(),
             "ABOUT.txt": (label + "\nBuild " + info["build"] +
                "\nCopy this ZIP into KUI/updates/test, then select it in Settings -> Updates.\n"
                "Only the embedded checked package is staged. Source files are not installed.\n"
                "Replacements:\n" + "".join("  " + name + "\n" for name in replaced) +
                "Other declared modules keep their installed root versions. Saves remain shared.\n").encode()}
    files["SHA256SUMS"] = "".join(sha(data) + "  " + name + "\n"
        for name, data in sorted(files.items())).encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kui-test-bundle-", dir=output.parent) as temporary:
        snapshot = Path(temporary) / "checked.kup"
        snapshot.write_bytes(package_data)
        if verify(snapshot) != info:
            raise ValueError("Captured test package differs from its inspected contents")
        partial = Path(temporary) / "bundle.zip"
        with zipfile.ZipFile(partial, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(partial) as archive:
            if archive.testzip() is not None or set(archive.namelist()) != set(files):
                raise ValueError("Test ZIP failed its inventory or CRC check")
            if archive.read(member) != package_data:
                raise ValueError("Embedded package changed while writing the ZIP")
        os.link(partial, output)
    return {"file": str(output), "bytes": output.stat().st_size,
            "sha256": sha(output.read_bytes()), **record}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(bundle(args.package, args.output, args.source_commit, args.label), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
