#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Metadata-only selected-game card status, with actual strict wire codecs."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="kui-vmu-game-info-") as work:
    output = Path(work)/"game-info"
    sources=("tests/test_vmu_game_info.c", "src/apps/vmu_game_info.c",
             "src/core/vmu_emulation_settings.c", "src/core/vmu_game_card.c",
             "src/core/vmu_game_assign.c", "src/core/data.c", "src/core/retail_image.c")
    subprocess.run([*shlex.split(os.environ.get("CC","cc")), "-std=c11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-DKUI_VMU_GAME_CARDS=1", "-Iinclude", "-I.deps/fatfs/source",
                    "-Isrc/dreamcast", *sources, "-o", str(output)],cwd=ROOT,check=True)
    subprocess.run([str(output)],check=True,env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0"))
