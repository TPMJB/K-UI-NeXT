#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host per-game wizard tests with actual inspection/subset and product keys."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="kui-vmu-wizard-") as work:
    output = Path(work) / "wizard"
    sources = ("tests/test_vmu_wizard.c", "src/apps/vmu_wizard.c",
               "src/core/vmu_image.c", "src/core/vmu_image_pack.c",
               "src/core/vmu_image_subset.c", "src/core/vmu_import_plan.c", "src/core/vmu_import_match.c", "src/core/hash.c", "src/core/vmu_game_card.c", "src/core/vmu_game_assign.c",
               "src/core/data.c", "src/core/retail_image.c")
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-DKUI_VMU_GAME_CARDS=1", "-Iinclude", "-I.deps/fatfs/source",
                    "-Isrc/dreamcast", *sources, "-o", str(output)],
                   cwd=ROOT, check=True)
    subprocess.run([str(output)], check=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
