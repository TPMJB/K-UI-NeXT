#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make a runtime-only CHD test overlay that preserves personalized loaders."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile
from runtime_package import verify
from retail_package import inspect_retail
from package import resolve_bundle_links

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--distribution", type=Path, default=ROOT / "dist/sd-update")
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT):
        parser.error("source must be clean and committed before packaging")
    if args.output.exists():
        parser.error("output already exists; use the exact source build in a new filename")
    source = args.distribution.resolve()
    runtime = source / "KUI/runtime.kui"
    module = source / "KUI/apps/games/retail-boot.kui"
    info = verify(runtime.read_bytes())
    standard = inspect_retail(module.read_bytes())
    fixtures = json.loads((args.fixtures / "CHD-CONSOLE-TESTS.json").read_text())
    distribution = json.loads((source / "build.json").read_text())
    if (info["build"] != commit[:12] or standard["build"] != commit[:12] or
            fixtures["source_build"] != commit[:12] or distribution["commit"] != commit):
        parser.error("runtime, optional reader, fixtures and distribution must match this commit")
    with tempfile.TemporaryDirectory(prefix="kui-chd-overlay-") as temporary:
        output = Path(temporary)
        (output / "KUI").mkdir()
        shutil.copyfile(runtime, output / "KUI/runtime.kui")
        (output / "OPTIONAL-STANDARD-READER").mkdir()
        shutil.copyfile(module, output / "OPTIONAL-STANDARD-READER/retail-boot.kui")
        for name in ("LICENSE", "THIRD_PARTY.md", "GAMES-CHD.md"):
            shutil.copyfile(source / name, output / name)
        shutil.copytree(source / "LICENSES", output / "LICENSES")
        shutil.copytree(args.fixtures / "Games", output / "Games")
        for name in ("CHD-CONSOLE-TESTS.json", "CHD-CONSOLE-TESTS.txt"):
            shutil.copyfile(args.fixtures / name, output / name)
        (output / "READ-ME-FIRST.txt").write_text(
            "K-UI CHD preparation test — " + commit[:12] + "\n\n"
            "1. Keep your current KUI/runtime.kui for rollback. Copy this ZIP's\n"
            "   KUI folder onto the card. It contains only the new runtime.\n"
            "   Your Games loaders, tools.kui, VMU profiles and saves stay in place.\n"
            "2. Select your CHD in Games, choose Prepare CHD, confirm space and\n"
            "   wait for decoding/readback verification. Then choose Play.\n"
            "   The expanded GDI cache is reused on later visits. This is not\n"
            "   compressed streaming during gameplay; keep expanded SD space.\n\n"
            "Original test discs: copy Games, then follow CHD-CONSOLE-TESTS.txt.\n"
            "A personalized Toy VMU loader rejects other executables, including\n"
            "these original tests. For ordinary test launches, first back up\n"
            "your exact KUI/apps/games/retail-boot.kui OFF the card, then temporarily\n"
            "copy OPTIONAL-STANDARD-READER/retail-boot.kui into that slot. Restore\n"
            "your exact backup afterwards to resume the VMU test. The optional\n"
            "reader is outside KUI so installing the overlay cannot replace it.\n\n"
            "The decoder supports parentless optical CHDv4/v5 and default CD\n"
            "codecs within GAMES-CHD.md's profile. Nonzero pregaps/postgaps, stored\n"
            "subchannels and parent images are refused. Physical console tests\n"
            "remain pending. ATA/IDE DMA is not implemented by this work.\n",
            encoding="utf-8")
        resolve_bundle_links(output, commit)
        hashes = {str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in sorted(output.rglob("*")) if path.is_file()}
        record = {"kind": "chd-runtime-overlay", "commit": commit, "runtime": info,
                  "optional_standard_reader": standard, "console_validation": "pending",
                  "preserves": ["KUI/apps/games", "KUI/tools.kui", "VMU profiles and saves"],
                  "chd_path": "verified expanded GDI cache before ordinary launch", "sha256": hashes}
        (output / "CHD-BUILD.json").write_text(json.dumps(record, indent=2) + "\n")
        (output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in hashes.items()))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(output.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(output))
    with zipfile.ZipFile(args.output) as archive:
        if archive.testzip() is not None:
            raise ValueError("ZIP integrity failure")
        automatic = [name for name in archive.namelist() if name.startswith("KUI/")]
        if automatic != ["KUI/runtime.kui"]:
            raise ValueError("Overlay would replace personalized loader data")
    print(json.dumps({"file": str(args.output.resolve()), "commit": commit,
                      "bytes": args.output.stat().st_size,
                      "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    main()
