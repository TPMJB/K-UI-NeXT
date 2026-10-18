#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build an original runnable Dreamcast CHD diagnostic and codec test set.

Source the repository's pinned KOS environment first. This program needs only
the SH compiler/binutils and MAME chdman; it does not link KOS or use any game
material. The GDI control and all positive CHDs contain the same executable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DATA = 2048
RAW = 2352
SESSION = 45000
HD_SECTORS = 512


def run(command, *, cwd=ROOT):
    result = subprocess.run(list(map(str, command)), cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"command failed: {' '.join(map(str, command))}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def pattern(lba, offset):
    return ((lba * 37) ^ (offset * 13) ^ (offset >> 8) ^ 0xA5) & 255


def audio_pattern(sector, offset):
    return ((sector * 29) + (offset * 11) + ((offset >> 8) * 3) + 0x5B) & 255


def raw_sector(lba, payload):
    assert len(payload) == DATA
    result = bytearray(RAW)
    result[:12] = b"\0" + b"\xff" * 10 + b"\0"
    minute, rest = divmod(lba + 150, 4500)
    second, frame = divmod(rest, 75)
    result[12:16] = bytes(n // 10 * 16 + n % 10 for n in (minute, second, frame)) + b"\1"
    result[16:2064] = payload
    # These are deliberately original Mode1 raw bytes; zero EDC/ECC is kept
    # verbatim and checked after CHD conversion rather than copied from a disc.
    return result


def dual16(data, offset, value):
    struct.pack_into("<H", data, offset, value)
    struct.pack_into(">H", data, offset + 2, value)


def dual32(data, offset, value):
    struct.pack_into("<I", data, offset, value)
    struct.pack_into(">I", data, offset + 4, value)


def record(name, extent, length, directory=False):
    data = bytearray(33 + len(name) + (len(name) % 2 == 0))
    data[0] = len(data)
    dual32(data, 2, extent)
    dual32(data, 10, length)
    data[25] = 2 if directory else 0
    dual16(data, 28, 1)
    data[32] = len(name)
    data[33:33 + len(name)] = name
    return data


def build_executable(prefix, build, build_id):
    compiler = prefix + "gcc"
    flags = ["-std=c11", "-Os", "-g", "-Wall", "-Wextra", "-Werror", "-m4-single-only", "-ml",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fno-pic",
             "-ffunction-sections", "-fdata-sections", "-Iinclude", f'-DKUI_BUILD_ID="{build_id}"']
    objects = []
    for number, source in enumerate(("tests/chd_console_probe_start.S", "tests/chd_console_probe.c",
                                     "src/dreamcast/shell_font.c", "src/loader/minic.c")):
        obj = build / f"probe-{number}.o"
        run([compiler, *flags, "-c", source, "-o", obj])
        objects.append(obj)
    elf = build / "chd-console-probe.elf"
    run([compiler, "-m4-single-only", "-ml", "-nostdlib", "-Wl,--gc-sections", "-Wl,--build-id=none",
         "-Ttests/chd_console_probe.ld", f"-Wl,-Map,{build / 'chd-console-probe.map'}",
         "-o", elf, *objects, "-lgcc"])
    undefined = run([prefix + "nm", "-u", elf]).strip()
    if undefined:
        raise RuntimeError(f"freestanding executable has unresolved symbols: {undefined}")
    from check_retail_instructions import audit
    instruction_count = audit("chd-console-probe", run([prefix + "objdump", "-d", elf]))
    executable, bootstrap = build / "1ST_READ.BIN", build / "bootstrap2.bin"
    run([prefix + "objcopy", "-O", "binary", "--remove-section", ".bootstrap2", elf, executable])
    run([prefix + "objcopy", "-O", "binary", "--only-section", ".bootstrap2", elf, bootstrap])
    if not 128 <= executable.stat().st_size < 107 * DATA or not 8 <= bootstrap.stat().st_size <= 64:
        raise RuntimeError("unexpected executable/bootstrap size")
    return executable.read_bytes(), bootstrap.read_bytes(), {
        "compiler": run([compiler, "-dumpfullversion"]).strip(),
        "binary_bytes": executable.stat().st_size,
        "binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "bootstrap_bytes": bootstrap.stat().st_size,
        "undefined_symbols": [],
        "instruction_audit": instruction_count,
    }


def build_gdi(folder, executable, bootstrap):
    folder.mkdir(parents=True, exist_ok=True)
    sectors = [bytearray(pattern(SESSION + n, at) for at in range(DATA)) for n in range(HD_SECTORS)]
    # Only the ISO/IP/executable area differs from the pattern checked at LBA
    # 45128 and later. The whole 32KiB IP is original, including bootstrap2.
    for n in range(21 + (len(executable) + DATA - 1) // DATA):
        sectors[n][:] = bytes(DATA)
    ip = bytearray(16 * DATA)
    ip[:256] = b" " * 256
    fields = ((0, b"SEGA SEGAKATANA "), (16, b"SEGA ENTERPRISES"), (32, b"0000 GD-ROM1/1  "),
              (48, b"JUE     "), (56, b"0000000 "), (64, b"KUI-CHD1  "), (74, b"V1.000"),
              (80, b"20261018        "), (96, b"1ST_READ.BIN    "), (112, b"K-UI ORIGINAL   "),
              (128, b"K-UI CHD CONSOLE READ TEST V1"))
    for offset, value in fields:
        ip[offset:offset + len(value)] = value
    ip[0x6000:0x6000 + len(bootstrap)] = bootstrap
    for n in range(16):
        sectors[n][:] = ip[n * DATA:(n + 1) * DATA]
    pvd = sectors[16]
    pvd[:7] = b"\x01CD001\x01"
    pvd[8:40] = b"KUI ORIGINAL".ljust(32)
    pvd[40:72] = b"CHD CONSOLE READ TEST V1".ljust(32)
    dual32(pvd, 80, SESSION + HD_SECTORS)
    dual16(pvd, 120, 1)
    dual16(pvd, 124, 1)
    dual16(pvd, 128, DATA)
    pvd[156:190] = record(b"\0", SESSION + 20, DATA, True)
    pvd[881] = 1
    sectors[17][:7] = b"\xffCD001\x01"
    entries = (record(b"\0", SESSION + 20, DATA, True), record(b"\1", SESSION + 20, DATA, True),
               record(b"1ST_READ.BIN;1", SESSION + 21, len(executable)))
    at = 0
    for entry in entries:
        sectors[20][at:at + len(entry)] = entry
        at += len(entry)
    for n in range((len(executable) + DATA - 1) // DATA):
        block = executable[n * DATA:(n + 1) * DATA]
        sectors[21 + n][:len(block)] = block
    (folder / "track01.bin").write_bytes(b"".join(raw_sector(n, bytes(pattern(n, at) for at in range(DATA)))
                                                   for n in range(32)))
    (folder / "track02.raw").write_bytes(bytes(audio_pattern(n, at) for n in range(32) for at in range(RAW)))
    (folder / "track03.bin").write_bytes(b"".join(raw_sector(SESSION + n, payload) for n, payload in enumerate(sectors)))
    gdi = folder / "disc.gdi"
    gdi.write_text("3\n1 0 4 2352 track01.bin 0\n2 32 0 2352 track02.raw 0\n3 45000 4 2352 track03.bin 0\n", encoding="ascii")
    assert (folder / "track03.bin").stat().st_size == HD_SECTORS * RAW
    return gdi


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--tool-prefix", default="sh-elf-")
    parser.add_argument("--chdman", default=shutil.which("chdman"))
    parser.add_argument("--build-id", default=None)
    parser.add_argument("--host-cc", default="cc")
    args = parser.parse_args()
    if not args.chdman:
        parser.error("MAME chdman is required (--chdman /path/to/chdman)")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output must be empty; an existing test set is never overwritten")
    output.mkdir(parents=True, exist_ok=True)
    build = output / "build-evidence"
    build.mkdir()
    build_id = args.build_id or run(["git", "rev-parse", "--short=12", "HEAD"]).strip()
    if not re.fullmatch(r"[0-9a-f]{12,40}", build_id):
        parser.error("build-id must be the source commit's 12-40 lowercase hex digits")
    executable, bootstrap, report = build_executable(args.tool_prefix, build, build_id)
    games_name = "CHD Console Tests " + build_id[:12]
    games = output / "Games" / games_name
    gdi = build_gdi(games / "00 GDI control", executable, bootstrap)
    host = build / "host-probe"
    run([args.host_cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
         "-fno-pie", "-no-pie", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-DKUI_CHD_CONSOLE_HOST=1", "-Iinclude", "tests/chd_console_probe.c",
         "tests/test_chd_console_probe.c", "-o", host])
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    oracle_output = run([host, gdi.parent])
    injected_output = run([host, gdi.parent, "inject-corrupt-sector"])
    (build / "host-probe-pass.txt").write_text(oracle_output, encoding="utf-8")
    (build / "host-probe-injected-failure.txt").write_text(injected_output, encoding="utf-8")
    report["host_probe_checks"] = {"original_gdi": "9 passed", "injected_sector_corruption": "detected",
                                    "sanitizers": "AddressSanitizer + UndefinedBehaviorSanitizer"}
    chd_info = {}
    roundtrips = {}
    for filename, codec in (("01 Default", None), ("02 CD LZMA", "cdlz"),
                            ("03 CD DEFLATE", "cdzl"), ("04 CD FLAC", "cdfl")):
        image = games / (filename + ".chd")
        command = [args.chdman, "createcd", "-i", gdi, "-o", image]
        if codec:
            command += ["-c", codec]
        run(command)
        run([args.chdman, "verify", "-i", image])
        chd_info[image.name] = run([args.chdman, "info", "-i", image])
        with tempfile.TemporaryDirectory(prefix="extract-oracle-", dir=build) as temporary:
            oracle = Path(temporary)
            run([args.chdman, "extractcd", "-i", image, "-o", oracle / "disc.gdi"])
            for source, extracted in (("track01.bin", "disc01.bin"), ("track02.raw", "disc02.raw"),
                                      ("track03.bin", "disc03.bin")):
                if (gdi.parent / source).read_bytes() != (oracle / extracted).read_bytes():
                    raise RuntimeError(f"MAME round-trip changed source track: {image.name}: {source}")
            roundtrips[image.name] = "all three source tracks byte-identical"
    valid = games / "03 CD DEFLATE.chd"
    negative = games / "05 Truncated EXPECT PREPARE FAILURE.chd"
    negative.write_bytes(valid.read_bytes()[:124])
    checksums = {str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in sorted(games.rglob("*")) if path.is_file()}
    report.update({"fixture_version": 1, "source_build": build_id, "game_material": False,
                   "console_validation": "not yet run", "chdman_info": chd_info,
                   "mame_roundtrips": roundtrips, "sha256": checksums,
                   "read_tests": ["low/high density TOC", "low density cooked", "high density cooked",
                                  "16-sector boundary", "GD DMA command", "random/backwards/tail",
                                  "raw data framing and bytes", "audio sample byte order", "data/audio boundary"]})
    (output / "CHD-CONSOLE-TESTS.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (output / "CHD-CONSOLE-TESTS.txt").write_text(
        "K-UI original CHD console read test v1\n"
        f"Source build: {build_id}\n\n"
        "Copy the Games folder into the matching CHD candidate installation.\n"
        "0. VMU experimental users: this diagnostic needs the optional ordinary\n"
        "   reader. Back up the EXACT existing KUI/apps/games/retail-boot.kui\n"
        "   off the card before replacing it. Copy OPTIONAL-STANDARD-READER/\n"
        "   retail-boot.kui temporarily into KUI/apps/games/retail-boot.kui.\n"
        "   Restore your exact backed-up reader after these tests to continue\n"
        "   VMU testing. The persistent Toy Commander reader intentionally\n"
        "   rejects this original diagnostic; there is no automatic fallback.\n"
        f"1. Open {games_name}, then 00 GDI control; launch with Background\n"
        "   loading Off and CD music Off. Photograph the PASS/FAIL screen.\n"
        "2. Reboot and select 01 Default.chd. Choose Prepare CHD, wait for\n"
        "   completion, then Launch. The expected result is the same PASS screen.\n"
        "3. Repeat with 02 CD LZMA, 03 CD DEFLATE and 04 CD FLAC.\n"
        "4. Select 05 Truncated EXPECT PREPARE FAILURE. Preparation must refuse\n"
        "   it and keep Launch unavailable; it must never enter a game.\n"
        "5. Reopen a completed positive CHD: its cache should be reused.\n"
        "6. For cancellation, start preparing a positive CHD before its cache\n"
        "   exists, press B, then reopen/retry. Partial caches must not launch.\n\n"
        "All disc metadata, bootstrap code, executable and sectors are original.\n"
        "No commercial game or firmware bootstrap is included. These tests have\n"
        "not yet been run on a physical console. A PASS checks this fixture and\n"
        "the detached reader; it does not establish compatibility for all games.\n"
        "'GD DMA command' exercises the game's GD BIOS command interface. It\n"
        "does not test or implement physical ATA/IDE DMA. Power-cycle to return.\n",
        encoding="utf-8")
    print(json.dumps({"output": str(output), "source_build": build_id, "images": len(chd_info) + 1,
                      "binary_bytes": report["binary_bytes"], "console_validation": "not yet run"}))


if __name__ == "__main__":
    main()
