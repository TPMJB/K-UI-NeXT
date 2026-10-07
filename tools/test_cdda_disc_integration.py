#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual disc BIOS-vector CDDA engine and controlled client natively.

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
    parser.add_argument("--profile", type=int, choices=(11, 12), default=11,
                        help="11: generated six-track map; 12: selected original track 14")
    parser.add_argument("--case", choices=("pass", "pcm-fail", "data-fail", "data-corrupt", "data-delay90",
                        "data-delay200", "leave-delay200", "command-reprime-fail", "client-clock-stall",
                        "restore-readback-fail", "map-parse", "map-missing", "map-truncated", "map-overlap",
                        "source-switch-fail", "raw-mode"), help="run one scenario")
    parser.add_argument("--selected-raw", type=Path, help="optional owner-supplied track14.raw reference (profile12)")
    parser.add_argument("--selected-gdi", type=Path, help="optional owner-supplied GDI; requires --selected-raw")
    args = parser.parse_args()
    if (args.selected_raw or args.selected_gdi) and args.profile != 12:
        parser.error("private references require profile12")
    if args.selected_gdi and not args.selected_raw:
        parser.error("--selected-gdi requires --selected-raw")
    root = Path(__file__).resolve().parents[1]
    environment = os.environ.copy()
    if args.sanitize:
        environment.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=0")
        environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    with tempfile.TemporaryDirectory(prefix="kui-cdda-disc-") as temporary:
        binary = Path(temporary) / "disc"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   f"-DCDDA_TEST_PROFILE={args.profile}", "-DCDDA_HARNESS_HOST_TEST=1",
                   "-I", str(root / "include"), str(root / "tests/test_cdda_disc_integration.c"),
                   str(root / ("src/loader/cdda_disc_toy_client.c" if args.profile == 12 else
                               "src/loader/cdda_disc_client.c"))]
        command += [str(root / f"src/core/cdda_{name}.c")
                    for name in ("pcm", "ring", "clock", "stream", "control", "handoff", "job", "disc", "disc_bios")]
        command += [str(root / "src/core/game_image.c")]
        if args.sanitize:
            command += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command + ["-o", str(binary)], check=True)
        cases = (("pass", "pcm-fail", "command-reprime-fail", "client-clock-stall", "restore-readback-fail",
                  "map-parse", "map-missing", "map-truncated", "map-overlap") if args.profile == 12 else
                 ("pass", "pcm-fail", "data-fail", "data-corrupt", "data-delay90", "data-delay200",
                  "leave-delay200", "command-reprime-fail", "source-switch-fail", "client-clock-stall",
                  "restore-readback-fail", "map-parse", "map-missing", "map-truncated", "map-overlap", "raw-mode"))
        if args.case:
            if args.case not in cases:
                parser.error("scenario is not supported by this profile")
            cases = (args.case,)
        for case in cases:
            arguments = [str(binary), case]
            if args.selected_raw:
                arguments.append(str(args.selected_raw.resolve()))
            if args.selected_gdi:
                arguments.append(str(args.selected_gdi.resolve()))
            subprocess.run(arguments, check=True, timeout=30, env=environment)
    print(f"CDDA profile {args.profile} disc BIOS-vector simulations passed "
          "(SH ABI and hardware validation remain separate).")


if __name__ == "__main__":
    main()
