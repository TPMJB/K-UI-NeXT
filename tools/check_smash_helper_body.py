#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify a freshly built CE helper permits only its one build-tag change.

validate(raw, kind, build) accepts immutable raw helper bytes. The exact ab1
body, size and one BUILD tag location are pinned; no bytes are rewritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


REFERENCE_BUILD = "ab1b145b33b4"
PINS = {
    1: {"raw_bytes": 41152, "prepared_bytes": 49152, "tag_offset": 4082,
        "reference_sha256": "85e864ae8d386b4b2d5cec731f788441367fdf9f2bef86f57ccd12a430e214e8",
        "normalized_sha256": "8b39eae1f78c2bc9f5c2b0bab71c5b458e42b6ce69aa2611bcff8b4e4560d9ee"},
    2: {"raw_bytes": 41152, "prepared_bytes": 49152, "tag_offset": 3834,
        "reference_sha256": "47b5687e400b25b43ffa378b2b04171e59c039c57e018e83ac4de6bf7a8719e8",
        "normalized_sha256": "32674570edf98e2521e2e117f1429b83a217203f1da980bfc874410d22fd7bd6"},
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate(raw, kind, build):
    """Return proof for VC2(1)/Swirl(2), or refuse any size/body/tag change."""
    if not isinstance(raw, bytes) or kind not in PINS or not isinstance(build, str):
        raise ValueError("Use captured helper bytes, VC2(1)/Swirl(2), and the build ID")
    if not re.fullmatch(r"[0-9a-f]{12}", build):
        raise ValueError("Helper build ID must be twelve lowercase hexadecimal characters")
    pin = PINS[kind]
    if len(raw) != pin["raw_bytes"]:
        raise ValueError("Helper size differs from the admitted component")
    at = pin["tag_offset"]
    tag = build.encode("ascii")
    if (raw[at:at + 12] != tag or raw.count(tag) != 1 or
            raw[at - 6:at] != b"BUILD " or raw[at + 12:at + 13] != b"\0"):
        raise ValueError("Helper must contain exactly its current build at the pinned BUILD tag")
    normalized = raw[:at] + bytes(12) + raw[at + 12:]
    if sha(normalized) != pin["normalized_sha256"]:
        raise ValueError("Helper body differs beyond its one build tag")
    reference = raw[:at] + REFERENCE_BUILD.encode("ascii") + raw[at + 12:]
    if sha(reference) != pin["reference_sha256"]:
        raise ValueError("Helper does not reproduce the admitted reference body")
    prepared = raw + bytes(pin["prepared_bytes"] - len(raw))
    return {"schema": "kui.smash.helper-body.v1", "status": "PASS", "kind": kind,
        "build": build, "reference_build": REFERENCE_BUILD, "raw_bytes": len(raw),
        "prepared_bytes": len(prepared), "build_tag_offset": at,
        "raw_sha256": sha(raw), "prepared_sha256": sha(prepared),
        "reference_sha256": pin["reference_sha256"],
        "normalized_sha256": pin["normalized_sha256"], "only_build_tag_changed": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path)
    parser.add_argument("--kind", required=True, choices=(1, 2), type=int)
    parser.add_argument("--build", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        text = json.dumps(validate(args.raw.read_bytes(), args.kind, args.build), indent=2) + "\n"
        if args.output:
            args.output.write_text(text)
        print(text, end="")
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
