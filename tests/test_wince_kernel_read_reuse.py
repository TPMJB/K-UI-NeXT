#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove the real CE prepare fixture detects the former two-read behavior."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = "tests/test_armada_ce_excerpt_prepare.c"
source = (ROOT / "src/apps/games_retail.c").read_text()
reuse = "if(!extent_crc_proof_take(&ce_kernel_proof,image,map->boot_lba,map->boot_bytes,&map->boot_crc32))"
assert source.count(reuse) == 1
old = source.replace(reuse, "if(true || !extent_crc_proof_take(&ce_kernel_proof,image,map->boot_lba,map->boot_bytes,&map->boot_crc32))")
# Use the maintained production fixture's actual source/dependency recipe.
recipe = subprocess.check_output(["make", "-nB", "build/test-armada-ce-excerpt-prepare"], cwd=ROOT, text=True)
commands = [shlex.split(line) for line in recipe.splitlines() if FIXTURE in line and " -o " in line]
assert len(commands) == 1
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")

with tempfile.TemporaryDirectory(prefix="kui-ce-source-reuse-") as temporary:
    path = Path(temporary)
    old_source, fixture, binary = path / "games_retail.c", path / "prepare.c", path / "prepare"
    old_source.write_text(old)
    fixture.write_text((ROOT / FIXTURE).read_text().replace(
        '#include "../src/apps/games_retail.c"', f'#include "{old_source}"'))
    command = commands[0]
    command[command.index(FIXTURE)] = str(fixture)
    command[command.index("-o") + 1] = str(binary)
    command += ["-I", str(ROOT / "src/apps"), "-fno-pie", "-no-pie"]
    subprocess.run(command, cwd=ROOT, check=True)
    two = subprocess.run([str(binary)], cwd=ROOT, env=dict(env, KUI_TEST_CE_KERNEL_READ_PASSES="2"),
        capture_output=True, text=True)
    assert two.returncode == 0, two.stdout + two.stderr
    one = subprocess.run([str(binary)], cwd=ROOT, env=dict(env, KUI_TEST_CE_KERNEL_READ_PASSES="1"),
        capture_output=True, text=True)
    assert one.returncode != 0 and "launch.kernel_read_calls==passes" in one.stderr, one.stdout + one.stderr
    print("PASS old-source control: real preparation counts two full kernel reads across four profiles/raw/cooked; the one-read regression rejects that behavior")
