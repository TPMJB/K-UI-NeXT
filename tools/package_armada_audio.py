#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the isolated Armada CE audio-request observer and exact CE restore."""

import argparse
import gzip
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DELIVERABLES = ROOT.parent / "deliverables"
CONTROL_COMMIT = "fd3bde28d5388ab3e08bcf18167e331a0da0d69d"
CONTROL_SHA256 = "42e378a7af567c751d9413ef03c6f8509762e90c0001f75a4648a7fba04c4198"
CONTROL_BYTES = 82544
INSTALL_MEMBER = "KUI/apps/games/ce-probe.kui"
sys.path.insert(0, str(ROOT / "tools"))
from runtime_package import flatten_elf, verify  # noqa: E402
from retail_package import inspect_retail  # noqa: E402
from check_retail_loader_layout import check_directory  # noqa: E402
from check_armada_ce_bind import check as check_bind  # noqa: E402


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check(condition, message):
    if not condition:
        raise ValueError(message)


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_archive(commit, prefix):
    return gzip.compress(git("archive", "--format=tar", "--prefix=" + prefix,
                             commit), mtime=0)


def add_evidence(files, specification):
    if "=" not in specification:
        raise ValueError("Evidence must have the form NAME=PATH")
    name, path = specification.split("=", 1)
    parsed = PurePosixPath(name)
    check(bool(name) and not parsed.is_absolute() and
          all(part not in ("", ".", "..") for part in parsed.parts) and
          "\\" not in name, "Invalid evidence member name")
    member = "evidence/" + name
    check(member not in files, "Duplicate evidence member: " + member)
    source = Path(path)
    check(0 < source.stat().st_size <= 4 * 1024 * 1024,
          "Evidence must contain at most 4 MiB: " + str(source))
    check(source.suffix.lower() in (".json", ".log", ".txt", ".md"),
          "Evidence must be JSON or text, not a private input binary")
    files[member] = source.read_bytes()


def build(args):
    args.elf=args.elf.resolve()
    args.payload=args.payload.resolve()
    args.output=args.output.resolve()
    check(re.fullmatch(r"[0-9a-f]{40}", args.commit) is not None,
          "--commit requires a full lowercase commit hash")
    check(git("rev-parse", "HEAD").decode().strip() == args.commit,
          "Requested commit does not match HEAD")
    check(not git("status", "--porcelain"),
          "Commit all source changes before packaging")
    stamp = args.commit[:12]
    check(args.payload.stat().st_size <= 4 * 1024 * 1024 + 64,
          "CE package exceeds its envelope size limit")
    candidate = args.payload.read_bytes()
    candidate_info = verify(candidate)
    check(candidate_info["build"] == stamp,
          "Candidate build does not match the source commit")
    candidate_contract = inspect_retail(candidate, ce=True)
    candidate_contract.pop("abi", None)  # Historical generic label is not this test's scope.
    elf_payload, elf_memory = flatten_elf(args.elf.read_bytes())
    check(candidate[64:] == elf_payload and
          candidate_info["memory_bytes"] == elf_memory,
          "Candidate package does not match the built ELF")
    layout = check_directory(args.elf.parent, ce=True)
    binding_layout = check_bind(args.elf.parent) if args.bindings else None
    check(not args.table_trace or args.bindings,"--table-trace requires --bindings")
    label = "Page-Tables" if args.table_trace else "Sound-Bindings" if args.bindings else "Audio-Requests"
    document = "armada-ce-page-tables-test.md" if args.table_trace else "armada-ce-sound-bindings-test.md" if args.bindings else "armada-ce-audio-observe-test.md"

    files = {
        INSTALL_MEMBER: candidate,
        "docs/" + document: (ROOT / "docs" / document).read_bytes(),
        "docs/evidence/armada-ce-audio-2026-10-13.json":
            (ROOT / "docs/evidence/armada-ce-audio-2026-10-13.json").read_bytes(),
        "docs/evidence/armada-game-audio-2026-10-13.json":
            (ROOT / "docs/evidence/armada-game-audio-2026-10-13.json").read_bytes(),
        "source/K-UI-Armada-" + label + "-" + stamp + ".tar.gz":
            source_archive(args.commit, "K-UI-Armada-" + label + "/"),
        "source/K-UI-Armada-CE-DMA-" + CONTROL_COMMIT[:12] + ".tar.gz":
            source_archive(CONTROL_COMMIT, "K-UI-Armada-CE-DMA/"),
        "source/package_armada_audio.py": Path(__file__).read_bytes(),
        "evidence/layout-and-stack.json":
            (json.dumps(layout, indent=2) + "\n").encode(),
    }
    with zipfile.ZipFile(args.baseline) as prior:
        check(prior.testzip() is None, "Prior Armada ZIP failed its CRC check")
        check(len(prior.namelist()) == len(set(prior.namelist())),
              "Prior Armada ZIP contains duplicate member names")
        control = prior.read(INSTALL_MEMBER)
        control_info = verify(control)
        check(len(control) == CONTROL_BYTES and digest(control) == CONTROL_SHA256 and
              control_info["build"] == CONTROL_COMMIT[:12],
              "Prior CE payload does not match the installed DMA-corrected build")
        inspect_retail(control, ce=True)
        files["restore-ce/" + INSTALL_MEMBER] = control
        for name in prior.namelist():
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                files[name] = prior.read(name)

    for specification in args.evidence:
        add_evidence(files, specification)
    info = {
        "source_commit": args.commit,
        "ce_build": stamp,
        "ce": candidate_contract,
        "ce_sha256": digest(candidate),
        "control_source_commit": CONTROL_COMMIT,
        "control": control_info,
        "control_sha256": digest(control),
        "control_is_exact_prior_payload": True,
        "hardware_tested": False,
        "audio_enabled": False,
        "scope": "Original US Armada CE SCI-background audio request observation",
        "installed_files": [INSTALL_MEMBER],
        "runtime_replaced": False,
        "native_mdk2_toy_payloads_replaced": False,
        "reader_scheduling_changed": False,
        "observer_overhead": "Small GD-dispatch bookkeeping; console impact unmeasured",
        "ce_sci_resume_fix": 1,
        "armada_ce_audio_observe": 1,
        "armada_ce_ram_bind": int(args.bindings),
        "armada_ce_table_trace": int(args.table_trace),
        "binding_controller": binding_layout,
        "admission": {
            "product": "T40301N 00",
            "bootfile": "0WINCEOS.BIN",
            "kernel_bytes": 1253376,
            "kernel_sha256": "a7628c78090fce2c93012d5e4db6142096e138dceb7ebbf555142e902ca03c1f",
            "ip_crc32": "684f8040",
            "prefix_crc32": "6200729c",
            "body_crc32": "b9f755bc",
            "track_start_lbas": [0, 756, 45000, 149842, 434062],
            "track_controls": [4, 0, 4, 0, 4],
            "sector_bytes": 2352,
            "track_file_offsets": [0, 0, 0, 0, 0],
            "storage": "SCI", "reader": "ASYNC",
        },
        "static_play_parameters": [4, 4, 15],
        "layout": layout,
        "build_commands": [
            "source .deps/kos/environ.sh",
            "make -j2 -f " + ("Makefile.armada-bind" if args.bindings else "Makefile.armada-audio") +
            " " + str(args.elf.relative_to(ROOT)) + " BUILD_ID=" + stamp,
            "python3 tools/runtime_package.py build " + str(args.elf.relative_to(ROOT)) +
            " " + str(args.payload.relative_to(ROOT)) + " --build-id " + stamp,
            "python3 tools/check_retail_launcher_package.py --ce " + str(args.payload.relative_to(ROOT)),
            "python3 tools/package_armada_audio.py --commit " + args.commit +
            (" --bindings " + ("--table-trace " if args.table_trace else "") + "--elf " + str(args.elf.relative_to(ROOT)) + " --payload " +
             str(args.payload.relative_to(ROOT)) + " --output " + str(args.output) if args.bindings else ""),
        ],
        "source_archive_sha256": {
            name: digest(data) for name, data in files.items()
            if name.endswith(".tar.gz")
        },
        "verification": {
            "candidate_envelope_and_crc": True,
            "candidate_matches_elf": True,
            "blank_card_manifest_and_ce_relocation": True,
            "resident_layout_and_stack": True,
            "source_commit_matches_clean_head": True,
            "exact_prior_ce_payload_sha256": True,
        },
    }
    files["build.json"] = (json.dumps(info, indent=2) + "\n").encode()
    files["README.md"] = f"""# Armada audio requests test

CE loader build: **{stamp}**. This first live observer records Armada's CD audio
requests. It retains the DMA fix and does not enable CD music playback.

1. With the console off, back up `KUI/apps/games/ce-probe.kui`.
2. Merge this ZIP's KUI folder onto the card. It replaces only that CE payload.
3. Cold boot on SCI microSD. Select **Original Armada** and launch with **A**
   through the normal confirmation. Leave the CD audio option off.
4. Stay at the main menu for **30 seconds**, then start a game and play for
   about **two minutes**. Note whether effects still work and FMVs look the same.
5. Return with **A+B+X+Y+Start**. Send **one full-screen photograph** containing
   BUILD, GUARD and all four new audio rows.

CD music remains absent in this build. Accepted PLAY requests alone do not
mean audio played. The preserved request parameters will tell us whether the
verified CE device path reaches the loader before implementing CE playback.
Static game calls request track 4–4 with repeat field 15; the live test verifies
that flow. It does not alter sound memory, PCM ports, the CE timer or GD replies.
Reader scheduling is unchanged; only small observer bookkeeping is added.

Merge `restore-ce/KUI` onto the card and cold boot for exact restoration of
the prior DMA-corrected CE loader **{CONTROL_COMMIT[:12]}**. Your own backup
restores the CE file present immediately before this test. The menu runtime,
general native, MDK2 and Toy Commander payloads are not replaced by this ZIP.

This package is limited to the verified original US raw Armada image. Other
images stop with ARMADA AUDIO TEST PROFILE MISMATCH. No new runtime or CD is
needed. No game, CE kernel, ARM driver, music or private startup ZIP is included.

See `docs/armada-ce-audio-observe-test.md` for the row meanings. Source,
dependency sources, audit metadata, validation evidence, licenses and checksums
are included. Console acceptance is not yet established.
""".encode()
    if args.table_trace:
        info["scope"]="Original US Armada CE SCI-background bounded page-table checkpoints"
        info["observer_overhead"]="At most 64 two-level traces / 256 read-only RAM accesses per accepted PLAY; unmeasured"
        files["build.json"]=(json.dumps(info,indent=2)+"\n").encode()
        files["README.md"] = f"""# Armada page-table checkpoints

CE loader build: **{stamp}**. This observer records the raw table entries that
stopped the prior sound-pointer lookup. It retains the DMA fix and adds no music.

1. With the console off, back up `KUI/apps/games/ce-probe.kui`.
2. Merge this ZIP's `KUI` folder onto the card. Only that CE file is replaced.
3. Cold boot, launch **Original Armada with A**, and leave CD audio off.
4. Wait **30 seconds** in the menu, then play for **two minutes**.
5. Return with **A+B+X+Y+Start** and send **one full-screen photograph** including
   BUILD, GUARD and the four new rows. Mention any new effects/video problem.

No CD music is expected. Only bounded P1 main-RAM table words are read; block
pointers, page entries and sound holders are never followed. No CE APIs,
allocations, cache maintenance or audio/timer/interrupt writes are added.
See `docs/{document}` for the raw-entry/status/mask meanings.

`restore-ce/KUI` restores exact DMA-corrected CE build **{CONTROL_COMMIT[:12]}**
after a cold boot. Your backup restores the immediately preceding file.
Menu/native/MDK2/Toy files are not installed. Source, evidence, dependency
sources/licenses and checksums are included; private game/kernel/audio bytes
are excluded. This candidate has not yet been tested on the console.
""".encode()
    if args.bindings and not args.table_trace:
        info["scope"] = "Original US Armada CE SCI-background sound binding observation"
        info["observer_overhead"] = "At most eight bounded RAM bindings per accepted PLAY; impact unmeasured"
        files["build.json"] = (json.dumps(info, indent=2) + "\n").encode()
        files["README.md"] = f"""# Armada sound bindings test

CE loader build: **{stamp}**. This observer keeps the DMA fix and resolves CE's
live sound holder mappings. It does not enable CD music or call sound APIs.

1. With the console off, back up `KUI/apps/games/ce-probe.kui`.
2. Merge this ZIP's `KUI` folder onto the card. Only that CE file is replaced.
3. Cold boot, choose **Original Armada**, and launch with **A** through the
   normal confirmation. Leave CD audio off.
4. Stay in the main menu about **30 seconds**, then play for **two minutes**.
5. Return with **A+B+X+Y+Start** and send **one full-screen photograph** of BUILD,
   GUARD and the four binding rows. Mention any new problem with effects/video.

No music is expected. A status digit 9 means a mapping was resolved but its
value was deliberately skipped because physical-alias cache coherence was
not established. This is not an audio error or permission to take sound RAM.
No G2/AICA, timer or interrupt writes, allocations or CE API calls are added.
This build does not promise improved FMVs. See `docs/{document}` for details.

`restore-ce/KUI` restores the exact prior DMA-corrected CE build
**{CONTROL_COMMIT[:12]}** after a cold boot. Your own backup restores the file
installed immediately before this test. Native, MDK2, Toy and menu files are
not installed by this ZIP. Source, validation evidence, dependency sources,
licenses and checksums are included; private game/kernel/audio bytes are not.
""".encode()
    check([name for name in files if name.startswith("KUI/")] == [INSTALL_MEMBER],
          "Only the CE observer payload may appear in the installation folder")
    files["SHA256SUMS"] = "".join(
        digest(data) + "  " + name + "\n" for name, data in sorted(files.items())
    ).encode()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=args.output.name + ".",
                                         suffix=".partial", dir=args.output.parent,
                                         delete=False) as handle:
            temporary = Path(handle.name)
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=False) as archive:
            for name, data in sorted(files.items()):
                member = zipfile.ZipInfo(name, (2026, 10, 13, 0, 0, 0))
                member.external_attr = 0o100644 << 16
                member.compress_type = (zipfile.ZIP_STORED if name.endswith(".tar.gz")
                                        else zipfile.ZIP_DEFLATED)
                archive.writestr(member, data)
        with zipfile.ZipFile(temporary) as archive:
            check(archive.testzip() is None and len(archive.infolist()) == len(files),
                  "Output ZIP failed its CRC check")
            for name, data in files.items():
                check(archive.read(name) == data, "Output member differs: " + name)
        check(temporary.stat().st_size < 50 * 1024 * 1024,
              "Output ZIP exceeds the 50 MiB delivery limit")
        temporary.replace(args.output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(json.dumps({
        "path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
        "entries": len(files), "sha256": digest(args.output.read_bytes()),
        "source_commit": args.commit, "ce_build": stamp,
    }, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--payload", type=Path,
                        default=ROOT / "build/armada-ce-audio/ce-probe.kui")
    parser.add_argument("--elf", type=Path,
                        default=ROOT / "build/armada-ce-audio/entry.elf")
    parser.add_argument("--baseline", type=Path,
                        default=DELIVERABLES / "K-UI-Armada-CE-Reader-Test.zip")
    parser.add_argument("--output", type=Path,
                        default=DELIVERABLES / "K-UI-Armada-Audio-Requests-Test.zip")
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--bindings", action="store_true",
                        help="Package the isolated retained RAM-binding observer")
    parser.add_argument("--table-trace",action="store_true",
                        help="Package the bounded raw page-table checkpoint variant")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError,
            zipfile.BadZipFile, KeyError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
