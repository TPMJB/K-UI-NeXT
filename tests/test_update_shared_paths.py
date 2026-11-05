#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutable app records stay physical through updater admission and routing."""
import os
from pathlib import Path
import shlex
import sys
import tempfile
import unittest

from test_games_library_images import fatfs_source, private_formatter
from test_update_images import run

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import update_package as update

MUTABLE = (
    "apps/games/launch-settings-a.bin", "apps/games/launch-settings-b.bin",
    "apps/games/selection-a.bin", "apps/games/selection-b.bin",
    "apps/games/vc2-vmu-session.bin", "apps/games/smash-vc2-vmu-session.bin",
    "apps/games/smash-swirl-vmu-session.bin", "apps/games/future-state.bin",
    "apps/games/smash-menu-native.cap", "apps/games/smash-menu-outer.cap",
    "apps/games/smash-menu-vc2.cap", "apps/games/smash-menu-swirl.cap",
    "apps/games/last-launch-a.txt", "apps/games/view.txt",
)
SOFTWARE = (
    "runtime.kui", "tools.kui", "recovery.kui", "apps/games/reader.kui",
    "apps/disc/reader.kui", "apps/music/player.kui",
    "apps/games/smash-vc2-vmu-helper.bin", "apps/games/smash-swirl-vmu-helper.bin",
    "apps/games/vc2-vmu-helper.bin", "apps/games/future-helper.bin",
    "apps/disc/future-helper.bin", "apps/music/future-helper.bin",
    "apps/games/probe.dat", "apps/music/title.ogg", "apps/music/title.wav",
    "assets/theme/font.bin", "assets/theme/icons/button.png", "redump.db", "tosec.db",
)


class ProducerOwnership(unittest.TestCase):
    def test_mutable_and_generated_files_cannot_be_packaged(self):
        for name in MUTABLE:
            with self.subTest(path=name), tempfile.TemporaryDirectory() as tmp:
                stage = Path(tmp) / "KUI"
                path = stage / name
                path.parent.mkdir(parents=True)
                path.write_bytes(b"generated mutable record")
                self.assertFalse(update.software_path_v2(name))
                with self.assertRaises(ValueError):
                    update.member_info_v2(name, path.read_bytes())
                with self.assertRaises(ValueError):
                    update.inventory_v2(stage)
                output = Path(tmp) / "bad.kup"
                with self.assertRaises(ValueError):
                    update.build_package_v2(stage, output)
                self.assertFalse(output.exists())

    def test_immutable_helpers_still_build_with_actual_consumer(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp) / "KUI"
            for name in SOFTWARE:
                self.assertTrue(update.software_path_v2(name), name)
                if name.endswith("-helper.bin"):
                    path = stage / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b"immutable helper bytes")
            output = Path(tmp) / "helpers.kup"
            report = update.build_package_v2(stage, output)
            self.assertEqual({entry["path"] for entry in report["members"]},
                             {name for name in SOFTWARE if name.endswith("-helper.bin")})
            self.assertEqual(report["consumer"]["decoder"], "current-source")


def main():
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromTestCase(ProducerOwnership))
    if not result.wasSuccessful():
        raise SystemExit(1)
    source = fatfs_source()
    with tempfile.TemporaryDirectory(prefix="kui-shared-paths-") as temporary:
        base = Path(temporary)
        binary = base / "shared-paths"
        flags = [*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-Iinclude", "-I" + str(source), "-Isrc/dreamcast"]
        run(*flags, "tests/test_update_shared_paths.c", "src/core/update.c", "src/core/update_file.c",
            "src/core/update_boot.c", "src/core/update_software_file.c", "src/core/update_handoff.c",
            "src/core/runtime_image.c", "src/core/runtime_file.c", "src/core/storage_policy.c",
            "src/core/hash.c", "src/core/data.c", "src/core/diskio.c",
            "src/core/game_launch_settings.c", "src/core/game_ram_patches.c",
            "src/apps/game_launch_settings_file.c", "src/core/games_selection.c",
            "src/apps/games_selection.c", "src/core/destination.c",
            str(source / "ff.c"), str(source / "ffunicode.c"),
            "-Wl,--wrap=f_open,--wrap=f_stat", "-o", str(binary))
        paths = SOFTWARE + MUTABLE + (
            "apps/games/helper.bin", "apps/disc/session.bin", "apps/music/user-data.bin",
            "apps/games/x-helper.bin.bak", "apps/games/../escape-helper.bin",
        )
        actual = [line == "1" for line in run(str(binary), "--namespace", *paths).splitlines()]
        assert actual == [update.software_path_v2(path) for path in paths], "C/Python namespace disagreement"
        formatter = private_formatter(base)
        for kind in ("fat32", "exfat"):
            image = base / (kind + ".img")
            run(str(formatter), kind, str(image))
            print(kind + ":\n" + run(str(binary), str(image)).strip(), flush=True)
    print("PASS C/Python ownership, producer rejection and real app I/O under ASan/UBSan", flush=True)


if __name__ == "__main__":
    main()
