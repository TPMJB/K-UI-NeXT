#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch independent sources at locked revisions; never import DreamShell."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / ".deps"
LOCK = json.loads((ROOT / "dependencies.json").read_text())


def run(*args, cwd=None):
    subprocess.run(args, cwd=cwd, check=True)


def fetch_git(name):
    dest = DEPS / name
    spec = LOCK[name]
    if not (dest / ".git").exists():
        # A restored cache may contain only toolchain source archives below
        # .deps/kos; initializing git here preserves those ignored downloads.
        run("git", "init", str(dest))
        run("git", "remote", "add", "origin", spec["url"], cwd=dest)
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=dest,
                          capture_output=True, text=True)
    if head.stdout.strip() != spec["commit"]:
        if head.returncode == 0 and subprocess.check_output(["git", "status", "--porcelain"], cwd=dest):
            raise SystemExit(f"Refusing to replace modified dependency: {dest}")
        run("git", "fetch", "--depth", "1", "origin", spec["commit"], cwd=dest)
        run("git", "checkout", "--detach", spec["commit"], cwd=dest)


def fetch_fatfs():
    cache = DEPS / "downloads"
    cache.mkdir(parents=True, exist_ok=True)
    files = []
    for spec in LOCK["fatfs"]:
        target = cache / spec["url"].rsplit("/", 1)[-1]
        if not target.exists():
            with urllib.request.urlopen(spec["url"], timeout=60) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != spec["sha256"]:
                raise SystemExit(f"Download checksum mismatch: {target.name}")
            target.write_bytes(data)
        if hashlib.sha256(target.read_bytes()).hexdigest() != spec["sha256"]:
            raise SystemExit(f"Cached checksum mismatch: {target}")
        files.append(target)
    dest = DEPS / "fatfs"
    # Reconstruct only generated FatFs files, keeping source and notices intact.
    with zipfile.ZipFile(files[0]) as archive:
        for name in ("source/ff.c", "source/ff.h", "source/diskio.h",
                     "source/ffunicode.c", "LICENSE.txt"):
            target = dest / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(name).replace(b"\r\n", b"\n"))
    for patch in files[1:]:
        run("patch", "--batch", "--fuzz=0", str(dest / "source/ff.c"), str(patch))
    shutil.copyfile(ROOT / "config/ffconf.h", dest / "source/ffconf.h")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fatfs-only", action="store_true")
    args = parser.parse_args()
    DEPS.mkdir(exist_ok=True)
    fetch_fatfs()
    if not args.fatfs_only:
        fetch_git("kos")
        fetch_git("mkdcdisc")


if __name__ == "__main__":
    main()
