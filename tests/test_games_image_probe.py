#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT selected-GDI extent handoff, with bounded fault coverage."""
from pathlib import Path
import shutil
import struct
import tempfile
from games_fixture import make_fixture
from test_images import run
from test_loader_probe_images import digest, partition_image, check_fs, envelope

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-image-probe")
CASES = (
    "valid", "fragmented", "missing-track", "payload-checksum", "layout-manifest",
    "bad-ip", "sector-beforedata", "sector-aftercard", "sector-repeat", "seek-fail",
    "read-fail", "close-fail", "unmount-fail", "cancel-before", "cancel-map", "size-change",
)


def synthetic_package():
    # Structurally valid preparation input, never executed. It contains no
    # Dreamcast or retail executable bytes and reserves a fresh empty manifest.
    payload = bytearray(0x12010)
    payload[0x100:0x108] = b"KUIIMG01"
    struct.pack_into("<14I", payload, 0x108,
                     1, 64, 0x1000, 65536, 0x8CE00000, 16,
                     0x8CE00000, 0x8C010000, 0x8CD00000, 0x8CFF0000,
                     0x12000, 0x100000, 0, 0)
    payload[0x12000:] = b"ORIGINALPROBE123"
    return envelope(payload, len(payload), "0123456789ab")


def main():
    for binary in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat"):
        if not shutil.which(binary):
            raise SystemExit(f"Missing test prerequisite: {binary}")
    with tempfile.TemporaryDirectory(prefix="kui-selected-image-") as temp:
        base = Path(temp)
        fixture = base / "original-gdi"
        make_fixture(fixture)
        (fixture / "track02.raw").rename(fixture / "music track02.raw")
        gdi = fixture / "disc.gdi"
        gdi.write_text(gdi.read_text().replace("track02.raw", '"music track02.raw"'), encoding="ascii")
        (fixture / "image-probe.kui").write_bytes(synthetic_package())
        for kind in ("fat32", "exfat"):
            volume = base / f"{kind}-volume.img"
            with volume.open("wb") as out:
                out.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(volume)) if kind == "fat32" else run("mkfs.exfat", str(volume))
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
                    assert f"PASS selected-image probe check {case}; no active-operation writes" in output
                    assert digest(image) == before, f"Selected-image preparation changed {kind} {layout} card in {case}"
                    if case in ("valid", "fragmented"):
                        assert "Detached selected-image read PASS" in output
                        check_fs(image, base / "check-volume.img", kind, partitioned)
                    image.unlink()
                    print(f"PASS {kind} {layout} selected-image: {case}; whole-card SHA256 unchanged", flush=True)


if __name__ == "__main__":
    main()
