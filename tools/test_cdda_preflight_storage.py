#!/usr/bin/env python3
"""Compile the actual profile13 adapter against independent readonly primitives.

The mock controls directory enumeration, file lengths/content and fragmented
allocation. It deliberately rejects CREATE_LINKMAP, allowing the actual bounded
adapter to prove EOF coverage, cache lifetime and refusal/output preservation.
No game assets, console, mount or external service is required.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / ".deps/fatfs/source"
    if not (source / "ff.h").is_file():
        parser.error("The pinned FatFs dependency is required; run tools/fetch_deps.py first")
    values = {
        "FF_FS_READONLY": "1", "FF_FS_MINIMIZE": "0", "FF_USE_FASTSEEK": "1",
        "FF_USE_EXPAND": "0", "FF_USE_CHMOD": "0", "FF_FS_NORTC": "1",
        "FF_FS_CRTIME": "0",
    }
    with tempfile.TemporaryDirectory(prefix="kui-preflight-storage-") as temporary:
        build = Path(temporary)
        shutil.copyfile(source / "ff.h", build / "ff.h")
        config = (root / "config/ffconf.h").read_text()
        config = re.sub(
            r"(?m)^#define (" + "|".join(values) + r")\s+[^\n]*",
            lambda match: "#define " + match[1] + " " + values[match[1]], config,
        )
        (build / "ffconf.h").write_text(config)
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-D_POSIX_C_SOURCE=200809L", "-DCDDA_TEST_PROFILE=13"]
        if args.sanitize:
            flags += ["-O1", "-g", "-fsanitize=address,undefined",
                      "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        executable = build / "test-storage"
        subprocess.run([
            os.environ.get("CC", "cc"), *flags, "-I" + str(build),
            "-I" + str(root / "include"), "-I" + str(root / "src/loader"),
            str(root / "tests/test_cdda_preflight_storage.c"),
            str(root / "src/loader/cdda_preflight_storage.c"),
            str(root / "src/core/hash.c"), str(root / "src/core/data.c"),
            "-o", str(executable),
        ], check=True, cwd=root)
        environment = os.environ.copy()
        if args.sanitize:
            # The container cannot expose /proc task enumeration to LSan.
            # Address and undefined behavior instrumentation remain enabled.
            environment.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=0")
        result = subprocess.run([str(executable)], text=True, capture_output=True,
                                cwd=root, env=environment)
        if result.returncode:
            print(result.stdout, end="")
            print(result.stderr, end="")
            raise subprocess.CalledProcessError(result.returncode, result.args)
        print(result.stdout, end="")
        match = re.fullmatch(r"preflight storage host checks: (\d+)\n", result.stdout)
        if not match:
            raise RuntimeError("Unexpected storage test result")
        if args.json:
            args.json.parent.mkdir(parents=True, exist_ok=True)
            args.json.write_text(json.dumps({
                "checks": int(match[1]), "sanitized": args.sanitize,
                "readonly": True, "private_assets": False, "passed": True,
            }, indent=2) + "\n")


if __name__ == "__main__":
    main()
