#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit and package the isolated physical-VMU read-only import preparer."""
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
from check_retail_stack import EDGE, NODE


REQUIRED_FUNCTIONS = (
    "_kui_vmu_import_source_read", "_kui_vmu_import_prepare_export", "_kui_vmu_import_active_admit",
    "_kui_vmu_read_bounded", "_kui_vmu_read_bounded_last", "_kui_vmu_fs_admit",
    "_kui_vmu_image_clone_capture", "_kui_vmu_image_clone_verify_source",
    "_kui_vmu_image_clone_copy", "_kui_vmu_image_validate",
    "_kui_vmu_save_snapshot_capture", "_kui_vmu_save_snapshot_read_block",
    "_kui_vmu_fs_collect", "_kui_vmu_persist_profile_encode",
    "_kui_vmu_persist_profile_collect_snapshot",
)
MODE_SYMBOL = "_kui_vmu_import_prep_build_mode"
FORBIDDEN_FUNCTIONS = (
    "kui_vmu_image_format_blank", "kui_vmu_store_format", "kui_vmu_store_open",
    "kui_vmu_store_write", "kui_vmu_store_checkpoint", "prepare_store", "run_test", "new_probe",
)
PHYSICAL_WRITE_PREFIXES = (
    "vmu_block_write", "vmufs_write", "vmufs_create", "vmufs_delete", "vmufs_remove",
    "vmufs_rename", "vmufs_format", "vmufs_raw_write",
    "vmufs_root_write", "vmufs_dir_write", "vmufs_fat_write", "vmufs_file_write",
)
PHYSICAL_READ_ONLY_CALLS = {"vmufs_mutex_lock", "vmufs_mutex_unlock"}
FILESYSTEM_UNIT = "src/core/vmu_fs_admission.c"
FILESYSTEM_REPORT = "src/core/vmu_fs_admission.ci"
TITLE = b"Virtual VMU - import preparation\0"
ARCHIVE = "vmu-import-prep-test.zip"
AUDIT = "native-import-prep-audit.json"


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


def function_base(name):
    return name.rsplit(":", 1)[-1].lstrip("_").split(".", 1)[0]


def owned_filesystem_helpers(relative, graph):
    """Admit local helper definitions only in the exact filesystem unit."""
    if (relative != FILESYSTEM_REPORT or
            not graph.startswith('graph: { title: "' + FILESYSTEM_UNIT + '"\n')):
        return set()
    definitions = NODE.findall(graph)
    titles = [title for title, _ in definitions]
    admitted = set()
    for title, label in definitions:
        if not re.fullmatch(re.escape(FILESYSTEM_UNIT) + r":vmu_fs_[A-Za-z0-9_.]+", title):
            continue
        function = title.split(":", 1)[1]
        # GCC's frame label omits the final ordinal on an IPA clone even
        # though the node title and every edge retain its complete identity.
        display = re.sub(r"\.(isra|constprop|part)\.[0-9]+$", r".\1", function)
        frame = ("(?:" + re.escape(function) + "|" + re.escape(display) + ")" +
                 r"\\n" + re.escape(FILESYSTEM_UNIT) +
                 r":[0-9]+:[0-9]+\\n[0-9]+ bytes \(static\)")
        if titles.count(title) == 1 and re.fullmatch(frame, label):
            admitted.add(title)
    return admitted


def inspect_project_vm_access(directory, image):
    """Audit owned compiler edges while distinguishing dormant SDK code.

    KOS can retain read/write SDK functions in one text section. Their mere
    presence is recorded; no project call to a physical writer is permitted.
    Original compiler reports are included so this distinction is reviewable.
    """
    reports = sorted(path for path in directory.rglob("*.ci")
                     if path.relative_to(directory).parts[0] != "package")
    if not reports:
        raise ValueError("Missing project compiler callgraph proof of physical VMU read-only access")
    files, reads, transfers, nodes, filesystem_helpers = {}, [], [], set(), set()
    for path in reports:
        data = path.read_bytes()
        graph = data.decode("utf-8")
        relative = path.relative_to(directory).as_posix()
        local_helpers = owned_filesystem_helpers(relative, graph)
        filesystem_helpers.update(local_helpers)
        nodes.update(function_base(title) for title, _ in NODE.findall(graph))
        for source, target, _ in EDGE.findall(graph):
            caller, callee = function_base(source), function_base(target)
            if (callee.startswith(("vmu_", "vmufs_")) and
                    callee not in PHYSICAL_READ_ONLY_CALLS and target not in local_helpers):
                raise ValueError("Project call can write a physical VMU: " + caller + " -> " + callee)
            if caller == "kui_vmu_import_source_read" and callee == "kui_vmu_read_bounded":
                reads.append(dict(source=source, target=target))
            if caller == "kui_vmu_read_bounded" and callee == "maple_queue_frame":
                transfers.append(dict(source=source, target=target))
        files["native-proof/callgraph/" + relative] = data
    if "kui_vmu_import_source_read" not in nodes or not reads or not transfers:
        raise ValueError("Physical source wrapper lacks its bounded block-read/frame edge")
    dormant = sorted(name for name in image["symbols"]
                     if function_base(name).startswith(PHYSICAL_WRITE_PREFIXES))
    return dict(project_physical_write_edges=0, source_read_edges=reads,
                bounded_read_frame_edges=transfers, sdk_call_allowlist=sorted(PHYSICAL_READ_ONLY_CALLS),
                owned_filesystem_helpers=sorted(filesystem_helpers),
                sdk_write_symbols_present=dormant,
                evidence="owned compiler callgraphs; exact source-bound ELF and read-only source adapter",
                compiler_reports=len(reports)), files


def inspect_import_preparation(directory, expected_commit):
    directory = Path(directory)
    if not re.fullmatch(r"[0-9a-f]{40}", expected_commit):
        raise ValueError("Expected source commit must be 40 lowercase hexadecimal characters")
    elf = (directory / "vmu-harness.elf").read_bytes()
    provenance_bytes = (directory / "vmu-harness.compile.json").read_bytes()
    provenance = json.loads(provenance_bytes)
    if (not isinstance(provenance, dict) or provenance.get("commit") != expected_commit or
            provenance.get("source_dirty") is not False or
            provenance.get("elf_sha256") != sha256(elf)):
        raise ValueError("Import preparation ELF lacks matching clean-source compile provenance")
    config = (directory / "prep-config").read_bytes()
    if config != b"PERSIST_IMPORT=1\n":
        raise ValueError("Import preparation configuration must be exactly PERSIST_IMPORT=1")
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
        raise ValueError("Linked KUI_VMU_PERSIST_IMPORT build-mode marker is not 1")
    forbidden = [name for name in image["symbols"]
                 if (function_base(name) in FORBIDDEN_FUNCTIONS or
                     function_base(name).startswith(("kui_vmu_store_", "kui_vmu_persist_session_")))]
    if forbidden:
        raise ValueError("Formatting or synthetic harness code remains linked: " +
                         ", ".join(sorted(forbidden)))
    if TITLE not in payload:
        raise ValueError("Import preparation lacks its distinct on-screen application identity")
    vm_access, callgraph_files = inspect_project_vm_access(directory, image)
    files = {
        "KUI/tools.kui": module,
        "native-proof/vmu-harness.elf": elf,
        "native-proof/vmu-harness.compile.json": provenance_bytes,
        "native-proof/prep-config": config,
        "SOURCE-COMMIT.txt": (expected_commit + "\n").encode("ascii"),
    }
    files.update(callgraph_files)
    audit = {
        "format": 1,
        "application": "VMU import preparation",
        "source_commit": expected_commit,
        "source_dirty": False,
        "install_path": "KUI/tools.kui",
        "physical_vmu_import_only": True,
        "physical_vmu_writes_enabled": False,
        "source_capture": "all saves and metadata exact; FAT-free user blocks normalized to zero without physical reads",
        "physical_vmu_access": vm_access,
        "retail_sd_saving_enabled": False,
        "source_image_journal_writes_enabled": False,
        "new_pair_output_create_new_only": True,
        "new_pair_path_policy": "/KUI/vmu-test/p0002..p9999; identity installed last",
        "filesystem_metadata_writes_enabled": True,
        "formatting_enabled": False,
        "profile_output_create_new_enabled": True,
        "compile_macro": {"KUI_VMU_PERSIST_IMPORT": 1,
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
    audit, files = inspect_import_preparation(directory, expected_commit)
    guide_bytes = Path(guide).read_bytes()
    if not guide_bytes or b"KUI/tools.kui" not in guide_bytes:
        raise ValueError("Import preparation guide is empty or lacks the Card tools install path")
    files["README-FIRST.md"] = guide_bytes
    audit["artifacts"] = {name: {"bytes": len(data), "sha256": sha256(data)}
                          for name, data in sorted(files.items())}
    files[AUDIT] = (json.dumps(audit, indent=2, sort_keys=True) + "\n").encode("utf-8")
    files["SHA256SUMS.txt"] = "".join(
        f"{sha256(data)}  {name}\n" for name, data in sorted(files.items())
    ).encode("ascii")
    temporary = Path(tempfile.mkdtemp(prefix="import-prep-package-", dir=directory))
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
                        default=Path(__file__).resolve().parents[1] / "docs/vmu-import-prep-test.md")
    args = parser.parse_args()
    try:
        print(json.dumps(package(args.directory, args.expected_commit, args.guide), indent=2))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
