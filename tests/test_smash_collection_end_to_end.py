#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production unified UI preparation with explicit bounded I/O seams."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="smash-unified-ui-") as temporary:
        work = Path(temporary)
        template = work / "template.bin"
        subprocess.run(["python3", "tools/vmu_persist_profile.py", "--template", str(template)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        executable = work / "test-prepare"
        sources = ["tests/test_smash_collection_end_to_end.c", "src/core/smash_native_child.c",
                   "src/core/smash_genesis_save_header.c", "src/core/game_launch_settings.c", "src/core/smash_card_session.c",
                   "src/core/smash_native_menu.c", "src/core/smash_menu_collection.c", "src/core/smash_menu_loader.c",
                   "src/core/smash_menu_return.c", "src/core/smash_menu_capsule.c", "src/core/smash_ce_child.c",
                   "src/core/smash_ce_profile.c", "src/core/smash_ce_helper.c", "src/core/vmu_persist_profile.c", "src/core/vmu_game_card.c",
                   "src/core/retail_image.c", "src/core/hash.c", "src/core/data.c"]
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wpedantic", "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections", "-Iinclude", "-Ifatfs/source",
                        *sources, "-Wl,--gc-sections", "-o", str(executable)], cwd=ROOT, check=True)
        subprocess.run([str(executable), str(template)], cwd=ROOT, check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
