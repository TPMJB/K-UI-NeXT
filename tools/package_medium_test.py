#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package one copy-over medium-feature runtime with source and build evidence."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import zipfile
import zlib

from runtime_package import MAX_BYTES, MAX_MEMORY, envelope, flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
GUIDE = "docs/3.0-medium-first-pass.md"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def commit(value):
    require(bool(re.fullmatch(r"[0-9a-f]{7,40}", value)), "Base must be a hexadecimal Git commit")
    return git("rev-parse", "--verify", value + "^{commit}").decode().strip()


def clean_head(expected=None):
    head = git("rev-parse", "HEAD").decode().strip()
    require(not git("status", "--porcelain=v1", "--untracked-files=all"),
            "Source is dirty (including untracked files); commit the complete source and packager first")
    require(expected is None or expected == head, "HEAD changed while packaging")
    return head


def build_info(elf, runtime, record_bytes, head):
    record = json.loads(record_bytes)
    require(isinstance(record, dict) and record.get("commit") == head and
            record.get("source_dirty") is False, "Compile record is not from this clean HEAD")
    require(record.get("elf_sha256") == sha(elf), "ELF hash disagrees with compile record")
    payload, memory = flatten_elf(elf)
    info = verify(runtime)
    require(info["build"] == head[:12] and len(payload) < MAX_BYTES and memory < MAX_MEMORY,
            "Runtime build identifier or strict 4 MiB payload / 8 MiB memory limit refused")
    require(head[:12].encode() + b"\0" in payload, "Compiled display build identifier differs from HEAD")
    require(runtime == envelope(payload, memory, head[:12]), "ELF and runtime envelope disagree")
    return info


def public_mapping(local, public):
    require(bool(re.fullmatch(r"[0-9a-f]{40}", public)), "Public commits must be full lowercase SHA-1 values")
    tree = git("rev-parse", local + "^{tree}").decode().strip()
    available = subprocess.run(["git", "cat-file", "-e", public + "^{commit}"], cwd=ROOT,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if available:
        require(git("rev-parse", public + "^{tree}").decode().strip() == tree,
                "Public commit has a different source tree")
    return {"local_commit": local, "local_tree": tree, "public_commit": public,
            "public_tree_mapping": "verified in local Git" if available else "supplied by caller; public object unavailable locally"}


def readme(guide, head, public):
    text = guide.decode("utf-8")
    require("FLOW_VIEW_PENDING" not in text, "Finish the Flow documentation before packaging")
    text = re.sub(r"\]\((?![a-z]+:|/|#)([^)]+\.md(?:#[^)]+)?)\)", r"](docs/\1)", text)
    title, _, rest = text.partition("\n")
    return (title + "\n\n" +
            (f"Local build: `{head[:12]}`. Public source commit: `{public}`.\n\n" if public else
             f"Local build: `{head[:12]}`. Source is included in this ZIP; this update is not published to GitHub.\n\n") +
            "Copy only `KUI/runtime.kui` onto the existing SD installation with the console powered off. "
            "No Python installer or loader-module replacement is needed. Before testing, back up the "
            "working runtime and preferences/game-choice metadata (or the complete existing KUI folder). "
            "Older runtimes may not understand new schemas and may use fallback/default choices; "
            "restoring only the runtime does not restore those settings. Keep original cards and saves.\n\n" +
            rest.lstrip() + "\n\n" +
            "## Included evidence\n\n"
            "`source/HEAD.tar.gz` is the complete committed source; `source/delta.patch` is the binary-capable "
            "change from the manifest's local base. `build/` contains the linked ELF, map and compile record. "
            "Optional `evidence/` files are host/build logs supplied by the packager caller. "
            "They do not establish physical-console acceptance. `manifest.json` lists file sizes and SHA-256 hashes.\n").encode()


def verify_zip(path, files):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        require(len(names) == len(set(names)) and set(names) == set(files), "ZIP member set mismatch")
        require(all(not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts
                    and "\\" not in name and not name.endswith("/") for name in names), "Unsafe ZIP member")
        require(archive.testzip() is None, "ZIP CRC verification failed")
        manifest = json.loads(archive.read("manifest.json"))
        require(isinstance(manifest, dict) and isinstance(manifest.get("files"), dict), "Invalid manifest structure")
        require(set(manifest["files"]) == set(names) - {"manifest.json"}, "Manifest member set mismatch")
        for name in names:
            data = archive.read(name)
            require(data == files[name], "ZIP content mismatch: " + name)
            if name != "manifest.json":
                require(manifest["files"][name] == {"bytes": len(data), "sha256": sha(data)},
                        "Manifest hash/size mismatch: " + name)


def package(args):
    head = clean_head()
    base = commit(args.base)
    subprocess.run(["git", "merge-base", "--is-ancestor", base, head], cwd=ROOT, check=True)
    source = public_mapping(head, args.public_commit) if args.public_commit else {
        "local_commit": head, "local_tree": git("rev-parse", head + "^{tree}").decode().strip(),
        "public_commit": None, "public_tree_mapping": "not published; exact source included in ZIP"}
    baseline = public_mapping(base, args.public_base)
    output = args.output.resolve()
    require(output.suffix.lower() == ".zip", "Output must be a ZIP path")
    if output.is_relative_to(ROOT):
        relative = str(output.relative_to(ROOT))
        require(subprocess.run(["git", "check-ignore", "-q", "--", relative], cwd=ROOT).returncode == 0,
                "ZIP output inside the checkout must be Git-ignored")
        require(not git("ls-files", "--", relative), "Output may not replace a tracked source file")
    elf = args.elf.read_bytes()
    runtime = args.runtime.read_bytes()
    record = (args.compile_record or args.elf.with_suffix(".compile.json")).read_bytes()
    info = build_info(elf, runtime, record, head)
    files = {"KUI/runtime.kui": runtime, "build/kui-runtime.elf": elf,
             "build/kui-runtime.map": (args.map or args.elf.with_suffix(".map")).read_bytes(),
             "build/kui-runtime.compile.json": record,
             "source/HEAD.tar.gz": gzip.compress(git("archive", "--format=tar", "--prefix=K-UI-NeXT/", head), mtime=0),
             "source/delta.patch": git("diff", "--binary", "--full-index", base, head, "--")}
    require(files["build/kui-runtime.map"], "Linked map is empty")
    # Keep historical evidence and relative guide links usable when the ZIP
    # is opened without unpacking the separate complete source archive.
    docs = set(git("ls-tree", "-r", "--name-only", head, "--", "docs").decode().splitlines())
    docs.add(GUIDE)
    for name in sorted(docs):
        files[name] = git("show", head + ":" + name)
    files["README.md"] = readme(files[GUIDE], head, args.public_commit)
    evidence = []
    for index, path in enumerate(args.evidence, 1):
        require(path.is_file(), "Evidence is not a regular file: " + str(path))
        name = f"evidence/{index:02d}-" + re.sub(r"[^A-Za-z0-9._-]", "_", path.name)
        files[name] = path.read_bytes()
        evidence.append(name)
    manifest = {"format": 1, "purpose": "K-UI 3.0 medium-feature copy-over console test",
                "source": source, "base": baseline, "runtime": info, "install": ["KUI/runtime.kui"],
                "evidence": evidence, "console_acceptance": "not established by this package or host logs",
                "manifest_coverage": "All ZIP members except manifest.json itself",
                "files": {name: {"bytes": len(data), "sha256": sha(data)} for name, data in sorted(files.items())}}
    files["manifest.json"] = wire(manifest)
    # All reads are complete before final publication. Never rebuild artifacts
    # or modify an input; an interrupted output copy cannot replace an old ZIP.
    with tempfile.TemporaryDirectory(prefix="kui-medium-package-") as stage:
        staged = Path(stage) / "candidate.zip"
        with zipfile.ZipFile(staged, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(files.items()):
                entry = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, data, compresslevel=9)
        verify_zip(staged, files)
        clean_head(head)
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(prefix="." + output.name + ".", dir=output.parent, delete=False) as stream:
                temporary = Path(stream.name)
                with staged.open("rb") as source_stream:
                    shutil.copyfileobj(source_stream, stream)
                stream.flush()
                os.fsync(stream.fileno())
            verify_zip(temporary, files)
            os.replace(temporary, output)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
    return {"output": str(output), "bytes": output.stat().st_size, "sha256": sha(output.read_bytes()),
            "members": len(files), "build": info["build"], "source": source, "base": baseline}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--base", required=True, help="Local source base commit (abbreviated SHA accepted)")
    parser.add_argument("--public-commit", help="Published source commit, when available; otherwise package local source only")
    parser.add_argument("--public-base", required=True)
    parser.add_argument("--runtime", type=Path, default=ROOT / "build/runtime.kui")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/kui-runtime.elf")
    parser.add_argument("--map", type=Path)
    parser.add_argument("--compile-record", type=Path)
    parser.add_argument("--evidence", type=Path, action="append", default=[])
    try:
        print(json.dumps(package(parser.parse_args()), indent=2))
    except (OSError, ValueError, KeyError, EOFError, zlib.error, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
