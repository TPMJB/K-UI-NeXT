#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Strict saved-salvage admission on actual FAT32/exFAT fault images."""
from pathlib import Path
import argparse
import hashlib
import shutil
import struct
import tempfile
import zlib

from make_recovery_vectors import sector, mode2_sector
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build/salvage-revalidate-image"
RAW = 2352
CASES = (
    "clean", "cd-clean", "clean-zero-repairs", "zero-missing-patches", "no-guard", "part-only", "torn-part",
    "header-crc", "header-plan", "header-reserved", "header-anchor",
    "journal-crc", "journal-sequence", "journal-state", "journal-candidate",
    "journal-torn", "not-ready", "pending-candidate", "baseline-forged", "chunk-forged",
    "patch-crc", "patch-coordinate", "patch-identity", "patch-reserved",
    "patch-backup", "patch-tail", "patch-missing", "forged-invalid-data",
    "track-outside-target", "track-repaired", "track-unresolved", "track-short", "track-long", "track-missing",
    "foreign-guard", "foreign-part", "guard-crc",
    "header-repeat", "journal-repeat", "patch-repeat", "track-repeat", "no-guard-track-repeat", "no-guard-track-repeat-mark-fail",
    "read-fail", "short-read", "open-fail", "close-fail", "stat-fail", "mount-fail",
    "index-write-fail", "index-short-write", "index-read-fail", "index-close-fail", "index-unlink-fail",
    "guard-unlink-fail", "guard-sync-fail", "final-unmount-fail", "cancel-before", "cancel-journal", "cancel-candidates", "cancel-tracks", "cancel-final",
)


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def finish(data):
    struct.pack_into("<I", data, len(data)-4, zlib.crc32(data[:-4]))
    return data


def fixture(directory, cd=False):
    directory.mkdir()
    plan = [(1, 4, 0, 150, 166, 166 if cd else 600),
            (2, 0, 0, 166 if cd else 600, 170 if cd else 604, 170 if cd else 1825),
            (3, 4, 0 if cd else 1, 170 if cd else 45150, 1194 if cd else 46174, 1194 if cd else 549300)]
    data_tracks = []
    for i, (_, control, _, start, end, _) in enumerate(plan):
        if not control:
            data_tracks.append(bytearray((i*31+7) & 255 for i in range((end-start)*RAW)))
        else:
            encode = mode2_sector if cd and i == 2 else sector
            data_tracks.append(bytearray().join(bytes(RAW) if i == 2 and 10 <= n < 1010
                                               else encode(start+n, n+i*17)
                                               for n in range(end-start)))
    original = [bytes(data) for data in data_tracks]
    header = bytearray(4096)
    header[:8] = b"KUISALV3" if cd else b"KUISALV2"
    struct.pack_into("<4I", header, 8, 3 if cd else 2, 4096, 3, 1)
    anchor = plan[2][3]
    struct.pack_into("<2I", header, 24, anchor, anchor+1)
    for i in range(2):
        header[32+i*32:64+i*32] = hashlib.sha256(original[2][i*RAW:(i+1)*RAW]).digest()
    for i, track in enumerate(plan):
        struct.pack_into("<6I", header, 96+i*24, *track)
    if cd:
        struct.pack_into("<I", header, 4064, 1)
    finish(header)
    identity = hashlib.sha256(header).digest()
    events = []

    def event(kind, track=0, at=0, count=0, crc=0):
        data = bytearray(512)
        data[:8] = b"KUISJNL1"
        struct.pack_into("<Q5I", data, 8, len(events)+1, kind, track, at, count, crc)
        data[40:72] = identity
        events.append(finish(data))

    for i, track in enumerate(plan):
        crc = 0
        for at in range(0, track[4]-track[3], 32):
            count = min(32, track[4]-track[3]-at)
            if i == 2:
                for target in range(max(at, 10), min(at+count, 1010)):
                    event(1, i, target)
            crc = zlib.crc32(original[i][at*RAW:(at+count)*RAW], crc)
            event(2, i, at, count, crc)
        event(3, i, crc=crc)
    event(4, count=1000)
    patches = []
    for slot in range(173):
        at = slot+10
        patch = bytearray(5120)
        patch[:8] = b"KUISPAT2"
        struct.pack_into("<4I", patch, 8, slot, 2, at, 1)
        patch[32:64] = identity
        patch[2416:4768] = (mode2_sector if cd else sector)(anchor+at, slot+73)
        struct.pack_into("<2I", patch, 24, zlib.crc32(bytes(RAW)), zlib.crc32(patch[2416:4768]))
        finish(patch)
        patches.append(patch)
        event(7, slot, slot, crc=u32(patch, 28))
        event(5, slot, crc=u32(patch, 28))
        data_tracks[2][at*RAW:(at+1)*RAW] = patch[2416:4768]
    for name, data in zip(("track01.bin", "track02.raw", "track03.bin"), data_tracks):
        (directory/name).write_bytes(data)
    (directory/"header.bin").write_bytes(header)
    (directory/"journal.bin").write_bytes(b"".join(events))
    (directory/"patches.bin").write_bytes(b"".join(patches))
    (directory/"targets.cache").write_bytes(b"DO NOT TRUST OR REWRITE THIS DERIVED CACHE\x00\xff"*19)


def mutate(directory, case):
    def edit(name, callback, checksum=False):
        path = directory/name
        data = bytearray(path.read_bytes())
        callback(data)
        if checksum:
            finish(data)
        path.write_bytes(data)

    def event_edit(kind, callback, last=False):
        data = bytearray((directory/"journal.bin").read_bytes())
        offsets = [n for n in range(0, len(data), 512) if u32(data, n+16) == kind]
        at = offsets[-1 if last else 0]
        item = data[at:at+512]
        callback(item)
        data[at:at+512] = finish(item)
        (directory/"journal.bin").write_bytes(data)

    if case in ("clean-zero-repairs", "zero-missing-patches"):
        data = (directory/"journal.bin").read_bytes()
        at = next(n for n in range(0, len(data), 512) if u32(data, n+16) == 7)
        (directory/"journal.bin").write_bytes(data[:at])
        edit("track03.bin", lambda x: x.__setitem__(slice(10*RAW, 183*RAW), bytes(173*RAW)))
        if case == "clean-zero-repairs":
            (directory/"patches.bin").write_bytes(b"")
        else:
            (directory/"patches.bin").unlink()
    elif case == "header-crc":
        edit("header.bin", lambda x: x.__setitem__(100, x[100]^1))
    elif case == "header-plan":
        edit("header.bin", lambda x: struct.pack_into("<I", x, 96+4, 0), True)
    elif case == "header-reserved":
        edit("header.bin", lambda x: x.__setitem__(4000, 1), True)
    elif case == "header-anchor":
        # Rebind journal and patch identities so only exact saved-anchor binding
        # can reject this otherwise coherent immutable header.
        edit("header.bin", lambda x: x.__setitem__(32, x[32]^1), True)
        identity = hashlib.sha256((directory/"header.bin").read_bytes()).digest()
        for name, stride, offset in (("journal.bin", 512, 40), ("patches.bin", 5120, 32)):
            data = bytearray((directory/name).read_bytes())
            for at in range(0, len(data), stride):
                item = data[at:at+stride]
                item[offset:offset+32] = identity
                data[at:at+stride] = finish(item)
            (directory/name).write_bytes(data)
    elif case == "journal-crc":
        edit("journal.bin", lambda x: x.__setitem__(100, 1))
    elif case == "journal-sequence":
        event_edit(2, lambda x: struct.pack_into("<Q", x, 8, 999))
    elif case == "journal-state":
        event_edit(5, lambda x: struct.pack_into("<I", x, 24, 1))
    elif case == "journal-candidate":
        event_edit(7, lambda x: struct.pack_into("<I", x, 24, 50))
    elif case == "journal-torn":
        with (directory/"journal.bin").open("ab") as out:
            out.write(b"x")
    elif case == "not-ready":
        data = (directory/"journal.bin").read_bytes()
        at = next(n for n in range(0, len(data), 512) if u32(data, n+16) == 4)
        (directory/"journal.bin").write_bytes(data[:at])
        (directory/"patches.bin").write_bytes(b"")
    elif case == "pending-candidate":
        data = (directory/"journal.bin").read_bytes()
        assert u32(data, len(data)-512+16) == 5
        (directory/"journal.bin").write_bytes(data[:-512])
        edit("track03.bin", lambda x: x.__setitem__(slice(182*RAW, 183*RAW), bytes(RAW)))
    elif case == "baseline-forged":
        event_edit(2, lambda x: struct.pack_into("<I", x, 32, u32(x, 32)^1), last=True)
        event_edit(3, lambda x: struct.pack_into("<I", x, 32, u32(x, 32)^1), last=True)
    elif case == "chunk-forged":
        event_edit(2, lambda x: struct.pack_into("<I", x, 32, u32(x, 32)^1))
    elif case.startswith("patch-") and case not in ("patch-tail", "patch-missing", "patch-repeat"):
        data = bytearray((directory/"patches.bin").read_bytes())
        item = data[:5120]
        if case == "patch-crc":
            item[3000] ^= 1
        else:
            if case == "patch-coordinate":
                struct.pack_into("<I", item, 16, 11)
            elif case == "patch-identity":
                item[32] ^= 1
            elif case == "patch-reserved":
                item[5000] = 1
            elif case == "patch-backup":
                item[64] = 1
                struct.pack_into("<I", item, 24, zlib.crc32(item[64:2416]))
            else:
                return
            finish(item)
        data[:5120] = item
        (directory/"patches.bin").write_bytes(data)
    elif case == "patch-tail":
        with (directory/"patches.bin").open("ab") as out:
            out.write(b"x")
    elif case == "patch-missing":
        (directory/"patches.bin").unlink()
    elif case == "forged-invalid-data":
        data = bytearray((directory/"patches.bin").read_bytes())
        item = data[:5120]
        item[2416+2076] ^= 1  # wrong P parity, coherent outer checksums
        value = zlib.crc32(item[2416:4768])
        struct.pack_into("<I", item, 28, value)
        data[:5120] = finish(item)
        (directory/"patches.bin").write_bytes(data)
        event_edit(7, lambda x: struct.pack_into("<I", x, 32, value))
        event_edit(5, lambda x: struct.pack_into("<I", x, 32, value))
        edit("track03.bin", lambda x: x.__setitem__(slice(10*RAW, 11*RAW), item[2416:4768]))
    elif case == "track-outside-target":
        edit("track03.bin", lambda x: x.__setitem__(RAW*1023+17, x[RAW*1023+17]^1))
    elif case == "track-repaired":
        edit("track03.bin", lambda x: x.__setitem__(RAW*10+17, x[RAW*10+17]^1))
    elif case == "track-unresolved":
        edit("track03.bin", lambda x: x.__setitem__(RAW*1009+17, 1))
    elif case == "track-short":
        edit("track03.bin", lambda x: x.__delitem__(-1))
    elif case == "track-long":
        with (directory/"track03.bin").open("ab") as out:
            out.write(b"x")
    elif case == "track-missing":
        (directory/"track03.bin").unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=CASES, action="append", dest="cases")
    parser.add_argument("--filesystem", choices=("fat32", "exfat"), action="append", dest="filesystems")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kui-salvage-revalidate-") as temporary:
        base = Path(temporary)
        fixture(base/"gd")
        fixture(base/"cd", cd=True)
        for kind in args.filesystems or ("fat32", "exfat"):
            seed = base/(kind+"-seed.img")
            with seed.open("wb") as out:
                out.truncate(96*1024*1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", "-c", "131072", str(seed))
            for case in args.cases or CASES:
                directory = base/"current"
                shutil.copytree(base/("cd" if case == "cd-clean" else "gd"), directory)
                mutate(directory, case)
                image = base/(kind+"-"+case+".img")
                shutil.copyfile(seed, image)
                output = run(str(BINARY), str(image), str(directory), case)
                assert f"PASS salvage revalidate {case}; source files unchanged" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                shutil.rmtree(directory)
                print(f"PASS {kind} salvage revalidate: {case}", flush=True)


if __name__ == "__main__":
    main()
