#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual read-only physical VMU importer on disposable real FAT32/exFAT."""
from pathlib import Path
import hashlib
import re
import shutil
import struct
import sys
import tempfile
import zlib

import test_vmu_persist_harness_images as prep
import test_vmu_physical_proof_images as physical

ROOT = Path(__file__).resolve().parents[1]
IMPORT = ROOT / "build/vmu-import-prep/host-sequence"
sys.path.insert(0, str(ROOT / "tools"))
from vmu_persist_profile import decode_profile  # noqa: E402


def old_pair_bytes(image, wire):
    return tuple(prep.mapped_bytes(image, wire, role)[0] for role in range(3))


def verify_import(image, output, number, first, old_wire):
    source = re.search(r"^IMPORT-SOURCE-SHA ([0-9a-f]{64})$", output, re.MULTILINE)
    match = re.search(r"^IMPORT-PATH (.+)\nIMPORT-WIRE ([0-9a-f]+)$", output, re.MULTILINE)
    assert source and match, "host did not report independently reread profile/source"
    path, wire = match[1], bytes.fromhex(match[2])
    decode_profile(wire)
    assert path == f"0:/KUI/vmu-test/p{number:04d}/retail-persistent-profile-{number:04d}.bin"
    assert struct.unpack_from("<QQQ", wire, 32) == (
        image.stat().st_size // 512, first, image.stat().st_size // 512)
    pair_path = f"/KUI/vmu-test/p{number:04d}".encode()
    assert wire[192:192 + len(pair_path)] == pair_path
    assert wire[72:88] != old_wire[72:88], "new clone reused active pair identity"
    base, journal, identity = old_pair_bytes(image, wire)
    assert len(base) == 131072 and journal == bytes(1024) and identity[:16] == wire[72:88]
    assert hashlib.sha256(base).hexdigest() == source[1]
    assert hashlib.sha256(base).digest() == wire[88:120]
    assert hashlib.sha256(journal).digest() == wire[120:152]
    assert hashlib.sha256(identity[:16]).digest() == wire[152:184]
    assert hashlib.sha256(bytes(512)).digest() == wire[384:416]
    for block in range(256):
        assert struct.unpack_from("<I", wire, 1856 + block * 4)[0] == zlib.crc32(base[block * 512:(block + 1) * 512])
    # Preserve protected saves exactly; canonical FAT-free payload becomes
    # zero without physically requesting any free user block. The host checks
    # every allocated/metadata byte and every physical per-block read count.
    assert base[253 * 512 + 1] == 0xff
    assert base[253 * 512 + 4:253 * 512 + 15] == b"SOURCE_SAVE"
    assert base[57 * 512:58 * 512] == bytes(512)
    assert base[:198 * 512] == bytes(198 * 512)
    assert base[198 * 512:199 * 512] == bytes((i * 29 + 7) & 255 for i in range(512))
    assert base[199 * 512:200 * 512] == bytes((i * 17 + 31) & 255 for i in range(512))
    assert re.search(r"^READY: photograph and upload this new \.bin\.$", output, re.MULTILINE)
    return wire, base


def checker(image, kind, first, temporary):
    volume = temporary / f"check-{kind}.img"
    with image.open("rb") as source, volume.open("wb") as destination:
        source.seek(first * 512)
        shutil.copyfileobj(source, destination)
    prep.run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", volume)
    volume.unlink()


def active_refusals(image, kind, first, wire, original, temporary):
    """Never let FatFs allocate a live VMU cluster whose bit/chain was freed."""
    for corruption in ("missing", "crc", "map", "base-free", "journal-free", "identity-free"):
        bad = temporary / f"{kind}-active-{corruption}.img"
        shutil.copyfile(image, bad)
        if corruption == "missing":
            prep.run(IMPORT, bad, "hide-anchor")
        elif corruption in ("crc", "map"):
            replacement = bytearray(wire)
            if corruption == "crc":
                replacement[88] ^= 1
            else:
                replacement = physical.wrong_mapping(wire)
            offset = bad.read_bytes().find(wire)
            assert offset >= 0
            prep.change_at(bad, offset, replacement)
        else:
            role = {"base-free": 0, "journal-free": 1, "identity-free": 2}[corruption]
            cluster = struct.unpack_from("<I", wire, 512 + role * 448 + 8)[0]
            if kind == "exfat":
                assert struct.unpack_from("<I", wire, 512 + role * 448 + 12)[0] & 2
                bitmap_cluster = struct.unpack_from("<I", wire, 352)[0]
                spc = struct.unpack_from("<I", wire, 296)[0]
                heap = struct.unpack_from("<Q", wire, 328)[0]
                byte = (cluster - 2) // 8
                offset = (heap + (bitmap_cluster - 2) * spc) * 512 + byte
                original_bit = prep.read_at(bad, offset, 1)[0]
                assert original_bit & (1 << ((cluster - 2) % 8))
                prep.change_at(bad, offset, bytes([original_bit & ~(1 << ((cluster - 2) % 8))]))
            else:
                fat_first, fat_sectors = struct.unpack_from("<QQ", wire, 312)
                fat_count = struct.unpack_from("<I", wire, 304)[0]
                for copy in range(fat_count):
                    prep.change_at(bad, (fat_first + copy * fat_sectors) * 512 + cluster * 4, bytes(4))
        before = prep.digest(bad)
        output = prep.run(IMPORT, bad, "refuse-anchor")
        assert "READY:" not in output and "IMPORT-WIRE" not in output
        assert prep.digest(bad) == before, f"{corruption}: allocation started before anchor admission"
        assert old_pair_bytes(bad, wire) == original
        bad.unlink()


def exercise(image, kind, first, temporary):
    prep.run(prep.SEED, image, "create")
    _, old_wire, _ = prep.exported(image, prep.run(prep.PREP, image, "export-persist"), first)
    # Preserve an actual real-map redo record and post-preparation changed base,
    # matching the important lifecycle of the owner's already-working p0001.
    prep.run(physical.PROOF, image, "commit")
    original = old_pair_bytes(image, old_wire)
    assert original[1][512:520] == b"KUIVMUJ1"
    base = temporary / f"{kind}-seed.img"
    shutil.copyfile(image, base)
    active_refusals(base, kind, first, old_wire, original, temporary)
    before_guards = prep.digest(base)
    guard_output = prep.run(IMPORT, base, "refuse-source-guards")
    guard_cases = ("context-null", "output-null", "block-256", "missing", "replacement", "invalid",
                   "functions", "memcard", "name-first", "name-last", "context-device", "block-65535",
                   "combined-inputs", "combined-identity", "combined-missing")
    for case in guard_cases:
        assert re.search(rf"^PASS source guard {case} mask [0-9a-f]{{3}} enumerations 1 "
                         r"reads 0 writes 0 frozen report$", guard_output, re.MULTILINE)
    assert "READY:" not in guard_output and "IMPORT-WIRE" not in guard_output
    assert prep.digest(base) == before_guards, "pre-read guard diagnostics wrote storage"
    assert old_pair_bytes(base, old_wire) == original
    compare_output = prep.run(IMPORT, base, "refuse-source-mismatches")
    compare_cases = ("first-byte", "last-byte", "zero-to-ff", "ff-to-zero",
                     "compare-anomaly", "compare-false-equal", "raw-reply-difference",
                     "copy-output-corruption", "first-output-mutated")
    for case in compare_cases:
        assert re.search(rf"^PASS source compare {case} reads 2 SD 0 output unchanged frozen report$",
                         compare_output, re.MULTILINE)
    for case in ("same-zero", "same-ff", "raw-change-observation"):
        assert f"PASS source compare {case} reads 2 SD 0 output exact" in compare_output
    reports = re.findall(r"Reading all 256[^\n]+\n(.*?)\nPASS source compare ([^\n ]+)[^\n]+",
                         compare_output, re.DOTALL)
    assert len(reports) == len(compare_cases)
    for report, case in reports:
        rows = report.splitlines()
        assert len(rows) == 13 and rows[0].startswith("Capture refused:") and rows[-1].startswith("FAIL:")
        assert rows[1].startswith("READ block ") and rows[2].startswith("CLONE phase ")
        assert "PASS1 stage 10 result 0 type 8 words 130" in rows
        assert "PASS2 stage 10 result 0 type 8 words 130" in rows
        crc = re.search(r"^CRC32 first ([0-9a-f]{8}) second ([0-9a-f]{8})$", report, re.MULTILINE)
        assert crc
        block_crc = tuple(int(value, 16) for value in crc.groups())
        fingerprints = re.findall(r"^PASS([12]) RX ([0-9a-f]{8})/([0-9a-f]{8}) COPY ([0-9a-f]{8})$",
                                  report, re.MULTILINE)
        assert [item[0] for item in fingerprints] == ["1", "2"]
        for number, before, after, copied in fingerprints:
            expected_raw = block_crc[0] if case == "copy-output-corruption" else (
                block_crc[1] if case == "first-output-mutated" else block_crc[int(number) - 1])
            expected_copy = block_crc[1] if case == "first-output-mutated" else block_crc[int(number) - 1]
            assert int(before, 16) == int(after, 16) == expected_raw
            assert int(copied, 16) == expected_copy
        if case in ("zero-to-ff", "ff-to-zero"):
            first_byte, second_byte = (0, 255) if case == "zero-to-ff" else (255, 0)
            assert f"DIFF first 0 count 512 bytes {first_byte:02x}/{second_byte:02x}" in rows
            assert int(crc[1], 16) == zlib.crc32(bytes([first_byte]) * 512)
            assert int(crc[2], 16) == zlib.crc32(bytes([second_byte]) * 512)
        elif case == "compare-anomaly":
            assert "COMPARE anomaly memcmp 73" in rows and "DIFF first 512 count 0 bytes 00/00" in rows
            assert int(crc[1], 16) == int(crc[2], 16) == zlib.crc32(b"\x9b" * 512)
        elif case == "compare-false-equal":
            assert "COMPARE anomaly memcmp 0" in rows and "DIFF first 7 count 1 bytes 9b/9a" in rows
        elif case in ("raw-reply-difference", "copy-output-corruption", "first-output-mutated"):
            clean = b"\x9b" * 512
            changed = bytearray(clean)
            index = 12 if case == "first-output-mutated" else 18
            changed[index] ^= 1
            expected = (zlib.crc32(changed), zlib.crc32(clean)) if case == "first-output-mutated" else (
                zlib.crc32(clean), zlib.crc32(changed))
            assert block_crc == expected
            assert f"DIFF first {index} count 1 bytes {'9a/9b' if case == 'first-output-mutated' else '9b/9a'}" in rows
        else:
            index = 0 if case == "first-byte" else 511
            assert re.search(rf"^DIFF first {index} count 1 bytes [0-9a-f]{{2}}/[0-9a-f]{{2}}$", report, re.MULTILINE)
    assert "READY:" not in compare_output and "IMPORT-WIRE" not in compare_output
    assert prep.digest(base) == before_guards, "duplicate-read comparison diagnostics wrote storage"
    assert old_pair_bytes(base, old_wire) == original
    geometry_output = prep.run(IMPORT, base, "refuse-root-geometries")
    geometry_cases = ("magic", "custom", "fat-first", "fat-count", "dir-first", "dir-count", "user-count", "icon")
    reports = re.findall(r"Reading saves and metadata[^\n]+\n(.*?)\nPASS source geometry ([^\n ]+) reads 236 SD 0 frozen root report",
                         geometry_output, re.DOTALL)
    assert [case for _, case in reports] == list(geometry_cases)
    for report, case in reports:
        rows = report.splitlines()
        assert len(rows) == 7 and rows[0] == "Capture refused: status -5, image -4"
        assert rows[1] == "SNAP result -5 last 255 reads 118" and rows[-1].startswith("FAIL:")
        magic = ("00" + "55" * 15) if case == "magic" else "55" * 16
        assert rows[2] == f"ROOT magic {magic} custom {2 if case == 'custom' else 0}"
        layout = [254, 1, 253, 13, 200, 0]
        if case not in ("magic", "custom"):
            index, value = {"fat-first": (0, 253), "fat-count": (1, 2), "dir-first": (2, 252),
                            "dir-count": (3, 12), "user-count": (4, 199), "icon": (5, 124)}[case]
            layout[index] = value
        assert rows[3] == "ROOT FAT %u/%u DIR %u/%u USER %u ICON %u" % tuple(layout)
        assert rows[4] == "ROOT opaque 40-45 1234abcd5678"
        assert rows[5] == "ROOT opaque 52-57 fedcba987654"
        assert "READ block " not in report and "COMPARE " not in report
    assert "READY:" not in geometry_output and "IMPORT-WIRE" not in geometry_output
    assert prep.digest(base) == before_guards, "root geometry diagnostics wrote storage"
    assert old_pair_bytes(base, old_wire) == original
    for mode in ("refuse-before-preview", "refuse-guard-before-read", "refuse-guard-after-first-read",
                 "refuse-guard-after-second-read", "refuse-no-vmu", "refuse-malformed", "refuse-read",
                 "refuse-busy", "refuse-response-length",
                 "refuse-short", "refuse-oversize", "refuse-mismatch",
                 "refuse-source-change", "refuse-slot-change", "refuse-copy-read", "refuse-cid",
                 "refuse-anchor-raw", "refuse-raw", "refuse-final-raw", "refuse-release", "refuse-final-release",
                 "refuse-journal-payload", "refuse-final-journal-payload"):
        bad = temporary / f"{kind}-{mode}.img"
        shutil.copyfile(base, bad)
        before = prep.digest(bad)
        output = prep.run(IMPORT, bad, mode)
        assert "READY:" not in output and "IMPORT-WIRE" not in output
        if "journal-payload" in mode:
            assert "Imported journal must be exactly 1024 zero bytes." in output
        if mode.startswith("refuse-guard-"):
            guard_pass = {"refuse-guard-before-read": 0, "refuse-guard-after-first-read": 1,
                          "refuse-guard-after-second-read": 2}[mode]
            assert f"READ block 254 pass {guard_pass} reason {3 if guard_pass else 1}" in output
            assert "SNAP result -3 last 254 reads 1" in output
            assert "GUARD bits 008 slot A1" in output
            assert "MAPLE stage " not in output
        if mode in ("refuse-read", "refuse-busy", "refuse-response-length"):
            assert "READ block " in output and "MAPLE stage " in output
            assert "REPLY type " in output and "BLOCK expected " in output
        if mode == "refuse-mismatch":
            assert "READ block 254 pass 0 reason 5" in output and "DIFF first 7 count 1 bytes ff/fe" in output
            assert "CRC32 first " in output and "PASS1 stage 10 " in output and "PASS2 stage 10 " in output
            assert "PASS1 RX " in output and "PASS2 RX " in output
        if mode == "refuse-malformed":
            assert "Capture refused: status -5, image -4" in output
            assert "SNAP result -5 last 255 reads 118" in output
            assert "ROOT magic 00" + "55" * 15 + " custom 0" in output
            assert "ROOT FAT 254/1 DIR 253/13 USER 200 ICON 0" in output
            assert "READ block " not in output and "ROOT opaque 40-45 " in output and "ROOT opaque 52-57 " in output
        assert old_pair_bytes(bad, old_wire) == original, f"{mode} changed active pair"
        if mode in ("refuse-before-preview", "refuse-guard-before-read", "refuse-guard-after-first-read",
                    "refuse-guard-after-second-read", "refuse-no-vmu", "refuse-malformed", "refuse-read",
                    "refuse-short", "refuse-oversize", "refuse-mismatch",
                    "refuse-source-change", "refuse-slot-change", "refuse-anchor-raw", "refuse-cid", "refuse-release",
                    "refuse-busy", "refuse-response-length"):
            assert prep.digest(bad) == before, f"{mode} wrote storage before admission"
        if mode == "refuse-copy-read":
            # A partial p0002 is retained, never reused. A new process must
            # allocate p0003 and leave the active physical journal untouched.
            verify_import(bad, prep.run(IMPORT, bad, "import"), 3, first, old_wire)
            assert old_pair_bytes(bad, old_wire) == original
        checker(bad, kind, first, temporary)
        bad.unlink()
    first_wire, first_base = verify_import(image, prep.run(IMPORT, image, "import"), 2, first, old_wire)
    assert old_pair_bytes(image, old_wire) == original
    opaque_patterns = {"zero": (bytes(6), bytes(6)), "ff": (b"\xff" * 6, b"\xff" * 6),
                       "varied": (bytes.fromhex("1234abcd5678"), bytes.fromhex("fedcba987654"))}
    for name, (opaque40, opaque52) in opaque_patterns.items():
        varied = temporary / f"{kind}-root-opaque-{name}.img"
        shutil.copyfile(base, varied)
        _, varied_base = verify_import(varied, prep.run(IMPORT, varied, f"import-root-opaque-{name}"), 2, first, old_wire)
        expected = bytearray(first_base)
        expected[255 * 512 + 0x40:255 * 512 + 0x46] = opaque40
        expected[255 * 512 + 0x52:255 * 512 + 0x58] = opaque52
        assert varied_base == expected, "opaque root metadata or other source bytes changed during import"
        assert old_pair_bytes(varied, old_wire) == original
        checker(varied, kind, first, temporary)
        varied.unlink()
    second_wire, second_base = verify_import(image, prep.run(IMPORT, image, "import-slot7"), 3, first, old_wire)
    assert first_base == second_base and first_wire[72:88] != second_wire[72:88]
    assert old_pair_bytes(image, old_wire) == original
    assert prep.mapped_bytes(image, first_wire, 0)[0] == first_base
    # The active P2 still admits its original current allocations after sibling
    # directory growth; its retained physical record can still be reopened.
    prep.run(physical.PROOF, image, "check")
    assert old_pair_bytes(image, old_wire) == original
    checker(image, kind, first, temporary)
    skipped = temporary / f"{kind}-partial.img"
    shutil.copyfile(base, skipped)
    verify_import(skipped, prep.run(IMPORT, skipped, "skip-partial"), 3, first, old_wire)
    assert old_pair_bytes(skipped, old_wire) == original
    checker(skipped, kind, first, temporary)
    skipped.unlink();base.unlink()


def main():
    for command in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat"):
        if not shutil.which(command):
            raise SystemExit(f"Missing prerequisite: {command}")
    with tempfile.TemporaryDirectory(prefix="kui-vmu-import-") as directory:
        temporary = Path(directory)
        for kind in ("fat32", "exfat"):
            volume = temporary / f"{kind}-volume.img"
            with volume.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            prep.run("mkfs.fat", "-F", "32", volume) if kind == "fat32" else prep.run("mkfs.exfat", volume)
            image = temporary / f"{kind}-partitioned.img"
            first = 2048
            sector0 = bytearray(512);sector0[446 + 4] = 0x0c if kind == "fat32" else 7
            struct.pack_into("<II", sector0, 446 + 8, first, volume.stat().st_size // 512)
            sector0[510:512] = b"\x55\xaa"
            with image.open("wb") as destination, volume.open("rb") as source:
                destination.write(sector0);destination.seek(first * 512)
                shutil.copyfileobj(source, destination)
            volume.unlink();prep.partition_boot_geometry(image, first, kind)
            exercise(image, kind, first, temporary)
            print(f"PASS {kind}/MBR2048: canonical save snapshot, zero skipped free blocks, unique pairs, identity last, "
                  "fresh P2, active retained journal preserved, fault refusals and fsck", flush=True)


if __name__ == "__main__":
    main()
