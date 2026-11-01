#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Physical-disc VMU frontdoor dispatch and mounted-session failure paths."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="kui-disc-vmu-launcher-") as work:
    output=Path(work)/"disc-vmu-launcher"
    subprocess.run([*shlex.split(os.environ.get("CC","cc")),"-std=c11","-O1","-g",
                    "-Wall","-Wextra","-Werror","-Wpedantic",
                    "-fsanitize=address,undefined","-fno-omit-frame-pointer","-fno-pie","-no-pie",
                    "-ffunction-sections","-fdata-sections","-Wl,--gc-sections",
                    "-Iinclude","-I.deps/fatfs/source","-Isrc/dreamcast","-Isrc/loader",
                    "tests/test_disc_vmu_launcher.c","src/apps/disc_vmu_launcher.c",
                    "src/core/vmu_game_card.c","-o",str(output)],
                   cwd=ROOT,check=True)
    subprocess.run([str(output)],check=True,env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0"))
