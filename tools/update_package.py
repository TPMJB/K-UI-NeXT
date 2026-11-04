#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and verify bounded, complete K-UI offline update packages.

The console's catalog is the authority. This tool never includes user files,
extracts arbitrary paths, downloads code, or runs package-provided commands.
Hashes detect corruption; offline packages must come from a trusted release.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import os
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
    if not 1 <= len(paths) <= MAX_ENTRIES or len(set(paths)) != len(paths) or paths[0] != "runtime.kui":
        raise ValueError("Invalid console update cohort")
    return tuple(paths)


def legacy_catalog():
    result = {}
    for line in (ROOT / "include/kui/update_legacy.inc").read_text(encoding="ascii").splitlines():
        if not line.strip() or line.lstrip().startswith("/*"):
            continue
        match = re.fullmatch(r'KUI_UPDATE_LEGACY\("([a-z0-9/.-]+)","([0-9a-f]{12})",([0-9]+),"([0-9a-f]{64})"\)', line.strip())
        if not match:
            raise ValueError("Invalid approved legacy component catalog")
        path, build, size, sha = match.groups()
        if path in result or path not in catalog():
            raise ValueError("Duplicate or unexpected legacy component")
        result[path] = {"path": path, "build": build, "bytes": int(size), "sha256": sha}
    return result


def handoff_empty(payload):
    if payload.count(HANDOFF_HEADER) != 1:
        return False
    offset = payload.index(HANDOFF_HEADER)
    return payload[offset:offset + 192] == HANDOFF_HEADER + bytes(176)


def member_info(path, data, build):
    if not 68 <= len(data) <= MAX_MEMBER:
        raise ValueError("Invalid member size: " + path)
    info = verify_runtime(data)
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
    expected = catalog()
    with Path(path).open("rb") as stream:
        header = read_exact(stream, HEADER_BYTES)
        version, header_bytes, count, manifest_bytes, total = struct.unpack_from("<IIIIQ", header, 8)
        build = header[32:44].decode("ascii", errors="replace")
        channel = header[44]
        if (header[:8] != MAGIC or version != 1 or header_bytes != HEADER_BYTES or
                count != len(expected) or manifest_bytes != count * ENTRY_BYTES or
                total > MAX_PAYLOAD or channel not in CHANNELS.values() or
                not re.fullmatch(r"[0-9a-f]{12}", build) or
                any(header[45:64]) or any(header[96:124]) or
                zlib.crc32(header[:124]) != struct.unpack_from("<I", header, 124)[0]):
            raise ValueError("Invalid update header, channel or cohort")
        manifest = read_exact(stream, manifest_bytes)
        if hashlib.sha256(manifest).digest() != header[64:96]:
            raise ValueError("Manifest checksum mismatch")
        entries = []
        for index, name in enumerate(expected):
            entry = manifest[index * ENTRY_BYTES:(index + 1) * ENTRY_BYTES]
            encoded = name.encode("ascii")
            size, = struct.unpack_from("<Q", entry, 80)
            if (entry[:80] != encoded + bytes(80 - len(encoded)) or entry[120] > 1 or any(entry[121:]) or
                    not 68 <= size <= MAX_MEMBER):
                raise ValueError("Invalid or incomplete update member: " + name)
            entries.append({"path": name, "bytes": size, "sha256": entry[88:120].hex(), "policy": entry[120]})
        if sum(entry["bytes"] for entry in entries) != total:
            raise ValueError("Update payload size mismatch")
        for entry in entries:
            actual = member_info(entry["path"], read_exact(stream, entry["bytes"]), build)
            if actual != entry:
                raise ValueError("Member checksum mismatch: " + entry["path"])
        if stream.read(1):
            raise ValueError("Trailing update package data")
    return {"format": 1, "build": build, "channel": next(k for k, v in CHANNELS.items() if v == channel),
            "manifest_sha256": header[64:96].hex(), "payload_bytes": total,
            "file_bytes": HEADER_BYTES + manifest_bytes + total, "members": entries}


def source_member(root, relative):
    path = root
    for part in relative.split("/"):
        path = path / part
        if path.is_symlink():
            raise ValueError("Symlink in update source: " + relative)
    if not path.is_file() or not 68 <= path.stat().st_size <= MAX_MEMBER:
        raise ValueError("Missing or oversized update member: " + relative)
    return path


def read_member(root, relative):
    with source_member(root, relative).open("rb") as stream:
        data = stream.read(MAX_MEMBER + 1)
    if len(data) > MAX_MEMBER:
        raise ValueError("Source grew beyond the member limit: " + relative)
    return data


def build_package(root, output, build, channel="stable"):
    if not re.fullmatch(r"[0-9a-f]{12}", build) or channel not in CHANNELS:
        raise ValueError("Expected a 12-character lowercase build ID and stable/testing channel")
    root, output = Path(root).resolve(), Path(output)
    if not root.is_dir():
        raise ValueError("The source must be the KUI directory of a complete release")
    paths = catalog()
    if output.resolve() in {root / name for name in paths}:
        raise ValueError("The output cannot replace a source member")
    entries = [member_info(name, read_member(root, name), build) for name in paths]
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
    commands.add_parser("catalog")
    args = parser.parse_args()
    try:
        if args.command == "build":
            result = build_package(args.kui_directory, args.output, args.build_id, args.channel)
        elif args.command == "verify":
            result = verify(args.package)
        else:
            result = {"required_members": catalog()}
        print(json.dumps(result, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
