#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify a completed K-UI GDI capture using Python's independent hash routines.

Usage: python3 verify_dump.py DUMP_DIRECTORY [--reference REFERENCE_MANIFEST]
Reference manifests must declare the same profile and track filenames/sizes.
Each reference track needs CRC32 and/or SHA-256. Partial coverage is reported
separately and returns exit 2; a mismatch or invalid capture returns exit 1.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import zlib

PROFILE = "gdi-raw2352-typegap150-v1"


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load_json(path):
    if path.is_symlink():
        raise ValueError(f"Unsafe metadata (a symbolic link): {path}")
    if not path.is_file():
        # A relative path is taken from the directory the tool is run in. Run from tools/ with
        # docs/evidence/x.json, this used to say only "Missing/unsafe metadata: x.json".
        raise ValueError(f"Missing metadata: {path.name}. Looked for {path.resolve()}; a relative "
                         "path is taken from the directory you ran this in")
    require(path.stat().st_size <= 128 * 1024, "Metadata is too large")
    return json.loads(path.read_text())


def verify(directory):
    directory = Path(directory)
    m = load_json(directory / "manifest.json")
    require(isinstance(m, dict) and m.get("schema") in (1, 2), "Unsupported manifest schema")
    schema = m["schema"]
    if schema == 1:
        require(m.get("complete") is True and m.get("saved_data_verified") is True,
                "Capture is incomplete or has not passed console readback")
    else:
        # Schema 2 (CRC-only captures) makes no verification claim of its own: how
        # the console verified is in its report, and this tool recomputes from the files.
        require(m.get("complete") is True and m.get("hashes") == ["crc32"],
                "Capture is incomplete or records unexpected hashes")
        require("saved_data_verified" not in m and "reference" not in m,
                "Schema 2 must not embed a verification claim")
    require(m.get("profile") == PROFILE and m.get("sector_bytes") == 2352, "Unsupported GDI profile")
    require(isinstance(m.get("identity"), str) and re.fullmatch(r"[0-9a-f]{64}", m["identity"]), "Invalid disc identity")
    tracks = m.get("tracks")
    require(isinstance(tracks, list) and 2 <= len(tracks) <= 99, "Invalid track list")
    require(all(isinstance(t, dict) for t in tracks), "Invalid track record")
    lines = [str(len(tracks))]
    results = []
    for i, t in enumerate(tracks, 1):
        require(isinstance(t, dict), "Invalid track record")
        for key in ("number", "session", "control", "start_fad", "end_fad", "toc_end_fad", "excluded_tail_sectors", "bytes"):
            require(type(t.get(key)) is int, f"Track {i}: invalid {key}")
        require(t["number"] == i and t["control"] in (0, 4) and t["session"] in (0, 1), "Invalid track numbering/type/session")
        require(150 <= t["start_fad"] < t["end_fad"] <= t["toc_end_fad"] <= 0xFFFFFF, "Invalid track bounds")
        following = tracks[i] if i < len(tracks) else None
        gap = 150 if following and following.get("session") == t["session"] and following.get("control") != t["control"] else 0
        require(t["toc_end_fad"] - t["end_fad"] == gap == t["excluded_tail_sectors"], "Incorrect declared gap")
        if following and following.get("session") == t["session"]:
            require(t["toc_end_fad"] == following.get("start_fad"), "Noncontiguous TOC within session")
        elif following:
            require(t["session"] == 0 and following.get("session") == 1 and
                    t["toc_end_fad"] <= following.get("start_fad", 0), "Invalid session order")
        expected_name = f"track{i:02}.{'bin' if t['control'] == 4 else 'raw'}"
        require(t.get("file") == expected_name, "Unsafe or inconsistent track filename")
        require(t["bytes"] == (t["end_fad"] - t["start_fad"]) * 2352 <= 0xFFFFFFFF, "Invalid track byte count")
        require(isinstance(t.get("crc32"), str) and re.fullmatch(r"[0-9a-f]{8}", t["crc32"]), "Invalid CRC32")
        if schema == 1:
            require(isinstance(t.get("sha256"), str) and re.fullmatch(r"[0-9a-f]{64}", t["sha256"]), "Invalid SHA-256")
        else:
            require("sha256" not in t, "Schema 2 tracks record CRC32 only")
        file = directory / expected_name
        require(file.is_file() and not file.is_symlink(), f"Missing/unsafe {expected_name}")
        require(file.stat().st_size == t["bytes"], f"{expected_name}: size mismatch")
        digest = hashlib.sha256()
        crc = 0
        with file.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
                crc = zlib.crc32(block, crc)
        crc_text, sha_text = f"{crc:08x}", digest.hexdigest()
        require(crc_text == t["crc32"] and (schema == 2 or sha_text == t["sha256"]),
                f"{expected_name}: saved-data hash mismatch")
        # SHA-256 is always computed here (it is free on a PC); it is compared only
        # when the manifest recorded one.
        results.append({"file": expected_name, "bytes": t["bytes"], "crc32": crc_text, "sha256": sha_text,
                        "sha256_recorded": schema == 1})
        lines.append(f"{i} {t['start_fad'] - 150} {t['control']} 2352 {expected_name} 0")
    require(tracks[0]["session"] == 0 and tracks[0]["start_fad"] == 150, "Invalid low-density start")
    high = next((t for t in tracks if t["session"] == 1), None)
    require(high is not None and high["start_fad"] == 45150 and high["control"] == 4, "Invalid high-density start")
    # Older jobs have no explicit descriptor field. Named jobs retain the same
    # track paths and GDI contents; only the descriptor's safe basename changes.
    gdi_name = m.get("gdi_file", "disc.gdi")
    require(isinstance(gdi_name, str) and 4 < len(gdi_name.encode("utf-8")) <= 100
            and gdi_name.lower().endswith(".gdi")
            and not any(ord(c) < 32 or ord(c) == 127 or c in '/\\:*?"<>|' for c in gdi_name)
            and gdi_name not in (".", "..") and not gdi_name.startswith(".")
            and not gdi_name.endswith((" ", ".")), "Unsafe GDI filename")
    descriptor = directory / gdi_name
    require(descriptor.is_file() and not descriptor.is_symlink() and descriptor.stat().st_size <= 8192, "Missing/unsafe GDI")
    require(descriptor.read_text().splitlines() == lines, "GDI descriptor differs from manifest")
    return results


def compare_reference(results, reference):
    require(isinstance(reference, dict) and reference.get("profile") == PROFILE, "Reference uses a different/unspecified layout profile")
    entries = reference.get("tracks")
    require(isinstance(entries, list), "Invalid reference track list")
    seen, matched = set(), 0
    actual = {t["file"]: t for t in results}
    for r in entries:
        require(isinstance(r, dict) and isinstance(r.get("file"), str), "Invalid reference track")
        name = r["file"]
        require(name not in seen and name in actual, "Reference contains duplicate/unknown tracks")
        seen.add(name)
        t = actual[name]
        require(type(r.get("bytes")) is int and r["bytes"] == t["bytes"], f"Reference size mismatch: {name}")
        hashes = [key for key in ("crc32", "sha256") if key in r]
        require(hashes, f"Reference has no hash for {name}")
        for key in hashes:
            require(isinstance(r[key], str) and r[key].lower() == t[key], f"Reference {key} mismatch: {name}")
        matched += 1
    return matched == len(results), matched


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", type=Path)
    args = parser.parse_args()
    try:
        results = verify(args.directory)
        for t in results:
            note = "" if t["sha256_recorded"] else " (computed here; not in the manifest)"
            print(f"PASS {t['file']} {t['bytes']} bytes CRC32={t['crc32']} SHA256={t['sha256']}{note}")
        print("SAVED DATA VERIFIED: sizes, CRC32, SHA-256 and GDI layout agree" if results[0]["sha256_recorded"]
              else "SAVED DATA VERIFIED: sizes, CRC32 and GDI layout agree")
        if args.reference:
            full, matched = compare_reference(results, load_json(args.reference))
            print(f"REFERENCE {'MATCH' if full else 'PARTIAL MATCH ONLY'}: {matched}/{len(results)} tracks from {args.reference}")
            return 0 if full else 2
        print("NO REFERENCE COMPARED; storage consistency does not establish disc accuracy")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
