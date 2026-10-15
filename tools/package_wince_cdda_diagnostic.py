#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package matched WinCE CDDA bounded-worker modules and the audio-off loader."""
import argparse
import gzip
import json
from pathlib import Path
import subprocess

from package_armada_excerpt import check, digest, verify_build
from package_armada_ce_stream import add_evidence, inspect_stream_seed
from package_bam4_ce_stream import (BASELINE, DELIVERABLES, ROOT, RUNTIME,
    audit_stream as audit_bam4, clean_head, dependency_sources, git, json_bytes)
from package_wince_ce_stream import audit_stream as audit_worms, write_zip
from retail_package import inspect_retail
from runtime_package import flatten_elf, verify
from check_retail_loader_layout import check_directory

PROFILES = {"armada": "armada-us", "bam4": "bam4-us", "worms": "worms-us"}


def matched_package(elf, package, commit, retail=False):
    compile_info, compile_bytes = verify_build(elf, commit)
    data = package.read_bytes()
    check(len(data) <= 4 * 1024 * 1024 + 64, "Oversized package: " + str(package))
    info = inspect_retail(data, ce=True) if retail else verify(data)
    info.pop("abi", None)
    payload, memory = flatten_elf(elf.read_bytes())
    stamp = commit[:12]
    check(info["build"] == stamp and stamp.encode() + b"\0" in data[64:] and
          data[64:] == payload and info["memory_bytes"] == memory,
          "ELF/envelope/display/commit mismatch: " + str(package))
    return data, info, compile_info, compile_bytes


def build(args):
    clean_head(args.commit)
    stamp = args.commit[:12]
    files, provenance = dependency_sources(args.baseline)
    # The reference ZIP supplies licenses/sources, never an old binary.
    reference = provenance.pop("armada_rc9_preserved")
    provenance["armada_rc9_reference"] = {
        "member": reference["member"], "sha256": reference["sha256"],
        "included_in_this_package": False, "available_in_prior_zip": True}
    subprocess.run(["git", "merge-base", "--is-ancestor", provenance["source_commit"],
                    args.commit], cwd=ROOT, check=True)
    runtime, runtime_info, runtime_compile, runtime_evidence = matched_package(
        ROOT / "build/kui-runtime.elf", ROOT / "build/runtime.kui", args.commit)
    check(b"L Memory" not in runtime[64:], "Stale memory shortcut in runtime")
    files[RUNTIME] = runtime
    files["evidence/runtime-compile.json"] = runtime_evidence
    installed = [RUNTIME]
    profiles = {}
    for name, title in PROFILES.items():
        directory = ROOT / ("build/" + name + "-ce-stream")
        member = "KUI/apps/games/ce-cdda-" + name + ".kui"
        data, info, compile_info, compile_bytes = matched_package(
            directory / "entry.elf", directory / ("ce-cdda-" + name + ".kui"),
            args.commit, retail=True)
        if name == "worms":
            seed, audit = audit_worms(directory, data)
        elif name == "bam4":
            seed, audit = audit_bam4(directory, data)
        else:
            from check_armada_ce_excerpt import check as linked_audit
            seed = inspect_stream_seed(data)
            audit = linked_audit(directory, stream=True)
        check(audit["title_profile"] == title and audit["stream_profile"] is True and
              audit["distributed_pcm_bytes"] == 0 and
              audit["distributed_audio_map_bytes"] == 4096,
              "Wrong linked profile or nonblank source: " + name)
        check(audit["stream_boot_arm"]["attach_only_after_relay"] is True and
              audit["stream_boot_arm"]["preserves_controller_zero_check"] is True,
              "Missing linked bootstrap proof: " + name)
        files[member] = data
        installed.append(member)
        files["evidence/" + name + "-compile.json"] = compile_bytes
        files["evidence/" + name + "-layout-and-stack.json"] = json_bytes(audit)
        profiles[name] = {"package": member, "sha256": digest(data), "envelope": info,
                          "compile": compile_info, "seed": seed,
                          "new_binary_hardware_tested": False}
    ordinary_directory = ROOT / "build/retail-ce"
    ordinary, ordinary_info, ordinary_compile, ordinary_compile_bytes = matched_package(
        ordinary_directory / "entry.elf", ROOT / "build/ce-probe.kui", args.commit, retail=True)
    ordinary_config = (ordinary_directory / "layout-config").read_text()
    check("CE_SCI_RESUME_FIX=1" in ordinary_config and "LOW=0" in ordinary_config,
          "Ordinary CE control must retain its DMA resume fix and placement")
    check(b"ARMADA AUDIO TEST PROFILE MISMATCH" not in ordinary[64:] and
          b"CE CD MUSIC PROFILE MISMATCH" not in ordinary[64:] and b"CEX2" not in ordinary[64:],
          "A music-specific payload occupied the ordinary CE control slot")
    ordinary_member = "KUI/apps/games/ce-probe.kui"
    files[ordinary_member] = ordinary
    installed.append(ordinary_member)
    files["evidence/ordinary-ce-compile.json"] = ordinary_compile_bytes
    files["evidence/ordinary-ce-layout.json"] = json_bytes(check_directory(ordinary_directory, ce=True))
    files["evidence/ordinary-ce-config.txt"] = ordinary_config.encode()
    archive = "source/K-UI-WinCE-CDDA-Worker-Budget-" + stamp + ".tar.gz"
    files[archive] = gzip.compress(git("archive", "--format=tar",
        "--prefix=K-UI-WinCE-CDDA-Diagnostics/", args.commit), mtime=0)
    for name in ("wince-cdda-diagnostic-test.md", "wince-cdda-worms-test.md",
                 "wince-cdda-next-titles.md", "wince-cdda-refill-fix.md",
                 "wince-cdda-diagnostic-result-62687.md", "wince-cdda-worker-budget-test.md",
                 "wince-cdda-refill-console-result.md"):
        files["docs/" + name] = (ROOT / "docs" / name).read_bytes()
    for specification in args.evidence:
        add_evidence(files, specification)
    files["build.json"] = json_bytes({
        "source_commit": args.commit, "source_tree": git("rev-parse", "HEAD^{tree}").decode().strip(),
        "source_dirty": False, "installed_files": installed,
        "runtime": runtime_info, "runtime_sha256": digest(runtime),
        "runtime_compile": runtime_compile, "profiles": profiles,
        "hardware_tested": False, "experimental": True,
        "playback_retuned": True, "ordinary_ce_loader_replaced": True,
        "ordinary_ce_behavior_retuned": False,
        "ordinary_ce": {"package": ordinary_member, "envelope": ordinary_info,
                        "compile": ordinary_compile, "sha256": digest(ordinary)},
        "playing_worker_pumps_per_step": 32,
        "urgent_worker_polled_reception": True, "irq_data_dma_path_retained": True,
        "cooked_model_track_control_decoded": True,
        "terminal_pages": 4, "last_underrun_context_frozen": True,
        "added_native_clock_calls": 0, "disk_logging": False,
        "cooked_data_with_raw_audio_admitted": True,
        "cooked_audio_admitted": False, "raw_data_synthesis": False,
        "dependency_source_provenance": provenance,
        "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                  if name.endswith(".tar.gz")}})
    files["README.md"] = (f"# K-UI WinCE CDDA bounded-worker candidate\n\n"
        f"Build **{stamp}**. This new binary has not been tested on console.\n\n"
        "Merge the `KUI` folder onto the SCI card with the console off, then cold boot. "
        "This updates the matching menu, Armada/BAM4/Worms CD-music modules and ordinary "
        "audio-off CE loader. The latter prevents an old Armada test payload from blocking "
        "the audio-off comparison. Previous working binaries remain in their earlier ZIPs.\n\n"
        "The playing music worker now shares a 32-pump budget across its entire turn, "
        "roughly 16 KiB of polled reception, and publishes checked audio before sleeping. "
        "Full 16-sector requests, urgent reception, priorities, buffers and copy guards remain. "
        "Actual throughput and game pacing still require console validation.\n\n"
        "Use Worms with Background loading and CD music On in the same match where Off "
        "was smooth. Check game responsiveness and music continuity for about two minutes. "
        "Settings save separately per image. No title-screen wait is required. "
        "If a cut occurs, A+B+X+Y+Start and a 20-second recording retain all four pages; "
        "the fourth page freezes the last underrun. Existing recordings have already been read. "
        "Cooked audio and reconstruction of discarded raw DATA sectors are unsupported.\n\n"
        "See `docs/wince-cdda-worker-budget-test.md` for changes, evidence and limitations. "
        "Complete committed source, verified dependency sources/licenses, linked audits and "
        "member SHA256/CRC manifests are included. No game executable, CE kernel, "
        "sound firmware or PCM sample is distributed.\n").encode()
    clean_head(args.commit)
    write_zip(args.output, files, installed_files=installed)
    print(json.dumps({"path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
                      "sha256": digest(args.output.read_bytes()), "build": stamp,
                      "installed_files": installed, "hardware_tested": False}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--output", type=Path,
        default=DELIVERABLES / "K-UI-2.0-WinCE-CDDA-Worker-Budget-Test.zip")
    build(parser.parse_args())


if __name__ == "__main__":
    main()
