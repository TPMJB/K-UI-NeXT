#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect exact release sources and verified dependency sources, offline.

Compiler executables and generated SDK trees are never copied. An installed
compiler or separately obtained compiler sources are still needed to rebuild.
"""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NEWLIB_VERSION = "4.6.0.20260123"
NEWLIB_SHA256 = "6ff27e3bf022666f43f7802255be680eeff722ac181b1725d21e2e8318604ee3"
NON_SOURCE_SUFFIXES = {".vmd", ".vms", ".vmi", ".dci", ".cdi", ".iso",
                       ".elf", ".exe", ".dll", ".so", ".a", ".o", ".kui"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def blob_sha(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def source_path(name):
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or "\\" in name or
            any(part in ("", ".", "..") for part in name.split("/"))):
        raise ValueError("Unsafe source archive path")
    return path


def audit_archive(data, *, permitted_source_names=()):
    """Inspect source-only archives without extracting arbitrary paths."""
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as archive:
        for member in archive:
            name = member.name.rstrip("/")
            parsed = source_path(name)
            if member.isdir():
                continue
            if not member.isfile() or name in files:
                raise ValueError("Linked, special or duplicate source archive member")
            if parsed.parts[0] in (".deps", "build", "dist"):
                raise ValueError("Generated SDK/build output in source archive")
            if parsed.suffix.lower() in NON_SOURCE_SUFFIXES and name not in permitted_source_names:
                raise ValueError("Save, disc image or executable in source archive: " + name)
            body = archive.extractfile(member).read()
            if body.startswith(b"\x7fELF") or body.startswith(b"MZ"):
                raise ValueError("Compiled executable in source archive: " + name)
            files[name] = (member.mode & 0o777, body)
        comment = archive.pax_headers.get("comment")
    return files, comment


def archive_bytes(prefix, files):
    output = io.BytesIO()
    with gzip.GzipFile(fileobj=output, mode="wb", filename="", mtime=0) as zipped:
        with tarfile.open(fileobj=zipped, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for name, (mode, body) in sorted(files.items()):
                source_path(name)
                info = tarfile.TarInfo(prefix + "/" + name)
                info.mode = mode
                info.size = len(body)
                info.mtime = 0
                archive.addfile(info, io.BytesIO(body))
    data = output.getvalue()
    audit_archive(data)
    return data


def project_archive(commit):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Source commits must be full lowercase Git object IDs")
    if git("rev-parse", commit + "^{commit}").decode().strip() != commit:
        raise ValueError("Source revision does not identify an exact local commit")
    prefix = "K-UI-" + commit[:12] + "/"
    raw = git("-c", "tar.umask=022", "archive", "--format=tar", "--prefix=" + prefix, commit)
    data = gzip.compress(raw, mtime=0)
    files, comment = audit_archive(data)
    if comment != commit:
        raise ValueError("Project archive lacks its exact Git commit comment")
    expected = {}
    for row in git("ls-tree", "-rz", commit).split(b"\0"):
        if not row:
            continue
        fields, name = row.split(b"\t", 1)
        mode, kind, digest = fields.decode().split()
        if kind != "blob" or mode not in ("100644", "100755"):
            raise ValueError("Unexpected source tree link or submodule")
        expected[prefix + name.decode()] = (int(mode, 8) & 0o777, digest)
    if set(expected) != set(files):
        raise ValueError("Git archive and exact source tree paths differ")
    for name, (mode, digest) in expected.items():
        if files[name][0] != mode or blob_sha(files[name][1]) != digest:
            raise ValueError("Git archive source bytes or executable mode differ: " + name)
    return data, files


def kos_sources(lock, metadata_dir):
    commit = json.loads((metadata_dir / "kos-commit.json").read_text())
    tree = json.loads((metadata_dir / "kos-tree.json").read_text())
    pin = lock["kos"]["commit"]
    if commit["sha"] != pin or tree.get("truncated") is not False or tree["sha"] != commit["tree"]["sha"]:
        raise ValueError("KOS source metadata does not identify the complete pinned tree")
    entries = {}
    children = {}
    files = {}
    for entry in tree["tree"]:
        name = entry["path"]
        source_path(name)
        if name in entries:
            raise ValueError("Duplicate KOS tree path")
        entries[name] = entry
        parent, _, leaf = name.rpartition("/")
        children.setdefault(parent, []).append((leaf, entry))
        if entry["type"] == "blob" and entry["mode"] in ("100644", "100755"):
            path = ROOT / ".deps/kos" / name
            if path.is_symlink() or not path.is_file():
                raise ValueError("Missing regular pinned KOS source: " + name)
            body = path.read_bytes()
            if blob_sha(body) != entry["sha"]:
                raise ValueError("Pinned KOS source blob differs: " + name)
            files[name] = (int(entry["mode"], 8) & 0o777, body)
        elif entry["type"] != "tree" or entry["mode"] != "040000":
            raise ValueError("Unsupported KOS source tree entry")
    for name in entries:
        parent = name.rpartition("/")[0]
        if parent and (parent not in entries or entries[parent]["type"] != "tree"):
            raise ValueError("KOS entry has an unlisted or non-tree parent")
    computed = {}
    for directory in sorted(children, key=lambda item: (item.count("/"), len(item)), reverse=True):
        body = bytearray()
        for leaf, entry in sorted(children[directory],
                                  key=lambda item: (item[0] + ("/" if item[1]["type"] == "tree" else "")).encode()):
            name = directory + "/" + leaf if directory else leaf
            if entry["type"] == "tree" and computed.get(name) != entry["sha"]:
                raise ValueError("Incomplete or altered KOS source subtree")
            mode = "40000" if entry["type"] == "tree" else entry["mode"]
            body.extend(mode.encode() + b" " + leaf.encode() + b"\0" + bytes.fromhex(entry["sha"]))
        digest = hashlib.sha1(b"tree " + str(len(body)).encode() + b"\0" + body).hexdigest()
        computed[directory] = digest
    if computed.get("") != tree["sha"]:
        raise ValueError("KOS recursive source tree does not hash to its pinned root")
    tracked_files = len(files)
    excluded = []
    for name in list(files):
        if PurePosixPath(name).suffix.lower() in NON_SOURCE_SUFFIXES:
            if not name.startswith("examples/"):
                raise ValueError("Unexpected prebuilt file in required KOS source")
            mode, body = files.pop(name)
            excluded.append({"path": name, "git_blob": blob_sha(body), "sha256": sha(body),
                             "reason": "Unlinked upstream example payload; not corresponding kernel/build source"})
    return archive_bytes("KallistiOS-" + pin[:12], files), {
        "commit": pin, "tree": tree["sha"], "verified_tracked_files": tracked_files,
        "archived_source_files": len(files), "excluded_prebuilt_examples": excluded,
        "all_blob_bytes_modes_and_recursive_tree_hashes_verified": True,
        "commit_tree_binding": "https://api.github.com/repos/KallistiOS/KallistiOS/git/commits/" + pin,
    }


def fatfs_sources(commit, lock):
    originals = {}
    for item in lock["fatfs"]:
        name = item["url"].rsplit("/", 1)[-1]
        body = git("show", commit + ":third_party/fatfs-downloads/" + name)
        if sha(body) != item["sha256"]:
            raise ValueError("Pinned FatFs input differs in source commit")
        originals[name] = (0o644, body)
    files = {}
    with zipfile.ZipFile(io.BytesIO(originals["ff16.zip"][1])) as zipped:
        for name in ("source/ff.c", "source/ff.h", "source/diskio.h", "source/ffunicode.c", "LICENSE.txt"):
            files[name] = (0o644, zipped.read(name).replace(b"\r\n", b"\n"))
    with tempfile.TemporaryDirectory(prefix="kui-fatfs-source-") as temp:
        target = Path(temp) / "ff.c"
        target.write_bytes(files["source/ff.c"][1])
        for item in lock["fatfs"][1:]:
            name = item["url"].rsplit("/", 1)[-1]
            subprocess.run(["patch", "--batch", "--fuzz=0", str(target)],
                           input=originals[name][1], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        files["source/ff.c"] = (0o644, target.read_bytes())
    files["source/ffconf.h"] = (0o644, git("show", commit + ":config/ffconf.h"))
    for name, (_, body) in files.items():
        current = ROOT / ".deps/fatfs" / name
        if current.is_symlink() or not current.is_file() or current.read_bytes() != body:
            raise ValueError("Compiled FatFs source/config differs from requested source commit: " + name)
    return files, originals


def source_bundle(output_dir, commits, legacy_sources=None, *, kos_metadata_dir=None,
                  newlib_source=None):
    """Return artifact paths/hashes; distributable provenance contains no local paths."""
    output = Path(output_dir).absolute()
    if output.is_symlink():
        raise ValueError("Source output directory may not be a symlink")
    output.mkdir(parents=True, exist_ok=True)
    metadata = Path(kos_metadata_dir or ROOT / ".deps/update-source")
    if not isinstance(commits, dict):
        commits = {commit[:12]: commit for commit in commits}
    if not commits:
        raise ValueError("At least one exact source commit is required")
    if legacy_sources is None:
        raise ValueError("The complete release needs its approved historical source manifest")
    artifacts = []
    provenance = {"schema": "kui.update.corresponding-source.v1", "projects": {},
                  "dependencies": {}, "historical": {}, "artifacts": [],
                  "compiler_executables_included": False,
                  "offline_compiler_bootstrap_included": False}

    def save(name, data):
        source_path(name)
        target = output / name
        if target.is_symlink():
            raise ValueError("Source output leaf may not be a symlink")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        info = {"file": name, "bytes": len(data), "sha256": sha(data)}
        provenance["artifacts"].append(info)
        artifacts.append({"path": str(target), **info})
        return info

    lock = None
    for label, commit in commits.items():
        if not re.fullmatch(r"[a-zA-Z0-9_.-]+", label):
            raise ValueError("Invalid source cohort label")
        data, files = project_archive(commit)
        current_lock = json.loads(git("show", commit + ":dependencies.json"))
        if lock is not None and current_lock != lock:
            raise ValueError("Cohorts with different dependency locks need separate source bundles")
        lock = current_lock
        prefix = "K-UI-" + commit[:12] + "/"
        for relative, expected in lock["lwext4"]["files_sha256"].items():
            if sha(files[prefix + lock["lwext4"]["path"] + "/" + relative][1]) != expected:
                raise ValueError("Vendored lwext4 source pin differs")
        for name in ("stb_vorbis", "stb_image"):
            if sha(files[prefix + "third_party/stb/" + lock[name]["path"]][1]) != lock[name]["sha256"]:
                raise ValueError("Vendored stb source pin differs")
        record = save("K-UI-" + label + "-" + commit[:12] + ".tar.gz", data)
        record.update({"commit": commit, "tree": git("rev-parse", commit + "^{tree}").decode().strip(),
                       "tracked_files": len(files), "all_archived_blobs_and_modes_match_git": True})
        provenance["projects"][label] = record
        fatfs, originals = fatfs_sources(commit, lock)
        fatfs_archive = archive_bytes("fatfs", fatfs)
        fatfs_name = "dependencies/fatfs-build-source-" + sha(fatfs_archive)[:12] + ".tar.gz"
        if not any(item["file"] == fatfs_name for item in provenance["artifacts"]):
            save(fatfs_name, fatfs_archive)
        provenance["projects"][label]["fatfs_source"] = fatfs_name
    kos, kos_proof = kos_sources(lock, metadata)
    kos_proof.update(save("dependencies/kos-" + lock["kos"]["commit"][:12] + "-source.tar.gz", kos))
    provenance["dependencies"]["kos"] = kos_proof
    save("dependencies/kos-source-tree.json", json.dumps({
        "commit": lock["kos"]["commit"],
        "commit_tree_binding": kos_proof["commit_tree_binding"],
        "tree": json.loads((metadata / "kos-tree.json").read_text()),
    }, indent=2).encode() + b"\n")
    for name, (_, body) in originals.items():
        save("dependencies/fatfs-originals/" + name, body)
    provenance["dependencies"]["fatfs"] = {"pinned_inputs": lock["fatfs"],
        "patches_applied_with_fuzz_zero": True, "compiled_sources_and_config_match": True}
    newlib_path = Path(newlib_source or metadata / ("newlib-" + NEWLIB_VERSION + ".tar.gz"))
    newlib = newlib_path.read_bytes()
    if lock["toolchain"]["newlib"] != NEWLIB_VERSION or sha(newlib) != NEWLIB_SHA256:
        raise ValueError("Newlib source does not match the pinned recovered input")
    permitted = ("newlib-" + NEWLIB_VERSION + "/libgloss/mips/hal/libcm3.a",
                 "newlib-" + NEWLIB_VERSION + "/makefile.vms")
    newlib_files, _ = audit_archive(newlib, permitted_source_names=permitted)
    provenance["dependencies"]["newlib"] = save("dependencies/newlib-" + NEWLIB_VERSION + ".tar.gz", newlib)
    provenance["dependencies"]["newlib"]["kos_patch_and_build_scripts"] = "Included in the verified KOS source archive"
    provenance["dependencies"]["newlib"]["source_filename_exceptions"] = [
        {"path": name, "sha256": sha(newlib_files[name][1]),
         "reason": "Textual upstream linker script or OpenVMS makefile, not compiled code"}
        for name in permitted]
    provenance["dependencies"]["vendored"] = "Exact project archives include lwext4, stb, miniz, minilzo, fonts, kwlink and all retained notices"
    provenance["dependencies"]["toolchain"] = lock["toolchain"]
    provenance["dependencies"]["compiler_runtime_notice"] = "historical/notices/LICENSES/GCC-runtime-exception.txt"

    if legacy_sources is not None:
        legacy = json.loads(Path(legacy_sources).read_text()) if isinstance(legacy_sources, (str, Path)) else legacy_sources
        provenance["historical"] = {"archives": [], "metadata": [], "licenses": []}
        for group in ("archives", "metadata", "licenses"):
            for spec in legacy.get(group, []):
                source = Path(spec["path"])
                data = source.read_bytes()
                if sha(data) != spec["sha256"]:
                    raise ValueError("Historical source or evidence digest differs")
                if group == "archives":
                    _, comment = audit_archive(data)
                    if comment != spec.get("git_archive_commit"):
                        raise ValueError("Historical source Git archive comment differs")
                    name = "historical/" + source.name
                elif group == "licenses":
                    parts = source.parts
                    if "historical-notices" not in parts:
                        raise ValueError("Historical notice lacks its curated source root")
                    name = "historical/notices/" + "/".join(parts[parts.index("historical-notices") + 1:])
                else:
                    # Curated original binding metadata is relative/public; do
                    # not copy recovery reports containing host or Library IDs.
                    text = data.decode("utf-8")
                    if re.search(r"/workspace/|libfile_|https?://[^\s\"]*[?&](?:sig|token)=", text, re.I):
                        raise ValueError("Private host or transfer metadata in historical evidence")
                    json.loads(text)
                    name = "historical/" + source.name
                record = save(name, data)
                if group == "archives":
                    record["git_archive_commit"] = spec["git_archive_commit"]
                provenance["historical"][group].append(record)
        supplied_archives = {item["file"].split("/")[-1]: item["sha256"]
                             for item in provenance["historical"]["archives"]}
        rebuilds = [item for item in provenance["historical"]["metadata"]
                    if item["file"] == "historical/exact-rebuild-results.json"]
        if len(rebuilds) != 1:
            raise ValueError("Historical source coverage needs its exact-byte rebuild records")
        proof = json.loads((output / rebuilds[0]["file"]).read_text())
        paths = {"apps/games/cdda-toy-raw.kui", "apps/games/cdda-toy-cooked.kui",
                 "apps/games/retail-cdda-mdk2.kui"}
        if (len(proof["components"]) != 3 or {item["path"] for item in proof["components"]} != paths or
                any(item.get("byte_identical") is not True or
                    supplied_archives.get(item["source_archive"]) != item["source_archive_sha256"]
                    for item in proof["components"])):
            raise ValueError("Historical exact-rebuild records do not cover all three source bindings")
        if not (output / provenance["dependencies"]["compiler_runtime_notice"]).is_file():
            raise ValueError("GCC runtime exception notice is missing")
        provenance["historical"]["all_three_frozen_adapters_have_exact_byte_rebuild_records"] = True
    readme = ("Corresponding source for the supplied K-UI test cohorts\n\n"
              "Project archives are exact Git archives with per-file blob and mode checks.\n"
              "KOS contains only files verified against the pinned upstream Git tree.\n"
              "FatFs sources are reconstructed from pinned originals and official patches,\n"
              "using each cohort's configuration. Vendored inputs and notices remain in\n"
              "the project archives. Newlib source and the KOS patches are included.\n\n"
              "Historical source archives and exact-byte rebuild recipes, when supplied,\n"
              "cover the three approved frozen adapters separately from current cohorts.\n\n"
              "Use an installed matching SH compiler and each project's build scripts.\n"
              "This source collection does not include compiler executables or the full\n"
              "GCC/binutils compiler source set needed to bootstrap a compiler offline.\n")
    save("README-SOURCE.txt", readme.encode())
    proof_path = output / "source-provenance.json"
    proof_path.write_text(json.dumps(provenance, indent=2) + "\n")
    return {"artifacts": artifacts, "provenance_path": str(proof_path),
            "provenance_sha256": sha(proof_path.read_bytes()), "provenance": provenance}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--commit", action="append", required=True, help="label=full-commit")
    parser.add_argument("--legacy-sources", type=Path)
    parser.add_argument("--kos-metadata-dir", type=Path)
    parser.add_argument("--newlib-source", type=Path)
    args = parser.parse_args()
    try:
        commits = dict(item.split("=", 1) for item in args.commit)
        result = source_bundle(args.output_dir, commits, args.legacy_sources,
                               kos_metadata_dir=args.kos_metadata_dir, newlib_source=args.newlib_source)
        print(json.dumps({key: value for key, value in result.items() if key != "provenance"}, indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
