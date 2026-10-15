#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package only the optional Armada CE excerpt loader and its exact rollback."""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import tarfile
import tempfile
import warnings
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
DELIVERABLES = Path("/workspace/scratch/7a5934db4038/deliverables")
BASELINE = Path("/workspace/scratch/7a5934db4038/ce-module-fix-inputs/"
                "K-UI-Armada-CE-CDDA-Excerpt-Test.zip")
INSTALL_EXCERPT = "KUI/apps/games/ce-cdda-armada.kui"
RUNTIME = "KUI/runtime.kui"
BASELINE_COMMIT = "785184b6db0542412ed5eed235c1ab73fcc8a610"
BASELINE_ZIP_BYTES = 36184517
BASELINE_ZIP_SHA256 = "e8004badcc7edaef13f789ade46749f9a12549acbf207ad161c7f0e1be27d442"
BASELINE_CE_SHA256 = "3ecec97e88fb196f34c83c3754a7599d8f916fbe5901bc5ec0b246dbb5097ec6"
BASELINE_RUNTIME_SHA256 = "b7767c73a88543806cd15a5a70551cdae0feac5c90312fd7267b8d17b6984444"
BASELINE_SOURCE = "source/K-UI-Armada-CE-Excerpt-785184b6db05.tar.gz"
BASELINE_SOURCE_SHA256 = "4ae1fa048efb14a9ea93ea9349a78f5fa817aba0e087ef2cf3a5b3fa58f7cc74"
RECOVERED_COMMIT = "c882641d19dd1da218615a4b4b78af8a65fd5cd6"
DEPENDENCIES = {
    "source/dependencies/fatfs-build-source.tar.gz":
        "65cf94ba386dc535f1f3255f260f3599ea401c856348f16a73705b5a4ccc35be",
    "source/dependencies/kos-fcfa7d869471-source.tar.gz":
        "9689add8572a096e4603dc360b46224b56213f5aba392645c79912cdf9f9e30a",
    "source/dependencies/newlib-4.6.0.20260123.tar.gz":
        "6ff27e3bf022666f43f7802255be680eeff722ac181b1725d21e2e8318604ee3",
}
DELIVERY_LIMIT = 50 * 1024 * 1024

from package_armada_excerpt import (check, digest, inspect_seed, safe_member,
                                    verify_build, verify_members)
from retail_package import inspect_retail
from runtime_package import flatten_elf, verify


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_archive(commit):
    return gzip.compress(git("archive", "--format=tar",
                             "--prefix=K-UI-Armada-CE-Module-Fix/", commit), mtime=0)


def recovered_tree(source):
    """Prove the new Git history starts with every archived 785 source byte."""
    entries = {}
    prefix = "K-UI-Armada-CE-Excerpt/"
    with tarfile.open(fileobj=io.BytesIO(source), mode="r:gz") as archive:
        for member in archive.getmembers():
            if member.isdir():
                continue
            check(member.isfile() and member.name.startswith(prefix),
                  "Recovered source has an unexpected non-file archive member")
            name = member.name[len(prefix):]
            check(safe_member(name) and name not in entries,
                  "Recovered source has an unsafe or duplicate path")
            check(member.size <= 16 * 1024 * 1024,
                  "Recovered source file exceeds its audit bound")
            entries[name] = ("100755" if member.mode & 0o111 else "100644",
                             archive.extractfile(member).read())
    tree = {}
    for record in git("ls-tree", "-r", "-z", RECOVERED_COMMIT).split(b"\0"):
        if not record:
            continue
        meta, path = record.split(b"\t", 1)
        mode, kind, object_id = meta.decode("ascii").split()
        check(kind == "blob", "Recovered Git tree contains a non-blob entry")
        name = path.decode("utf-8")
        check(safe_member(name) and name not in tree,
              "Recovered Git tree has an unsafe or duplicate path")
        tree[name] = (mode, object_id)
    check(set(tree) == set(entries),
          "Recovered Git tree paths differ from the pinned 785 source archive")
    # A single batch avoids one Git process for each of the 1,552 source files.
    ordered = sorted(tree)
    requested = b"".join(tree[name][1].encode("ascii") + b"\n" for name in ordered)
    result = subprocess.run(["git", "cat-file", "--batch"], cwd=ROOT,
                            input=requested, stdout=subprocess.PIPE, check=True).stdout
    position = 0
    for name in ordered:
        end = result.index(b"\n", position)
        object_id, kind, size = result[position:end].decode("ascii").split()
        length = int(size)
        position = end + 1
        data = result[position:position + length]
        position += length
        check(position < len(result) and result[position:position + 1] == b"\n",
              "Git batch object framing differs")
        position += 1
        check(object_id == tree[name][1] and kind == "blob" and
              (tree[name][0], data) == entries[name],
              "Recovered Git source byte or mode differs: " + name)
    check(position == len(result), "Unexpected trailing Git source object data")
    return {"original_source_commit": BASELINE_COMMIT,
            "original_source_archive": BASELINE_SOURCE,
            "original_source_archive_sha256": BASELINE_SOURCE_SHA256,
            "recovered_baseline_commit": RECOVERED_COMMIT,
            "recovered_baseline_tree": git("rev-parse", RECOVERED_COMMIT + "^{tree}").decode().strip(),
            "all_archived_file_bytes_and_modes_match": True,
            "source_file_count": len(entries)}


def validate_baseline(path):
    data = path.read_bytes()
    check(len(data) == BASELINE_ZIP_BYTES and digest(data) == BASELINE_ZIP_SHA256,
          "Baseline ZIP does not match the exact previous excerpt delivery")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = verify_members(archive)
        previous = json.loads(archive.read("build.json"))
        check(previous["source_commit"] == BASELINE_COMMIT and
              previous["runtime_sha256"] == BASELINE_RUNTIME_SHA256 and
              previous["ce_sha256"] == BASELINE_CE_SHA256 and
              previous["installed_files"] == [RUNTIME, INSTALL_EXCERPT] and
              previous["ce_audio_enabled"] is True and
              previous["full_track_streaming"] is False,
              "Previous excerpt source/build/scope metadata differs")
        runtime = archive.read(RUNTIME)
        runtime_info = verify(runtime)
        check(len(runtime) == 1782068 and digest(runtime) == BASELINE_RUNTIME_SHA256 and
              runtime_info == {"format": 1, "build": BASELINE_COMMIT[:12],
                               "payload_bytes": 1782004, "memory_bytes": 5679248,
                               "crc32": "f07f3227"} and runtime_info == previous["runtime"],
              "Retained menu runtime differs from the exact installed 785 build")
        restore = archive.read(INSTALL_EXCERPT)
        restore_info = inspect_retail(restore, ce=True)
        restore_info.pop("abi", None)
        check(len(restore) == 447364 and digest(restore) == BASELINE_CE_SHA256 and
              restore_info == previous["ce"] and
              restore_info["build"] == BASELINE_COMMIT[:12] and
              restore_info["crc32"] == "01ea7543",
              "Rollback CE excerpt loader differs from its exact previous bytes")
        check(inspect_seed(restore) == previous["excerpt_placeholder"],
              "Rollback excerpt placeholder differs")
        source = archive.read(BASELINE_SOURCE)
        check(digest(source) == BASELINE_SOURCE_SHA256 and
              previous["source_archive_sha256"][BASELINE_SOURCE] == BASELINE_SOURCE_SHA256,
              "Original 785 source archive provenance differs")
        files = {"restore-ce-excerpt/" + INSTALL_EXCERPT: restore,
                 BASELINE_SOURCE: source,
                 "evidence/baseline/build.json": archive.read("build.json"),
                 "evidence/baseline/SHA256SUMS": archive.read("SHA256SUMS"),
                 "evidence/baseline/MEMBERS.json": archive.read("MEMBERS.json"),
                 "source/baseline/package_armada_excerpt.py":
                     archive.read("source/package_armada_excerpt.py")}
        inherited = set()
        for name in archive.namelist():
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                check(name not in files, "Duplicate inherited package member")
                value = archive.read(name)
                if name.startswith("source/dependencies/"):
                    check(name in DEPENDENCIES and digest(value) == DEPENDENCIES[name] and
                          previous["source_archive_sha256"][name] == members[name],
                          "Dependency source identity differs: " + name)
                    inherited.add(name)
                files[name] = value
        check(inherited == set(DEPENDENCIES) and "LICENSE" in files and
              "THIRD_PARTY.md" in files, "Baseline licenses/dependency sources are incomplete")
    return files, previous, runtime_info, restore_info, source


def add_evidence(files, specification):
    check("=" in specification, "Evidence must have the form NAME=PATH")
    name, source = specification.split("=", 1)
    check(safe_member(name), "Invalid evidence member name")
    member = "evidence/" + name
    check(member not in files, "Duplicate evidence member: " + member)
    path = Path(source)
    check(0 < path.stat().st_size <= 4 * 1024 * 1024 and
          path.suffix.lower() in (".json", ".log", ".txt", ".md"),
          "Evidence must be at most 4 MiB of JSON or text")
    files[member] = path.read_bytes()


def corruption_checks():
    """Exercise member traversal/identity and seed corruption rejection in memory."""
    checks = 0

    def rejects(operation, message):
        nonlocal checks
        try:
            operation()
        except (ValueError, KeyError, zipfile.BadZipFile):
            checks += 1
            return
        raise ValueError("Packaging corruption check did not reject " + message)

    def archive_bytes(names, manifest):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as archive:
            for name, data in names:
                archive.writestr(name, data)
            archive.writestr("SHA256SUMS", manifest)
        return buffer.getvalue()

    def members(data):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            return verify_members(archive)

    good = archive_bytes([("proof.txt", b"good")], digest(b"good") + "  proof.txt\n")
    check(members(good) == {"proof.txt": digest(b"good")},
          "Packaging positive member control failed")
    checks += 1
    rejects(lambda: members(archive_bytes([("proof.txt", b"bad")],
                                          digest(b"good") + "  proof.txt\n")), "member byte corruption")
    corrupt_zip = bytearray(good)
    with zipfile.ZipFile(io.BytesIO(good)) as archive:
        entry = archive.getinfo("proof.txt")
        name_bytes, extra_bytes = struct.unpack_from("<HH", good, entry.header_offset + 26)
        content_offset = entry.header_offset + 30 + name_bytes + extra_bytes
    corrupt_zip[content_offset] ^= 1
    rejects(lambda: members(bytes(corrupt_zip)), "member CRC corruption")
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        repeated = archive_bytes([("proof.txt", b"good"), ("proof.txt", b"good")],
                                 digest(b"good") + "  proof.txt\n")
    rejects(lambda: members(repeated), "duplicate ZIP member name")
    rejects(lambda: members(archive_bytes([("../proof.txt", b"good")],
                                          digest(b"good") + "  ../proof.txt\n")), "path traversal")
    rejects(lambda: members(archive_bytes([("proof.txt", b"good")], "")), "incomplete SHA coverage")
    rejects(lambda: members(archive_bytes([("proof.txt", b"good")],
                                          digest(b"good") + "  proof.txt\n" +
                                          digest(b"good") + "  proof.txt\n")), "duplicate SHA entry")
    records = {"proof.txt": {"bytes": 5, "sha256": digest(b"good"), "crc32": "00000000"}}
    metadata = json.dumps(records).encode()
    rejects(lambda: members(archive_bytes([("proof.txt", b"good"), ("MEMBERS.json", metadata)],
                       digest(b"good") + "  proof.txt\n" + digest(metadata) + "  MEMBERS.json\n")),
            "member size/CRC metadata corruption")
    from package_armada_excerpt import PCM_BYTES, SEED, SEED_BYTES
    from retail_package import STAGE_BLOB_OFFSET
    candidate = bytearray(64 + STAGE_BLOB_OFFSET + SEED_BYTES + PCM_BYTES)
    offset = 64 + STAGE_BLOB_OFFSET
    candidate[offset:offset + SEED_BYTES] = SEED
    check(inspect_seed(bytes(candidate))["all_zero"], "Packaging positive seed control failed")
    checks += 1
    corrupt = candidate.copy()
    corrupt[-1] = 1
    rejects(lambda: inspect_seed(bytes(corrupt)), "private/nonzero PCM placeholder")
    corrupt = candidate.copy()
    struct.pack_into("<I", corrupt, offset + 12, 1)
    rejects(lambda: inspect_seed(bytes(corrupt)), "nonzero seed CRC")
    rejects(lambda: inspect_seed(bytes(candidate[:-1])), "truncated PCM placeholder")
    duplicate = candidate + candidate[offset:]
    rejects(lambda: inspect_seed(bytes(duplicate)), "ambiguous excerpt seed")
    return checks


def build(args):
    check(re.fullmatch(r"[0-9a-f]{40}", args.commit) is not None,
          "--commit requires a full lowercase commit hash")
    check(git("rev-parse", "HEAD").decode().strip() == args.commit and
          not git("status", "--porcelain"), "Commit all final source changes before packaging")
    subprocess.run(["git", "merge-base", "--is-ancestor", RECOVERED_COMMIT, args.commit],
                   cwd=ROOT, check=True)
    stamp = args.commit[:12]
    directory = args.directory.resolve()
    elf = directory / "entry.elf"
    package = directory / "ce-cdda-armada.kui"
    compile_record, compile_data = verify_build(elf, args.commit)
    check(package.stat().st_size <= 4 * 1024 * 1024 + 64,
          "CE excerpt package exceeds its envelope size limit")
    candidate = package.read_bytes()
    ce_info = inspect_retail(candidate, ce=True)
    ce_info.pop("abi", None)
    flattened, memory = flatten_elf(elf.read_bytes())
    check(ce_info["build"] == stamp and stamp.encode() + b"\0" in candidate[64:] and
          candidate[64:] == flattened and ce_info["memory_bytes"] == memory,
          "CE package, built ELF, compiled display and commit IDs differ")
    seed = inspect_seed(candidate)
    from check_armada_ce_excerpt import check as check_excerpt
    audit = check_excerpt(directory)
    files, previous, runtime_info, restore_info, original_source = validate_baseline(args.baseline)
    recovery = recovered_tree(original_source)
    checks = corruption_checks()
    current_source = "source/K-UI-Armada-CE-Module-Fix-" + stamp + ".tar.gz"
    files.update({INSTALL_EXCERPT: candidate,
                  current_source: source_archive(args.commit),
                  "source/package_armada_ce_module_fix.py": Path(__file__).read_bytes(),
                  "docs/armada-ce-module-fix-test.md":
                      (ROOT / "docs/armada-ce-module-fix-test.md").read_bytes(),
                  "evidence/ce-compile.json": compile_data,
                  "evidence/source-recovery.json": (json.dumps(recovery, indent=2) + "\n").encode(),
                  "evidence/excerpt-layout-and-stack.json": (json.dumps(audit, indent=2) + "\n").encode()})
    # Retain the two-commit recovered history as well as ordinary source.
    # This makes the pinned recovery ancestry available after a fresh clone,
    # without requiring a reconstruction of commit timestamps/identities.
    bundle_member = "source/K-UI-Armada-CE-Module-Fix-" + stamp + ".bundle"
    with tempfile.TemporaryDirectory(prefix="kui-module-source-") as temporary:
        bundle = Path(temporary) / "source.bundle"
        subprocess.run(["git", "bundle", "create", str(bundle), "--all"],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["git", "bundle", "verify", str(bundle)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        files[bundle_member] = bundle.read_bytes()
    add_evidence(files, "armada-ce-module-backing-2026-10-14.json=" +
                 str(ROOT / "docs/evidence/armada-ce-module-backing-2026-10-14.json"))
    for specification in args.evidence:
        add_evidence(files, specification)
    info = {"source_commit": args.commit, "source_recovery": recovery,
            "source_git_bundle": {"member": bundle_member,
                                  "sha256": digest(files[bundle_member]),
                                  "bytes": len(files[bundle_member])},
            "ce": ce_info, "ce_sha256": digest(candidate), "ce_compile": compile_record,
            "hardware_tested": False, "ce_hardware_tested": False,
            "ce_audio_enabled": True, "full_track_streaming": False,
            "universal_ce_support": False,
            "ce_audio_mode": "conditional two-second loop through the verified CE sound owner",
            "scope": "Armada CE disk-backed sound-module admission and refusal witness; optional loader only",
            "binding_admission": {"accepted_backing": ["audited ROM", "audited filesystem PE"],
                "disk_sound_modules": ["DSOUND", "SNDCORE"],
                "exact_section_count": 4, "immutable_crc_guards_per_disk_module": 3,
                "crc_guard_bytes": 32, "guards_repeated_before_publish": True,
                "zero_adjusted_section_allowed": "only fourth .reloc section with flags 0x42000040",
                "read_callback_limit": 4096, "module_limit": 64, "page_limit": 64,
                "existing_owner_object_com_cs_mapping_checks_retained": True,
                "ce_api_calls_before_admission": False,
                "prior_console_record_branch_identified": False},
            "installed_files": [INSTALL_EXCERPT], "runtime_replaced": False,
            "retained_runtime_source_commit": BASELINE_COMMIT,
            "retained_runtime": runtime_info, "retained_runtime_sha256": BASELINE_RUNTIME_SHA256,
            "restore_ce_source_commit": BASELINE_COMMIT,
            "restore_ce": restore_info, "restore_ce_sha256": BASELINE_CE_SHA256,
            "baseline_zip_bytes": BASELINE_ZIP_BYTES, "baseline_zip_sha256": BASELINE_ZIP_SHA256,
            "ordinary_ce_payload_replaced": False, "native_mdk2_toy_payloads_replaced": False,
            "preserved_ce_source_commit": previous["preserved_ce_source_commit"],
            "preserved_ce_sha256": previous["preserved_ce_sha256"],
            "unchanged_loader_sha256": previous["unchanged_loader_sha256"],
            "excerpt_placeholder": seed, "admission": previous["admission"],
            "pool": previous["pool"], "static_play_parameters": previous["static_play_parameters"],
            "layout_and_controller": audit,
            "prior_console_observation": {"gd": "00000002", "bind": "00000004",
                "read": "0000004a", "queue": "00000000", "attempt": "00000000",
                "play": "00000000", "audible_music": False,
                "interpretation": "MODULE binding refused before any queued native playback attempt"},
            "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                      if name.endswith(".tar.gz")},
            "build_commands": ["source .deps/kos/environ.sh",
                "make -j2 -f Makefile.armada-excerpt build/armada-ce-excerpt/entry.elf BUILD_ID=" + stamp,
                "python3 tools/runtime_package.py build build/armada-ce-excerpt/entry.elf "
                "build/armada-ce-excerpt/ce-cdda-armada.kui --build-id " + stamp,
                "python3 tools/check_armada_ce_excerpt.py build/armada-ce-excerpt",
                "python3 tools/package_armada_ce_module_fix.py --commit " + args.commit +
                " --directory build/armada-ce-excerpt --baseline " + str(args.baseline)],
            "verification": {"ce_envelope_crc_and_blank_card_manifest": True,
                "ce_package_matches_built_elf_and_clean_compile_record": True,
                "compiled_display_and_envelope_build_ids_match_clean_head": True,
                "exact_baseline_zip_and_all_member_sha_crc_metadata": True,
                "rollback_matches_exact_previous_ce_candidate": True,
                "runtime_retained_and_absent_from_install_folder": True,
                "source_recovery_matches_all_archived_bytes_and_modes": True,
                "unique_aligned_seed_and_zero_pcm_placeholder": True,
                "packaging_corruption_checks": checks,
                "output_member_sizes_sha256_and_crc32_verified": True}}
    files["build.json"] = (json.dumps(info, indent=2) + "\n").encode()
    files["README.md"] = f"""# Armada CE CD music module-binding test

Optional excerpt loader build **{stamp}**. Keep menu build **{BASELINE_COMMIT[:12]}**
and its saved options. This ZIP installs only `{INSTALL_EXCERPT}`.

1. With the console off, back up that file, then merge this ZIP's `KUI` folder
   onto the SCI microSD card and cold boot.
2. Select **Original Armada → Y Options → CD music On → START**. The saved On
   choice continues to select this separate excerpt loader.
3. Listen for **30 seconds at the main menu**, then play for **one or two
   minutes**, checking guns and other effects. Expected success is a **two-second
   repeating music excerpt**; full-song streaming is still a later step.
4. Press **A+B+X+Y+Start** and photograph the whole result screen even if silent.
   Include the new **BIND WHY NODE VALUE EXPECT DETAIL** row.

Your prior result recorded a MODULE binding refusal and zero playback attempts.
The earlier binder admitted only ROM-backed sound DLLs. This adds admission for
the exact audited disk-backed DLLs and keeps the sound-owner and mapping checks.
That gap is consistent with the refusal; the prior screen did not identify its
failing record. The new witness row will. Build checks passed; console playback
is not yet confirmed.
No new extraction or upload is required. The menu and other loader files stay
as installed. Set **CD music Off** for the ordinary CE loader.

For exact excerpt-loader rollback, merge `restore-ce-excerpt/KUI` onto the card
and cold boot; it contains byte-identical build **{BASELINE_COMMIT[:12]}**.
The pinned original source archive, its verified recovered Git tree identity,
new source, dependency source/license files, build evidence, member SHA256 and
CRC checks are included. No private executable, kernel, ARM driver or audio
sample is distributed. See `docs/armada-ce-module-fix-test.md`.
""".encode()
    check([name for name in files if name.startswith("KUI/")] == [INSTALL_EXCERPT],
          "Only the optional CE excerpt loader may appear under KUI")
    files["MEMBERS.json"] = (json.dumps({name: {"bytes": len(data), "sha256": digest(data),
                                               "crc32": f"{zlib.crc32(data):08x}"}
                                        for name, data in sorted(files.items())}, indent=2) + "\n").encode()
    files["SHA256SUMS"] = "".join(digest(data) + "  " + name + "\n"
                                    for name, data in sorted(files.items())).encode()
    check(not git("status", "--porcelain") and
          git("rev-parse", "HEAD").decode().strip() == args.commit,
          "Source changed while packaging")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=args.output.name + ".", suffix=".partial",
                                         dir=args.output.parent, delete=False) as handle:
            temporary = Path(handle.name)
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=False) as archive:
            for name, data in sorted(files.items()):
                member = zipfile.ZipInfo(name, (2026, 10, 14, 0, 0, 0))
                member.external_attr = 0o100644 << 16
                member.compress_type = zipfile.ZIP_STORED if name.endswith(".tar.gz") else zipfile.ZIP_DEFLATED
                archive.writestr(member, data)
        with zipfile.ZipFile(temporary) as archive:
            verify_members(archive)
            check(len(archive.infolist()) == len(files), "Output ZIP member count differs")
            for name, data in files.items():
                check(archive.read(name) == data, "Output member differs: " + name)
        check(temporary.stat().st_size < DELIVERY_LIMIT, "Output ZIP exceeds 50 MiB")
        temporary.replace(args.output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(json.dumps({"path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
                      "entries": len(files), "sha256": digest(args.output.read_bytes()),
                      "source_commit": args.commit, "ce_excerpt_build": stamp,
                      "retained_runtime_build": BASELINE_COMMIT[:12],
                      "restore_ce_build": BASELINE_COMMIT[:12]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/armada-ce-excerpt")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--output", type=Path, default=DELIVERABLES / "K-UI-Armada-CE-Module-Fix.zip")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile,
            KeyError, ImportError, json.JSONDecodeError, tarfile.TarError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
