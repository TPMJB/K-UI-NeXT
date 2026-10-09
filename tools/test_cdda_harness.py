#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run actual CDDA homebrew profiles against deterministic host hardware models.

No copyrighted audio is used. This checks orchestration, source cursors, ring
continuity, timer wrapping and failure containment; it does not certify real
SCI/G2/AICA timing, audible quality, stack usage or game sound coexistence.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true",
                        help="also enable AddressSanitizer and UndefinedBehaviorSanitizer")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    cc = os.environ.get("CC", "cc")
    environment = os.environ.copy()
    if args.sanitize:
        # The harness owns no heap allocations. LeakSanitizer's /proc process
        # scan is unavailable under some managed test executors; ASan's actual
        # bounds/use-after-free checks and UBSan remain fully enabled.
        environment.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=0")
        environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    with tempfile.TemporaryDirectory(prefix="kui-cdda-harness-") as temporary:
        for profile in range(7):
            binary = Path(temporary) / f"profile{profile}"
            command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                       f"-DCDDA_TEST_PROFILE={profile}", "-I", str(root / "include"),
                       str(root / "tests/test_cdda_harness.c"),
                       str(root / "src/core/cdda_pcm.c"),
                       str(root / "src/core/cdda_ring.c"),
                       str(root / "src/core/cdda_clock.c")]
            if args.sanitize:
                command += ["-O1", "-g", "-fsanitize=address,undefined",
                            "-fno-omit-frame-pointer"]
            if profile:
                command.append(str(root / "src/core/cdda_stream.c"))
            if profile == 4:
                command.append(str(root / "src/core/cdda_timing.c"))
            if profile >= 5:
                command.append(str(root / "src/core/cdda_control.c"))
            if profile == 6:
                command.append(str(root / "src/core/cdda_job.c"))
            subprocess.run(command + ["-o", str(binary)], check=True)
            cases = ["pass", "pcm-fail"]
            if profile == 3:
                cases += ["data-fail", "data-corrupt", "data-delay", "data-starvation"]
            if profile == 4:
                cases += ["rate-offset", "timing-read-delay"]
            if profile == 5:
                cases.append("command-reprime-fail")
            if profile == 6:
                cases += ["data-slow", "data-fail", "data-corrupt", "data-delay",
                          "data-starvation", "data-coverage-slow", "command-reprime-fail"]
            for case in cases:
                subprocess.run([str(binary), case], check=True, timeout=30,
                               env=environment)
    print("CDDA profile integration simulations passed (hardware validation remains separate).")


if __name__ == "__main__":
    main()
