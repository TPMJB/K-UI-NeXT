#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Count real FAT reads for near-end repairs, with the production cache helper."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = (
    "fresh-forward", "fresh-reverse", "cached-forward", "cached-reverse",
    "grown-map", "map-cap", "allocation-fallback", "map-error", "close-error",
    "track-change-close-error", "packed", "missing-packed", "stop", "not-ready",
    "length", "readonly", "readonly-track-upgrade", "readonly-packed-upgrade", "upgrade",
)


def fixture(path, kind):
    # A real FAT32 superfloppy, with an exactly sized fixed track. Keeping the
    # sparse fixture formatter here avoids requiring mkfs tools for this test.
    total, reserved, fats = 327680, 32, 2
    fat_sectors = 1
    while True:
        clusters = total - reserved - fats * fat_sectors
        required = (clusters + 2 + 127) // 128
        if required == fat_sectors:
            break
        fat_sectors = required
    assert fat_sectors == 2521 and clusters >= 65525
    file_sectors = (64 * 1024 * 1024) // 2352
    file_bytes = file_sectors * 2352
    count = (file_bytes + 511) // 512
    boot = bytearray(512)
    boot[:3], boot[3:11] = b"\xebX\x90", b"KUI TEST"
    struct.pack_into("<HBHBHHBHHHII", boot, 11, 512, 1, reserved, fats, 0, 0, 0xF8, 0, 63, 255, 0, total)
    struct.pack_into("<IHHIHH", boot, 36, fat_sectors, 0, 0, 2, 1, 6)
    boot[64], boot[66] = 0x80, 0x29
    struct.pack_into("<I", boot, 67, 12345)
    boot[71:82], boot[82:90], boot[510:512] = b"KUI PROFILE", b"FAT32   ", b"\x55\xaa"
    info = bytearray(512)
    struct.pack_into("<I", info, 0, 0x41615252)
    struct.pack_into("<I", info, 484, 0x61417272)
    struct.pack_into("<III", info, 488, 0xFFFFFFFF, 0xFFFFFFFF, 0xAA550000)
    fat = bytearray(fat_sectors * 512)
    struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF)

    def cluster(n):
        if kind == "cap":
            return 3 + 2 * n
        if kind == "fragmented":
            return 3 + n + n // 4096
        return 3 + n

    for n in range(count):
        struct.pack_into("<I", fat, cluster(n) * 4, cluster(n + 1) if n + 1 < count else 0x0FFFFFFF)
    assert cluster(count - 1) < clusters + 2
    root = bytearray(512)
    root[:11], root[11] = b"TRACK   BIN", 0x20
    struct.pack_into("<H", root, 26, 3)
    struct.pack_into("<I", root, 28, file_bytes)
    with path.open("wb") as stream:
        stream.truncate(total * 512)
        stream.write(boot)
        stream.write(info)
        stream.seek(6 * 512)
        stream.write(boot)
        stream.write(info)
        for n in range(fats):
            stream.seek((reserved + n * fat_sectors) * 512)
            stream.write(fat)
        stream.seek((reserved + fats * fat_sectors) * 512)
        stream.write(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/salvage-io-profile")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    results = {}
    with tempfile.TemporaryDirectory(prefix="kui-salvage-io-") as temporary:
        path = Path(temporary) / "fat32.img"
        for case in CASES:
            kind = "cap" if case == "map-cap" else "fragmented" if case in ("grown-map", "allocation-fallback") else "contiguous"
            fixture(path, kind)
            run = subprocess.run([str(args.binary.resolve()), str(path), case], text=True, capture_output=True)
            if run.returncode:
                raise RuntimeError(f"{case}: exit {run.returncode}\n{run.stdout}{run.stderr}")
            results[case] = json.loads(run.stdout)
            print(f"PASS salvage I/O: {case}", flush=True)
    for direction in ("forward", "reverse"):
        fresh, cached = results[f"fresh-{direction}"], results[f"cached-{direction}"]
        assert fresh["targets"] == cached["targets"] == 128
        assert fresh["fat_sectors"] > 100000
        assert cached["fat_sectors"] == 0
        assert cached["setup_fat_sectors"] < fresh["fat_sectors"] // 100
        assert cached["read_sectors"] < fresh["read_sectors"] // 100
        assert fresh["write_calls"] == cached["write_calls"]
        assert fresh["sync_calls"] == cached["sync_calls"] == 128
    assert 128 < results["grown-map"]["map_peak_bytes"] <= 32768
    assert results["map-cap"]["map_peak_bytes"] == results["allocation-fallback"]["map_peak_bytes"] == 0
    output = {"fixture": "64MiB planned raw track in a sparse FAT32 volume;512-byte clusters;128 near-end targets", "results": results}
    if args.output:
        args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(f"PASS salvage I/O suite: {len(results)} cases")


if __name__ == "__main__":
    main()
