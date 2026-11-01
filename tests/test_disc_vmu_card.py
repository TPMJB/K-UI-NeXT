#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Physical-disc same-product card selection and its mounted/raw bridge."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="kui-disc-vmu-card-") as work:
    output = Path(work)/"disc-card"
    sources=("tests/test_disc_vmu_card.c", "src/apps/disc_vmu_card.c",
             "src/core/vmu_game_card.c", "src/core/vmu_persist_profile.c",
             "src/core/hash.c", "src/core/data.c", "src/core/retail_image.c")
    subprocess.run([*shlex.split(os.environ.get("CC","cc")), "-std=c11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-DKUI_VMU_GAME_CARDS=1", "-Iinclude", "-I.deps/fatfs/source",
                    "-Isrc/dreamcast", "-Isrc/loader", *sources, "-o", str(output)],cwd=ROOT,check=True)
    subprocess.run([str(output)],check=True,env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0"))
