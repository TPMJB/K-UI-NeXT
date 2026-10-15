#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package only the ordinary CE loader used when CD music is Off."""
import argparse
import gzip
import json
from pathlib import Path

from package_armada_excerpt import check, digest
from package_bam4_ce_stream import ROOT, BASELINE, DELIVERABLES, clean_head, dependency_sources, git, json_bytes
from package_wince_cdda_diagnostic import matched_package
from package_wince_ce_stream import write_zip
from check_retail_loader_layout import check_directory

INSTALL = "KUI/apps/games/ce-probe.kui"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--output", type=Path, default=DELIVERABLES / "K-UI-CE-Audio-Off-Loader-Fix.zip")
    args = parser.parse_args()
    clean_head(args.commit)
    stamp = args.commit[:12]
    directory = ROOT / "build/retail-ce"
    config = (directory / "layout-config").read_text()
    check("LOW=0" in config and "CE_SCI_RESUME_FIX=1" in config and "BUILD=" + stamp in config,
          "Ordinary CE build must retain the DMA resume fix and expected placement")
    data, info, compile_info, compile_bytes = matched_package(
        directory / "entry.elf", ROOT / "build/ce-probe.kui", args.commit, retail=True)
    for marker in (b"ARMADA AUDIO TEST PROFILE MISMATCH", b"CE CD MUSIC PROFILE MISMATCH",
                   b"CEX2", b"ARMADA AUDIO TEST"):
        check(marker not in data[64:], "A specialized audio payload occupied the ordinary CE slot")
    audit = check_directory(directory, ce=True)
    files, provenance = dependency_sources(args.baseline)
    provenance.pop("armada_rc9_preserved")
    files[INSTALL] = data
    files["evidence/compile.json"] = compile_bytes
    files["evidence/layout.json"] = json_bytes(audit)
    files["evidence/build-config.txt"] = config.encode()
    files["evidence/build.log"] = (ROOT / "build/ce-off-control-build.log").read_bytes()
    archive = "source/K-UI-CE-Audio-Off-Control-" + stamp + ".tar.gz"
    files[archive] = gzip.compress(git("archive", "--format=tar", "--prefix=K-UI-CE-Control/", args.commit), mtime=0)
    files["build.json"] = json_bytes({
        "source_commit": args.commit, "source_dirty": False, "build": stamp,
        "installed_files": [INSTALL], "sha256": digest(data), "envelope": info,
        "compile": compile_info, "ce_sci_resume_fix": True,
        "audio_modules_replaced": False, "runtime_replaced": False,
        "hardware_tested": False, "dependency_source_provenance": provenance})
    files["README.md"] = (
        "# K-UI ordinary CE loader repair\n\n"
        "Build **" + stamp + "**. New binary: not yet console tested.\n\n"
        "With the console off, merge the KUI folder onto the SCI card and cold boot. "
        "This replaces only KUI/apps/games/ce-probe.kui, the ordinary loader selected "
        "when CD music is Off. Keep the working runtime and ce-cdda-* music modules.\n\n"
        "An old Armada observer installed at this same path rejects Worms with "
        "ARMADA AUDIO TEST PROFILE MISMATCH. This ordinary CE build has no "
        "Armada music-profile gate and retains the CE DMA resume correction. "
        "It accepts the ordinary loader's existing raw/cooked DATA formats.\n\n"
        "Use the same Worms image and match with Background loading On and CD music Off. "
        "Compare about 30 seconds of gameplay against CD music On. No title-screen "
        "wait or diagnostic recording is needed for that speed comparison.\n\n"
        "This repair enables the control comparison; it does not retune music or "
        "establish the cause of reported lag. Source, dependency sources, licenses "
        "and linked audit evidence are included.\n").encode()
    clean_head(args.commit)
    write_zip(args.output, files, installed_files=[INSTALL])
    print(json.dumps({"path": str(args.output.resolve()), "bytes": args.output.stat().st_size,
                      "sha256": digest(args.output.read_bytes()), "build": stamp,
                      "installed_files": [INSTALL]}, indent=2))

if __name__ == "__main__":
    main()
