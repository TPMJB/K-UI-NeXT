#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the isolated Worms Armageddon US Windows CE/CDDA console candidate."""

import argparse
import gzip
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import zlib

from package_armada_excerpt import check, digest, safe_member, verify_build, verify_members
from package_armada_ce_stream import add_evidence
from package_bam4_ce_stream import (BASELINE, DELIVERY_LIMIT, DELIVERABLES, ROOT,
                                   RUNTIME, clean_head, dependency_sources, git, json_bytes)
from retail_package import STAGE_BLOB_OFFSET, inspect_retail
from runtime_package import flatten_elf, verify

INSTALL_AUDIO = "KUI/apps/games/ce-cdda-worms.kui"
PRESERVED_AUDIO = ["KUI/apps/games/ce-cdda-armada.kui", "KUI/apps/games/ce-cdda-bam4.kui"]
SEED_BYTES, WIRE_BYTES = 64, 4096
POOL_BEGIN, POOL_END = 0x8ce70000, 0x8cef0000
SOURCE_BEGIN, MAP_BEGIN, CODE_BEGIN = 0x8ce70040, 0x8cedf000, 0x8cec7000
STREAM_SEED = struct.pack("<16I", 0x32584543, 2, SOURCE_BEGIN, 352800,
                          MAP_BEGIN, WIRE_BYTES, 0, 352768, *([0] * 8))


def inspect_stream_seed(candidate):
    """Require one exact Worms CEX2 header and its complete blank source map."""
    payload = candidate[64:]
    found, position = [], payload.find(STREAM_SEED)
    while position >= 0:
        found.append(position)
        position = payload.find(STREAM_SEED, position + 1)
    check(len(found) == 1 and found[0] >= STAGE_BLOB_OFFSET and found[0] % 4 == 0,
          "Worms CEX2 seed must be unique and aligned inside the CE stage")
    offset = found[0]
    wire = payload[offset + SEED_BYTES:offset + SEED_BYTES + WIRE_BYTES]
    check(len(wire) == WIRE_BYTES and not any(wire),
          "Distributed Worms CEX2 source map must be exactly 4096 zero bytes")
    return {"format": "CEX2", "version": 2, "payload_offset": offset,
            "stage_offset": offset - STAGE_BLOB_OFFSET, "seed_bytes": SEED_BYTES,
            "wire_bytes": WIRE_BYTES, "wire_crc32": f"{zlib.crc32(wire):08x}",
            "wire_sha256": digest(wire), "wire_all_zero": True,
            "source_buffer_address": hex(SOURCE_BEGIN), "source_buffer_bytes": 352800,
            "map_address": hex(MAP_BEGIN), "native_ring_bytes": 352768,
            "seed_map_crc32": "00000000", "distributed_pcm_bytes": 0}


def audit_stream(directory, candidate):
    seed = inspect_stream_seed(candidate)
    from check_armada_ce_excerpt import check as check_audio
    audit = check_audio(directory, stream=True, worms=True)
    check(audit.get("title_profile") == "worms-us" and audit.get("stream_profile") is True and
          audit.get("preboot_pool_begin") == hex(POOL_BEGIN) and
          audit.get("preboot_pool_end") == hex(POOL_END) and
          audit.get("controller_entry") == hex(CODE_BEGIN),
          "Linked audit did not select the relocated Worms stream profile")
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
          "CEX2 blank source map and linked Worms stage disagree")
    return seed, audit


def write_zip(output, files):
    check(sorted(name for name in files if name.startswith("KUI/")) ==
          sorted([RUNTIME, INSTALL_AUDIO]),
          "Install set must contain only the menu runtime and Worms audio module")
    check(not any(name.endswith(".kui") and name not in (RUNTIME, INSTALL_AUDIO)
                  for name in files), "Unexpected loader or rollback binary in package")
    check(not any(name.endswith(".bundle") for name in files),
          "This delivery must not include a Git history bundle")
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
    audio_elf, audio_package = directory / "entry.elf", directory / "ce-cdda-worms.kui"
    runtime_elf, runtime_package = args.elf.resolve(), args.runtime.resolve()
    audio_compile, audio_compile_data = verify_build(audio_elf, args.commit)
    runtime_compile, runtime_compile_data = verify_build(runtime_elf, args.commit)
    check(audio_package.stat().st_size <= 4 * 1024 * 1024 + 64 and
          runtime_package.stat().st_size <= 4 * 1024 * 1024 + 64,
          "Runtime or Worms module exceeds its envelope size limit")
    audio, runtime = audio_package.read_bytes(), runtime_package.read_bytes()
    audio_info, runtime_info = inspect_retail(audio, ce=True), verify(runtime)
    audio_info.pop("abi", None)
    for data, elf, info, label in ((runtime, runtime_elf, runtime_info, "menu"),
                                  (audio, audio_elf, audio_info, "Worms module")):
        payload, memory = flatten_elf(elf.read_bytes())
        check(info["build"] == stamp and stamp.encode() + b"\0" in data[64:] and
              data[64:] == payload and info["memory_bytes"] == memory,
              label + " envelope/ELF/display/commit identities differ")
    check(b"L Memory" not in runtime[64:], "Stale Diagnostics shortcut remains in menu")
    seed, audit = audit_stream(directory, audio)
    files, provenance = dependency_sources(args.baseline)
    subprocess.run(["git", "merge-base", "--is-ancestor", provenance["source_commit"], args.commit],
                   cwd=ROOT, check=True)
    source_member = "source/K-UI-WinCE-Worms-CDDA-" + stamp + ".tar.gz"
    files.update({
        RUNTIME: runtime, INSTALL_AUDIO: audio,
        source_member: gzip.compress(git("archive", "--format=tar",
                                        "--prefix=K-UI-WinCE-Worms-CDDA/", args.commit), mtime=0),
        "source/package_wince_ce_stream.py": Path(__file__).read_bytes(),
        "docs/wince-cdda-worms-test.md": (ROOT / "docs/wince-cdda-worms-test.md").read_bytes(),
        "evidence/runtime-compile.json": runtime_compile_data,
        "evidence/worms-compile.json": audio_compile_data,
        "evidence/worms-layout-and-stack.json": json_bytes(audit),
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
        "title_profile": "worms-us", "product": "T40601N", "track_count": 8,
        "scope": "First original US Worms Armageddon SCI Windows CE/CDDA console candidate",
        "universal_ce_support": False,
        "optional_wince_cd_music": {
            "default": "Off", "saved_per_image": True,
            "offered_for_original_ce_gdi_with_high_density_audio": True,
            "available_adapters": ["armada-us", "bam4-us", "worms-us"],
            "unknown_runtime_refused_before_handoff": True,
            "generic_address_guessing": False},
        "preserved_audio_modules": PRESERVED_AUDIO,
        "armada_rc9_preserved": provenance["armada_rc9_preserved"],
        "ordinary_ce_payload_replaced": False, "native_mdk2_toy_payloads_replaced": False,
        "stream_placeholder": seed, "layout_and_controller": audit,
        "dependency_source_provenance": provenance,
        "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                  if name.endswith(".tar.gz")},
        "source_git_bundle_included": False,
        "build_commands": [
            "source .deps/kos/environ.sh",
            "make -B -j2 -f Makefile.dc build/kui-runtime.elf BUILD_ID=" + stamp,
            "python3 tools/runtime_package.py build build/kui-runtime.elf build/runtime.kui --build-id " + stamp,
            "make -B -j2 -f Makefile.worms-stream build/worms-ce-stream/entry.elf BUILD_ID=" + stamp,
            "python3 tools/runtime_package.py build build/worms-ce-stream/entry.elf "
            "build/worms-ce-stream/ce-cdda-worms.kui --build-id " + stamp,
            "python3 tools/check_armada_ce_excerpt.py --stream --worms build/worms-ce-stream",
            "python3 tools/package_wince_ce_stream.py --profile worms --commit " + args.commit,
        ],
        "verification": {
            "both_packages_match_clean_commit_elf_and_display_build_id": True,
            "worms_linked_layout_stack_bootstrap_and_owner_reader_checked": True,
            "relocated_pool_below_original_ram_end_and_oem_region": True,
            "unique_aligned_CEX2_and_blank_complete_wire_map": True,
            "distributed_private_game_kernel_firmware_or_pcm_bytes": False,
            "exact_rc9_zip_and_all_source_license_member_sha_crc_checked": True,
            "output_member_sizes_sha256_crc32_verified": True,
            "inherited_historical_corruption_tests_replayed": False,
        },
    }
    files["build.json"] = json_bytes(info)
    files["README.md"] = (f"# K-UI 2.0 RC9 — Worms Armageddon CD music test\n\n"
        f"Matching menu and Worms module build **{stamp}**. This first candidate has "
        "not been tested on Dreamcast hardware.\n\n"
        f"With the console off, merge `KUI` onto the SCI microSD card, replacing `{RUNTIME}` "
        f"and adding `{INSTALL_AUDIO}`, then cold boot. Keep the existing Armada and "
        "Bust-A-Move 4 audio modules; neither is included or replaced.\n\n"
        "Choose the original US Worms Armageddon GDI (T40601N, eight tracks). In "
        "**Y Options**, turn **Background loading On** and **CD music On**, then launch "
        "with **START**. Settings save for this image. Play a real match for at least "
        "three minutes, including a new or restarted match. Check music and sound effects "
        "during play and whether music returns after loading. No menu-music wait is required.\n\n"
        "After a failure, or after that test, press **A+B+X+Y+Start** together. Record a "
        "complete **15-second cycle** of the three diagnostic pages (five seconds each), "
        "then power-cycle. Report silence, looping, cuts, delayed starts or lockups.\n\n"
        "CD music defaults Off. The optional toggle is offered for original Windows CE "
        "GDIs with high-density audio; this does not promise every CE game works. "
        "The current adapters cover the admitted Armada, Bust-A-Move 4 and Worms "
        "profiles. Unknown runtimes are refused before handoff; no generic runtime "
        "addresses are guessed. CD music Off selects the ordinary CE loader.\n\n"
        "Complete committed source, verified dependency sources/licenses, build/audit "
        "evidence and member SHA256/CRC manifests are included. No Git history bundle, "
        "private executable, CE kernel, ARM firmware or audio sample is distributed. "
        "The blank CEX2 source map is filled from your own GDI before boot. "
        "See `docs/wince-cdda-worms-test.md`.\n").encode()
    clean_head(args.commit)
    write_zip(args.output, files)
    print(json.dumps({"path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
                      "entries": len(files), "sha256": digest(args.output.read_bytes()),
                      "source_commit": args.commit, "runtime_build": stamp, "worms_build": stamp,
                      "installed_files": [RUNTIME, INSTALL_AUDIO], "hardware_tested": False}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("worms",), default="worms")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/worms-ce-stream")
    parser.add_argument("--runtime", type=Path, default=ROOT / "build/runtime.kui")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/kui-runtime.elf")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--output", type=Path, default=DELIVERABLES / "K-UI-Worms-CE-CDDA-Test.zip")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile,
            KeyError, ImportError, json.JSONDecodeError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
