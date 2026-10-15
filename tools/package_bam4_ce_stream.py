#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the first experimental Bust-A-Move 4 US CE/CDDA console candidate."""

import argparse
import gzip
import io
import json
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile
import zlib

from package_armada_excerpt import check, digest, safe_member, verify_build, verify_members
from package_armada_ce_loop_alignment import DEPENDENCIES
from package_armada_ce_stream import add_evidence, inspect_stream_seed, WIRE_BYTES
from retail_package import inspect_retail
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
DELIVERABLES = ROOT.parents[1] / "deliverables"
BASELINE = DELIVERABLES / "K-UI-2.0-RC9-Armada-Audio.zip"
BASELINE_SHA256 = "8a201e12842eb66b8008f4ae720961791ce84bfa44988616f1c60a2af85156f1"
RUNTIME = "KUI/runtime.kui"
INSTALL_AUDIO = "KUI/apps/games/ce-cdda-bam4.kui"
ARMADA_AUDIO = "KUI/apps/games/ce-cdda-armada.kui"
DELIVERY_LIMIT = 50 * 1024 * 1024


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def json_bytes(value):
    return (json.dumps(value, indent=2) + "\n").encode()


def clean_head(commit):
    check(re.fullmatch(r"[0-9a-f]{40}", commit) is not None,
          "--commit requires a full lowercase commit hash")
    check(git("rev-parse", "HEAD").decode().strip() == commit and
          not git("status", "--porcelain"),
          "Commit all final source changes before packaging")


def dependency_sources(path):
    """Reuse only pinned RC9 licenses/dependencies, without prior test replay."""
    data = path.read_bytes()
    check(len(data) < DELIVERY_LIMIT and digest(data) == BASELINE_SHA256,
          "License/source baseline is not the exact verified Armada RC9 ZIP")
    files = {}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = verify_members(archive)
        previous = json.loads(archive.read("build.json"))
        check(previous["installed_files"] == [RUNTIME, ARMADA_AUDIO] and
              previous["full_track_streaming"] is True,
              "RC9 baseline installation metadata differs")
        check(digest(archive.read(ARMADA_AUDIO)) == previous["ce_sha256"],
              "Preserved Armada RC9 module identity differs")
        for name in archive.namelist():
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                value = archive.read(name)
                if name.startswith("source/dependencies/"):
                    check(name in DEPENDENCIES and digest(value) == DEPENDENCIES[name] and
                          previous["source_archive_sha256"][name] == members[name],
                          "Dependency source identity differs: " + name)
                files[name] = value
        check({name for name in files if name.startswith("source/dependencies/")} ==
              set(DEPENDENCIES) and "LICENSE" in files and "THIRD_PARTY.md" in files and
              any(name.startswith("LICENSES/") for name in files),
              "Verified dependency sources/licenses are incomplete")
        provenance = {
            "zip_sha256": BASELINE_SHA256, "source_commit": previous["source_commit"],
            "inherited_members": {name: digest(value) for name, value in sorted(files.items())},
            "armada_rc9_preserved": {"member": ARMADA_AUDIO,
                                     "sha256": previous["ce_sha256"],
                                     "included_or_replaced": False},
            "prior_binary_and_evidence_members_inherited": False,
        }
    return files, provenance


def audit_stream(directory, candidate):
    seed = inspect_stream_seed(candidate)
    from check_armada_ce_excerpt import check as check_audio
    audit = check_audio(directory, stream=True, bam4=True)
    check(audit.get("title_profile") == "bam4-us" and audit.get("stream_profile") is True,
          "Linked CE audit did not select the exact BAM4 stream profile")
    arm = audit.get("stream_boot_arm", {})
    check(arm.get("entry_slot") == 52 and arm.get("preserves_controller_zero_check") is True and
          arm.get("attach_only_after_relay") is True and
          arm.get("bootstrap_audio_and_bus_gated") is True,
          "Stream bootstrap arm and pristine-state checks were not audited")
    owner = audit.get("native_owner_reader", {})
    check(owner.get("complete_owner_reader_bodies_pinned") == 10 and
          owner.get("native_owner_va_sample_only") is True and
          owner.get("worker_cache_array_observer_absent") is True and
          owner.get("irq_mask_covers_both_walks_and_native_sample") is True and
          owner.get("entire_saved_sr_restored") is True and
          owner.get("sr_stability_mask") == "0x700000f0" and
          owner.get("arithmetic_status_flags_do_not_refuse") is True,
          "Native owner memory read and complete IRQ/SR contract were not audited")
    check(audit["distributed_pcm_bytes"] == 0 and
          audit["distributed_audio_map_bytes"] == WIRE_BYTES and
          int(audit["seed_address"], 16) - 0x8ce10000 == seed["stage_offset"],
          "CEX2 blank source map and linked BAM4 stage disagree")
    return seed, audit


def write_zip(output, files):
    check(sorted(name for name in files if name.startswith("KUI/")) ==
          sorted([RUNTIME, INSTALL_AUDIO]),
          "Install set must contain only the menu runtime and BAM4 audio module")
    check(not any(name.endswith(".kui") and name not in (RUNTIME, INSTALL_AUDIO)
                  for name in files), "Unexpected loader or rollback binary in package")
    check(all(safe_member(name) for name in files), "Unsafe output member name")
    files["MEMBERS.json"] = json_bytes({
        name: {"bytes": len(data), "sha256": digest(data), "crc32": f"{zlib.crc32(data):08x}"}
        for name, data in sorted(files.items())})
    files["SHA256SUMS"] = "".join(digest(data) + "  " + name + "\n"
                                    for name, data in sorted(files.items())).encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=output.name + ".", suffix=".partial",
                                         dir=output.parent, delete=False) as handle:
            temporary = Path(handle.name)
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=False) as archive:
            for name, data in sorted(files.items()):
                member = zipfile.ZipInfo(name, (2026, 10, 15, 0, 0, 0))
                member.external_attr = 0o100644 << 16
                member.compress_type = (zipfile.ZIP_STORED if name.endswith(".tar.gz")
                                        else zipfile.ZIP_DEFLATED)
                archive.writestr(member, data)
        with zipfile.ZipFile(temporary) as archive:
            verify_members(archive)
            check(len(archive.infolist()) == len(files), "Output ZIP member count differs")
            for name, data in files.items():
                check(archive.read(name) == data, "Output ZIP member differs: " + name)
        check(temporary.stat().st_size < DELIVERY_LIMIT, "Output ZIP exceeds 50 MiB")
        temporary.replace(output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def build(args):
    clean_head(args.commit)
    stamp = args.commit[:12]
    directory = args.directory.resolve()
    audio_elf, audio_package = directory / "entry.elf", directory / "ce-cdda-bam4.kui"
    runtime_elf, runtime_package = args.elf.resolve(), args.runtime.resolve()
    audio_compile, audio_compile_data = verify_build(audio_elf, args.commit)
    runtime_compile, runtime_compile_data = verify_build(runtime_elf, args.commit)
    check(audio_package.stat().st_size <= 4 * 1024 * 1024 + 64 and
          runtime_package.stat().st_size <= 4 * 1024 * 1024 + 64,
          "Runtime or BAM4 module exceeds its envelope size limit")
    audio, runtime = audio_package.read_bytes(), runtime_package.read_bytes()
    audio_info, runtime_info = inspect_retail(audio, ce=True), verify(runtime)
    audio_info.pop("abi", None)
    for data, elf, info, label in ((runtime, runtime_elf, runtime_info, "menu"),
                                  (audio, audio_elf, audio_info, "BAM4 module")):
        payload, memory = flatten_elf(elf.read_bytes())
        check(info["build"] == stamp and stamp.encode() + b"\0" in data[64:] and
              data[64:] == payload and info["memory_bytes"] == memory,
              label + " envelope/ELF/display/commit identities differ")
    check(b"L Memory" not in runtime[64:], "Stale Diagnostics shortcut remains in menu")
    seed, audit = audit_stream(directory, audio)
    files, provenance = dependency_sources(args.baseline)
    subprocess.run(["git", "merge-base", "--is-ancestor", provenance["source_commit"], args.commit],
                   cwd=ROOT, check=True)
    source_member = "source/K-UI-BAM4-CE-CDDA-" + stamp + ".tar.gz"
    files.update({
        RUNTIME: runtime, INSTALL_AUDIO: audio,
        source_member: gzip.compress(git("archive", "--format=tar",
                                        "--prefix=K-UI-BAM4-CE-CDDA/", args.commit), mtime=0),
        "source/package_bam4_ce_stream.py": Path(__file__).read_bytes(),
        "docs/bam4-ce-cdda-test.md": (ROOT / "docs/bam4-ce-cdda-test.md").read_bytes(),
        "evidence/runtime-compile.json": runtime_compile_data,
        "evidence/bam4-compile.json": audio_compile_data,
        "evidence/bam4-layout-and-stack.json": json_bytes(audit),
        "evidence/dependency-source-provenance.json": json_bytes(provenance),
    })
    for specification in args.evidence:
        add_evidence(files, specification)
    info = {
        "source_commit": args.commit, "source_tree": git("rev-parse", "HEAD^{tree}").decode().strip(),
        "source_dirty": False, "installed_files": [RUNTIME, INSTALL_AUDIO],
        "runtime": runtime_info, "runtime_sha256": digest(runtime), "runtime_compile": runtime_compile,
        "ce": audio_info, "ce_sha256": digest(audio), "ce_compile": audio_compile,
        "hardware_tested": False, "runtime_hardware_tested": False, "ce_hardware_tested": False,
        "experimental": True, "ce_audio_enabled": True, "full_track_streaming": True,
        "title_profile": "bam4-us", "product": "T-8117N", "track_count": 26,
        "scope": "First original US Bust-A-Move 4 SCI Windows CE/CDDA console candidate",
        "universal_ce_support": False, "worms_enabled": False,
        "armada_rc9_preserved": provenance["armada_rc9_preserved"],
        "ordinary_ce_payload_replaced": False, "native_mdk2_toy_payloads_replaced": False,
        "stream_placeholder": seed, "layout_and_controller": audit,
        "dependency_source_provenance": provenance,
        "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                  if name.endswith(".tar.gz")},
        "build_commands": [
            "source .deps/kos/environ.sh",
            "make -B -j2 -f Makefile.dc build/kui-runtime.elf BUILD_ID=" + stamp,
            "python3 tools/runtime_package.py build build/kui-runtime.elf build/runtime.kui --build-id " + stamp,
            "make -B -j2 -f Makefile.bam4-stream build/bam4-ce-stream/entry.elf BUILD_ID=" + stamp,
            "python3 tools/runtime_package.py build build/bam4-ce-stream/entry.elf "
            "build/bam4-ce-stream/ce-cdda-bam4.kui --build-id " + stamp,
            "python3 tools/check_armada_ce_excerpt.py --stream --bam4 build/bam4-ce-stream",
            "python3 tools/package_bam4_ce_stream.py --commit " + args.commit,
        ],
        "verification": {
            "both_packages_match_clean_commit_elf_and_display_build_id": True,
            "bam4_linked_layout_stack_bootstrap_and_owner_reader_checked": True,
            "unique_aligned_CEX2_and_blank_complete_wire_map": True,
            "distributed_private_game_kernel_firmware_or_pcm_bytes": False,
            "exact_rc9_zip_and_all_source_license_member_sha_crc_checked": True,
            "output_member_sizes_sha256_crc32_verified": True,
            "inherited_historical_corruption_tests_replayed": False,
        },
    }
    files["build.json"] = json_bytes(info)
    files["README.md"] = (f"# K-UI — Bust-A-Move 4 CE/CDDA experimental test\n\n"
        f"Matching menu and BAM4 module build **{stamp}**. This first candidate has "
        "not been tested on Dreamcast hardware.\n\n"
        f"With the console off, merge `KUI` onto the SCI microSD card, replacing `{RUNTIME}` "
        f"and adding `{INSTALL_AUDIO}`, then cold boot. These are the only two installed files. "
        "Keep the existing Armada RC9 audio module; this ZIP neither includes nor replaces it.\n\n"
        "Choose the original US Bust-A-Move 4 GDI (T-8117N, 26 tracks). In Y Options set "
        "**Background loading On** and **CD music On**; settings save automatically. "
        "Launch with START. Play an actual puzzle stage for at least three minutes and "
        "include a stage change. Check that music starts or returns after loading and "
        "that sound effects continue. No title/menu music wait is required.\n\n"
        "After a failure, or after that test, return with **A+B+X+Y+Start**. Record one "
        "complete **15-second cycle** of the three diagnostic pages (five seconds each), "
        "then power-cycle. Report silence, looping, delayed starts, cuts or lockups.\n\n"
        "CD music Off selects the ordinary CE loader. Worms is not enabled: its different "
        "audio consumer/locking contract still needs a separate port. FMV improvements "
        "and general Windows CE compatibility are not claimed.\n\n"
        "Complete committed source, verified dependency sources "
        "and licenses, build/audit evidence and complete member SHA256/CRC manifests "
        "are included. The module carries a blank CEX2 source map, filled from your own "
        "GDI before boot. No private game executable, CE kernel, ARM firmware or audio "
        "sample is distributed. See `docs/bam4-ce-cdda-test.md`.\n").encode()
    clean_head(args.commit)
    write_zip(args.output, files)
    print(json.dumps({"path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
                      "entries": len(files), "sha256": digest(args.output.read_bytes()),
                      "source_commit": args.commit, "runtime_build": stamp, "bam4_build": stamp,
                      "installed_files": [RUNTIME, INSTALL_AUDIO], "hardware_tested": False}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/bam4-ce-stream")
    parser.add_argument("--runtime", type=Path, default=ROOT / "build/runtime.kui")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/kui-runtime.elf")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--output", type=Path, default=DELIVERABLES / "K-UI-BAM4-CE-CDDA-Test.zip")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile,
            KeyError, ImportError, json.JSONDecodeError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
