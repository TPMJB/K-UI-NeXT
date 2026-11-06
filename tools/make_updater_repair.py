#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build one current updater bootstrap ZIP and its identical standalone KUP.

The input is staged immutable KUI software with a current-engine GUI. The tool
does not rebuild or retag any component. Both physical boot paths and ROOT
selection records are generated here, rather than inherited from an old ZIP.
Each output is validated before atomic exclusive publication; ordinary errors
roll back newly published outputs. Multi-file publication is not a filesystem
transaction across power loss; the primary ZIP is complete on its own.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zipfile
import zlib

from make_update import make_update
from runtime_package import verify as runtime_verify
from update_package import (ROOT, ENGINE_CURRENT, REQUIRES_ENGINE3,
                            consumer_fingerprint, inventory_v2, verify)

SOURCE_INPUTS = (
    "tools/make_updater_repair.py", "tools/make_update.py", "tools/update_package.py",
    "tools/update_consumer_check.c", "include/kui/update.h", "include/kui/update_boot.h",
    "include/kui/update_handoff.h", "include/kui/update_software_file.h",
    "include/kui/update_browser.h", "include/kui/update_zip.h",
    "src/core/update.c", "src/core/update_file.c", "src/core/update_boot.c",
    "src/core/update_handoff.c", "src/core/update_software_file.c",
    "src/core/update_browser.c", "src/core/update_zip.c", "src/core/runtime_file.c",
)
CONTROL_CHECK = r'''
#include "kui/update.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc!=3)return 1;
    for(unsigned i=0u;i<2u;i++) {
        FILE *file=fopen(argv[i+1u],"rb");if(!file)return 2;
        unsigned char wire[128];
        int ok=fread(wire,1u,sizeof(wire),file)==sizeof(wire) && fgetc(file)==EOF;
        if(fclose(file))ok=0;
        struct kui_update_activation value;
        if(!ok || !kui_update_activation_decode(wire,&value) ||
           value.sequence!=i+1u || value.selected[0] || value.previous[0])return 3;
        for(unsigned n=0u;n<32u;n++)if(value.manifest_sha256[n])return 4;
    }
    puts("{\"decoder\":\"current-source\",\"root_selected\":true,\"sequences\":[1,2]}");
    return 0;
}
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def root_activation(sequence):
    if sequence not in (1, 2):
        raise ValueError("Bootstrap ROOT activation sequence must be 1 or 2")
    wire = bytearray(128)
    wire[:8] = b"KUIACT1\0"
    struct.pack_into("<IIQ", wire, 8, 1, 128, sequence)
    struct.pack_into("<I", wire, 124, zlib.crc32(wire[:124]))
    return bytes(wire)


def source_identity():
    hashes = {name: digest((ROOT / name).read_bytes()) for name in SOURCE_INPUTS}
    try:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                         text=True, stderr=subprocess.DEVNULL).strip()
        dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=all"],
                                            cwd=ROOT, stderr=subprocess.DEVNULL))
    except (OSError, subprocess.CalledProcessError):
        commit, dirty = None, None
    return {"git_commit": commit, "source_dirty": dirty, "sha256": hashes}


def check_controls(paths, directory):
    """Decode the exact generated activation bytes with current production C."""
    before = consumer_fingerprint()
    directory = Path(directory)
    source = directory / "activation-check.c"
    binary = directory / "activation-check"
    source.write_text(CONTROL_CHECK)
    command = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O2",
               "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / ".deps/fatfs/source"), str(source),
               *(str(ROOT / name) for name in ("src/core/update.c", "src/core/hash.c", "src/core/data.c")),
               "-o", str(binary)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise ValueError("Current activation decoder did not compile: " + result.stderr[-4000:])
    checked = subprocess.run([str(binary), *(str(path) for path in paths)], capture_output=True, text=True)
    if checked.returncode:
        raise ValueError("Current activation decoder rejected the generated ROOT controls")
    if consumer_fingerprint() != before:
        raise ValueError("Updater source changed during activation verification; rerun packaging")
    return json.loads(checked.stdout)


def output_path(path, suffix):
    path = Path(path).absolute()
    if path.suffix.lower() != suffix or path.is_symlink() or any(p.is_symlink() for p in path.parents):
        raise ValueError("Choose a regular " + suffix + " output without symlink paths")
    if path.exists():
        raise ValueError("Refusing to replace existing output: " + str(path))
    return path


def publish_new(outputs):
    """Atomically expose each validated file without replacing existing paths."""
    staged, published = [], []
    try:
        for destination, data in outputs:
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists() or destination.is_symlink() or any(p.is_symlink() for p in destination.parents):
                raise ValueError("Output appeared or became unsafe before publication: " + str(destination))
            with tempfile.NamedTemporaryFile(prefix=".kui-repair-", dir=destination.parent, delete=False) as stream:
                source = Path(stream.name)
                staged.append((source, destination))
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            if source.read_bytes() != data:
                raise ValueError("Staged output differs from its verified bytes")
        for source, destination in staged:
            # link is exclusive: a file created by someone else is never lost.
            os.link(source, destination, follow_symlinks=False)
            published.append((source, destination))
    except BaseException:
        for source, destination in reversed(published):
            try:
                if source.stat().st_ino == destination.lstat().st_ino and source.stat().st_dev == destination.lstat().st_dev:
                    destination.unlink()
            except FileNotFoundError:
                pass
        raise
    finally:
        for source, _ in staged:
            source.unlink(missing_ok=True)


def build_repair(stage, output, *, kup_output=None, channel="testing"):
    stage = Path(stage).absolute()
    paths = inventory_v2(stage)
    if "runtime.kui" not in paths:
        raise ValueError("Bootstrap staging requires the current runtime.kui")
    output = output_path(output, ".zip")
    kup_output = output_path(kup_output or output.with_suffix(".kup"), ".kup")
    if output == kup_output or output.is_relative_to(stage.resolve()) or kup_output.is_relative_to(stage.resolve()):
        raise ValueError("Bootstrap ZIP and KUP must be distinct outputs outside the software stage")
    before = source_identity()
    with tempfile.TemporaryDirectory(prefix="kui-current-bootstrap-") as temporary:
        temporary = Path(temporary)
        kup = temporary / kup_output.name
        package = make_update(stage, kup, channel=channel)
        if package["flags"] != REQUIRES_ENGINE3 or package["engine_minimum"] != ENGINE_CURRENT:
            raise ValueError("Bootstrap must contain a current-engine partial software package")
        payload = kup.read_bytes()
        software = {entry["path"]: (stage / entry["path"]).read_bytes() for entry in package["members"]}
        if tuple(software) != paths or any(len(software[e["path"]]) != e["bytes"] or
                digest(software[e["path"]]) != e["sha256"] for e in package["members"]):
            raise ValueError("Staged software changed after package validation")
        gui = software["runtime.kui"]
        gui_info = runtime_verify(gui)
        records = [root_activation(1), root_activation(2)]
        record_paths = [temporary / "active0.bin", temporary / "active1.bin"]
        for path, data in zip(record_paths, records):
            path.write_bytes(data)
        controls = check_controls(record_paths, temporary)
        evidence = {"kind": "current-updater-bootstrap", "engine": ENGINE_CURRENT,
                    "gui": {**gui_info, "bytes": len(gui), "sha256": digest(gui)},
                    "updater_source": before, "package": package,
                    "kup_sha256": digest(payload), "activation_validation": controls,
                    "reader_bytes_rebuilt_or_retagged": False, "console_tested": False,
                    "software": {name: {"bytes": len(data), "sha256": digest(data)}
                                 for name, data in software.items()}}
        entries = {"KUI/" + name: data for name, data in software.items()}
        entries["KUI/boot.kui"] = gui
        entries["KUI/system/active0.bin"] = records[0]
        entries["KUI/system/active1.bin"] = records[1]
        embedded = "KUI/updates/test/" + kup_output.name
        entries[embedded] = payload
        entries["updater-repair.json"] = (json.dumps(evidence, indent=2) + "\n").encode()
        entries["START-HERE.txt"] = (
            "K-UI CURRENT UPDATER BOOTSTRAP\n\n"
            "Prefer normal KUP installation when your updater works. This manual fallback\n"
            "includes the current updater and the same test software as the supplied KUP.\n\n"
            "Power off and copy this ZIP's KUI folder to the SD card root. Merge folders\n"
            "and replace the supplied files, including both KUI/system activation files.\n"
            "The copied boot.kui and runtime.kui are identical current-engine GUIs.\n"
            "Both activation records select this copied ROOT software so an older bank\n"
            "cannot immediately replace it with its obsolete updater.\n\n"
            "Existing bank folders, cards, saves, preferences and games remain in place.\n"
            "Selecting ROOT makes bank-only software inactive and clears the previous\n"
            "selection. Updates installed only in a bank may need reinstalling; this\n"
            "fallback does not preserve automatic rollback to that selection.\n\n"
            "Only the declared immutable software and generated boot/selection controls\n"
            "are supplied. Reader/module/helper bytes are copied exactly, never rebuilt\n"
            "or retagged. The GUI's declared build is " + gui_info["build"] + ".\n\n"
            "After booting, Settings -> Updates -> test contains " + kup_output.name +
            ".\nIt is byte-identical to the standalone KUP delivered beside this ZIP.\n"
            "Install it and Restart K-UI to exercise the normal package workflow.\n"
            "Future prepared tests use KUP selection/install/restart.\n\n"
            "Current C decoder and activation checks passed before publication.\n"
            "Console behavior has not been established by these host checks.\n"
        ).encode()
        entries["SHA256SUMS.txt"] = "".join(digest(data) + "  " + name + "\n"
                                             for name, data in sorted(entries.items())).encode()
        archive_path = temporary / "bootstrap.zip"
        with zipfile.ZipFile(archive_path, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(entries.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(archive_path) as archive:
            if archive.testzip() is not None or set(archive.namelist()) != set(entries) or any(
                    archive.read(name) != data for name, data in entries.items()):
                raise ValueError("Final bootstrap archive differs from verified delivery bytes")
            if [name for name in archive.namelist() if name.endswith(".kup")] != [embedded]:
                raise ValueError("Bootstrap archive must contain exactly one checked KUP")
        if source_identity() != before or inventory_v2(stage) != paths or any(
                (stage / name).read_bytes() != data for name, data in software.items()):
            raise ValueError("Source or staged software changed before bootstrap publication")
        if verify(kup)["manifest_sha256"] != package["manifest_sha256"]:
            raise ValueError("Final standalone KUP changed before publication")
        publish_new(((output, archive_path.read_bytes()), (kup_output, payload)))
    return {"zip": str(output), "kup": str(kup_output), "zip_sha256": digest(output.read_bytes()),
            "kup_sha256": digest(payload), "gui_build": gui_info["build"],
            "engine": ENGINE_CURRENT, "software_files": len(software), "package": package["build"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path, help="Immutable staged KUI directory with current runtime.kui")
    parser.add_argument("output", type=Path, help="New complete bootstrap ZIP")
    parser.add_argument("--kup-output", type=Path, help="New standalone KUP; default is the ZIP's sibling .kup")
    parser.add_argument("--channel", choices=("stable", "testing"), default="testing")
    args = parser.parse_args()
    try:
        print(json.dumps(build_repair(args.stage, args.output, kup_output=args.kup_output, channel=args.channel), indent=2))
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
