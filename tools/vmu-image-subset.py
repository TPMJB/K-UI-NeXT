#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a fresh offline VMU image through the production C constructor."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

IMAGE_BYTES = 131072
DEFAULT_HOST = Path(__file__).resolve().parents[1] / "build/vmu-subset-tool/vmu-image-subset"


def run_host(host, *arguments):
    result = subprocess.run([str(host), *map(str, arguments)], capture_output=True, check=False)
    if result.returncode:
        reason = result.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(reason or f"Host constructor failed ({result.returncode}).")
    return result.stdout


def inventory(host, snapshot):
    try:
        result = json.loads(run_host(host, "--inspect", snapshot))
        if not isinstance(result, dict) or result["protocol"] != "kui-vmu-subset-v1":
            raise ValueError("Unrecognized host inventory protocol.")
        summary = result["summary"]
        if (not isinstance(summary, dict) or not isinstance(result["entries"], list)
                or any(type(summary[key]) is not int or not 0 <= summary[key] <= 200
                       for key in ("files", "game_files", "used_blocks", "free_blocks"))
                or summary["used_blocks"] + summary["free_blocks"] != 200
                or len(result["entries"]) != summary["files"]):
            raise ValueError("Invalid host filesystem summary.")
        seen = set()
        for item in result["entries"]:
            raw_name = bytes.fromhex(item["name_hex"])
            if (len(raw_name) != 12 or type(item["slot"]) is not int or not 0 <= item["slot"] < 208
                    or item["slot"] in seen or type(item["file_type"]) is not int
                    or item["file_type"] not in (0x33, 0xcc) or type(item["blocks"]) is not int
                    or not 1 <= item["blocks"] <= 200 or not isinstance(item["chain"], list)
                    or len(item["chain"]) != item["blocks"]
                    or any(type(block) is not int or not 0 <= block < 200 for block in item["chain"])):
                raise ValueError("Invalid host directory inventory.")
            seen.add(item["slot"])
            item["name"] = raw_name.rstrip(b"\0 ").decode("latin-1")
        return result
    except (KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError("Invalid host inventory response.") from error


def read_source(path):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0))
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_size != IMAGE_BYTES:
            raise ValueError("Source must be a regular file of exactly 131072 bytes.")
        stream = os.fdopen(descriptor, "rb")
        descriptor = None
        with stream:
            data = stream.read(IMAGE_BYTES + 1)
    finally:
        if descriptor is not None:
            os.close(descriptor)
    if len(data) != IMAGE_BYTES:
        raise ValueError("Source must contain exactly 131072 readable bytes.")
    return data


def choose_entries(args, entries):
    if args.slot is not None:
        slots = args.slot
        if any(slot < 0 or slot >= 208 for slot in slots):
            raise ValueError("Directory slots must be in 0..207.")
    else:
        names = []
        if args.name is not None:
            for name in args.name:
                try:
                    raw = name.encode("ascii")
                except UnicodeEncodeError as error:
                    raise ValueError("--name requires ASCII; use --name-hex for raw names.") from error
                if not 1 <= len(raw) <= 12 or any(byte < 32 or byte == 127 for byte in raw):
                    raise ValueError("--name requires 1..12 printable ASCII bytes.")
                names.append(raw.ljust(12, b"\0"))
        else:
            for text in args.name_hex:
                if len(text) != 24 or any(char not in "0123456789abcdefABCDEF" for char in text):
                    raise ValueError("--name-hex requires exactly 24 hexadecimal digits.")
                names.append(bytes.fromhex(text))
        slots = []
        for name in names:
            matches = [item["slot"] for item in entries if bytes.fromhex(item["name_hex"]) == name]
            if len(matches) != 1:
                raise ValueError(f"Exact 12-byte name {name.hex()} was not found uniquely.")
            slots.append(matches[0])
    if len(slots) != len(set(slots)):
        raise ValueError("A directory slot was selected more than once.")
    by_slot = {item["slot"]: item for item in entries}
    if any(slot not in by_slot for slot in slots):
        raise ValueError("Every selected slot must contain an admitted directory entry.")
    return [by_slot[slot] for slot in slots]


def retained_proof(source, derived, selected):
    if len(derived) != IMAGE_BYTES or derived[255 * 512:] != source[255 * 512:]:
        raise ValueError("Host output length/root verification failed.")
    proof = []
    for item in selected:
        slot = item["slot"]
        offset = (253 - slot // 16) * 512 + slot % 16 * 32
        if derived[offset:offset + 32] != source[offset:offset + 32]:
            raise ValueError("Retained directory metadata verification failed.")
        original = b"".join(source[b * 512:(b + 1) * 512] for b in item["chain"])
        copied = b"".join(derived[b * 512:(b + 1) * 512] for b in item["chain"])
        if copied != original:
            raise ValueError("Retained allocated payload verification failed.")
        proof.append({**item, "allocated_payload_sha256": hashlib.sha256(copied).hexdigest()})
    return proof


def write_new(path, data):
    """Never overwrite; remove only our own incomplete newly created inode."""
    descriptor = os.open(path, os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
    identity = os.fstat(descriptor)
    try:
        with os.fdopen(descriptor, "w+b") as stream:
            if stream.write(data) != len(data):
                raise OSError("Incomplete output write.")
            stream.flush()
            os.fsync(stream.fileno())
            stream.seek(0)
            if stream.read() != data:
                raise OSError("Output readback verification failed.")
    except BaseException:
        try:
            current = os.stat(path, follow_symlinks=False)
            if (current.st_dev, current.st_ino) == (identity.st_dev, identity.st_ino):
                os.unlink(path)
        except FileNotFoundError:
            pass
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="existing exact 131072-byte card image")
    parser.add_argument("output", type=Path, nargs="?", help="new offline image; must not exist")
    parser.add_argument("--list", action="store_true", help="validate source and print directory inventory only")
    parser.add_argument("--host-tool", type=Path, default=DEFAULT_HOST, help="prebuilt C host adapter path")
    selectors = parser.add_mutually_exclusive_group()
    selectors.add_argument("--slot", type=int, action="append", help="exact directory slot 0..207; repeat for a fileset")
    selectors.add_argument("--name", action="append", help="exact ASCII name, padded to 12 bytes with NUL; repeat")
    selectors.add_argument("--name-hex", action="append", help="exact 12-byte raw name as 24 hex digits; repeat")
    args = parser.parse_args(argv)
    has_selection = any(value is not None for value in (args.slot, args.name, args.name_hex))
    if args.list:
        if args.output is not None or has_selection:
            parser.error("--list takes only a source and no output or selectors.")
    elif args.output is None or not has_selection:
        parser.error("creation requires a fresh output path and explicit selectors.")
    try:
        if not args.host_tool.is_file():
            raise ValueError("Host adapter is missing. Build once with: make -f Makefile.vmu-subset-tool")
        args.host_tool = args.host_tool.resolve()
        if args.output is not None:
            if args.source.resolve() == args.output.resolve():
                raise ValueError("Source and output must be distinct paths.")
            if args.output.exists() or args.output.is_symlink():
                raise ValueError("Output already exists; choose a fresh path.")
        source = read_source(args.source)
        source_hash = hashlib.sha256(source).hexdigest()
        with tempfile.TemporaryDirectory(prefix="kui-vmu-subset-") as temporary:
            snapshot = Path(temporary) / "source.snapshot"
            snapshot.write_bytes(source)
            admitted = inventory(args.host_tool, snapshot)
            if args.list:
                report = {"source": str(args.source.resolve()), "source_sha256": source_hash, **admitted}
            else:
                selected = choose_entries(args, admitted["entries"])
                derived = run_host(args.host_tool, "--construct", snapshot,
                                   *(item["slot"] for item in selected))
                selected_proof = retained_proof(source, derived, selected)
                blocks = sum(item["blocks"] for item in selected)
                report = {
                    "kind": "offline-vmu-image-subset", "loader_pair_created": False,
                    "source": str(args.source.resolve()), "source_sha256": source_hash,
                    "source_summary": admitted["summary"],
                    "output": str(args.output.absolute()), "output_sha256": hashlib.sha256(derived).hexdigest(),
                    "output_bytes": len(derived),
                    "output_summary": {"files": len(selected), "game_files": sum(item["file_type"] == 0xcc for item in selected),
                                       "used_blocks": blocks, "free_blocks": 200 - blocks},
                    "selection": selected_proof, "root_identical": True,
                    "unused_data_policy": "zero", "output_readback_verified": True,
                }
                write_new(args.output, derived)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"vmu-image-subset: {error}\n")


if __name__ == "__main__":
    sys.exit(main())
