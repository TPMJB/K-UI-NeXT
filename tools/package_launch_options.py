#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package saved launch options and the byte-identical tested Armada CE payload."""

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
DEFAULT_DELIVERABLES = ROOT.parent / "deliverables"
BASELINE_RUNTIME_BUILD = "5d7502a23326"
BASELINE_RUNTIME_SHA256 = (
    "ba90fa790fcdf8fb34169f6b9fb21fdf94221c762b02e0fd6837deba39a4662a"
)
BASELINE_GENERAL_SHA256 = (
    "3c342febf7409a0aedfa0d4973a8c0fd6dbdbf3675ad783fa373fc8cb30c82a7"
)
BASELINE_MDK2_SHA256 = (
    "42b9a8c958548521ec0bac351ff55b4edc5a51bd83b26f8a5c7cd97ebcd1a1b7"
)
CE_MEMBER = "KUI/apps/games/ce-probe.kui"
CE_COMMIT = "33298cc0bdb88ec9df12fe1d97bc26295c83b433"
CE_BUILD = CE_COMMIT[:12]
CE_BYTES = 85804
CE_SHA256 = "118ebca552f649ad639b02eebcac01bdbfb2e2b1531b0d1baadb1818dc159fde"
CE_CRC32 = "296d7ff2"
CE_ZIP_SHA256 = "ec01202558b74656874beaf7aea6f773831c4b35e206de2d3137c11c9af287ae"
CE_CONTROL_COMMIT = "fd3bde28d5388ab3e08bcf18167e331a0da0d69d"
CE_CONTROL_SHA256 = "42e378a7af567c751d9413ef03c6f8509762e90c0001f75a4648a7fba04c4198"
CE_SOURCE_ARCHIVES = {
    "source/K-UI-Armada-Page-Tables-33298cc0bdb8.tar.gz":
        "b8b98f650c415be209b1ee74d4bac8caca9d4f563d8e43642306413efe2689cf",
    "source/K-UI-Armada-CE-DMA-fd3bde28d538.tar.gz":
        "52070f6f0b53f37fc8b53392de615e77a9071f5a120e2e30eb3bb8d0aec5ab22",
}
sys.path.insert(0, str(ROOT / "tools"))
from runtime_package import flatten_elf, verify  # noqa: E402
from retail_package import inspect_retail  # noqa: E402


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_archive(commit):
    return gzip.compress(git("archive", "--format=tar",
                             "--prefix=K-UI-Launch-Options/", commit), mtime=0)


def check(condition, message):
    if not condition:
        raise ValueError(message)


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


def add_ce_baseline(files, path):
    """Copy the already tested candidate and its exact source/restore provenance."""
    check(path.stat().st_size < 50 * 1024 * 1024 and
          digest(path.read_bytes()) == CE_ZIP_SHA256,
          "CE baseline ZIP does not match the tested delivery")
    with zipfile.ZipFile(path) as archive:
        check(archive.testzip() is None, "CE baseline ZIP failed its CRC check")
        check(len(archive.namelist()) == len(set(archive.namelist())),
              "CE baseline ZIP contains duplicate member names")
        prior = json.loads(archive.read("build.json"))
        check(prior["source_commit"] == CE_COMMIT and prior["ce_build"] == CE_BUILD and
              prior["ce_sha256"] == CE_SHA256 and prior["ce"]["crc32"] == CE_CRC32 and
              prior["control_source_commit"] == CE_CONTROL_COMMIT and
              prior["control_sha256"] == CE_CONTROL_SHA256 and
              prior["audio_enabled"] is False and prior["reader_scheduling_changed"] is False,
              "CE baseline source/build/scope metadata differs")
        payload = archive.read(CE_MEMBER)
        ce = inspect_retail(payload, ce=True)
        ce.pop("abi", None)  # Historical generic label does not describe this observer.
        check(len(payload) == CE_BYTES and digest(payload) == CE_SHA256 and
              ce["build"] == CE_BUILD and ce["crc32"] == CE_CRC32 and
              ce["payload_bytes"] == 85740 and ce["memory_bytes"] == 85740 and
              ce["stage_bytes"] == 77548 and ce["stage_address"] == "0x8ce10000",
              "CE baseline payload/envelope does not match the tested bytes")
        restore = archive.read("restore-ce/" + CE_MEMBER)
        control = inspect_retail(restore, ce=True)
        control.pop("abi", None)
        check(len(restore) == 82544 and digest(restore) == CE_CONTROL_SHA256 and
              control["build"] == CE_CONTROL_COMMIT[:12] and control["crc32"] == "45735389",
              "CE restoration does not match the exact prior DMA payload")
        files[CE_MEMBER] = payload
        files["restore-ce/" + CE_MEMBER] = restore
        for name, expected in CE_SOURCE_ARCHIVES.items():
            data = archive.read(name)
            check(digest(data) == expected and prior["source_archive_sha256"][name] == expected,
                  "CE source archive does not match its pinned source: " + name)
            files[name] = data
        files["evidence/armada-existing/build.json"] = archive.read("build.json")
        files["source/armada/package_armada_audio.py"] = archive.read("source/package_armada_audio.py")
        for name in archive.namelist():
            if name.startswith("evidence/"):
                files["evidence/armada-existing/" + name[len("evidence/"):]] = archive.read(name)
            elif name.startswith("docs/") or name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                data = archive.read(name)
                check(name not in files or files[name] == data,
                      "CE/runtime dependency or document conflict: " + name)
                files[name] = data
    return ce, control


def build(args):
    check(re.fullmatch(r"[0-9a-f]{40}", args.commit) is not None,
          "--commit requires a full lowercase commit hash")
    check(git("rev-parse", "HEAD").decode().strip() == args.commit,
          "Requested commit does not match HEAD")
    check(not git("status", "--porcelain"),
          "Commit all source changes before packaging")

    check(args.runtime.stat().st_size <= 4 * 1024 * 1024 + 64,
          "Runtime package exceeds its size limit")
    runtime = args.runtime.read_bytes()
    runtime_info = verify(runtime)
    check(runtime_info["build"] == args.commit[:12],
          "Runtime build does not match the source commit")
    elf_payload, elf_memory = flatten_elf(args.elf.read_bytes())
    check(runtime[64:] == elf_payload and
          runtime_info["memory_bytes"] == elf_memory,
          "Runtime package does not match the built ELF")

    files = {
        "KUI/runtime.kui": runtime,
        "docs/launch-options-test.md":
            (ROOT / "docs/launch-options-test.md").read_bytes(),
        "docs/armada-ce-reader-test.md":
            (ROOT / "docs/armada-ce-reader-test.md").read_bytes(),
        "tools/kui-armada-startup-bundle.py":
            (ROOT / "tools/kui-armada-startup-bundle.py").read_bytes(),
        "source/K-UI-Launch-Options-" + args.commit[:12] + ".tar.gz":
            source_archive(args.commit),
        "source/package_launch_options.py": Path(__file__).read_bytes(),
    }
    with zipfile.ZipFile(args.baseline) as baseline:
        check(baseline.testzip() is None, "Baseline ZIP failed its CRC check")
        check(len(baseline.namelist()) == len(set(baseline.namelist())),
              "Baseline ZIP contains duplicate member names")
        restore = baseline.read("KUI/runtime.kui")
        check(digest(restore) == BASELINE_RUNTIME_SHA256 and
              verify(restore)["build"] == BASELINE_RUNTIME_BUILD,
              "Baseline runtime does not match the installed working build")
        files["restore-runtime/KUI/runtime.kui"] = restore
        unchanged = {}
        for name, expected in (
                ("KUI/apps/games/retail-boot.kui", BASELINE_GENERAL_SHA256),
                ("KUI/apps/games/retail-cdda-mdk2.kui", BASELINE_MDK2_SHA256)):
            data = baseline.read(name)
            check(digest(data) == expected,
                  "Baseline loader does not match its preserved payload: " + name)
            unchanged[name] = digest(data)
        for name in baseline.namelist():
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(
                    ("LICENSES/", "source/dependencies/")):
                files[name] = baseline.read(name)

    ce_info, ce_control = add_ce_baseline(files, args.ce_baseline)

    for specification in args.evidence:
        add_evidence(files, specification)
    info = {
        "source_commit": args.commit,
        "runtime": runtime_info,
        "runtime_sha256": digest(runtime),
        "restore_runtime": verify(restore),
        "restore_runtime_sha256": digest(restore),
        "hardware_tested": False,
        "runtime_hardware_tested": False,
        "scope": "Automatically saved per-image launch settings and exact previously tested Armada CE observer",
        "installed_files": ["KUI/runtime.kui", CE_MEMBER],
        "ce_source_commit": CE_COMMIT,
        "ce": ce_info,
        "ce_sha256": CE_SHA256,
        "ce_baseline_zip_sha256": CE_ZIP_SHA256,
        "ce_payload_previously_tested": True,
        "ce_payload_rebuilt": False,
        "ce_audio_enabled": False,
        "ce_reader_scheduling_changed": False,
        "ce_binding_changes_added": False,
        "ce_repeat_console_test_requested": False,
        "restore_ce_source_commit": CE_CONTROL_COMMIT,
        "restore_ce": ce_control,
        "restore_ce_sha256": CE_CONTROL_SHA256,
        "unchanged_loader_sha256": unchanged,
        "unchanged_loader_builds": {
            "general_native": "72e3bf049b26",
            "mdk2_cdda": "89c4650e444f",
        },
        "build_commands": [
            "source .deps/kos/environ.sh",
            "make -j2 -f Makefile.dc build/kui-runtime.elf BUILD_ID=" + args.commit[:12],
            "python3 tools/runtime_package.py build build/kui-runtime.elf "
            "build/kui-runtime.kui --build-id " + args.commit[:12],
            "python3 tools/package_launch_options.py --commit " + args.commit +
            " --runtime build/kui-runtime.kui",
        ],
        "source_archive_sha256": {
            name: digest(data) for name, data in files.items()
            if name.endswith(".tar.gz")
        },
        "verification": {
            "runtime_envelope_and_crc": True,
            "runtime_matches_elf": True,
            "source_commit_matches_clean_head": True,
            "restore_runtime_matches_known_sha256": True,
            "baseline_loaders_match_known_sha256": True,
            "ce_baseline_zip_matches_known_sha256": True,
            "ce_payload_and_source_archives_match_pinned_delivery": True,
            "ce_restore_matches_known_sha256": True,
        },
    }
    files["build.json"] = (json.dumps(info, indent=2) + "\n").encode()
    files["README.md"] = f"""# Saved launch options and Armada

Menu runtime: **{args.commit[:12]}**. Armada CE: **{CE_BUILD}**, copied byte-for-byte
from the payload you already tested. CE music remains disabled.

1. With the console off, back up `KUI/runtime.kui` and
   `KUI/apps/games/ce-probe.kui`.
2. Merge this ZIP's `KUI` folder onto the card and cold boot. These are the only
   two installed files; native general/MDK2/Toy payloads remain installed.
3. Inspect a game. **A** opens launch confirmation; **A** on that confirmation
   launches. **Y** from the details or confirmation opens Options.
4. Options has **Background loading**, **CD music** and **Advanced**. Change an
   available toggle with **A** or **Left/Right**; it saves automatically for
   that exact image path. **START** launches from Options. Advanced contains
   **Boot encoding**, **Background speed**, **Image information** and
   **Developer tests**. Its tests are **Image read test** and **Loader read
   test**, each with confirmation. CE boot uses ordinary launch.
5. Check the new menu controls and persistence on games you already use.
   Original and 2048 image paths retain separate settings. The header displays
   `github.com/TPMJB/K-UI-NeXT`, linked to the complete HTTPS URL. Its compact
   glyphs draw opaquely; body text keeps the existing blended font.

You do not need to repeat the previous Armada CE diagnostic run for this ZIP.
It adds no CE binding change, audio backend or reader scheduling change. Prior
CE test documents/evidence are included for provenance, not a new test request.
The updated menu still needs its first hardware check.

For exact menu restoration, merge `restore-runtime/KUI` and cold boot; this is
previous runtime **{BASELINE_RUNTIME_BUILD}**. `restore-ce/KUI` restores exact
DMA CE **{CE_CONTROL_COMMIT[:12]}**. Your backups restore the files present
immediately before this combined package.

Source, dependency sources, licenses, hashes and verification evidence are
included. See `docs/launch-options-test.md` for the controls and checks.
""".encode()
    check(sorted(name for name in files if name.startswith("KUI/")) ==
          sorted(["KUI/runtime.kui", CE_MEMBER]),
          "Only the menu runtime and tested CE payload may appear in the installation folder")
    files["SHA256SUMS"] = "".join(
        digest(data) + "  " + name + "\n" for name, data in sorted(files.items())
    ).encode()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=args.output.name + ".",
                                         suffix=".partial",
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
            check(archive.testzip() is None and
                  len(archive.infolist()) == len(files), "Output ZIP failed its CRC check")
            for name, data in files.items():
                check(archive.read(name) == data, "Output member differs: " + name)
        check(temporary.stat().st_size < 50 * 1024 * 1024,
              "Output ZIP exceeds the 50 MiB delivery limit")
        temporary.replace(args.output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)

    result = {
        "path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
        "entries": len(files), "sha256": digest(args.output.read_bytes()),
        "source_commit": args.commit, "runtime_build": runtime_info["build"],
        "ce_build": CE_BUILD, "ce_source_commit": CE_COMMIT,
    }
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/kui-runtime.elf")
    parser.add_argument("--baseline", type=Path,
                        default=DEFAULT_DELIVERABLES / "K-UI-MDK2-CDDA-Early-Lease-Test.zip")
    parser.add_argument("--ce-baseline", type=Path,
                        default=DEFAULT_DELIVERABLES / "K-UI-Armada-Page-Tables-Test.zip")
    parser.add_argument("--output", type=Path,
                        default=DEFAULT_DELIVERABLES / "K-UI-Saved-Launch-Options-and-Armada-Test.zip")
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError,
            zipfile.BadZipFile, KeyError) as error:
        parser.exit(1, "Packaging stopped: " + str(error) + "\n")


if __name__ == "__main__":
    main()
