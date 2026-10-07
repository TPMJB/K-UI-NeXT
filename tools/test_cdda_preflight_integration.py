#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual profile13 read-only preflight adapter with generated files.

Synthetic IP/ISO/boot bytes and all 15 backing files are generated independently
of production parsers. hashlib/zlib supply exact digest expectations. No game
executable, original boot header, audio or disc image is required.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

STARTS = (0, 7257, 45000, 201900, 219712, 234091, 255599, 272017,
          282799, 303092, 323352, 341314, 356690, 374201, 377422)
BOOT_BYTES = 5003


def dual16(value):
    return struct.pack("<H", value) + struct.pack(">H", value)


def dual32(value):
    return struct.pack("<I", value) + struct.pack(">I", value)


def directory_record(lba, length, flags, name):
    size = 33 + len(name) + (0 if len(name) & 1 else 1)
    out = bytearray(size)
    out[0] = size
    out[2:10], out[10:18] = dual32(lba), dual32(length)
    out[25], out[28:32], out[32] = flags, dual16(1), len(name)
    out[33:33 + len(name)] = name
    return out


def pattern(size, tag):
    # A byte-level reference, deliberately separate from production SHA/CRC.
    return bytes(((i * 37 + (i >> 4) * 13 + tag * 29) ^ (i >> 8)) & 255
                 for i in range(size))


def bcd(value):
    return (value // 10 << 4) | value % 10


def raw_sector(lba, payload):
    assert len(payload) == 2048
    fad = lba + 150
    out = bytearray(2352)
    out[1:11] = b"\xff" * 10
    out[12:16] = bytes((bcd(fad // 4500), bcd(fad // 75 % 60),
                         bcd(fad % 75), 1))
    out[16:2064] = payload
    out[2064:] = pattern(288, lba % 97)
    return out


def fixture(directory, cooked=False, prefix=0):
    sectors = [bytearray(pattern(2048, i + 3)) for i in range(64)]
    ip = sectors[0]
    ip[:256] = b" " * 256
    ip[:16] = b"SEGA SEGAKATANA "
    ip[37:43], ip[48:51], ip[56:63] = b"GD-ROM", b"JUE", b"0000000"
    ip[64:71], ip[74:80] = b"T-TEST3", b"V1.000"
    ip[96:108] = b"1ST_READ.BIN"
    title = b"K-UI GENERATED PREFLIGHT"
    ip[128:128 + len(title)] = title
    pvd = bytearray(2048)
    pvd[0], pvd[1:6], pvd[6], pvd[881] = 1, b"CD001", 1, 1
    pvd[80:88] = dual32(64)  # Volume length; extents remain absolute LBAs.
    pvd[120:124], pvd[124:128], pvd[128:132] = dual16(1), dual16(1), dual16(2048)
    pvd[156:190] = directory_record(45020, 2048, 2, b"\x00")
    sectors[16] = pvd
    root = bytearray(2048)
    records = (directory_record(45020, 2048, 2, b"\x00") +
               directory_record(45020, 2048, 2, b"\x01") +
               directory_record(45021, BOOT_BYTES, 0, b"1ST_READ.BIN;1"))
    root[:len(records)] = records
    sectors[20] = root
    boot = pattern(BOOT_BYTES, 71)
    for i, value in enumerate(boot):
        sectors[21 + i // 2048][i % 2048] = value
    descriptor = ["15"]
    for number, start in enumerate(STARTS, 1):
        data = number in (1, 3, 15)
        name = f"track{number:02d}.{'bin' if data else 'raw'}"
        count = 64 if number == 3 else 32 if number == 15 else 1
        stride = 2048 if number == 3 and cooked else 2352
        offset = prefix if number == 3 else 0
        descriptor.append(f"{number} {start} {4 if data else 0} {stride} {name} {offset}")
        contents = bytearray(b"\xa3" * offset)
        for relative in range(count):
            payload = sectors[relative] if number == 3 else pattern(2048, number + relative)
            if data:
                contents += payload if stride == 2048 else raw_sector(start + relative, payload)
            else:
                contents += pattern(2352, number)
        (directory / name).write_bytes(contents)
    (directory / "fixture.gdi").write_text("\n".join(descriptor) + "\n")
    ip_bytes = b"".join(sectors[:16])
    return (f"{zlib.crc32(ip_bytes):08x}", hashlib.sha256(ip_bytes).hexdigest(),
            f"{zlib.crc32(boot):08x}", hashlib.sha256(boot).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--case", help="run one integration scenario")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    environment = os.environ.copy()
    if args.sanitize:
        environment.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=0")
        environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    cases = ("pass", "padding-corrupt", "map-parse", "map-missing",
             "map-truncated", "map-overlap", "descriptor-io", "source-io", "raw-sync", "raw-mode",
             "raw-address", "ip-header", "iso-endian", "boot-missing", "boot-extent",
             "payload-corrupt", "cancel-before", "cancel-inspect", "cancel-ip", "cancel-boot",
             "progress-inspect", "progress-ip", "progress-boot", "cancel-last", "geometry-invalid", "extent-size",
             "run-gap", "run-zero", "run-partition", "run-overlap", "slots64-pass", "slots65-refused",
             "slots161-refused", "deadline-inspect", "deadline-map", "stack-guard")
    if args.case and args.case not in cases:
        parser.error("unknown scenario")
    if args.case:
        cases = (args.case,)
    with tempfile.TemporaryDirectory(prefix="kui-cdda-preflight-") as temporary:
        directory = Path(temporary)
        binary = directory / "preflight"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-DCDDA_TEST_PROFILE=13", "-DCDDA_PREFLIGHT_HOST_TEST=1",
                   '-DKUI_BUILD_ID="000000000000"', "-ffunction-sections",
                   "-fdata-sections", "-I", str(root / "include"),
                   str(root / "tests/test_cdda_preflight_integration.c")]
        command += [str(root / path) for path in ("src/core/cdda_preflight.c", "src/core/cdda_disc.c",
                    "src/core/game_image.c", "src/core/game_metadata.c", "src/core/hash.c",
                    "src/core/retail_image.c", "src/core/cdda_pcm.c", "src/core/cdda_clock.c",
                    "src/core/cdda_stream.c", "src/core/data.c")]
        command += ["-Wl,--gc-sections"]
        if args.sanitize:
            command += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command + ["-o", str(binary)], check=True)
        for case in cases:
            inputs = directory / case
            inputs.mkdir()
            expected = fixture(inputs, cooked=case == "cooked-pass", prefix=512 if case == "prefix-pass" else 0)
            subprocess.run([str(binary), case, str(inputs), *expected], check=True,
                           timeout=30, env=environment)
    print(f"CDDA profile13 preflight: {len(cases)} independent adapter simulations passed "
          "(native stack/timing and console validation remain separate).")


if __name__ == "__main__":
    main()
