#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make one current-engine .kup from staged test or release software.

Example: python3 tools/make_update.py stage/KUI test.kup
The producer inventories the stage and checks the exact output with the current
C consumer before delivery. Reader-only tests preserve the effective GUI and
other installed software; full releases stage their GUI and all intended files,
and keep omitted old software absent rather than leaking a root fallback.
"""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import stat
import tempfile
import zipfile

from update_package import build_package_v2, verify, current_consumer_check, software_path_v2, MAX_PAYLOAD, MAX_ENTRIES


def _from_zip(source, output, *, release, full, remove, keep, channel):
    """Stage declared software only; never run or extract archived source code."""
    with tempfile.TemporaryDirectory(prefix="kui-update-input-") as temporary:
        stage = Path(temporary) / "KUI"
        stage.mkdir()
        copied, seen, kup, delivery, ignored = [], set(), [], [], 0
        with zipfile.ZipFile(source) as archive:
            if len(archive.infolist()) > 10000:
                raise ValueError("Input ZIP has too many entries")
            total = 0
            for entry in archive.infolist():
                name = entry.filename
                path = PurePosixPath(name)
                if (not name or path.is_absolute() or "\\" in name or
                        any(p in ("", ".", "..") for p in name.rstrip("/").split("/"))):
                    raise ValueError("Input ZIP path is not canonical: " + name)
                if name in seen:
                    raise ValueError("Duplicate input ZIP member: " + name)
                seen.add(name)
                if entry.is_dir():
                    continue
                mode = entry.external_attr >> 16
                if stat.S_ISLNK(mode):
                    raise ValueError("Symlink in input ZIP: " + name)
                if entry.flag_bits & 1:
                    raise ValueError("Encrypted input ZIP software is unsupported")
                if name.lower().endswith(".kup"):
                    kup.append(entry)
                if not name.startswith("KUI/"):
                    ignored += 1
                    continue
                relative = name[4:]
                if relative.startswith("updates/") and relative.lower().endswith(".kup"):
                    delivery.append(name)
                    continue
                if not software_path_v2(relative):
                    raise ValueError("Input ZIP contains non-software KUI data: " + relative)
                if mode and stat.S_IFMT(mode) not in (0, stat.S_IFREG):
                    raise ValueError("Input ZIP software is not a regular file: " + relative)
                total += entry.file_size
                if total > MAX_PAYLOAD or len(copied) >= MAX_ENTRIES:
                    raise ValueError("Input ZIP software exceeds update bounds")
                destination = stage / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                data = archive.read(entry)
                if len(data) != entry.file_size:
                    raise ValueError("Input ZIP software size mismatch: " + relative)
                destination.write_bytes(data)
                copied.append(relative)
            if copied:
                report = make_update(stage, output, release=release, full=full, remove=remove, keep=keep, channel=channel)
                report["input"] = "software-zip"
                report["ignored_nonsoftware_archive_members"] = ignored
                report["replaced_embedded_update_files"] = delivery
                return report
            if len(kup) != 1 or remove or keep:
                raise ValueError("Input ZIP needs staged KUI software or exactly one existing v2 .kup")
            entry = kup[0]
            if entry.file_size > MAX_PAYLOAD + 128 + MAX_ENTRIES * 160:
                raise ValueError("Embedded update package exceeds its limit")
            package = Path(temporary) / "checked.kup"
            package.write_bytes(archive.read(entry))
        report = verify(package)
        if report["format"] != 2:
            raise ValueError("Embedded legacy .kup must be rebuilt from staged software with the current Main producer")
        if full and not report["full"]:
            raise ValueError("Embedded partial .kup must be rebuilt from its staged software for a full-version update")
        if full and "runtime.kui" not in {e["path"] for e in report["members"] if e["policy"] == 3}:
            raise ValueError("Full software release is missing its current-engine GUI")
        report["consumer"] = current_consumer_check(package)
        report["mode"] = "release" if release else "full" if report["full"] else "test"
        output = Path(output).absolute()
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix=".kui-checked-", dir=output.parent, delete=False) as target:
            ready = Path(target.name)
            try:
                if output.suffix.lower() == ".kup":
                    target.write(package.read_bytes())
                else:
                    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
                        archive.write(package, output.stem + ".kup")
                target.flush()
                os.fsync(target.fileno())
            except BaseException:
                ready.unlink(missing_ok=True)
                raise
        ready.replace(output)
        report["input"] = "verified-v2-zip"
        return report


def make_update(stage, output, *, release=False, full=False, remove=(), keep=(), channel=None):
    output = Path(output).absolute()
    if output.suffix.lower() not in (".kup", ".zip"):
        raise ValueError("Choose a .kup output, or .zip containing exactly one .kup")
    channel = channel or ("stable" if release else "testing")
    full = full or release
    stage = Path(stage)
    if stage.is_file():
        if not zipfile.is_zipfile(stage):
            raise ValueError("Input must be a staged KUI software directory or a ZIP")
        return _from_zip(stage, output, release=release, full=full, remove=remove, keep=keep, channel=channel)
    if output.suffix.lower() == ".kup":
        return build_package_v2(stage, output, channel, release=release, full=full, remove=remove, keep=keep)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".kui-delivery-", dir=output.parent) as temporary:
        root = Path(temporary)
        kup = root / (output.stem + ".kup")
        report = build_package_v2(stage, kup, channel, release=release, full=full, remove=remove, keep=keep)
        archive = root / "delivery.zip"
        with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as stream:
            stream.write(kup, kup.name)
        with zipfile.ZipFile(archive) as stream:
            if stream.testzip() is not None or stream.namelist() != [kup.name] or stream.read(kup.name) != kup.read_bytes():
                raise ValueError("Final ZIP does not contain the verified .kup")
        archive.replace(output)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path, help="Staged KUI software directory or input ZIP; no saves/settings")
    parser.add_argument("output", type=Path, help="Result .kup (or one-package .zip)")
    parser.add_argument("--release", action="store_true", help="Full software release; requires current-engine runtime.kui")
    parser.add_argument("--full", action="store_true", help="Complete version software; omitted old software stays absent")
    parser.add_argument("--channel", choices=("stable", "testing"))
    parser.add_argument("--remove", action="append", default=[], metavar="SOFTWARE_PATH", help="Explicitly preserve absence of a software role")
    parser.add_argument("--keep", action="append", default=[], metavar="SOFTWARE_PATH", help="Explicitly keep a currently effective software role")
    args = parser.parse_args()
    try:
        report = make_update(args.stage, args.output, release=args.release, full=args.full, channel=args.channel,
                             remove=args.remove, keep=args.keep)
        print(json.dumps({"file": str(args.output.absolute()), **report}, indent=2))
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
