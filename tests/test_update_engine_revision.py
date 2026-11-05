#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute frozen delivered engine2 C and current C as separate programs.

Old SH payloads are only data. The historical failure is exercised by compiling
the actual frozen C implementation, rather than using today's functions after
pretending an old GUI executed. Both programs use real FatFs and settings APIs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib

from test_games_library_images import fatfs_source, private_formatter

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/updater-engine2"
FROZEN = {
    "include/kui/update.h": "9c437a89d2008d1a5db74cd064ddb4724134c18c606c5ee415239a1257ed6862",
    "src/core/update.c": "06bb565d1df0a2281a3c71d5cba986cd93e443f0d8dbe57985604486e5dd869b",
    "src/core/update_boot.c": "1e6ccb7c88ccde52a778b6b15cb90f7be659916ee9a92b840d7bb7de5d8cd102",
    "src/core/update_software_file.c": "aecf57b11b1389bebea872ce36240c3b91b7d5b0a81748a07d0eb871a1e33b4d",
}
SOURCES = (
    "src/core/update.c", "src/core/update_file.c", "src/core/update_boot.c",
    "src/core/update_software_file.c", "src/core/update_handoff.c",
    "src/core/runtime_image.c", "src/core/runtime_file.c", "src/core/storage_policy.c",
    "src/core/hash.c", "src/core/data.c", "src/core/diskio.c",
    "src/core/game_launch_settings.c", "src/core/game_ram_patches.c",
    "src/apps/game_launch_settings_file.c",
)


def run(*args, cwd=ROOT):
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0",
                                 "UBSAN_OPTIONS": "halt_on_error=1"})
    if result.returncode:
        print(result.stdout, end="")
        print(result.stderr, end="")
        raise SystemExit("Engine revision fixture failed: " + " ".join(map(str, args)))
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy-source", type=Path, default=FIXTURE,
                        help="Frozen delivered530 source; defaults to the pinned offline negative fixture")
    args = parser.parse_args()
    old = args.legacy_source.resolve()
    provenance = json.loads((FIXTURE / "provenance.json").read_text())
    for group in ("copied_files", "shared_current_files"):
        for path, entry in provenance[group].items():
            file = old / path
            if not file.is_file() and group == "shared_current_files":
                file = ROOT / path
            if (file.is_symlink() or not file.is_file() or file.stat().st_size != entry["bytes"] or
                    hashlib.sha256(file.read_bytes()).hexdigest() != entry["sha256"]):
                parser.error("Frozen/shared input changed; the negative fixture needs explicit review: " + path)
    for path, expected in FROZEN.items():
        file = old / path
        if file.is_symlink() or not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != expected:
            parser.error("Legacy fixture is not the pinned delivered engine2 implementation: " + path)
    fatfs = fatfs_source()
    with tempfile.TemporaryDirectory(prefix="kui-engine-revision-") as temporary:
        base = Path(temporary)
        flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
                 "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                 "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]
        binaries = []
        for source, frozen, case in ((old, True, "frozen-defect"), (ROOT, False, "current-contract")):
            binary = base / ("frozen-engine2" if frozen else "current-engine3")
            linked = [source / name if (source / name).is_file() else ROOT / name for name in SOURCES]
            run(*flags, "-DFROZEN_ENGINE2=" + str(int(frozen)), "-DKUI_GUI_FAST_CRC32=0",
                "-I" + str(source / "include"), "-I" + str(ROOT / "include"),
                "-I" + str(fatfs), "-I" + str(source / "src/dreamcast"),
                str(ROOT / "tests/test_update_engine_revision_image.c"),
                *(str(file) for file in linked),
                str(fatfs / "ff.c"), str(fatfs / "ffunicode.c"),
                "-Wl,--wrap=f_open,--wrap=f_stat", "-o", str(binary))
            binaries.append((binary, case))
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            for binary, case in binaries:
                image = base / (kind + "-" + case + ".img")
                run(str(formatter), kind, str(image))
                print(kind + ": " + run(str(binary), str(image), case).strip(), flush=True)
            # A root-only repair cannot make an old selected GUI execute new
            # code. Carry actual checked handoff/disk bytes across programs.
            image = base / (kind + "-bridge.img")
            context = base / (kind + "-bridge.handoff")
            run(str(formatter), kind, str(image))
            print(kind + ": " + run(str(binaries[1][0]), str(image), "bridge-dispatch", str(context)).strip(), flush=True)
            print(kind + ": " + run(str(binaries[0][0]), str(image), "bridge-recipient", str(context)).strip(), flush=True)
        sys.path.insert(0, str(ROOT / "tools"))
        import update_package as update
        from runtime_package import envelope
        stage = base / "stage"
        (stage / "apps/games").mkdir(parents=True)
        (stage / "apps/games/revision-reader.kui").write_bytes(envelope(bytes(16), 16, "333333333333"))
        output = base / "current.kup"
        report = update.build_package_v2(stage, output)
        assert report["flags"] == update.REQUIRES_ENGINE3 and report["engine_minimum"] == 3
        assert report["consumer"]["engine_minimum"] == 3
        # A structurally valid old overlay remains readable for migration, but
        # the delivery preflight must refuse to hand it to an engine2 console.
        legacy = bytearray(output.read_bytes())
        legacy[45] = 0
        struct.pack_into("<I", legacy, 124, zlib.crc32(legacy[:124]))
        (base / "legacy.kup").write_bytes(legacy)
        assert update.verify(base / "legacy.kup")["engine_minimum"] == 2
        try:
            update.current_consumer_check(base / "legacy.kup")
        except ValueError as error:
            assert "must require the current engine3" in str(error)
        else:
            raise AssertionError("current delivery consumer accepted an obsolete feature contract")
        payload = update.CAPABILITY + update.ENGINE_LEGACY + update.HANDOFF_HEADER + bytes(176)
        (stage / "runtime.kui").write_bytes(envelope(payload, len(payload), "222222222222"))
        retained = output.read_bytes()
        try:
            update.build_package_v2(stage, output)
        except ValueError as error:
            assert "current updater engine3" in str(error)
        else:
            raise AssertionError("new producer allowed a GUI2 downgrade")
        assert output.read_bytes() == retained
        payload = update.CAPABILITY + update.ENGINE + update.HANDOFF_HEADER + bytes(176)
        (stage / "runtime.kui").write_bytes(envelope(payload, len(payload), "222222222222"))
        report = update.build_package_v2(stage, output, full=True)
        assert report["flags"] == update.FULL | update.REQUIRES_ENGINE3
        print("PASS current producer/consumer: reader-only requires3, legacy delivery refused, GUI2 rejected atomically, GUI3 FULL accepted", flush=True)
    print("PASS separate frozen engine2/current engine3 C programs on FAT32/exFAT; SH code was not executed", flush=True)


if __name__ == "__main__":
    main()
