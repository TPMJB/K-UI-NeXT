#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refresh the GUI provider-input identity only when its selection changes."""
import argparse
import hashlib
import json
import os
from pathlib import Path


def refresh(bundle, output):
    record = {"version": 1, "enabled": bool(bundle), "directory": "", "files": {}}
    if bundle:
        directory = Path(bundle).resolve()
        record["directory"] = str(directory)
        for name in ("bundle.S", "bundle.bin"):
            body = (directory / name).read_bytes()
            if not body:
                raise ValueError("Runtime module bundle input is empty: " + name)
            record["files"][name] = {"bytes": len(body), "sha256": hashlib.sha256(body).hexdigest()}
    body = (json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n").encode()
    try:
        if output.read_bytes() == body:
            return False
    except FileNotFoundError:
        pass
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".new")
    temporary.write_bytes(body)
    os.replace(temporary, output)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", default="")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        refresh(args.bundle, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
