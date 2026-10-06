#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT preparation for native retail boot; no retail game bytes."""
from pathlib import Path
import argparse
import re
import shutil
import struct
import tempfile
import zlib
from games_fixture import make_fixture, dual32
from game_format_fixture import make_format_fixture
from test_images import run
from test_loader_probe_images import digest, partition_image, check_fs, envelope

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-retail")
CASES = (
    "valid", "boot-tail", "fragmented", "fragment-limit", "missing-track",
    "payload-checksum", "layout-manifest", "layout-entry", "layout-size",
    "layout-resident", "layout-flags", "manifest-not-empty", "bad-ip", "other-title",
    "alternate-bootfile", "blank-title", "cdda-warning", "bad-cooked-size",
    "cooked-2048", "cooked-boot-tail", "async-cooked-2048", "ce-probe-cooked",
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
            "windows-ce", "bad-flags", "bad-media", "bad-bootfile", "bad-cooked-size",
            "tracks-99", "boot-low-density", "boot-overlap-ip", "blank-title", "ce-probe",
            "cooked-2048", "cooked-boot-tail", "async-cooked-2048", "ce-probe-cooked")
SUCCESS_CASES = ("valid", "boot-tail", "fragmented", "other-title",
                 "alternate-bootfile", "cdda-warning", "blank-title",
                 "async-on-sci", "async-on-scif", "ce-probe", "ce-probe-async",
                 "tracks-31", "tracks-99", "async-tracks-31", "async-tracks-40",
                 "cooked-2048", "cooked-boot-tail", "async-cooked-2048", "ce-probe-cooked")
# Track counts beyond the fixture's three: MDK2's 31, GD-ROM's 99, and 40,
# which fits the background reader's 64 slots only with audio left unmapped.
MANY_TRACKS = {"tracks-31": 31, "tracks-99": 99, "async-tracks-31": 31, "async-tracks-40": 40}
AUDIO_UNMAPPED = ("tracks-99", "async-tracks-40")
FORMAT_CASES = (
    "format-gdi-offset-raw", "format-gdi-offset-cooked",
    "format-iso-gd", "format-iso-cd11700", "format-iso-cd0",
    "format-iso-cd11700-bad-size", "format-iso-cd11700-bad-extent",
    "format-raw-bin", "format-raw-mode2-img",
    "format-cue-shared", "format-cue-2336", "format-cue-2448", "format-cue-scrambled",
    "format-cue-gd-density", "format-cue-scrambled-force-plain",
    "format-cue-shared-fragmented", "format-cue-shared-async", "format-cue-2336-async",
    "format-cue-bad-offset", "format-cue-bad-form2",
    "format-cdi-v2", "format-cdi-v3-native", "format-cdi-v35",
    "format-cdi-v35-pregap150",
    "format-cdi-v35-2336", "format-cdi-v35-2448",
    "format-cdi-v35-async",
    "format-cdi-v35-force-scramble", "format-cdi-v35-bad-encoding",
    "format-cdi-v35-bad-ce-scramble", "format-cdi-v35-bad-ce-plain",
    "format-cdi-v35-bad-footer", "format-cdi-v35-bad-payload",
)
CHECKSUM_CASES = (
    "format-checksum-cooked", "format-checksum-gdi-raw", "format-checksum-iso",
    "format-checksum-raw-bin", "format-checksum-cue-shared", "format-checksum-cue-2336",
    "format-checksum-cue-2448", "format-checksum-cdi",
    "format-checksum-cooked-read-fail", "format-checksum-cooked-short-read",
    "format-checksum-cooked-cancel-read", "format-checksum-cue-shared-read-fail",
    "format-checksum-cue-shared-short-read", "format-checksum-cue-shared-cancel-read",
    "format-checksum-cue-shared-cancel-between-reads", "format-checksum-cue-shared-bad-mode",
    "format-checksum-cue-shared-bad-ip-header",
)
FORMAT_CASES += CHECKSUM_CASES


def make_checksum_fixture(folder, case):
    """Independent exact-byte CRCs and a boot spanning 33 logical sectors.

    The existing generated source sectors/geometry define expected bytes. We
    modify those sources directly, then copy their span into the backing file;
    no parser or preparation code computes the expected CRCs.
    """
    if "gdi-raw" in case:
        source_case, backing = "format-gdi-offset-raw", "selected-track.bin"
    elif "cooked" in case:
        source_case, backing = "format-gdi-offset-cooked", "selected-track.bin"
    elif "iso" in case:
        source_case, backing = "format-iso-cd11700", "selected.iso"
    elif "raw-bin" in case:
        source_case, backing = "format-raw-bin", "selected.bin"
    elif "2336" in case:
        source_case, backing = "format-cue-2336", "data.bin"
    elif "2448" in case:
        source_case, backing = "format-cue-2448", "data.bin"
    elif "cdi" in case:
        source_case, backing = "format-cdi-v35", "selected.cdi"
    else:
        source_case, backing = "format-cue-shared", "shared tracks.bin"
    make_format_fixture(folder, source_case)
    lines = (folder / "format.expect").read_text().splitlines()
    valid, session, boot_lba, _, flags, count = map(int, lines[1].split())
    boot_bytes = 65536 + 1001
    spans = [list(map(int, line.split())) for line in lines[2:]]
    data_index = next(i for i, span in enumerate(spans) if span[0] == session and span[2] == 4)
    _, sectors, _, stride, _, header, offset = spans[data_index]
    source_path = folder / f"expected-track-{data_index + 1:02d}.bin"
    source = bytearray(source_path.read_bytes())
    assert sectors == 64 and len(source) == sectors * stride
    dual32(source, 20 * stride + header + 68 + 10, boot_bytes)
    boot_sectors = (boot_bytes + 2047) // 2048
    boot_padded = bytes((n * 73 + (n >> 8) * 19 + 5) & 255 for n in range(boot_sectors * 2048))
    for sector in range(boot_sectors):
        at = (boot_lba - session + sector) * stride + header
        source[at:at + 2048] = boot_padded[sector * 2048:(sector + 1) * 2048]
    source_path.write_bytes(source)
    actual_path = folder / backing
    actual = bytearray(actual_path.read_bytes())
    actual[offset:offset + len(source)] = source
    # Damage a late sector after metadata probing, without altering the known
    # independent expected source. Checksums must still validate every sector.
    if case.endswith("bad-mode"):
        at = offset + 39 * stride + 18
        actual[at] |= 32
        actual[at + 4] |= 32
    if case.endswith("bad-ip-header"):
        actual[offset + 14 * stride + 1] = 0
    actual_path.write_bytes(actual)
    failed = any(word in case for word in ("read-fail", "short-read", "cancel-", "bad-"))
    lines[1] = f"{int(valid and not failed)} {session} {boot_lba} {boot_bytes} {flags} {count}"
    (folder / "format.expect").write_text("\n".join(lines) + "\n", encoding="ascii")
    ip = b"".join(source[n * stride + header:n * stride + header + 2048] for n in range(16))
    expected_boot = zlib.crc32(boot_padded[:boot_bytes])
    assert expected_boot != zlib.crc32(boot_padded), "tail must affect CRC coverage"
    (folder / "checksum.expect").write_text(f"{zlib.crc32(ip):08x} {expected_boot:08x}\n", encoding="ascii")


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
    if case in FORMAT_CASES:
        if case in CHECKSUM_CASES:
            make_checksum_fixture(folder, case)
        else:
            make_format_fixture(folder, case)
        (folder / "retail-boot.kui").write_bytes(synthetic_package())
        (folder / "ce-probe.kui").write_bytes(synthetic_package(ce=True))
        return
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
    boot_bytes = {"boot-tail": 3001, "cooked-boot-tail": 3001,
                  "boot-small": 127, "ce-probe-small": 2048,
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
    if case == "bad-cooked-size":
        # Merely changing the descriptor must not make raw bytes a valid
        # cooked file. This track's raw length is not divisible by 2048.
        gdi.write_text(gdi.read_text().replace("4 2352 track01", "4 2048 track01"), encoding="ascii")
    if case in ("cooked-2048", "cooked-boot-tail", "async-cooked-2048", "ce-probe-cooked"):
        names = ("track01.bin", "track03.bin") if case == "async-cooked-2048" else ("track03.bin",)
        for name in names:
            path = folder / name
            raw = path.read_bytes()
            assert len(raw) % 2352 == 0
            cooked = b"".join(raw[offset + 16:offset + 2064] for offset in range(0, len(raw), 2352))
            path.write_bytes(cooked)
            gdi.write_text(gdi.read_text().replace(f"4 2352 {name}", f"4 2048 {name}"), encoding="ascii")
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
    parser.add_argument("--formats-only", action="store_true",
                        help="Only generic image-format preparation, on MBR FAT32/exFAT")
    parser.add_argument("--checksums-only", action="store_true",
                        help="Only exact CRC, physical-bound and fault cases, on MBR FAT32/exFAT")
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
            layouts = ((True, CHECKSUM_CASES),) if args.checksums_only else ((True, FORMAT_CASES),) if args.formats_only else ((True, RC_CASES),) if args.rc_only else (
                (True, CASES + FORMAT_CASES), (False, ("valid", "boot-tail", "fragmented", "cooked-2048")))
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
                    if case in CHECKSUM_CASES and not any(word in case for word in (
                            "read-fail", "short-read", "cancel-", "bad-")):
                        counters = re.search(r"Checksum direct reads PASS: IP (\d+) reads, exact 66537-byte boot (\d+) reads; "
                                             r"max FatFs read (\d+) bytes", output)
                        assert counters, "missing actual FatFs checksum counters"
                        ip_calls, boot_calls, largest = map(int, counters.groups())
                        assert ip_calls == 16 and largest in (2048, 2336, 2352, 2448)
                        assert boot_calls == (0 if case.endswith("gdi-raw") else 33)
                        print(f"Checksum I/O {kind} {case}: IP={ip_calls}, boot={boot_calls}, max={largest} bytes", flush=True)
                    if case in SUCCESS_CASES or (case in FORMAT_CASES and not any(
                            word in case for word in ("bad", "read-fail", "short-read", "cancel-"))):
                        assert "full IP CRC, exact boot bytes and headers" in output
                        check_fs(image, base / "check-volume.img", kind, partitioned)
                    if case == "cdda-warning":
                        assert "CD audio playback is unsupported" in output
                    if case in ("async-on-sci", "async-cooked-2048"):
                        assert "Retail boot reader: background SCI stream" in output
                    if case == "async-on-scif":
                        assert "background reader needs SCI microSD; using the standard reader" in output
                    if case == "windows-ce":
                        assert "Windows CE game launching is not supported" in output
                    if case in ("ce-probe", "ce-probe-cooked"):
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
                    if case == "bad-cooked-size":
                        assert "Invalid track length" in output
                    if case in ("cooked-2048", "cooked-boot-tail", "async-cooked-2048", "ce-probe-cooked"):
                        assert "checking exact executable CRC before detached launch" in output
                        assert "Cooked data RAW requests refused before IO; exact boot CRC" in output
                    if case in MANY_TRACKS:
                        unmapped = "audio tracks listed without their files" in output
                        assert unmapped == (case in AUDIO_UNMAPPED), case
                        assert ("Retail boot reader: background SCI stream" in output) == \
                            case.startswith("async-"), case
                    if case in ("boot-low-density", "boot-overlap-ip"):
                        assert "boot executable and full IP must be in the selected data session" in output
                    image.unlink()
                    print(f"PASS {kind} {layout} retail preparation: {case}; whole-card SHA256 unchanged", flush=True)


if __name__ == "__main__":
    main()
