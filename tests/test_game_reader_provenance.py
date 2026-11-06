#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Final Games source identity through production fallback on real FatFs volumes.

Generated game data is never executed. Optional linked module inputs are read
unchanged; this checks preparation/admission, not console execution or timing.
The negative control compiles the actual pre-fix Games source from Git.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import zlib

from test_games_library_images import private_formatter
from test_games_retail import make_retail_fixture, assert_native_background_payload
from test_loader_probe_images import partition_image

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = "2aac9af84e8246efab046b12918c626ec1252087"
FALLBACK = "native-dual-standalone-report"
CASES = (FALLBACK, "native-dual-standalone-y-report", "native-dual-x-report",
         "native-standard-report", "native-standard-cooked-report")


def run(*command):
    result = subprocess.run(command, cwd=ROOT, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0",
                            UBSAN_OPTIONS="halt_on_error=1"), text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{shlex.join(map(str, command))}\n{result.stdout}{result.stderr}")
    return result.stdout


def compile_harness(directory, old=False):
    commands = run("make", "-B", "-n", "build/games-retail").splitlines()
    command = next(shlex.split(line) for line in commands
                   if "tests/games_retail.c" in line and "-o build/games-retail" in line)
    output = directory / ("original-reader-provenance" if old else "reader-provenance")
    command[command.index("-o") + 1] = str(output)
    if old:
        old_directory = directory / "original-games-source"
        old_directory.mkdir()
        # Keep the original private include closure too: current proof/hint
        # contracts may evolve independently of this historical bug control.
        for name in run("git", "ls-tree", "-r", "--name-only", ORIGINAL, "src/apps").splitlines():
            if name.endswith(".inc"):
                (old_directory / Path(name).name).write_text(run("git", "show", f"{ORIGINAL}:{name}"))
        original = old_directory / "games_retail.c"
        original.write_text(run("git", "show", f"{ORIGINAL}:src/apps/games_retail.c"))
        command[command.index("src/apps/games_retail.c")] = str(original)
    command += ["-DKUI_SMASH_LAUNCH_TIMING=1", "-DKUI_SMASH_PHASE_CLOCK_HOST_TEST=1",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-fno-sanitize-recover=all", "-fno-pie", "-no-pie", "-Isrc/apps",
                "src/apps/smash_launch_timing.c", "src/core/smash_phase_clock.c"]
    if "src/core/smash_menu_loader.c" not in command:
        command.append("src/core/smash_menu_loader.c")
    run(*command)
    return output


def source_identity(data):
    assert data[:8] == b"KUIRUN1\0" and zlib.crc32(data[:60]) == struct.unpack_from("<I", data, 60)[0]
    size, = struct.unpack_from("<I", data, 16)
    crc, = struct.unpack_from("<I", data, 32)
    assert size == len(data) - 64 and crc == zlib.crc32(data[64:])
    return data[40:52].decode("ascii"), crc, size


def record_fields(data):
    body, footer = data.rsplit(b"crc32: ", 1)
    assert len(footer) == 9 and footer.endswith(b"\n") and int(footer, 16) == zlib.crc32(body)
    return dict(line.split(": ", 1) for line in body.decode("ascii").splitlines()[1:] if ": " in line)


def fixture_modules(directory, real):
    if real:
        shutil.copyfile(real[0], directory / "retail-native-dual.kui")
        if (directory / "retail-native-standalone.kui").exists():
            shutil.copyfile(real[1], directory / "retail-native-standalone.kui")
    else:
        # Distinct authored fixture builds separate the GUI, initial and final
        # identities. Never alter the optional real linked artifact inputs.
        for name, label in (("retail-native-dual.kui", b"111111111111"),
                            ("retail-native-standalone.kui", b"222222222222")):
            path = directory / name
            if not path.exists():
                continue
            data = bytearray(path.read_bytes())
            data[40:52] = label
            struct.pack_into("<I", data, 60, zlib.crc32(data[:60]))
            path.write_bytes(data)


def prepare(binary, clean, directory, case, real=None):
    fixture = directory / "fixture"
    make_retail_fixture(fixture, case, 0x47000)
    fixture_modules(fixture, real)
    image = directory / "working.img"
    shutil.copyfile(clean, image)
    run(str(binary), str(image), str(fixture), "seed", case)
    run(str(binary), str(image), str(fixture), "check", case)
    timing = record_fields((fixture / "launch-timing.txt").read_bytes())
    reports = [record_fields(path.read_bytes()) for path in fixture.glob("after-last-launch-*.txt")
               if path.stat().st_size]
    assert len(reports) == 1
    return fixture, timing, reports[0]


def expect_final(fixture, timing, report, case):
    module = "retail-native-standalone.kui" if case == FALLBACK else (
        "retail-boot.kui" if case.startswith("native-standard-") else "retail-native-dual.kui")
    build, crc, size = source_identity((fixture / module).read_bytes())
    # Existing fields first: the old source genuinely reports the discarded
    # module here, rather than failing only because new optional fields lack.
    assert timing["metric.module_crc32"] == f"0x{crc:08X}", "stale final module CRC"
    assert timing["metric.module_bytes"] == str(size), "stale final module byte count"
    assert timing["source_module_path"] == "0:/KUI/apps/games/" + module
    assert timing["source_module_build"] == build
    assert report["package"] == "/kui/apps/games/" + module
    assert report["source module build"] == build
    assert report["source module crc32"] == f"{crc:08X}"
    assert report["source module payload bytes"] == str(size)
    payload_name = "standard-payload.bin" if case.startswith("native-standard-") else "dual-payload.bin"
    payload = (fixture / payload_name).read_bytes()
    assert len(payload) == size and zlib.crc32(payload) != crc, "source identity confused with patched launch payload"
    assert_native_background_payload(fixture, module, payload)
    assert (fixture / ("after-" + module)).read_bytes() == (fixture / module).read_bytes()
    for name in ("retail-boot.kui", "retail-native-dual.kui", "retail-native-standalone.kui",
                 "disc.gdi", "track01.bin", "music track02.raw", "track03.bin"):
        before, after = fixture / name, fixture / ("after-" + name)
        if after.exists() and after.stat().st_size:
            assert after.read_bytes() == before.read_bytes(), name
    if case == FALLBACK:
        assert report["reader"] == timing["reader"] == "Background New (X), no virtual VMU"
        assert timing["virtual_vmu"] == "Off"
        _, discarded_crc, discarded_size = source_identity((fixture / "retail-native-dual.kui").read_bytes())
        assert crc != discarded_crc and size != discarded_size
    return {"case": case, "source_build": build, "source_crc32": f"{crc:08X}", "source_bytes": size}


def audit(real=None):
    with tempfile.TemporaryDirectory(prefix="kui-reader-provenance-") as temporary:
        directory = Path(temporary)
        binary = compile_harness(directory)
        old = compile_harness(directory, old=True)
        formatter = private_formatter(directory)
        rows, controls = [], []
        for kind in ("fat32", "exfat"):
            volume, clean = directory / f"{kind}.img", directory / f"{kind}-mbr.img"
            run(str(formatter), kind, str(volume))
            partition_image(volume, clean, kind)
            for case in CASES:
                fixture, timing, report = prepare(binary, clean, directory, case)
                rows.append(dict(filesystem=kind, modules="authored", **expect_final(fixture, timing, report, case)))
            fixture, timing, report = prepare(old, clean, directory, FALLBACK)
            _, discarded_crc, discarded_size = source_identity((fixture / "retail-native-dual.kui").read_bytes())
            assert report["package"] == "/kui/apps/games/retail-native-standalone.kui"
            assert timing["metric.module_crc32"] == f"0x{discarded_crc:08X}"
            assert timing["metric.module_bytes"] == str(discarded_size)
            try:
                expect_final(fixture, timing, report, FALLBACK)
            except AssertionError as error:
                assert str(error) == "stale final module CRC", f"unexpected old-source failure: {error}"
                _, final_crc, _ = source_identity((fixture / "retail-native-standalone.kui").read_bytes())
                controls.append({"filesystem": kind, "refusal": str(error),
                                 "observed_discarded_crc32": f"{discarded_crc:08X}",
                                 "expected_final_crc32": f"{final_crc:08X}"})
            else:
                raise AssertionError("pre-fix production source unexpectedly passed final fallback identity")
            if real:
                for case in (FALLBACK, "native-dual-standalone-y-report"):
                    fixture, timing, report = prepare(binary, clean, directory, case, real)
                    rows.append(dict(filesystem=kind, modules="unchanged linked inputs",
                                     **expect_final(fixture, timing, report, case)))
        return {"ok": True, "scope": "production Games preparation/admission only; no module execution",
                "cases": rows, "old_source_commit": ORIGINAL, "old_source_controls": controls,
                "old_source_sha256": hashlib.sha256(run("git", "show",
                    f"{ORIGINAL}:src/apps/games_retail.c").encode()).hexdigest(),
                "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in (
                    "src/apps/games_retail.c", "src/apps/game_launch_report.c", "src/apps/smash_launch_timing.c",
                    "include/kui/game_launch_report.h", "include/kui/smash_launch_timing.h",
                    "tests/games_retail.c", "tests/test_game_reader_provenance.py")},
                "sanitizers": ["address", "undefined"],
                "real_module_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                       for path in (real or ())}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--real-dual", type=Path)
    parser.add_argument("--real-standalone", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if bool(args.real_dual) != bool(args.real_standalone):
        parser.error("supply both real module inputs or neither")
    result = audit((args.real_dual.resolve(), args.real_standalone.resolve()) if args.real_dual else None)
    print(json.dumps(result, indent=2) if args.json else
          f"PASS final reader source provenance: {len(result['cases'])} production preparations; "
          f"{len(result['old_source_controls'])} old-source CRC failures")
