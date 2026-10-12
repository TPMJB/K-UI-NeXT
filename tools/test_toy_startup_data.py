#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run DATA-only startup scheduling fixtures with sanitizers and exact flags."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
COMMON = ["KUI_TOY_PILOT_DATA_SCHEDULE_MODE=2", "KUI_TOY_PILOT_PRIVATE_P2=1",
          "KUI_TOY_PILOT_GD_FIXED_STEP=2", "KUI_TOY_LOADER_SCHEDULE_HOST_TEST=1",
          "KUI_RETAIL_GD_ASYNC=1"]
ENGINE = ["src/loader/toy_pilot_sci.c", "src/core/retail_cursor.c",
          "src/core/retail_image.c", "src/core/retail_gd.c"]
CASES = [
    ("startup-sci", ["tests/test_toy_startup_sci.c", *ENGINE],
     ["KUI_TOY_PILOT_SCI_TEST=1"]),
    ("startup-gd", ["tests/test_toy_startup_gd.c", "src/loader/toy_pilot_gd.c",
                    "src/core/retail_gd.c"], ["KUI_RETAIL_GD_REJECTION_DETAILS=1"]),
    ("startup-sci-wire", ["tests/test_toy_startup_sci_wire.c", *ENGINE],
     ["KUI_TOY_PILOT_SCI_TEST=1", "KUI_SCI_STREAM_TEST=1"]),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/startup-host")
    parser.add_argument("--no-sanitizers", action="store_true")
    args = parser.parse_args()
    directory = args.build_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
             "-Iinclude", "-Isrc/loader"]
    if not args.no_sanitizers:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    env = dict(os.environ)
    # The execution environment restricts /proc task enumeration. Keep ASan
    # and UBSan active; these fixtures perform no heap allocation requiring
    # LeakSanitizer's process/thread discovery.
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    results = []
    for name, sources, extra in CASES:
        executable = directory / name
        command = [*shlex.split(args.cc), *flags,
                   *["-D" + value for value in [*COMMON, *extra]],
                   *sources, "-o", str(executable)]
        compiled = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
        if compiled.returncode:
            raise RuntimeError(name + " compilation failed:\n" + compiled.stdout + compiled.stderr)
        run = subprocess.run([str(executable)], cwd=ROOT, env=env, capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(name + " failed:\n" + run.stdout + run.stderr)
        print(run.stdout.rstrip(), flush=True)
        results.append({"name": name, "command": command, "exit_code": run.returncode,
                        "output": run.stdout, "stderr": run.stderr,
                        "source_sha256": {source: hashlib.sha256((ROOT / source).read_bytes()).hexdigest()
                                          for source in sources}})
    (directory / "startup-data-tests.json").write_text(json.dumps({
        "schema": 1, "sanitizers": [] if args.no_sanitizers else ["address", "undefined"],
        "leak_sanitizer": "disabled: restricted /proc and no fixture heap allocation",
        "scope": "host protocol/card/timing/cache seams; hardware playback not established",
        "results": results,
    }, indent=2) + "\n")
    print("Startup DATA scheduling suites passed (independent audio profile)")


if __name__ == "__main__":
    main()
