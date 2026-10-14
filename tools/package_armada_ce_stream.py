#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the isolated Armada full-stream test with the exact working rollback."""

import argparse
import gzip
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

from package_armada_excerpt import (check, digest, inspect_seed, safe_member,
                                    verify_build, verify_members)
from package_armada_ce_loop_alignment import DEPENDENCIES, RECOVERED_COMMIT
from retail_package import STAGE_BLOB_OFFSET, inspect_retail
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
DELIVERABLES = Path("/workspace/scratch/7a5934db4038/deliverables")
BASELINE = DELIVERABLES / "K-UI-Armada-CE-Loop-Alignment-Fix.zip"
BASELINE_COMMIT = "ecf23db0a90299793513316acb3c3bd58899bbfd"
BASELINE_ZIP_BYTES = 45645905
BASELINE_ZIP_SHA256 = "00596f13eab97c85001b3f051224c3b46d86402468825303238790bb31da520b"
BASELINE_RUNTIME_SHA256 = "675cc65c2ee4784c87fc43d0795cc57643f5e8ffcffa1a2faabbcd7536730425"
BASELINE_CE_SHA256 = "69af1c61da870a9a91c196b941aba19b6fd2188115a4f691e756189f91773b8a"
BASELINE_SOURCE = "source/K-UI-Armada-CE-Loop-Alignment-Fix-ecf23db0a902.tar.gz"
BASELINE_SOURCE_SHA256 = "c3b1044b9829bb66474093555302535c377447bb5bfa803a1c0f9a693ca001a4"
RUNTIME = "KUI/runtime.kui"
INSTALL_AUDIO = "KUI/apps/games/ce-cdda-armada.kui"
RESTORE_PREFIX = "restore-full-stream/"
DELIVERY_LIMIT = 50 * 1024 * 1024
SEED_BYTES, WIRE_BYTES = 64, 4096
STREAM_SEED = struct.pack("<16I", 0x32584543, 2, 0x8cf50040, 352800,
                          0x8cfbf000, WIRE_BYTES, 0, 352768, *([0] * 8))


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_archive(commit):
    return gzip.compress(git("archive", "--format=tar",
                             "--prefix=K-UI-Armada-CE-Full-Stream-Test/", commit), mtime=0)


def inspect_stream_seed(candidate):
    """Require one aligned CEX2 header followed by a blank complete wire map."""
    payload = candidate[64:]
    found = []
    position = payload.find(STREAM_SEED)
    while position >= 0:
        found.append(position)
        position = payload.find(STREAM_SEED, position + 1)
    check(len(found) == 1 and found[0] >= STAGE_BLOB_OFFSET and found[0] % 4 == 0,
          "The CEX2 stream seed must be unique and aligned inside the CE stage")
    offset = found[0]
    wire = payload[offset + SEED_BYTES:offset + SEED_BYTES + WIRE_BYTES]
    check(len(wire) == WIRE_BYTES and not any(wire),
          "The distributed CEX2 complete audio map must be exactly 4096 zero bytes")
    return {"format": "CEX2", "version": 2, "payload_offset": offset,
            "stage_offset": offset - STAGE_BLOB_OFFSET, "seed_bytes": SEED_BYTES,
            "wire_bytes": WIRE_BYTES, "wire_crc32": f"{zlib.crc32(wire):08x}",
            "wire_sha256": digest(wire), "wire_all_zero": True,
            "seed_map_crc32": "00000000", "distributed_pcm_bytes": 0,
            "source_buffer_address": "0x8cf50040", "source_buffer_bytes": 352800,
            "native_ring_bytes": 352768, "map_address": "0x8cfbf000"}


def verify_archived_tree(source):
    """Prove every retained working source byte and mode matches its Git commit."""
    archived = {}
    prefix = "K-UI-Armada-CE-Loop-Alignment-Fix/"
    with tarfile.open(fileobj=io.BytesIO(source), mode="r:gz") as archive:
        for member in archive.getmembers():
            if member.isdir():
                continue
            check(member.isfile() and member.name.startswith(prefix) and
                  member.size <= 16 * 1024 * 1024,
                  "Working source archive contains an unexpected file")
            name = member.name[len(prefix):]
            check(safe_member(name) and name not in archived,
                  "Working source archive has an unsafe or duplicate path")
            archived[name] = ("100755" if member.mode & 0o111 else "100644",
                              archive.extractfile(member).read())
    tree = {}
    for record in git("ls-tree", "-r", "-z", BASELINE_COMMIT).split(b"\0"):
        if not record:
            continue
        meta, path = record.split(b"\t", 1)
        mode, kind, object_id = meta.decode("ascii").split()
        name = path.decode("utf-8")
        check(kind == "blob" and safe_member(name) and name not in tree,
              "Working Git tree has an unexpected object")
        tree[name] = (mode, object_id)
    check(set(tree) == set(archived), "Working source archive and Git tree paths differ")
    names = sorted(tree)
    requested = b"".join(tree[name][1].encode("ascii") + b"\n" for name in names)
    result = subprocess.run(["git", "cat-file", "--batch"], cwd=ROOT,
                            input=requested, stdout=subprocess.PIPE, check=True).stdout
    position = 0
    for name in names:
        end = result.index(b"\n", position)
        object_id, kind, length = result[position:end].decode("ascii").split()
        position = end + 1
        data = result[position:position + int(length)]
        position += int(length)
        check(result[position:position + 1] == b"\n", "Working Git object framing differs")
        position += 1
        check(object_id == tree[name][1] and kind == "blob" and
              (tree[name][0], data) == archived[name],
              "Working source byte or mode differs: " + name)
    check(position == len(result), "Unexpected trailing working Git object data")
    return {"source_commit": BASELINE_COMMIT, "source_archive": BASELINE_SOURCE,
            "source_archive_sha256": BASELINE_SOURCE_SHA256,
            "source_tree": git("rev-parse", BASELINE_COMMIT + "^{tree}").decode().strip(),
            "source_file_count": len(names), "all_archived_bytes_and_modes_match": True}


def validate_baseline(path):
    data = path.read_bytes()
    check(len(data) == BASELINE_ZIP_BYTES and digest(data) == BASELINE_ZIP_SHA256,
          "Baseline ZIP differs from the exact working ecf23 delivery")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = verify_members(archive)
        previous = json.loads(archive.read("build.json"))
        check(previous["source_commit"] == BASELINE_COMMIT and
              previous["installed_files"] == [RUNTIME, INSTALL_AUDIO] and
              previous["runtime_replaced"] is True and
              previous["ce_audio_enabled"] is True and
              previous["full_track_streaming"] is False and
              previous["universal_ce_support"] is False and
              previous["ordinary_ce_payload_replaced"] is False and
              previous["native_mdk2_toy_payloads_replaced"] is False,
              "Working delivery source or scope metadata differs")
        check(sorted(name for name in archive.namelist() if name.startswith("KUI/")) ==
              sorted([RUNTIME, INSTALL_AUDIO]), "Working delivery installs unexpected files")
        runtime = archive.read(RUNTIME)
        runtime_info = verify(runtime)
        check(len(runtime) == 1782092 and digest(runtime) == BASELINE_RUNTIME_SHA256 and
              previous["runtime_sha256"] == BASELINE_RUNTIME_SHA256 and
              runtime_info == previous["runtime"] and runtime_info == {
                  "format": 1, "build": BASELINE_COMMIT[:12], "payload_bytes": 1782028,
                  "memory_bytes": 5679280, "crc32": "b07270f0"} and
              b"L Memory" not in runtime[64:], "Working menu bytes differ")
        audio = archive.read(INSTALL_AUDIO)
        audio_info = inspect_retail(audio, ce=True)
        audio_info.pop("abi", None)
        check(len(audio) == 449804 and digest(audio) == BASELINE_CE_SHA256 and
              previous["ce_sha256"] == BASELINE_CE_SHA256 and
              audio_info == previous["ce"] and audio_info["build"] == BASELINE_COMMIT[:12] and
              audio_info["crc32"] == "b17d4f52" and
              inspect_seed(audio) == previous["excerpt_placeholder"],
              "Working optional loader bytes or blank excerpt differ")
        source = archive.read(BASELINE_SOURCE)
        check(digest(source) == BASELINE_SOURCE_SHA256 and
              previous["source_archive_sha256"][BASELINE_SOURCE] == BASELINE_SOURCE_SHA256,
              "Working source archive provenance differs")
        recovery = previous["source_recovery"]
        check(recovery["recovered_baseline_commit"] == RECOVERED_COMMIT and
              recovery["all_archived_file_bytes_and_modes_match"] is True and
              recovery["recovered_baseline_tree"] ==
              git("rev-parse", RECOVERED_COMMIT + "^{tree}").decode().strip(),
              "Recovered original source-tree identity differs")
        old_bundle = previous["source_git_bundle"]
        check(old_bundle["member"] in members and
              digest(archive.read(old_bundle["member"])) == old_bundle["sha256"] and
              len(archive.read(old_bundle["member"])) == old_bundle["bytes"],
              "Working source Git bundle identity differs")
        files = {RESTORE_PREFIX + RUNTIME: runtime,
                 RESTORE_PREFIX + INSTALL_AUDIO: audio, BASELINE_SOURCE: source,
                 "evidence/previous-working/build.json": archive.read("build.json"),
                 "evidence/previous-working/SHA256SUMS": archive.read("SHA256SUMS"),
                 "evidence/previous-working/MEMBERS.json": archive.read("MEMBERS.json"),
                 "source/baseline/package_armada_ce_loop_alignment.py":
                     archive.read("source/package_armada_ce_loop_alignment.py")}
        dependencies = set()
        for name in archive.namelist():
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/", "source/baseline/")):
                check(name not in files, "Duplicate inherited source/license member")
                value = archive.read(name)
                if name.startswith("source/dependencies/"):
                    check(name in DEPENDENCIES and digest(value) == DEPENDENCIES[name] and
                          previous["source_archive_sha256"][name] == members[name],
                          "Dependency source identity differs: " + name)
                    dependencies.add(name)
                files[name] = value
            elif name.startswith("evidence/"):
                # Preserve every prior source/GPL/build proof without overwriting
                # the current compilation or stream audit records.
                inherited = "evidence/previous-working/" + name
                check(inherited not in files, "Duplicate inherited evidence member")
                files[inherited] = archive.read(name)
        check(dependencies == set(DEPENDENCIES) and "LICENSE" in files and
              "THIRD_PARTY.md" in files, "Working delivery licenses/sources are incomplete")
    return files, previous, runtime_info, audio_info, source


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
    """Reject changed ZIP identity, paths and every field/blank byte in CEX2."""
    checks = 0

    def rejects(operation, label):
        nonlocal checks
        try:
            operation()
        except (ValueError, KeyError, zipfile.BadZipFile):
            checks += 1
            return
        raise ValueError("Packaging corruption check did not reject " + label)

    def make_zip(items, manifest):
        out = io.BytesIO()
        with zipfile.ZipFile(out, "w") as archive:
            for name, data in items:
                archive.writestr(name, data)
            archive.writestr("SHA256SUMS", manifest)
        return out.getvalue()

    def members(data):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            return verify_members(archive)

    good = make_zip([("proof.txt", b"good")], digest(b"good") + "  proof.txt\n")
    check(members(good) == {"proof.txt": digest(b"good")}, "Positive member control failed")
    checks += 1
    rejects(lambda: members(make_zip([("proof.txt", b"bad")],
                                     digest(b"good") + "  proof.txt\n")), "SHA mismatch")
    broken = bytearray(good)
    with zipfile.ZipFile(io.BytesIO(good)) as archive:
        member = archive.getinfo("proof.txt")
        name_size, extra_size = struct.unpack_from("<HH", good, member.header_offset + 26)
        broken[member.header_offset + 30 + name_size + extra_size] ^= 1
    rejects(lambda: members(bytes(broken)), "CRC mismatch")
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        duplicate = make_zip([("proof.txt", b"good"), ("proof.txt", b"good")],
                             digest(b"good") + "  proof.txt\n")
    rejects(lambda: members(duplicate), "duplicate member")
    for name in ("../proof.txt", "/proof.txt", "a\\proof.txt", "a/../proof.txt"):
        rejects(lambda name=name: members(make_zip([(name, b"good")],
                                digest(b"good") + "  " + name + "\n")), "unsafe path")
    rejects(lambda: members(make_zip([("proof.txt", b"good")], "")), "incomplete SHA coverage")
    rejects(lambda: members(make_zip([("proof.txt", b"good")],
             (digest(b"good") + "  proof.txt\n") * 2)), "duplicate SHA entry")
    record = json.dumps({"proof.txt": {"bytes": 5, "sha256": digest(b"good"),
                                      "crc32": "00000000"}}).encode()
    rejects(lambda: members(make_zip([("proof.txt", b"good"), ("MEMBERS.json", record)],
             digest(b"good") + "  proof.txt\n" + digest(record) + "  MEMBERS.json\n")),
             "size/CRC metadata mismatch")
    offset = 64 + STAGE_BLOB_OFFSET
    seed = STREAM_SEED + bytes(WIRE_BYTES)
    candidate = bytes(offset) + seed
    check(inspect_stream_seed(candidate)["wire_all_zero"], "Positive CEX2 control failed")
    checks += 1
    for at in range(SEED_BYTES):
        changed = bytearray(candidate)
        changed[offset + at] ^= 1
        rejects(lambda changed=changed: inspect_stream_seed(bytes(changed)), "CEX2 header field")
    for at in (0, WIRE_BYTES // 2, WIRE_BYTES - 1):
        changed = bytearray(candidate)
        changed[offset + SEED_BYTES + at] = 1
        rejects(lambda changed=changed: inspect_stream_seed(bytes(changed)), "nonblank wire map")
    rejects(lambda: inspect_stream_seed(candidate[:-1]), "truncated wire map")
    rejects(lambda: inspect_stream_seed(candidate + seed), "duplicate CEX2 seed")
    rejects(lambda: inspect_stream_seed(bytes(offset + 1) + seed), "unaligned CEX2 seed")
    rejects(lambda: inspect_stream_seed(bytes(64) + seed), "CEX2 outside CE stage")
    return checks


def build(args):
    check(re.fullmatch(r"[0-9a-f]{40}", args.commit) is not None,
          "--commit requires a full lowercase commit hash")
    check(git("rev-parse", "HEAD").decode().strip() == args.commit and
          not git("status", "--porcelain"), "Commit all final source changes before packaging")
    for ancestor in (RECOVERED_COMMIT, BASELINE_COMMIT):
        subprocess.run(["git", "merge-base", "--is-ancestor", ancestor, args.commit],
                       cwd=ROOT, check=True)
    stamp = args.commit[:12]
    directory = args.directory.resolve()
    elf, package = directory / "entry.elf", directory / "ce-cdda-armada.kui"
    ce_compile, ce_compile_data = verify_build(elf, args.commit)
    runtime_elf, runtime_package = args.elf.resolve(), args.runtime.resolve()
    runtime_compile, runtime_compile_data = verify_build(runtime_elf, args.commit)
    check(package.stat().st_size <= 4 * 1024 * 1024 + 64 and
          runtime_package.stat().st_size <= 4 * 1024 * 1024 + 64,
          "Runtime or CE stream package exceeds its envelope size limit")
    audio, runtime = package.read_bytes(), runtime_package.read_bytes()
    audio_info, runtime_info = inspect_retail(audio, ce=True), verify(runtime)
    audio_info.pop("abi", None)
    for data, built, info, label in ((audio, elf, audio_info, "CE stream"),
                                     (runtime, runtime_elf, runtime_info, "menu")):
        payload, memory = flatten_elf(built.read_bytes())
        check(info["build"] == stamp and stamp.encode() + b"\0" in data[64:] and
              data[64:] == payload and info["memory_bytes"] == memory,
              label + " envelope/ELF/display/commit identities differ")
    check(b"L Memory" not in runtime[64:], "Stale Diagnostics shortcut remains in new menu")
    seed = inspect_stream_seed(audio)
    from check_armada_ce_excerpt import check as check_audio
    audit = check_audio(directory, stream=True)
    boot_arm = audit.get("stream_boot_arm", {})
    check(boot_arm.get("entry_slot") == 52 and
          boot_arm.get("preserves_controller_zero_check") is True and
          boot_arm.get("attach_only_after_relay") is True and
          boot_arm.get("bootstrap_audio_and_bus_gated") is True,
          "Stream bootstrap arm and pristine-state checks were not audited")
    owner_reader = audit.get("native_owner_reader", {})
    check(owner_reader.get("complete_owner_reader_bodies_pinned") == 10 and
          owner_reader.get("native_owner_va_sample_only") is True and
          owner_reader.get("worker_cache_array_observer_absent") is True and
          owner_reader.get("irq_mask_covers_both_walks_and_native_sample") is True and
          owner_reader.get("entire_saved_sr_restored") is True and
          owner_reader.get("sr_stability_mask") == "0x700000f0" and
          owner_reader.get("arithmetic_status_flags_do_not_refuse") is True,
          "Native owner memory read and complete IRQ/SR contract were not audited")
    check(audit["distributed_pcm_bytes"] == 0 and
          audit["distributed_audio_map_bytes"] == WIRE_BYTES and
          int(audit["seed_address"], 16) - 0x8ce10000 == seed["stage_offset"],
          "CEX2 seed and linked stream audit disagree")
    files, previous, restore_runtime, restore_audio, source = validate_baseline(args.baseline)
    archive_proof = verify_archived_tree(source)
    checks = corruption_checks()
    current_source = "source/K-UI-Armada-CE-Full-Stream-Test-" + stamp + ".tar.gz"
    files.update({RUNTIME: runtime, INSTALL_AUDIO: audio,
                  current_source: source_archive(args.commit),
                  "source/package_armada_ce_stream.py": Path(__file__).read_bytes(),
                  "docs/armada-ce-full-stream-test.md":
                      (ROOT / "docs/armada-ce-full-stream-test.md").read_bytes(),
                  "evidence/runtime-compile.json": runtime_compile_data,
                  "evidence/ce-compile.json": ce_compile_data,
                  "evidence/working-source-archive.json":
                      (json.dumps(archive_proof, indent=2) + "\n").encode(),
                  "evidence/stream-layout-and-stack.json":
                      (json.dumps(audit, indent=2) + "\n").encode()})
    bundle_member = "source/K-UI-Armada-CE-Full-Stream-Test-" + stamp + ".bundle"
    with tempfile.TemporaryDirectory(prefix="kui-full-stream-source-") as temporary:
        bundle = Path(temporary) / "source.bundle"
        subprocess.run(["git", "bundle", "create", str(bundle), "HEAD"], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["git", "bundle", "verify", str(bundle)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        files[bundle_member] = bundle.read_bytes()
    for specification in args.evidence:
        add_evidence(files, specification)
    info = {"source_commit": args.commit, "source_recovery": previous["source_recovery"],
            "working_source_archive_verification": archive_proof,
            "source_git_bundle": {"member": bundle_member, "bytes": len(files[bundle_member]),
                                  "sha256": digest(files[bundle_member])},
            "runtime": runtime_info, "runtime_sha256": digest(runtime),
            "runtime_compile": runtime_compile, "ce": audio_info,
            "ce_sha256": digest(audio), "ce_compile": ce_compile,
            "installed_files": [RUNTIME, INSTALL_AUDIO], "runtime_replaced": True,
            "restore_files": [RESTORE_PREFIX + RUNTIME, RESTORE_PREFIX + INSTALL_AUDIO],
            "restore_runtime_source_commit": BASELINE_COMMIT, "restore_runtime": restore_runtime,
            "restore_runtime_sha256": BASELINE_RUNTIME_SHA256,
            "restore_ce_source_commit": BASELINE_COMMIT, "restore_ce": restore_audio,
            "restore_ce_sha256": BASELINE_CE_SHA256,
            "baseline_zip_bytes": BASELINE_ZIP_BYTES, "baseline_zip_sha256": BASELINE_ZIP_SHA256,
            "hardware_tested": False, "runtime_hardware_tested": False,
            "ce_hardware_tested": False, "ce_audio_enabled": True,
            "full_track_streaming": True, "universal_ce_support": False,
            "ce_audio_mode": "experimental full-track streaming through the verified CE sound owner",
            "scope": "Original US Armada CD audio full-stream SCI test; matching menu and optional loader",
            "ui_update": {"diagnostics_L_Memory_shortcut_removed": True,
                          "saved_options_and_game_library_preserved": True,
                          "GitHub_link_beneath_logo_retained": True},
            "binding_admission": previous["binding_admission"],
            "mapped_word_read_policy": {
                "worker": "verified native owner VA; complete IRQ-masked transaction",
                "witnesses": "owner process/slot/handle and software mapping before and after",
                "physical_alias_object_sampling": False,
                "entry_binder_cache_observer_preserved": True},
            "legacy_mapped_word_read_policy": previous["mapped_word_read_policy"],
            "audio_handoff_policy": previous["audio_handoff_policy"],
            "admission": previous["admission"], "pool": previous["pool"],
            "audio_source_map": {"format": "CEX2", "complete_wire_bytes": WIRE_BYTES,
                                 "separate_from_data_resident_manifest": True,
                                 "filled_from_selected_original_GDI_before_boot": True,
                                 "distributed_pcm_bytes": 0},
            "stream_placeholder": seed, "layout_and_controller": audit,
            "bootstrap_correction": boot_arm,
            "failure_cleanup": "revoke scoped audio jobs; fence cancelled reception without new reads",
            "native_owner_reader": owner_reader,
            "ordinary_ce_payload_replaced": False, "native_mdk2_toy_payloads_replaced": False,
            "preserved_ce_source_commit": previous["preserved_ce_source_commit"],
            "preserved_ce_sha256": previous["preserved_ce_sha256"],
            "unchanged_loader_sha256": previous["unchanged_loader_sha256"],
            "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                      if name.endswith(".tar.gz")},
            "build_commands": ["source .deps/kos/environ.sh",
                "make -B -j2 -f Makefile.dc build/kui-runtime.elf BUILD_ID=" + stamp,
                "python3 tools/runtime_package.py build build/kui-runtime.elf "
                "build/runtime.kui --build-id " + stamp,
                "make -B -j2 -f Makefile.armada-stream build/armada-ce-stream/entry.elf BUILD_ID=" + stamp,
                "python3 tools/runtime_package.py build build/armada-ce-stream/entry.elf "
                "build/armada-ce-stream/ce-cdda-armada.kui --build-id " + stamp,
                "python3 tools/check_armada_ce_excerpt.py --stream build/armada-ce-stream",
                "python3 tools/package_armada_ce_stream.py --commit " + args.commit +
                " --runtime " + str(args.runtime) + " --elf " + str(args.elf) +
                " --directory " + str(args.directory) + " --baseline " + str(args.baseline)],
            "verification": {"both_packages_match_clean_commit_elf_and_compiled_build_id": True,
                "exact_working_zip_and_all_member_sizes_sha256_crc32": True,
                "exact_working_source_bytes_and_modes_match_git": True,
                "two_file_rollback_matches_exact_working_bytes": True,
                "stream_linked_layout_stack_bridge_and_seed_checked": True,
                "stream_bootstrap_pristine_until_postcheck_arm": True,
                "unique_aligned_CEX2_with_blank_complete_wire_and_no_distributed_pcm": True,
                "packaging_corruption_checks": checks,
                "output_member_sizes_sha256_crc32_verified": True}}
    files["build.json"] = (json.dumps(info, indent=2) + "\n").encode()
    files["README.md"] = f"""# Armada CE full CD music stream test

Menu and optional audio loader build **{stamp}**. Merge this ZIP's `KUI` folder
onto the SCI microSD card with the console off, replacing **both**
`{RUNTIME}` and `{INSTALL_AUDIO}`, then cold boot.

Select **Original Armada → Y Options → CD music On → START**. This test streams
the requested audio tracks from your image; it is not the earlier two-second
excerpt loop. Recording62654 confirms repeated native Play with audible music,
but the measured source supply is about75kB/s where176.4kB/s is required.
This build gives low-buffer music an urgent64-block audio grant instead of8,
then restores normal grants after1.5seconds of fresh PCM is available. DATA
keeps its16-block grant and immediate foreground preemption. Each pump still
handles one block. The working startup, priority2,16-sector reads, recovery
and shared DATA reader are retained.

Use the same in-game scene where the earlier loop was audible. No main-menu
wait or gameplay video is requested. Return with **A+B+X+Y+Start** after music
fails, or after about 30 seconds if it continues. Record one complete
**15-second cycle** of the three diagnostic pages, then power-cycle.
The pages repeat every five seconds until power-off.

The event page now also records discovery5, owner binding6 and worker start7,
even before streaming starts. These startup rows have MS0; AUX0 means success.
Running history records native timestamps, source bytes and fresh ring bytes.
Sample AUX bit2 marks urgent audio grants, so the next trace shows whether
priority engaged while the source budget and fresh buffer changed. It also retains start/underrun/fault events and longest worker/source-job
intervals. Recording62654 reached native playback with no native refusals and measured
underruns. Sustained playback with this new arbitration remains unverified;
FMV echo is still unresolved.

This is a console test of the corrected full-stream path. Build and host checks
do not establish hardware playback; other CE games are not admitted. No new
extraction, private upload or 2048 run is needed. The menu keeps saved options,
the GitHub link beneath the logo and the removed unused L Memory shortcut.

**CD music Off** selects the unchanged ordinary CE loader. To restore the last
working excerpt exactly, merge `restore-full-stream/KUI` onto the card with
the console off and cold boot. Both backup files are exact build
**{BASELINE_COMMIT[:12]}**.

Current and working ecf23 source archives, complete recovered Git history,
dependency sources/licenses, prior GPL provenance, build evidence and member
SHA256/CRC checks are included. The current loader distributes only a blank
CEX2 seed/map, not game audio. No private executable, kernel or ARM firmware
is included. See `docs/armada-ce-full-stream-test.md`.
""".encode()
    check([name for name in files if name.startswith("KUI/")] == [RUNTIME, INSTALL_AUDIO],
          "Only the requested menu runtime and optional audio loader may be installed")
    check(sorted(name for name in files if name.startswith(RESTORE_PREFIX)) ==
          sorted([RESTORE_PREFIX + RUNTIME, RESTORE_PREFIX + INSTALL_AUDIO]),
          "Rollback must contain exactly the two working installed files")
    files["MEMBERS.json"] = (json.dumps({name: {"bytes": len(data), "sha256": digest(data),
                                               "crc32": f"{zlib.crc32(data):08x}"}
                                        for name, data in sorted(files.items())}, indent=2) + "\n").encode()
    files["SHA256SUMS"] = "".join(digest(data) + "  " + name + "\n"
                                    for name, data in sorted(files.items())).encode()
    check(not git("status", "--porcelain") and
          git("rev-parse", "HEAD").decode().strip() == args.commit, "Source changed while packaging")
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
                      "source_commit": args.commit, "runtime_build": stamp, "ce_stream_build": stamp,
                      "restore_runtime_build": BASELINE_COMMIT[:12],
                      "restore_ce_build": BASELINE_COMMIT[:12]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/armada-ce-stream")
    parser.add_argument("--runtime", type=Path, default=ROOT / "build/runtime.kui")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/kui-runtime.elf")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--output", type=Path, default=DELIVERABLES / "K-UI-Armada-CE-Low-Buffer-Audio-Fix.zip")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile,
            KeyError, ImportError, json.JSONDecodeError, tarfile.TarError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
