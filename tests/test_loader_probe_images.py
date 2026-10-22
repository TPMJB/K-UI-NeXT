#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate resident-loader extent handoff on real partitioned FAT32/exFAT cards.

The C harness rereads every mapped sector through a raw file after the production
adapter has closed all files and disconnected storage. This catches missing MBR
translation, stale FIL.sect values, and broken fragmented-file maps. These are
host adapter checks; they do not establish SH-4 execution or console SD support.
"""
from pathlib import Path
import hashlib
import shutil
import struct
import sys
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from make_loader_probe import fixture_bytes
from runtime_package import envelope

BINARY = str(ROOT / "build/loader-probe-image")
PARTITION_START = 2048
CASES = (
    "valid", "fragmented", "missing-payload", "missing-fixture",
    "payload-checksum", "payload-truncated", "payload-memory",
    "layout-magic", "layout-manifest", "layout-entry", "layout-overrun",
    "layout-reserved", "manifest-not-empty", "fixture-short", "fixture-long",
    "fixture-corrupt", "connect-fail", "mount-fail", "payload-open-fail",
    "fixture-open-fail", "payload-read-fail", "fixture-read-fail",
    "payload-short-read", "fixture-short-read", "seek-fail", "payload-close-fail",
    "fixture-close-fail", "unmount-fail", "cancel-before", "cancel-payload",
    "cancel-fixture", "cancel-close", "sector-beforedata", "sector-aftercard",
    "sector-repeat",
)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def synthetic_package():
    # Host preparation fixture only: never executed. Same structural header and
    # envelope as the real independently linked SH-4 probe, with a 16-byte blob.
    payload = bytearray(0x2010)
    payload[0x100:0x108] = b"KUILDR01"
    struct.pack_into("<14I", payload, 0x108,
                     1, 64, 0x1000, 1600, 0x8CE00000, 16,
                     0x8CE00000, 0x8C010000, 0x8CD00000, 0x8CFF0000,
                     0x2000, 0x100000, 0, 0)
    payload[0x2000:] = b"ORIGINALPROBE123"
    return envelope(payload, len(payload), "0123456789ab")


def partition_image(source, destination, kind):
    sector_count = source.stat().st_size // 512
    mbr = bytearray(512)
    mbr[450] = 0x0C if kind == "fat32" else 0x07
    struct.pack_into("<II", mbr, 454, PARTITION_START, sector_count)
    mbr[510:512] = b"\x55\xaa"
    with destination.open("wb") as out, source.open("rb") as volume:
        out.write(mbr)
        out.write(b"\xa7" * (PARTITION_START * 512 - 512))
        shutil.copyfileobj(volume, out)
        out.write(b"\xc3" * (17 * 512))


def check_fs(card, scratch, kind, partitioned):
    if not partitioned:
        run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(card))
        return
    with card.open("rb") as source, scratch.open("wb") as volume:
        source.seek(PARTITION_START * 512)
        remaining = 96 * 1024 * 1024
        while remaining:
            block = source.read(min(1024 * 1024, remaining))
            assert block
            volume.write(block)
            remaining -= len(block)
    run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(scratch))
    scratch.unlink()


def main():
    for binary in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat"):
        if not shutil.which(binary):
            raise SystemExit(f"Missing test prerequisite: {binary}")
    with tempfile.TemporaryDirectory(prefix="kui-loader-probe-") as temp:
        base = Path(temp)
        fixture = base / "original-fixture"
        fixture.mkdir()
        (fixture / "probe.dat").write_bytes(fixture_bytes())
        (fixture / "probe.kui").write_bytes(synthetic_package())
        for kind in ("fat32", "exfat"):
            volume = base / f"{kind}-volume.img"
            with volume.open("wb") as out:
                out.truncate(96 * 1024 * 1024)
            if kind == "fat32":
                run("mkfs.fat", "-F", "32", str(volume))
            else:
                run("mkfs.exfat", str(volume))
            partitioned_clean = base / f"{kind}-mbr.img"
            partition_image(volume, partitioned_clean, kind)
            for partitioned, cases in ((True, CASES), (False, ("valid", "fragmented"))):
                clean = partitioned_clean if partitioned else volume
                layout = "MBR" if partitioned else "superfloppy"
                for case in cases:
                    image = base / "working.img"
                    shutil.copyfile(clean, image)
                    run(BINARY, str(image), str(fixture), "seed", case)
                    before = digest(image)
                    output = run(BINARY, str(image), str(fixture), "check", case)
                    assert f"PASS loader probe check {case}; no active-operation writes" in output
                    assert digest(image) == before, f"Loader adapter changed {kind} {layout} card in {case}"
                    if case in ("valid", "fragmented", "fixture-corrupt"):
                        assert "Physical manifest PASS: 111 blocks" in output
                        check_fs(image, base / "check-volume.img", kind, partitioned)
                    image.unlink()
                    print(f"PASS {kind} {layout} loader probe: {case}; whole-card SHA256 unchanged", flush=True)


if __name__ == "__main__":
    main()
