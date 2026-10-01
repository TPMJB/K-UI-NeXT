#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Boot a separate FAT partition without touching damaged ext4 data."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from runtime_package import envelope

PROFILE = "none,has_journal,ext_attr,resize_inode,dir_index,filetype,extent,64bit,flex_bg,sparse_super,large_file,huge_file,dir_nlink,extra_isize,metadata_csum"
BINARY = ROOT / "build/boot-recovery"


def run(*args):
    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    result = subprocess.run([str(x) for x in args], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise AssertionError(f"{args}: exit {result.returncode}\n{result.stdout}")
    return result.stdout


def package(recovery=False, marker=True):
    data = bytearray([0x72 if recovery else 0x61]) * 65536
    if marker:
        struct.pack_into("<5I", data, 32, 0x5349554B, 0x544F4F42, 1, 3, 0xFFFFFFFC)
    return envelope(data, len(data), "222222222222" if recovery else "111111111111")


def partitioned(path, fat, ext, gpt=False):
    first = 2048
    fat_count = fat.stat().st_size // 512
    ext_first = first + fat_count + 2048
    ext_count = ext.stat().st_size // 512
    total = ext_first + ext_count + 2048
    mbr = bytearray(512)
    mbr[510:512] = b"\x55\xaa"
    if gpt:
        mbr[450] = 0xEE
        struct.pack_into("<II", mbr, 454, 1, total - 1)
    else:
        mbr[450] = 0x0C
        struct.pack_into("<II", mbr, 454, first, fat_count)
        mbr[466] = 0x83
        struct.pack_into("<II", mbr, 470, ext_first, ext_count)
    with path.open("wb") as f:
        f.truncate(total * 512)
        f.write(mbr)
        for at, source in ((first, fat), (ext_first, ext)):
            f.seek(at * 512)
            with source.open("rb") as incoming:
                shutil.copyfileobj(incoming, f)
        if gpt:
            entries = bytearray(128 * 128)
            # EFI System Partition and Linux filesystem partition GUIDs.
            for n, kind, start, count in (
                (0, "28732ac11ff8d211ba4b00a0c93ec93b", first, fat_count),
                (1, "af3dc60f838472478e793d69d8477de4", ext_first, ext_count),
            ):
                at = n * 128
                entries[at:at + 16] = bytes.fromhex(kind)
                entries[at + 16:at + 32] = bytes([n + 1]) * 16
                struct.pack_into("<QQ", entries, at + 32, start, start + count - 1)

            def header(current, other, table):
                h = bytearray(512)
                h[:8] = b"EFI PART"
                struct.pack_into("<II", h, 8, 0x10000, 92)
                struct.pack_into("<QQQQ", h, 24, current, other, 34, total - 34)
                h[56:72] = bytes(range(16))
                struct.pack_into("<QIII", h, 72, table, 128, 128, zlib.crc32(entries))
                struct.pack_into("<I", h, 16, zlib.crc32(h[:92]))
                return h

            f.seek(512)
            f.write(header(1, total - 1, 2))
            f.write(entries)
            f.seek((total - 33) * 512)
            f.write(entries)
            f.write(header(total - 1, 1, total - 33))
    return ext_first, ext_count


def main():
    for tool in ("mkfs.fat", "mkfs.ext4"):
        if not shutil.which(tool):
            raise SystemExit(f"{tool} is required for boot recovery fixtures")
    with tempfile.TemporaryDirectory(prefix="kui-boot-recovery-") as temporary:
        root = Path(temporary)
        clean = root / "fat-clean.img"
        with clean.open("wb") as f:
            f.truncate(96 * 1024 * 1024)
        run("mkfs.fat", "-F", "32", clean)
        ext = root / "dirty-ext4.img"
        with ext.open("wb") as f:
            f.truncate(32 * 1024 * 1024)
        run("mkfs.ext4", "-q", "-F", "-b", "4096", "-I", "256", "-O", PROFILE, ext)
        with ext.open("r+b") as f:
            f.seek(1024 + 58)
            f.write(b"\0\0")  # Must not be inspected during independent FAT boot.
        good, rescue = root / "runtime.kui", root / "recovery.kui"
        good.write_bytes(package())
        rescue.write_bytes(package(True))
        corrupt = root / "corrupt.kui"
        corrupt.write_bytes(package()[:-1] + b"\xff")
        no_marker = root / "no-marker.kui"
        no_marker.write_bytes(package(marker=False))

        def fat_image(name, primary=good, recovery=rescue):
            image = root / (name + ".img")
            shutil.copyfile(clean, image)
            run(BINARY, image, "seed", primary or "-", recovery or "-")
            return image

        checks = 0

        def check(image, expected, mode="normal", transport=1, blocked=(0, 0), fault="none"):
            nonlocal checks
            run(BINARY, image, expected, mode, transport, *blocked, fault, 2)
            checks += 1

        valid = fat_image("valid")
        # Existing whole-device FAT remains bootable; both packages use the
        # selected transport and independently validated original envelopes.
        for transport in range(3):
            check(valid, "runtime", transport=transport)
        check(valid, "recovery", "recovery")
        check(valid, "cancel", fault="cancel")
        check(valid, "fail", fault="io")
        check(valid, "recovery", fault="io-once")
        for name, primary, recovery, expected in (
            ("bad-primary", corrupt, rescue, "recovery"),
            ("missing-primary", None, rescue, "recovery"),
            ("missing-marker", no_marker, rescue, "recovery"),
            ("both-bad", corrupt, corrupt, "fail"),
        ):
            fat = fat_image(name, primary, recovery)
            check(fat, expected)
            for gpt in (False, True):
                image = root / (name + ("-gpt.img" if gpt else "-mbr.img"))
                blocked = partitioned(image, fat, ext, gpt)
                check(image, expected, blocked=blocked)
        for gpt in (False, True):
            image = root / ("valid-gpt.img" if gpt else "valid-mbr.img")
            blocked = partitioned(image, valid, ext, gpt)
            check(image, "runtime", blocked=blocked)
            check(image, "recovery", "recovery", transport=2, blocked=blocked)
        only_normal = fat_image("no-recovery", good, None)
        check(only_normal, "fail", "recovery")  # X never substitutes normal runtime.
        check(fat_image("bad-recovery", good, corrupt), "fail", "recovery")

        # A damaged FAT boot filesystem must not redirect into the ext4 data
        # filesystem; the media harness rejects even a read of that partition.
        bad_fat = root / "bad-fat.img"
        shutil.copyfile(valid, bad_fat)
        with bad_fat.open("r+b") as f:
            f.write(bytes(512))
        image = root / "bad-fat-split.img"
        blocked = partitioned(image, bad_fat, ext)
        check(image, "fail", blocked=blocked)

        # Direct ext4 remains a supported clean-volume option, including its
        # recovery filename; it cannot solve recovery of its own dirty mount.
        tree = root / "tree"
        (tree / "KUI").mkdir(parents=True)
        shutil.copyfile(good, tree / "KUI/runtime.kui")
        shutil.copyfile(rescue, tree / "KUI/recovery.kui")
        direct = root / "direct-ext4.img"
        with direct.open("wb") as f:
            f.truncate(32 * 1024 * 1024)
        run("mkfs.ext4", "-q", "-F", "-b", "4096", "-I", "256", "-O", PROFILE, "-d", tree, direct)
        check(direct, "runtime")
        check(direct, "recovery", "recovery")
        print(f"PASS {checks} boot recovery scenarios: real FAT/ext4, MBR/GPT, fallback, forced recovery, cancellation, isolation and zero writes")


if __name__ == "__main__":
    main()
