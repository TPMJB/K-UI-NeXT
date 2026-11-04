#!/usr/bin/env python3
"""Exercise actual terminal export/binding and streamed sink for both Toy profiles."""
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/toy-export-host';out.mkdir(parents=True,exist_ok=True)
for profile in (1,2):
    exe=out/str(profile)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-pie','-no-pie','-Iinclude','-Isrc/loader',
        '-DKUI_TOY_TEXT_REPORT=1',f'-DKUI_TOY_DECISION_PROFILE={profile}',
        '-DKUI_TOY_PILOT_DATA_PIPELINE_PROFILE=2','-DKUI_TOY_PILOT_DATA_SCHEDULE_MODE=2',
        '-DKUI_TOY_PILOT_DATA_BUDGET_PROFILE=4',f'-DKUI_BUILD_ID="d2026110403{profile}"',
        'tests/test_toy_report_export.c','src/loader/toy_report_binding.c',
        'src/loader/toy_report_text.c','src/core/report_file.c','-o',str(exe)],cwd=ROOT,check=True)
    subprocess.run([str(exe)],cwd=ROOT,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'},check=True)
