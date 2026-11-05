#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production bounded ZIP staging on actual FAT32/exFAT under ASan/UBSan."""
import io
import os
from pathlib import Path
import random
import shlex
import struct
import tempfile
import zipfile
from test_games_library_images import fatfs_source, private_formatter
from test_update_images import run

ROOT = Path(__file__).resolve().parents[1]


class Stream(io.BytesIO):
    def seek(self, *args):
        raise io.UnsupportedOperation("non-seekable descriptor fixture")


def archive(payload, method=zipfile.ZIP_DEFLATED, descriptor=False, entries=None):
    stream = Stream() if descriptor else io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=method) as z:
        for name, data in entries or [("bundle/KUI/updates/test.kup", payload)]:
            z.writestr(name, data)
    return stream.getvalue()


def changed(data, edits):
    value = bytearray(data)
    central = value.index(b"PK\x01\x02")
    end = value.rindex(b"PK\x05\x06")
    offsets = {"local": 0, "central": central, "end": end}
    for role, at, fmt, new in edits:
        struct.pack_into(fmt, value, offsets[role] + at, new)
    return bytes(value)


def main():
    rng = random.Random(42017)
    payload = b"KUIUPD1\0" + rng.randbytes(180000) + b"BOUNDED DEFLATE RING" * 7000
    good = archive(payload)
    stored = archive(payload, zipfile.ZIP_STORED)
    descriptor = archive(payload, descriptor=True)
    checksum = struct.unpack_from("<I", good, good.index(b"PK\x01\x02") + 16)[0] ^ 1
    cases = [("deflate", good, 0), ("stored", stored, 0), ("descriptor", descriptor, 0),
        ("legacy", archive(payload, entries=[("KUI/runtime.kui", b"OLD GUI"), ("source/notes.txt", b"ignored")]), 3),
        ("multiple", archive(payload, entries=[("a.kup", payload), ("b.kup", payload)]), 3),
        ("traversal", archive(payload, entries=[("../escape.kup", payload)]), 3),
        ("truncated", good[:-5], 3),
        ("encrypted", changed(good, [("local", 6, "<H", 1), ("central", 8, "<H", 1)]), 3),
        ("method", changed(good, [("local", 8, "<H", 99), ("central", 10, "<H", 99)]), 3),
        ("local-method", changed(good, [("local", 8, "<H", 0)]), 3),
        ("local-name", changed(good, [("local", 30, "B", ord("x"))]), 3),
        ("crc", changed(good, [("local", 14, "<I", checksum), ("central", 16, "<I", checksum)]), 4),
        ("split", changed(good, [("end", 4, "<H", 1)]), 3),
        ("zip64", changed(good, [("end", 16, "<I", 0xffffffff)]), 3),
        ("entries-bound", changed(good, [("end", 8, "<H", 4097), ("end", 10, "<H", 4097)]), 3),
        ("directory-bound", changed(good, [("end", 12, "<I", 1048577)]), 3),
        ("payload-bound", changed(good, [("central", 24, "<I", 134226049)]), 3),
        ("symlink", changed(good, [("central", 38, "<I", 0o120777 << 16)]), 3),
        ("fifo", changed(good, [("central", 38, "<I", 0o010777 << 16)]), 3),
        ("ignore-extra-files", archive(payload, entries=[("KUI/runtime.kui", b"DO NOT INSTALL"),
            ("source/large.txt", b"IGNORED" * 100000), ("bundle/test.kup", payload)]), 0)]
    name = b"bundle/KUI/updates/test.kup"
    descriptor_at = 30 + len(name) + struct.unpack_from("<I", descriptor, descriptor.index(b"PK\x01\x02") + 20)[0]
    damaged = bytearray(descriptor)
    assert damaged[descriptor_at:descriptor_at+4] == b"PK\x07\x08"
    damaged[descriptor_at+4] ^= 1
    cases.append(("descriptor-crc", bytes(damaged), 3))
    unsigned = bytearray(descriptor[:descriptor_at] + descriptor[descriptor_at+4:])
    end = unsigned.rindex(b"PK\x05\x06")
    struct.pack_into("<I", unsigned, end + 16, struct.unpack_from("<I", unsigned, end + 16)[0] - 4)
    cases.append(("unsigned-descriptor", bytes(unsigned), 0))
    commented = io.BytesIO(good)
    with zipfile.ZipFile(commented, "a") as z:
        z.comment = b"bounded comment " * 4000
    cases.append(("comment-bound", commented.getvalue(), 0))
    central = good.index(b"PK\x01\x02")
    trailing = bytearray(good[:central] + b"TRAILING" + good[central:])
    old_compressed = struct.unpack_from("<I", good, central + 20)[0]
    struct.pack_into("<I", trailing, 18, old_compressed + 8)
    struct.pack_into("<I", trailing, central + 8 + 20, old_compressed + 8)
    struct.pack_into("<I", trailing, trailing.rindex(b"PK\x05\x06") + 16, central + 8)
    cases.append(("trailing", bytes(trailing), 3))
    for mode, result in [("cancel-before", 8), ("cancel-mid", 8), ("write-fault", 7),
        ("short-write", 7), ("sync-fault", 7), ("close-fault", 7), ("read-fault", 7),
        ("rename-fault", 7), ("space", 9), ("readonly-cache", 11),
        ("directory-cache", 11), ("directory-partial", 11), ("readonly-partial", 11), ("outside", 1)]:
        cases.append((mode, good, result))
    source = fatfs_source()
    with tempfile.TemporaryDirectory(prefix="kui-update-zip-") as temporary:
        base = Path(temporary)
        binary = base / "zip-image"
        flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-Iinclude", "-I" + str(source), "-Isrc/dreamcast", "-Ithird_party/miniz",
            "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_STDIO", "-DMINIZ_NO_TIME", "-DMINIZ_NO_ZLIB_APIS", "-DMINIZ_NO_MALLOC"]
        # Existing vendor miniz predates the project's strict warning policy.
        miniz = base / "miniz.o"
        run(*flags, "-w", "-c", "third_party/miniz/miniz.c", "-o", str(miniz))
        run(*flags, "tests/update_zip_image.c", "src/core/update_zip.c", "src/core/hash.c", "src/core/data.c", "src/core/diskio.c",
            str(source / "ff.c"), str(source / "ffunicode.c"), str(miniz),
            "-Wl,--wrap=f_open,--wrap=f_read,--wrap=f_write,--wrap=f_sync,--wrap=f_close,--wrap=f_getfree,--wrap=f_unlink,--wrap=f_rename",
            "-o", str(binary))
        expected = base / "expected.kup"
        expected.write_bytes(payload)
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            for name, data, result in cases:
                path = base / (name + ".zip")
                path.write_bytes(data)
                print(kind + ": " + run(str(binary), str(image), str(path), str(expected), name, str(result)).strip(), flush=True)
    print(f"PASS ZIP intake: {len(cases)} cases on each real filesystem under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
