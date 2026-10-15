#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the isolated audible Armada CE excerpt test and corrected menu."""

import argparse
import gzip
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
DELIVERABLES = ROOT.parent / "deliverables"
INSTALL_RUNTIME = "KUI/runtime.kui"
INSTALL_EXCERPT = "KUI/apps/games/ce-cdda-armada.kui"
BASELINE_COMMIT = "1e00513dd14ab6fba5ab061e63ef1cf9f4c918e6"
BASELINE_ZIP_SHA256 = "67a28f5dd337dc3a2ae6308edfd770e456272ec262004477c88fafde7ab78598"
BASELINE_RUNTIME_SHA256 = "436b8eb4ce43a6d07e08bf484f41fca36c8311d6ee6c05f6d7d81eef6857b74e"
BASELINE_SOURCE = "source/K-UI-Launch-Options-1e00513dd14a.tar.gz"
BASELINE_SOURCE_SHA256 = "464bffbcb7397b6be733c98e73a8325a74f2e6cc806f6a124e750af4ed4c5f2d"
PRESERVED_CE_MEMBER = "KUI/apps/games/ce-probe.kui"
PRESERVED_CE_COMMIT = "33298cc0bdb88ec9df12fe1d97bc26295c83b433"
PRESERVED_CE_SHA256 = "118ebca552f649ad639b02eebcac01bdbfb2e2b1531b0d1baadb1818dc159fde"
PRESERVED_NATIVE_SHA256 = "3c342febf7409a0aedfa0d4973a8c0fd6dbdbf3675ad783fa373fc8cb30c82a7"
PRESERVED_MDK2_SHA256 = "42b9a8c958548521ec0bac351ff55b4edc5a51bd83b26f8a5c7cd97ebcd1a1b7"
PCM_BYTES = 150 * 2352
SEED_BYTES = 64
SEED = struct.pack("<16I", 0x31584543, 1, PCM_BYTES, 0, 150, 149992,
                   *([0] * 10))
DELIVERY_LIMIT = 50 * 1024 * 1024

sys.path.insert(0, str(ROOT / "tools"))
from runtime_package import flatten_elf, verify  # noqa: E402
from retail_package import inspect_retail, STAGE_BLOB_OFFSET  # noqa: E402


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check(condition, message):
    if not condition:
        raise ValueError(message)


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_archive(commit):
    return gzip.compress(git("archive", "--format=tar",
                             "--prefix=K-UI-Armada-CE-Excerpt/", commit), mtime=0)


def safe_member(name):
    parsed = PurePosixPath(name)
    return (bool(name) and not parsed.is_absolute() and "\\" not in name and
            all(part not in ("", ".", "..") for part in name.split("/")))


def verify_members(archive):
    """Check CRC and the complete explicit SHA manifest, including provenance."""
    names = archive.namelist()
    check(len(names) == len(set(names)) and all(safe_member(n) for n in names),
          "ZIP has duplicate or unsafe member names")
    check(archive.testzip() is None, "ZIP failed its member CRC check")
    manifest = archive.read("SHA256SUMS").decode("ascii")
    expected = {}
    for line in manifest.splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        check(match is not None, "Malformed ZIP SHA256SUMS line")
        sha, name = match.groups()
        check(safe_member(name) and name not in expected and name != "SHA256SUMS",
              "ZIP SHA256SUMS has an unsafe, repeated or self-referential name")
        expected[name] = sha
    check(set(expected) == set(names) - {"SHA256SUMS"},
          "ZIP SHA256SUMS does not cover exactly its members")
    for name, sha in expected.items():
        check(digest(archive.read(name)) == sha, "ZIP SHA mismatch: " + name)
    if "MEMBERS.json" in names:
        records = json.loads(archive.read("MEMBERS.json"))
        check(set(records) == set(names) - {"SHA256SUMS", "MEMBERS.json"},
              "ZIP member metadata does not cover exactly its content members")
        for name, record in records.items():
            data = archive.read(name)
            check(record == {"bytes": len(data), "sha256": digest(data),
                             "crc32": f"{zlib.crc32(data):08x}"} and
                  archive.getinfo(name).CRC == zlib.crc32(data),
                  "ZIP member size/SHA/CRC metadata differs: " + name)
    return expected


def inspect_seed(candidate):
    """No actual PCM may ship: exactly one aligned complete zero seed exists."""
    payload = candidate[64:]
    offsets = [offset for offset in
               range(STAGE_BLOB_OFFSET, len(payload) - SEED_BYTES - PCM_BYTES + 1, 4)
               if payload[offset:offset + SEED_BYTES] == SEED]
    check(len(offsets) == 1, "CE loader must contain one unique aligned excerpt seed")
    offset = offsets[0]
    pcm = payload[offset + SEED_BYTES:offset + SEED_BYTES + PCM_BYTES]
    check(len(pcm) == PCM_BYTES and not any(pcm),
          "Distributed excerpt PCM must be a complete all-zero placeholder")
    return {"payload_offset": offset, "stage_offset": offset - STAGE_BLOB_OFFSET,
            "seed_bytes": SEED_BYTES, "pcm_bytes": PCM_BYTES,
            "placeholder_crc32": f"{zlib.crc32(pcm):08x}",
            "placeholder_sha256": digest(pcm), "all_zero": True,
            "seed_pcm_crc32": "00000000", "sectors": 150, "start_lba": 149992}


def verify_build(elf, commit):
    check(elf.stat().st_size <= 16 * 1024 * 1024, "ELF exceeds its audit size limit")
    record_path = elf.with_suffix(".compile.json")
    record_data = record_path.read_bytes()
    record = json.loads(record_data)
    check(record["commit"] == commit and record["source_dirty"] is False and
          record["elf_sha256"] == digest(elf.read_bytes()),
          "ELF clean-source provenance does not match this commit: " + str(elf))
    return record, record_data


def add_baseline(files, path):
    check(path.stat().st_size < DELIVERY_LIMIT and
          digest(path.read_bytes()) == BASELINE_ZIP_SHA256,
          "Baseline ZIP does not match the exact previous menu delivery")
    with zipfile.ZipFile(path) as archive:
        members = verify_members(archive)
        prior_data = archive.read("build.json")
        prior = json.loads(prior_data)
        check(prior["source_commit"] == BASELINE_COMMIT and
              prior["runtime_sha256"] == BASELINE_RUNTIME_SHA256 and
              prior["ce_source_commit"] == PRESERVED_CE_COMMIT and
              prior["ce_sha256"] == PRESERVED_CE_SHA256 and
              prior["ce_audio_enabled"] is False and
              prior["installed_files"] == [INSTALL_RUNTIME, PRESERVED_CE_MEMBER],
              "Baseline source/build/scope metadata differs")
        restore = archive.read(INSTALL_RUNTIME)
        restore_info = verify(restore)
        check(digest(restore) == BASELINE_RUNTIME_SHA256 and
              restore_info == {"format": 1, "build": BASELINE_COMMIT[:12],
                               "payload_bytes": 1781448, "memory_bytes": 5678704,
                               "crc32": "06d54ee2"},
              "Baseline runtime is not the exact installed saved-options build")
        ce = archive.read(PRESERVED_CE_MEMBER)
        ce_info = inspect_retail(ce, ce=True)
        ce_info.pop("abi", None)
        check(len(ce) == 85804 and digest(ce) == PRESERVED_CE_SHA256 and
              ce_info == prior["ce"] and ce_info["build"] == PRESERVED_CE_COMMIT[:12] and
              ce_info["crc32"] == "296d7ff2",
              "Preserved normal CE loader differs from its tested bytes")
        source = archive.read(BASELINE_SOURCE)
        check(digest(source) == BASELINE_SOURCE_SHA256 and
              prior["source_archive_sha256"][BASELINE_SOURCE] == BASELINE_SOURCE_SHA256,
              "Baseline runtime source archive does not match its pinned provenance")
        preserved = prior["unchanged_loader_sha256"]
        check(preserved == {"KUI/apps/games/retail-boot.kui": PRESERVED_NATIVE_SHA256,
                            "KUI/apps/games/retail-cdda-mdk2.kui": PRESERVED_MDK2_SHA256},
              "Baseline native/MDK2 preservation metadata differs")
        files["restore-runtime/" + INSTALL_RUNTIME] = restore
        files[BASELINE_SOURCE] = source
        files["evidence/baseline/build.json"] = prior_data
        files["evidence/baseline/SHA256SUMS"] = archive.read("SHA256SUMS")
        files["source/baseline/package_launch_options.py"] = archive.read(
            "source/package_launch_options.py")
        dependency_count = 0
        for name in archive.namelist():
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                data = archive.read(name)
                if name.startswith("source/dependencies/"):
                    dependency_count += 1
                    check(prior["source_archive_sha256"][name] == members[name],
                          "Dependency source provenance differs: " + name)
                check(name not in files, "Duplicate inherited member: " + name)
                files[name] = data
        check(dependency_count == 3 and "LICENSE" in files and "THIRD_PARTY.md" in files,
              "Baseline license/dependency source set is incomplete")
    return restore_info, ce_info, preserved


def add_evidence(files, specification):
    check("=" in specification, "Evidence must have the form NAME=PATH")
    name, path = specification.split("=", 1)
    check(safe_member(name), "Invalid evidence member name")
    member = "evidence/" + name
    check(member not in files, "Duplicate evidence member: " + member)
    source = Path(path)
    check(0 < source.stat().st_size <= 4 * 1024 * 1024,
          "Evidence must contain at most 4 MiB: " + str(source))
    check(source.suffix.lower() in (".json", ".log", ".txt", ".md"),
          "Evidence must be JSON or text, not a private input binary")
    files[member] = source.read_bytes()


def build(args):
    args.directory = args.directory.resolve()
    args.runtime = args.runtime.resolve()
    args.elf = args.elf.resolve()
    args.payload = (args.payload or args.directory / "ce-cdda-armada.kui").resolve()
    candidate_elf = args.directory / "entry.elf"
    check(re.fullmatch(r"[0-9a-f]{40}", args.commit) is not None,
          "--commit requires a full lowercase commit hash")
    check(git("rev-parse", "HEAD").decode().strip() == args.commit,
          "Requested commit does not match HEAD")
    check(not git("status", "--porcelain"), "Commit all source changes before packaging")
    stamp = args.commit[:12]

    runtime_record, runtime_record_data = verify_build(args.elf, args.commit)
    candidate_record, candidate_record_data = verify_build(candidate_elf, args.commit)
    check(args.runtime.stat().st_size <= 4 * 1024 * 1024 + 64 and
          args.payload.stat().st_size <= 4 * 1024 * 1024 + 64,
          "Runtime or CE package exceeds its envelope size limit")
    runtime, candidate = args.runtime.read_bytes(), args.payload.read_bytes()
    runtime_info, candidate_info = verify(runtime), inspect_retail(candidate, ce=True)
    candidate_info.pop("abi", None)
    check(runtime_info["build"] == stamp and candidate_info["build"] == stamp,
          "Both package build IDs must match the final source commit")
    for data, elf, info, label in ((runtime, args.elf, runtime_info, "runtime"),
                                  (candidate, candidate_elf, candidate_info, "CE loader")):
        elf_payload, elf_memory = flatten_elf(elf.read_bytes())
        check(data[64:] == elf_payload and info["memory_bytes"] == elf_memory,
              label + " package does not match its built ELF")
        check(stamp.encode() + b"\0" in data[64:],
              label + " compiled display build ID differs from its envelope")
    seed = inspect_seed(candidate)
    # This isolated checker includes the ordinary stage/resident layout checks,
    # plus the live binder, guarded ABI bridge, pool and controller audits.
    from check_armada_ce_excerpt import check as check_excerpt  # noqa: E402
    excerpt_audit = check_excerpt(args.directory)

    files = {INSTALL_RUNTIME: runtime, INSTALL_EXCERPT: candidate,
             "docs/armada-ce-excerpt-test.md":
                 (ROOT / "docs/armada-ce-excerpt-test.md").read_bytes(),
             "source/K-UI-Armada-CE-Excerpt-" + stamp + ".tar.gz":
                 source_archive(args.commit),
             "source/package_armada_excerpt.py": Path(__file__).read_bytes(),
             "evidence/runtime-compile.json": runtime_record_data,
             "evidence/ce-compile.json": candidate_record_data,
             "evidence/excerpt-layout-and-stack.json":
                 (json.dumps(excerpt_audit, indent=2) + "\n").encode()}
    restore_info, ce_info, preserved = add_baseline(files, args.baseline)
    for specification in args.evidence:
        add_evidence(files, specification)
    info = {"source_commit": args.commit, "runtime": runtime_info,
            "runtime_sha256": digest(runtime), "ce": candidate_info,
            "ce_sha256": digest(candidate), "runtime_compile": runtime_record,
            "ce_compile": candidate_record,
            "hardware_tested": False, "runtime_hardware_tested": False,
            "ce_hardware_tested": False, "ce_audio_enabled": True,
            "ce_audio_mode": "conditional two-second loop through the verified CE sound owner",
            "full_track_streaming": False, "universal_ce_support": False,
            "scope": "Original raw US Armada CE CDDA excerpt and corrected saved-options menu",
            "installed_files": [INSTALL_RUNTIME, INSTALL_EXCERPT],
            "ordinary_ce_payload_replaced": False,
            "native_mdk2_toy_payloads_replaced": False,
            "preserved_ce_source_commit": PRESERVED_CE_COMMIT,
            "preserved_ce": ce_info, "preserved_ce_sha256": PRESERVED_CE_SHA256,
            "unchanged_loader_sha256": preserved,
            "unchanged_loader_builds": {"general_native": "72e3bf049b26",
                                         "mdk2_cdda": "89c4650e444f"},
            "restore_runtime_source_commit": BASELINE_COMMIT,
            "restore_runtime": restore_info,
            "restore_runtime_sha256": BASELINE_RUNTIME_SHA256,
            "baseline_zip_sha256": BASELINE_ZIP_SHA256,
            "excerpt_placeholder": seed,
            "admission": {"product": "T40301N 00", "bootfile": "0WINCEOS.BIN",
                          "kernel_bytes": 1253376,
                          "kernel_sha256": "a7628c78090fce2c93012d5e4db6142096e138dceb7ebbf555142e902ca03c1f",
                          "track_start_lbas": [0, 756, 45000, 149842, 434062],
                          "track_controls": [4, 0, 4, 0, 4], "sector_bytes": 2352,
                          "track_file_offsets": [0, 0, 0, 0, 0],
                          "storage": "SCI", "reader": "ASYNC", "variant": "Original"},
            "pool": {"begin": "0x8cf50000", "end": "0x8cfd0000", "bytes": 524288,
                     "preboot_reserved": True, "direct_gd_destination_excluded": True},
            "static_play_parameters": [4, 4, 15],
            "layout_and_controller": excerpt_audit,
            "build_commands": [
                "source .deps/kos/environ.sh",
                "make -j2 -f Makefile.dc build/kui-runtime.elf BUILD_ID=" + stamp,
                "python3 tools/runtime_package.py build build/kui-runtime.elf "
                "build/kui-runtime.kui --build-id " + stamp,
                "make -j2 -f Makefile.armada-excerpt "
                "build/armada-ce-excerpt/entry.elf BUILD_ID=" + stamp,
                "python3 tools/runtime_package.py build build/armada-ce-excerpt/entry.elf "
                "build/armada-ce-excerpt/ce-cdda-armada.kui --build-id " + stamp,
                "python3 tools/check_armada_ce_excerpt.py build/armada-ce-excerpt",
                "python3 tools/package_armada_excerpt.py --commit " + args.commit +
                " --runtime build/kui-runtime.kui --directory build/armada-ce-excerpt"],
            "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                      if name.endswith(".tar.gz")},
            "verification": {"runtime_and_ce_envelopes_and_crc": True,
                             "runtime_and_ce_match_built_elfs": True,
                             "compiled_display_build_ids_match_envelopes": True,
                             "compile_records_match_clean_source_commit_and_elf_hashes": True,
                             "source_commit_matches_clean_head": True,
                             "blank_card_manifest_and_ce_relocation": True,
                             "unique_aligned_seed_and_zero_pcm_placeholder": True,
                             "baseline_zip_and_all_members_match_sha256_and_crc": True,
                             "output_member_sizes_sha256_and_crc32_verified": True,
                             "restore_runtime_and_source_match_pinned_prior_delivery": True,
                             "preserved_ce_matches_pinned_prior_payload": True}}
    files["build.json"] = (json.dumps(info, indent=2) + "\n").encode()
    files["README.md"] = f"""# Armada CE CD music excerpt test

Menu and optional CE excerpt loader build: **{stamp}**. The GitHub link is back
beneath the K-UI logo. Saved launch toggles and Advanced read tests are included.

1. With the console off, back up `KUI/runtime.kui` and any existing
   `KUI/apps/games/ce-cdda-armada.kui`.
2. Merge this ZIP's `KUI` folder onto the SCI microSD card and cold boot. These
   are the only two installed files. Keep existing `ce-probe.kui`, native,
   MDK2 and Toy Commander loaders in place.
3. Select **Original Armada**, open **Y Options**, turn **CD music On**, and
   launch with **START**. The saved option selects the separate excerpt test.
4. Listen at the menu for **30 seconds**, then play for **one or two minutes**
   and check game effects. Success is a **two-second repeating excerpt**, not
   a complete song. Return with **A+B+X+Y+Start** and photograph the result,
   even if music stays silent.

This test conditionally plays audio through the verified CE sound owner; it can
refuse an unsafe or unavailable owner/context/buffer/voice and report why. It
has not been tested on hardware. No full-song streaming, other CE title support
or FMV improvement is claimed. No new startup extraction is required.

**CD music Off** uses the unchanged installed CE loader **33298cc0bdb8**.
Merge `restore-runtime/KUI` and cold boot for the exact previous menu
**{BASELINE_COMMIT[:12]}**. The optional excerpt loader can remain unused with
CD music Off. Your backups restore files present immediately before this ZIP.

See `docs/armada-ce-excerpt-test.md` for the test and saved controls. Source,
dependency sources, licenses, build/layout evidence, hashes and member CRCs are
included. The audio placeholder is all zero; private game/kernel/ARM-driver
bytes and music samples are not distributed.
""".encode()
    check(sorted(name for name in files if name.startswith("KUI/")) ==
          sorted([INSTALL_RUNTIME, INSTALL_EXCERPT]),
          "Only the menu and optional CE excerpt loader may appear under KUI")
    files["MEMBERS.json"] = (json.dumps({name: {"bytes": len(data),
                                              "sha256": digest(data),
                                              "crc32": f"{zlib.crc32(data):08x}"}
                                         for name, data in sorted(files.items())},
                                        indent=2) + "\n").encode()
    files["SHA256SUMS"] = "".join(digest(data) + "  " + name + "\n"
                                  for name, data in sorted(files.items())).encode()
    # Audits generate only ignored build evidence. Verify no source changed
    # while constructing the archive before publishing the atomic result.
    check(not git("status", "--porcelain") and
          git("rev-parse", "HEAD").decode().strip() == args.commit,
          "Source changed during packaging")
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
                member.compress_type = (zipfile.ZIP_STORED if name.endswith(".tar.gz")
                                        else zipfile.ZIP_DEFLATED)
                archive.writestr(member, data)
        with zipfile.ZipFile(temporary) as archive:
            verify_members(archive)
            check(len(archive.infolist()) == len(files), "Output ZIP member count differs")
            for name, data in files.items():
                check(archive.read(name) == data, "Output member differs: " + name)
        check(temporary.stat().st_size < DELIVERY_LIMIT,
              "Output ZIP exceeds the 50 MiB delivery limit")
        temporary.replace(args.output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(json.dumps({"path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
                      "entries": len(files), "sha256": digest(args.output.read_bytes()),
                      "source_commit": args.commit, "runtime_build": stamp,
                      "ce_excerpt_build": stamp, "preserved_ce_build": PRESERVED_CE_COMMIT[:12]},
                     indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/kui-runtime.elf",
                        help="Built menu ELF; its clean .compile.json record is required")
    parser.add_argument("--directory", type=Path, default=ROOT / "build/armada-ce-excerpt",
                        help="Isolated CE build directory, including entry.elf and audits")
    parser.add_argument("--payload", type=Path,
                        help="CE envelope, default DIRECTORY/ce-cdda-armada.kui")
    parser.add_argument("--baseline", type=Path,
                        default=DELIVERABLES / "K-UI-Saved-Launch-Options-and-Armada-Test.zip")
    parser.add_argument("--output", type=Path,
                        default=DELIVERABLES / "K-UI-Armada-CE-CDDA-Excerpt-Test.zip")
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile,
            KeyError, ImportError, json.JSONDecodeError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
