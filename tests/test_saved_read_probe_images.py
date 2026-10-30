#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only candidate comparison on actual FAT32/exFAT and independent sectors."""
from pathlib import Path
import hashlib
import shutil
import struct
import tempfile
import zlib
from make_recovery_vectors import sector
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build/saved-read-probe-image"
CASES = ("clean", "cd-clean", "header-change", "length-change", "bad-header", "header-reserved", "truncated", "empty",
         "bad-record", "bad-index", "bad-coordinate", "bad-identity", "bad-sector",
         "bulk-transient", "valid-different", "read-fail", "short-read", "open-fail",
         "close-fail", "mount-fail", "cancel-before", "cancel-mid", "budget", "observe-mutation")


def fixture(directory, cd=False):
    directory.mkdir()
    header = bytearray(4096)
    start = 150 if cd else 45150
    header[:8] = b"KUISALV3" if cd else b"KUISALV2"
    struct.pack_into("<4I", header, 8, 3 if cd else 2, 4096, 1, 1)
    struct.pack_into("<2I", header, 24, start, start+1)
    header[32:64] = hashlib.sha256(sector(start, 0)).digest()
    header[64:96] = hashlib.sha256(sector(start+1, 1)).digest()
    struct.pack_into("<6I", header, 96, 1, 4, 0 if cd else 1, start, start+64, start+64)
    if cd:
        struct.pack_into("<I", header, 4064, 1)
    struct.pack_into("<I", header, 4092, zlib.crc32(header[:-4]))
    identity = hashlib.sha256(header).digest()

    def patch(slot, seed):
        record = bytearray(5120)
        record[:8] = b"KUISPAT2"
        struct.pack_into("<4I", record, 8, slot, 0, slot, 1)
        record[32:64] = identity
        record[2416:4768] = sector(start + slot, seed)
        struct.pack_into("<2I", record, 24, zlib.crc32(bytes(2352)),
                         zlib.crc32(record[2416:4768]))
        struct.pack_into("<I", record, 5116, zlib.crc32(record[:-4]))
        return record

    (directory / "header.bin").write_bytes(header)
    (directory / "patches.bin").write_bytes(b"".join(patch(i, i) for i in range(64)))
    (directory / "alternate.bin").write_bytes(patch(0, 193))


def main():
    with tempfile.TemporaryDirectory(prefix="kui-saved-read-") as temporary:
        base = Path(temporary)
        fixture(base / "fixture")
        fixture(base / "fixture-cd", cd=True)
        for kind in ("fat32", "exfat"):
            seed = base / (kind + "-seed.img")
            with seed.open("wb") as output:
                output.truncate(96 * 1024 * 1024)
            if kind == "fat32":
                run("mkfs.fat", "-F", "32", str(seed))
            else:
                run("mkfs.exfat", "-c", "131072", str(seed))
            for case in CASES:
                image = base / (kind + "-" + case + ".img")
                shutil.copyfile(seed, image)
                directory = base / ("fixture-cd" if case == "cd-clean" else "fixture")
                output = run(str(BINARY), str(image), str(directory), case)
                assert f"PASS saved read probe {case}; sources unchanged, no probe writes" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} saved read probe: {case}", flush=True)


if __name__ == "__main__":
    main()
