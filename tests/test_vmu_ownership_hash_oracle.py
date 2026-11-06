#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently serialize a canonical P2 fixture into ownership SHA input."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def audit(binary):
    with tempfile.TemporaryDirectory(prefix='kui-ownership-sha-') as directory:
        path = Path(directory) / 'authored-profile-and-sha.bin'
        subprocess.run([str(Path(binary).resolve()), str(path)], check=True)
        blob = path.read_bytes()
    assert len(blob) == 4128 and blob[:8] == b'KUIVMUP2'
    wire, actual = blob[:4096], blob[4096:]
    parts = [b'KUI/VMU/OWNERSHIP/SHA256/V1\0']
    # Read directly from the independent canonical P2 wire layout. There is
    # no dependency on C structure alignment, pointer width or host byte order.
    parts += [wire[o:o+n] for o,n in ((32,8),(40,8),(48,8),(20,4),(24,4),(28,4),
        (56,16),(72,16),(192,96),(288,4),(292,4),(296,4),(300,4),(304,4),(308,4),
        (312,8),(320,8),(328,8),(336,4),(340,4),(344,4),(352,4),(356,4),(360,8),(368,8))]
    for role in range(3):
        at = 512 + role * 448
        parts += [wire[at+o:at+o+n] for o,n in ((0,8),(48,8),(8,4),(12,4),(16,4),(20,4),
            (24,8),(32,4),(36,4),(40,4),(44,4))]
        for kind in range(2):
            for index in range(16):
                first = at + 64 + kind * 192 + index * 12
                parts.append(wire[first:first+12])
    encoded = b''.join(parts)
    expected = hashlib.sha256(encoded).digest()
    assert actual == expected, 'C ownership SHA differs from independent canonical-wire oracle'
    return dict(schema='kui.vmu.ownership-sha-oracle.v1', status='PASS',
                authored_input_bytes=len(encoded), sha256=expected.hex(),
                independent_oracle='Python hashlib + canonical P2 wire offsets',
                original_assets_used=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    rendered = json.dumps(audit(args.binary), indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end='')
