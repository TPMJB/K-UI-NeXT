#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production-loop, timing, report and launch-route checks for the suite."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    for command in (
        ['python3', 'tools/test_fmv_controls.py'],
        ['python3', 'tools/test_toy_demand_controls.py'],
        ['python3', 'tests/test_retail_demand_diagnostic.py', '-v'],
        ['python3', 'tests/test_vmu_native_perf_capture.py'],
        ['python3', 'tests/test_games_retail_perf_route.py'],
        ['python3', 'tools/run_native_standalone_demand.py', '--structural',
         '--output', 'build/performance-evidence/standalone-demand'],
        ['python3', 'tests/test_performance_test_plan.py'],
    ):
        print('RUN ' + ' '.join(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
