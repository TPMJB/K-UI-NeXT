#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT preparation for native retail boot; no retail game bytes."""
from pathlib import Path
import argparse
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
    "layout-resident", "layout-flags", "manifest-not-empty", "bad-ip", "other-title",
    "alternate-bootfile", "blank-title", "cdda-warning", "unsupported-2048",
    "tracks-31", "tracks-99", "async-tracks-31", "async-tracks-40",
    "boot-low-density", "boot-overlap-ip",
    "bad-bootfile", "bad-media", "windows-ce", "bad-flags", "boot-small", "boot-large",
    "sector-beforedata", "sector-aftercard", "sector-repeat", "seek-fail",
    "read-fail", "close-fail", "unmount-fail", "cancel-before", "cancel-map",
    "cancel-ip", "size-change", "async-on-sci", "async-on-scif",
    "ce-probe", "ce-probe-native", "ce-probe-package", "ce-probe-small", "ce-probe-scif",
    "ce-probe-async",
)
RC_CASES = ("valid", "other-title", "alternate-bootfile", "cdda-warning",
            "windows-ce", "bad-flags", "bad-media", "bad-bootfile", "unsupported-2048",
            "tracks-99", "boot-low-density", "boot-overlap-ip", "blank-title", "ce-probe")
SUCCESS_CASES = ("valid", "boot-tail", "fragmented", "other-title",
                 "alternate-bootfile", "cdda-warning", "blank-title",
                 "async-on-sci", "async-on-scif", "ce-probe", "ce-probe-async",
                 "tracks-31", "tracks-99", "async-tracks-31", "async-tracks-40")
# Track counts beyond the fixture's three: MDK2's 31, GD-ROM's 99, and 40,
# which fits the background reader's 64 slots only with audio left unmapped.
MANY_TRACKS = {"tracks-31": 31, "tracks-99": 99, "async-tracks-31": 31, "async-tracks-40": 40}
AUDIO_UNMAPPED = ("tracks-99", "async-tracks-40")


def synthetic_package(ce=False):
    # Structural preparation fixture. The stage is original data, never code
    # executed on the host; every retail-looking IP field is generated here.
    # ce: the Windows CE boot test's package, with its higher stage.
    payload = bytearray(0x2010)
    payload[0x100:0x108] = b"KUIRCE01" if ce else b"KUIRBT01"
    stage = 0x8CE10000 if ce else 0x8CE00000
    struct.pack_into("<14I", payload, 0x108,
                     1, 64, 0x1000, 4096, stage, 16,
                     stage, 0x8C010000, 0xC00000, 0x8CFF0000,
                     0x2000, 0x8C008300, 0x8C00BB00, 0)
    payload[0x2000:] = b"ORIGINALSTAGE123"
    return envelope(payload, len(payload), "0123456789ab")


def make_retail_fixture(folder, case):
    make_fixture(folder)
    track = folder / "track03.bin"
    data = bytearray(track.read_bytes())
    data[16 + 128:16 + 256] = b"DEAD OR ALIVE 2".ljust(128)
    if case == "other-title":
        data[16 + 128:16 + 256] = b"Independent Native Game".ljust(128)
    if case == "blank-title":
        data[16 + 128:16 + 256] = b" " * 128
    if case.startswith("ce-probe") and case != "ce-probe-native":
        # The IP's peripheral field selects Windows CE (bit zero of its last
        # digit). Set in the fixture so the mapped tracks still match it.
        data[16 + 62] = ord("1")
    if case == "alternate-bootfile":
        data[16 + 96:16 + 112] = b"ALT_BOOT.BIN".ljust(16)
        data[20 * 2352 + 16 + 68 + 33:20 * 2352 + 16 + 68 + 47] = b"ALT_BOOT.BIN;1"
    boot_bytes = {"boot-tail": 3001, "boot-small": 127, "ce-probe-small": 2048,
                  "boot-large": 0xC00001}.get(case, 4096)
    # The third root record describes our generated random test bytes.
    dual32(data, 20 * 2352 + 16 + 68 + 10, boot_bytes)
    if case in ("boot-low-density", "boot-overlap-ip"):
        dual32(data, 20 * 2352 + 16 + 68 + 2,
               0 if case == "boot-low-density" else 45000)
    track.write_bytes(data)
    (folder / "track02.raw").rename(folder / "music track02.raw")
    gdi = folder / "disc.gdi"
    gdi.write_text(gdi.read_text().replace("track02.raw", '"music track02.raw"'), encoding="ascii")
    if case == "cdda-warning":
        shutil.copyfile(folder / "music track02.raw", folder / "music track04.raw")
        gdi.write_text(gdi.read_text().replace("3\n", "4\n", 1) +
                       '4 45064 0 2352 "music track04.raw" 0\n', encoding="ascii")
    if case == "unsupported-2048":
        gdi.write_text(gdi.read_text().replace("4 2352 track01", "4 2048 track01"), encoding="ascii")
    if case in MANY_TRACKS:
        count = MANY_TRACKS[case]
        text = gdi.read_text().replace("3\n", f"{count}\n", 1)
        for number in range(4, count + 1):
            name = "music track04.raw" if number == 4 else f"track{number:02d}.raw"
            shutil.copyfile(folder / "music track02.raw", folder / name)
            text += f'{number} {45064 + (number - 4) * 4} 0 2352 "{name}" 0\n'
        gdi.write_text(text, encoding="ascii")
    (folder / "retail-boot.kui").write_bytes(synthetic_package())
    (folder / "ce-probe.kui").write_bytes(synthetic_package(ce=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rc-only", action="store_true",
                        help="Only native-title/boot selection and compatibility cases, on MBR FAT32/exFAT")
    args = parser.parse_args()
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
            layouts = ((True, RC_CASES),) if args.rc_only else (
                (True, CASES), (False, ("valid", "boot-tail", "fragmented")))
            for partitioned, cases in layouts:
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
                    if case in SUCCESS_CASES:
                        assert "full IP CRC, exact boot bytes and headers" in output
                        check_fs(image, base / "check-volume.img", kind, partitioned)
                    if case == "cdda-warning":
                        assert "CD audio playback is unsupported" in output
                    if case == "async-on-sci":
                        assert "Retail boot reader: background SCI stream" in output
                    if case == "async-on-scif":
                        assert "background reader needs SCI microSD; using the standard reader" in output
                    if case == "windows-ce":
                        assert "Windows CE game launching is not supported" in output
                    if case == "ce-probe":
                        assert "Windows CE boot test prepared" in output
                        assert "background SCI stream" not in output
                    if case == "ce-probe-async":
                        assert "Windows CE boot test prepared" in output
                        assert "Retail boot reader: background SCI stream (test, Windows CE interrupts)" in output
                    if case == "ce-probe-native":
                        assert "the Windows CE boot test needs a Windows CE image" in output
                    if case == "ce-probe-package":
                        assert "unsupported Windows CE probe package layout" in output
                    if case == "ce-probe-small":
                        assert "larger than its 2048-byte prefix" in output
                    if case == "ce-probe-scif":
                        assert "the Windows CE boot test needs SCI microSD" in output
                    if case == "unsupported-2048":
                        assert "raw 2352-byte GDI tracks with zero file offsets required" in output
                    if case in MANY_TRACKS:
                        unmapped = "audio tracks listed without their files" in output
                        assert unmapped == (case in AUDIO_UNMAPPED), case
                        assert ("Retail boot reader: background SCI stream" in output) == \
                            case.startswith("async-"), case
                    if case in ("boot-low-density", "boot-overlap-ip"):
                        assert "boot executable and full IP must be in high-density data tracks" in output
                    image.unlink()
                    print(f"PASS {kind} {layout} retail preparation: {case}; whole-card SHA256 unchanged", flush=True)


if __name__ == "__main__":
    main()
