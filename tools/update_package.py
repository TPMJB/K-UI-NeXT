#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and verify bounded K-UI offline update packages.

The console's bounded software namespace is the authority; its catalog supplies
the producer's default members. This tool never includes user files,
extracts arbitrary paths, downloads code, or runs package-provided commands.
Hashes detect corruption; offline packages must come from a trusted release.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import os
import stat
import struct
import tempfile
import zlib

from runtime_package import verify as verify_runtime

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"KUIUPD1\0"
HEADER_BYTES = ENTRY_BYTES = 128
MAX_ENTRIES = 64
MAX_PAYLOAD = 128 * 1024 * 1024
MAX_MEMBER = 4 * 1024 * 1024 + 64
CAPABILITY = b"KUIBANK1" + struct.pack("<II", 1, 0xFFFFFFFE)
HANDOFF_HEADER = b"KUIHOF1\0" + struct.pack("<II", 1, 192)
CHANNELS = {"stable": 0, "testing": 1}
INHERIT_ROOT = 2
WITHDRAWN_BUILDS = frozenset(("28cadccf0e71", "33b04057db86"))
RAW_INHERIT_PATHS = frozenset(("apps/games/smash-vc2-vmu-helper.bin",
    "apps/games/smash-swirl-vmu-helper.bin", "apps/games/vc2-vmu-helper.bin"))


def software_path(path):
    return isinstance(path, str) and len(path) < 80 and (path == "runtime.kui" or
        (re.fullmatch(r"apps/(games|disc)/[a-z0-9][a-z0-9._-]*\.kui", path) is not None and ".." not in path))


def module_path(path):
    return software_path(path) or (isinstance(path, str) and path in RAW_INHERIT_PATHS)


def package_paths(paths):
    if (not isinstance(paths, (list, tuple)) or not 1 <= len(paths) <= MAX_ENTRIES or
            any(not module_path(path) for path in paths) or paths[0] != "runtime.kui" or len(set(paths)) != len(paths)):
        raise ValueError("Members must be unique allowed software paths with runtime.kui first (1 to 64 entries)")
    return tuple(paths)


def catalog():
    lines = (ROOT / "include/kui/update_catalog.inc").read_text(encoding="ascii").splitlines()
    paths = []
    for line in lines:
        if not line.strip() or line.lstrip().startswith("/*"):
            continue
        match = re.fullmatch(r'KUI_UPDATE_MEMBER\("([a-z0-9/.-]+)"\)', line.strip())
        if not match:
            raise ValueError("Invalid console update catalog")
        paths.append(match[1])
    return package_paths(paths)


def legacy_catalog():
    result = {}
    for line in (ROOT / "include/kui/update_legacy.inc").read_text(encoding="ascii").splitlines():
        if not line.strip() or line.lstrip().startswith("/*"):
            continue
        match = re.fullmatch(r'KUI_UPDATE_LEGACY\("([a-z0-9/.-]+)","([0-9a-f]{12})",([0-9]+),"([0-9a-f]{64})"\)', line.strip())
        if not match:
            raise ValueError("Invalid approved legacy component catalog")
        path, build, size, sha = match.groups()
        if path in result or path == "runtime.kui" or not software_path(path):
            raise ValueError("Duplicate or unexpected legacy component")
        result[path] = {"path": path, "build": build, "bytes": int(size), "sha256": sha}
    return result


def handoff_empty(payload):
    if payload.count(HANDOFF_HEADER) != 1:
        return False
    offset = payload.index(HANDOFF_HEADER)
    return payload[offset:offset + 192] == HANDOFF_HEADER + bytes(176)


def member_info(path, data, build):
    if path in RAW_INHERIT_PATHS:
        raise ValueError("Raw helper paths must inherit the installed root file: " + path)
    if not software_path(path):
        raise ValueError("Invalid software member path: " + str(path))
    if not 68 <= len(data) <= MAX_MEMBER:
        raise ValueError("Invalid member size: " + path)
    info = verify_runtime(data)
    if path == "runtime.kui" and info["build"] in WITHDRAWN_BUILDS:
        raise ValueError("GUI build is withdrawn: " + info["build"])
    sha = hashlib.sha256(data).hexdigest()
    policy = 0
    if info["build"] != build:
        pin = legacy_catalog().get(path)
        if not pin or pin["build"] != info["build"] or pin["bytes"] != len(data) or pin["sha256"] != sha:
            raise ValueError("Mixed build cohort or unapproved historical component: " + path)
        policy = 1
    if path == "runtime.kui" and data[64:].count(CAPABILITY) != 1:
        raise ValueError("GUI does not support immutable update banks")
    if path == "runtime.kui" and not handoff_empty(data[64:]):
        raise ValueError("GUI must contain exactly one empty update handoff slot")
    return {"path": path, "bytes": len(data), "sha256": sha, "policy": policy}


def read_exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise ValueError("Truncated update package")
    return data


def verify(path):
    """Validate using at most one bounded member in memory; never extract."""
    with Path(path).open("rb") as stream:
        header = read_exact(stream, HEADER_BYTES)
        version, header_bytes, count, manifest_bytes, total = struct.unpack_from("<IIIIQ", header, 8)
        build = header[32:44].decode("ascii", errors="replace")
        channel = header[44]
        if build in WITHDRAWN_BUILDS:
            raise ValueError("Update GUI build is withdrawn: " + build)
        if (header[:8] != MAGIC or version != 1 or header_bytes != HEADER_BYTES or
                not 1 <= count <= MAX_ENTRIES or manifest_bytes != count * ENTRY_BYTES or
                total > MAX_PAYLOAD or channel not in CHANNELS.values() or
                not re.fullmatch(r"[0-9a-f]{12}", build) or
                any(header[45:64]) or any(header[96:124]) or
                zlib.crc32(header[:124]) != struct.unpack_from("<I", header, 124)[0]):
            raise ValueError("Invalid update header, channel or cohort")
        manifest = read_exact(stream, manifest_bytes)
        if hashlib.sha256(manifest).digest() != header[64:96]:
            raise ValueError("Manifest checksum mismatch")
        entries = []
        seen = set()
        for index in range(count):
            entry = manifest[index * ENTRY_BYTES:(index + 1) * ENTRY_BYTES]
            end = entry[:80].find(b"\0")
            try:
                name = entry[:end].decode("ascii") if end >= 0 else ""
            except UnicodeDecodeError:
                raise ValueError("Unknown update member path") from None
            encoded = name.encode("ascii")
            size, = struct.unpack_from("<Q", entry, 80)
            policy = entry[120]
            if (not module_path(name) or name in seen or (index == 0 and name != "runtime.kui") or
                    entry[:80] != encoded + bytes(80 - len(encoded)) or policy > INHERIT_ROOT or any(entry[121:])):
                raise ValueError("Invalid or incomplete update member: " + name)
            seen.add(name)
            if policy == INHERIT_ROOT:
                if index == 0 or size or any(entry[88:120]):
                    raise ValueError("Invalid inherited root member: " + name)
            elif not 68 <= size <= MAX_MEMBER:
                raise ValueError("Invalid update member size: " + name)
            elif name in RAW_INHERIT_PATHS:
                raise ValueError("Raw helper paths must inherit the installed root file: " + name)
            entries.append({"path": name, "bytes": size, "sha256": entry[88:120].hex(), "policy": policy})
        if sum(entry["bytes"] for entry in entries) != total:
            raise ValueError("Update payload size mismatch")
        for entry in entries:
            if entry["policy"] == INHERIT_ROOT:
                continue
            actual = member_info(entry["path"], read_exact(stream, entry["bytes"]), build)
            if actual != entry:
                raise ValueError("Member checksum mismatch: " + entry["path"])
        if stream.read(1):
            raise ValueError("Trailing update package data")
    return {"format": 1, "build": build, "channel": next(k for k, v in CHANNELS.items() if v == channel),
            "manifest_sha256": header[64:96].hex(), "payload_bytes": total,
            "file_bytes": HEADER_BYTES + manifest_bytes + total, "members": entries}


def source_member(root, relative, *, allow_missing=False):
    path = root
    parts = relative.split("/")
    for index, part in enumerate(parts):
        path = path / part
        try:
            info = path.lstat()
        except FileNotFoundError:
            if allow_missing:
                return None
            raise ValueError("Missing update member: " + relative) from None
        if stat.S_ISLNK(info.st_mode):
            raise ValueError("Symlink in update source: " + relative)
        if index + 1 < len(parts) and not stat.S_ISDIR(info.st_mode):
            raise ValueError("Non-directory in update source: " + relative)
    if not stat.S_ISREG(info.st_mode) or not 68 <= info.st_size <= MAX_MEMBER:
        raise ValueError("Missing or oversized update member: " + relative)
    return path


def read_member(root, relative):
    with source_member(root, relative).open("rb") as stream:
        data = stream.read(MAX_MEMBER + 1)
    if len(data) > MAX_MEMBER:
        raise ValueError("Source grew beyond the member limit: " + relative)
    return data


def build_package(root, output, build, channel="stable", *, inherit_missing=False, paths=None):
    """Build explicit replacements; optionally inherit absent non-GUI paths.

    Inheritance records only the baseline-root path, preserving its presence
    or absence at runtime. It never reads or snapshots an installed baseline.
    """
    if not re.fullmatch(r"[0-9a-f]{12}", build) or channel not in CHANNELS:
        raise ValueError("Expected a 12-character lowercase build ID and stable/testing channel")
    if build in WITHDRAWN_BUILDS:
        raise ValueError("Update GUI build is withdrawn: " + build)
    if not isinstance(inherit_missing, bool):
        raise ValueError("inherit_missing must be an explicit boolean")
    root, output = Path(root).resolve(), Path(output)
    if not root.is_dir():
        raise ValueError("The source must be a KUI release directory")
    paths = catalog() if paths is None else package_paths(paths)
    if output.resolve() in {root / name for name in paths}:
        raise ValueError("The output cannot replace a source member")
    entries = []
    for name in paths:
        if source_member(root, name, allow_missing=inherit_missing and name != "runtime.kui") is None:
            entries.append({"path": name, "bytes": 0, "sha256": "00" * 32, "policy": INHERIT_ROOT})
        else:
            entries.append(member_info(name, read_member(root, name), build))
    total = sum(entry["bytes"] for entry in entries)
    if total > MAX_PAYLOAD:
        raise ValueError("Update payload exceeds 128 MiB")
    manifest = bytearray(len(entries) * ENTRY_BYTES)
    for index, entry in enumerate(entries):
        start = index * ENTRY_BYTES
        path = entry["path"].encode("ascii")
        manifest[start:start + len(path)] = path
        struct.pack_into("<Q", manifest, start + 80, entry["bytes"])
        manifest[start + 88:start + 120] = bytes.fromhex(entry["sha256"])
        manifest[start + 120] = entry["policy"]
    header = bytearray(HEADER_BYTES)
    header[:8] = MAGIC
    struct.pack_into("<IIIIQ", header, 8, 1, HEADER_BYTES, len(entries), len(manifest), total)
    header[32:44] = build.encode("ascii")
    header[44] = CHANNELS[channel]
    header[64:96] = hashlib.sha256(manifest).digest()
    struct.pack_into("<I", header, 124, zlib.crc32(header[:124]))
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".kui-update-", dir=output.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(header)
            stream.write(manifest)
            for entry in entries:
                if entry["policy"] == INHERIT_ROOT:
                    if source_member(root, entry["path"], allow_missing=True) is not None:
                        raise ValueError("Source appeared while packaging: " + entry["path"])
                    continue
                data = read_member(root, entry["path"])
                if member_info(entry["path"], data, build) != entry:
                    raise ValueError("Source changed while packaging: " + entry["path"])
                stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        result = verify(temporary)
        os.replace(temporary, output)
        temporary = None
        return result
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("verify")
    check.add_argument("package", type=Path)
    make = commands.add_parser("build")
    make.add_argument("kui_directory", type=Path)
    make.add_argument("output", type=Path)
    make.add_argument("--build-id", required=True)
    make.add_argument("--channel", choices=tuple(CHANNELS), default="stable")
    make.add_argument("--inherit-missing", action="store_true",
                      help="Explicitly inherit absent non-GUI modules from the retained root baseline")
    make.add_argument("--members", type=Path,
                      help="Explicit JSON array of software paths; defaults to the current build catalog")
    commands.add_parser("catalog")
    args = parser.parse_args()
    try:
        if args.command == "build":
            paths = None if args.members is None else json.loads(args.members.read_text(encoding="utf-8"))
            if args.members is not None and not isinstance(paths, list):
                raise ValueError("--members must contain a JSON array of software paths")
            result = build_package(args.kui_directory, args.output, args.build_id, args.channel,
                                   inherit_missing=args.inherit_missing, paths=paths)
        elif args.command == "verify":
            result = verify(args.package)
        else:
            result = {"required_members": catalog()}
        print(json.dumps(result, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
