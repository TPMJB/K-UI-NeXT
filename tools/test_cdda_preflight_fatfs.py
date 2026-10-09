#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check profile13 against actual upstream FatFs and independent exFAT parsing.

Creates only synthetic backings and disposable cards. Requires a host C
compiler, FatFs R0.16, and mkfs.exfat; --sanitize enables strict ASan/UBSan.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from test_cdda_preflight_integration import fixture

ROOT = Path(__file__).resolve().parents[1]
DESCRIPTOR_SHA = "96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803"


def run(command, quiet=False, env=None):
    subprocess.run([str(value) for value in command], check=True, env=env,
                   stdout=subprocess.DEVNULL if quiet else None, timeout=120)


def private_fatfs(source, destination, writable):
    destination.mkdir()
    for name in ("ff.c", "ff.h", "ffunicode.c", "diskio.h"):
        shutil.copy2(source / name, destination / name)
    values = {"FF_FS_READONLY": "0" if writable else "1", "FF_FS_MINIMIZE": "0",
              "FF_USE_FASTSEEK": "1", "FF_USE_EXPAND": "0", "FF_USE_CHMOD": "0",
              "FF_FS_NORTC": "1", "FF_FS_CRTIME": "0"}
    config = (ROOT / "config/ffconf.h").read_text()
    config = re.sub(r"(?m)^#define (" + "|".join(values) + r")\s+[^\n]*",
                    lambda match: "#define " + match[1] + " " + values[match[1]], config)
    (destination / "ffconf.h").write_text(config)


def compile_test(args, fatfs, output, writable):
    command = [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-function", "-ffunction-sections", "-fdata-sections",
               "-I" + str(fatfs), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "src/loader")]
    if writable:
        command.append("-DKUI_CDDA_TEST_POPULATE=1")
    else:
        command += ["-DCDDA_TEST_PROFILE=13", "-DKUI_RETAIL_FAST_IO=1"]
    command.append(ROOT / "tests/test_cdda_preflight_fatfs.c")
    if not writable:
        command += [ROOT / name for name in (
            "src/loader/cdda_storage.c", "src/loader/cdda_preflight_storage.c",
            "src/core/cdda_preflight.c", "src/core/cdda_disc.c", "src/core/game_image.c",
            "src/core/game_metadata.c", "src/core/retail_image.c", "src/core/hash.c",
            "src/core/boot_volume.c", "src/core/data.c")]
    command += [fatfs / "ff.c", fatfs / "ffunicode.c", "-Wl,--gc-sections"]
    if args.sanitize:
        command += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    run(command + ["-o", output])


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


class Exfat:
    """Small independent read-only exFAT allocation/directory parser."""
    def __init__(self, path):
        self.stream = path.open("rb")
        boot = self.read(0, 512)
        self.partition = 0
        if boot[3:11] != b"EXFAT   ":
            assert boot[510:512] == b"\x55\xaa"
            self.partition = struct.unpack_from("<I", boot, 454)[0]
            boot = self.read(self.partition * 512, 512)
        assert boot[3:11] == b"EXFAT   " and boot[108] == 9
        self.fat, self.heap, self.cluster_count, self.root = struct.unpack_from("<I4xIII", boot, 80)
        self.csize = 1 << boot[109]
        self.cluster_bytes = self.csize * 512
        self.files = {}
        self.walk(self.root, None, "/")

    def read(self, offset, size):
        self.stream.seek(offset)
        data = self.stream.read(size)
        assert len(data) == size
        return data

    def sector(self, lba, count=1):
        return self.read((self.partition + lba) * 512, count * 512)

    def chain(self, start, count=None, contiguous=False):
        clusters, seen = [], set()
        while count is None or len(clusters) < count:
            assert 2 <= start < self.cluster_count + 2 and start not in seen
            clusters.append(start)
            seen.add(start)
            if count is not None and len(clusters) == count:
                break
            if contiguous:
                start += 1
            else:
                entry = self.read((self.partition + self.fat) * 512 + start * 4, 4)
                start = struct.unpack("<I", entry)[0]
                if start >= 0xfffffff8:
                    assert count is None
                    break
        return clusters

    def walk(self, start, stream, folder):
        length, contiguous = stream if stream else (None, False)
        count = (length + self.cluster_bytes - 1) // self.cluster_bytes if length is not None else None
        clusters = self.chain(start, count, contiguous)
        data = b"".join(self.sector(self.heap + (cluster - 2) * self.csize, self.csize)
                        for cluster in clusters)
        position = 0
        while position < len(data):
            entry = data[position:position + 32]
            if entry[0] == 0:
                break
            if entry[0] != 0x85:
                position += 32
                continue
            secondary = entry[1]
            group = data[position + 32:position + 32 * (secondary + 1)]
            assert len(group) == secondary * 32 and group[0] == 0xc0
            flags, name_length = group[1], group[3]
            first = struct.unpack_from("<I", group, 20)[0]
            size = struct.unpack_from("<Q", group, 24)[0]
            encoded = b"".join(group[at + 2:at + 32] for at in range(32, len(group), 32)
                               if group[at] == 0xc1)
            name = encoded[:name_length * 2].decode("utf-16-le")
            attrs = struct.unpack_from("<H", entry, 4)[0]
            absolute = folder + name
            if attrs & 0x10:
                self.walk(first, (size, bool(flags & 2)), absolute + "/")
            else:
                self.files[absolute] = (first, size, bool(flags & 2))
            position += 32 * (secondary + 1)

    def expectations(self, output):
        rows = []
        fragmented = 0
        for number in range(1, 16):
            name = f"track{number:02d}.{'bin' if number in (1, 3, 15) else 'raw'}"
            first, size, contiguous = self.files["/Games/Nested/Original/" + name]
            count = (size + self.cluster_bytes - 1) // self.cluster_bytes
            clusters = self.chain(first, count, contiguous)
            runs = []
            for cluster in clusters:
                if runs and cluster == runs[-1][0] + runs[-1][1]:
                    runs[-1][1] += 1
                else:
                    runs.append([cluster, 1])
            fragmented += len(runs) > 1
            wire = struct.pack("<6I", size, 512, self.csize, self.heap, count, len(runs))
            before, run_rows = 0, []
            for start, length in runs:
                lba = self.heap + (start - 2) * self.csize
                wire += struct.pack("<3I", length, start, lba)
                run_rows.append(f"{start} {length} {lba} {before * self.csize} {length * self.csize}")
                before += length
            lba = self.heap + (first - 2) * self.csize
            rows.append(f"{size} {self.cluster_bytes} {count} {len(runs)} {2 * len(runs) + 2} "
                        f"{lba} {count * self.cluster_bytes} {hashlib.sha256(wire).hexdigest()}")
            rows += run_rows
        assert fragmented >= 1, "fixture must exercise a FAT-linked fragmented backing"
        output.write_text("\n".join(rows) + "\n")
        self.stream.close()


def verify(reader, image, inputs, expected, identities, mode, environment):
    before = digest(image)
    run([reader, image, inputs, expected, *identities, mode], env=environment)
    assert digest(image) == before, "read-only preflight changed card bytes"


def exercise(args, directory):
    inputs = directory / "fixture"
    inputs.mkdir()
    identities = fixture(inputs)
    assert len((inputs / "fixture.gdi").read_bytes()) == 451
    assert digest(inputs / "fixture.gdi").hex() == DESCRIPTOR_SHA
    readonly, writable = directory / "readonly", directory / "writable"
    private_fatfs(args.fatfs_source, readonly, False)
    private_fatfs(args.fatfs_source, writable, True)
    writer, reader = directory / "populate", directory / "read-preflight"
    compile_test(args, writable, writer, True)
    compile_test(args, readonly, reader, False)
    environment = os.environ.copy()
    if args.sanitize:
        environment.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=0")
        environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    cases = 0
    for cluster in (4096, 131072):
        image = directory / f"exfat-{cluster}.img"
        with image.open("wb") as stream:
            stream.truncate(64 * 1024 * 1024)
        run([args.mkfs_exfat, "-c", cluster, image], quiet=True)
        run([writer, image, inputs, "populate"], env=environment)
        expected = directory / f"allocation-{cluster}.txt"
        Exfat(image).expectations(expected)
        verify(reader, image, inputs, expected, identities, "normal", environment)
        cases += 1
        if cluster == 4096:
            partitioned = directory / "exfat-mbr.img"
            header = bytearray(2048 * 512)
            header[450] = 0x07
            struct.pack_into("<II", header, 454, 2048, image.stat().st_size // 512)
            header[510:512] = b"\x55\xaa"
            with partitioned.open("wb") as out, image.open("rb") as source:
                out.write(header)
                shutil.copyfileobj(source, out)
            Exfat(partitioned).expectations(expected)
            verify(reader, partitioned, inputs, expected, identities, "normal", environment)
            cases += 1
            for operation, mode in (("duplicate", "ambiguous"), ("config", "configured"),
                                    ("fixture-config", "fixture-config"), ("invalid-config", "invalid-config")):
                scenario = directory / f"{mode}.img"
                shutil.copyfile(image, scenario)
                run([writer, scenario, inputs, operation], env=environment)
                verify(reader, scenario, inputs, expected, identities, mode, environment)
                cases += 1
    print(f"PASS actual FatFs preflight: {cases} exFAT/layout/discovery scenarios; "
          "independent allocation runs and whole-card SHA256 unchanged")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fatfs-source", type=Path, default=ROOT / ".deps/fatfs/source")
    parser.add_argument("--mkfs-exfat", default=shutil.which("mkfs.exfat"))
    parser.add_argument("--cc", default=shutil.which("cc") or "cc")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    if not args.mkfs_exfat:
        parser.error("mkfs.exfat is required; supply --mkfs-exfat")
    if not all((args.fatfs_source / name).is_file() for name in ("ff.c", "ff.h", "ffunicode.c", "diskio.h")):
        parser.error("--fatfs-source must contain upstream FatFs R0.16")
    with tempfile.TemporaryDirectory(prefix="kui-cdda-preflight-fatfs-") as temporary:
        exercise(args, Path(temporary))


if __name__ == "__main__":
    main()
