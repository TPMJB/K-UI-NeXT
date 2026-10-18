#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit and package the isolated owned-image physical journal proof."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile
import zipfile

from check_loader_layout import EH, SH, SYM, inspect_elf, padded, region
from package_vmu_persist_prep import loaded_symbol, sha256
from runtime_package import ADDRESS, MAX_MEMORY, envelope, flatten_elf, verify
from toy_pilot_symbols import _linked_symbols


REQUIRED_FUNCTIONS = (
    "_kui_vmu_persist_session_init", "_kui_vmu_persist_session_commit",
    "_kui_vmu_persist_session_refresh", "_kui_vmu_fs_admit",
    "_kui_vmu_store_open", "_kui_vmu_store_write",
)
MODE_SYMBOL = "_kui_vmu_persist_proof_build_mode"
FORBIDDEN_FUNCTIONS = (
    "kui_vmu_image_format_blank", "kui_vmu_store_format", "prepare_store", "run_test",
)
TITLE = b"Virtual VMU - physical journal proof\0"
ARCHIVE = "vmu-physical-journal-proof-test.zip"
AUDIT = "native-physical-journal-proof-audit.json"


def require_public_symbols(data, wanted):
    """Public proof ABI names must be global definitions, including the marker."""
    header = EH.unpack_from(data)
    sections = [SH.unpack_from(data, header[6] + index * header[11])
                for index in range(header[12])]
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = region(data, strings[4], strings[5], "public symbol names")
        for at in range(section[4], section[4] + section[5], section[9]):
            name, _, _, info, other, _ = SYM.unpack_from(data, at)
            end = names.find(b"\0", name)
            label = names[name:end].decode("ascii")
            if label in wanted and (info >> 4 != 1 or other & 3):
                raise ValueError("Proof ABI symbol is not a public global definition: " + label)


def inspect_physical_proof(directory, expected_commit):
    directory = Path(directory)
    if not re.fullmatch(r"[0-9a-f]{40}", expected_commit):
        raise ValueError("Expected source commit must be 40 lowercase hexadecimal characters")
    elf = (directory / "vmu-harness.elf").read_bytes()
    provenance_bytes = (directory / "vmu-harness.compile.json").read_bytes()
    provenance = json.loads(provenance_bytes)
    if (not isinstance(provenance, dict) or provenance.get("commit") != expected_commit or
            provenance.get("source_dirty") is not False or
            provenance.get("elf_sha256") != sha256(elf)):
        raise ValueError("Physical proof ELF lacks matching clean-source compile provenance")
    config = (directory / "prep-config").read_bytes()
    if config != b"PERSIST_PROOF=1\n":
        raise ValueError("Physical proof configuration must be exactly PERSIST_PROOF=1")
    image = inspect_elf(elf, ADDRESS, ADDRESS + MAX_MEMORY)
    payload, memory = flatten_elf(elf)
    if (padded(image["payload"]) != payload or
            ((image["memory_end"] - ADDRESS + 3) & ~3) != memory):
        raise ValueError("Independent ELF load inspections disagree")
    module = (directory / "KUI/tools.kui").read_bytes()
    runtime = verify(module)
    if module != envelope(payload, memory, expected_commit[:12]):
        raise ValueError("Card tools module differs from the exact source-bound ELF envelope")
    wanted = set(REQUIRED_FUNCTIONS) | {MODE_SYMBOL}
    typed = _linked_symbols(elf, wanted)
    require_public_symbols(elf, wanted)
    linked = {name: loaded_symbol(elf, typed, name, 2, 6)[0]
              for name in REQUIRED_FUNCTIONS}
    marker, marker_bytes = loaded_symbol(elf, typed, MODE_SYMBOL, 1, 2, size=4)
    if struct.unpack("<I", marker_bytes)[0] != 1:
        raise ValueError("Linked KUI_VMU_PERSIST_PROOF build-mode marker is not 1")
    forbidden = [name for name in image["symbols"]
                 if name.lstrip("_").split(".", 1)[0] in FORBIDDEN_FUNCTIONS]
    if forbidden:
        raise ValueError("Formatting or synthetic harness code remains linked: " +
                         ", ".join(sorted(forbidden)))
    if TITLE not in payload:
        raise ValueError("Physical proof lacks its distinct on-screen application identity")
    files = {
        "KUI/tools.kui": module,
        "native-proof/vmu-harness.elf": elf,
        "native-proof/vmu-harness.compile.json": provenance_bytes,
        "native-proof/prep-config": config,
        "SOURCE-COMMIT.txt": (expected_commit + "\n").encode("ascii"),
    }
    audit = {
        "format": 1,
        "application": "VMU physical journal proof",
        "source_commit": expected_commit,
        "source_dirty": False,
        "install_path": "KUI/tools.kui",
        "physical_journal_proof_only": True,
        "retail_sd_saving_enabled": False,
        "source_image_journal_writes_enabled": True,
        "physical_write_scope": "existing owned image and journal after fresh filesystem admission",
        "filesystem_metadata_writes_enabled": False,
        "formatting_enabled": False,
        "profile_output_create_new_enabled": False,
        "compile_macro": {"KUI_VMU_PERSIST_PROOF": 1,
                          "linked_readonly_marker": MODE_SYMBOL, **marker},
        "required_linked_functions": linked,
        "forbidden_functions_absent_including_compiler_clones": list(FORBIDDEN_FUNCTIONS),
        "runtime": runtime,
        "elf_sha256": sha256(elf),
        "module_sha256": sha256(module),
        "payload_sha256": sha256(payload),
        "compile_provenance_sha256": sha256(provenance_bytes),
    }
    return audit, files


def package(directory, expected_commit, guide):
    directory = Path(directory)
    output, archive = directory / "package", directory / ARCHIVE
    if (output.is_symlink() or (output.exists() and not output.is_dir()) or
            archive.is_symlink() or (archive.exists() and not archive.is_file())):
        raise ValueError("Generated package destinations must be ordinary directories/files")
    # Wrong input must invalidate old successful deliverables before admission.
    if output.exists():
        shutil.rmtree(output)
    archive.unlink(missing_ok=True)
    audit, files = inspect_physical_proof(directory, expected_commit)
    guide_bytes = Path(guide).read_bytes()
    if not guide_bytes or b"KUI/tools.kui" not in guide_bytes:
        raise ValueError("Physical proof guide is empty or lacks the Card tools install path")
    files["README-FIRST.md"] = guide_bytes
    audit["artifacts"] = {name: {"bytes": len(data), "sha256": sha256(data)}
                          for name, data in sorted(files.items())}
    files[AUDIT] = (json.dumps(audit, indent=2, sort_keys=True) + "\n").encode("utf-8")
    files["SHA256SUMS.txt"] = "".join(
        f"{sha256(data)}  {name}\n" for name, data in sorted(files.items())
    ).encode("ascii")
    temporary = Path(tempfile.mkdtemp(prefix="physical-proof-package-", dir=directory))
    try:
        folder = temporary / "package"
        for name, data in files.items():
            target = folder / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        with zipfile.ZipFile(temporary / ARCHIVE, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                zipped.writestr(info, data)
        folder.replace(output)
        (temporary / ARCHIVE).replace(archive)
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
                        default=Path(__file__).resolve().parents[1] / "docs/vmu-physical-journal-proof.md")
    args = parser.parse_args()
    try:
        print(json.dumps(package(args.directory, args.expected_commit, args.guide), indent=2))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
