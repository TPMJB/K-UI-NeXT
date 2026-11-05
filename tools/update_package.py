#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build verified current-engine K-UI test and software-release packages.

The ordinary producer inventories staged software and runs the actual current
C consumer before delivery. The old catalog-based v1 producer remains only for
explicit bootstrap/migration use. No user files or package-provided commands
are included or executed. Hashes establish integrity, not publisher identity.
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
import shlex
import subprocess

from runtime_package import verify as verify_runtime

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"KUIUPD1\0"
V2_MAGIC = b"KUIUPD2\0"
HEADER_BYTES = ENTRY_BYTES = 128
V2_ENTRY_BYTES = 160
V1_MAX_ENTRIES = 64
MAX_ENTRIES = 256
MAX_PAYLOAD = 128 * 1024 * 1024
MAX_MEMBER = 4 * 1024 * 1024 + 64
CAPABILITY = b"KUIBANK1" + struct.pack("<II", 1, 0xFFFFFFFE)
ENGINE = b"KUIENG2!" + struct.pack("<II", 2, 0xFFFFFFFD)
HANDOFF_HEADER = b"KUIHOF1\0" + struct.pack("<II", 1, 192)
CHANNELS = {"stable": 0, "testing": 1}
INHERIT_ROOT = 2
SELF_KUI, RAW_SOFTWARE, REMOVE = 3, 4, 5
FULL = 1
WITHDRAWN_BUILDS = frozenset(("28cadccf0e71", "33b04057db86"))
RAW_INHERIT_PATHS = frozenset(("apps/games/smash-vc2-vmu-helper.bin",
    "apps/games/smash-swirl-vmu-helper.bin", "apps/games/vc2-vmu-helper.bin"))


def software_path(path):
    return isinstance(path, str) and len(path) < 80 and (path == "runtime.kui" or
        (re.fullmatch(r"apps/(games|disc)/[a-z0-9][a-z0-9._-]*\.kui", path) is not None and ".." not in path))


def module_path(path):
    return software_path(path) or (isinstance(path, str) and path in RAW_INHERIT_PATHS)


def package_paths(paths):
    if (not isinstance(paths, (list, tuple)) or not 1 <= len(paths) <= V1_MAX_ENTRIES or
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


def verify_v1(path):
    """Validate using at most one bounded member in memory; never extract."""
    with Path(path).open("rb") as stream:
        header = read_exact(stream, HEADER_BYTES)
        version, header_bytes, count, manifest_bytes, total = struct.unpack_from("<IIIIQ", header, 8)
        build = header[32:44].decode("ascii", errors="replace")
        channel = header[44]
        if build in WITHDRAWN_BUILDS:
            raise ValueError("Update GUI build is withdrawn: " + build)
        if (header[:8] != MAGIC or version != 1 or header_bytes != HEADER_BYTES or
                not 1 <= count <= V1_MAX_ENTRIES or manifest_bytes != count * ENTRY_BYTES or
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
    """Legacy v1 bootstrap/migration producer, retained for explicit callers.

    Ordinary tests and releases must use build_package_v2/current CLI build.
    Build explicit replacements; optionally inherit absent non-GUI paths.

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


def software_path_v2(path):
    """Match the current consumer's immutable-software namespace."""
    if (not isinstance(path, str) or not 0 < len(path) < 80 or
            ".." in path or not path.isascii()):
        return False
    if path in ("runtime.kui", "tools.kui", "recovery.kui", "redump.db", "tosec.db"):
        return True
    if path == "apps/games/probe.dat":
        return True
    if re.fullmatch(r"apps/(games|disc|music)/[a-z0-9][a-z0-9._-]*\.(kui|bin)", path):
        return True
    if re.fullmatch(r"apps/music/[a-z0-9][a-z0-9._-]*\.(ogg|wav)", path):
        return True
    if not path.startswith("assets/"):
        return False
    parts = path[7:].split("/")
    return 1 <= len(parts) <= 4 and all(not p.endswith(".") and
        re.fullmatch(r"[a-z0-9][a-z0-9._-]*", p) for p in parts)


def member_info_v2(path, data):
    if not software_path_v2(path):
        raise ValueError("Not an immutable software path: " + str(path))
    if len(data) > MAX_PAYLOAD:
        raise ValueError("Software member exceeds package limit: " + path)
    own_build = ""
    policy = RAW_SOFTWARE
    if path.endswith(".kui"):
        if not 68 <= len(data) <= MAX_MEMBER:
            raise ValueError("Invalid .kui size: " + path)
        info = verify_runtime(data)
        own_build = info["build"]
        policy = SELF_KUI
        if path == "runtime.kui":
            if own_build in WITHDRAWN_BUILDS:
                raise ValueError("GUI build is withdrawn: " + own_build)
            payload = data[64:]
            if payload.count(CAPABILITY) != 1 or not handoff_empty(payload):
                raise ValueError("GUI has invalid bank or handoff capability: " + path)
            if payload.count(ENGINE) != 1:
                raise ValueError("GUI needs the current updater engine (v2); build the GUI from current Main")
    return {"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "policy": policy, "build": own_build}


def verify_v2(path):
    """Check v2 bytes independently before the actual C consumer checks them."""
    with Path(path).open("rb") as stream:
        header = read_exact(stream, HEADER_BYTES)
        version, header_bytes, count, manifest_bytes, total = struct.unpack_from("<IIIIQ", header, 8)
        package_id = header[32:44].decode("ascii", errors="replace")
        channel = header[44]
        flags = header[45]
        if (header[:8] != V2_MAGIC or version != 2 or header_bytes != HEADER_BYTES or
                not 1 <= count <= MAX_ENTRIES or manifest_bytes != count * V2_ENTRY_BYTES or
                total > MAX_PAYLOAD or channel not in CHANNELS.values() or
                not re.fullmatch(r"[0-9a-f]{12}", package_id) or
                flags & ~FULL or any(header[46:64]) or any(header[96:124]) or
                zlib.crc32(header[:124]) != struct.unpack_from("<I", header, 124)[0]):
            raise ValueError("Invalid v2 update header")
        manifest = read_exact(stream, manifest_bytes)
        if hashlib.sha256(manifest).digest() != header[64:96]:
            raise ValueError("Manifest checksum mismatch")
        entries, seen = [], set()
        for index in range(count):
            row = manifest[index * V2_ENTRY_BYTES:(index + 1) * V2_ENTRY_BYTES]
            end = row[:80].find(b"\0")
            try:
                name = row[:end].decode("ascii") if end >= 0 else ""
                own_build = row[128:140].decode("ascii")
            except UnicodeDecodeError:
                raise ValueError("Invalid v2 software entry") from None
            size, = struct.unpack_from("<Q", row, 80)
            policy = row[120]
            if (not software_path_v2(name) or name in seen or
                    row[:80] != name.encode() + bytes(80 - len(name)) or
                    any(row[121:128]) or any(row[140:]) or
                    policy not in (INHERIT_ROOT, SELF_KUI, RAW_SOFTWARE, REMOVE)):
                raise ValueError("Invalid v2 member: " + name)
            seen.add(name)
            if policy in (INHERIT_ROOT, REMOVE):
                if (name == "runtime.kui" and policy == REMOVE) or size or any(row[88:120]) or any(row[128:140]):
                    raise ValueError("Invalid keep/remove entry: " + name)
                own_build = ""
            elif policy == SELF_KUI:
                if (not name.endswith(".kui") or not 68 <= size <= MAX_MEMBER or
                        not re.fullmatch(r"[0-9a-f]{12}", own_build) or not any(row[88:120])):
                    raise ValueError("Invalid self-described .kui entry: " + name)
            elif (name.endswith(".kui") or size > MAX_PAYLOAD or
                    any(row[128:140]) or not any(row[88:120])):
                raise ValueError("Invalid raw software entry: " + name)
            else:
                own_build = ""
            entries.append({"path": name, "bytes": size, "sha256": row[88:120].hex(),
                            "policy": policy, "build": own_build})
        if flags & FULL:
            if not any(e["path"] == "runtime.kui" and e["policy"] == SELF_KUI for e in entries):
                raise ValueError("Full software package needs its current-engine runtime.kui replacement")
            if any(e["policy"] == INHERIT_ROOT for e in entries):
                raise ValueError("Full software package cannot depend on unspecified prior software")
        if sum(entry["bytes"] for entry in entries) != total:
            raise ValueError("Update payload size mismatch")
        for entry in entries:
            if entry["policy"] in (INHERIT_ROOT, REMOVE):
                continue
            if entry["policy"] == SELF_KUI:
                actual = member_info_v2(entry["path"], read_exact(stream, entry["bytes"]))
                if actual != entry:
                    raise ValueError("Member metadata/checksum mismatch: " + entry["path"])
            else:
                remaining, digest = entry["bytes"], hashlib.sha256()
                while remaining:
                    data = read_exact(stream, min(remaining, 1024 * 1024))
                    digest.update(data)
                    remaining -= len(data)
                if digest.hexdigest() != entry["sha256"]:
                    raise ValueError("Raw software checksum mismatch: " + entry["path"])
        if stream.read(1):
            raise ValueError("Trailing update package data")
    return {"format": 2, "build": package_id, "channel": next(k for k, v in CHANNELS.items() if v == channel),
            "engine_minimum": 2, "flags": flags, "full": bool(flags & FULL),
            "manifest_sha256": header[64:96].hex(), "payload_bytes": total,
            "file_bytes": HEADER_BYTES + manifest_bytes + total, "members": entries}


def verify(path):
    with Path(path).open("rb") as stream:
        magic = read_exact(stream, 8)
    if magic == V2_MAGIC:
        return verify_v2(path)
    return verify_v1(path)


def inventory_v2(root):
    """Discover every staged software file; never fall back to a catalog."""
    root = Path(root).absolute()
    if root.is_symlink() or not root.is_dir() or any(p.is_symlink() for p in root.parents):
        raise ValueError("The stage must be a regular KUI software directory without symlink parents")
    paths = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode):
            raise ValueError("Symlink in staged software: " + relative)
        if stat.S_ISDIR(info.st_mode):
            # Empty data directories are also rejected: a stage is software,
            # not an installation/card backup that happens to include saves.
            if not (relative in ("apps", "apps/games", "apps/disc", "apps/music", "assets") or
                    relative.startswith("assets/") and software_path_v2(relative + "/x")):
                raise ValueError("Non-software directory in stage: " + relative)
            continue
        if not stat.S_ISREG(info.st_mode) or not software_path_v2(relative):
            raise ValueError("Non-software file in stage: " + relative)
        paths.append(relative)
    paths.sort(key=lambda p: (p != "runtime.kui", p))
    return tuple(paths)


_CONSUMER_CACHE = None
CONSUMER_SOURCES = ("tools/update_consumer_check.c", "src/core/update.c", "src/core/runtime_image.c",
                    "src/core/update_handoff.c", "src/core/hash.c", "src/core/data.c")
CONSUMER_INPUTS = (*CONSUMER_SOURCES, "include/kui/update.h", "include/kui/update_boot.h",
                   "include/kui/update_handoff.h", "include/kui/runtime.h", "include/kui/apps.h",
                   "include/kui/probe.h", "include/kui/core.h", "include/kui/vmu_image.h", "include/kui/hash.h",
                   "include/kui/update_catalog.inc", "include/kui/update_legacy.inc")


def consumer_fingerprint():
    digest = hashlib.sha256()
    for name in CONSUMER_INPUTS:
        data = (ROOT / name).read_bytes()
        digest.update(name.encode() + b"\0" + hashlib.sha256(data).digest())
    for name in ("ff.h", "ffconf.h"):
        try:
            data = (ROOT / ".deps/fatfs/source" / name).read_bytes()
        except OSError as error:
            raise ValueError("Pinned FatFs headers are missing; prepare the current Main build dependencies") from error
        digest.update(("pinned-fatfs/" + name).encode() + b"\0" + hashlib.sha256(data).digest())
    digest.update(b"HOST_CC\0" + os.environ.get("HOST_CC", "cc").encode())
    return digest.hexdigest()


def current_consumer_check(package):
    """Compile/run the actual current decoder; no older decoder is selected."""
    global _CONSUMER_CACHE
    before = consumer_fingerprint()
    if _CONSUMER_CACHE is None or _CONSUMER_CACHE[0] != before:
        if _CONSUMER_CACHE is not None:
            _CONSUMER_CACHE[1].cleanup()
            _CONSUMER_CACHE = None
        temporary = tempfile.TemporaryDirectory(prefix="kui-current-consumer-")
        binary = Path(temporary.name) / "update-consumer"
        fatfs = ROOT / ".deps/fatfs/source"
        if not (fatfs / "ff.h").is_file():
            temporary.cleanup()
            raise ValueError("Pinned FatFs headers are missing; prepare the current Main build dependencies")
        command = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-I" + str(ROOT / "include"), "-I" + str(fatfs),
                   "-I" + str(ROOT / "src/dreamcast"), *(str(ROOT / p) for p in CONSUMER_SOURCES),
                   "-o", str(binary)]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode:
            temporary.cleanup()
            raise ValueError("Current consumer checker did not compile:\n" + compiled.stderr[-6000:])
        if consumer_fingerprint() != before:
            temporary.cleanup()
            raise ValueError("Updater source changed during consumer compilation; rerun packaging")
        _CONSUMER_CACHE = (before, temporary, binary)
    checked = subprocess.run([str(_CONSUMER_CACHE[2]), str(Path(package).resolve())],
                             cwd=ROOT, text=True, capture_output=True)
    if checked.returncode:
        raise ValueError("Current Main consumer rejected the generated package:\n" + checked.stderr.strip())
    if consumer_fingerprint() != before:
        raise ValueError("Updater source changed during verification; rerun packaging")
    report = json.loads(checked.stdout)
    report["source_sha256"] = before
    return report


def build_package_v2(root, output, channel="testing", *, remove=(), keep=(), release=False, full=False):
    """Package staged software and verify it before making it available.

    Partial tests preserve other effective software. Full/release packages
    define the complete managed software inventory, including omitted absence.
    """
    if channel not in CHANNELS or not isinstance(release, bool) or not isinstance(full, bool):
        raise ValueError("Expected stable/testing channel and explicit release/full booleans")
    full = full or release
    flags = FULL if full else 0
    if full and keep:
        raise ValueError("A full software package cannot keep unspecified prior-version software")
    root, output = Path(root).absolute(), Path(output).absolute()
    paths = inventory_v2(root)
    root = root.resolve()
    if output.is_symlink() or any(p.is_symlink() for p in output.parents):
        raise ValueError("The output must not use symlink paths")
    output = output.resolve()
    if output.is_relative_to(root):
        raise ValueError("The .kup output must be outside the staged software directory")
    declared = list(paths)
    for name in (*remove, *keep):
        if not software_path_v2(name) or name in declared:
            raise ValueError("Duplicate or non-software directive: " + str(name))
        declared.append(name)
    if not 1 <= len(declared) <= MAX_ENTRIES:
        raise ValueError("A package needs 1 to 256 distinct software replacements/directives")
    if full and "runtime.kui" not in paths:
        raise ValueError("A full software release must stage its current-engine runtime.kui")
    if sum((root / name).stat().st_size for name in paths) > MAX_PAYLOAD:
        raise ValueError("Staged software exceeds the 128 MiB package limit")
    data = {}
    entries = []
    loaded_bytes = 0
    for name in sorted(declared, key=lambda p: (p != "runtime.kui", p)):
        if name in paths:
            file = root / name
            if file.stat().st_size > MAX_PAYLOAD:
                raise ValueError("Staged software member is oversized: " + name)
            contents = file.read_bytes()
            loaded_bytes += len(contents)
            if loaded_bytes > MAX_PAYLOAD:
                raise ValueError("Staged software grew beyond the 128 MiB package limit")
            data[name] = contents
            entries.append(member_info_v2(name, contents))
        else:
            entries.append({"path": name, "bytes": 0, "sha256": "00" * 32,
                            "policy": REMOVE if name in remove else INHERIT_ROOT, "build": ""})
    total = sum(e["bytes"] for e in entries)
    if total > MAX_PAYLOAD:
        raise ValueError("Update payload exceeds 128 MiB")
    manifest = bytearray(V2_ENTRY_BYTES * len(entries))
    for index, entry in enumerate(entries):
        start = index * V2_ENTRY_BYTES
        name = entry["path"].encode()
        manifest[start:start + len(name)] = name
        struct.pack_into("<Q", manifest, start + 80, entry["bytes"])
        manifest[start + 88:start + 120] = bytes.fromhex(entry["sha256"])
        manifest[start + 120] = entry["policy"]
        manifest[start + 128:start + 128 + len(entry["build"])] = entry["build"].encode()
    package_id = hashlib.sha256(V2_MAGIC + bytes((CHANNELS[channel], flags)) + manifest).hexdigest()[:12]
    header = bytearray(HEADER_BYTES)
    header[:8] = V2_MAGIC
    struct.pack_into("<IIIIQ", header, 8, 2, HEADER_BYTES, len(entries), len(manifest), total)
    header[32:44] = package_id.encode()
    header[44] = CHANNELS[channel]
    header[45] = flags
    header[64:96] = hashlib.sha256(manifest).digest()
    struct.pack_into("<I", header, 124, zlib.crc32(header[:124]))
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".kui-v2-", dir=output.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(header)
            stream.write(manifest)
            for entry in entries:
                if entry["path"] in data:
                    # Check again so a modified stage is refused before output
                    # is replaced, even if the initially read bytes were valid.
                    if (root / entry["path"]).read_bytes() != data[entry["path"]]:
                        raise ValueError("Staged source changed while packaging: " + entry["path"])
                    stream.write(data[entry["path"]])
            stream.flush()
            os.fsync(stream.fileno())
        report = verify(temporary)
        report["consumer"] = current_consumer_check(temporary)
        report["mode"] = "release" if release else "full" if full else "test"
        if inventory_v2(root) != paths:
            raise ValueError("Staged software inventory changed while packaging")
        os.replace(temporary, output)
        temporary = None
        return report
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("verify")
    check.add_argument("package", type=Path)
    make = commands.add_parser("build", help="Build a v2 test or full software package with current consumer validation")
    make.add_argument("kui_directory", type=Path)
    make.add_argument("output", type=Path)
    make.add_argument("--channel", choices=tuple(CHANNELS), default="testing")
    make.add_argument("--release", action="store_true")
    make.add_argument("--full", action="store_true", help="Complete software replacement; omit old optional software")
    make.add_argument("--remove", action="append", default=[])
    make.add_argument("--keep", action="append", default=[])
    make = commands.add_parser("build-legacy", help="Explicit v1 bootstrap/migration producer; not for ordinary tests")
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
            result = build_package_v2(args.kui_directory, args.output, args.channel,
                                      release=args.release, full=args.full, remove=args.remove, keep=args.keep)
        elif args.command == "build-legacy":
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
