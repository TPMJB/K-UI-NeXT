#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise generated CE descriptors through the production C parser."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    spec = importlib.util.spec_from_file_location("smash_ce_profile", ROOT / "tools/smash_ce_profile.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    helper = bytes((i * 19 + 7) & 255 for i in range(32768))
    with tempfile.TemporaryDirectory(prefix="smash-ce-profile-") as temporary:
        work = Path(temporary)
        profiles = [work / name for name in ("vc2.bin", "swirl.bin", "vc2-embedded.bin", "swirl-embedded.bin")]
        for i, path in enumerate(profiles):
            path.write_bytes(module.profile(helper, i % 2 + 1, embedded=i >= 2))
        assert profiles[0].read_bytes() != profiles[1].read_bytes()
        for bad in (b"", b"a", bytes(49156)):
            try:
                module.profile(bad, 1)
            except ValueError:
                pass
            else:
                raise AssertionError("invalid helper accepted")
        executable = work / "test-profile"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wpedantic", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-pie", "-no-pie",
                        "-Iinclude", "tests/test_smash_ce_profile.c", "src/core/smash_ce_profile.c",
                        "src/core/retail_image.c", "-o", str(executable)], cwd=ROOT, check=True)
        subprocess.run([str(executable), *map(str, profiles)], cwd=ROOT, check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
