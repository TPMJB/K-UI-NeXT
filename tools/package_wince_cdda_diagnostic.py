#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package matched WinCE CDDA diagnostics and cooked-DATA admission modules."""
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
    archive = "source/K-UI-WinCE-CDDA-Diagnostics-" + stamp + ".tar.gz"
    files[archive] = gzip.compress(git("archive", "--format=tar",
        "--prefix=K-UI-WinCE-CDDA-Diagnostics/", args.commit), mtime=0)
    for name in ("wince-cdda-diagnostic-test.md", "wince-cdda-worms-test.md",
                 "wince-cdda-next-titles.md"):
        files["docs/" + name] = (ROOT / "docs" / name).read_bytes()
    for specification in args.evidence:
        add_evidence(files, specification)
    files["build.json"] = json_bytes({
        "source_commit": args.commit, "source_tree": git("rev-parse", "HEAD^{tree}").decode().strip(),
        "source_dirty": False, "installed_files": installed,
        "runtime": runtime_info, "runtime_sha256": digest(runtime),
        "runtime_compile": runtime_compile, "profiles": profiles,
        "hardware_tested": False, "experimental": True,
        "playback_retuned": False, "ordinary_ce_loader_replaced": False,
        "terminal_pages": 4, "last_underrun_context_frozen": True,
        "added_native_clock_calls": 0, "disk_logging": False,
        "cooked_data_with_raw_audio_admitted": True,
        "cooked_audio_admitted": False, "raw_data_synthesis": False,
        "dependency_source_provenance": provenance,
        "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                  if name.endswith(".tar.gz")}})
    files["README.md"] = (f"# K-UI WinCE CDDA diagnostic candidate\n\n"
        f"Build **{stamp}**. This new binary has not been tested on console.\n\n"
        "Merge the `KUI` folder onto the SCI card with the console off, then cold boot. "
        "This updates the matching menu and the Armada, BAM4 and Worms CD-music modules. "
        "Ordinary CE/native/Toy/MDK2 loaders are not replaced. Previous working binaries "
        "remain in their earlier ZIPs.\n\n"
        "Use Worms first. Choose the original image, enable Background loading and CD music, "
        "and play normally until a cut, or about three minutes. Press A+B+X+Y+Start and "
        "record **20 seconds** covering all four diagnostic pages, then power-cycle. "
        "The fourth page freezes evidence at the last underrun. No playback timing, "
        "thread priority, buffer size or scheduling quota is retuned in this candidate.\n\n"
        "For the 2048 test, select the converted GDI with cooked DATA and original raw AUDIO. "
        "Settings save per image: enable Background loading and CD music for that copy. "
        "Start a match and check playback. If blocked, photograph the complete error. "
        "Cooked audio and reconstruction of discarded raw DATA sectors are unsupported.\n\n"
        "See `docs/wince-cdda-diagnostic-test.md` for field meanings and limitations. "
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
        default=DELIVERABLES / "K-UI-2.0-WinCE-CDDA-Diagnostic1.zip")
    build(parser.parse_args())


if __name__ == "__main__":
    main()
