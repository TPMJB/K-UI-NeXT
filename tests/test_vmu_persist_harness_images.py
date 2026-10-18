#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual dedicated preparation program on disposable FAT32/exFAT images."""
from pathlib import Path
import hashlib
import os
import re
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
SEED = ROOT / "build/vmu-harness/host-sequence"
PREP = ROOT / "build/vmu-persist-prep/host-sequence"


def run(*arguments):
    result = subprocess.run([str(value) for value in arguments], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    if result.returncode:
        raise RuntimeError(f"{arguments}: exit {result.returncode}\n{result.stdout}")
    return result.stdout


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def read_at(image, offset, count):
    with image.open("rb") as stream:
        stream.seek(offset)
        data = stream.read(count)
    assert len(data) == count
    return data


def change_at(image, offset, data):
    with image.open("r+b") as stream:
        stream.seek(offset)
        stream.write(data)


def mapped_bytes(image, wire, role, allocated=False):
    record = 512 + role * 448
    count = struct.unpack_from("<I", wire, record + (44 if allocated else 40))[0]
    table = record + (256 if allocated else 64)
    result = bytearray()
    ranges = []
    previous_end = None
    for index in range(count):
        logical, lba, blocks = struct.unpack_from("<III", wire, table + index * 12)
        assert logical == len(result) // 512 and blocks and lba != previous_end
        result.extend(read_at(image, lba * 512, blocks * 512))
        ranges.append((lba, lba + blocks))
        previous_end = lba + blocks
    assert not any(wire[table + count * 12:table + 192])
    return bytes(result), ranges


def exported(image, output, partition_first=0):
    match = re.search(r"^PERSIST-PATH (.+)\nPERSIST-WIRE ([0-9a-f]+)$", output, re.MULTILINE)
    assert match, "preparation did not report exact persisted bytes"
    path, wire = match[1], bytes.fromhex(match[2])
    assert len(wire) == 4096 and wire[:8] == b"KUIVMUP2"
    assert struct.unpack_from("<II", wire, 8) == (2, 4096)
    assert struct.unpack_from("<I", wire, 16)[0] == zlib.crc32(wire[:16] + bytes(4) + wire[20:])
    assert struct.unpack_from("<III", wire, 20) == (1, 1, 1)
    capacity, first, end = struct.unpack_from("<QQQ", wire, 32)
    assert capacity == image.stat().st_size // 512 and first == partition_first and first < end <= capacity
    assert wire[56:72] == bytes.fromhex("123456789abcdef013579bdf2468ace0")
    assert wire[72:84] == b"KUI-VMU-PB01"
    number = struct.unpack_from("<I", wire, 84)[0]
    length = struct.unpack_from("<I", wire, 184)[0]
    expected_path = f"/KUI/vmu-test/p{number:04d}".encode()
    assert length == len(expected_path) and wire[192:192 + length] == expected_path
    assert not any(wire[192 + length:288]) and f"/p{number:04d}/" in path
    fs_type, sector_bytes, cluster_sectors = struct.unpack_from("<III", wire, 288)
    assert fs_type in (1, 2) and sector_bytes == 512 and cluster_sectors and not cluster_sectors & (cluster_sectors - 1)
    contents, allocations = [], []
    for role, file_bytes in enumerate((131072, 1024, 16)):
        record = 512 + role * 448
        assert struct.unpack_from("<Q", wire, record)[0] == file_bytes
        assert struct.unpack_from("<Q", wire, record + 48)[0] == file_bytes
        data, useful = mapped_bytes(image, wire, role)
        whole, ranges = mapped_bytes(image, wire, role, allocated=True)
        expected_sectors = ((file_bytes + cluster_sectors * 512 - 1) // (cluster_sectors * 512)) * cluster_sectors
        assert len(data) == ((file_bytes + 511) // 512) * 512 and len(whole) == expected_sectors * 512
        for lba, stop in useful + ranges:
            assert first <= lba < stop <= end
        for lba, stop in ranges:
            assert all(stop <= other_first or other_end <= lba for other_first, other_end in allocations)
        allocations.extend(ranges)
        assert data == whole[:len(data)]
        contents.append(data[:file_bytes])
    base, journal, identity = contents
    assert identity == wire[72:88] and not any(journal[512:])
    assert hashlib.sha256(base).digest() == wire[88:120]
    assert hashlib.sha256(journal).digest() == wire[120:152]
    assert hashlib.sha256(identity).digest() == wire[152:184]
    assert hashlib.sha256(bytes(512)).digest() == wire[384:416]
    assert struct.unpack_from("<Q", wire, 2880)[0] == 0
    for block in range(256):
        assert struct.unpack_from("<I", wire, 1856 + block * 4)[0] == zlib.crc32(base[block * 512:(block + 1) * 512])
    assert base[255 * 512:255 * 512 + 16] == b"\x55" * 16
    return path, wire, tuple(contents)


def logical_lba(wire, role, block):
    record = 512 + role * 448
    count = struct.unpack_from("<I", wire, record + 40)[0]
    for index in range(count):
        logical, lba, blocks = struct.unpack_from("<III", wire, record + 64 + index * 12)
        if logical <= block < logical + blocks:
            return lba + block - logical
    raise AssertionError("logical block missing")


def synthetic_marker(wire, journal_payload):
    identity = wire[72:88]
    mapping = zlib.crc32(identity + struct.pack("<QII", 258, 1, 1) +
                        struct.pack("<QI", 0, 256) + struct.pack("<QI", 256, 2))
    marker = bytearray(512)
    marker[:8] = b"KUIVMUJ1"
    struct.pack_into("<II", marker, 8, 1, 512)
    marker[16:32] = identity
    struct.pack_into("<QIIIIQ", marker, 32, 258, mapping, 253,
                     zlib.crc32(journal_payload), 1, 1)
    struct.pack_into("<I", marker, 508, zlib.crc32(marker[:508]))
    return marker


def crosslink_journal(image, wire):
    record = 512 + 448
    first_cluster = struct.unpack_from("<I", wire, 512 + 8)[0]
    entry_offset = struct.unpack_from("<I", wire, record + 20)[0]
    entry_lba = struct.unpack_from("<Q", wire, record + 24)[0]
    count = struct.unpack_from("<I", wire, record + 32)[0]
    offset = entry_lba * 512 + entry_offset
    if struct.unpack_from("<I", wire, 288)[0] == 1:
        change_at(image, offset + 20, struct.pack("<H", first_cluster >> 16))
        change_at(image, offset + 26, struct.pack("<H", first_cluster & 0xffff))
    else:
        assert entry_offset + count * 32 <= 512, "fixture entry set crosses sector"
        entries = bytearray(read_at(image, offset, count * 32))
        assert entries[0] == 0x85 and entries[32] == 0xc0
        struct.pack_into("<I", entries, 32 + 20, first_cluster)
        checksum = 0
        for index, value in enumerate(entries):
            if index not in (2, 3):
                checksum = (((checksum & 1) << 15) + (checksum >> 1) + value) & 0xffff
        struct.pack_into("<H", entries, 2, checksum)
        change_at(image, offset, entries)


def partition_boot_geometry(image, first, kind):
    """Turn the wrapped superfloppy into a coherent actual partition VBR."""
    if kind == "fat32":
        boot = bytearray(read_at(image, first * 512, 512))
        backup = struct.unpack_from("<H", boot, 50)[0]
        struct.pack_into("<I", boot, 28, first)
        change_at(image, first * 512, boot)
        if backup and backup != 0xffff:
            duplicate = bytearray(read_at(image, (first + backup) * 512, 512))
            struct.pack_into("<I", duplicate, 28, first)
            change_at(image, (first + backup) * 512, duplicate)
    else:
        for start in (first, first + 12):
            boot = bytearray(read_at(image, start * 512, 11 * 512))
            struct.pack_into("<Q", boot, 64, first)
            checksum = 0
            for index, value in enumerate(boot):
                if index not in (106, 107, 112):
                    checksum = (((checksum & 1) << 31) + (checksum >> 1) + value) & 0xffffffff
            change_at(image, start * 512, boot)
            change_at(image, (start + 11) * 512, struct.pack("<I", checksum) * 128)


def exercise(image, partition_first=0):
    path1, wire1, files1 = exported(image, run(PREP, image, "export-persist"), partition_first)
    path2, wire2, files2 = exported(image, run(PREP, image, "export-persist"), partition_first)
    assert path1.endswith("retail-persistent-profile-0001.bin")
    assert path2.endswith("retail-persistent-profile-0002.bin") and path1 != path2
    assert wire1 == wire2 and files1 == files2
    for mode in ("prep-refuse-create", "prep-refuse-verify", "refuse-persist-scif",
                 "refuse-persist-raw", "refuse-persist-identity"):
        before = digest(image)
        run(PREP, image, mode)
        assert digest(image) == before, f"{mode} wrote storage"
    for mode in ("refuse-persist-final-raw", "refuse-persist-final-identity"):
        run(PREP, image, mode)  # Native host assertions compare all three source files.
        assert tuple(mapped_bytes(image, wire2, role)[0][:len(files2[role])]
                     for role in range(3)) == files2
    return wire2, files2


def main():
    for name in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat"):
        if not shutil.which(name):
            raise SystemExit(f"Missing test prerequisite: {name}")
    with tempfile.TemporaryDirectory(prefix="kui-vmu-preparation-") as temporary:
        base = Path(temporary)
        for kind in ("fat32", "exfat"):
            image = base / f"{kind}.img"
            with image.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", image) if kind == "fat32" else run("mkfs.exfat", image)
            run(SEED, image, "create")
            run(SEED, image, "repeat")
            # Default R1 export remains available in its original binary.
            assert "PROFILE-WIRE" in run(SEED, image, "export")
            wire, files = exercise(image)
            # A valid named save need not be the synthetic KUIPROOF fixture.
            change_at(image, logical_lba(wire, 0, 253) * 512 + 4, b"USER SAVE   ")
            _, named_wire, named_files = exported(image, run(PREP, image, "export-persist"))
            assert named_files[0] != files[0] and named_wire[88:120] != wire[88:120]
            checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"
            run(checker, "-n", image)
            for refusal in ("dirty-marker", "valid-old-marker", "bad-identity", "bad-vmu", "crosslink"):
                bad = base / f"{kind}-{refusal}.img"
                shutil.copyfile(image, bad)
                if refusal == "dirty-marker":
                    change_at(bad, logical_lba(named_wire, 1, 1) * 512 + 71, b"\x01")
                elif refusal == "valid-old-marker":
                    change_at(bad, logical_lba(named_wire, 1, 1) * 512,
                              synthetic_marker(named_wire, named_files[1][:512]))
                elif refusal == "bad-identity":
                    change_at(bad, logical_lba(named_wire, 2, 0) * 512, b"\xff")
                elif refusal == "bad-vmu":
                    change_at(bad, logical_lba(named_wire, 0, 255) * 512 + 70, b"\xff\xff")
                else:
                    crosslink_journal(bad, named_wire)
                before = digest(bad)
                run(PREP, bad, "refuse-persist")
                assert digest(bad) == before, f"{refusal} was changed/recovered"
            # Existing files inside a nonzero partition need absolute LBAs.
            partitioned = base / f"{kind}-partitioned.img"
            first = 2048
            mbr = bytearray(512)
            mbr[450] = 0x0c if kind == "fat32" else 0x07
            struct.pack_into("<II", mbr, 454, first, image.stat().st_size // 512)
            mbr[510:512] = b"\x55\xaa"
            with partitioned.open("wb") as out, image.open("rb") as source:
                out.write(mbr)
                out.seek(first * 512)
                shutil.copyfileobj(source, out)
            partition_boot_geometry(partitioned, first, kind)
            # New profile numbers continue, and old absolute-map files are
            # unrelated namespace entries rather than ownership authority.
            exported(partitioned, run(PREP, partitioned, "export-persist"), first)
            print(f"PASS {kind}: P2 image/journal/identity maps+hashes, whole-cluster ownership, generic VMU, unchanged sources, disabled recovery, unique outputs and final fresh checks", flush=True)


if __name__ == "__main__":
    main()
