#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise read-only CDDA storage against independently formatted exFAT images.

Requires a host C compiler, FatFs R0.16 sources, and exfatprogs mkfs.exfat.
Example: python3 tools/test_cdda_storage.py --fatfs-source .deps/fatfs/source
Use --mkfs-exfat /absolute/path/mkfs.exfat when it is not on PATH.
This creates only synthetic tone audio and disposable images; no game files.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from cdda_fixture import make_fixture

ROOT = Path(__file__).resolve().parents[1]
READONLY = {
    "FF_FS_READONLY": "1", "FF_FS_MINIMIZE": "2", "FF_USE_FASTSEEK": "0",
    "FF_USE_EXPAND": "0", "FF_USE_CHMOD": "0", "FF_FS_NORTC": "1",
    "FF_FS_CRTIME": "0",
}


def run(command, quiet=False):
    subprocess.run([str(value) for value in command], check=True,
                   stdout=subprocess.DEVNULL if quiet else None)


def private_fatfs(source, destination, writable=False):
    destination.mkdir()
    for name in ("ff.c", "ff.h", "ffunicode.c", "diskio.h"):
        shutil.copy2(source / name, destination / name)
    values = dict(READONLY)
    if writable:
        values.update(FF_FS_READONLY="0", FF_FS_MINIMIZE="0")
    config = (ROOT / "config/ffconf.h").read_text()
    config = re.sub(r"(?m)^#define (" + "|".join(values) + r")\s+[^\n]*",
                    lambda match: "#define " + match[1] + " " + values[match[1]], config)
    (destination / "ffconf.h").write_text(config)


def compile_test(compiler, fatfs, output, writable=False):
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-function", "-I" + str(fatfs),
               "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src/loader")]
    if writable:
        command.append("-DKUI_CDDA_TEST_POPULATE=1")
    command.append(ROOT / "tests/test_cdda_storage.c")
    if not writable:
        command.extend(ROOT / path for path in (
            "src/loader/cdda_storage.c", "src/core/boot_volume.c", "src/core/data.c"))
    command.extend((fatfs / "ff.c", fatfs / "ffunicode.c", "-o", output))
    run(command)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            h.update(block)
    return h.digest()


def verify(reader, image, raw, expected_largest=1):
    before = digest(image)
    run((reader, image, raw, expected_largest))
    if digest(image) != before:
        raise RuntimeError("Read-only integration changed its disk image")


def exercise(args, directory):
    readonly, writable = directory / "readonly", directory / "writable"
    private_fatfs(args.fatfs_source, readonly)
    private_fatfs(args.fatfs_source, writable, writable=True)
    writer, reader = directory / "populate", directory / "read-storage"
    compile_test(args.cc, writable, writer, writable=True)
    compile_test(args.cc, readonly, reader)
    raw = directory / "stereo.raw"
    raw.write_bytes(make_fixture())
    for name, cluster in (("exfat", 4096), ("exfat-large-clusters", 131072)):
        image = directory / (name + ".img")
        with image.open("wb") as stream:
            stream.truncate(64 * 1024 * 1024)
        run((args.mkfs_exfat, "-c", cluster, image), quiet=True)
        run((writer, image, raw))
        verify(reader, image, raw, 128 if cluster == 131072 else 1)
        if cluster == 4096:
            partitioned = directory / "exfat-mbr.img"
            header = bytearray(2048 * 512)
            header[450] = 0x07
            struct.pack_into("<II", header, 454, 2048, image.stat().st_size // 512)
            header[510:512] = b"\x55\xaa"
            with partitioned.open("wb") as output, image.open("rb") as source:
                output.write(header)
                shutil.copyfileobj(source, output)
            verify(reader, partitioned, raw)
    print("PASS CDDA storage integration: all three exFAT layouts, image hashes unchanged")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fatfs-source", type=Path, default=ROOT / ".deps/fatfs/source")
    parser.add_argument("--mkfs-exfat", default=shutil.which("mkfs.exfat"))
    parser.add_argument("--cc", default=shutil.which("cc") or "cc")
    args = parser.parse_args()
    args.fatfs_source = args.fatfs_source.resolve()
    if not args.mkfs_exfat:
        parser.error("mkfs.exfat is required; supply --mkfs-exfat or install exfatprogs")
    if not all((args.fatfs_source / name).is_file() for name in
               ("ff.c", "ff.h", "ffunicode.c", "diskio.h")):
        parser.error("--fatfs-source must contain FatFs R0.16 source files")
    with tempfile.TemporaryDirectory(prefix="kui-cdda-storage-") as temporary:
        exercise(args, Path(temporary))


if __name__ == "__main__":
    main()
