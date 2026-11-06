#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test generated Standard cohort admission, including a gate-off fallback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools')]
from native_standard_manifest import generate, ALIAS


def audit(build, allow_dirty=False):
    build = Path(build).resolve()
    header, manifest = generate(build, allow_dirty)
    inputs = ('tests/test_native_standard_manifest.c', 'src/core/native_vmu_standard.c',
              'src/core/vmu_game_card.c', 'src/core/vmu_persist_profile.c',
              'src/core/runtime_image.c', 'src/core/retail_image.c', 'src/core/hash.c', 'src/core/data.c')
    outputs = []
    with tempfile.TemporaryDirectory(prefix='kui-standard-manifest-') as temporary:
        work = Path(temporary)
        include = work / 'standard-manifest.h'
        include.write_text(header)
        for enabled in (False, True):
            binary = work / ('enabled' if enabled else 'historical-only')
            flags = [*shlex.split(os.environ.get('HOST_CC', 'cc')), '-std=c11', '-O1', '-g',
                     '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fsanitize=address,undefined',
                     '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                     '-Iinclude', '-I.deps/fatfs/source']
            if enabled:
                flags.append('-DKUI_NATIVE_STANDARD_MANIFEST="' + str(include) + '"')
            subprocess.run([*flags, *inputs, '-o', str(binary)], cwd=ROOT, check=True)
            output = subprocess.check_output([str(binary), str(build / ALIAS)], cwd=ROOT, text=True,
                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
            outputs.append(dict(manifest_enabled=enabled, output=output.strip()))
    return dict(schema='kui.native.standard-build-manifest.test.v1', status='PASS',
        manifest=manifest, sanitizer_runs=outputs, current_module_only=True,
        no_installed_module_self_authorization=True,
        source_sha256={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            for name in (*inputs, 'include/kui/native_vmu_standard.h', 'tools/native_standard_manifest.py',
                         'tests/test_native_standard_manifest.py')})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--allow-dirty', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = audit(args.build, args.allow_dirty)
    rendered = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end='')
