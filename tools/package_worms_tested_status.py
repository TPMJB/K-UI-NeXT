#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the menu-only Worms tested-status correction; retain installed loaders."""
import argparse
import gzip
import json
from pathlib import Path
import subprocess
import zipfile

from package_armada_excerpt import check, digest, verify_members
from package_armada_ce_loop_alignment import DEPENDENCIES
from package_bam4_ce_stream import ROOT, RUNTIME, clean_head, git, json_bytes
from package_wince_cdda_diagnostic import matched_package
from package_wince_ce_stream import write_zip
from retail_package import inspect_retail

BASE_COMMIT = "69140d55b8d702071db906ef7c8d0ef736672951"
BASELINE_SHA = "9e867bb8bf93eebbd30f1f77494fe5d4e95000beb2f001f35193513659bbd7e7"
DIAGNOSTIC_SHA = "2ba06ce304cd8b6f9fbbf6b8b3a6cd764c6dffaefc2c69af2aa1ceb7a47bb54a"
WORMS_MEMBER = "KUI/apps/games/ce-cdda-worms.kui"
WORMS_SHA = "3c39d5b4e149b4ed911f7d4cf1fd19ea750f94a0a783b149cf4c5357cd36651a"
ALLOWED = {"include/kui/games_cdda_status.h", "tests/test_games_cdda_status.c",
    "docs/worms-2048-cold-start-result-2026-10-15.md", "tools/package_worms_tested_status.py"}


def checked_zip(path, expected):
    check(path.is_file() and path.stat().st_size < 50 * 1024 * 1024 and
          digest(path.read_bytes()) == expected, "Pinned ZIP differs: " + str(path))
    archive = zipfile.ZipFile(path)
    verify_members(archive)
    return archive


def build(args):
    clean_head(args.commit)
    subprocess.run(["git", "merge-base", "--is-ancestor", BASE_COMMIT, args.commit], cwd=ROOT, check=True)
    changed = set(git("diff", "--name-only", BASE_COMMIT, args.commit).decode().splitlines())
    check(changed <= ALLOWED and "include/kui/games_cdda_status.h" in changed,
          "Changes exceed the menu-only tested-status correction")
    stamp = args.commit[:12]
    runtime, info, compiled, compile_data = matched_package(
        ROOT / "build/kui-runtime.elf", args.runtime, args.commit)
    files = {RUNTIME: runtime, "evidence/runtime-compile.json": compile_data}
    with checked_zip(args.baseline, BASELINE_SHA) as archive:
        for name in archive.namelist():
            if name in ("LICENSE", "THIRD_PARTY.md") or name.startswith(("LICENSES/", "source/dependencies/")):
                files[name] = archive.read(name)
    check({name for name in files if name.startswith("source/dependencies/")} == set(DEPENDENCIES),
          "Dependency source inventory differs")
    for name, expected in DEPENDENCIES.items():
        check(digest(files[name]) == expected, "Dependency source differs: " + name)
    with checked_zip(args.diagnostic, DIAGNOSTIC_SHA) as archive:
        working = archive.read(WORMS_MEMBER)
        prior = json.loads(archive.read("build.json"))
        check(digest(working) == WORMS_SHA and prior["source_commit"] == BASE_COMMIT and
              prior["package_sha256"] == WORMS_SHA, "Working Worms module/source identity differs")
        worms = {"package": WORMS_MEMBER, "sha256": WORMS_SHA,
            "envelope": inspect_retail(working, ce=True), "source_commit": BASE_COMMIT,
            "diagnostic_zip_sha256": DIAGNOSTIC_SHA, "rebuild": False, "included_in_this_zip": False}
    working_file = ROOT / "build/worms-ce-stream/ce-cdda-worms.kui"
    check(digest(working_file.read_bytes()) == WORMS_SHA, "Working Worms module changed")
    tests = args.test_log.read_bytes()
    check(0 < len(tests) <= 4 * 1024 * 1024 and all(s in tests for s in (
        b"PASS CD audio evidence:", b"raw CE and exact cooked Worms tested",
        b"PASS inspector identity IO:", b"PASS shell:")) and not any(s in tests for s in (
        b"ERROR: AddressSanitizer", b"runtime error:", b"Assertion")), "Required host checks missing or failed")
    files["evidence/status-identity-shell-tests.log"] = tests
    files["evidence/working-worms-module-unchanged.json"] = json_bytes(worms)
    for name in ("experimental-cdda.md", "worms-2048-cold-start-result-2026-10-15.md"):
        files["docs/" + name] = (ROOT / "docs" / name).read_bytes()
    source = "source/K-UI-Worms-Tested-Status-" + stamp + ".tar.gz"
    files[source] = gzip.compress(git("archive", "--format=tar",
        "--prefix=K-UI-Worms-Tested-Status/", args.commit), mtime=0)
    files["source/worms-tested-status.patch"] = git("diff", "--binary", BASE_COMMIT, args.commit)
    files["source/package_worms_tested_status.py"] = Path(__file__).read_bytes()
    files["build.json"] = json_bytes({
        "source_commit": args.commit, "source_tree": git("rev-parse", "HEAD^{tree}").decode().strip(),
        "base_commit": BASE_COMMIT, "source_dirty": False, "installed_files": [RUNTIME],
        "menu_status_update_only": True, "game_loader_rebuilt": False, "audio_pacing_retuned": False,
        "launch_admission_changed": False, "new_runtime_hardware_tested": False,
        "owner_hardware_result": "Exact converted US Worms image, CD music On, three cold starts",
        "universal_cdda_support_claim": False, "runtime": info, "runtime_sha256": digest(runtime),
        "runtime_compile": compiled, "working_worms_module": worms, "baseline_zip_sha256": BASELINE_SHA,
        "source_archive_sha256": {name: digest(data) for name, data in files.items() if name.endswith(".tar.gz")},
        "source_changes": sorted(changed), "build_commands": ["source .deps/kos/environ.sh",
            "make -j2 test-games-cdda-status test-games-cdda-identity-io build/test-shell",
            "ASAN_OPTIONS=detect_leaks=0 ./build/test-shell",
            "make -j2 -f Makefile.dc build/kui-runtime.elf BUILD_ID=" + stamp,
            "python3 tools/runtime_package.py build build/kui-runtime.elf build/worms-tested-runtime.kui --build-id " + stamp]})
    files["README.md"] = (
        f"# Worms 2048 tested-status correction\n\nMenu build **{stamp}**.\n\n"
        "With the console off, copy only `KUI/runtime.kui` onto your existing installation, then cold boot. "
        "Keep the working `ce-cdda-worms.kui` from `K-UI-Worms-2048-Map-Diagnostic.zip` installed.\n\n"
        "The exact converted US Worms Armageddon image now displays Tested following your three cold starts "
        "with CD music On. Its original IP fingerprint, complete kernel SHA and recorded eight-track layout "
        "must match. Mixed DATA conversions, altered audio geometry and other unfamiliar images remain experimental.\n\n"
        "This changes only the menu evidence label. No loader is replaced; audio copies, worker scheduling, "
        "SCI behavior and launch admission remain as installed. This makes no universal CDDA claim. "
        "The new menu passed host checks and has not yet been tested on console. To roll back, restore "
        "`KUI/runtime.kui` from the earlier experimental update.\n\n"
        "Hardware-result documentation, committed source, focused patch, pinned dependency sources/licenses, "
        "compile identity, test evidence and SHA256/CRC manifests are included.\n").encode()
    clean_head(args.commit)
    check(digest(working_file.read_bytes()) == WORMS_SHA, "Working Worms module changed during packaging")
    write_zip(args.output, files, installed_files=[RUNTIME])
    print(json.dumps({"path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
        "sha256": digest(args.output.read_bytes()), "build": stamp, "installed_files": [RUNTIME],
        "working_worms_sha256": WORMS_SHA}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--diagnostic", required=True, type=Path)
    parser.add_argument("--runtime", type=Path, default=ROOT / "build/worms-tested-runtime.kui")
    parser.add_argument("--test-log", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    build(parser.parse_args())


if __name__ == "__main__":
    main()
