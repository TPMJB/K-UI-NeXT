#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent canonical KUIVMUP2 preparation codec; never authorizes SD writes."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

WIRE_BYTES = 4096
FILE_BYTES = (131072, 1024, 16)
FILE_SECTORS = (256, 2, 1)
FILE_LIMITS = (16, 2, 1)
MARKER_SHA = hashlib.sha256(bytes(512)).hexdigest()
GEOMETRY = {"fs_type": (288, "I"), "sector_bytes": (292, "I"),
    "sectors_per_cluster": (296, "I"), "volume_serial": (300, "I"),
    "fat_count": (304, "I"), "active_fat": (308, "I"),
    "fat_first_lba": (312, "Q"), "fat_sectors": (320, "Q"),
    "data_first_lba": (328, "Q"), "cluster_count": (336, "I"),
    "root_cluster": (340, "I"), "fat_mirrored": (344, "I"),
    "bitmap_first_cluster": (352, "I"), "upcase_first_cluster": (356, "I"),
    "bitmap_bytes": (360, "Q"), "upcase_bytes": (368, "Q")}
FILE_FIELDS = {"file_bytes": (0, "Q"), "first_cluster": (8, "I"),
    "allocation_flags": (12, "I"), "directory_first_cluster": (16, "I"),
    "entry_offset": (20, "I"), "entry_lba": (24, "Q"), "entry_count": (32, "I"),
    "entry_kind": (36, "I"), "valid_bytes": (48, "Q")}
SNAPSHOT_KEYS = {"base_sha256", "journal_sha256", "identity_sha256", "marker_sha256",
                 "block_crc", "journal_sequence", "wire_sha256"}


def _uint(value, bits=32):
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError("Requires an unsigned integer of the specified wire width")
    return value


def _hex(value, length):
    if not isinstance(value, str) or re.fullmatch("[0-9a-f]{" + str(length * 2) + "}", value) is None:
        raise ValueError("Requires canonical lowercase hexadecimal identity/digest")
    return bytes.fromhex(value)


def validate_ownership(p):
    for key in ("card_sectors", "partition_first", "partition_end"):
        _uint(p[key], 64)
    for key in ("transport", "high_capacity", "flags"):
        _uint(p[key])
    capacity, first, end = p["card_sectors"], p["partition_first"], p["partition_end"]
    cid, image_id = _hex(p["cid"], 16), _hex(p["image_id"], 16)
    if (p["transport"] != 1 or p["high_capacity"] not in (0, 1) or p["flags"] != 1 or
            not isinstance(p["pair_path"], str) or
            re.fullmatch(r"/KUI/vmu-test/p(?!0000)[0-9]{4}", p["pair_path"]) is None or
            not 1 <= capacity <= 1 << 32 or
            (not p["high_capacity"] and capacity > 1 << 23) or
            not 0 <= first < end <= capacity or cid in (bytes(16), bytes([255]) * 16) or
            image_id == bytes(16)):
        raise ValueError("Invalid persistent preparation identity, path or descriptor kind")
    g = p["geometry"]
    for name, (_, fmt) in GEOMETRY.items():
        _uint(g[name], 64 if fmt == "Q" else 32)
    spc, clusters = g["sectors_per_cluster"], g["cluster_count"]
    cluster_ok = lambda c: 2 <= c < clusters + 2
    if (g["fs_type"] not in (1, 2) or g["sector_bytes"] != 512 or
            not 1 <= spc <= 65536 or spc & (spc - 1) or
            not 1 <= clusters <= 0xfffffff5 or not cluster_ok(g["root_cluster"]) or
            g["fat_count"] not in (1, 2) or g["active_fat"] >= g["fat_count"] or
            g["fat_mirrored"] not in (0, 1) or (g["fat_mirrored"] and g["active_fat"]) or
            not g["fat_sectors"] or not first < g["fat_first_lba"] < end or
            g["fat_first_lba"] + g["fat_sectors"] * g["fat_count"] > g["data_first_lba"] or
            not g["data_first_lba"] < end or g["data_first_lba"] + clusters * spc > end or
            (clusters + 2) * 4 > g["fat_sectors"] * 512):
        raise ValueError("Invalid bounded filesystem geometry")
    if g["fs_type"] == 1:
        if (spc > 128 or not 65525 <= clusters <= 0x0ffffff5 or
                any(g[name] for name in ("bitmap_first_cluster", "upcase_first_cluster", "bitmap_bytes", "upcase_bytes"))):
            raise ValueError("Invalid FAT32 geometry")
    elif (g["fat_mirrored"] or not cluster_ok(g["bitmap_first_cluster"]) or
          not cluster_ok(g["upcase_first_cluster"]) or
          len({g["root_cluster"], g["bitmap_first_cluster"], g["upcase_first_cluster"]}) != 3 or
          not (clusters + 7) // 8 <= g["bitmap_bytes"] <= clusters * spc * 512 or
          not 0 < g["upcase_bytes"] <= 131072 or g["upcase_bytes"] & 1):
        raise ValueError("Invalid exFAT system-file geometry")
    files = p["files"]
    if not isinstance(files, list) or len(files) != 3:
        raise ValueError("Requires exactly base, journal and identity records")
    for role, f in enumerate(files):
        for name, (_, fmt) in FILE_FIELDS.items():
            _uint(f[name], 64 if fmt == "Q" else 32)
        if (f["file_bytes"] != FILE_BYTES[role] or f["valid_bytes"] != f["file_bytes"] or
                not cluster_ok(f["first_cluster"]) or not cluster_ok(f["directory_first_cluster"]) or
                not g["data_first_lba"] <= f["entry_lba"] < g["data_first_lba"] + clusters * spc or
                not 0 <= f["entry_offset"] <= 480 or f["entry_offset"] % 32 or
                f["entry_kind"] != g["fs_type"] or
                not 1 <= f["entry_count"] <= (21 if g["fs_type"] == 1 else 19) or
                (g["fs_type"] == 2 and f["entry_count"] < 3) or
                f["allocation_flags"] not in ((0,) if g["fs_type"] == 1 else (1, 3))):
            raise ValueError("Invalid file size, allocation flags or entry locator")
        for kind in ("useful", "allocated"):
            entries = f[kind]
            total = FILE_SECTORS[role] if kind == "useful" else ((FILE_SECTORS[role] + spc - 1) // spc) * spc
            if not isinstance(entries, list) or not 1 <= len(entries) <= FILE_LIMITS[role]:
                raise ValueError("Invalid bounded role extent count")
            logical = 0
            for index, e in enumerate(entries):
                for key in ("logical_block", "lba", "blocks"):
                    _uint(e[key])
                if (e["logical_block"] != logical or not 0 < e["blocks"] <= total - logical or
                        not g["data_first_lba"] <= e["lba"] < e["lba"] + e["blocks"] <= g["data_first_lba"] + clusters * spc or
                        (kind == "allocated" and ((e["lba"] - g["data_first_lba"]) % spc or e["blocks"] % spc)) or
                        any(e["lba"] < old["lba"] + old["blocks"] and old["lba"] < e["lba"] + e["blocks"] for old in entries[:index]) or
                        (index and entries[index - 1]["lba"] + entries[index - 1]["blocks"] == e["lba"])):
                    raise ValueError("Extent map is not canonical, bounded and cluster-aligned")
                logical += e["blocks"]
            if logical != total:
                raise ValueError("Extent map does not cover the exact file/allocation")
        if f["allocated"][0]["lba"] != g["data_first_lba"] + (f["first_cluster"] - 2) * spc:
            raise ValueError("First cluster differs from full allocated map")
        expected, left = [], FILE_SECTORS[role]
        for e in f["allocated"]:
            if not left:
                break
            blocks = min(e["blocks"], left)
            expected.append(dict(logical_block=e["logical_block"], lba=e["lba"], blocks=blocks))
            left -= blocks
        if f["useful"] != expected:
            raise ValueError("Useful map must be the exact prefix of full cluster ownership")
    contains = lambda f, lba: any(e["lba"] <= lba < e["lba"] + e["blocks"] for e in f["allocated"])
    for role, f in enumerate(files):
        for cluster in (g["root_cluster"], g["bitmap_first_cluster"], g["upcase_first_cluster"]):
            if cluster and contains(f, g["data_first_lba"] + (cluster - 2) * spc):
                raise ValueError("Target file overlaps filesystem metadata")
        for other, o in enumerate(files):
            if (contains(f, o["entry_lba"]) or
                    contains(f, g["data_first_lba"] + (o["directory_first_cluster"] - 2) * spc)):
                raise ValueError("Target allocation overlaps directory ownership")
            if other < role and any(a["lba"] < b["lba"] + b["blocks"] and b["lba"] < a["lba"] + a["blocks"]
                                    for a in f["allocated"] for b in o["allocated"]):
                raise ValueError("Base, journal and identity full allocations overlap")
            if other < role and (f["entry_lba"], f["entry_offset"]) == (o["entry_lba"], o["entry_offset"]):
                raise ValueError("Target roles cannot share a directory-entry locator")
    return p


def validate_profile(p):
    validate_ownership(p)
    if (p["journal_sequence"] != 0 or not _hex(p["base_sha256"], 32).strip(b"\0") or
            not _hex(p["journal_sha256"], 32).strip(b"\0") or p["marker_sha256"] != MARKER_SHA or
            p["identity_sha256"] != hashlib.sha256(_hex(p["image_id"], 16)).hexdigest() or
            not isinstance(p["block_crc"], list) or len(p["block_crc"]) != 256):
        raise ValueError("Invalid preparation snapshot/clean-marker provenance")
    _uint(p["journal_sequence"], 64)
    for crc in p["block_crc"]:
        _uint(crc)
    return p


def encode_profile(p):
    validate_profile(p)
    wire = bytearray(WIRE_BYTES)
    wire[:8] = b"KUIVMUP2"
    def put(at, value, fmt="I"):
        struct.pack_into("<" + fmt, wire, at, value)
    for at, value in ((8, 2), (12, WIRE_BYTES), (20, p["transport"]),
                      (24, p["high_capacity"]), (28, p["flags"]), (184, 19)):
        put(at, value)
    for at, name in ((32, "card_sectors"), (40, "partition_first"), (48, "partition_end"), (2880, "journal_sequence")):
        put(at, p[name], "Q")
    for at, name, count in ((56, "cid", 16), (72, "image_id", 16), (88, "base_sha256", 32),
                             (120, "journal_sha256", 32), (152, "identity_sha256", 32), (384, "marker_sha256", 32)):
        wire[at:at + count] = _hex(p[name], count)
    path = p["pair_path"].encode("ascii");wire[192:192 + len(path)] = path
    for name, (at, fmt) in GEOMETRY.items():
        put(at, p["geometry"][name], fmt)
    for role, f in enumerate(p["files"]):
        base = 512 + role * 448
        for name, (at, fmt) in FILE_FIELDS.items():
            put(base + at, f[name], fmt)
        for kind, at, counter in (("useful", 64, 40), ("allocated", 256, 44)):
            put(base + counter, len(f[kind]))
            for index, e in enumerate(f[kind]):
                struct.pack_into("<III", wire, base + at + index * 12, e["logical_block"], e["lba"], e["blocks"])
    for index, crc in enumerate(p["block_crc"]):
        put(1856 + index * 4, crc)
    put(16, zlib.crc32(wire))
    return bytes(wire)


def decode_profile(data):
    if len(data) != WIRE_BYTES or data[:8] != b"KUIVMUP2":
        raise ValueError("Requires one4096-byte KUIVMUP2 preparation profile")
    get = lambda at, fmt="I": struct.unpack_from("<" + fmt, data, at)[0]
    unsigned = bytearray(data);unsigned[16:20] = bytes(4)
    if get(8) != 2 or get(12) != WIRE_BYTES or get(16) != zlib.crc32(unsigned):
        raise ValueError("Preparation version/length/checksum mismatch")
    for at, size in ((188, 4), (348, 4), (376, 8), (416, 96), (2888, 1208)):
        if any(data[at:at + size]):
            raise ValueError("Nonzero reserved preparation bytes")
    if get(184) != 19 or any(data[211:288]):
        raise ValueError("Noncanonical preparation path length/padding")
    try:
        path = data[192:211].decode("ascii")
    except UnicodeError as exc:
        raise ValueError("Preparation path is not ASCII") from exc
    p = {"transport": get(20), "high_capacity": get(24), "flags": get(28),
         "card_sectors": get(32, "Q"), "partition_first": get(40, "Q"),
         "partition_end": get(48, "Q"), "cid": data[56:72].hex(), "image_id": data[72:88].hex(),
         "pair_path": path, "base_sha256": data[88:120].hex(),
         "journal_sha256": data[120:152].hex(), "identity_sha256": data[152:184].hex(),
         "marker_sha256": data[384:416].hex(), "journal_sequence": get(2880, "Q"),
         "geometry": {name: get(at, fmt) for name, (at, fmt) in GEOMETRY.items()}, "files": [],
         "block_crc": [get(1856 + index * 4) for index in range(256)]}
    for role in range(3):
        base = 512 + role * 448
        if any(data[base + 56:base + 64]):
            raise ValueError("Nonzero file-record reserved bytes")
        f = {name: get(base + at, fmt) for name, (at, fmt) in FILE_FIELDS.items()}
        for kind, at, counter in (("useful", 64, 40), ("allocated", 256, 44)):
            count = get(base + counter)
            if not 1 <= count <= FILE_LIMITS[role] or any(data[base + at + count * 12:base + at + 192]):
                raise ValueError("Invalid role count/nonzero unused map slot")
            f[kind] = [dict(zip(("logical_block", "lba", "blocks"),
                struct.unpack_from("<III", data, base + at + index * 12))) for index in range(count)]
        p["files"].append(f)
    validate_profile(p)
    p["wire_sha256"] = hashlib.sha256(data).hexdigest()
    return p


def same_ownership(a, b):
    validate_ownership(a);validate_ownership(b)
    return ({key: value for key, value in a.items() if key not in SNAPSHOT_KEYS} ==
            {key: value for key, value in b.items() if key not in SNAPSHOT_KEYS})


def collect_snapshot(p, read):
    validate_ownership(p)
    result = copy.deepcopy(p)
    result.pop("wire_sha256", None)
    result["block_crc"] = []
    for role in range(3):
        digest = hashlib.sha256()
        for logical in range(FILE_SECTORS[role]):
            block = read(role, logical)
            if not isinstance(block, bytes) or len(block) != 512:
                raise ValueError("Sector callback must return exactly512 bytes")
            if role == 1 and logical == 1 and any(block):
                raise ValueError("Initial physical journal binding requires an exactly zero marker")
            if role == 2 and block[:16] != _hex(p["image_id"], 16):
                raise ValueError("Identity file differs from the owned pair")
            digest.update(block[:16] if role == 2 else block)
            if role == 0:
                result["block_crc"].append(zlib.crc32(block))
        result[("base_sha256", "journal_sha256", "identity_sha256")[role]] = digest.hexdigest()
    result.update(marker_sha256=MARKER_SHA, journal_sequence=0)
    return validate_profile(result)


def verify_snapshot(p, read):
    validate_profile(p)
    observed = collect_snapshot(p, read)
    if any(observed[key] != p[key] for key in SNAPSHOT_KEYS - {"wire_sha256"}):
        raise ValueError("Preparation snapshot differs from current file data")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    args = parser.parse_args()
    try:
        p = decode_profile(args.profile.read_bytes())
        print(json.dumps({"format": "KUIVMUP2", "prep_only": True,
            "retail_writers_enabled": False, "wire_sha256": p["wire_sha256"],
            "pair_path": p["pair_path"], "card_sectors": p["card_sectors"],
            "filesystem": "exFAT" if p["geometry"]["fs_type"] == 2 else "FAT32",
            "base_sha256": p["base_sha256"], "journal_sha256": p["journal_sha256"],
            "files": p["files"]}, indent=2))
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(2, f"Persistent preparation profile refused: {error}\n")


if __name__ == "__main__":
    main()
