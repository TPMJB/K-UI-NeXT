#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package matched experimental CDDA adapters without inheriting game content."""
import argparse
import gzip
import io
import json
from pathlib import Path
import subprocess
import zipfile

from package_armada_excerpt import check, digest, verify_members
from package_armada_ce_loop_alignment import DEPENDENCIES
from package_armada_ce_stream import add_evidence, inspect_stream_seed
from package_bam4_ce_stream import ROOT, RUNTIME, clean_head, git, json_bytes
from package_bam4_ce_stream import audit_stream as audit_bam4
from package_wince_ce_stream import audit_stream as audit_worms, write_zip
from package_wince_cdda_diagnostic import matched_package
from check_retail_loader_layout import check_directory as check_ce
from retail_package import inspect_retail
from runtime_package import verify

BASE_COMMIT = "1aa16821209bcd9a186c344dcba6223afee7f420"
BASELINE_SHA256 = "48feff2cfd3d8dabe04e9a47b89d6379e432b360148b94032340a50249b962b6"
BASELINE_RUNTIME_SHA256 = "c68abe6c8ba1e284ea92c2f31995360aadfce566dd71076ccbc69feb54399832"
MAX_BASELINE_BYTES = 50 * 1024 * 1024
PROFILES = {"armada": "armada-us", "bam4": "bam4-us", "worms": "worms-us"}
NATIVE_PROFILES = {
    "toy-raw": ("cdda-native-toy-raw.kui", "retail-toy-pilot.kui"),
    "toy-cooked": ("cdda-native-toy-cooked.kui", "retail-toy-pilot.kui"),
    "mdk2": ("cdda-native-mdk2.kui", "retail-mdk2-cdda.kui"),
}
TOY_BASELINE_SHA256 = "c88f69a1a3b8fa4acac0d55b9f43f0873a9591fd2a60c819fd5a430726276f89"
TOY_SOURCE_MEMBER = "source/Toy-pilot-867f2ca9ff5a.tar.gz"
TOY_SOURCE_SHA256 = "838016cd3a834cf208cae56e295709aa13c1a1586e4c4b90666439f7525b497e"
PRESERVED_TOY = {
    "raw": ("preserved-toy-tests/AP-raw-reference.kui", "9739eb693b02",
            "b935e90a744e5ea4e7ec114182682e4e418cae320b55db25b01a3f2540cd46ab",
            "78c15a8580320dc2b5301d8576cd4877df7ae9d84cc5518671c9a958af7ed541"),
    "cooked": ("preserved-toy-tests/AT-cooked-data.kui", "a8eba3e74b03",
               "733cea7241557d215256365aa7003e252860b4d02e70e4611787d45916f13211",
               "cde2a088e63292c164bd62780a644e83787f3e1d466b2920ad68a8dd6b9bcd82"),
}


def baseline_sources(path):
    """Reuse only authenticated dependency sources and their license notices."""
    check(path.is_file() and path.stat().st_size < MAX_BASELINE_BYTES,
          "Missing or oversized RAM-copy baseline ZIP")
    raw = path.read_bytes()
    check(digest(raw) == BASELINE_SHA256,
          "Baseline ZIP is not the exact delivered RAM-copy build")
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        members = verify_members(archive)
        prior = json.loads(archive.read("build.json"))
        check(prior["source_commit"] == BASE_COMMIT and
              prior["runtime_sha256"] == BASELINE_RUNTIME_SHA256 and
              digest(archive.read(RUNTIME)) == BASELINE_RUNTIME_SHA256,
              "RAM-copy baseline source/runtime identity differs")
        files = {name: archive.read(name) for name in members
                 if name in ("LICENSE", "THIRD_PARTY.md") or
                 name.startswith(("LICENSES/", "source/dependencies/"))}
        inherited = prior["dependency_source_provenance"]["inherited_members"]
        check(set(files) == set(inherited) and
              {name for name in files if name.startswith("source/dependencies/")} == set(DEPENDENCIES),
              "Baseline dependency or license inventory differs")
        for name, data in files.items():
            check(digest(data) == inherited[name] and
                  (name not in DEPENDENCIES or digest(data) == DEPENDENCIES[name]),
                  "Baseline dependency/license digest differs: " + name)
    return files, {"zip_sha256": BASELINE_SHA256, "source_commit": BASE_COMMIT,
                   "inherited_members": {name: digest(data) for name, data in sorted(files.items())},
                   "prior_binaries_inherited": False, "prior_evidence_inherited": False}


def add_module(files, member, elf, package, commit, *, retail=False):
    check(member not in files, "Duplicate installation member: " + member)
    data, envelope, compiled, compile_data = matched_package(elf, package, commit, retail=retail)
    files[member] = data
    label = Path(member).stem
    files["evidence/" + label + "-compile.json"] = compile_data
    return {"package": member, "sha256": digest(data), "envelope": envelope,
            "compile": compiled, "new_binary_hardware_tested": False}


def add_ce_modules(files, commit):
    profiles = {}
    for name, adapter in PROFILES.items():
        directory = ROOT / ("build/" + name + "-ce-stream")
        member = "KUI/apps/games/ce-cdda-" + name + ".kui"
        record = add_module(files, member, directory / "entry.elf",
                            directory / ("ce-cdda-" + name + ".kui"), commit, retail=True)
        if name == "worms":
            seed, audit = audit_worms(directory, files[member])
        elif name == "bam4":
            seed, audit = audit_bam4(directory, files[member])
        else:
            from check_armada_ce_excerpt import check as linked_audit
            seed = inspect_stream_seed(files[member])
            audit = linked_audit(directory, stream=True)
        check(audit["title_profile"] == adapter and audit["stream_profile"] is True and
              audit["distributed_pcm_bytes"] == 0 and
              audit["distributed_audio_map_bytes"] == 4096,
              "Wrong CE implementation adapter or distributed nonblank audio: " + name)
        check(audit["stream_boot_arm"]["attach_only_after_relay"] is True and
              audit["stream_boot_arm"]["preserves_controller_zero_check"] is True,
              "CE bootstrap ownership checks are absent: " + name)
        files["evidence/" + name + "-layout-and-stack.json"] = json_bytes(audit)
        record.update({"implementation_adapter": adapter, "seed": seed,
                       "broader_runtime_matching_experimental": True})
        profiles[name] = record
    return profiles


def add_native_modules(files, specifications, commit):
    """Optional native implementation adapters, always rebuilt from this commit."""
    records = {}
    for specification in specifications:
        check("=" in specification, "Native adapter must have the form PROFILE=DIRECTORY")
        name, source = specification.split("=", 1)
        check(name in NATIVE_PROFILES and name not in records,
              "Unknown or duplicate native adapter: " + name)
        directory = Path(source).resolve()
        install_name, package_name = NATIVE_PROFILES[name]
        member = "KUI/apps/games/" + install_name
        # Generic envelope/ELF verification is followed by the native, rather
        # than CE, relocation inspector and the actual native linked audit.
        record = add_module(files, member, directory / "entry.elf",
                            directory / package_name, commit)
        record["envelope"] = inspect_retail(files[member])
        config = (directory / "build-config").read_text()
        values = dict(line.split("=", 1) for line in config.splitlines())
        check(values.get("NATIVE_COMPAT") == "1" and values.get("BUILD") == commit[:12] and
              values.get("SCI") == "1" and values.get("PRIVATE_P2") == "1",
              "Native compatibility build flags/source identity differ: " + name)
        if name == "mdk2":
            check(values.get("MDK2_CDDA") == "1", "Wrong MDK2 implementation adapter")
        else:
            check(values.get("NATIVE_CACHE") == "0" and
                  values.get("DATA_PIPELINE_PROFILE") == "2" and
                  values.get("COOKED_DATA_PROFILE", "0") == ("1" if name == "toy-cooked" else "0"),
                  "Toy adapter must retain the AP/AT playback configuration: " + name)
        from check_native_cdda_compat import check_directory
        audit = check_directory(directory, name)
        record.update({"implementation_adapter": name, "experimental": True,
                       "loaded_executable_and_sound_driver_guards_retained": True,
                       "playback_timing_retuned": False})
        records[name] = record
        files["evidence/native-" + name + "-config.txt"] = config.encode()
        files["evidence/native-" + name + "-layout-and-stack.json"] = json_bytes(audit)
    return records


def preserve_toy(files, path):
    """Retain only known exact native binaries and their corresponding source."""
    if path is None:
        return {}
    check(path.is_file() and path.stat().st_size < MAX_BASELINE_BYTES and
          digest(path.read_bytes()) == TOY_BASELINE_SHA256,
          "Toy baseline is not the exact general-games restore ZIP")
    records = {}
    with zipfile.ZipFile(path) as archive:
        verify_members(archive)
        source = archive.read(TOY_SOURCE_MEMBER)
        check(digest(source) == TOY_SOURCE_SHA256, "Exact Toy corresponding source changed")
        files[TOY_SOURCE_MEMBER] = source
        for name, (original, stamp, file_sha, payload_sha) in PRESERVED_TOY.items():
            data = archive.read(original)
            envelope = verify(data)
            check(digest(data) == file_sha and digest(data[64:]) == payload_sha and
                  envelope["build"] == stamp,
                  "Exact Toy native binary or payload identity changed: " + name)
            inspect_retail(data)
            member = "KUI/apps/games/cdda-toy-" + name + ".kui"
            check(member not in files, "Duplicate preserved native member")
            files[member] = data
            records[name] = {"package": member, "sha256": file_sha,
                             "payload_sha256": payload_sha, "envelope": envelope,
                             "byte_identical_to_prior_delivery": True,
                             "new_hardware_test_claim": False,
                             "source_member": TOY_SOURCE_MEMBER,
                             "source_sha256": TOY_SOURCE_SHA256,
                             "source_commit": "867f2ca9ff5a728197ecffd5c7156aeaf4946e1b",
                             "baseline_zip_sha256": TOY_BASELINE_SHA256,
                             "baseline_member": original}
    files["evidence/preserved-toy-provenance.json"] = json_bytes(records)
    return records


def build(args):
    clean_head(args.commit)
    subprocess.run(["git", "merge-base", "--is-ancestor", BASE_COMMIT, args.commit],
                   cwd=ROOT, check=True)
    stamp = args.commit[:12]
    files, provenance = baseline_sources(args.baseline)
    runtime = add_module(files, RUNTIME, ROOT / "build/kui-runtime.elf",
                         ROOT / "build/runtime.kui", args.commit)
    profiles = add_ce_modules(files, args.commit)
    ordinary_directory = ROOT / "build/retail-ce"
    ordinary_member = "KUI/apps/games/ce-probe.kui"
    ordinary = add_module(files, ordinary_member, ordinary_directory / "entry.elf",
                          ROOT / "build/ce-probe.kui", args.commit, retail=True)
    config = (ordinary_directory / "layout-config").read_text()
    check("CE_SCI_RESUME_FIX=1" in config and "LOW=0" in config,
          "Ordinary CE loader must retain its DMA resume fix and placement")
    check(b"CEX2" not in files[ordinary_member][64:] and
          b"CE CD MUSIC PROFILE MISMATCH" not in files[ordinary_member][64:],
          "A music adapter occupied the ordinary CE loader slot")
    files["evidence/ordinary-ce-layout.json"] = json_bytes(check_ce(ordinary_directory, ce=True))
    files["evidence/ordinary-ce-config.txt"] = config.encode()
    native = add_native_modules(files, args.native, args.commit)
    preserved = preserve_toy(files, args.preserve_toy)
    documentation = ROOT / "docs/experimental-cdda.md"
    check(documentation.is_file() and documentation.stat().st_size > 0,
          "Experimental feature documentation is missing")
    files["docs/experimental-cdda.md"] = documentation.read_bytes()
    source_member = "source/K-UI-Experimental-CDDA-" + stamp + ".tar.gz"
    files[source_member] = gzip.compress(git("archive", "--format=tar",
        "--prefix=K-UI-Experimental-CDDA/", args.commit), mtime=0)
    files["source/experimental-cdda.patch"] = git("diff", "--binary", BASE_COMMIT, args.commit)
    files["source/package_experimental_cdda.py"] = Path(__file__).read_bytes()
    for specification in args.evidence:
        add_evidence(files, specification)
    installed = sorted(name for name in files if name.startswith("KUI/"))
    files["evidence/dependency-source-provenance.json"] = json_bytes(provenance)
    files["build.json"] = json_bytes({
        "source_commit": args.commit, "base_commit": BASE_COMMIT,
        "source_tree": git("rev-parse", "HEAD^{tree}").decode().strip(),
        "source_dirty": False, "installed_files": installed,
        "hardware_tested": False, "experimental": True,
        "new_binary_hardware_tested": False, "playback_timing_retuned": False,
        "working_ram_copy_and_worker_budget_retained": True,
        "unknown_native_executables_allowed": False,
        "unknown_ce_kernels_allowed": False,
        "runtime": runtime, "ce_adapters": profiles, "ordinary_ce": ordinary,
        "native_adapters": native,
        "preserved_exact_native_adapters": preserved,
        "exact_mdk2_module_included": False,
        "installation_kind": "Update to an existing compatible K-UI installation",
        "compatibility_scope": "Opt-in matching of inspected implementation contracts; no universal CDDA support claim",
        "existing_console_evidence": "Applies only to the exact images and earlier builds identified in the documentation",
        "dependency_source_provenance": provenance,
        "source_archive_sha256": {name: digest(data) for name, data in files.items()
                                  if name.endswith(".tar.gz")},
        "build_commands": [
            "source .deps/kos/environ.sh",
            "make -j2 -f Makefile.dc build/kui-runtime.elf BUILD_ID=" + stamp,
            "make -j2 -f Makefile.dc build/retail-ce/entry.elf KUI_RETAIL_LOW_RESIDENT=0 KUI_CE_SCI_RESUME_FIX=1 BUILD_ID=" + stamp,
            "make -j2 -f Makefile.armada-stream build/armada-ce-stream/entry.elf BUILD_ID=" + stamp,
            "make -j2 -f Makefile.armada-stream build/bam4-ce-stream/entry.elf KUI_WINCE_CDDA_PROFILE=bam4 BUILD_ID=" + stamp,
            "make -j2 -f Makefile.worms-stream build/worms-ce-stream/entry.elf BUILD_ID=" + stamp,
            "python3 tools/runtime_package.py build ELF OUTPUT --build-id " + stamp],
        "native_build_configuration_evidence": [
            "evidence/native-" + name + "-config.txt" for name in native],
        "source_git_bundle_included": False,
        "source_patch_base_commit": BASE_COMMIT})
    files["README.md"] = (
        f"# K-UI experimental CD music\n\nBuild **{stamp}**. This newly built package has not been tested on console.\n\n"
        "Merge this ZIP's `KUI` folder onto the storage card with the console off, then cold boot. "
        "Install the matching menu and all supplied adapters together. Existing compatible boot discs can be retained.\n\n"
        "This is an update for an existing K-UI installation. Existing ordinary native game-loader files are retained. "
        "This ZIP does not include the older exact `retail-cdda-mdk2.kui`. If that file is absent, a supplied "
        "MDK2 compatibility adapter can be used as an experimental fallback. Supplied compatibility adapters "
        "use separate filenames.\n\n"
        "CD music remains an optional per-image setting. Broader native and Windows CE/CDDA matching is experimental. "
        "Only the explicitly documented exact game images have prior console evidence; another region, revision or "
        "game is experimental even when it shares a recognized implementation. Unrecognized native executables "
        "and CE kernels are refused before incompatible hooks are installed.\n\n"
        "The successful RAM-copy and worker budgets are retained. This package extends detection and routing; "
        "it does not establish compatibility with every CDDA game. See `docs/experimental-cdda.md` for support "
        "boundaries and a short console test.\n\n"
        "Committed source, pinned dependency sources/licenses, compile identities, linked audits and complete "
        "SHA256/CRC manifests are included. No game executable, CE kernel, sound firmware or PCM audio is included.\n"
    ).encode()
    clean_head(args.commit)
    write_zip(args.output, files, installed_files=installed)
    print(json.dumps({"path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
                      "sha256": digest(args.output.read_bytes()), "build": stamp,
                      "installed_files": installed, "hardware_tested": False}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--native", action="append", default=[], metavar="PROFILE=DIRECTORY",
                        help="Include a newly rebuilt optional native compatibility adapter")
    parser.add_argument("--preserve-toy", type=Path,
                        help="Exact prior general-games restore ZIP, to retain its tested Toy binaries and source")
    parser.add_argument("--evidence", action="append", default=[], metavar="NAME=PATH")
    build(parser.parse_args())


if __name__ == "__main__":
    main()
