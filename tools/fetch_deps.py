#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch independent sources at locked revisions; never import DreamShell."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import urllib.error
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / ".deps"
LOCK = json.loads((ROOT / "dependencies.json").read_text())


def run(*args, cwd=None):
    subprocess.run(args, cwd=cwd, check=True)


def download(url, attempts=5):
    """Fetch one pinned file, retrying transient network failures.

    elm-chan.org is a single small host and regularly times out from CI
    runners. A single urlopen there fails the whole build for a reason that
    has nothing to do with the code, so retry with a widening backoff and a
    longer timeout. The caller still checks the SHA-256, so a retry can never
    weaken the pin: a truncated or substituted body is rejected either way.
    """
    last = None
    for attempt in range(1, attempts + 1):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "K-UI-NeXT/deps"})
            with urllib.request.urlopen(request, timeout=180) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            last = error
            if attempt == attempts:
                break
            delay = 2 ** attempt
            print(f"  {url}: {error}; retry {attempt}/{attempts - 1} in {delay}s", flush=True)
            time.sleep(delay)
    raise SystemExit(f"Could not download {url} after {attempts} attempts: {last}")


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
            print(f"Downloading {spec['url']}", flush=True)
            data = download(spec["url"])
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
