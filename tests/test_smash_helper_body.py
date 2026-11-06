#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the normalized-body gate with actual admitted helper fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_smash_helper_body import PINS, REFERENCE_BUILD, validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vc2-prepared", required=True, type=Path)
    parser.add_argument("--swirl-prepared", required=True, type=Path)
    parser.add_argument("--other-vc2-raw", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    checks, refusals, proofs = 0, 0, []

    def accepted(raw, kind, build):
        nonlocal checks
        proof = validate(raw, kind, build)
        assert proof["status"] == "PASS" and proof["only_build_tag_changed"]
        checks += 1
        return proof

    def refused(raw, kind, build):
        nonlocal checks, refusals
        try:
            validate(raw, kind, build)
        except ValueError:
            checks += 1
            refusals += 1
            return
        raise AssertionError("A changed helper body/tag/size was admitted")

    for kind, path in ((1, args.vc2_prepared), (2, args.swirl_prepared)):
        pin, prepared = PINS[kind], path.read_bytes()
        assert len(prepared) == pin["prepared_bytes"]
        raw = prepared[:pin["raw_bytes"]]
        assert not any(prepared[len(raw):])
        proof = accepted(raw, kind, REFERENCE_BUILD)
        assert proof["prepared_sha256"] == hashlib.sha256(prepared).hexdigest()
        proofs.append(proof)
        at, build = pin["tag_offset"], "012345abcdef"
        current = raw[:at] + build.encode("ascii") + raw[at + 12:]
        current_proof = accepted(current, kind, build)
        assert current_proof["normalized_sha256"] == proof["normalized_sha256"]
        assert current_proof["raw_sha256"] != proof["raw_sha256"]
        refused(current, kind, REFERENCE_BUILD)
        refused(current, 3 - kind, build)
        refused(current[:-4], kind, build)
        refused(current + bytes(4), kind, build)
        refused(bytearray(current), kind, build)
        refused(current, 3, build)
        for bad in ("", build.upper(), build[:-1], build + "0", "../012345abc"):
            refused(current, kind, bad)
        for index in (0, at - 7, at - 1, at + 12, at + 13, len(raw) - 1):
            changed = bytearray(current)
            changed[index] ^= 1
            refused(bytes(changed), kind, build)
        duplicate = current[:256] + build.encode("ascii") + current[268:]
        refused(duplicate, kind, build)
        moved = bytearray(duplicate)
        moved[at:at + 12] = bytes(12)
        refused(bytes(moved), kind, build)
    if args.other_vc2_raw:
        raw = args.other_vc2_raw.read_bytes()
        at = PINS[1]["tag_offset"]
        proofs.append(accepted(raw, 1, raw[at:at + 12].decode("ascii")))
    report = {"schema": "kui.smash.helper-body-tests.v1", "status": "PASS", "checks": checks,
        "refusals": refusals, "actual_reference_proofs": proofs,
        "cases": ["exact admitted ab1 raw/prepared components", "one current build-tag substitution",
            "wrong kind/build/size/body rejected", "duplicate or moved tag rejected",
            "mutable input and malformed IDs rejected"], "hardware_acceptance": False}
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
