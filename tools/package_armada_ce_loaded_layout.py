#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the optional Armada CE loaded-layout test and exact b9aa rollback."""

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
ORIGINAL_BASELINE = Path("/workspace/scratch/7a5934db4038/ce-driver-fix-inputs/"
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
MODULE_BASELINE = Path("/workspace/scratch/7a5934db4038/ce-driver-fix-inputs/"
                       "K-UI-Armada-CE-Module-Fix.zip")
MODULE_COMMIT = "617ab22416df9f8d766fd6b5ec8d257f38d9f9ac"
MODULE_ZIP_BYTES = 43534818
MODULE_ZIP_SHA256 = "375f585a65c862a5e9326fa2f1a8c0a1863a9b2810abbd393ea782cc36ff29c7"
MODULE_CE_SHA256 = "f5b7305ca9c19ade54c07150dae7c840f6dce70207a25148579a86e13b22fdb8"
MODULE_SOURCE = "source/K-UI-Armada-CE-Module-Fix-617ab22416df.tar.gz"
MODULE_SOURCE_SHA256 = "df057d47db6effe7fe4677bd3959ee7f299be427a0c0d49d21a7e75b224454a8"
CACHE_BASELINE = DELIVERABLES / "K-UI-Armada-CE-Cache-Fix.zip"
CACHE_COMMIT = "b9aa5c70a431d9a4c724835917785b986e6ccdfa"
CACHE_ZIP_BYTES = 43604806
CACHE_ZIP_SHA256 = "cdfe519f007b891454f9292a87f8f0fccc7ee0565eed8b777e376fbfb47110e6"
CACHE_CE_SHA256 = "eb8e75679204ba8ebcbb550a25a794ddf8a9e85407817268a70d668e7d1f081d"
CACHE_SOURCE = "source/K-UI-Armada-CE-Cache-Fix-b9aa5c70a431.tar.gz"
CACHE_SOURCE_SHA256 = "68421186930f91528515a02decc5e9ebfa977d4f5c96496c443aeb56689450e9"
DELIVERY_LIMIT = 50 * 1024 * 1024

from package_armada_excerpt import (check, digest, inspect_seed, safe_member,
                                    verify_build, verify_members)
from retail_package import inspect_retail
from runtime_package import flatten_elf, verify


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_archive(commit):
    return gzip.compress(git("archive", "--format=tar",
                             "--prefix=K-UI-Armada-CE-Loaded-Layout-Fix/", commit), mtime=0)


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


def validate_module_baseline(path, files, previous):
    """Pin the 617 delivery, retain its source, and replace only the rollback."""
    data = path.read_bytes()
    check(len(data) == MODULE_ZIP_BYTES and digest(data) == MODULE_ZIP_SHA256,
          "Module ZIP does not match the exact preceding 617 delivery")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = verify_members(archive)
        module = json.loads(archive.read("build.json"))
        check(module["source_commit"] == MODULE_COMMIT and
              module["installed_files"] == [INSTALL_EXCERPT] and
              module["ce_audio_enabled"] is True and
              module["full_track_streaming"] is False and
              module["universal_ce_support"] is False and
              module["runtime_replaced"] is False and
              module["ordinary_ce_payload_replaced"] is False and
              module["native_mdk2_toy_payloads_replaced"] is False and
              module["retained_runtime_sha256"] == BASELINE_RUNTIME_SHA256 and
              module["retained_runtime_source_commit"] == BASELINE_COMMIT and
              module["baseline_zip_sha256"] == BASELINE_ZIP_SHA256 and
              module["restore_ce_source_commit"] == BASELINE_COMMIT and
              module["restore_ce_sha256"] == BASELINE_CE_SHA256,
              "Preceding module delivery source/runtime/scope metadata differs")
        for key in ("admission", "pool", "static_play_parameters",
                    "preserved_ce_source_commit", "preserved_ce_sha256",
                    "unchanged_loader_sha256"):
            check(module[key] == previous[key],
                  "Preceding module delivery changed retained behavior: " + key)
        restore = archive.read(INSTALL_EXCERPT)
        restore_info = inspect_retail(restore, ce=True)
        restore_info.pop("abi", None)
        check(len(restore) == 448484 and digest(restore) == MODULE_CE_SHA256 and
              module["ce_sha256"] == MODULE_CE_SHA256 and
              restore_info == module["ce"] and
              restore_info["build"] == MODULE_COMMIT[:12] and
              restore_info["crc32"] == "b85b9c5d" and
              inspect_seed(restore) == module["excerpt_placeholder"],
              "Rollback 617 loader differs from its exact verified bytes")
        check(archive.read("restore-ce-excerpt/" + INSTALL_EXCERPT) ==
              files["restore-ce-excerpt/" + INSTALL_EXCERPT],
              "The 617 delivery's original rollback differs")
        source = archive.read(MODULE_SOURCE)
        check(digest(source) == MODULE_SOURCE_SHA256 and
              module["source_archive_sha256"][MODULE_SOURCE] == MODULE_SOURCE_SHA256,
              "Preceding 617 source identity differs")
        # Avoid carrying a redundant 785 source tar in addition to the 617 tar,
        # current tar and complete Git history. The 785 bytes/modes were checked
        # against the recovered baseline; that exact tree remains in the bundle.
        check(archive.read(BASELINE_SOURCE) == files[BASELINE_SOURCE],
              "The 617 delivery's recovered original source differs")
        for name in files:
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                check(archive.read(name) == files[name],
                      "The 617 delivery's retained license/source differs: " + name)
        bundle = module["source_git_bundle"]
        check(bundle["member"] in members and
              digest(archive.read(bundle["member"])) == bundle["sha256"] and
              len(archive.read(bundle["member"])) == bundle["bytes"],
              "The 617 delivery's Git history identity differs")
        files["restore-ce-excerpt/" + INSTALL_EXCERPT] = restore
        del files[BASELINE_SOURCE]
        files[MODULE_SOURCE] = source
        files["evidence/previous-module/build.json"] = archive.read("build.json")
        files["evidence/previous-module/SHA256SUMS"] = archive.read("SHA256SUMS")
        files["evidence/previous-module/MEMBERS.json"] = archive.read("MEMBERS.json")
        files["source/baseline/package_armada_ce_module_fix.py"] = archive.read(
            "source/package_armada_ce_module_fix.py")
    return module, restore_info


def validate_cache_baseline(path, files, previous, module_previous):
    """Pin b9aa bytes and provenance, then retain its source and exact rollback."""
    data = path.read_bytes()
    check(len(data) == CACHE_ZIP_BYTES and digest(data) == CACHE_ZIP_SHA256,
          "Cache ZIP does not match the exact preceding b9aa delivery")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = verify_members(archive)
        cache = json.loads(archive.read("build.json"))
        check(cache["source_commit"] == CACHE_COMMIT and
              cache["installed_files"] == [INSTALL_EXCERPT] and
              cache["ce_audio_enabled"] is True and
              cache["full_track_streaming"] is False and
              cache["universal_ce_support"] is False and
              cache["runtime_replaced"] is False and
              cache["ordinary_ce_payload_replaced"] is False and
              cache["native_mdk2_toy_payloads_replaced"] is False and
              cache["retained_runtime_sha256"] == BASELINE_RUNTIME_SHA256 and
              cache["retained_runtime_source_commit"] == BASELINE_COMMIT and
              cache["baseline_zip_bytes"] == MODULE_ZIP_BYTES and
              cache["baseline_zip_sha256"] == MODULE_ZIP_SHA256 and
              cache["original_baseline_zip_bytes"] == BASELINE_ZIP_BYTES and
              cache["original_baseline_zip_sha256"] == BASELINE_ZIP_SHA256 and
              cache["restore_ce_source_commit"] == MODULE_COMMIT and
              cache["restore_ce_sha256"] == MODULE_CE_SHA256,
              "Preceding cache delivery source/runtime/scope metadata differs")
        for key in ("admission", "pool", "static_play_parameters",
                    "preserved_ce_source_commit", "preserved_ce_sha256",
                    "unchanged_loader_sha256"):
            check(cache[key] == previous[key],
                  "Preceding cache delivery changed retained behavior: " + key)
        check(cache["previous_module_binding_admission"] ==
              module_previous["binding_admission"],
              "Preceding cache delivery's module admission provenance differs")
        check([name for name in archive.namelist() if name.startswith("KUI/")] ==
              [INSTALL_EXCERPT], "Preceding cache ZIP includes another installed file")
        restore = archive.read(INSTALL_EXCERPT)
        restore_info = inspect_retail(restore, ce=True)
        restore_info.pop("abi", None)
        check(len(restore) == 449260 and digest(restore) == CACHE_CE_SHA256 and
              cache["ce_sha256"] == CACHE_CE_SHA256 and
              restore_info == cache["ce"] and
              restore_info["build"] == CACHE_COMMIT[:12] and
              restore_info["crc32"] == "7c9f0133" and
              inspect_seed(restore) == cache["excerpt_placeholder"],
              "Rollback b9aa loader differs from its exact verified bytes")
        check(archive.read("restore-ce-excerpt/" + INSTALL_EXCERPT) ==
              files["restore-ce-excerpt/" + INSTALL_EXCERPT],
              "The b9aa delivery's exact 617 rollback differs")
        source = archive.read(CACHE_SOURCE)
        check(digest(source) == CACHE_SOURCE_SHA256 and
              cache["source_archive_sha256"][CACHE_SOURCE] == CACHE_SOURCE_SHA256 and
              archive.read(MODULE_SOURCE) == files[MODULE_SOURCE],
              "Preceding cache delivery's source archive provenance differs")
        for name in files:
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                check(archive.read(name) == files[name],
                      "Preceding cache delivery's retained license/source differs: " + name)
        bundle = cache["source_git_bundle"]
        check(bundle["member"] in members and
              digest(archive.read(bundle["member"])) == bundle["sha256"] and
              len(archive.read(bundle["member"])) == bundle["bytes"],
              "Preceding cache delivery's Git history identity differs")
        files["restore-ce-excerpt/" + INSTALL_EXCERPT] = restore
        # Both old source archives were verified. Retain b9aa + current tar;
        # complete Git history preserves the earlier 785 and 617 trees too.
        del files[MODULE_SOURCE]
        files[CACHE_SOURCE] = source
        files["evidence/previous-cache/build.json"] = archive.read("build.json")
        files["evidence/previous-cache/SHA256SUMS"] = archive.read("SHA256SUMS")
        files["evidence/previous-cache/MEMBERS.json"] = archive.read("MEMBERS.json")
        files["source/baseline/package_armada_ce_cache_fix.py"] = archive.read(
            "source/package_armada_ce_cache_fix.py")
    return cache, restore_info


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
    subprocess.run(["git", "merge-base", "--is-ancestor", MODULE_COMMIT, args.commit],
                   cwd=ROOT, check=True)
    subprocess.run(["git", "merge-base", "--is-ancestor", CACHE_COMMIT, args.commit],
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
    files, previous, runtime_info, restore_info, original_source = validate_baseline(args.original_baseline)
    recovery = recovered_tree(original_source)
    module_previous, restore_info = validate_module_baseline(args.module_baseline, files, previous)
    cache_previous, restore_info = validate_cache_baseline(
        args.baseline, files, previous, module_previous)
    recovery["original_source_archive_included"] = False
    recovery["preceding_617_source_archive_included"] = False
    recovery["preceding_617_tree_retained_in_complete_git_bundle"] = True
    recovery["original_baseline_tree_retained_in_complete_git_bundle"] = True
    checks = corruption_checks()
    current_source = "source/K-UI-Armada-CE-Loaded-Layout-Fix-" + stamp + ".tar.gz"
    files.update({INSTALL_EXCERPT: candidate,
                  current_source: source_archive(args.commit),
                  "source/package_armada_ce_loaded_layout.py": Path(__file__).read_bytes(),
                  "docs/armada-ce-loaded-layout-test.md":
                      (ROOT / "docs/armada-ce-loaded-layout-test.md").read_bytes(),
                  "evidence/ce-compile.json": compile_data,
                  "evidence/source-recovery.json": (json.dumps(recovery, indent=2) + "\n").encode(),
                  "evidence/excerpt-layout-and-stack.json": (json.dumps(audit, indent=2) + "\n").encode()})
    # Retain the complete recovered history as well as current/preceding source.
    # This makes the pinned recovery ancestry available after a fresh clone,
    # without requiring a reconstruction of commit timestamps/identities.
    bundle_member = "source/K-UI-Armada-CE-Loaded-Layout-Fix-" + stamp + ".bundle"
    with tempfile.TemporaryDirectory(prefix="kui-loaded-layout-source-") as temporary:
        bundle = Path(temporary) / "source.bundle"
        subprocess.run(["git", "bundle", "create", str(bundle), "--all"],
                       cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["git", "bundle", "verify", str(bundle)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        files[bundle_member] = bundle.read_bytes()
    add_evidence(files, "armada-ce-module-backing-2026-10-14.json=" +
                 str(ROOT / "docs/evidence/armada-ce-module-backing-2026-10-14.json"))
    add_evidence(files, "armada-ce-loaded-layout-2026-10-14.json=" +
                 str(ROOT / "docs/evidence/armada-ce-loaded-layout-2026-10-14.json"))
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
            "scope": "Armada CE canonical loaded .data section sizes; optional loader only",
            "mapped_word_read_policy": cache_previous["mapped_word_read_policy"],
            "binding_admission": {
                "accepted_backing": ["audited ROM", "audited filesystem PE"],
                "disk_sound_modules": ["DSOUND", "SNDCORE"],
                "section_counts": {"disk_sound": 4, "rom_sound": 3, "rom_coredll": 4},
                "canonical_loaded_data_sizes": {
                    "DSOUND": {"disk": "0x4dc", "rom": "0x4c0"},
                    "SNDCORE": {"disk": "0x1208", "rom": "0x11ec"},
                    "COREDLL": {"rom": "0x1e0"}},
                "canonical_embedded_table_rvas": {
                    "DSOUND": "0x846c", "SNDCORE": "0x6198", "COREDLL": "0x2e170"},
                "canonical_module_base_plus_table_rva_required": True,
                "kernel_loader_metadata_checked_against_exact_kernel": True,
                "disk_or_rom_input_data_sizes_not_accepted_as_loaded_sizes": True,
                "immutable_crc_guards_per_disk_module": 3, "crc_guard_bytes": 32,
                "guards_repeated_before_publish": True,
                "zero_adjusted_section_allowed": "only fourth .reloc section with flags 0x42000040",
                "read_callback_limit": 4096, "module_limit": 64, "page_limit": 64,
                "existing_owner_object_com_cs_mapping_checks_retained": True,
                "ce_api_calls_before_admission": False,
                "prior_console_record_branch_identified": True},
            "installed_files": [INSTALL_EXCERPT], "runtime_replaced": False,
            "retained_runtime_source_commit": BASELINE_COMMIT,
            "retained_runtime": runtime_info, "retained_runtime_sha256": BASELINE_RUNTIME_SHA256,
            "restore_ce_source_commit": CACHE_COMMIT,
            "restore_ce": restore_info, "restore_ce_sha256": CACHE_CE_SHA256,
            "baseline_zip_bytes": CACHE_ZIP_BYTES, "baseline_zip_sha256": CACHE_ZIP_SHA256,
            "module_baseline_zip_bytes": MODULE_ZIP_BYTES,
            "module_baseline_zip_sha256": MODULE_ZIP_SHA256,
            "original_baseline_zip_bytes": BASELINE_ZIP_BYTES,
            "original_baseline_zip_sha256": BASELINE_ZIP_SHA256,
            "previous_module_binding_admission": module_previous["binding_admission"],
            "previous_cache_binding_admission": cache_previous["binding_admission"],
            "ordinary_ce_payload_replaced": False, "native_mdk2_toy_payloads_replaced": False,
            "preserved_ce_source_commit": previous["preserved_ce_source_commit"],
            "preserved_ce_sha256": previous["preserved_ce_sha256"],
            "unchanged_loader_sha256": previous["unchanged_loader_sha256"],
            "excerpt_placeholder": seed, "admission": previous["admission"],
            "pool": previous["pool"], "static_play_parameters": previous["static_play_parameters"],
            "layout_and_controller": audit,
            "prior_console_observation": {"source_commit": CACHE_COMMIT,
                "gd": "00000006", "bind": "00000004", "read": "000000cc",
                "queue": "00000000", "attempt": "00000000", "play": "00000000",
                "bind_why": "0000000e", "node": "00000000", "value": "00000000",
                "expect": "8cf4e5f4", "detail": "00000010", "audible_music": False,
                "interpretation": "MODULE missing after loaded-section shape filtering; no playback attempt"},
            "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                      if name.endswith(".tar.gz")},
            "build_commands": ["source .deps/kos/environ.sh",
                "make -j2 -f Makefile.armada-excerpt build/armada-ce-excerpt/entry.elf BUILD_ID=" + stamp,
                "python3 tools/runtime_package.py build build/armada-ce-excerpt/entry.elf "
                "build/armada-ce-excerpt/ce-cdda-armada.kui --build-id " + stamp,
                "python3 tools/check_armada_ce_excerpt.py build/armada-ce-excerpt",
                "python3 tools/package_armada_ce_loaded_layout.py --commit " + args.commit +
                " --directory build/armada-ce-excerpt --baseline " + str(args.baseline) +
                " --module-baseline " + str(args.module_baseline) +
                " --original-baseline " + str(args.original_baseline)],
            "verification": {"ce_envelope_crc_and_blank_card_manifest": True,
                "ce_package_matches_built_elf_and_clean_compile_record": True,
                "compiled_display_and_envelope_build_ids_match_clean_head": True,
                "exact_original_module_and_previous_zip_and_all_member_sha_crc_metadata": True,
                "rollback_matches_exact_previous_ce_candidate": True,
                "runtime_retained_and_absent_from_install_folder": True,
                "source_recovery_matches_all_archived_bytes_and_modes": True,
                "unique_aligned_seed_and_zero_pcm_placeholder": True,
                "packaging_corruption_checks": checks,
                "output_member_sizes_sha256_and_crc32_verified": True}}
    files["build.json"] = (json.dumps(info, indent=2) + "\n").encode()
    files["README.md"] = f"""# Armada CE CD music loaded-layout test

Optional excerpt loader build **{stamp}**. Keep menu build **{BASELINE_COMMIT[:12]}**
and its saved options. This ZIP installs only `{INSTALL_EXCERPT}`.

1. With the console off, back up that file, then merge this ZIP's `KUI` folder
   onto the SCI microSD card and cold boot.
2. Select **Original Armada → Y Options → CD music On → START**.
3. Listen for **30 seconds at the main menu**, then play for **one or two
   minutes**, checking guns and other effects. Expected success is a **two-second
   repeating music excerpt**; full-song streaming is still a later step.
4. Press **A+B+X+Y+Start** and photograph the whole result screen even if silent.
   Include **BIND WHY NODE VALUE EXPECT DETAIL**.

The previous test passed the earlier cache blocker, then failed to recognize
loaded sound modules. The audited CE loader extends their data sections with
its own small metadata area; the binder was comparing the original file sizes.
This update checks the exact loaded sizes while retaining module code identity,
cache, mapping, owner and native sound admission checks. It does not alter the
game, reset its sound driver, or change the other loaders. Host and build checks
are included; console playback is not yet confirmed.

No new extraction or upload is required. Set **CD music Off** for the ordinary
CE loader. For exact excerpt-loader rollback, merge `restore-ce-excerpt/KUI`
onto the card and cold boot; it contains byte-identical build **{CACHE_COMMIT[:12]}**.

Current and preceding b9aa source archives, complete recovered Git history,
dependency source/license files, evidence, and member SHA256/CRC checks are
included. Older 785 and 617 source archives are verified during packaging and
their exact trees retained in the Git bundle rather than redundant source tars.
No private executable, kernel, ARM driver or audio sample is distributed.
See `docs/armada-ce-loaded-layout-test.md`.
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
                      "restore_ce_build": CACHE_COMMIT[:12]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/armada-ce-excerpt")
    parser.add_argument("--baseline", type=Path, default=CACHE_BASELINE)
    parser.add_argument("--module-baseline", type=Path, default=MODULE_BASELINE)
    parser.add_argument("--original-baseline", type=Path, default=ORIGINAL_BASELINE)
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--output", type=Path, default=DELIVERABLES / "K-UI-Armada-CE-Loaded-Layout-Fix.zip")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile,
            KeyError, ImportError, json.JSONDecodeError, tarfile.TarError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
