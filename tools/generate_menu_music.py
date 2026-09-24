#!/usr/bin/env python3
"""Generate the independently authored original K-UI PCM loops."""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT / 'resources/music')
    args = parser.parse_args()
    source = ROOT / 'resources/music/original_generator.py'
    spec = importlib.util.spec_from_file_location('original_kui_music', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for index, name in enumerate(module.TRACKS):
        pcm = module.compose() if index == 0 else module.synthwave(index - 1)
        module.write_track(args.directory / name, pcm)

if __name__ == '__main__':
    main()
