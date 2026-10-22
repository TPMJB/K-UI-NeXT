#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract the production native-wrapper definition for the host harness."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PATTERN = r"(?m)^enum\s+kui_armada_ce_ram_bind_status\s+kui_armada_ce_cache_line_native\s*\("


def extract(source):
    starts = list(re.finditer(PATTERN, source))
    if len(starts) != 1:
        raise ValueError("Expected exactly one production native-wrapper definition")
    start = starts[0].start()
    opening = source.index("{", starts[0].end())
    depth = 0
    state = "code"
    i = opening
    while i < len(source):
        char = source[i]
        pair = source[i:i + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if pair == "*/":
                state = "code"
                i += 1
        elif state in ('"', "'"):
            if char == "\\":
                i += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            i += 1
        elif pair == "/*":
            state = "block"
            i += 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1] + "\n"
        i += 1
    raise ValueError("Unbalanced production native-wrapper definition")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = (ROOT / "src/loader/armada_ce_maple_traffic.c").read_text()
    definition = extract(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("/* Extracted unchanged from production source. */\n" + definition)


if __name__ == "__main__":
    main()
