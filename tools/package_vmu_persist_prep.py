#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit and package the native preparation-only Card tools runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile
import zipfile

from check_loader_layout import EH, PH, inspect_elf, padded, region
from runtime_package import ADDRESS, MAX_MEMORY, envelope, flatten_elf, verify
from toy_pilot_symbols import _linked_symbols


REQUIRED_FUNCTIONS = (
    "_kui_vmu_fs_collect",
    "_kui_vmu_persist_profile_encode",
    "_kui_vmu_persist_profile_collect_snapshot",
)
MODE_SYMBOL = "_kui_vmu_persist_prep_build_mode"
FORBIDDEN_FUNCTIONS = (
    "kui_vmu_store_open", "kui_vmu_store_write", "kui_vmu_store_checkpoint",
    "kui_vmu_store_format", "kui_vmu_image_format_blank",
)
ARCHIVE = "vmu-persistence-preparation-test.zip"
AUDIT = "native-preparation-audit.json"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def loaded_symbol(data, typed, name, kind, flags, *, size=None):
    """Bind a typed section symbol independently to its actual PT_LOAD bytes."""
    address, length, actual_kind, section = typed.get(name, (0, 0, 0, None))
    alignment = 2 if kind == 2 else 4
    if (actual_kind != kind or not length or address % alignment or
            (kind == 2 and length % 2) or (size is not None and length != size) or
            section is None or section[1] != 1 or section[2] & 7 != flags or
            not section[3] <= address < address + length <= section[3] + section[5]):
        raise ValueError("Missing exact typed linked preparation symbol: " + name)
    offset = section[4] + address - section[3]
    contents = region(data, offset, length, name)
    header = EH.unpack_from(data)
    matches = []
    for index in range(header[10]):
        segment = PH.unpack_from(data, header[5] + index * header[9])
        typ, file_at, va, pa, file_bytes, _, permissions, _ = segment
        if typ == 1 and va <= address < address + length <= va + file_bytes:
            if (pa != va or offset != file_at + address - va or
                    contents != region(data, file_at + address - va, length, name) or
                    (kind == 2 and not permissions & 1) or
                    (kind == 1 and permissions & 2)):
                raise ValueError("Preparation symbol differs from its load segment: " + name)
            matches.append(segment)
    if len(matches) != 1:
        raise ValueError("Preparation symbol lacks one unambiguous load segment: " + name)
    return {"address": address, "bytes": length, "elf_file_offset": offset}, contents


def inspect_preparation(directory, expected_commit):
    directory = Path(directory)
    if not re.fullmatch(r"[0-9a-f]{40}", expected_commit):
        raise ValueError("Expected source commit must be 40 lowercase hexadecimal characters")
    elf = (directory / "vmu-harness.elf").read_bytes()
    provenance_bytes = (directory / "vmu-harness.compile.json").read_bytes()
    provenance = json.loads(provenance_bytes)
    if (provenance.get("commit") != expected_commit or
            provenance.get("source_dirty") is not False or
            provenance.get("elf_sha256") != sha256(elf)):
        raise ValueError("Preparation ELF lacks matching clean-source compile provenance")
    config = (directory / "prep-config").read_bytes()
    if config != b"PERSIST_PREP=1\n":
        raise ValueError("Preparation build configuration must be exactly PERSIST_PREP=1")
    image = inspect_elf(elf, ADDRESS, ADDRESS + MAX_MEMORY)
    payload, memory = flatten_elf(elf)
    if padded(image["payload"]) != payload or (image["memory_end"] - ADDRESS + 3) & ~3 != memory:
        raise ValueError("Independent ELF load inspections disagree")
    module = (directory / "KUI/tools.kui").read_bytes()
    runtime = verify(module)
    if module != envelope(payload, memory, expected_commit[:12]):
        raise ValueError("Card tools module differs from the exact source-bound ELF envelope")
    typed = _linked_symbols(elf, set(REQUIRED_FUNCTIONS) | {MODE_SYMBOL})
    linked = {}
    for name in REQUIRED_FUNCTIONS:
        linked[name] = loaded_symbol(elf, typed, name, 2, 6)[0]
    marker, marker_bytes = loaded_symbol(elf, typed, MODE_SYMBOL, 1, 2, size=4)
    if struct.unpack("<I", marker_bytes)[0] != 1:
        raise ValueError("Linked KUI_VMU_PERSIST_PREP build-mode marker is not 1")
    # GCC can retain a specialized clone even when the unsuffixed name is gone.
    forbidden = [name for name in image["symbols"]
                 if name.lstrip("_").split(".", 1)[0] in FORBIDDEN_FUNCTIONS]
    if forbidden:
        raise ValueError("Legacy source writer/recovery/format code remains linked: " +
                         ", ".join(sorted(forbidden)))
    if b"Virtual VMU - persistence preparation\0" not in payload:
        raise ValueError("Preparation runtime lacks its distinct on-screen application identity")
    files = {
        "KUI/tools.kui": module,
        "native-proof/vmu-harness.elf": elf,
        "native-proof/vmu-harness.compile.json": provenance_bytes,
        "native-proof/prep-config": config,
        "SOURCE-COMMIT.txt": (expected_commit + "\n").encode("ascii"),
    }
    audit = {
        "format": 1,
        "application": "VMU persistent-profile preparation",
        "source_commit": expected_commit,
        "source_dirty": False,
        "install_path": "KUI/tools.kui",
        "preparation_only": True,
        "retail_sd_saving_enabled": False,
        "source_image_journal_writes_enabled": False,
        "profile_output_create_new_enabled": True,
        "compile_macro": {"KUI_VMU_PERSIST_PREP": 1,
                          "linked_readonly_marker": MODE_SYMBOL, **marker},
        "required_linked_functions": linked,
        "legacy_functions_absent_including_compiler_clones": list(FORBIDDEN_FUNCTIONS),
        "runtime": runtime,
        "elf_sha256": sha256(elf),
        "module_sha256": sha256(module),
        "payload_sha256": sha256(payload),
        "compile_provenance_sha256": sha256(provenance_bytes),
    }
    return audit, files


def package(directory, expected_commit, guide):
    directory = Path(directory)
    output = directory / "package"
    archive = directory / ARCHIVE
    if (output.is_symlink() or (output.exists() and not output.is_dir()) or
            archive.is_symlink() or (archive.exists() and not archive.is_file())):
        raise ValueError("Generated package destinations must be ordinary directories/files")
    # Failed admission must not leave a previous build looking deliverable.
    if output.exists():
        shutil.rmtree(output)
    archive.unlink(missing_ok=True)
    audit, files = inspect_preparation(directory, expected_commit)
    guide_bytes = Path(guide).read_bytes()
    if not guide_bytes or b"KUI/tools.kui" not in guide_bytes:
        raise ValueError("Preparation guide is empty or lacks the Card tools install path")
    files["README-FIRST.md"] = guide_bytes
    audit["artifacts"] = {
        name: {"bytes": len(data), "sha256": sha256(data)}
        for name, data in sorted(files.items())
    }
    files[AUDIT] = (json.dumps(audit, indent=2, sort_keys=True) + "\n").encode("utf-8")
    files["SHA256SUMS.txt"] = "".join(
        f"{sha256(data)}  {name}\n" for name, data in sorted(files.items())
    ).encode("ascii")
    temporary = Path(tempfile.mkdtemp(prefix="persist-prep-package-", dir=directory))
    temporary_archive = temporary / ARCHIVE
    try:
        folder = temporary / "package"
        for name, data in files.items():
            target = folder / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        with zipfile.ZipFile(temporary_archive, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                zipped.writestr(info, data)
        folder.replace(output)
        temporary_archive.replace(archive)
    finally:
        shutil.rmtree(temporary)
    return {"source_commit": expected_commit, "build": expected_commit[:12],
            "package": str(output), "zip": str(archive),
            "zip_sha256": sha256(archive.read_bytes()), "module_sha256": audit["module_sha256"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--guide", type=Path,
                        default=Path(__file__).resolve().parents[1] / "docs/vmu-persistent-profile-prep.md")
    args = parser.parse_args()
    try:
        print(json.dumps(package(args.directory, args.expected_commit, args.guide), indent=2))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
