#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Wizard unit boundary: real inspection/subset/keys, mocked storage services.

Remembered-match records and publication failures are exercised separately by
test_vmu_match_memory.py and the real-FatFs wizard integration suite.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="kui-vmu-wizard-") as work:
    output = Path(work) / "wizard"
    fingerprint = Path(work) / "match-memory-fingerprint.o"
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
             "-ffunction-sections", "-fdata-sections", "-DKUI_VMU_GAME_CARDS=1",
             "-Iinclude", "-I.deps/fatfs/source", "-Isrc/dreamcast"]
    compiler = shlex.split(os.environ.get("CC", "cc"))
    # Keep the production pure fingerprint. Rename the persistent entry points
    # in this object so the unit fixture can mock only that storage boundary;
    # the unused real record paths are dropped by the ordinary section GC.
    persistent = ("load", "remember", "forget")
    subprocess.run([*compiler, *flags,
                    *(f"-Dkui_vmu_match_memory_{name}=unused_real_match_memory_{name}" for name in persistent),
                    "-c", "src/apps/vmu_match_memory.c", "-o", str(fingerprint)], cwd=ROOT, check=True)
    sources = ("tests/test_vmu_wizard.c", "src/apps/vmu_wizard.c",
               "src/core/vmu_image.c", "src/core/vmu_image_pack.c",
               "src/core/vmu_image_subset.c", "src/core/vmu_import_plan.c", "src/core/vmu_import_match.c", "src/core/hash.c", "src/core/vmu_game_card.c", "src/core/vmu_game_assign.c",
               "src/core/data.c", "src/core/retail_image.c")
    subprocess.run([*compiler, *flags, "-Wl,--gc-sections", *sources, str(fingerprint), "-o", str(output)],
                   cwd=ROOT, check=True)
    subprocess.run([str(output)], check=True,
                   env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))
