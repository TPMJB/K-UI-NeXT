#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the Armada CE native-loop alignment test with exact bd6 rollbacks."""

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
LAYOUT_BASELINE = DELIVERABLES / "K-UI-Armada-CE-Loaded-Layout-Fix.zip"
LAYOUT_COMMIT = "1524aa62a7304b95fadde5352b974630ff76e525"
LAYOUT_ZIP_BYTES = 43674453
LAYOUT_ZIP_SHA256 = "144cd598f9e13feee09f6eac9f67bb89fb5e1fd30d1ee93b477418d6ddbee2ce"
LAYOUT_CE_SHA256 = "fa8b1fc76e5bc1a25ef5522e1080df20704ee5d4b5fe3a3f4350d836f553a961"
LAYOUT_SOURCE = "source/K-UI-Armada-CE-Loaded-Layout-Fix-1524aa62a730.tar.gz"
LAYOUT_SOURCE_SHA256 = "0a810cba64d17e5a1040ff30e22e619e609682d9d02d52f901b9d04f98bcd19f"
HANDOFF_BASELINE = DELIVERABLES / "K-UI-Armada-CE-Audio-Handoff-Fix.zip"
HANDOFF_COMMIT = "bd6b94787375232a2f7fa691140a9e144c8f65db"
HANDOFF_ZIP_BYTES = 45565250
HANDOFF_ZIP_SHA256 = "4b4d1f3741caa763215a6c17ea99673e40092dac1b600b379bafc8876fdc8e90"
HANDOFF_CE_SHA256 = "dce1bf4d0e1e66ee217ecfe54260153e76ebac33fdc0c7b207a629cdc7420376"
HANDOFF_RUNTIME_SHA256 = "6b088925ef5e2f3fc332634b3d54776b9089a7e95c32667d6a71af9b227ad8bb"
HANDOFF_SOURCE = "source/K-UI-Armada-CE-Audio-Handoff-Fix-bd6b94787375.tar.gz"
HANDOFF_SOURCE_SHA256 = "62aa70bbb6906a062e0102c9dfb83ffbeb2114e80fe81fcada6d7ca8d2b8554c"
RESTORE_PREFIX = "restore-loop-alignment/"
DELIVERY_LIMIT = 50 * 1024 * 1024

from package_armada_excerpt import (check, digest, inspect_seed, safe_member,
                                    verify_build, verify_members)
from retail_package import inspect_retail
from runtime_package import flatten_elf, verify


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_archive(commit):
    return gzip.compress(git("archive", "--format=tar",
                             "--prefix=K-UI-Armada-CE-Loop-Alignment-Fix/", commit), mtime=0)


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


def validate_layout_baseline(path, files, previous, module_previous, cache_previous,
                             original_baseline):
    """Pin 1524 optional bytes and original 785 menu for a combined rollback."""
    data = path.read_bytes()
    check(len(data) == LAYOUT_ZIP_BYTES and digest(data) == LAYOUT_ZIP_SHA256,
          "Loaded-layout ZIP differs from the exact preceding 1524 delivery")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = verify_members(archive)
        layout = json.loads(archive.read("build.json"))
        check(layout["source_commit"] == LAYOUT_COMMIT and
              layout["installed_files"] == [INSTALL_EXCERPT] and
              layout["ce_audio_enabled"] is True and
              layout["full_track_streaming"] is False and
              layout["universal_ce_support"] is False and
              layout["runtime_replaced"] is False and
              layout["ordinary_ce_payload_replaced"] is False and
              layout["native_mdk2_toy_payloads_replaced"] is False and
              layout["retained_runtime_sha256"] == BASELINE_RUNTIME_SHA256 and
              layout["retained_runtime_source_commit"] == BASELINE_COMMIT and
              layout["baseline_zip_bytes"] == CACHE_ZIP_BYTES and
              layout["baseline_zip_sha256"] == CACHE_ZIP_SHA256 and
              layout["module_baseline_zip_bytes"] == MODULE_ZIP_BYTES and
              layout["module_baseline_zip_sha256"] == MODULE_ZIP_SHA256 and
              layout["original_baseline_zip_bytes"] == BASELINE_ZIP_BYTES and
              layout["original_baseline_zip_sha256"] == BASELINE_ZIP_SHA256 and
              layout["restore_ce_source_commit"] == CACHE_COMMIT and
              layout["restore_ce_sha256"] == CACHE_CE_SHA256,
              "Loaded-layout delivery source/runtime/scope metadata differs")
        for key in ("admission", "pool", "static_play_parameters",
                    "preserved_ce_source_commit", "preserved_ce_sha256",
                    "unchanged_loader_sha256"):
            check(layout[key] == previous[key],
                  "Loaded-layout delivery changed retained behavior: " + key)
        check(layout["previous_module_binding_admission"] ==
              module_previous["binding_admission"] and
              layout["previous_cache_binding_admission"] ==
              cache_previous["binding_admission"] and
              layout["mapped_word_read_policy"] == cache_previous["mapped_word_read_policy"],
              "Loaded-layout delivery's preceding binder/read provenance differs")
        check([name for name in archive.namelist() if name.startswith("KUI/")] ==
              [INSTALL_EXCERPT], "Loaded-layout ZIP includes another installed file")
        restore = archive.read(INSTALL_EXCERPT)
        restore_info = inspect_retail(restore, ce=True)
        restore_info.pop("abi", None)
        check(len(restore) == 449264 and digest(restore) == LAYOUT_CE_SHA256 and
              layout["ce_sha256"] == LAYOUT_CE_SHA256 and
              restore_info == layout["ce"] and
              restore_info["build"] == LAYOUT_COMMIT[:12] and
              restore_info["crc32"] == "4de14567" and
              inspect_seed(restore) == layout["excerpt_placeholder"],
              "Rollback 1524 loader differs from its exact verified bytes")
        check(archive.read("restore-ce-excerpt/" + INSTALL_EXCERPT) ==
              files["restore-ce-excerpt/" + INSTALL_EXCERPT],
              "Loaded-layout delivery's exact b9aa rollback differs")
        source = archive.read(LAYOUT_SOURCE)
        check(digest(source) == LAYOUT_SOURCE_SHA256 and
              layout["source_archive_sha256"][LAYOUT_SOURCE] == LAYOUT_SOURCE_SHA256 and
              archive.read(CACHE_SOURCE) == files[CACHE_SOURCE],
              "Loaded-layout delivery's source archive provenance differs")
        for name in files:
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                check(archive.read(name) == files[name],
                      "Loaded-layout delivery's retained license/source differs: " + name)
        bundle = layout["source_git_bundle"]
        check(bundle["member"] in members and
              digest(archive.read(bundle["member"])) == bundle["sha256"] and
              len(archive.read(bundle["member"])) == bundle["bytes"],
              "Loaded-layout delivery's Git history identity differs")
        del files["restore-ce-excerpt/" + INSTALL_EXCERPT]
        files[RESTORE_PREFIX + INSTALL_EXCERPT] = restore
        del files[CACHE_SOURCE]
        files[LAYOUT_SOURCE] = source
        files["evidence/previous-loaded-layout/build.json"] = archive.read("build.json")
        files["evidence/previous-loaded-layout/SHA256SUMS"] = archive.read("SHA256SUMS")
        files["evidence/previous-loaded-layout/MEMBERS.json"] = archive.read("MEMBERS.json")
        files["source/baseline/package_armada_ce_loaded_layout.py"] = archive.read(
            "source/package_armada_ce_loaded_layout.py")
    # The original ZIP has already passed its exact size/SHA/member checks.
    # Repeat the menu byte identity when adding it to the combined rollback.
    with zipfile.ZipFile(original_baseline) as archive:
        runtime = archive.read(RUNTIME)
    check(len(runtime) == 1782068 and digest(runtime) == BASELINE_RUNTIME_SHA256 and
          verify(runtime) == layout["retained_runtime"],
          "Rollback 785 menu differs from its exact retained bytes")
    files[RESTORE_PREFIX + RUNTIME] = runtime
    return layout, restore_info


def validate_handoff_baseline(path, files, previous, layout_previous):
    """Pin bd6 menu/loader and source; update the common rollback together."""
    data = path.read_bytes()
    check(len(data) == HANDOFF_ZIP_BYTES and digest(data) == HANDOFF_ZIP_SHA256,
          "Audio-handoff ZIP differs from the exact preceding bd6 delivery")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = verify_members(archive)
        handoff = json.loads(archive.read("build.json"))
        check(handoff["source_commit"] == HANDOFF_COMMIT and
              handoff["installed_files"] == [RUNTIME, INSTALL_EXCERPT] and
              handoff["ce_audio_enabled"] is True and
              handoff["full_track_streaming"] is False and
              handoff["universal_ce_support"] is False and
              handoff["runtime_replaced"] is True and
              handoff["ordinary_ce_payload_replaced"] is False and
              handoff["native_mdk2_toy_payloads_replaced"] is False and
              handoff["baseline_zip_bytes"] == LAYOUT_ZIP_BYTES and
              handoff["baseline_zip_sha256"] == LAYOUT_ZIP_SHA256 and
              handoff["cache_baseline_zip_bytes"] == CACHE_ZIP_BYTES and
              handoff["cache_baseline_zip_sha256"] == CACHE_ZIP_SHA256 and
              handoff["module_baseline_zip_bytes"] == MODULE_ZIP_BYTES and
              handoff["module_baseline_zip_sha256"] == MODULE_ZIP_SHA256 and
              handoff["original_baseline_zip_bytes"] == BASELINE_ZIP_BYTES and
              handoff["original_baseline_zip_sha256"] == BASELINE_ZIP_SHA256 and
              handoff["restore_ce_source_commit"] == LAYOUT_COMMIT and
              handoff["restore_ce_sha256"] == LAYOUT_CE_SHA256 and
              handoff["restore_runtime_source_commit"] == BASELINE_COMMIT and
              handoff["restore_runtime_sha256"] == BASELINE_RUNTIME_SHA256,
              "Audio-handoff delivery source/runtime/scope metadata differs")
        for key in ("admission", "pool", "static_play_parameters",
                    "preserved_ce_source_commit", "preserved_ce_sha256",
                    "unchanged_loader_sha256"):
            check(handoff[key] == previous[key],
                  "Audio-handoff delivery changed retained behavior: " + key)
        check(handoff["binding_admission"] == layout_previous["binding_admission"] and
              handoff["mapped_word_read_policy"] == layout_previous["mapped_word_read_policy"] and
              handoff["previous_loaded_layout_source_commit"] == LAYOUT_COMMIT and
              handoff["previous_loaded_layout_binding_admission"] ==
              layout_previous["binding_admission"],
              "Audio-handoff delivery's previous binder/read provenance differs")
        check(sorted(name for name in archive.namelist() if name.startswith("KUI/")) ==
              sorted([RUNTIME, INSTALL_EXCERPT]), "Audio-handoff ZIP installs unexpected files")
        restore = archive.read(INSTALL_EXCERPT)
        restore_info = inspect_retail(restore, ce=True)
        restore_info.pop("abi", None)
        check(len(restore) == 449800 and digest(restore) == HANDOFF_CE_SHA256 and
              handoff["ce_sha256"] == HANDOFF_CE_SHA256 and
              restore_info == handoff["ce"] and
              restore_info["build"] == HANDOFF_COMMIT[:12] and
              restore_info["crc32"] == "9067258e" and
              inspect_seed(restore) == handoff["excerpt_placeholder"],
              "Rollback bd6 loader differs from its exact verified bytes")
        restore_runtime = archive.read(RUNTIME)
        restore_runtime_info = verify(restore_runtime)
        check(len(restore_runtime) == 1782088 and
              digest(restore_runtime) == HANDOFF_RUNTIME_SHA256 and
              handoff["runtime_sha256"] == HANDOFF_RUNTIME_SHA256 and
              restore_runtime_info == handoff["runtime"] and
              restore_runtime_info["build"] == HANDOFF_COMMIT[:12] and
              restore_runtime_info["crc32"] == "a52a5086" and
              b"L Memory" not in restore_runtime[64:],
              "Rollback bd6 menu differs from its exact verified bytes")
        for name in (RUNTIME, INSTALL_EXCERPT):
            check(archive.read("restore-audio-handoff/" + name) ==
                  files[RESTORE_PREFIX + name],
                  "Audio-handoff delivery's earlier combined rollback differs: " + name)
        source = archive.read(HANDOFF_SOURCE)
        check(digest(source) == HANDOFF_SOURCE_SHA256 and
              handoff["source_archive_sha256"][HANDOFF_SOURCE] == HANDOFF_SOURCE_SHA256 and
              archive.read(LAYOUT_SOURCE) == files[LAYOUT_SOURCE],
              "Audio-handoff delivery's source archive provenance differs")
        for name in files:
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                check(archive.read(name) == files[name],
                      "Audio-handoff delivery's retained license/source differs: " + name)
        bundle = handoff["source_git_bundle"]
        check(bundle["member"] in members and
              digest(archive.read(bundle["member"])) == bundle["sha256"] and
              len(archive.read(bundle["member"])) == bundle["bytes"],
              "Audio-handoff delivery's Git history identity differs")
        files[RESTORE_PREFIX + INSTALL_EXCERPT] = restore
        files[RESTORE_PREFIX + RUNTIME] = restore_runtime
        del files[LAYOUT_SOURCE]
        files[HANDOFF_SOURCE] = source
        files["evidence/previous-audio-handoff/build.json"] = archive.read("build.json")
        files["evidence/previous-audio-handoff/SHA256SUMS"] = archive.read("SHA256SUMS")
        files["evidence/previous-audio-handoff/MEMBERS.json"] = archive.read("MEMBERS.json")
        files["source/baseline/package_armada_ce_audio_handoff.py"] = archive.read(
            "source/package_armada_ce_audio_handoff.py")
    return handoff, restore_info, restore_runtime_info


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
    subprocess.run(["git", "merge-base", "--is-ancestor", LAYOUT_COMMIT, args.commit],
                   cwd=ROOT, check=True)
    subprocess.run(["git", "merge-base", "--is-ancestor", HANDOFF_COMMIT, args.commit],
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
    runtime_elf, runtime_package = args.elf.resolve(), args.runtime.resolve()
    runtime_record, runtime_compile_data = verify_build(runtime_elf, args.commit)
    check(runtime_package.stat().st_size <= 4 * 1024 * 1024 + 64,
          "Menu runtime exceeds its envelope size limit")
    runtime = runtime_package.read_bytes()
    new_runtime_info = verify(runtime)
    runtime_payload, runtime_memory = flatten_elf(runtime_elf.read_bytes())
    check(new_runtime_info["build"] == stamp and stamp.encode() + b"\0" in runtime[64:] and
          runtime[64:] == runtime_payload and new_runtime_info["memory_bytes"] == runtime_memory,
          "Menu package, built ELF, compiled display and commit IDs differ")
    check(b"L Memory" not in runtime[64:], "Stale L Memory shortcut remains in the new menu")
    seed = inspect_seed(candidate)
    from check_armada_ce_excerpt import check as check_excerpt
    audit = check_excerpt(directory)
    files, previous, restore_runtime_info, restore_info, original_source = validate_baseline(args.original_baseline)
    recovery = recovered_tree(original_source)
    module_previous, restore_info = validate_module_baseline(args.module_baseline, files, previous)
    cache_previous, restore_info = validate_cache_baseline(
        args.cache_baseline, files, previous, module_previous)
    layout_previous, restore_info = validate_layout_baseline(
        args.layout_baseline, files, previous, module_previous, cache_previous, args.original_baseline)
    handoff_previous, restore_info, restore_runtime_info = validate_handoff_baseline(
        args.baseline, files, previous, layout_previous)
    recovery["preceding_1524_source_archive_included"] = False
    recovery["preceding_1524_tree_retained_in_complete_git_bundle"] = True
    recovery["original_source_archive_included"] = False
    recovery["preceding_b9aa_source_archive_included"] = False
    recovery["preceding_b9aa_tree_retained_in_complete_git_bundle"] = True
    recovery["preceding_617_source_archive_included"] = False
    recovery["preceding_617_tree_retained_in_complete_git_bundle"] = True
    recovery["original_baseline_tree_retained_in_complete_git_bundle"] = True
    checks = corruption_checks()
    current_source = "source/K-UI-Armada-CE-Loop-Alignment-Fix-" + stamp + ".tar.gz"
    files.update({RUNTIME: runtime, INSTALL_EXCERPT: candidate,
                  current_source: source_archive(args.commit),
                  "source/package_armada_ce_loop_alignment.py": Path(__file__).read_bytes(),
                  "docs/armada-ce-loop-alignment-test.md":
                      (ROOT / "docs/armada-ce-loop-alignment-test.md").read_bytes(),
                  "evidence/ce-compile.json": compile_data,
                  "evidence/runtime-compile.json": runtime_compile_data,
                  "evidence/source-recovery.json": (json.dumps(recovery, indent=2) + "\n").encode(),
                  "evidence/excerpt-layout-and-stack.json": (json.dumps(audit, indent=2) + "\n").encode()})
    # Retain the complete recovered history as well as current/preceding source.
    # This makes the pinned recovery ancestry available after a fresh clone,
    # without requiring a reconstruction of commit timestamps/identities.
    bundle_member = "source/K-UI-Armada-CE-Loop-Alignment-Fix-" + stamp + ".bundle"
    with tempfile.TemporaryDirectory(prefix="kui-loop-alignment-source-") as temporary:
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
    add_evidence(files, "armada-ce-owner-callback-2026-10-14.json=" +
                 str(ROOT / "docs/evidence/armada-ce-owner-callback-2026-10-14.json"))
    for specification in args.evidence:
        add_evidence(files, specification)
    info = {"source_commit": args.commit, "source_recovery": recovery,
            "source_git_bundle": {"member": bundle_member,
                                  "sha256": digest(files[bundle_member]),
                                  "bytes": len(files[bundle_member])},
            "ce": ce_info, "ce_sha256": digest(candidate), "ce_compile": compile_record,
            "runtime": new_runtime_info, "runtime_sha256": digest(runtime),
            "runtime_compile": runtime_record,
            "hardware_tested": False, "ce_hardware_tested": False, "runtime_hardware_tested": False,
            "ce_audio_enabled": True, "full_track_streaming": False,
            "universal_ce_support": False,
            "ce_audio_mode": "conditional two-second loop through the verified CE sound owner",
            "scope": "Armada CE 64-byte native stereo-loop alignment; matching runtime and optional loader",
            "native_loop_alignment": {
                "source_pcm_bytes": 352800, "native_buffer_bytes": 352768,
                "native_buffer_alignment_bytes": 64, "stereo_frame_bytes": 4,
                "trimmed_frames": 8, "trimmed_duration_ms": 8 * 1000 / 44100,
                "native_loop_duration_seconds": 88192 / 44100,
                "complete_source_pcm_crc_retained": True,
                "distributed_seed_and_pcm_placeholder_size_unchanged": True},
            "mapped_word_read_policy": layout_previous["mapped_word_read_policy"],
            "binding_admission": layout_previous["binding_admission"],
            "ui_update": {"diagnostics_L_Memory_shortcut_removed": True,
                          "saved_options_and_game_library_preserved": True},
            "audio_handoff_policy": handoff_previous["audio_handoff_policy"],
            "installed_files": [RUNTIME, INSTALL_EXCERPT], "runtime_replaced": True,
            "restore_runtime_source_commit": HANDOFF_COMMIT,
            "restore_runtime": restore_runtime_info,
            "restore_runtime_sha256": HANDOFF_RUNTIME_SHA256,
            "restore_ce_source_commit": HANDOFF_COMMIT,
            "restore_ce": restore_info, "restore_ce_sha256": HANDOFF_CE_SHA256,
            "restore_files": [RESTORE_PREFIX + RUNTIME, RESTORE_PREFIX + INSTALL_EXCERPT],
            "baseline_zip_bytes": HANDOFF_ZIP_BYTES, "baseline_zip_sha256": HANDOFF_ZIP_SHA256,
            "layout_baseline_zip_bytes": LAYOUT_ZIP_BYTES,
            "layout_baseline_zip_sha256": LAYOUT_ZIP_SHA256,
            "cache_baseline_zip_bytes": CACHE_ZIP_BYTES,
            "cache_baseline_zip_sha256": CACHE_ZIP_SHA256,
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
            "prior_console_observation": {"source_commit": HANDOFF_COMMIT,
                "excerpt_status": "0000001c", "attempt": "00000004",
                "play": "00000000", "refused": "00000004", "hresult": "887800ff",
                "current_process": "8c14e9ec", "owner_process": "8c14e9ec",
                "buffer": "0104096c", "caps_flags": "000000e8", "caps_bytes": "00056220",
                "free_voices": "00000012", "pcm_crc32": "cde9480b",
                "audible_music": False,
                "interpretation": "Owner handoff succeeded; native Play rejected unaligned stereo loop length"},
            "previous_audio_handoff_source_commit": HANDOFF_COMMIT,
            "previous_loaded_layout_source_commit": LAYOUT_COMMIT,
            "previous_loaded_layout_binding_admission": layout_previous["binding_admission"],
            "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                      if name.endswith(".tar.gz")},
            "build_commands": ["source .deps/kos/environ.sh",
                "make -j2 -f Makefile.dc build/kui-runtime.elf BUILD_ID=" + stamp,
                "python3 tools/runtime_package.py build build/kui-runtime.elf "
                "build/runtime.kui --build-id " + stamp,
                "make -j2 -f Makefile.armada-excerpt build/armada-ce-excerpt/entry.elf BUILD_ID=" + stamp,
                "python3 tools/runtime_package.py build build/armada-ce-excerpt/entry.elf "
                "build/armada-ce-excerpt/ce-cdda-armada.kui --build-id " + stamp,
                "python3 tools/check_armada_ce_excerpt.py build/armada-ce-excerpt",
                "python3 tools/package_armada_ce_loop_alignment.py --commit " + args.commit +
                " --runtime " + str(args.runtime) + " --elf " + str(args.elf) +
                " --directory build/armada-ce-excerpt --baseline " + str(args.baseline) +
                " --layout-baseline " + str(args.layout_baseline) +
                " --cache-baseline " + str(args.cache_baseline) +
                " --module-baseline " + str(args.module_baseline) +
                " --original-baseline " + str(args.original_baseline)],
            "verification": {"ce_envelope_crc_and_blank_card_manifest": True,
                "both_packages_match_built_elf_and_clean_compile_records": True,
                "compiled_display_and_envelope_build_ids_match_clean_head": True,
                "exact_original_module_cache_layout_and_previous_zip_member_metadata": True,
                "both_rollbacks_match_exact_previous_installed_bytes": True,
                "only_requested_runtime_and_optional_loader_installed": True,
                "source_recovery_matches_all_archived_bytes_and_modes": True,
                "unique_aligned_seed_and_zero_pcm_placeholder": True,
                "packaging_corruption_checks": checks,
                "output_member_sizes_sha256_and_crc32_verified": True}}
    files["build.json"] = (json.dumps(info, indent=2) + "\n").encode()
    files["README.md"] = f"""# Armada CE CD music loop-alignment test

Menu and optional excerpt loader build **{stamp}**. This ZIP installs exactly
`{RUNTIME}` and `{INSTALL_EXCERPT}`. The unused **L Memory** Diagnostics shortcut
remains removed. Saved game options stay available.

1. With the console off, back up both files, merge this ZIP's `KUI` folder
   onto the SCI microSD card, replace both files and cold boot.
2. Select **Original Armada → Y Options → CD music On → START**.
3. Listen for **30 seconds at the main menu**, then play for **two minutes**,
   checking guns and other effects. Expected success is a **roughly two-second
   repeating track-4 excerpt**. Full-song streaming is still a later step.
4. Press **A+B+X+Y+Start** and photograph the whole result screen even if silent.

The last test entered the correct sound-owner process and created the native
buffer, but Play rejected its loop length. The audited stereo driver requires
64-byte alignment. This test uses **352,768 bytes** for the native buffer,
omitting the last eight stereo frames (about **0.18 ms**) from playback. The
complete **352,800-byte** source excerpt and its CRC remain unchanged. Owner,
cache, mapping, caller-context and sound-voice checks remain required. Host
and build evidence is included; console playback remains unverified.

No new extraction or upload is required. Set **CD music Off** for the ordinary
CE loader. To restore both previous files exactly, merge
`restore-loop-alignment/KUI` onto the card and cold boot. Both backup files
are byte-identical build **{HANDOFF_COMMIT[:12]}**.

Current and preceding bd6 source archives, complete recovered Git history,
dependency source/license files, evidence, and member SHA256/CRC checks are
included. Older source archives are verified during packaging and their exact
trees retained in the Git bundle rather than redundant source tars. No private
executable, kernel, ARM driver or audio sample is distributed.
See `docs/armada-ce-loop-alignment-test.md`.
""".encode()
    check([name for name in files if name.startswith("KUI/")] == [RUNTIME, INSTALL_EXCERPT],
          "Only the requested menu runtime and optional CE loader may appear under KUI")
    check(sorted(name for name in files if name.startswith(RESTORE_PREFIX)) ==
          sorted([RESTORE_PREFIX + RUNTIME, RESTORE_PREFIX + INSTALL_EXCERPT]),
          "Combined rollback must contain exactly the two previous installed files")
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
                      "runtime_build": stamp, "restore_runtime_build": HANDOFF_COMMIT[:12],
                      "restore_ce_build": HANDOFF_COMMIT[:12]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/armada-ce-excerpt")
    parser.add_argument("--runtime", type=Path, default=ROOT / "build/runtime.kui")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/kui-runtime.elf")
    parser.add_argument("--baseline", type=Path, default=HANDOFF_BASELINE)
    parser.add_argument("--layout-baseline", type=Path, default=LAYOUT_BASELINE)
    parser.add_argument("--cache-baseline", type=Path, default=CACHE_BASELINE)
    parser.add_argument("--module-baseline", type=Path, default=MODULE_BASELINE)
    parser.add_argument("--original-baseline", type=Path, default=ORIGINAL_BASELINE)
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--output", type=Path, default=DELIVERABLES / "K-UI-Armada-CE-Loop-Alignment-Fix.zip")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile,
            KeyError, ImportError, json.JSONDecodeError, tarfile.TarError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
