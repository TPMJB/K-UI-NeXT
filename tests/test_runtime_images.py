#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read runtime envelopes with the console's FatFs loader on both filesystems."""
from pathlib import Path
import hashlib
import shutil
import struct
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import runtime_package as runtime
from test_images import run


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def main():
    good = runtime.envelope(bytes(range(256)) * 300, 100000, "0123456789ab")
    cases = {"valid": good, **runtime.rejection_cases(good), "short-header": good[:30],
             "trailing-data": good + b"tail",
             "max-valid": runtime.envelope(bytes(range(256)) * (runtime.MAX_BYTES // 256),
                                            runtime.MAX_MEMORY, "0123456789ab")}
    bad_header_crc = bytearray(good)
    bad_header_crc[60] ^= 1
    cases["bad-header-checksum"] = bytes(bad_header_crc)
    for name, offset, value in (("bad-load-address", 20, runtime.ADDRESS - 4),
            ("bad-entry-address", 24, runtime.ADDRESS + 4), ("unaligned", 16, 7),
            ("zero-payload", 16, 0), ("overflow", 16, 0xFFFFFFFC),
            ("small-memory", 28, 4), ("unaligned-memory", 28, 100001),
            ("excess-memory", 28, runtime.MAX_MEMORY + 4), ("unknown-flags", 36, 1),
            ("bad-build", 40, 0xFFFFFFFF)):
        changed = bytearray(good)
        struct.pack_into("<I", changed, offset, value)
        struct.pack_into("<I", changed, 60, zlib.crc32(changed[:60]))
        cases[name] = bytes(changed)
    binary = str(ROOT / "build/runtime-image")
    with tempfile.TemporaryDirectory(prefix="kui-runtime-images-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream: stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(clean)) if kind == "fat32" else run("mkfs.exfat", str(clean))
            for name, data in {"missing": None, **cases}.items():
                image = base / f"{kind}-{name}.img"
                shutil.copyfile(clean, image)
                if data is not None:
                    package = base / "fixture.kui"
                    package.write_bytes(data)
                    run(binary, str(image), "seed", str(package))
                before = digest(image)
                valid = name in ("valid", "max-valid")
                output = run(binary, str(image), "load", "none", expected=0 if valid else 1)
                assert "RESULT: OK" in output if valid else "RESULT: OK" not in output
                assert digest(image) == before, (kind, name, "loader modified the card image")
                if name == "valid":
                    for fault in ("cancel-before", "cancel-during", "read-fail"):
                        output = run(binary, str(image), "load", fault, expected=1)
                        assert "RESULT: OK" not in output
                        assert digest(image) == before
                    run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
            print(f"PASS {kind}: runtime load (including 4 MiB limit), {len(cases) - 2} malformed cases, missing file, cancellation, read fault; no card writes", flush=True)


if __name__ == "__main__":
    main()
