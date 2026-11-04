#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual FAT32/exFAT prepare_launch routing with a modeled successful SDK proof.

The synthetic game bytes do not contain an SDK. Only its complete-proof
boundary is modeled; actual settings, launcher, capabilities, file reads,
handoff payload and checksummed launch report are exercised unchanged.
"""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import zlib

from test_games_retail import ROOT, make_retail_fixture, synthetic_package, standalone_package
from test_images import run
from test_loader_probe_images import partition_image, check_fs, digest
from vmu_native_cache_seed import template_seed

CASES = ("new-off", "new-off-absent", "new-off-badcap", "new-off-corrupt", "new-off-directory",
         "new-on", "legacy-off", "standard-off", "ce-off", "cdda-off")


def fixture(folder, case):
    make_retail_fixture(folder, "native-dual-x-report")
    combined = struct.pack("<8I", 0x3152444B, 0x4C415544, 1, 32, 1, 2, 0x51000, 0)
    toggle = struct.pack("<8I", 0x5649554B, 0x3130544D, 1, 32, 7, 0x51000, 0x30000, 1)
    mode = b"KUINVM01" + struct.pack("<6I", 1, 32, 0, 0, 0, 0)
    (folder / "retail-native-dual.kui").write_bytes(synthetic_package(dual=combined + toggle + mode + template_seed()))
    physical = struct.pack("<8I", 0x5049554B, 0x31594850, 1, 32, 0, 0, 0, 0)
    (folder / "retail-physical.kui").write_bytes(synthetic_package(dual=physical))
    standalone = bytearray(standalone_package("native-dual-standalone-badcap" if "badcap" in case else "unused"))
    if "corrupt" in case:
        standalone[-1] ^= 1
    (folder / "retail-native-standalone.kui").write_bytes(standalone)
    if case == "ce-off":
        source = bytearray((folder / "track03.bin").read_bytes())
        source[16 + 62] = ord("1")
        (folder / "track03.bin").write_bytes(source)


def main():
    environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    for flag in (0, 1):
        subprocess.run(["make", "-f", "tests/performance_route.mk", f"PERF_ROUTE_FLAG={flag}",
                        f"build/games-retail-perf-route-{flag}"], cwd=ROOT, check=True)
    with tempfile.TemporaryDirectory(prefix="kui-perf-route-") as temporary:
        base = Path(temporary)
        for kind in ("fat32", "exfat"):
            volume = base / f"{kind}-volume.img"
            with volume.open("wb") as output:
                output.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(volume)) if kind == "fat32" else run("mkfs.exfat", str(volume))
            clean = base / f"{kind}-mbr.img"
            partition_image(volume, clean, kind)
            for flag in (0, 1):
                binary = ROOT / f"build/games-retail-perf-route-{flag}"
                for case in CASES:
                    folder = base / "fixture"
                    fixture(folder, case)
                    image = base / "working.img"
                    shutil.copyfile(clean, image)
                    name = "native-perf-" + case
                    subprocess.run([str(binary), str(image), str(folder), "seed", name],
                                   cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
                    before = digest(image)
                    result = subprocess.run([str(binary), str(image), str(folder), "check", name],
                                            cwd=ROOT, env=environment, capture_output=True, text=True)
                    if result.returncode:
                        print(result.stdout, result.stderr, flush=True)
                        result.check_returncode()
                    text = result.stdout
                    requested = bool(flag and case.startswith("new-off"))
                    selected = requested and case == "new-off"
                    assert ("PERFORMANCE N0:" in text) == requested
                    assert ("Performance route: standalone New" in text) == selected
                    if requested and "absent" in case:
                        assert "Performance N0 needs its standalone New module" in text
                    if requested and any(word in case for word in ("badcap", "corrupt", "directory")):
                        assert "standalone New module unreadable or capability missing" in text
                    for source in ("retail-native-dual.kui", "retail-physical.kui", "ce-probe.kui", "disc.gdi", "track03.bin"):
                        assert (folder / ("after-" + source)).read_bytes() == (folder / source).read_bytes()
                    if not any(word in case for word in ("absent", "directory")):
                        assert (folder / "after-retail-native-standalone.kui").read_bytes() == (folder / "retail-native-standalone.kui").read_bytes()
                    report = folder / "after-last-launch-a.txt"
                    if report.exists():
                        body, footer = report.read_bytes().rsplit(b"crc32: ", 1)
                        assert int(footer, 16) == zlib.crc32(body)
                        fields = dict(line.split(": ", 1) for line in body.decode().splitlines()[1:])
                        expected = "retail-native-standalone.kui" if selected else "retail-physical.kui" if case == "standard-off" else "ce-probe.kui" if case == "ce-off" else "retail-native-dual.kui"
                        assert fields["package"] == "/kui/apps/games/" + expected
                        payload = (folder / "perf-payload.bin").read_bytes()
                        original = (folder / expected).read_bytes()[64:]
                        assert payload[:0x1000] == original[:0x1000]
                        expected_tail = bytearray(original)
                        if expected == "retail-native-dual.kui":
                            mode_at = expected_tail.index(b"KUINVM01")
                            struct.pack_into("<I", expected_tail, mode_at + 16, 7)
                        assert payload[0x2000:] == expected_tail[0x2000:]
                    else:
                        assert digest(image) == before and not (folder / "perf-payload.bin").exists()
                    check_fs(image, base / "checked-volume.img", kind, True)
                    print(f"PASS {kind}: flag={flag} {case}; selected={int(selected)}; original sources preserved", flush=True)
    print("PASS 40 actual mounted-launch route cases; default-off and opt-in verified")


if __name__ == "__main__":
    main()
