# SPDX-License-Identifier: GPL-3.0-only
"""Prints the CF board's bill of materials as CSV, from design.py."""
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import design  # noqa: E402

# Suggested parts; any equivalent in the same package will do.
PART = {
    "J1": "3M N7E50-E516PG-30",
    "R1": "470 ohm 1% 0805",
    "R2": "10 kohm 1% 0805",
    "C1": "10 uF 16 V X5R 0805",
    "C2": "100 nF 50 V X7R 0805",
    "C3": "100 nF 50 V X7R 0805",
    "D1": "green LED 0805 (about 2 V at 2 mA)",
    "J2": "nothing (wires), or a 2 x 21 2.0 mm pin header",
    "JP1": "nothing: solder jumper on the board",
    "JP2": "nothing: solder jumper on the board",
    "H1": "nothing, or a nylon M2 screw",
    "H2": "nothing, or a nylon M2 screw",
}


def main():
    groups = {}
    for ref, (_, value, footprint, _, description) in design.PARTS.items():
        key = (value, footprint, description, PART[ref])
        groups.setdefault(key, []).append(ref)
    out = csv.writer(sys.stdout, lineterminator="\n")
    out.writerow(["References", "Quantity", "Value", "Footprint", "Description", "Suggested part"])
    for (value, footprint, description, part), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
        out.writerow([" ".join(refs), len(refs), value, footprint, description, part])


if __name__ == "__main__":
    main()
