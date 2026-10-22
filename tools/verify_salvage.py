#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently reread a resolved K-UI salvage job and its immutable evidence.

This checks complete journal, full raw files, original zero baseline and every
published patch. It does not establish a TOSEC/Redump match or certify that two
matching audio rereads were the original disc's correct bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

RAW = 2352


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_file(path, limit, exact=None):
    require(path.is_file() and not path.is_symlink(), f"Missing/unsafe {path.name}")
    size = path.stat().st_size
    require(size <= limit and (exact is None or size == exact), f"Invalid {path.name} length")
    return path.read_bytes()


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def checked(data, magic):
    require(data[:8] == magic and u32(data, len(data) - 4) == zlib.crc32(data[:-4]),
            f"Invalid {magic.decode()} record or checksum")


def verify(directory):
    root = Path(directory)
    header = read_file(root / "header.bin", 4096, 4096)
    checked(header, b"KUISALV1")
    count = u32(header, 16)
    require(u32(header, 8) == 1 and u32(header, 12) == 4096 and 1 <= count <= 99
            and u32(header, 20) in (0, 1), "Invalid salvage header fields")
    require(not any(header[96 + count * 24:-4]), "Unknown salvage header fields")
    identity = hashlib.sha256(header).digest()
    plan = [struct.unpack_from("<6I", header, 96 + i * 24) for i in range(count)]
    for i, (number, control, session, start, end, toc_end) in enumerate(plan):
        require(number == i + 1 and control in (0, 4) and session in (0, 1)
                and 150 <= start < end <= toc_end <= 720000,
                f"Invalid track {i + 1} bounds")
        require(not i or start >= plan[i - 1][4], "Overlapping tracks")
    anchors = [u32(header, 24), u32(header, 28)]
    require(anchors[0] != anchors[1], "Duplicate identity anchors")
    records = read_file(root / "journal.bin", 64 * 1024 * 1024)
    require(records and len(records) % 512 == 0, "Incomplete salvage journal tail")
    sectors, crc, baseline = [0] * count, [0] * count, [None] * count
    targets, pending = [], []
    ready = complete = False
    for sequence, at in enumerate(range(0, len(records), 512), 1):
        event = records[at:at + 512]
        checked(event, b"KUISJNL1")
        require(struct.unpack_from("<Q", event, 8)[0] == sequence
                and event[40:72] == identity and not any(event[36:40])
                and not any(event[72:508]) and not complete,
                "Invalid journal sequence, identity or reserved fields")
        kind, track, sector, length, value = struct.unpack_from("<5I", event, 16)
        active = next((i for i, item in enumerate(baseline) if item is None), count)
        if kind == 1:
            require(not ready and u32(header, 20) == 1 and track == active < count
                    and length == value == 0 and len(pending) < 32
                    and len(targets) + len(pending) < 8192
                    and sectors[track] <= sector < min(sectors[track] + 32, plan[track][4] - plan[track][3])
                    and (not pending or sector > pending[-1]), "Invalid unresolved-target record")
            pending.append(sector)
        elif kind == 2:
            require(not ready and track == active < count and sector == sectors[track]
                    and 1 <= length <= 32 and sector + length <= plan[track][4] - plan[track][3]
                    and all(item < sector + length for item in pending), "Invalid first-pass commit")
            targets.extend({"track": track, "sector": item, "crc": None} for item in pending)
            pending = []
            sectors[track] += length
            crc[track] = value
        elif kind == 3:
            require(not ready and not pending and track == active < count and sector == length == 0
                    and sectors[track] == plan[track][4] - plan[track][3] and value == crc[track],
                    "Invalid immutable baseline")
            baseline[track] = value
        elif kind == 4:
            require(not ready and not pending and active == count and track == sector == value == 0
                    and length == len(targets), "Invalid first-pass completion")
            ready = True
        elif kind == 5:
            require(ready and track < len(targets) and sector == length == 0
                    and targets[track]["crc"] is None, "Invalid repaired-target commit")
            targets[track]["crc"] = value
        elif kind == 6:
            require(ready and track == sector == value == 0 and length == len(targets)
                    and all(item["crc"] is not None for item in targets),
                    "Salvage completion still contains unresolved sectors")
            complete = True
        else:
            raise ValueError("Unknown salvage journal event")
    require(complete and ready and not pending, "Salvage is incomplete; no verified result")
    replacements = [[] for _ in plan]
    for index, target in enumerate(targets):
        patch = read_file(root / f"patch-{index:04}.bin", 8192, 8192)
        checked(patch, b"KUISPAT1")
        track, sector = target["track"], target["sector"]
        before, after = patch[64:64 + RAW], patch[64 + RAW:64 + 2 * RAW]
        require(u32(patch, 8) == index and u32(patch, 12) == track and u32(patch, 16) == sector
                and u32(patch, 20) == (1 if plan[track][1] == 4 else 2)
                and patch[32:64] == identity and not any(before)
                and not any(patch[64 + 2 * RAW:-4])
                and u32(patch, 24) == zlib.crc32(before)
                and u32(patch, 28) == zlib.crc32(after) == target["crc"],
                f"Invalid immutable patch {index}")
        replacements[track].append((sector * RAW, after))
    manifest = json.loads(read_file(root / "salvage.json", 32768))
    require(isinstance(manifest, dict), "Invalid salvage manifest")
    require(manifest.get("salvage_schema") == 1 and manifest.get("identity") == identity.hex()
            and manifest.get("requires_complete_journal") is True
            and manifest.get("full_saved_readback") is False
            and manifest.get("original_targets") == len(targets) and manifest.get("unresolved") == 0
            and isinstance(manifest.get("tracks"), list) and len(manifest["tracks"]) == count,
            "Final manifest disagrees with salvage journal")
    expected_gdi, results = [str(count)], []
    for i, (_, control, _, start, end, _) in enumerate(plan):
        name = f"track{i + 1:02}.{'bin' if control == 4 else 'raw'}"
        path, wanted = root / name, (end - start) * RAW
        require(path.is_file() and not path.is_symlink() and path.stat().st_size == wanted,
                f"Missing/unsafe/truncated {name}")
        expected_gdi.append(f"{i + 1} {start - 150} {control} 2352 {name} 0")
        full_crc = original_crc = position = 0
        sha = hashlib.sha256()
        intervals = sorted(replacements[i])
        target_index = 0
        with path.open("rb") as source:
            while chunk := source.read(1024 * 1024):
                full_crc = zlib.crc32(chunk, full_crc)
                sha.update(chunk)
                original = bytearray(chunk)
                while target_index < len(intervals) and intervals[target_index][0] + RAW <= position:
                    target_index += 1
                at = target_index
                while at < len(intervals) and intervals[at][0] < position + len(chunk):
                    offset, after = intervals[at]
                    lo, hi = max(offset, position), min(offset + RAW, position + len(chunk))
                    require(chunk[lo - position:hi - position] == after[lo - offset:hi - offset],
                            f"{name}: repaired bytes differ from immutable patch at sector {offset // RAW}")
                    original[lo - position:hi - position] = bytes(hi - lo)
                    at += 1
                original_crc = zlib.crc32(original, original_crc)
                position += len(chunk)
            for anchor_index, fad in enumerate(anchors):
                if control == 4 and start <= fad < end:
                    source.seek((fad - start) * RAW)
                    require(hashlib.sha256(source.read(RAW)).digest() == header[32 + anchor_index * 32:64 + anchor_index * 32],
                            "Saved identity anchor does not match original disc binding")
        require(original_crc == baseline[i], f"{name}: bytes outside repaired targets differ from immutable baseline")
        expected = manifest["tracks"][i]
        require(expected == {"number": i + 1, "bytes": wanted, "crc32": f"{full_crc:08x}"},
                f"{name}: complete saved-file CRC mismatch")
        results.append({"file": name, "bytes": wanted, "crc32": f"{full_crc:08x}", "sha256": sha.hexdigest()})
    for fad in anchors:
        require(any(control == 4 and session == 1 and start <= fad < end for _, control, session, start, end, _ in plan),
                "Identity anchor outside all data tracks")
    descriptor = read_file(root / "salvage.gdi", 32768)
    require(descriptor == ("\n".join(expected_gdi) + "\n").encode("ascii"), "Final GDI differs from original track plan")
    return {"complete": True, "saved_files_verified": True, "catalogue_match": False,
            "original_targets": len(targets), "unresolved": 0, "tracks": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.directory)
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    print("PASS: saved files, original baseline and every recovered target match.")
    print("No independent TOSEC/Redump comparison was performed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
