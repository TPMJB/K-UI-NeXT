#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inject a validated private card profile into an audited generic SD proof.

Compilation uses the public deterministic template. Personalization changes only
the fixed read-only profile bytes and the runtime envelope's two checksums; it
does not relink code or claim that the resulting ELF was compiled for this card.
Keep the output and real profile outside tracked source files.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile

from check_loader_layout import EH, PH, inspect_elf, padded, region
from runtime_package import ADDRESS, envelope, flatten_elf, verify
from toy_pilot_symbols import _linked_symbols
from vmu_sd_profile import decode_profile, template_profile

STAGE = 0x8CE00000
MODULE = "retail-maple-observe.kui"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def changed_ranges(before, after):
    if len(before) != len(after):
        raise ValueError("Personalization must preserve every artifact size")
    result = []
    for offset, (old, new) in enumerate(zip(before, after)):
        if old == new:
            continue
        if result and result[-1]["offset"] + result[-1]["bytes"] == offset:
            result[-1]["bytes"] += 1
        else:
            result.append(dict(offset=offset, bytes=1))
    return result


def artifact_change(before, after, allowed):
    changes = changed_ranges(before, after)
    for item in changes:
        if not any(start <= item["offset"] and
                   item["offset"] + item["bytes"] <= start + count
                   for start, count in allowed):
            raise ValueError("Changed bytes escape the admitted profile/checksum spans")
    return dict(base_sha256=sha(before), personalized_sha256=sha(after),
                bytes=len(after), changed_ranges=changes,
                allowed_ranges=[dict(offset=start, bytes=count) for start, count in allowed])


def readonly_blob(data, image, start_name, end_name, count):
    """Resolve exact symbols through both section and PT_LOAD file mappings."""
    typed = _linked_symbols(data, {start_name, end_name})
    start = typed.get(start_name)
    end = typed.get(end_name)
    if start is None or end is None:
        raise ValueError("Missing bounded read-only blob symbols: " + start_name)
    address, _, kind, section = start
    finish, _, end_kind, end_section = end
    if (kind not in (0, 1) or end_kind not in (0, 1) or section is None or
            section != end_section or section[1] != 1 or section[2] & 7 != 2 or
            address & 3 or finish - address != count or count <= 0 or
            not section[3] <= address < finish <= section[3] + section[5] or
            image["symbols"].get(start_name) != address or
            image["symbols"].get(end_name) != finish):
        raise ValueError("Profile/stage blob is not its exact read-only allocated span")
    header = EH.unpack_from(data)
    matches = []
    for index in range(header[10]):
        kind, offset, va, pa, size, _, _, _ = PH.unpack_from(
            data, header[5] + index * header[9])
        if kind == 1 and va <= address < finish <= va + size and pa == va:
            matches.append(offset + address - va)
    section_offset = section[4] + address - section[3]
    if len(matches) != 1 or matches[0] != section_offset:
        raise ValueError("Read-only blob has ambiguous section/load file offsets")
    region(data, section_offset, count, "read-only blob")
    return address, section_offset


def replace_span(data, offset, expected, replacement):
    if len(expected) != len(replacement) or region(data, offset, len(expected), "patch") != expected:
        raise ValueError("Patch input differs from the admitted template")
    return data[:offset] + replacement + data[offset + len(expected):]


def _transformation(base_directory, profile, *, expected_commit=None):
    """Derive all permitted bytes and audit fields from independent base files."""
    base = Path(base_directory).resolve()
    summary = decode_profile(profile)
    template = template_profile()
    if profile == template:
        raise ValueError("The build template is not an owner card profile")
    if not base.is_dir():
        raise ValueError("Requires an existing generic base proof directory")
    for path in base.rglob("*"):
        if path.is_symlink() or not (path.is_dir() or path.is_file()):
            raise ValueError("Base proof must contain ordinary files and directories only")
    if (base / "personalization-audit.json").exists() or (base / "base-provenance").exists():
        raise ValueError("Already-personalized input is not a clean generic build")
    original = {name: (base / name).read_bytes() for name in
                ("stage.elf", "stage.bin", "entry.elf", MODULE, "vmu-sd-profile.bin")}
    record_bytes = (base / "entry.compile.json").read_bytes()
    record = json.loads(record_bytes)
    if (not isinstance(record, dict) or record.get("source_dirty") is not False or
            not isinstance(record.get("commit"), str) or
            not re.fullmatch(r"[0-9a-f]{40}", record["commit"]) or
            record.get("elf_sha256") != sha(original["entry.elf"]) or
            (expected_commit is not None and record["commit"] != expected_commit)):
        raise ValueError("Base entry lacks matching clean source compile provenance")
    if original["vmu-sd-profile.bin"] != template:
        raise ValueError("Base SD profile is not the exact deterministic build template")
    stage = inspect_elf(original["stage.elf"], STAGE, 0x8CFE0000)
    entry = inspect_elf(original["entry.elf"], ADDRESS, 0x8C112000)
    profile_address, stage_file_offset = readonly_blob(original["stage.elf"], stage,
        "__vmu_sd_profile_blob_start", "__vmu_sd_profile_blob_end", len(template))
    stage_offset = profile_address - STAGE
    if (original["stage.bin"] != padded(stage["payload"]) or
            region(stage["payload"], stage_offset, len(template), "stage profile") != template):
        raise ValueError("Base stage binary differs from its ELF/template profile")
    stage_address, entry_file_offset = readonly_blob(original["entry.elf"], entry,
        "__retail_stage_blob_start", "__retail_stage_blob_end", len(original["stage.bin"]))
    entry_offset = stage_address - ADDRESS
    if (stage_address != ADDRESS + 0x2000 or
            stage_address + len(original["stage.bin"]) != ADDRESS + len(entry["payload"]) or
            region(entry["payload"], entry_offset, len(original["stage.bin"]), "entry stage") !=
            original["stage.bin"]):
        raise ValueError("Base entry contains a different stage or unexpected relocation bounds")
    payload, memory = flatten_elf(original["entry.elf"])
    runtime = verify(original[MODULE])
    build = record["commit"][:12]
    if (runtime["build"] != build or payload != padded(entry["payload"]) or
            original[MODULE] != envelope(payload, memory, build)):
        raise ValueError("Base runtime envelope differs from its clean entry ELF/build")
    if (base / "entry.bin").exists():
        original["entry.bin"] = (base / "entry.bin").read_bytes()
        if original["entry.bin"] != entry["payload"]:
            raise ValueError("Optional entry binary differs from its ELF")
    if (base / "worker.bin").exists():
        worker = (base / "worker.bin").read_bytes()
        if (len(worker) < 64 or struct.unpack_from("<3I", worker) != (0x564D4F31, 5, 64) or
                struct.unpack_from("<I", worker, 56)[0] != 2):
            raise ValueError("Personalization requires the SD-read-only worker mode")
    entry_profile_offset = entry_offset + stage_offset
    replacements = {
        "stage.elf": replace_span(original["stage.elf"], stage_file_offset, template, profile),
        "stage.bin": replace_span(original["stage.bin"], stage_offset, template, profile),
        "entry.elf": replace_span(original["entry.elf"], entry_file_offset + stage_offset, template, profile),
        "vmu-sd-profile.bin": profile,
    }
    if "entry.bin" in original:
        replacements["entry.bin"] = replace_span(original["entry.bin"], entry_profile_offset, template, profile)
    new_payload, new_memory = flatten_elf(replacements["entry.elf"])
    if new_memory != memory:
        raise ValueError("Personalization changed the ELF memory layout")
    replacements[MODULE] = envelope(new_payload, memory, build)
    if verify(replacements[MODULE])["build"] != build:
        raise ValueError("Personalized runtime envelope did not verify")
    offsets = {"stage.elf": [(stage_file_offset, len(profile))],
               "stage.bin": [(stage_offset, len(profile))],
               "entry.elf": [(entry_file_offset + stage_offset, len(profile))],
               "entry.bin": [(entry_profile_offset, len(profile))],
               "vmu-sd-profile.bin": [(0, len(profile))],
               MODULE: [(32, 4), (60, 4), (64 + entry_profile_offset, len(profile))]}
    artifacts = {name: artifact_change(original[name], value, offsets[name])
                 for name, value in replacements.items()}
    report = dict(format=1, source_commit=record["commit"],
                  operation="fixed-read-only-profile-personalization; no recompilation or relinking",
                  base_profile_sha256=sha(template), profile_sha256=sha(profile),
                  profile_summary=summary, profile_address=f"0x{profile_address:08x}",
                  base_compile_provenance=dict(name="base-provenance/entry.compile.json",
                      sha256=sha(record_bytes), record=record), artifacts=artifacts)
    return original, replacements, report


def validate_personalized(directory):
    """Read-only verification of every derivation against preserved base bytes.

    Expected offsets, byte changes, checksums and hashes are reconstructed from
    the original ELF/template/compile record. Audit JSON cannot widen a patch
    span or substitute hashes to excuse changes to instructions or other assets.
    """
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("Personalized proof must be an ordinary directory")
    for path in directory.rglob("*"):
        if path.is_symlink() or not (path.is_dir() or path.is_file()):
            raise ValueError("Personalized proof contains a symlink or special file")
    report = json.loads((directory / "personalization-audit.json").read_bytes())
    if not isinstance(report, dict):
        raise ValueError("Personalization audit must be an object")
    provenance = directory / "base-provenance"
    profile = (directory / "vmu-sd-profile.bin").read_bytes()
    _, replacements, expected = _transformation(provenance, profile)
    if set(report) != set(expected) | {"unchanged_artifacts"}:
        raise ValueError("Personalization audit has unexpected or missing fields")
    for key, value in expected.items():
        # JSON distinguishes bools from integers; compare canonical encodings
        # so True cannot pass as format=1 or a byte/count value.
        if json.dumps(report[key], sort_keys=True) != json.dumps(value, sort_keys=True):
            raise ValueError("Personalization audit differs from independent derivation: " + key)
    for name, value in replacements.items():
        if (directory / name).read_bytes() != value:
            raise ValueError("Personalized artifact differs from fixed-span derivation: " + name)
    unchanged = {}
    for path in sorted(provenance.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(provenance)
        name = str(relative)
        if (name in replacements or name.endswith(".compile.json") or
                name == "observer-artifact-audit.json"):
            continue
        original = path.read_bytes()
        if (directory / relative).read_bytes() != original:
            raise ValueError("Unchanged proof asset differs from its preserved base: " + name)
        unchanged[name] = sha(original)
    if report["unchanged_artifacts"] != unchanged:
        raise ValueError("Unchanged artifact manifest differs from preserved base assets")
    admitted = set(replacements) | set(unchanged) | {"personalization-audit.json"}
    for path in directory.rglob("*"):
        if not path.is_file() or path.relative_to(directory).parts[0] == "base-provenance":
            continue
        name = str(path.relative_to(directory))
        if name in admitted or name.endswith((".zip", ".dis")) or name == "observer-artifact-audit.json":
            continue
        raise ValueError("Unexpected file in personalized proof: " + name)
    return report


def personalize(base_directory, profile_path, output_directory, *, expected_commit=None):
    base = Path(base_directory).resolve()
    output = Path(output_directory).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Requires a new output directory")
    if base == output.resolve() or base in output.resolve().parents:
        raise ValueError("Personalization output must be outside the base directory")
    original, replacements, report = _transformation(base, Path(profile_path).read_bytes(),
                                                     expected_commit=expected_commit)
    # Retain untouched base bytes and their compiler record as evidence. The
    # personalized directory deliberately has no entry.compile.json claiming
    # the new profile was the native compiler input.
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".vmu-personalize-", dir=output.parent))
    try:
        shutil.copytree(base, temporary, dirs_exist_ok=True,
                        ignore=lambda directory, names: [name for name in names
                            if name.endswith((".zip", ".dis"))])
        provenance = temporary / "base-provenance"
        provenance.mkdir()
        # Preserve every retained base asset, including compiler stack/callgraph
        # reports, to let validation compare unchanged bytes independently of
        # the hashes claimed in the personalization JSON.
        for path in [path for path in temporary.rglob("*") if path.is_file()]:
            relative = path.relative_to(temporary)
            destination = provenance / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
        for path in list(temporary.rglob("*.compile.json")):
            if path.relative_to(temporary).parts[0] != "base-provenance":
                path.unlink()
        prior_audit = temporary / "observer-artifact-audit.json"
        if prior_audit.exists():
            prior_audit.unlink()
        for name, value in replacements.items():
            (temporary / name).write_bytes(value)
        report["unchanged_artifacts"] = {
            str(path.relative_to(temporary)): sha(path.read_bytes())
            for path in sorted(temporary.rglob("*")) if path.is_file() and
            path.relative_to(temporary).parts[0] != "base-provenance" and
            str(path.relative_to(temporary)) not in replacements}
        (temporary / "personalization-audit.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n")
        validate_personalized(temporary)
        os.rename(temporary, output)
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("profile", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--expected-commit", required=True)
    args = parser.parse_args()
    try:
        report = personalize(args.base, args.profile, args.output, expected_commit=args.expected_commit)
        print(json.dumps({key: report[key] for key in
            ("source_commit", "profile_sha256", "profile_address")}, indent=2))
    except (OSError, ValueError, struct.error, UnicodeError) as error:
        parser.exit(2, f"SD proof personalization refused: {error}\n")


if __name__ == "__main__":
    main()
