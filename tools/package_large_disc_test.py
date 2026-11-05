#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package a matching GUI and three native GD Play modules for a copy-over test."""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile
import zlib

from package_medium_test import (ROOT, build_info, clean_head, commit, git,
                                 public_mapping, require, sha, verify_zip, wire)
from package_physical_disc_test import collect_vmu_build, ordinary, provenance
from runtime_package import ELF_HEADER, PROGRAM_HEADER, envelope, flatten_elf, verify
from package_update_payload import add_update_payload, publish_update_payload

GUIDE = "docs/gd-play-large-executables.md"
PURPOSE = "Native GD Play larger-executable console test"
TARGETS = ("KUI/runtime.kui", "KUI/apps/disc/native-disc.kui",
           "KUI/apps/disc/native-disc-vmu.kui", "KUI/apps/disc/native-disc-vmu-a1.kui")


def executable_limit(head):
    text = git("show", head + ":include/kui/disc_launch_limits.h").decode("ascii")
    found = re.findall(r"^#define\s+KUI_DISC_LAUNCH_EXEC_MAX_BYTES\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$",
                       text, re.MULTILINE)
    require(len(found) == 1, "Cannot read the committed physical-disc executable limit")
    value = int(found[0], 0)
    require(value == 12 * 1024 * 1024, "Review a changed physical-disc executable limit")
    return value


def capability(module, flags, limit):
    """Check the actual linked entry header, not a manifest's claimed limit."""
    info = verify(module)
    payload = module[64:]
    require(len(payload) >= 0x2004 and len(payload) % 4 == 0, "Invalid physical-disc module size")
    expected = (b"KUIDISC1", 1, 64, 0x1000, 512, 0x8ce00000,
                len(payload) - 0x2000, 0x8ce00000, 0x8c010000, limit,
                0x8cff0000, 0x2000, flags, 0, 0)
    actual = struct.unpack_from("<8s14I", payload, 0x100)
    require(actual == expected, "Physical-disc module capability differs from the committed contract")
    stage_limit = 0x10000 if flags == 1 else 0x100000
    require(4 <= len(payload) - 0x2000 <= stage_limit, "Physical-disc module stage exceeds its reservation")
    require(not any(payload[0x1000:0x1200]), "Physical-disc template contains a prefilled launch request")
    return info


def stage_binary(elf, binary, vmu):
    """Bind the embedded stage bytes to its one linked high-RAM load segment."""
    require(len(elf) >= ELF_HEADER.size, "Truncated physical-disc stage ELF")
    h = ELF_HEADER.unpack_from(elf)
    require(h[0][:7] == b"\x7fELF\x01\x01\x01" and h[1:5] == (2, 42, 1, 0x8ce00000) and
            h[8] == ELF_HEADER.size and h[9] == PROGRAM_HEADER.size and h[10] == 1 and
            ELF_HEADER.size <= h[5] <= len(elf) - PROGRAM_HEADER.size,
            "Physical-disc stage ELF has an unexpected load contract")
    kind, offset, address, physical, size, memory, flags, _ = PROGRAM_HEADER.unpack_from(elf, h[5])
    require(kind == 1 and address == physical == 0x8ce00000 and flags & 1 and
            4 <= size <= (0x100000 if vmu else 0x10000) and size <= memory <=
            (0x1e0000 if vmu else 0x20000) and offset <= len(elf) - size,
            "Physical-disc stage load segment exceeds its checked reservation")
    require(binary == elf[offset:offset + size], "Physical-disc stage binary differs from its linked ELF")


def native_build(directory, head, limit):
    directory = directory.resolve(strict=True)
    names = ("entry.elf", "entry.map", "stage.elf", "stage.map", "stage.bin")
    parts = {name: ordinary(directory / name) for name in names}
    for name in ("entry", "stage"):
        parts[name + ".compile.json"] = provenance(directory / (name + ".compile.json"),
            parts[name + ".elf"], head, True)
    module = ordinary(directory / "native-disc.kui")
    info = capability(module, 1, limit)
    require(info["build"] == head[:12], "Physical-only disc module build differs from HEAD")
    payload, memory = flatten_elf(parts["entry.elf"])
    require(module == envelope(payload, memory, head[:12]), "Physical-only module differs from its linked ELF")
    require(payload[0x2000:] == parts["stage.bin"], "Physical-only entry embeds a different stage binary")
    stage_binary(parts["stage.elf"], parts["stage.bin"], False)
    for path in sorted(directory.rglob("*")):
        if path.suffix in (".su", ".ci") and path.is_file():
            parts[path.relative_to(directory).as_posix()] = ordinary(path)
    return module, {"build/disc-native/" + name: data for name, data in parts.items()}, info


def vmu_build(directory, head, limit, slot):
    module, artifacts = collect_vmu_build(directory, head[:12], head)
    info = capability(module, 3, limit)
    stage_binary(artifacts["artifacts/disc-vmu/disc-stage.elf"],
                 artifacts["artifacts/disc-vmu/disc-stage.bin"], True)
    stamp = ordinary(directory / "disc-vmu-slot-config")
    require(stamp == f"DISC_VMU_VIRTUAL_A1={int(slot == 'A1')}\n".encode(),
            "Physical-disc VMU build has the wrong slot configuration")
    caption = f"PHYSICAL DISC / VIRTUAL {slot} / SCI\0".encode()
    require(caption in module[64:], "Physical-disc VMU stage does not identify the expected slot")
    other = "A2" if slot == "A1" else "A1"
    require(f"PHYSICAL DISC / VIRTUAL {other} / SCI\0".encode() not in module[64:],
            "Physical-disc VMU stage has conflicting slot identities")
    prefix = f"build/disc-vmu-{slot.lower()}/"
    files = {prefix + name.removeprefix("artifacts/disc-vmu/"): data for name, data in artifacts.items()}
    files[prefix + "disc-vmu-slot-config"] = stamp
    return module, files, info


def readme(guide, head, public):
    title, _, rest = guide.decode("utf-8").partition("\n")
    publication = (f"Public source commit: `{public}`." if public else
                   "This update is not published to GitHub; exact source is included.")
    return (f"{title}\n\nLocal build: `{head[:12]}`. {publication}\n\n"
            "Install the accompanying `.kup` from Settings → Updates, or select this ZIP there. "
            "The standalone `.kup` contains the GUI and all three test modules; the ZIP is optional evidence. "
            "Older installations first need the current updater-engine root upgrade.\n\n" + rest.lstrip()).encode()


def package(args):
    head = clean_head()
    base = commit(args.base)
    subprocess.run(["git", "merge-base", "--is-ancestor", base, head], cwd=ROOT, check=True)
    source = public_mapping(head, args.public_commit) if args.public_commit else {
        "local_commit": head, "local_tree": git("rev-parse", head + "^{tree}").decode().strip(),
        "public_commit": None, "public_tree_mapping": "not published; exact source included in ZIP"}
    baseline = public_mapping(base, args.public_base)
    limit = executable_limit(head)
    elf, runtime = ordinary(args.elf), ordinary(args.runtime)
    record = ordinary(args.compile_record or args.elf.with_suffix(".compile.json"))
    gui = build_info(elf, runtime, record, head)
    files = {TARGETS[0]: runtime, "build/gui/kui-runtime.elf": elf,
             "build/gui/kui-runtime.map": ordinary(args.map or args.elf.with_suffix(".map")),
             "build/gui/kui-runtime.compile.json": record,
             "source/HEAD.tar.gz": gzip.compress(git("archive", "--format=tar", "--prefix=K-UI-NeXT/", head), mtime=0),
             "source/delta.patch": git("diff", "--binary", "--full-index", base, head, "--"),
             GUIDE: git("show", head + ":" + GUIDE),
             "LICENSE": git("show", head + ":LICENSE"),
             "THIRD_PARTY.md": git("show", head + ":THIRD_PARTY.md")}
    require(files["build/gui/kui-runtime.map"], "Linked GUI map is empty")
    modules = {}
    for target, result in ((TARGETS[1], native_build(args.disc_build, head, limit)),
                           (TARGETS[2], vmu_build(args.vmu_build, head, limit, "A2")),
                           (TARGETS[3], vmu_build(args.vmu_a1_build, head, limit, "A1"))):
        module, artifacts, info = result
        require(not set(artifacts).intersection(files), "Conflicting build artifact paths")
        files[target] = module
        files.update(artifacts)
        modules[target] = info
    files["README.md"] = readme(files[GUIDE], head, args.public_commit)
    require(args.evidence, "Supply the targeted host/build audit evidence")
    evidence = []
    for index, path in enumerate(args.evidence, 1):
        name = f"evidence/{index:02d}-" + re.sub(r"[^A-Za-z0-9._-]", "_", path.name)
        files[name] = ordinary(path)
        evidence.append(name)
    output = args.output.resolve()
    require(output.suffix.lower() == ".zip", "Output must be a ZIP path")
    require(not output.is_relative_to(ROOT), "Save the package outside the source checkout")
    update_name = output.with_suffix(".kup").name
    update_report = add_update_payload(files, update_name)
    manifest = {"format": 1, "purpose": PURPOSE,
                "source": source, "base": baseline, "runtime": gui, "modules": modules,
                "install": [update_name], "update_package": update_report,
                "native_executable_limit_bytes": limit,
                "staging_ram_required": "available contiguous main RAM with checked handoff margins",
                "virtual_vmu_preference_changed_by_install": False,
                "native_gd_only": True, "windows_ce_direct_boot_added": False,
                "evidence": evidence, "console_acceptance": "not established by this package or host logs",
                "manifest_coverage": "All ZIP members except manifest.json itself",
                "files": {name: {"bytes": len(data), "sha256": sha(data)} for name, data in sorted(files.items())}}
    files["manifest.json"] = wire(manifest)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix="." + output.name + ".", dir=output.parent, delete=False) as stream:
            temporary = Path(stream.name)
            with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
                for name, data in sorted(files.items()):
                    item = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
                    item.compress_type = zipfile.ZIP_DEFLATED
                    item.external_attr = 0o100644 << 16
                    archive.writestr(item, data, compresslevel=9)
            stream.flush()
            os.fsync(stream.fileno())
        verify_zip(temporary, files)
        clean_head(head)
        update_output = publish_update_payload(files[update_name], output.with_suffix(".kup"))
        os.replace(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return {"output": str(output), "bytes": output.stat().st_size, "sha256": sha(ordinary(output)),
            "members": len(files), "build": head[:12], "install": [update_name],
            "source": source, "base": baseline, "update_package": update_output}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--base", required=True)
    parser.add_argument("--public-base", required=True)
    parser.add_argument("--public-commit")
    parser.add_argument("--disc-build", type=Path, default=ROOT / "build/disc-native")
    parser.add_argument("--vmu-build", type=Path, required=True, help="A2 physical-disc VMU build directory")
    parser.add_argument("--vmu-a1-build", type=Path, required=True, help="A1 physical-disc VMU build directory")
    parser.add_argument("--elf", "--gui-elf", type=Path, default=ROOT / "build/kui-runtime.elf")
    parser.add_argument("--runtime", type=Path, default=ROOT / "build/runtime.kui")
    parser.add_argument("--map", type=Path)
    parser.add_argument("--compile-record", type=Path)
    parser.add_argument("--evidence", type=Path, action="append", default=[])
    try:
        print(json.dumps(package(parser.parse_args()), indent=2))
    except (OSError, ValueError, KeyError, EOFError, struct.error, zlib.error,
            subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
