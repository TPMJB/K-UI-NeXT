#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual cooperative CDDA engine and controlled client natively.

Generated storage and AICA models check call guards, actual source cursors,
ring continuity and failure containment. SH ABI/stack isolation, physical
timing and audible quality require separate console evidence.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true",
                        help="enable AddressSanitizer and UndefinedBehaviorSanitizer")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    environment = os.environ.copy()
    if args.sanitize:
        environment.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=0")
        environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    with tempfile.TemporaryDirectory(prefix="kui-cdda-service-") as temporary:
        binary = Path(temporary) / "service"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-DCDDA_TEST_PROFILE=7", "-DCDDA_HARNESS_HOST_TEST=1",
                   "-I", str(root / "include"), str(root / "tests/test_cdda_service.c"),
                   str(root / "src/loader/cdda_client.c")]
        command += [str(root / f"src/core/cdda_{name}.c")
                    for name in ("pcm", "ring", "clock", "stream", "control", "handoff", "job")]
        if args.sanitize:
            command += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command + ["-o", str(binary)], check=True)
        for case in ("pass", "pcm-fail", "data-fail", "data-corrupt", "data-delay90",
                     "data-delay200", "command-reprime-fail", "client-clock-stall"):
            subprocess.run([str(binary), case], check=True, timeout=30, env=environment)
    print("CDDA cooperative service simulations passed (SH ABI and hardware validation remain separate).")


if __name__ == "__main__":
    main()
