#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The read probe must pass the bootstrap contract on all three transports."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import runtime_package as runtime


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_vmu_read_probe_package.py check-package")
    checker = Path(sys.argv[1]).resolve()
    marker = struct.pack("<5I", 0x5349554B, 0x544F4F42, 1, 3, 0xFFFFFFFC)
    payload = bytearray(128)
    payload[32:52] = marker
    good = runtime.envelope(payload, 256, "0123456789ab")
    cases = [("valid", good, True)]
    end_marker = bytearray(128)
    end_marker[-20:] = marker
    cases.append(("marker-at-end", runtime.envelope(end_marker, 128, "0123456789ab"), True))
    for name, data in (
        ("absent", bytes(128)),
        ("duplicate", bytes(payload[:64]) + marker + bytes(44)),
        ("unaligned", bytes(5) + marker + bytes(103)),
    ):
        package = runtime.envelope(data, 256, "0123456789ab")
        runtime.verify(package)  # A valid envelope alone did not catch the launch failure.
        cases.append((name, package, False))
    for index, name in ((8, "wrong-marker-version"), (12, "patched-marker"),
                        (16, "wrong-marker-inverse")):
        malformed = bytearray(payload)
        malformed[32 + index] ^= 1
        cases.append((name, runtime.envelope(malformed, 256, "0123456789ab"), False))
    cases.extend((name, data, False) for name, data in runtime.rejection_cases(good).items())
    cases.append(("trailing-data", good + b"extra", False))
    with tempfile.TemporaryDirectory(prefix="vmu-read-probe-package-") as temporary:
        path = Path(temporary) / "tools.kui"
        for name, package, expected in cases:
            path.write_bytes(package)
            result = subprocess.run([str(checker), str(path)], text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            assert (result.returncode == 0) == expected, (name, result.returncode, result.stdout)
            assert "AddressSanitizer" not in result.stdout and "runtime error:" not in result.stdout, (
                name, result.stdout)
            assert path.read_bytes() == package, name
    print(f"PASS {len(cases)} read-probe package cases: exact C loader contract, malformed handoff and zero writes")


if __name__ == "__main__":
    main()
