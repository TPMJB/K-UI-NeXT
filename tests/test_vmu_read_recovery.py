#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the actual read-only GUI capture adapter without an SD filesystem.

The public capture API must never connect storage, so its already linked FatFs
boundary is deliberately given only an unformatted single-sector placeholder.
The C oracle verifies source bytes, exact reply counts, refusal boundaries,
unchanged output on failure and the independent later capture comparison.
"""
import os
from pathlib import Path
import subprocess
import tempfile
from test_vmu_images import BINARY, CAPTURE

with tempfile.TemporaryDirectory(prefix="kui-vmu-read-recovery-") as temporary:
    placeholder=Path(temporary)/"unmounted.img"
    placeholder.write_bytes(bytes(512))
    for case in CAPTURE:
        result=subprocess.run([BINARY,str(placeholder),case],text=True,capture_output=True,
            env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
        if result.returncode:
            print(result.stdout,end="")
            print(result.stderr,end="")
            raise SystemExit(f"VMU capture case failed: {case} ({result.returncode})")
        assert f"PASS VMU {case}" in result.stdout
        assert placeholder.read_bytes()==bytes(512)
        print(f"PASS actual VMU capture: {case}",flush=True)
print(f"PASS {len(CAPTURE)} actual VMU capture cases under ASan/UBSan; no SD or source writes")
