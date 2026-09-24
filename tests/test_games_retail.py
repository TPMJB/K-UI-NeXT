#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT preparation for native retail boot; no retail game bytes."""
from pathlib import Path
import shutil
import struct
import tempfile
from games_fixture import make_fixture, dual32
from test_images import run
from test_loader_probe_images import digest, partition_image, check_fs, envelope

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-retail")
CASES = (
    "valid", "boot-tail", "fragmented", "fragment-limit", "missing-track",
    "payload-checksum", "layout-manifest", "layout-entry", "layout-size",
    "layout-resident", "layout-flags", "manifest-not-empty", "bad-ip", "bad-title",
    "bad-bootfile", "bad-media", "windows-ce", "bad-flags", "boot-small", "boot-large",
    "sector-beforedata", "sector-aftercard", "sector-repeat", "seek-fail",
    "read-fail", "close-fail", "unmount-fail", "cancel-before", "cancel-map",
    "cancel-boot", "size-change",
)


def synthetic_package():
    # Structural preparation fixture. The stage is original data, never code
    # executed on the host; every retail-looking IP field is generated here.
    payload = bytearray(0x2010)
    payload[0x100:0x108] = b"KUIRBT01"
    struct.pack_into("<14I", payload, 0x108,
                     1, 64, 0x1000, 4096, 0x8CE00000, 16,
                     0x8CE00000, 0x8C010000, 0xC00000, 0x8CFF0000,
                     0x2000, 0x8C008300, 0x8C00D000, 0)
    payload[0x2000:] = b"ORIGINALSTAGE123"
    return envelope(payload, len(payload), "0123456789ab")


def make_retail_fixture(folder, case):
    make_fixture(folder)
    track = folder / "track03.bin"
    data = bytearray(track.read_bytes())
    data[16 + 128:16 + 256] = b"DEAD OR ALIVE 2".ljust(128)
    boot_bytes = {"boot-tail": 3001, "boot-small": 127,
                  "boot-large": 0xC00001}.get(case, 4096)
    # The third root record describes our generated random test bytes.
    dual32(data, 20 * 2352 + 16 + 68 + 10, boot_bytes)
    track.write_bytes(data)
    (folder / "track02.raw").rename(folder / "music track02.raw")
    gdi = folder / "disc.gdi"
    gdi.write_text(gdi.read_text().replace("track02.raw", '"music track02.raw"'), encoding="ascii")
    (folder / "retail-boot.kui").write_bytes(synthetic_package())


def main():
    for binary in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat"):
        if not shutil.which(binary):
            raise SystemExit(f"Missing test prerequisite: {binary}")
    with tempfile.TemporaryDirectory(prefix="kui-retail-prep-") as temp:
        base = Path(temp)
        fixture = base / "original-gdi"
        for kind in ("fat32", "exfat"):
            volume = base / f"{kind}-volume.img"
            with volume.open("wb") as out:
                out.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(volume)) if kind == "fat32" else run("mkfs.exfat", str(volume))
            partitioned_clean = base / f"{kind}-mbr.img"
            partition_image(volume, partitioned_clean, kind)
            for partitioned, cases in ((True, CASES), (False, ("valid", "boot-tail", "fragmented"))):
                clean = partitioned_clean if partitioned else volume
                layout = "MBR" if partitioned else "superfloppy"
                for case in cases:
                    make_retail_fixture(fixture, case)
                    image = base / "working.img"
                    shutil.copyfile(clean, image)
                    run(BINARY, str(image), str(fixture), "seed", case)
                    before = digest(image)
                    output = run(BINARY, str(image), str(fixture), "check", case)
                    assert f"PASS retail preparation check {case}; no active-operation writes" in output
                    assert digest(image) == before, f"Retail preparation changed {kind} {layout} in {case}"
                    if case in ("valid", "boot-tail", "fragmented"):
                        assert "full IP and exact boot CRCs" in output
                        check_fs(image, base / "check-volume.img", kind, partitioned)
                    image.unlink()
                    print(f"PASS {kind} {layout} retail preparation: {case}; whole-card SHA256 unchanged", flush=True)


if __name__ == "__main__":
    main()
